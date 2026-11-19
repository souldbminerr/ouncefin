/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Souldbminer
 * Drako3d hardware decoder for Nintendo Switch 2
 */

#include "viddec.h"

#include <string.h>

#include "libavutil/frame.h"
#include "libavutil/imgutils.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "decode.h"

static int viddec_g_active_codec = -1;
static unsigned long long viddec_g_frames_out;

int viddec_active_codec(void)
{
    return viddec_g_active_codec;
}

unsigned long long viddec_frames_out(void)
{
    return viddec_g_frames_out;
}

#ifdef __SWITCH2__
#include <switch/result.h>
#include <switch2/video/decoder.h>
#endif

#ifdef __SWITCH2__

int viddec_create(ViddecBackend *b, AVCodecContext *avctx, int codec,
                  int tiling_block, const void *codec_config, size_t config_size)
{
    VideoDecoderConfig cfg = { 0 };
    Result rc;

    b->codec = codec;
    b->tiling_block = tiling_block;
    b->ts_head = 0;
    b->ts_count = 0;
    b->ready = NULL;
    b->eos_sent = false;
    b->eos_done = false;

    cfg.codec = (ViddecCodec)codec;
    cfg.width = avctx->coded_width > 0 ? avctx->coded_width : avctx->width;
    cfg.height = avctx->coded_height > 0 ? avctx->coded_height : avctx->height;
    if (cfg.width <= 0 || cfg.height <= 0) {
        av_log(avctx, AV_LOG_ERROR, "viddec: invalid dimensions %dx%d\n",
               cfg.width, cfg.height);
        return AVERROR(EINVAL);
    }
    cfg.image_count = 8; /* VIDDEC_MAX_IMAGES */
    cfg.frame_rate = 0;  /* auto from pixel budget */
    cfg.tiling = tiling_block ? ViddecTilingMode_BlockLinear
                              : ViddecTilingMode_PitchLinear;
    /* Good values to avoid stutter and other issues */
    cfg.color_policy = (cfg.width >= 1920 || cfg.height >= 1080) ? 3 : 1;
    cfg.max_sample_size = cfg.height >= 2160 ? (4u << 20) : (1u << 20);
    cfg.codec_config = codec_config;
    cfg.codec_config_size = config_size;

    rc = videoDecoderCreate(&b->dec, &cfg);
    if (R_FAILED(rc)) {
        av_log(avctx, AV_LOG_ERROR, "viddec: videoDecoderCreate failed: 0x%x\n",
               (unsigned)rc);
        return AVERROR_EXTERNAL;
    }
    b->created = true;
    viddec_g_active_codec = codec;
    /* New stream: restart the frame count for this engine. */
    viddec_g_frames_out = 0;
    b->n_getframe_errs = 0;
    av_log(avctx, AV_LOG_INFO, "viddec: created codec=%d %dx%d %s\n",
           codec, cfg.width, cfg.height,
           tiling_block ? "block-linear" : "pitch-linear");
    return 0;
}

int64_t viddec_pts_to_ns(AVCodecContext *avctx, int64_t pts)
{
    AVRational tb = avctx->pkt_timebase;
    if (tb.num <= 0 || tb.den <= 0)
        tb = (AVRational){ 1, AV_TIME_BASE };
    if (pts == AV_NOPTS_VALUE)
        return 0;
    return av_rescale_q(pts, tb, (AVRational){ 1, 1000000000 });
}

int64_t viddec_ns_to_pts(AVCodecContext *avctx, int64_t ns)
{
    AVRational tb = avctx->pkt_timebase;
    if (tb.num <= 0 || tb.den <= 0)
        tb = (AVRational){ 1, AV_TIME_BASE };
    return av_rescale_q(ns, (AVRational){ 1, 1000000000 }, tb);
}

