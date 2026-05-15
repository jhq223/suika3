/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * StratoHAL
 * Gamepad HAL for PS Vita (sceCtrl + sceTouch)
 */

/*-
 * SPDX-License-Identifier: Zlib
 *
 * Copyright (c) 2025-2026 Awe Morris
 * Copyright (c) 1996-2024 Keiichi Tabata
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *      claim that you wrote the original software. If you use this software
 *      in a product, an acknowledgment in the product documentation would be
 *      appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *      misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

#include <strato/strato.h>
#include "callback.h"

#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <stdlib.h>

/*
 * Previous button state for edge detection.
 */
static uint32_t prev_buttons;

/*
 * Touch state.
 */
static int touch_start_x;
static int touch_start_y;
static int touch_last_x;
static int touch_last_y;
static bool is_touching;
static bool is_continuous_swipe_enabled;

/*
 * Initialize the gamepad.
 */
void
init_vitagamepad(void)
{
	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
	sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
	prev_buttons = 0;
	is_touching = false;
	is_continuous_swipe_enabled = false;
}

/*
 * Map a PS Vita button to a HAL key code.
 * Returns -1 for buttons handled separately.
 */
static int
map_button_to_key(uint32_t button)
{
	switch (button) {
	case SCE_CTRL_CROSS:		return HAL_KEY_GAMEPAD_B;
	case SCE_CTRL_CIRCLE:		return HAL_KEY_GAMEPAD_A;
	case SCE_CTRL_TRIANGLE:		return HAL_KEY_GAMEPAD_X;	/* system menu */
	case SCE_CTRL_UP:		return HAL_KEY_GAMEPAD_UP;
	case SCE_CTRL_DOWN:		return HAL_KEY_GAMEPAD_DOWN;
	case SCE_CTRL_LEFT:		return HAL_KEY_GAMEPAD_LEFT;
	case SCE_CTRL_RIGHT:		return HAL_KEY_GAMEPAD_RIGHT;
	default:			return -1;
	}
}

/*
 * Process button state changes.
 *
 * Mapping (Japanese VN convention):
 *   CIRCLE    → Confirm / Advance       (GAMEPAD_A → left click)
 *   CROSS     → Cancel / Hide UI        (GAMEPAD_B → right click)
 *   TRIANGLE  → System Menu             (GAMEPAD_X → Escape)
 *   SQUARE    → History / Backlog       (HAL_KEY_L)
 *   L         → (unused — engine has no Auto-mode key)
 *   R         → Skip toggle             (HAL_KEY_S)
 *   START     → System Menu             (HAL_KEY_ESCAPE)
 *   SELECT    → History (fallback)       (HAL_KEY_L)
 */
static void
process_buttons(uint32_t buttons)
{
	static const uint32_t all_buttons[] = {
		SCE_CTRL_CROSS, SCE_CTRL_CIRCLE, SCE_CTRL_TRIANGLE,
		SCE_CTRL_UP, SCE_CTRL_DOWN, SCE_CTRL_LEFT, SCE_CTRL_RIGHT,
	};
	uint32_t changed;
	int i, key;

	changed = buttons ^ prev_buttons;

	for (i = 0; i < (int)(sizeof(all_buttons) / sizeof(all_buttons[0])); i++) {
		if (changed & all_buttons[i]) {
			key = map_button_to_key(all_buttons[i]);
			if (key < 0)
				continue;

			if (buttons & all_buttons[i])
				hal_callback.on_key_press(key);
			else
				hal_callback.on_key_release(key);
		}
	}

	/* SQUARE → History / Backlog */
	if (changed & SCE_CTRL_SQUARE) {
		if (buttons & SCE_CTRL_SQUARE)
			hal_callback.on_key_press(HAL_KEY_L);
		else
			hal_callback.on_key_release(HAL_KEY_L);
	}

	/* R → Skip toggle */
	if (changed & SCE_CTRL_RTRIGGER) {
		if (buttons & SCE_CTRL_RTRIGGER)
			hal_callback.on_key_press(HAL_KEY_S);
		else
			hal_callback.on_key_release(HAL_KEY_S);
	}

	/* L → (no engine key for Auto mode, left unmapped) */
	(void)(changed & SCE_CTRL_LTRIGGER);

	/* START → System Menu (same as TRIANGLE, for traditional console players) */
	if (changed & SCE_CTRL_START) {
		if (buttons & SCE_CTRL_START)
			hal_callback.on_key_press(HAL_KEY_ESCAPE);
		else
			hal_callback.on_key_release(HAL_KEY_ESCAPE);
	}

	/* SELECT → History (engine has no quick-save key, best-effort fallback) */
	if (changed & SCE_CTRL_SELECT) {
		if (buttons & SCE_CTRL_SELECT)
			hal_callback.on_key_press(HAL_KEY_L);
		else
			hal_callback.on_key_release(HAL_KEY_L);
	}

	prev_buttons = buttons;
}

