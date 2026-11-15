/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Souldbminer
 * Drako3d hardware decoder for Nintendo Switch 2
 */

#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"
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
} ViddecH264Context;

static int viddec_h264_init(AVCodecContext *avctx)
{
    ViddecH264Context *ctx = avctx->priv_data;

    memset(&ctx->be, 0, sizeof(ctx->be));
    ctx->pending = NULL;
    ctx->config = NULL;
    ctx->config_size = 0;
    ctx->config_ok = 0;

    /* Try extradata now */
    if (avctx->extradata && avctx->extradata_size > 0) {
        int ret;
        if (viddec_is_annexb(avctx->extradata, avctx->extradata_size))
            ret = viddec_h264_config_from_annexb(avctx->extradata,
                                                 avctx->extradata_size,
                                                 &ctx->config, &ctx->config_size);
        else
            ret = viddec_h264_config_from_avcc(avctx->extradata,
                                               avctx->extradata_size,
                                               &ctx->config, &ctx->config_size);
        if (ret >= 0)
            ctx->config_ok = 1;
        else
            av_log(avctx, AV_LOG_VERBOSE,  "viddec_h264: no SPS/PPS in extradata\n");
    }

    if (ctx->config_ok) {
#ifdef __SWITCH2__
        int ret = viddec_create(&ctx->be, avctx, VIDDEC_CODEC_ID, 0,
                                ctx->config, ctx->config_size);
        if (ret < 0) {
            av_freep(&ctx->config);
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

static int viddec_h264_submit_pending(AVCodecContext *avctx, ViddecH264Context *ctx)
{
    AVPacket *pkt = ctx->pending;
    const uint8_t *data = pkt->data;
    size_t size = pkt->size;
    int64_t pts = (pkt->pts != AV_NOPTS_VALUE) ? pkt->pts
                  : (pkt->dts != AV_NOPTS_VALUE) ? pkt->dts : 0;
    int is_key = !!(pkt->flags & AV_PKT_FLAG_KEY);
    int ret;

    if (!viddec_is_annexb(data, size)) {
        ret = viddec_avcc_to_annexb(data, size, &ctx->be.conv_buf,
                                    &ctx->be.conv_size);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "viddec_h264: avcC convert failed\n");
            return ret;
        }
        data = ctx->be.conv_buf;
        size = (size_t)ret;
    }

    ret = viddec_submit(&ctx->be, data, size, pts, is_key, 0);
    if (ret < 0)
        return ret;
    av_packet_free(&ctx->pending);
    return 0;
}

static int viddec_h264_receive_frame(AVCodecContext *avctx, AVFrame *frame)
{
    ViddecH264Context *ctx = avctx->priv_data;
    int ret;

    /* 1. Poll the service */
    ret = viddec_poll(&ctx->be, avctx);
    if (ret < 0)
        return ret;

    /* 2. Feed one packet if none are in flight */
    if (!ctx->pending) {
        AVPacket pkt = { 0 };
        ret = ff_decode_get_packet(avctx, &pkt);
        if (ret == AVERROR_EOF) {
            if (!ctx->config_ok) {
                /* Empty stream */
                return AVERROR_EOF;
            }
            if (!ctx->be.eos_sent) {
                ret = viddec_submit(&ctx->be, NULL, 0, 0, 0, 1);
                if (ret < 0 && ret != AVERROR(EAGAIN))
                    return ret;
            }
        } else if (ret < 0) {
            return ret;
        } else if (!pkt.size) {
            av_packet_unref(&pkt);
        } else {
            /* Create first real packet */
            if (!ctx->config_ok) {
                if (viddec_is_annexb(pkt.data, pkt.size))
                    ret = viddec_h264_config_from_annexb(pkt.data, pkt.size,
                                                         &ctx->config,
                                                         &ctx->config_size);
                else
                    ret = AVERROR_INVALIDDATA;
                if (ret < 0) {
                    av_packet_unref(&pkt);
                    av_log(avctx, AV_LOG_ERROR, "viddec_h264: no SPS/PPS in first packet\n");
                    return ret;
                }
                ctx->config_ok = 1;
#ifdef __SWITCH2__
                ret = viddec_create(&ctx->be, avctx, VIDDEC_CODEC_ID, 0,
                                    ctx->config, ctx->config_size);
                if (ret < 0) {
                    av_packet_unref(&pkt);
                    av_freep(&ctx->config);
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

    /* 3. Submit the in-flight packet */
    if (ctx->pending) {
        ret = viddec_h264_submit_pending(avctx, ctx);
        if (ret < 0 && ret != AVERROR(EAGAIN))
            return ret;
        if (!ret)
            av_packet_free(&ctx->pending);
    }

    /* 4. Emit one frame if due */
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
    viddec_flush(&ctx->be);
}

static int viddec_h264_close(AVCodecContext *avctx)
{
    ViddecH264Context *ctx = avctx->priv_data;
    av_packet_free(&ctx->pending);
    viddec_close(&ctx->be);
    av_freep(&ctx->config);
    ctx->config_size = 0;
    return 0;
}

const FFCodec ff_h264_viddec_decoder = {
    .p.name = "h264_viddec",
    CODEC_LONG_NAME("H.264 drako3d viddec"),
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
