/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Souldbminer
 * Drako3d hardware decoder for Nintendo Switch 2
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "libavcodec/avcodec.h"
#include "libavutil/frame.h"
#include "libavutil/time.h"

#ifdef __SWITCH2__
#include <switch2/video/decoder.h>
#else
/* Stub for if libounce isnt used */
typedef struct { int dummy; } VideoDecoder;
typedef struct { int dummy; } VideoFrame;
#endif

#define VIDDEC_FIFO_SIZE 128
/* Consecutive failed images before the decoder is considered wedged. */
#define VIDDEC_MAX_GETFRAME_ERRORS 16

/* Packet timebase <-> the service's nanosecond timestamps (viddec.c). */
int64_t viddec_pts_to_ns(AVCodecContext *avctx, int64_t pts);
int64_t viddec_ns_to_pts(AVCodecContext *avctx, int64_t ns);

typedef struct ViddecTsEntry {
    int64_t ns;    /* submitted service timestamp */
    int64_t pts;   /* original packet pts (may be NOPTS) */
    int64_t dts;   /* original packet dts (may be NOPTS) */
    int is_key;
} ViddecTsEntry;

typedef struct ViddecBackend {
    VideoDecoder dec;
    bool created;
    bool eos_sent;
    bool eos_done;
    int codec;              /* ViddecCodec value  */
    int tiling_block;       /* 1: block-linear (AV1), 0: pitch-linear */

    /* Submit-order FIFO for timestamp/key mapping. Matched out of order
     * (service outputs in presentation order). */
    ViddecTsEntry ts_fifo[VIDDEC_FIFO_SIZE];
    int ts_head, ts_count;

    /* Single ready frame (service already returns images in order, so no
     * pts-sorted reorder queue like before). */
    AVFrame *ready;

    int in_flight;

    int64_t eagain_since;

    uint64_t n_submit, n_emit, n_eagain, n_empty, n_eos;
    int n_getframe_errs;    /* consecutive GetFrame errors (skipped images) */

    uint8_t *conv_buf;
    size_t conv_size;
} ViddecBackend;

int viddec_create(ViddecBackend *b, AVCodecContext *avctx, int codec,
                  int tiling_block, const void *codec_config, size_t config_size);

int viddec_submit(ViddecBackend *b, const uint8_t *data, size_t size,
                  int64_t pts_ns, int64_t pts, int64_t dts, int is_key, int eos);

int viddec_poll(ViddecBackend *b, AVCodecContext *avctx);

int viddec_emit_one(ViddecBackend *b, AVFrame *out);

void viddec_flush(ViddecBackend *b);
void viddec_close(ViddecBackend *b);

int viddec_drained(ViddecBackend *b);

/* Exported (C linkage) for frontend identification of the active HW
 * decoder engine: returns the ViddecCodec value in use, or -1 when no
 * viddec decoder is active. */
int viddec_active_codec(void);
unsigned long long viddec_frames_out(void);

int viddec_h264_config_from_avcc(const uint8_t *extradata, size_t extradata_size,
                                 uint8_t **out, size_t *out_size);
int viddec_h264_config_from_annexb(const uint8_t *data, size_t size,
                                   uint8_t **out, size_t *out_size);
/* Length-prefixed avcC packet -> Annex-B (4-byte start codes).
 * nal_len_size is 1, 2 or 4 (from avcC lengthSizeMinusOne). */
int viddec_avcc_to_annexb(const uint8_t *src, size_t src_size,
                           uint8_t **dst, size_t *dst_cap, int nal_len_size);
int viddec_is_annexb(const uint8_t *data, size_t size);
/* Extract first SPS/PPS from an Annex-B buffer (for change detection). */
int viddec_h264_extract_sps_pps(const uint8_t *data, size_t size,
                                const uint8_t **out_sps, size_t *out_sps_len,
                                const uint8_t **out_pps, size_t *out_pps_len);