int viddec_submit(ViddecBackend *b, const uint8_t *data, size_t size,
                  int64_t pts_ns, int64_t pts, int64_t dts, int is_key, int eos)
{
    Result rc;

    if (!b->created)
        return AVERROR_EXTERNAL;

    if (b->eos_done)
        return AVERROR_EOF;

    if (!eos) {
        ViddecTsEntry e = { pts_ns, pts, dts, is_key };
        if (b->ts_count < VIDDEC_FIFO_SIZE) {
            int idx = (b->ts_head + b->ts_count) % VIDDEC_FIFO_SIZE;
            b->ts_fifo[idx] = e;
            b->ts_count++;
        } else {
            b->ts_fifo[b->ts_head] = e;
            b->ts_head = (b->ts_head + 1) % VIDDEC_FIFO_SIZE;
        }
    }

    rc = videoDecoderSubmit(&b->dec, data, size, (s64)pts_ns,
                            eos ? true : false, 0);
    if (R_FAILED(rc)) {
        if (rc == KERNELRESULT(TimedOut)) {
            if (!eos && b->ts_count > 0) {
                b->ts_count--;
            }
            b->n_eagain++;
            if (!b->eagain_since)
                b->eagain_since = av_gettime();
            else if (av_gettime() - b->eagain_since > 5000000) {
                b->eagain_since = 0;
                return AVERROR_EXTERNAL;
            }
            return AVERROR(EAGAIN);
        }
        return AVERROR_EXTERNAL;
    }
    b->eagain_since = 0;
    if (eos) {
        b->eos_sent = true;
        b->n_eos++;
    } else {
        b->n_submit++;
        b->in_flight++;
    }
    return 0;
}

static int viddec_match_entry(ViddecBackend *b, int64_t echo_ns, ViddecTsEntry *out)
{
    int best = -1;
    int64_t best_d = 1001; /* tolerance: service truncates ns->us */
    for (int i = 0; i < b->ts_count; i++) {
        int idx = (b->ts_head + i) % VIDDEC_FIFO_SIZE;
        int64_t d = b->ts_fifo[idx].ns - echo_ns;
        if (d < 0)
            d = -d;
        if (d <= 1000 && d < best_d) {
            best_d = d;
            best = i;
        }
    }
    if (best < 0)
        return 0;
    {
        int idx = (b->ts_head + best) % VIDDEC_FIFO_SIZE;
        *out = b->ts_fifo[idx];
        /* Remove by shifting tail. */
        for (int j = best; j < b->ts_count - 1; j++) {
            int a = (b->ts_head + j) % VIDDEC_FIFO_SIZE;
            int c = (b->ts_head + j + 1) % VIDDEC_FIFO_SIZE;
            b->ts_fifo[a] = b->ts_fifo[c];
        }
        b->ts_count--;
        return 1;
    }
}

