/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * StratoHAL
 * Main code for PS Vita
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

/* Base */
#include "stratohal/platform.h"

/* HAL */
#include "vitamain.h"
#include "glrender.h"

/* Vita */
#include <vitaGL.h>
#include <psp2/apputil.h>
#include <psp2/kernel/threadmgr.h>

/* Standard C */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <time.h>
#include <assert.h>

/* POSIX */
#include <sys/time.h>

#include <sys/stat.h>
/*
 * Constants
 */

#define LOG_BUF_SIZE		(1024)
#define SCREEN_WIDTH		(960)
#define SCREEN_HEIGHT		(544)

/*
 * Variables
 */

static char *window_title;
static int screen_width;
static int screen_height;

/* Base path for resource resolution (set by suika3_run). */
const char *vita_base_path;

/*
 * Path buffer size.
 */
#define PATH_SIZE	(1024)

/*
 * PS Vita save data path (built at runtime from title_id parameter).
 * Commercial games use ux0:user/00/savedata/<TITLE_ID>/
 */
static char vita_savedata_dir[PATH_SIZE];

/*
 * External declarations (from other vita modules)
 */
extern bool init_file(void);
extern void cleanup_file(void);
extern bool init_sound(void);
extern void cleanup_sound(void);
extern void init_vitagamepad(void);
extern void update_vitagamepad(void);

// /*
//  * Engine entry point.
//  */
// bool
// suika3_run(const char *bp)
// {
// 	vita_base_path = bp;

// 	/* Init vitaGL with 8MB of VRAM for streaming textures. */
// 	vglInit(0x800000);

// 	/* Set window title for boot callback. */
// 	window_title = "suika3";

// 	/* Init the file HAL. */
// 	if (!init_file()) {
// 		hal_log_error("Failed to initialize file HAL.");
// 		return false;
// 	}

// 	/* Do a boot callback to acquire screen configuration. */
// 	screen_width = SCREEN_WIDTH;
// 	screen_height = SCREEN_HEIGHT;
// 	if (!hal_callback_on_event_boot(&window_title, &screen_width, &screen_height)) {
// 		hal_log_error("Initialization failed.");
// 		return false;
// 	}

// 	/* Init the graphics HAL. */
// 	if (!init_opengl(screen_width, screen_height)) {
// 		hal_log_error("Failed to initialize OpenGL.");
// 		return false;
// 	}

// 	/* Init the sound HAL. */
// 	if (!init_sound()) {
// 		hal_log_error("Failed to initialize sound.");
// 		return false;
// 	}

// 	/* Init the gamepad HAL. */
// 	init_vitagamepad();

// 	/* Do a start callback. */
// 	if (!hal_callback_on_event_start()) {
// 		hal_log_error("Failed to initialize event loop.");
// 		return false;
// 	}

// 	/* Main loop. */
// 	for (;;) {
// 		/* Update input. */
// 		update_vitagamepad();

// 		/* Start rendering. */
// 		opengl_start_rendering();

// 		/* Run a frame. */
// 		if (!hal_callback_on_event_frame())
// 			break;

// 		/* Finish rendering. */
// 		opengl_end_rendering();
// 		vglSwapBuffers(GL_FALSE);
// 	}

// 	/* Cleanup. */
// 	hal_callback_on_event_stop();
// 	cleanup_opengl();
// 	cleanup_sound();
// 	cleanup_file();
// 	/* vitaGL 无需显式清理 */

