/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * StratoHAL
 * Sound HAL for PS Vita
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

#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/clib.h>

#include <math.h>
#include <string.h>

#define SAMPLING_RATE	(48000)
/* TODO: Ogg Vorbis wave data is 44100 Hz but output is 48000 Hz.
 * This causes ~8.8% faster playback and higher pitch.
 * Proper fix: either resample or switch output to 44100 Hz
 * (grain=1024, BUF_FRAMES must be multiple of 1024). */
#define BUF_FRAMES	(960)	/* multiple of grain=96 for 48000Hz */
#define FRAME_SIZE	(4)
#define TMP_SAMPLES	(480)	/* multiple of grain=96 */

static SceUID audio_thread = -1;
static SceUID sound_mutex = -1;
static int audio_port = -1;
static volatile bool is_running;

static struct hal_wave *wave[HAL_SOUND_TRACKS];
static float volume[HAL_SOUND_TRACKS];
static float volume_pow[HAL_SOUND_TRACKS];
static bool finish[HAL_SOUND_TRACKS];

static uint32_t mix_buf[BUF_FRAMES];
static uint32_t tmp_buf[TMP_SAMPLES];

static void update_volume_curve(int stream);
static void scale_and_mix_samples(uint32_t *dst, const uint32_t *src, float vol, int samples);
static int audio_thread_main(SceSize args, void *argp);
static void lock_sound(void);
static void unlock_sound(void);

static void
lock_sound(void)
{
	if (sound_mutex >= 0)
		sceKernelLockMutex(sound_mutex, 1, NULL);
}

static void
unlock_sound(void)
{
	if (sound_mutex >= 0)
		sceKernelUnlockMutex(sound_mutex, 1);
}

static void
update_volume_curve(int stream)
{
	volume_pow[stream] = (powf(10.0f, volume[stream]) - 1.0f) / (10.0f - 1.0f);
}

static void
scale_and_mix_samples(uint32_t *dst, const uint32_t *src, float vol, int samples)
{
	int i;

	for (i = 0; i < samples; i++) {
		int16_t dl, dr;
		int16_t sl, sr;
		int32_t il, ir;

		dl = (int16_t)(uint16_t)dst[i];
		dr = (int16_t)(uint16_t)(dst[i] >> 16);
		sl = (int16_t)(uint16_t)src[i];
		sr = (int16_t)(uint16_t)(src[i] >> 16);

		il = (int32_t)dl + (int32_t)(sl * vol);
		ir = (int32_t)dr + (int32_t)(sr * vol);

		il = il > 32767 ? 32767 : il;
		il = il < -32768 ? -32768 : il;
		ir = ir > 32767 ? 32767 : ir;
		ir = ir < -32768 ? -32768 : ir;

		dst[i] = ((uint32_t)(uint16_t)(int16_t)il) |
			 (((uint32_t)(uint16_t)(int16_t)ir) << 16);
	}
}

static int
audio_thread_main(SceSize args, void *argp)
{
	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(argp);

	sceClibPrintf("[SND] Audio thread main loop entered.\n");

	while (is_running) {
		bool has_active_wave;
		int remain;
		int offset;
		int stream;

		memset(mix_buf, 0, sizeof(mix_buf));

		lock_sound();
		has_active_wave = false;
		for (stream = 0; stream < HAL_SOUND_TRACKS; stream++) {
			if (wave[stream] != NULL) {
				has_active_wave = true;
				break;
			}
		}

		if (!has_active_wave) {
			unlock_sound();
			sceKernelDelayThread(10 * 1000);
			continue;
		}

		remain = BUF_FRAMES;
		offset = 0;
		while (remain > 0) {
			int read_samples;

			read_samples = remain > TMP_SAMPLES ? TMP_SAMPLES : remain;
			for (stream = 0; stream < HAL_SOUND_TRACKS; stream++) {
				int ret;

				if (wave[stream] == NULL)
					continue;

				ret = hal_get_wave_samples(wave[stream], tmp_buf, read_samples);
				if (ret < read_samples) {
					memset(tmp_buf + ret,
					       0,
					       (size_t)(read_samples - ret) * sizeof(uint32_t));
					wave[stream] = NULL;
					finish[stream] = true;
				}

				scale_and_mix_samples(&mix_buf[offset],
						      tmp_buf,
						      volume_pow[stream],
						      read_samples);
			}

			offset += read_samples;
			remain -= read_samples;
		}
		unlock_sound();

		if (audio_port >= 0)
			sceAudioOutOutput(audio_port, mix_buf);
	}

	sceClibPrintf("[SND] Audio thread main loop exiting.\n");
	return sceKernelExitDeleteThread(0);
}