static int viddec_fetch(ViddecBackend *b, AVCodecContext *avctx, AVFrame **out)
{
    VideoFrame vf;
    Result rc;
    AVFrame *frame;
    int ret;
    uint32_t copy_w, copy_h;
    ViddecTsEntry ts = { 0, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0 };
    int have_ts = 0;

    *out = NULL;
    rc = videoDecoderGetFrame(&b->dec, &vf, 0);
    if (R_FAILED(rc)) {
        if (rc == KERNELRESULT(TimedOut))
            return 0; /* No frame ready yet */
        if (b->in_flight > 0)
            b->in_flight--;
        b->n_getframe_errs++;
        av_log(avctx, b->n_getframe_errs >= VIDDEC_MAX_GETFRAME_ERRORS
                          ? AV_LOG_ERROR : AV_LOG_VERBOSE,
               "viddec: GetFrame failed: 0x%x (consecutive %d)\n",
               (unsigned)rc, b->n_getframe_errs);
        if (b->n_getframe_errs >= VIDDEC_MAX_GETFRAME_ERRORS)
            return AVERROR_EXTERNAL;
        return 0;
    }
    b->n_getframe_errs = 0;

    if (b->in_flight > 0)
        b->in_flight--;
    b->eagain_since = 0;

    have_ts = viddec_match_entry(b, (int64_t)vf.timestamp_ns, &ts);

    frame = av_frame_alloc();
    if (!frame) {
        videoDecoderReleaseFrame(&b->dec, &vf);
        return AVERROR(ENOMEM);
    }
    if (have_ts) {
        frame->pts = ts.pts;
        frame->pkt_dts = ts.pts;
    } else if (vf.end_of_stream) {
        frame->pts = viddec_ns_to_pts(avctx, (int64_t)vf.timestamp_ns);
        frame->pkt_dts = frame->pts;
    } else {
        videoDecoderReleaseFrame(&b->dec, &vf);
        av_frame_free(&frame);
        b->n_empty++;
        return 0;
    }
    if (vf.end_of_stream)
        b->eos_done = true;

    /* Pictures may be padded, so this is needed */
    frame->width = avctx->width;
    frame->height = avctx->height;
    frame->format = AV_PIX_FMT_NV12;

    ret = ff_get_buffer(avctx, frame, 0);
    if (ret < 0) {
        videoDecoderReleaseFrame(&b->dec, &vf);
        av_frame_free(&frame);
        return ret;
    }

    copy_w = vf.width < (uint32_t)frame->width ? vf.width
                                               : (uint32_t)frame->width;
    copy_h = vf.height < (uint32_t)frame->height ? vf.height
                                                 : (uint32_t)frame->height;
    if (vf.pitches[0] < copy_w || vf.pitches[1] < copy_w || copy_w == 0 ||
        copy_h == 0) {
        av_log(avctx, AV_LOG_ERROR, "viddec: bad geometry %ux%u pitch %u/%u\n",
               vf.width, vf.height, vf.pitches[0], vf.pitches[1]);
        videoDecoderReleaseFrame(&b->dec, &vf);
        av_frame_unref(frame);
        av_frame_free(&frame);
        return AVERROR_EXTERNAL;
    }
    if (b->tiling_block || vf.planes[0] == NULL) {
        rc = videoDecoderCopyFrame(&b->dec, &vf, frame->data[0],
                                   frame->linesize[0], frame->data[1],
                                   frame->linesize[1]);
        if (R_FAILED(rc)) {
            av_log(avctx, AV_LOG_ERROR, "viddec: CopyFrame failed: 0x%x\n",
                   (unsigned)rc);
            videoDecoderReleaseFrame(&b->dec, &vf);
            av_frame_unref(frame);
            av_frame_free(&frame);
            return AVERROR_EXTERNAL;
        }
    } else if (vf.pitches[0] == (uint32_t)frame->linesize[0] &&
               vf.pitches[1] == (uint32_t)frame->linesize[1] &&
               copy_w == (uint32_t)frame->width &&
               copy_h == (uint32_t)frame->height) {
        memcpy(frame->data[0], vf.planes[0],
               (size_t)frame->linesize[0] * frame->height);
        memcpy(frame->data[1], vf.planes[1],
               (size_t)frame->linesize[1] * frame->height / 2);
    } else {
        const uint8_t *src_y = vf.planes[0], *src_uv = vf.planes[1];
        uint32_t y;
        for (y = 0; y < copy_h; y++)
            memcpy(frame->data[0] + (size_t)y * frame->linesize[0],
                   src_y + (size_t)y * vf.pitches[0], copy_w);
        for (y = 0; y < copy_h / 2; y++)
            memcpy(frame->data[1] + (size_t)y * frame->linesize[1],
                   src_uv + (size_t)y * vf.pitches[1], copy_w);
    }
    if ((uint32_t)frame->height > copy_h) {
        uint32_t y;
        for (y = copy_h; y < (uint32_t)frame->height; y++)
            memset(frame->data[0] + (size_t)y * frame->linesize[0], 0,
                   frame->width);
        for (y = copy_h / 2; y < (uint32_t)frame->height / 2; y++)
            memset(frame->data[1] + (size_t)y * frame->linesize[1], 0x80,
                   frame->width);
    }

    if (have_ts ? ts.is_key : 0)
        frame->flags |= AV_FRAME_FLAG_KEY;
    frame->pict_type = (frame->flags & AV_FRAME_FLAG_KEY) ? AV_PICTURE_TYPE_I
                                                          : AV_PICTURE_TYPE_NONE;

    videoDecoderReleaseFrame(&b->dec, &vf);

    ret = ff_decode_frame_props(avctx, frame);
    if (ret < 0) {
        av_frame_unref(frame);
        av_frame_free(&frame);
        return ret;
    }
    *out = frame;
    return 1;
}

int viddec_poll(ViddecBackend *b, AVCodecContext *avctx)
{
    AVFrame *f = NULL;
    int ret;

    if (!b->created)
        return 0; /* Not initialized yet */
    if (b->ready)
        return 1; /* Already have one ready (single slot, service ordered) */

    ret = viddec_fetch(b, avctx, &f);
    if (ret < 0)
        return ret;
    if (ret == 0)
        return 0;
    b->ready = f;
    return 1;
}

int viddec_emit_one(ViddecBackend *b, AVFrame *out)
{
    AVFrame *f;
    if (!b->ready)
        return 0;
    f = b->ready;
    b->ready = NULL;
    av_frame_move_ref(out, f);
    av_frame_free(&f);
    b->n_emit++;
    viddec_g_frames_out++;
    return 1;
}

