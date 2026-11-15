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

#define VIDDEC_FIFO_SIZE 32

typedef struct ViddecBackend {
    VideoDecoder dec;
    bool created;
    bool eos_sent;
    bool eos_done;
    int codec;              /* ViddecCodec value  */
    int tiling_block;       /* 1: block-linear (AV1), 0: pitch-linear */
    int64_t ts_fifo[VIDDEC_FIFO_SIZE];
    int ts_key[VIDDEC_FIFO_SIZE];
    int ts_head, ts_count;

    AVFrame **rq;
    int64_t *rq_in_us; /* arrival time per slot, for the emit timeout */
    int rq_len, rq_cap;

    int64_t last_out_pts;
    int have_out;

    int in_flight;

    int64_t eagain_since;

    uint64_t n_submit, n_emit, n_eagain, n_empty, n_eos;
    int rq_max;

    uint8_t *conv_buf;
    size_t conv_size;
} ViddecBackend;

#define VIDDEC_REORDER_DEPTH 4
#define VIDDEC_EMIT_TIMEOUT_US 150000

int viddec_create(ViddecBackend *b, AVCodecContext *avctx, int codec,
                  int tiling_block, const void *codec_config, size_t config_size);

int viddec_submit(ViddecBackend *b, const uint8_t *data, size_t size,
                  int64_t pts, int is_key, int eos);

int viddec_poll(ViddecBackend *b, AVCodecContext *avctx);

int viddec_emit_one(ViddecBackend *b, AVFrame *out);

void viddec_flush(ViddecBackend *b);
void viddec_close(ViddecBackend *b);

int viddec_drained(ViddecBackend *b);

int viddec_h264_config_from_avcc(const uint8_t *extradata, size_t extradata_size,
                                 uint8_t **out, size_t *out_size);
int viddec_h264_config_from_annexb(const uint8_t *data, size_t size,
                                   uint8_t **out, size_t *out_size);
int viddec_avcc_to_annexb(const uint8_t *src, size_t src_size,
                           uint8_t **dst, size_t *dst_cap);
int viddec_is_annexb(const uint8_t *data, size_t size);