/*
 * Initialize sound.
 */
bool
init_sound(void)
{
	int i;

	sceClibPrintf("[SND] init_sound() ENTER\n");

	sound_mutex = sceKernelCreateMutex("suika3_sound_mutex",
					 SCE_KERNEL_MUTEX_ATTR_RECURSIVE, 0, NULL);
	if (sound_mutex < 0) {
		sceClibPrintf("[SND] sceKernelCreateMutex() FAILED: 0x%08X\n", sound_mutex);
		return false;
	}
	sceClibPrintf("[SND] Mutex created OK (id=0x%08X).\n", sound_mutex);

	audio_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN,
					 BUF_FRAMES,
					 SAMPLING_RATE,
					 SCE_AUDIO_OUT_MODE_STEREO);
	if (audio_port < 0) {
		sceClibPrintf("[SND] sceAudioOutOpenPort() FAILED: 0x%08X\n", audio_port);
		sceKernelDeleteMutex(sound_mutex);
		sound_mutex = -1;
		return false;
	}
	sceClibPrintf("[SND] Audio port opened OK (id=0x%08X), buf_frames=%d, rate=%d.\n",
		      audio_port, BUF_FRAMES, SAMPLING_RATE);

	for (i = 0; i < HAL_SOUND_TRACKS; i++) {
		wave[i] = NULL;
		volume[i] = 1.0f;
		finish[i] = false;
		update_volume_curve(i);
	}
	sceClibPrintf("[SND] Track state initialized (%d tracks).\n", HAL_SOUND_TRACKS);

	is_running = true;
	sceClibPrintf("[SND] Creating audio thread...\n");
	audio_thread = sceKernelCreateThread("suika3_audio",
					     audio_thread_main,
					     0x10000100 + 10,
					     0x4000,
					     0,
					     0,
					     NULL);
	if (audio_thread < 0) {
		sceClibPrintf("[SND] sceKernelCreateThread() FAILED: 0x%08X\n", audio_thread);
		is_running = false;
		sceAudioOutReleasePort(audio_port);
		audio_port = -1;
		sceKernelDeleteMutex(sound_mutex);
		sound_mutex = -1;
		return false;
	}
	sceClibPrintf("[SND] Audio thread created OK (id=0x%08X).\n", audio_thread);

	if (sceKernelStartThread(audio_thread, 0, NULL) < 0) {
		sceClibPrintf("[SND] sceKernelStartThread() FAILED!\n");
		is_running = false;
		sceKernelDeleteThread(audio_thread);
		audio_thread = -1;
		sceAudioOutReleasePort(audio_port);
		audio_port = -1;
		sceKernelDeleteMutex(sound_mutex);
		sound_mutex = -1;
		return false;
	}

	sceClibPrintf("[SND] Audio thread started OK, init_sound() returning true.\n");
	return true;
}

/*
 * Cleanup sound.
 */
void
cleanup_sound(void)
{
	sceClibPrintf("[SND] cleanup_sound() ENTER\n");

	if (audio_thread >= 0) {
		int status;

		sceClibPrintf("[SND] Stopping audio thread...\n");
		is_running = false;
		sceKernelWaitThreadEnd(audio_thread, &status, NULL);
		sceClibPrintf("[SND] Audio thread stopped (status=0x%08X).\n", status);
		audio_thread = -1;
	}

	if (audio_port >= 0) {
		sceClibPrintf("[SND] Releasing audio port...\n");
		sceAudioOutReleasePort(audio_port);
		audio_port = -1;
		sceClibPrintf("[SND] Audio port released.\n");
	}

	if (sound_mutex >= 0) {
		sceKernelDeleteMutex(sound_mutex);
		sound_mutex = -1;
		sceClibPrintf("[SND] Mutex deleted.\n");
	}

	sceClibPrintf("[SND] cleanup_sound() done.\n");
}

/*
 * Start sound playback on a stream.
 */
bool
hal_play_sound(
	int stream,
	struct hal_wave *w)
{
	lock_sound();
	wave[stream] = w;
	finish[stream] = false;
	unlock_sound();
	return true;
}

/*
 * Stop sound playback on a stream.
 */
bool
hal_stop_sound(
	int stream)
{
	lock_sound();
	wave[stream] = NULL;
	finish[stream] = true;
	unlock_sound();
	return true;
}

/*
 * Set a sound volume for a stream.
 */
bool
hal_set_sound_volume(
	int stream,
	float vol)
{
	lock_sound();
	volume[stream] = vol;
	update_volume_curve(stream);
	unlock_sound();
	return true;
}

/*
 * Check if a sound stream is finished.
 */
bool
hal_is_sound_finished(
	int stream)
{
	return finish[stream];
}