// 	return true;
// }
  static FILE *dbg = NULL;

  static void dbg_write(const char *msg)
  {
        if (dbg) {
                fprintf(dbg, "%s\n", msg);
                fflush(dbg);
        }
  }

  bool
  suika3_run(const char *bp, const char *title_id)
  {
        char test_path[PATH_SIZE];

        vita_base_path = bp;

        /* Build savedata path from runtime title_id. */
        snprintf(vita_savedata_dir, PATH_SIZE, "ux0:user/00/savedata/%s",
                 title_id ? title_id : "SUIKA0001");

        /* Make sure save data directory exists for debug log. */
        mkdir(vita_savedata_dir, 0755);

        snprintf(test_path, PATH_SIZE, "%s/debug.log", vita_savedata_dir);
        dbg = fopen(test_path, "w");

        dbg_write("[suika3] Step 1: vglInit...");
        vglInit(0x800000);
		vglWaitVblankStart(GL_TRUE);
        dbg_write("[suika3] Step 1: vglInit done");

        window_title = "suika3";

        /* Test path resolution */
        char *real = hal_make_real_path("start.novel");
        snprintf(test_path, PATH_SIZE, "%s", real);
        if (dbg) { fprintf(dbg, "[suika3] path test: start.novel -> %s\n", test_path); fflush(dbg); }
        free(real);

        dbg_write("[suika3] Step 2: init_file...");
        if (!init_file()) {
                dbg_write("[suika3] FAIL at init_file");
                if (dbg) fclose(dbg);
                return false;
        }
        dbg_write("[suika3] Step 2: init_file done");

        /* Check if start.novel exists */
        if (dbg) {
                fprintf(dbg, "[suika3] check: start.novel exists = %d\n",
                        hal_check_file_exist("start.novel"));
                fflush(dbg);
        }

        screen_width = SCREEN_WIDTH;
        screen_height = SCREEN_HEIGHT;

        dbg_write("[suika3] Step 3: boot callback...");
        if (!hal_callback_on_event_boot(&window_title, &screen_width, &screen_height)) {
                dbg_write("[suika3] FAIL at boot callback");
                if (dbg) fclose(dbg);
                return false;
        }
        dbg_write("[suika3] Step 3: boot callback done");

        dbg_write("[suika3] Step 4: init_opengl...");
        if (!init_opengl(screen_width, screen_height)) {
                dbg_write("[suika3] FAIL at init_opengl");
                if (dbg) fclose(dbg);
                return false;
        }
        dbg_write("[suika3] Step 4: init_opengl done");

        dbg_write("[suika3] Step 5: init_sound...");
        if (!init_sound()) {
                dbg_write("[suika3] FAIL at init_sound");
                if (dbg) fclose(dbg);
                return false;
        }
        dbg_write("[suika3] Step 5: init_sound done");

        dbg_write("[suika3] Step 6: init_vitagamepad...");
        init_vitagamepad();
        dbg_write("[suika3] Step 6: init_vitagamepad done");

        dbg_write("[suika3] Step 7: start callback...");
        if (!hal_callback_on_event_start()) {
                dbg_write("[suika3] FAIL at start callback");
                cleanup_sound();
                cleanup_opengl();
                cleanup_file();
                if (dbg) fclose(dbg);
                return false;
        }
        dbg_write("[suika3] Step 7: start callback done");

        dbg_write("[suika3] Step 8: entering main loop");
          int fc = 0;
          for (;;) {
                  fc++;
                  if (dbg) { fprintf(dbg, "A%d", fc); fflush(dbg); }
                  update_vitagamepad();

                  if (dbg) { fprintf(dbg, "B%d", fc); fflush(dbg); }
                  opengl_start_rendering();

                  if (dbg) { fprintf(dbg, "C%d", fc); fflush(dbg); }
                  if (!hal_callback_on_event_frame()) {
                      if (dbg) { fprintf(dbg, "X%d", fc); fflush(dbg); }
                      break;
                  }

                  if (dbg) { fprintf(dbg, "D%d", fc); fflush(dbg); }
                  vglSwapBuffers(GL_FALSE);

                  if (dbg) { fprintf(dbg, "E%d\n", fc); fflush(dbg); }
          }

          dbg_write("[suika3] exiting main loop");
          hal_callback_on_event_stop();
          cleanup_sound();
          cleanup_opengl();
          cleanup_file();
          if (dbg) fclose(dbg);
  }