int viddec_drained(ViddecBackend *b)
{
    return b->eos_done && !b->ready;
}

void viddec_flush(ViddecBackend *b)
{
    if (!b->created)
        return;
    videoDecoderFlush(&b->dec);
    av_frame_free(&b->ready);
    b->ready = NULL;
    b->in_flight = 0;
    b->eagain_since = 0;
    b->n_getframe_errs = 0;
    b->ts_head = 0;
    b->ts_count = 0;
    b->eos_sent = false;
    b->eos_done = false;
}

void viddec_close(ViddecBackend *b)
{
    if (b->created) {
        videoDecoderClose(&b->dec);
        b->created = false;
        viddec_g_active_codec = -1;
    }
    av_frame_free(&b->ready);
    b->ready = NULL;
    b->in_flight = 0;
    av_freep(&b->conv_buf);
    b->conv_size = 0;
    b->ts_head = 0;
    b->ts_count = 0;
}

/* ---- H.264 helpers (S1-style robust NAL handling) ---- */

int viddec_is_annexb(const uint8_t *data, size_t size)
{
    if (size >= 4 && data[0] == 0 && data[1] == 0) {
        if (data[2] == 1)
            return 1;
        if (size >= 4 && data[2] == 0 && data[3] == 1)
            return 1;
    }
    return 0;
}

/* avcC extradata -> Annex-B SPS+PPS (00 00 00 01 + sps + 00 00 00 01 + pps). */
int viddec_h264_config_from_avcc(const uint8_t *extradata, size_t extradata_size,
                                 uint8_t **out, size_t *out_size)
{
    const uint8_t *p;
    int num_sps, num_pps, i;
    size_t need = 0;
    uint8_t *dst;
    static const uint8_t sc[4] = { 0, 0, 0, 1 };

    if (extradata_size < 8 || extradata[0] != 1)
        return AVERROR_INVALIDDATA;
    p = extradata + 5; /* skip version/profile/compat/level/lengthsize */
    num_sps = *p & 31;
    p++;
    for (i = 0; i < num_sps; i++) {
        uint16_t n;
        if (p + 2 > extradata + extradata_size)
            return AVERROR_INVALIDDATA;
        n = (uint16_t)(p[0] << 8 | p[1]);
        p += 2;
        if (p + n > extradata + extradata_size)
            return AVERROR_INVALIDDATA;
        need += 4 + n;
        p += n;
    }
    if (p + 1 > extradata + extradata_size)
        return AVERROR_INVALIDDATA;
    num_pps = *p++;
    for (i = 0; i < num_pps; i++) {
        uint16_t n;
        if (p + 2 > extradata + extradata_size)
            return AVERROR_INVALIDDATA;
        n = (uint16_t)(p[0] << 8 | p[1]);
        p += 2;
        if (p + n > extradata + extradata_size)
            return AVERROR_INVALIDDATA;
        need += 4 + n;
        p += n;
    }
    if (need == 0)
        return AVERROR_INVALIDDATA;

    dst = av_malloc(need);
    if (!dst)
        return AVERROR(ENOMEM);

    /* Second pass: copy. */
    p = extradata + 5;
    num_sps = *p & 31;
    p++;
    *out_size = 0;
    for (i = 0; i < num_sps; i++) {
        uint16_t n = (uint16_t)(p[0] << 8 | p[1]);
        p += 2;
        memcpy(dst + *out_size, sc, 4);
        *out_size += 4;
        memcpy(dst + *out_size, p, n);
        *out_size += n;
        p += n;
    }
    num_pps = *p++;
    for (i = 0; i < num_pps; i++) {
        uint16_t n = (uint16_t)(p[0] << 8 | p[1]);
        p += 2;
        memcpy(dst + *out_size, sc, 4);
        *out_size += 4;
        memcpy(dst + *out_size, p, n);
        *out_size += n;
        p += n;
    }
    *out = dst;
    return 0;
}

