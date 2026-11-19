/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Souldbminer
 * Drako3d hardware decoder for Nintendo Switch 2
 *
 * S1 port (averne envideo_h264.c): FFmpeg's H264 parser splits NALs, tracks
 * SPS/PPS/DPB/POC and reorder; only validated bitstreams reach NVDEC.
 * The T239 service parses internally, so we keep whole-AU submit but apply
 * the same discipline: nal_length_size from avcC (not hardcoded 4),
 * avcC-vs-AnnexB mode from extradata (not per-packet first-bytes guess),
 * in-band SPS/PPS change detection with decoder recreate, and exact
 * original PTS/DTS passthrough (no rescaled-echo derivation).
 * Streams without extradata mine SPS/PPS from the first AU (Annex-B or
 * avcC-converted), and a changed SPS updates avctx coded dimensions
 * before the recreate (viddec_h264_sps_dims).
 */

#include <string.h>

#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"
#include "get_bits.h"
#include "golomb.h"
#include "packet.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "viddec.h"

#ifdef __SWITCH2__
#include <switch2/video/decoder.h>
#define VIDDEC_CODEC_ID 0 /* ViddecCodec_H264 */
#endif

typedef struct ViddecH264Context {
    ViddecBackend be;
    AVPacket *pending; /* Unsubmitted packet kept across EAGAIN. */
    uint8_t *config;
    size_t config_size;
    int config_ok;
    int nal_len_size; /* 1/2/4 from avcC, 4 default */
    int is_avcc;      /* 1: avcC length-prefixed, 0: Annex-B, -1: unknown */
} ViddecH264Context;

static int viddec_h264_init(AVCodecContext *avctx)
{
    ViddecH264Context *ctx = avctx->priv_data;

    memset(&ctx->be, 0, sizeof(ctx->be));
    ctx->pending = NULL;
    ctx->config = NULL;
    ctx->config_size = 0;
    ctx->config_ok = 0;
    ctx->nal_len_size = 4;
    ctx->is_avcc = -1;

    /* Try extradata now (S1: parser reads SPS/PPS from extradata first). */
    if (avctx->extradata && avctx->extradata_size > 0) {
        int ret;
        if (viddec_is_annexb(avctx->extradata, avctx->extradata_size)) {
            ret = viddec_h264_config_from_annexb(avctx->extradata,
                                                 avctx->extradata_size,
                                                 &ctx->config, &ctx->config_size);
            if (ret >= 0) {
                ctx->config_ok = 1;
                ctx->nal_len_size = 4;
                ctx->is_avcc = 0;
            } else {
                av_freep(&ctx->config);
                av_log(avctx, AV_LOG_VERBOSE, "viddec_h264: no SPS/PPS in extradata\n");
            }
        } else {
            uint8_t *cfg = NULL;
            size_t sz = 0;
            ret = viddec_h264_config_from_avcc(avctx->extradata,
                                               avctx->extradata_size,
                                               &cfg, &sz);
            if (ret >= 0) {
                int l = 4;
                if (avctx->extradata_size >= 5)
                    l = (avctx->extradata[4] & 3) + 1;
                if (l != 1 && l != 2 && l != 4)
                    l = 4;
                ctx->config = cfg;
                ctx->config_size = sz;
                ctx->config_ok = 1;
                ctx->nal_len_size = l;
                ctx->is_avcc = 1;
            } else {
                av_log(avctx, AV_LOG_VERBOSE, "viddec_h264: no SPS/PPS in extradata\n");
            }
        }
        /* S1 would fail init on bad extradata; we defer to first packet. */
        if (ctx->config_ok && !ctx->config) {
            ctx->config_ok = 0;
        }
    }

    if (ctx->config_ok) {
#ifdef __SWITCH2__
        int ret = viddec_create(&ctx->be, avctx, VIDDEC_CODEC_ID, 0,
                                ctx->config, ctx->config_size);
        if (ret < 0) {
            av_freep(&ctx->config);
            ctx->config_ok = 0;
            return ret;
        }
#else
        return AVERROR(ENOSYS);
#endif
    }

    avctx->pix_fmt = AV_PIX_FMT_NV12;
    avctx->has_b_frames = 2;
    return 0;
}