/*
 * HAL
 */

bool
hal_log_info(
	const char *s,
	...)
{
	char buf[LOG_BUF_SIZE];
	va_list ap;

	va_start(ap, s);
	vsnprintf(buf, sizeof(buf), s, ap);
	printf("[INFO] %s\n", buf);
	va_end(ap);
	return true;
}

bool
hal_log_warn(
	const char *s,
	...)
{
	char buf[LOG_BUF_SIZE];
	va_list ap;

	va_start(ap, s);
	vsnprintf(buf, sizeof(buf), s, ap);
	printf("[WARN] %s\n", buf);
	va_end(ap);
	return true;
}

 bool
  hal_log_error(
        const char *s,
        ...)
  {
        char buf[LOG_BUF_SIZE];
        va_list ap;

        va_start(ap, s);
        vsnprintf(buf, sizeof(buf), s, ap);
        printf("[ERROR] %s\n", buf);
        va_end(ap);

        /* Also write to debug log */
        char err_path[PATH_SIZE];
        snprintf(err_path, PATH_SIZE, "%s/debug.log", vita_savedata_dir);
        FILE *d = fopen(err_path, "a");
        if (d) {
                fprintf(d, "[ERROR] %s\n", buf);
                fclose(d);
        }

        return true;
  }

// bool
// hal_log_error(
// 	const char *s,
// 	...)
// {
// 	char buf[LOG_BUF_SIZE];
// 	va_list ap;

// 	va_start(ap, s);
// 	vsnprintf(buf, sizeof(buf), s, ap);
// 	printf("[ERROR] %s\n", buf);
// 	va_end(ap);
// 	return true;
// }

bool
hal_log_out_of_memory(void)
{
	hal_log_error("Out of memory.");
	return true;
}

void
hal_notify_image_update(
	struct hal_image *img)
{
	opengl_notify_image_update(img);
}

void
hal_notify_image_free(
	struct hal_image *img)
{
	opengl_notify_image_free(img);
}

void
hal_render_image_normal(
	int dst_left,
	int dst_top,
	int dst_width,
	int dst_height,
	struct hal_image *src_image,
	int src_left,
	int src_top,
	int src_width,
	int src_height,
	int alpha)
{
	opengl_render_image_normal(dst_left,
				   dst_top,
				   dst_width,
				   dst_height,
				   src_image,
				   src_left,
				   src_top,
				   src_width,
				   src_height,
				   alpha);
}

void
hal_render_image_add(
	int dst_left,
	int dst_top,
	int dst_width,
	int dst_height,
	struct hal_image *src_image,
	int src_left,
	int src_top,
	int src_width,
	int src_height,
	int alpha)
{
	opengl_render_image_add(dst_left,
				dst_top,
				dst_width,
				dst_height,
				src_image,
				src_left,
				src_top,
				src_width,
				src_height,
				alpha);
}

void
hal_render_image_sub(
	int dst_left,
	int dst_top,
	int dst_width,
	int dst_height,
	struct hal_image *src_image,
	int src_left,
	int src_top,
	int src_width,
	int src_height,
	int alpha)
{
	opengl_render_image_sub(dst_left,
				dst_top,
				dst_width,
				dst_height,
				src_image,
				src_left,
				src_top,
				src_width,
				src_height,
				alpha);
}

void
hal_render_image_dim(
	int dst_left,
	int dst_top,
	int dst_width,
	int dst_height,
	struct hal_image *src_image,
	int src_left,
	int src_top,
	int src_width,
	int src_height,
	int alpha)
{
	opengl_render_image_dim(dst_left,
				dst_top,
				dst_width,
				dst_height,
				src_image,
				src_left,
				src_top,
				src_width,
				src_height,
				alpha);
}