/* Scan Annex-B for first SPS (nal 7) + PPS (nal 8) */
int viddec_h264_config_from_annexb(const uint8_t *data, size_t size,
                                   uint8_t **out, size_t *out_size)
{
    const uint8_t *sps = NULL, *pps = NULL;
    size_t sps_len = 0, pps_len = 0;
    uint8_t *dst;
    static const uint8_t sc[4] = { 0, 0, 0, 1 };
    size_t i = 0;
    /* Find all NAL boundaries (S1 parser splits NALs; we scan robustly). */
    size_t caps[64][2];
    int nals = 0;

    while (i + 3 < size && nals < 64) {
        size_t sc_len = 0;
        if (data[i] == 0 && data[i + 1] == 0) {
            if (data[i + 2] == 1)
                sc_len = 3;
            else if (i + 3 < size && data[i + 2] == 0 && data[i + 3] == 1)
                sc_len = 4;
        }
        if (sc_len) {
            size_t start = i + sc_len;
            size_t end = start;
            while (end + 2 < size) {
                if (data[end] == 0 && data[end + 1] == 0 &&
                    (data[end + 2] == 1 ||
                     (end + 3 < size && data[end + 2] == 0 &&
                      data[end + 3] == 1)))
                    break;
                end++;
            }
            if (start < size && end > start) {
                caps[nals][0] = i;       /* include start code */
                caps[nals][1] = end;     /* NAL end */
                nals++;
            }
            i = end;
        } else {
            i++;
        }
    }

    for (i = 0; (size_t)i < (size_t)nals; i++) {
        const uint8_t *nal = data + caps[i][0];
        size_t len = caps[i][1] - caps[i][0];
        size_t hdr = (nal[2] == 1) ? 3 : 4;
        int type;
        if (len <= hdr)
            continue;
        type = nal[hdr] & 31;
        if (type == 7 && !sps) {
            sps = nal;
            sps_len = len;
        } else if (type == 8 && !pps) {
            pps = nal;
            pps_len = len;
        }
        if (sps && pps)
            break;
    }
    if (!sps || !pps)
        return AVERROR_INVALIDDATA;
    dst = av_malloc(sps_len + pps_len + 8);
    if (!dst)
        return AVERROR(ENOMEM);
    *out_size = 0;
    if (sps[2] == 1) { /* 3-byte start code: normalize to 4-byte */
        memcpy(dst, sc, 4);
        *out_size += 4;
        memcpy(dst + *out_size, sps + 3, sps_len - 3);
        *out_size += sps_len - 3;
    } else {
        memcpy(dst, sps, sps_len);
        *out_size += sps_len;
    }
    if (pps[2] == 1) {
        memcpy(dst + *out_size, sc, 4);
        *out_size += 4;
        memcpy(dst + *out_size, pps + 3, pps_len - 3);
        *out_size += pps_len - 3;
    } else {
        memcpy(dst + *out_size, pps, pps_len);
        *out_size += pps_len;
    }
    *out = dst;
    return 0;
}

/* Extract first SPS/PPS pointers (without copying) for change detection. */
int viddec_h264_extract_sps_pps(const uint8_t *data, size_t size,
                                const uint8_t **out_sps, size_t *out_sps_len,
                                const uint8_t **out_pps, size_t *out_pps_len)
{
    size_t i = 0;
    const uint8_t *sps = NULL, *pps = NULL;
    size_t sps_len = 0, pps_len = 0;
    size_t caps[64][2];
    int nals = 0;

    *out_sps = NULL; *out_sps_len = 0;
    *out_pps = NULL; *out_pps_len = 0;

    while (i + 3 < size && nals < 64) {
        size_t sc_len = 0;
        if (data[i] == 0 && data[i + 1] == 0) {
            if (data[i + 2] == 1)
                sc_len = 3;
            else if (i + 3 < size && data[i + 2] == 0 && data[i + 3] == 1)
                sc_len = 4;
        }
        if (sc_len) {
            size_t start = i + sc_len;
            size_t end = start;
            while (end + 2 < size) {
                if (data[end] == 0 && data[end + 1] == 0 &&
                    (data[end + 2] == 1 ||
                     (end + 3 < size && data[end + 2] == 0 &&
                      data[end + 3] == 1)))
                    break;
                end++;
            }
            if (start < size && end > start) {
                caps[nals][0] = i;
                caps[nals][1] = end;
                nals++;
            }
            i = end;
        } else {
            i++;
        }
    }
    for (i = 0; (size_t)i < (size_t)nals; i++) {
        const uint8_t *nal = data + caps[i][0];
        size_t len = caps[i][1] - caps[i][0];
        size_t hdr = (nal[2] == 1) ? 3 : 4;
        int type;
        if (len <= hdr)
            continue;
        type = nal[hdr] & 31;
        if (type == 7 && !sps) {
            /* strip start code for comparison */
            sps = nal + hdr;
            sps_len = len - hdr;
        } else if (type == 8 && !pps) {
            pps = nal + hdr;
            pps_len = len - hdr;
        }
        if (sps && pps)
            break;
    }
    if (!sps || !pps)
        return AVERROR_INVALIDDATA;
    *out_sps = sps; *out_sps_len = sps_len;
    *out_pps = pps; *out_pps_len = pps_len;
    return 0;
}

