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
#define VIDDEC_CODEC_ID 1 /* ViddecCodec_Vp9 */
#endif

typedef struct {
    ViddecBackend be;
    AVPacket *pending; /* Unsubmitted packet kept across EAGAIN. */
    int inited;
} ViddecVp9Context;

static int viddec_vp9_init(AVCodecContext *avctx)
{
    ViddecVp9Context *ctx = avctx->priv_data;
    memset(&ctx->be, 0, sizeof(ctx->be));
    ctx->pending = NULL;
    ctx->inited = 0;
    avctx->pix_fmt = AV_PIX_FMT_NV12;
    avctx->has_b_frames = 1;
    return 0;
}

static int viddec_vp9_receive_frame(AVCodecContext *avctx, AVFrame *frame)
{
    ViddecVp9Context *ctx = avctx->priv_data;
    int ret;

    ret = viddec_poll(&ctx->be, avctx);
    if (ret < 0)
        return ret;

    if (!ctx->pending) {
            AVPacket pkt = { 0 };
            ret = ff_decode_get_packet(avctx, &pkt);
            if (ret == AVERROR_EOF) {
                if (ctx->inited && !ctx->be.eos_sent) {
                    ret = viddec_submit(&ctx->be, NULL, 0, 0, 0, 1);
                    if (ret == AVERROR(EAGAIN))
                        return AVERROR(EAGAIN);
                    if (ret < 0)
                        return ret;
                }
                if (!ctx->inited || viddec_drained(&ctx->be))
                    return AVERROR_EOF;
                return AVERROR(EAGAIN);
            }
            if (ret < 0)
                return ret;
            if (!pkt.size) {
                av_packet_unref(&pkt);
                return AVERROR(EAGAIN);
            }
            if (!ctx->inited) {
#ifdef __SWITCH2__
                ret = viddec_create(&ctx->be, avctx, VIDDEC_CODEC_ID, 0, NULL, 0);
                if (ret < 0) {
                    av_packet_unref(&pkt);
                    return ret;
                }
#else
                av_packet_unref(&pkt);
                return AVERROR(ENOSYS);
#endif
                ctx->inited = 1;
            }
            ctx->pending = av_packet_alloc();
            if (!ctx->pending) {
                av_packet_unref(&pkt);
                return AVERROR(ENOMEM);
            }
            av_packet_move_ref(ctx->pending, &pkt);
        }

        {
            AVPacket *pkt = ctx->pending;
            int64_t pts = (pkt->pts != AV_NOPTS_VALUE) ? pkt->pts
                          : (pkt->dts != AV_NOPTS_VALUE) ? pkt->dts : 0;
            ret = viddec_submit(&ctx->be, pkt->data, pkt->size, pts,
                                !!(pkt->flags & AV_PKT_FLAG_KEY), 0);
        }
        if (ret == AVERROR(EAGAIN))
            return AVERROR(EAGAIN);
        if (ret < 0)
            return ret;
        av_packet_free(&ctx->pending);
        if (viddec_emit_one(&ctx->be, frame))
            return 0;
        return AVERROR(EAGAIN);
}

static void viddec_vp9_flush(AVCodecContext *avctx)
{
    ViddecVp9Context *ctx = avctx->priv_data;
    av_packet_free(&ctx->pending);
    viddec_flush(&ctx->be);
}

static int viddec_vp9_close(AVCodecContext *avctx)
{
    ViddecVp9Context *ctx = avctx->priv_data;
    av_packet_free(&ctx->pending);
    viddec_close(&ctx->be);
    return 0;
}

const FFCodec ff_vp9_viddec_decoder = {
    .p.name = "vp9_viddec",
    CODEC_LONG_NAME("VP9 drako3d viddec"),
    .p.type = AVMEDIA_TYPE_VIDEO,
    .p.id = AV_CODEC_ID_VP9,
    .priv_data_size = sizeof(ViddecVp9Context),
    FF_CODEC_RECEIVE_FRAME_CB(viddec_vp9_receive_frame),
    .p.capabilities = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_AVOID_PROBING,
    .caps_internal = FF_CODEC_CAP_SETS_FRAME_PROPS | FF_CODEC_CAP_NOT_INIT_THREADSAFE,
    .p.pix_fmts = (const enum AVPixelFormat[]){ AV_PIX_FMT_NV12,
                                                AV_PIX_FMT_NONE },
    .flush = viddec_vp9_flush,
    .close = viddec_vp9_close,
    .init = viddec_vp9_init,
};