void
hal_render_image_rule(
	struct hal_image *src_img,
	struct hal_image *rule_img,
	int threshold)
{
	opengl_render_image_rule(src_img, rule_img, threshold);
}

void
hal_render_image_melt(
	struct hal_image *src_img,
	struct hal_image *rule_img,
	int progress)
{
	opengl_render_image_melt(src_img, rule_img, progress);
}

void
hal_render_image_cross(
	struct hal_image *src1_img,
	struct hal_image *src2_img,
	float src1_left,
	float src1_top,
	float src2_left,
	float src2_top,
	int alpha)
{
	opengl_render_image_cross(src1_img,
				  src2_img,
				  src1_left,
				  src1_top,
				  src2_left,
				  src2_top,
				  alpha);
}

void
hal_render_image_3d_normal(
	float x1,
	float y1,
	float x2,
	float y2,
	float x3,
	float y3,
	float x4,
	float y4,
	struct hal_image *src_image,
	int src_left,
	int src_top,
	int src_width,
	int src_height,
	int alpha)
{
	opengl_render_image_3d_normal(x1,
				      y1,
				      x2,
				      y2,
				      x3,
				      y3,
				      x4,
				      y4,
				      src_image,
				      src_left,
				      src_top,
				      src_width,
				      src_height,
				      alpha);
}

void
hal_render_image_3d_add(
	float x1,
	float y1,
	float x2,
	float y2,
	float x3,
	float y3,
	float x4,
	float y4,
	struct hal_image *src_image,
	int src_left,
	int src_top,
	int src_width,
	int src_height,
	int alpha)
{
	opengl_render_image_3d_add(x1,
				   y1,
				   x2,
				   y2,
				   x3,
				   y3,
				   x4,
				   y4,
				   src_image,
				   src_left,
				   src_top,
				   src_width,
				   src_height,
				   alpha);
}

void
hal_render_image_3d_sub(
	float x1,
	float y1,
	float x2,
	float y2,
	float x3,
	float y3,
	float x4,
	float y4,
	struct hal_image *src_image,
	int src_left,
	int src_top,
	int src_width,
	int src_height,
	int alpha)
{
	opengl_render_image_3d_sub(x1,
				   y1,
				   x2,
				   y2,
				   x3,
				   y3,
				   x4,
				   y4,
				   src_image,
				   src_left,
				   src_top,
				   src_width,
				   src_height,
				   alpha);
}

void
hal_render_image_3d_dim(
	float x1,
	float y1,
	float x2,
	float y2,
	float x3,
	float y3,
	float x4,
	float y4,
	struct hal_image *src_image,
	int src_left,
	int src_top,
	int src_width,
	int src_height,
	int alpha)
{
	opengl_render_image_3d_dim(x1,
				   y1,
				   x2,
				   y2,
				   x3,
				   y3,
				   x4,
				   y4,
				   src_image,
				   src_left,
				   src_top,
				   src_width,
				   src_height,
				   alpha);
}

void
hal_render_image_3d_cross(
	struct hal_image *src1_img,
	struct hal_image *src2_img,
	float src1_x1,
	float src1_y1,
	float src1_x2,
	float src1_y2,
	float src1_x3,
	float src1_y3,
	float src1_x4,
	float src1_y4,
	float src2_x1,
	float src2_y1,
	float src2_x2,
	float src2_y2,
	float src2_x3,
	float src2_y3,
	float src2_x4,
	float src2_y4,
	int alpha)
{
	opengl_render_image_cross_3d(src1_img,
				     src2_img,
				     src1_x1,
				     src1_y1,
				     src1_x2,
				     src1_y2,
				     src1_x3,
				     src1_y3,
				     src1_x4,
				     src1_y4,
				     src2_x1,
				     src2_y1,
				     src2_x2,
				     src2_y2,
				     src2_x3,
				     src2_y3,
				     src2_x4,
				     src2_y4,
				     alpha);
}