int viddec_avcc_to_annexb(const uint8_t *src, size_t src_size,
                           uint8_t **dst, size_t *dst_cap, int nal_len_size)
{
    size_t need = 0, out = 0;
    const uint8_t *p = src;
    const uint8_t *end = src + src_size;
    uint8_t *buf = *dst;
    static const uint8_t sc[4] = { 0, 0, 0, 1 };

    if (nal_len_size != 1 && nal_len_size != 2 && nal_len_size != 4)
        return AVERROR_INVALIDDATA;

    while (p + nal_len_size <= end) {
        uint32_t n = 0;
        for (int k = 0; k < nal_len_size; k++)
            n = (n << 8) | p[k];
        p += nal_len_size;
        if (n == 0) {
            /* Skip empty NAL (robustness, S1 parser would skip). */
            continue;
        }
        if (p + n > end)
            return AVERROR_INVALIDDATA;
        need += 4 + n;
        p += n;
    }
    if (p != end)
        return AVERROR_INVALIDDATA;
    if (need == 0)
        return AVERROR_INVALIDDATA;

    if (need > *dst_cap) {
        av_freep(dst);
        buf = av_malloc(need);
        if (!buf)
            return AVERROR(ENOMEM);
        *dst = buf;
        *dst_cap = need;
    }
    p = src;
    while (p + nal_len_size <= end) {
        uint32_t n = 0;
        for (int k = 0; k < nal_len_size; k++)
            n = (n << 8) | p[k];
        p += nal_len_size;
        if (n == 0)
            continue;
        memcpy(buf + out, sc, 4);
        out += 4;
        memcpy(buf + out, p, n);
        out += n;
        p += n;
    }
    return (int)out;
}

#else /* !__SWITCH2__ */

/* Stubs. */
int viddec_create(ViddecBackend *b, AVCodecContext *avctx, int codec,
                  int tiling_block, const void *codec_config, size_t config_size)
{
    (void)b; (void)avctx; (void)codec; (void)tiling_block;
    (void)codec_config; (void)config_size;
    return AVERROR(ENOSYS);
}
int viddec_submit(ViddecBackend *b, const uint8_t *data, size_t size,
                  int64_t pts_ns, int64_t pts, int64_t dts, int is_key, int eos)
{
    (void)b; (void)data; (void)size; (void)pts_ns; (void)pts; (void)dts;
    (void)is_key; (void)eos;
    return AVERROR(ENOSYS);
}
int viddec_poll(ViddecBackend *b, AVCodecContext *avctx)
{
    (void)b; (void)avctx;
    return AVERROR(ENOSYS);
}
int viddec_emit_one(ViddecBackend *b, AVFrame *out)
{
    (void)b; (void)out;
    return 0;
}
void viddec_flush(ViddecBackend *b) { (void)b; }
void viddec_close(ViddecBackend *b) { (void)b; }
int viddec_drained(ViddecBackend *b) { (void)b; return 1; }
int viddec_h264_config_from_avcc(const uint8_t *e, size_t n, uint8_t **o, size_t *s)
{
    (void)e; (void)n; (void)o; (void)s;
    return AVERROR(ENOSYS);
}
int viddec_h264_config_from_annexb(const uint8_t *d, size_t n, uint8_t **o, size_t *s)
{
    (void)d; (void)n; (void)o; (void)s;
    return AVERROR(ENOSYS);
}
int viddec_avcc_to_annexb(const uint8_t *s, size_t n, uint8_t **d, size_t *c, int l)
{
    (void)s; (void)n; (void)d; (void)c; (void)l;
    return AVERROR(ENOSYS);
}
int viddec_is_annexb(const uint8_t *d, size_t n)
{
    (void)d; (void)n;
    return 0;
}
int viddec_h264_extract_sps_pps(const uint8_t *d, size_t n,
                                const uint8_t **s, size_t *sl,
                                const uint8_t **p, size_t *pl)
{
    (void)d; (void)n; (void)s; (void)sl; (void)p; (void)pl;
    return AVERROR(ENOSYS);
}

#endif /* __SWITCH2__ */
