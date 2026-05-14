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

#include "stratohal/platform.h"

#include <psp2/ctrl.h>
#include <psp2/touch.h>

/*
 * Previous button state for edge detection.
 */
static uint32_t prev_buttons;

/*
 * Touch state.
 */
static int touch_start_x;
static int touch_start_y;
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
	prev_buttons = 0;
	is_touching = false;
	is_continuous_swipe_enabled = false;
}

/*
 * Map a PS Vita button to a HAL key code.
 */
static int
map_button_to_key(uint32_t button)
{
	switch (button) {
	case SCE_CTRL_CROSS:		return HAL_KEY_GAMEPAD_A;
	case SCE_CTRL_CIRCLE:		return HAL_KEY_GAMEPAD_B;
	case SCE_CTRL_TRIANGLE:		return HAL_KEY_GAMEPAD_Y;
	case SCE_CTRL_SQUARE:		return HAL_KEY_GAMEPAD_X;
	case SCE_CTRL_LTRIGGER:		return HAL_KEY_GAMEPAD_L;
	case SCE_CTRL_RTRIGGER:		return HAL_KEY_GAMEPAD_R;
	case SCE_CTRL_UP:		return HAL_KEY_GAMEPAD_UP;
	case SCE_CTRL_DOWN:		return HAL_KEY_GAMEPAD_DOWN;
	case SCE_CTRL_LEFT:		return HAL_KEY_GAMEPAD_LEFT;
	case SCE_CTRL_RIGHT:		return HAL_KEY_GAMEPAD_RIGHT;
	default:			return -1;
	}
}

/*
 * Process button state changes.
 */
static void
process_buttons(uint32_t buttons)
{
	static const uint32_t all_buttons[] = {
		SCE_CTRL_CROSS, SCE_CTRL_CIRCLE, SCE_CTRL_TRIANGLE, SCE_CTRL_SQUARE,
		SCE_CTRL_LTRIGGER, SCE_CTRL_RTRIGGER,
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
				hal_callback_on_event_key_press(key);
			else
				hal_callback_on_event_key_release(key);
		}
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
	hal_callback_on_event_analog_input(HAL_ANALOG_X1,
		(int)(pad->lx - 128) * 256);
	hal_callback_on_event_analog_input(HAL_ANALOG_Y1,
		(int)(pad->ly - 128) * 256);

	/* Right stick. */
	hal_callback_on_event_analog_input(HAL_ANALOG_X2,
		(int)(pad->rx - 128) * 256);
	hal_callback_on_event_analog_input(HAL_ANALOG_Y2,
		(int)(pad->ry - 128) * 256);
}

/*
 * Process front touch screen input.
 */
static void
process_touch(void)
{
	SceTouchData touch;
	int x, y;

	sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);

	if (touch.reportNum > 0) {
		/* Convert touch coordinates to screen coordinates.
		 * Touch panel: 1920×1088 → Screen: 960×544. */
		x = (int)touch.report[0].x * 960 / 1920;
		y = (int)touch.report[0].y * 544 / 1088;

		if (!is_touching) {
			/* Touch start. */
			touch_start_x = x;
			touch_start_y = y;
			touch_last_y = y;
			is_touching = true;
			hal_callback_on_event_mouse_press(HAL_MOUSE_LEFT, x, y);
		} else {
			/* Touch move. */
			int delta_y = y - touch_last_y;
			touch_last_y = y;

			if (is_continuous_swipe_enabled) {
				if (delta_y > 0 && delta_y < 30) {
					hal_callback_on_event_key_press(HAL_KEY_DOWN);
					hal_callback_on_event_key_release(HAL_KEY_DOWN);
				}
			}

			hal_callback_on_event_mouse_move(x, y);
		}
	} else if (is_touching) {
		/* Touch end. */
		int delta_y = touch_last_y - touch_start_y;
		const int FLICK_Y_DISTANCE = 50;
		const int TAP_DISTANCE = 10;

		if (delta_y > FLICK_Y_DISTANCE) {
			hal_callback_on_event_touch_cancel();
			hal_callback_on_event_swipe_down();
		} else if (delta_y < -FLICK_Y_DISTANCE) {
			hal_callback_on_event_touch_cancel();
			hal_callback_on_event_swipe_up();
		} else {
			hal_callback_on_event_mouse_release(HAL_MOUSE_LEFT,
				touch_last_y > 0 ? touch_start_x : 0,
				touch_last_y > 0 ? touch_start_y : 0);
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