bool
hal_make_save_directory(void)
{
	/* PS Vita saves go under ux0:user/00/savedata/<TITLE_ID>/ */
	mkdir("ux0:user/00/savedata", 0755);
	mkdir(vita_savedata_dir, 0755);
	return true;
}

/*
 * Convert a logical file name to a real filesystem path.
 *
 * Path resolution:
 *   "save/..." → "ux0:user/00/savedata/<TITLE_ID>/..."
 *   Others      → "app0:/..." (read-only VPK content)
 */
char *
hal_make_real_path(
	const char *fname)
{
	char *path;

	path = malloc(PATH_SIZE);
	if (path == NULL) {
		hal_log_out_of_memory();
		return NULL;
	}

	if (strncmp(fname, "save/", 5) == 0 ||
	    strncmp(fname, "save", 4) == 0) {
		snprintf(path, PATH_SIZE, "%s/%s", vita_savedata_dir, fname);
	} else if (fname[0] == '/' || strchr(fname, ':') != NULL) {
		snprintf(path, PATH_SIZE, "%s", fname);
	} else {
		snprintf(path, PATH_SIZE, "app0:/%s", fname);
	}

	return path;
}

char *
hal_make_valid_path(
	const char *dir,
	const char *fname)
{
	/* Not used on PS Vita. */
	assert(0);
	return NULL;
}

void
hal_reset_lap_timer(
	uint64_t *t)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);

	*t = (uint64_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

uint64_t
hal_get_lap_timer_millisec(
	uint64_t *t)
{
	struct timeval tv;
	uint64_t end;

	gettimeofday(&tv, NULL);

	end = (uint64_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);

	return (uint64_t)(end - *t);
}

bool
hal_play_video(
	const char *fname,
	bool is_skippable)
{
	UNUSED_PARAMETER(fname);
	UNUSED_PARAMETER(is_skippable);
	/* Video playback not supported on PS Vita. */
	return false;
}

void
hal_stop_video(void)
{
}

bool
hal_is_video_playing(void)
{
	return false;
}

void
hal_update_window_title(void)
{
}

bool
hal_is_full_screen_supported(void)
{
	/* PS Vita is always full screen. */
	return false;
}

bool
hal_is_full_screen_mode(void)
{
	return true;
}

void
hal_enter_full_screen_mode(void)
{
}

void
hal_leave_full_screen_mode(void)
{
}

const char *
hal_get_system_language(void)
{
	int lang;
	const char *lang_code;

	/* Read the system language setting. */
	lang = sceAppUtilSystemParamGetInt(0x01120000, NULL);

	switch (lang) {
	case 1:		lang_code = "ja";	break;
	case 2:		lang_code = "en-us";	break;
	case 3:		lang_code = "fr-fr";	break;
	case 4:		lang_code = "es-es";	break;
	case 5:		lang_code = "de";	break;
	case 6:		lang_code = "it";	break;
	case 10:	lang_code = "ko";	break;
	case 11:	lang_code = "zh-cn";	break;
	case 12:	lang_code = "zh-tw";	break;
	case 13:	lang_code = "en-gb";	break;
	case 16:	lang_code = "ru";	break;
	default:	lang_code = "en";	break;
	}

	return lang_code;
}

void
hal_speak_text(
	const char *text)
{
	UNUSED_PARAMETER(text);
}

void
hal_get_local_time(
	int *year,
	int *month,
	int *day,
	int *dow,
	int *hour,
	int *min,
	int *sec)
{
	time_t t;
	struct tm *tm_info;

	time(&t);
	tm_info = localtime(&t);

	*year = tm_info->tm_year + 1900;
	*month = tm_info->tm_mon + 1;
	*day = tm_info->tm_mday;
	*dow = tm_info->tm_wday;
	*hour = tm_info->tm_hour;
	*min = tm_info->tm_min;
	*sec = tm_info->tm_sec;
}