/* Parse coded and visible dimensions from an SPS NAL (nal points at the
 * NAL header byte, sps_len includes it). Emulation prevention bytes are
 * stripped first; high-profile scaling lists are skipped. Deltas outside
 * +-255 are clamped (result-equivalent modulo 256) to keep malformed-data
 * reads overflow-safe. Returns 0 on success. */
static int viddec_h264_sps_dims(const uint8_t *nal, size_t sps_len,
                                int *coded_w, int *coded_h,
                                int *vis_w, int *vis_h)
{
    GetBitContext gb;
    uint8_t *buf;
    size_t n = 0, i = 0;
    unsigned profile, sps_id, chroma_idc = 1, poc_type, cycles;
    unsigned pw_mbs, ph_map, cl = 0, crr = 0, ct = 0, cbb = 0;
    int separate = 0, frame_mbs_only, subw, subh;
    int ret = AVERROR_INVALIDDATA;

    if (sps_len < 4 || (nal[0] & 0x1f) != 7)
        return AVERROR_INVALIDDATA;

    buf = av_malloc(sps_len + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!buf)
        return AVERROR(ENOMEM);
    while (i < sps_len) { /* strip emulation prevention bytes */
        if (i + 2 < sps_len && nal[i] == 0 && nal[i + 1] == 0 &&
            nal[i + 2] == 3) {
            buf[n++] = 0;
            buf[n++] = 0;
            i += 3;
        } else {
            buf[n++] = nal[i++];
        }
    }
    memset(buf + n, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    if (init_get_bits8(&gb, buf, (int)n) < 0)
        goto fail;

    skip_bits(&gb, 8);                 /* NAL header */
    profile = get_bits(&gb, 8);
    skip_bits(&gb, 16);                /* constraint flags + level_idc */
    sps_id = get_ue_golomb(&gb);
    if (sps_id > 31)
        goto fail;
    if (profile == 100 || profile == 110 || profile == 122 || profile == 244 ||
        profile == 44  || profile == 83  || profile == 86  || profile == 118 ||
        profile == 128 || profile == 138 || profile == 139 || profile == 134 ||
        profile == 135) {
        chroma_idc = get_ue_golomb(&gb);
        if (chroma_idc > 3)
            goto fail;
        if (chroma_idc == 3)
            separate = get_bits1(&gb);
        get_ue_golomb(&gb);            /* bit_depth_luma_minus8 */
        get_ue_golomb(&gb);            /* bit_depth_chroma_minus8 */
        skip_bits1(&gb);               /* qpprime_y_zero_transform_bypass */
        if (get_bits1(&gb)) {          /* seq_scaling_matrix_present_flag */
            int lists = chroma_idc == 3 ? 12 : 8;
            for (int k = 0; k < lists; k++) {
                if (get_bits1(&gb)) { /* seq_scaling_list_present_flag */
                    int sz = k < 6 ? 16 : 64;
                    int last = 8, next = 8;
                    for (int j = 0; j < sz; j++) {
                        if (next != 0) {
                            int d = get_se_golomb(&gb);
                            if (d < -255) d = -255;
                            if (d > 255) d = 255;
                            next = (last + d + 256) % 256;
                        }
                        if (next != 0)
                            last = next;
                    }
                }
            }
        }
    }
    get_ue_golomb(&gb);                /* log2_max_frame_num_minus4 */
    poc_type = get_ue_golomb(&gb);
    if (poc_type > 2)
        goto fail;
    if (poc_type == 0) {
        get_ue_golomb(&gb);            /* log2_max_pic_order_cnt_lsb_minus4 */
    } else if (poc_type == 1) {
        skip_bits1(&gb);               /* delta_pic_order_always_zero_flag */
        get_se_golomb(&gb);
        get_se_golomb(&gb);
        cycles = get_ue_golomb(&gb);
        if (cycles > 255)
            goto fail;
        for (unsigned k = 0; k < cycles; k++)
            get_se_golomb(&gb);
    }
    get_ue_golomb(&gb);                /* max_num_ref_frames */
    skip_bits1(&gb);                   /* gaps_in_frame_num_value_allowed */
    pw_mbs = get_ue_golomb(&gb);       /* pic_width_in_mbs_minus1 */
    ph_map = get_ue_golomb(&gb);       /* pic_height_in_map_units_minus1 */
    if (pw_mbs > 511 || ph_map > 511)
        goto fail;
    frame_mbs_only = get_bits1(&gb);
    if (!frame_mbs_only)
        skip_bits1(&gb);               /* mb_adaptive_frame_field_flag */
    skip_bits1(&gb);                   /* direct_8x8_inference_flag */
    if (get_bits1(&gb)) {              /* frame_cropping_flag */
        cl = get_ue_golomb(&gb);
        crr = get_ue_golomb(&gb);
        ct = get_ue_golomb(&gb);
        cbb = get_ue_golomb(&gb);
        if (cl > 4095 || crr > 4095 || ct > 4095 || cbb > 4095)
            goto fail;
    }
    if (get_bits_count(&gb) > (int)(n * 8))
        goto fail;

    *coded_w = (int)(pw_mbs + 1) * 16;
    *coded_h = (int)(ph_map + 1) * 16 * (2 - frame_mbs_only);
    subw = (chroma_idc == 1 || chroma_idc == 2) ? 2 : 1;
    subh = (chroma_idc == 1) ? 2 : 1;
    if (separate)
        subw = subh = 1;
    *vis_w = *coded_w - (int)(cl + crr) * subw;
    *vis_h = *coded_h - (int)(ct + cbb) * subh * (2 - frame_mbs_only);
    if (*coded_w <= 0 || *coded_h <= 0 || *vis_w <= 0 || *vis_h <= 0)
        goto fail;
    ret = 0;
fail:
    av_free(buf);
    return ret;
}

static int viddec_h264_check_reconfig(AVCodecContext *avctx, ViddecH264Context *ctx,
                                      const uint8_t *annexb, size_t size)
{
    const uint8_t *sps, *pps;
    size_t sps_len, pps_len;
    const uint8_t *csps, *cpps;
    size_t csps_len, cpps_len;
    int ret;

    if (!ctx->config_ok)
        return 0;
    if (viddec_h264_extract_sps_pps(annexb, size, &sps, &sps_len, &pps, &pps_len) < 0)
        return 0; /* no SPS/PPS in this AU, keep current */
    if (viddec_h264_extract_sps_pps(ctx->config, ctx->config_size,
                                    &csps, &csps_len, &cpps, &cpps_len) < 0)
        return 0;
    if (sps_len == csps_len && pps_len == cpps_len &&
        !memcmp(sps, csps, sps_len) && !memcmp(pps, cpps, pps_len))
        return 0;

    av_log(avctx, AV_LOG_INFO, "viddec_h264: SPS/PPS changed, recreating decoder\n");
    /* Size the recreated decoder from the new SPS: the service config
     * takes the largest coded size of the stream, so a resolution change
     * must not recreate with stale avctx dimensions. (viddec_fetch()
     * re-syncs avctx to the service-reported visible size afterwards.) */
    {
        int cw, ch, vw, vh;
        if (viddec_h264_sps_dims(sps, sps_len, &cw, &ch, &vw, &vh) == 0 &&
            (cw != avctx->coded_width || ch != avctx->coded_height)) {
            av_log(avctx, AV_LOG_INFO, "viddec_h264: SPS size %dx%d -> coded %dx%d\n",
                   avctx->coded_width, avctx->coded_height, cw, ch);
            ff_set_dimensions(avctx, cw, ch);
        }
    }
    viddec_close(&ctx->be);
    av_freep(&ctx->config);
    ctx->config = NULL;
    ctx->config_size = 0;
    /* Build new config from this AU (already Annex-B). */
    ret = viddec_h264_config_from_annexb(annexb, size, &ctx->config, &ctx->config_size);
    if (ret < 0) {
        ctx->config_ok = 0;
        av_log(avctx, AV_LOG_ERROR, "viddec_h264: new SPS/PPS invalid\n");
        return ret;
    }
    ctx->config_ok = 1;
#ifdef __SWITCH2__
    ret = viddec_create(&ctx->be, avctx, VIDDEC_CODEC_ID, 0,
                        ctx->config, ctx->config_size);
    if (ret < 0) {
        av_freep(&ctx->config);
        ctx->config_ok = 0;
        return ret;
    }
#else
    return AVERROR(ENOSYS);
#endif
    return 1;
}

static int viddec_h264_submit_pending(AVCodecContext *avctx, ViddecH264Context *ctx)
{
    AVPacket *pkt = ctx->pending;
    const uint8_t *data = pkt->data;
    size_t size = pkt->size;
    int64_t pts = pkt->pts;
    int64_t dts = pkt->dts;
    int64_t eff = (pts != AV_NOPTS_VALUE) ? pts
                  : (dts != AV_NOPTS_VALUE) ? dts : 0;
    int is_key = !!(pkt->flags & AV_PKT_FLAG_KEY);
    int ret;

    if (ctx->is_avcc != 0) {
        /* avcC mode (or unknown): validate as avcC when not Annex-B.
         * Unknown mode uses Annex-B detect; avcC length that looks like a
         * start code (00 00 00 01 = len 1) is still parsed as avcC because
         * mode is sticky from extradata. */
        int as_annexb = 0;
        if (ctx->is_avcc < 0)
            as_annexb = viddec_is_annexb(data, size);
        if (!as_annexb) {
            ret = viddec_avcc_to_annexb(data, size, &ctx->be.conv_buf,
                                        &ctx->be.conv_size, ctx->nal_len_size);
            if (ret < 0) {
                av_log(avctx, AV_LOG_ERROR, "viddec_h264: avcC convert failed\n");
                return ret;
            }
            data = ctx->be.conv_buf;
            size = (size_t)ret;
            if (ctx->is_avcc < 0)
                ctx->is_avcc = 1;
        } else if (ctx->is_avcc < 0) {
            ctx->is_avcc = 0;
        }
    }

    /* S1-style: detect SPS/PPS change before submit. */
    {
        int was_conv = (data == ctx->be.conv_buf);
        const uint8_t *orig_data = pkt->data;
        size_t orig_size = pkt->size;
        ret = viddec_h264_check_reconfig(avctx, ctx, data, size);
        if (ret < 0)
            return ret;
        if (ret > 0) {
            /* Backend was recreated (conv_buf freed). Re-establish data. */
            if (was_conv) {
                ret = viddec_avcc_to_annexb(orig_data, orig_size,
                                            &ctx->be.conv_buf,
                                            &ctx->be.conv_size,
                                            ctx->nal_len_size);
                if (ret < 0) {
                    av_log(avctx, AV_LOG_ERROR, "viddec_h264: avcC reconvert failed\n");
                    return ret;
                }
                data = ctx->be.conv_buf;
                size = (size_t)ret;
            } else {
                data = orig_data;
                size = orig_size;
            }
        }
    }

    ret = viddec_submit(&ctx->be, data, size, viddec_pts_to_ns(avctx, eff),
                        pts, dts, is_key, 0);
    if (ret < 0)
        return ret;
    return 0;
}

static int viddec_h264_receive_frame(AVCodecContext *avctx, AVFrame *frame)
{
    ViddecH264Context *ctx = avctx->priv_data;
    int ret;

    /* 1. Drain any ready frame first (service returns in order). */
    ret = viddec_poll(&ctx->be, avctx);
    if (ret < 0)
        return ret;

    /* 2. Feed one packet if none pending */
    if (!ctx->pending) {
        AVPacket pkt = { 0 };
        ret = ff_decode_get_packet(avctx, &pkt);
        if (ret == AVERROR_EOF) {
            if (!ctx->config_ok) {
                /* Empty stream */
                return AVERROR_EOF;
            }
            if (!ctx->be.eos_sent) {
                ret = viddec_submit(&ctx->be, NULL, 0, 0,
                                    AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0, 1);
                if (ret < 0 && ret != AVERROR(EAGAIN))
                    return ret;
            }
        } else if (ret < 0) {
            return ret;
        } else if (!pkt.size) {
            av_packet_unref(&pkt);
        } else {
            if (!ctx->config_ok) {
                if (viddec_is_annexb(pkt.data, pkt.size)) {
                    uint8_t *cfg = NULL;
                    size_t sz = 0;
                    ret = viddec_h264_config_from_annexb(pkt.data, pkt.size,
                                                         &cfg, &sz);
                    if (ret < 0) {
                        av_packet_unref(&pkt);
                        av_log(avctx, AV_LOG_ERROR, "viddec_h264: no SPS/PPS in first packet\n");
                        return ret;
                    }
                    ctx->config = cfg;
                    ctx->config_size = sz;
                    ctx->config_ok = 1;
                    ctx->nal_len_size = 4;
                    ctx->is_avcc = 0;
                } else {
                    /* avcC length-prefixed stream without avcC extradata:
                     * convert with default 4-byte lengths, then mine
                     * SPS/PPS from the converted AU (in-band parameter
                     * sets). The packet itself is kept and submitted. */
                    uint8_t *cfg = NULL;
                    size_t sz = 0;
                    int conv = viddec_avcc_to_annexb(pkt.data, pkt.size,
                                                     &ctx->be.conv_buf,
                                                     &ctx->be.conv_size, 4);
                    if (conv < 0) {
                        av_packet_unref(&pkt);
                        av_log(avctx, AV_LOG_ERROR,
                               "viddec_h264: first packet avcC convert failed\n");
                        return AVERROR_INVALIDDATA;
                    }
                    ret = viddec_h264_config_from_annexb(ctx->be.conv_buf,
                                                         (size_t)conv,
                                                         &cfg, &sz);
                    if (ret < 0) {
                        av_packet_unref(&pkt);
                        av_log(avctx, AV_LOG_ERROR,
                               "viddec_h264: no SPS/PPS in first packet\n");
                        return AVERROR_INVALIDDATA;
                    }
                    ctx->config = cfg;
                    ctx->config_size = sz;
                    ctx->config_ok = 1;
                    ctx->nal_len_size = 4;
                    ctx->is_avcc = 1;
                }
#ifdef __SWITCH2__
                ret = viddec_create(&ctx->be, avctx, VIDDEC_CODEC_ID, 0,
                                    ctx->config, ctx->config_size);
                if (ret < 0) {
                    av_packet_unref(&pkt);
                    av_freep(&ctx->config);
                    ctx->config_ok = 0;
                    return ret;
                }
#else
                av_packet_unref(&pkt);
                return AVERROR(ENOSYS);
#endif
            }
            ctx->pending = av_packet_alloc();
            if (!ctx->pending) {
                av_packet_unref(&pkt);
                return AVERROR(ENOMEM);
            }
            av_packet_move_ref(ctx->pending, &pkt);
        }
    }

    /* 3. Submit pending (keep across EAGAIN for backpressure). */
    if (ctx->pending) {
        ret = viddec_h264_submit_pending(avctx, ctx);
        if (ret < 0 && ret != AVERROR(EAGAIN))
            return ret;
        if (!ret) {
            av_packet_free(&ctx->pending);
            ctx->pending = NULL;
        }
    }

    /* 4. Emit if ready (always try, even after submit EAGAIN, to drain). */
    if (viddec_emit_one(&ctx->be, frame))
        return 0;
    if (viddec_drained(&ctx->be))
        return AVERROR_EOF;
    return AVERROR(EAGAIN);
}

static void viddec_h264_flush(AVCodecContext *avctx)
{
    ViddecH264Context *ctx = avctx->priv_data;
    av_packet_free(&ctx->pending);
    ctx->pending = NULL;
    viddec_flush(&ctx->be);
}

static int viddec_h264_close(AVCodecContext *avctx)
{
    ViddecH264Context *ctx = avctx->priv_data;
    av_packet_free(&ctx->pending);
    ctx->pending = NULL;
    viddec_close(&ctx->be);
    av_freep(&ctx->config);
    ctx->config_size = 0;
    ctx->config_ok = 0;
    return 0;
}

const FFCodec ff_h264_viddec_decoder = {
    .p.name = "h264_viddec",
    CODEC_LONG_NAME("H.264 NVIDIA T239 NVDEC"),
    .p.type = AVMEDIA_TYPE_VIDEO,
    .p.id = AV_CODEC_ID_H264,
    .priv_data_size = sizeof(ViddecH264Context),
    FF_CODEC_RECEIVE_FRAME_CB(viddec_h264_receive_frame),
    .p.capabilities = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_AVOID_PROBING,
    .caps_internal = FF_CODEC_CAP_SETS_FRAME_PROPS | FF_CODEC_CAP_NOT_INIT_THREADSAFE,
    .p.pix_fmts = (const enum AVPixelFormat[]){ AV_PIX_FMT_NV12,
                                                AV_PIX_FMT_NONE },
    .p.profiles = NULL,
    .flush = viddec_h264_flush,
    .close = viddec_h264_close,
    .init = viddec_h264_init,
};
