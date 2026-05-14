/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * StratoHAL
 * File HAL for PS Vita (Standard C I/O via VitaSDK newlib)
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
#include "vitamain.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/*
 * Forward declarations.
 */
static void ungetc_rfile(struct hal_rfile *rf, char c);

/*
 * Initialize the vitafile module.
 */
bool
init_file(void)
{
	return true;
}

/*
 * Cleanup the vitafile module.
 */
void
cleanup_file(void)
{
}

/*
 * Check if a file exists.
 */
bool
hal_check_file_exist(
	const char *file)
{
	FILE *fp;
	char *real_path;

	real_path = hal_make_real_path(file);
	if (real_path == NULL)
		return false;

	fp = fopen(real_path, "rb");
	free(real_path);

	if (fp == NULL)
		return false;

	fclose(fp);
	return true;
}

/*
 * Open a read file stream.
 */
bool
hal_open_rfile(
	const char *file,
	struct hal_rfile **f)
{
	struct hal_rfile *fs;
	char *real_path;

	/* Allocate a file struct. */
	fs = malloc(sizeof(struct hal_rfile));
	if (fs == NULL) {
		hal_log_out_of_memory();
		return false;
	}

	/* Make a real path. */
	real_path = hal_make_real_path(file);
	if (real_path == NULL) {
		free(fs);
		return false;
	}

	/* Open the file. */
	fs->fp = fopen(real_path, "rb");
	free(real_path);
	if (fs->fp == NULL) {
		free(fs);
		return false;
	}

	/* Get file size. */
	fseek(fs->fp, 0, SEEK_END);
	fs->size = (uint64_t)ftell(fs->fp);
	fseek(fs->fp, 0, SEEK_SET);
	fs->pos = 0;

	*f = fs;
	return true;
}

/*
 * Get a file size.
 */
bool
hal_get_rfile_size(
	struct hal_rfile *f,
	size_t *ret)
{
	*ret = (size_t)f->size;
	return true;
}

/*
 * Enable de-obfuscation on a read file stream.
 * (Not used on PS Vita; resources are plain files in VPK.)
 */
void
hal_decode_rfile(
	struct hal_rfile *f)
{
	UNUSED_PARAMETER(f);
}

/*
 * Read bytes from a read file stream.
 */
bool
hal_read_rfile(
	struct hal_rfile *f,
	void *buf,
	size_t size,
	size_t *ret)
{
	size_t len;

	assert(f != NULL);
	assert(f->fp != NULL);

	if (f->pos + size > f->size)
		size = (size_t)(f->size - f->pos);
	if (size == 0)
		return false;

	len = fread(buf, 1, size, f->fp);
	f->pos += len;
	*ret = len;

	if (len == 0)
		return false;

	return true;
}

/*
 * Read a line from a read file stream.
 */
bool
hal_gets_rfile(
	struct hal_rfile *f,
	char *buf,
	size_t size)
{
	char *ptr;
	size_t len, read_size;
	char c;

	assert(f != NULL);
	assert(f->fp != NULL);
	assert(buf != NULL);
	assert(size > 0);

	ptr = buf;
	for (len = 0; len < size - 1; len++) {
		if (!hal_read_rfile(f, &c, 1, &read_size)) {
			*ptr = '\0';
			if (len == 0)
				return false;
			return true;
		}
		if (c == '\n' || c == '\0') {
			*ptr = '\0';
			return true;
		}
		if (c == '\r') {
			if (!hal_read_rfile(f, &c, 1, &read_size)) {
				*ptr = '\0';
				return true;
			}
			if (c == '\n') {
				*ptr = '\0';
				return true;
			}
			ungetc_rfile(f, c);
			*ptr = '\0';
			return true;
		}
		*ptr++ = c;
	}
	*ptr = '\0';
	return true;
}

/* Push back a character. */
static void
ungetc_rfile(
	struct hal_rfile *f,
	char c)
{
	assert(f != NULL);
	assert(f->fp != NULL);
	assert(f->pos != 0);

	ungetc(c, f->fp);
	f->pos--;
}

/*
 * Rewind a read file stream.
 */
void
hal_rewind_rfile(
	struct hal_rfile *f)
{
	assert(f != NULL);
	assert(f->fp != NULL);

	rewind(f->fp);
	f->pos = 0;
}

/*
 * Close a read file stream.
 */
void
hal_close_rfile(
	struct hal_rfile *f)
{
	assert(f != NULL);
	assert(f->fp != NULL);

	fclose(f->fp);
	free(f);
}

/*
 * Open a write file stream.
 */
bool
hal_open_wfile(
	const char *file,
	struct hal_wfile **wf)
{
	char *real_path;

	/* Allocate wfile struct. */
	*wf = malloc(sizeof(struct hal_wfile));
	if (*wf == NULL) {
		hal_log_out_of_memory();
		return false;
	}

	/* Make a real path. */
	real_path = hal_make_real_path(file);
	if (real_path == NULL) {
		hal_log_out_of_memory();
		free(*wf);
		*wf = NULL;
		return false;
	}

	/* Open the file for writing. */
	(*wf)->fp = fopen(real_path, "wb");
	free(real_path);
	if ((*wf)->fp == NULL) {
		free(*wf);
		*wf = NULL;
		return false;
	}

	return true;
}

/*
 * Write bytes to a write file stream.
 */
bool
hal_write_wfile(
	struct hal_wfile *wf,
	const void *buf,
	size_t size,
	size_t *ret)
{
	size_t out;

	assert(wf != NULL);
	assert(wf->fp != NULL);

	out = fwrite(buf, 1, size, wf->fp);
	*ret = out;

	return out == size;
}

/*
 * Close a write file stream.
 */
void
hal_close_wfile(
	struct hal_wfile *wf)
{
	assert(wf != NULL);
	assert(wf->fp != NULL);

	fflush(wf->fp);
	fclose(wf->fp);
	free(wf);
}

/*
 * Remove a file.
 */
bool
hal_remove_file(
	const char *file)
{
	char *real_path;
	int result;

	real_path = hal_make_real_path(file);
	if (real_path == NULL)
		return false;

	result = remove(real_path);
	free(real_path);

	return result == 0;
}