/*
 * Process analog stick input.
 */
static void
process_analog(const SceCtrlData *pad)
{
	/* Left stick: range 0-255, center at 128.
	 * Map to signed 16-bit range for HAL. */
	hal_callback.on_analog_input(HAL_ANALOG_X1,
		(int)(pad->lx - 128) * 256);
	hal_callback.on_analog_input(HAL_ANALOG_Y1,
		(int)(pad->ly - 128) * 256);

	/* Right stick. */
	hal_callback.on_analog_input(HAL_ANALOG_X2,
		(int)(pad->rx - 128) * 256);
	hal_callback.on_analog_input(HAL_ANALOG_Y2,
		(int)(pad->ry - 128) * 256);
}

/*
 * Process front touch screen input.
 *
 * Gestures:
 *   Tap              → Confirm / Advance  (left click)
 *   Swipe Up         → History / Backlog  (HAL_KEY_L)
 *   Swipe Down       → Hide UI            (right click)
 *   Swipe Right      → Skip               (HAL_KEY_S)
 *   Swipe Left       → Page Up            (best-effort for Auto)
 *   Two-finger Tap   → System Menu        (HAL_KEY_ESCAPE)
 */
static void
process_touch(void)
{
	SceTouchData touch;
	int x, y;
	const int FLICK_DISTANCE = 50;

	sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);

	if (touch.reportNum >= 2 && !is_touching) {
		/* Two-finger tap → System Menu */
		x = ((int)touch.report[0].x + (int)touch.report[1].x) / 2;
		y = ((int)touch.report[0].y + (int)touch.report[1].y) / 2;
		x = x * 960 / 1920;
		y = y * 544 / 1088;
		hal_callback.on_mouse_press(HAL_MOUSE_LEFT, x, y);
		hal_callback.on_mouse_release(HAL_MOUSE_LEFT, x, y);
		hal_callback.on_key_press(HAL_KEY_ESCAPE);
		hal_callback.on_key_release(HAL_KEY_ESCAPE);
		/* Drain remaining touch reports to prevent false single-tap. */
		sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
		return;
	}

	if (touch.reportNum == 1) {
		x = (int)touch.report[0].x * 960 / 1920;
		y = (int)touch.report[0].y * 544 / 1088;

		if (!is_touching) {
			touch_start_x = x;
			touch_start_y = y;
			touch_last_x = x;
			touch_last_y = y;
			is_touching = true;
			hal_callback.on_mouse_press(HAL_MOUSE_LEFT, x, y);
		} else {
			touch_last_x = x;
			touch_last_y = y;
			hal_callback.on_mouse_move(x, y);
		}
	} else if (is_touching) {
		/* Touch released — detect gesture from accumulated deltas. */
		int dx = touch_last_x - touch_start_x;
		int dy = touch_last_y - touch_start_y;

		if (dy > FLICK_DISTANCE && dy > abs(dx)) {
			/* Swipe Down → Hide UI (right click) */
			hal_callback.on_touch_cancel();
			hal_callback.on_mouse_press(HAL_MOUSE_RIGHT,
				touch_start_x, touch_start_y);
			hal_callback.on_mouse_release(HAL_MOUSE_RIGHT,
				touch_start_x, touch_start_y);
		} else if (dy < -FLICK_DISTANCE && -dy > abs(dx)) {
			/* Swipe Up → History */
			hal_callback.on_touch_cancel();
			hal_callback.on_key_press(HAL_KEY_L);
			hal_callback.on_key_release(HAL_KEY_L);
		} else if (dx > FLICK_DISTANCE && dx > abs(dy)) {
			/* Swipe Right → Skip */
			hal_callback.on_touch_cancel();
			hal_callback.on_key_press(HAL_KEY_S);
			hal_callback.on_key_release(HAL_KEY_S);
		} else if (dx < -FLICK_DISTANCE && -dx > abs(dy)) {
			/* Swipe Left → Page Up (best-effort, no Auto key in engine) */
			hal_callback.on_touch_cancel();
			hal_callback.on_key_press(HAL_KEY_PAGEUP);
			hal_callback.on_key_release(HAL_KEY_PAGEUP);
		} else {
			/* Tap → Confirm */
			hal_callback.on_mouse_release(HAL_MOUSE_LEFT,
				touch_start_x, touch_start_y);
		}

		is_touching = false;
	}
}

/*
 * Update gamepad state. Called once per frame.
 */
void
update_vitagamepad(void)
{
	SceCtrlData pad;

	sceCtrlPeekBufferPositive2(0, &pad, 1);

	process_buttons(pad.buttons);
	process_analog(&pad);
	process_touch();
}

/*
 * HAL callback for continuous swipe setting.
 */
void
hal_set_continuous_swipe_enabled(
	bool is_enabled)
{
	is_continuous_swipe_enabled = is_enabled;
}
