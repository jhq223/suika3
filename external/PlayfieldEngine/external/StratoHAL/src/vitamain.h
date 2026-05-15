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

#ifndef STRATOHAL_VITAMAIN_H
#define STRATOHAL_VITAMAIN_H

#include <strato/c89compat.h>
#include <stdio.h>

/* Path resolution */
char *hal_make_real_path(const char *fname);
char *hal_make_valid_path(const char *dir, const char *fname);

struct hal_rfile {
	FILE *fp;
	uint64_t size;
	uint64_t pos;
};

struct hal_wfile {
	FILE *fp;
};

bool suika3_run(const char *base_path, const char *title_id);

#endif
