#include <unistd.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include "libavutil/dict.h"
#include <libavutil/pixfmt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#ifdef RK_PLAT
#include "rockchip/rk_mpi.h"
#include "rockchip/mpp_buffer.h"
#include "rockchip/mpp_frame.h"
#include "rockchip/mpp_packet.h"
#endif

#include "mydecoder.h"

#define mydecoder_err(fmt, ...)  printf("[mydecoder]err: " fmt, ##__VA_ARGS__)
#define mydecoder_info(fmt, ...) printf("[mydecoder]info: "fmt, ##__VA_ARGS__)
#if DEBUG_LOG
#define mydecoder_dbg(fmt, ...)  printf("[mydecoder]dbg: " fmt, ##__VA_ARGS__)
#else
#define mydecoder_dbg(fmt, ...)
#endif

#ifdef RK_PLAT
int use_rkmpp = 0;
MppCtx mpp_ctx;
MppApi *mpi;
#define MAX_BUFFER_FRAMES    16
u8 *frames[MAX_BUFFER_FRAMES];
u32 w_idx;
u32 r_idx;
u32 width;
u32 height;

#define msleep(x)    usleep((x)*1000)
#define MPP_H264_DECODE_TIMEOUT    3
#endif

AVCodecContext *dec_ctx = NULL;
struct SwsContext *img_convert_ctx;
struct SwsContext *img_convert_ctx2;

MyPacket mydecoder_packet_alloc(void)
{
    MyPacket pkt;
    
    pkt = (MyPacket)av_packet_alloc();
    if (!pkt) {
        mydecoder_err("Error allocating packet\n");
    }
    return pkt;
}

MyFrame mydecoder_frame_alloc(void)
{
    MyFrame frame;
    
    frame = (MyFrame)av_frame_alloc();
    if (!frame) {
        mydecoder_err("Error allocating frame\n");
    }
    return frame;
}

MyContext mydecoder_context_alloc(void)
{
    MyContext ctx;
    ctx = (MyContext)avformat_alloc_context();
    if (!ctx) {
        mydecoder_err("Error allocating context\n");
    }
    return ctx;
}

s32 mydecoder_open_avcodec(AVFormatContext *fmt_ctx, const s8 *filename, 
    s8 *codec_name, s32 *frame_num)
{
    AVCodec *codec = NULL;
    s32 i;

    avformat_open_input(&fmt_ctx, filename, NULL, NULL);
    avformat_find_stream_info(fmt_ctx, NULL);

    /* find the video decoder: ie: h264_v4l2m2m */
    codec = avcodec_find_decoder_by_name(codec_name);
    if (!codec) {
        mydecoder_err("Codec not found codec\n");
        return -1;
    }
    
    dec_ctx = avcodec_alloc_context3(codec);
    if (!dec_ctx) {
        mydecoder_err("Could not allocate video codec context\n");
        exit(1);
    }

    for (i = 0; i < fmt_ctx->nb_streams; i++) {
        if (AVMEDIA_TYPE_VIDEO == fmt_ctx->streams[i]->codecpar->codec_type) {
            *frame_num = fmt_ctx->streams[i]->nb_frames;
            avcodec_parameters_to_context(dec_ctx, fmt_ctx->streams[i]->codecpar);
            //dec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
            //dec_ctx->coded_height = 1080;
            //dec_ctx->coded_width = 1920;
            mydecoder_info("dec_ctx w: %d    h: %d\n", dec_ctx->coded_width, dec_ctx->coded_height);
            /* open it */
            if (avcodec_open2(dec_ctx, codec, NULL) < 0) {
                mydecoder_err("Could not open codec\n");
                exit(1);
            }
            break;
        }
    }
    return 0;
}

#ifdef RK_PLAT
s32 mydecoder_open_rkmpp(AVFormatContext *fmt_ctx, const s8 *filename, s32 *frame_num)
{
    MPP_RET ret = MPP_OK;
    MpiCmd mpi_cmd = MPP_CMD_BASE;
    MppParam param = NULL;
    RK_U32 need_split = 1;
	AVDictionary *dict = NULL;
    s32 i;

    w_idx = 0;
    r_idx = 0;
    
	av_dict_set(&dict, "rtsp_transport", "tcp", 0);
    avformat_open_input(&fmt_ctx, filename, NULL, &dict);
    avformat_find_stream_info(fmt_ctx, NULL);
    
    for (i = 0; i < fmt_ctx->nb_streams; i++) {
        if (AVMEDIA_TYPE_VIDEO == fmt_ctx->streams[i]->codecpar->codec_type) {
            *frame_num = fmt_ctx->streams[i]->nb_frames;
            mydecoder_info("total frame: %d\n", *frame_num);
            break;
        }
    }
    
    ret = mpp_create(&mpp_ctx, &mpi);
    if (MPP_OK != ret) {
        mydecoder_err("mpi->control failed\n");
        exit(1);
    }

    mpi_cmd = MPP_DEC_SET_PARSER_SPLIT_MODE;
    param = &need_split;
    ret = mpi->control(mpp_ctx, mpi_cmd, param);
    if (MPP_OK != ret) {
        mydecoder_err("mpi->control failed\n");
        exit(1);
    }

    ret = mpp_init(mpp_ctx, MPP_CTX_DEC, MPP_VIDEO_CodingAVC);
    if (MPP_OK != ret) {
        mydecoder_err("mpp_init failed\n");
        exit(1);
    }

    return 0;
}
#endif

s32 mydecoder_open(MyContext *ctx, const s8 *file_name, s8 *codec_name, s32 *frame_num)
{
    AVCodec codec;
    AVFormatContext *fmt_ctx = (AVFormatContext *)(*ctx);

#ifdef RK_PLAT
    if (!strcmp(codec_name, "rkmpp")) {
        use_rkmpp = 1;
        mydecoder_open_rkmpp(fmt_ctx, file_name, frame_num);
    } else 
#endif
    mydecoder_open_avcodec(fmt_ctx, file_name, codec_name, frame_num);

    return 0;
}

s32 mydecoder_get_packet(MyContext ctx, MyPacket *packet, s32 *packet_size)
{
    s32 ret = 0;
    AVPacket *avpkt = (AVPacket *)(*packet);

    ret = av_read_frame((AVFormatContext *)ctx, avpkt);
    *packet_size = avpkt->size;
    
    return ret;
}

s32 mydecoder_decode_avcodec(AVFrame *frame, AVPacket *pkt, s32 *got_frame)
{
    int ret;

    *got_frame = 0;
    ret = avcodec_send_packet(dec_ctx, pkt);
    if (ret < 0) {
        mydecoder_err("Error sending a packet for decoding\n");
        return ret;
    }

    while (ret >= 0) {
        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return ret;
        else if (ret < 0) {
            mydecoder_err("Error during decoding\n");
            return ret;
        } else {
            *got_frame = 1;
            break;
        }
    }

    return 0;
}

#ifdef RK_PLAT
s32 mydecoder_fill_frame_mpp(MppFrame mpp_frame)
{
    u32 h_stride = mpp_frame_get_hor_stride(mpp_frame);
    u32 v_stride = mpp_frame_get_ver_stride(mpp_frame);
    u8 *src = mpp_buffer_get_ptr(mpp_frame_get_buffer(mpp_frame));
    u8 *dst, *base_y, *base_uv;
    s32 i;
    
    width = mpp_frame_get_width(mpp_frame);
    height = mpp_frame_get_height(mpp_frame);

    frames[w_idx] = (u8 *)malloc(width * height * 3 / 2);
    dst = frames[w_idx];
    base_y = src;
    base_uv = src + h_stride * v_stride;

    for (i = 0; i < height; i++, base_y += h_stride, dst += width)
        memcpy(dst, base_y, width);
    for (i = 0; i < height / 2; i++, base_uv += h_stride, dst += width)
        memcpy(dst, base_uv, width);

    mydecoder_dbg("write idx: %d\n", w_idx);

    w_idx++;
    if (w_idx >= MAX_BUFFER_FRAMES)
        w_idx = 0;
    
    if (r_idx == w_idx) {
        mydecoder_info("Busy! Discard current frame!\n");
        // Discard the oldest frame
        free(frames[r_idx]);
        r_idx++;
        if (r_idx >= MAX_BUFFER_FRAMES) 
            r_idx = 0;
    }

    return 0;
}

s32 mydecoder_decode_rkmpp(AVPacket *pkt, s32 *got_frame)
{
    int ret;
    unsigned int pkt_done = 0;
    MppPacket packet;
    MppFrame frame;
    MppBufferGroup frm_grp;
    
    *got_frame = 0;

    ret = mpp_packet_init(&packet, pkt->data, pkt->size);
    if (MPP_OK != ret) {
        mydecoder_err("mpp_packet_init failed\n");
        return ret;
    }

	mpp_packet_set_pts(packet, pkt->pts);
    if (!(pkt->data))
        mpp_packet_set_eos(packet);

        do {
            s32 times = 5;
            // send the packet first if packet is not done
            if (!pkt_done) {
                ret = mpi->decode_put_packet(mpp_ctx, packet);
                if (MPP_OK == ret)
                    pkt_done = 1;
            }

            // then get all available frame and release
            do {
                s32 get_frm = 0;
                u32 frm_eos = 0;
            try_again:
                ret = mpi->decode_get_frame(mpp_ctx, &frame);
                if (MPP_ERR_TIMEOUT == ret) {
                    if (times > 0) {
                        times--;
                        msleep(MPP_H264_DECODE_TIMEOUT);
                        goto try_again;
                    }
                    mydecoder_err("decode_get_frame failed too much time\n");
                }
                
                if (MPP_OK != ret) {
                    mydecoder_err("decode_get_frame failed ret %d\n", ret);
                    break;
                }

                if (frame) {
                    if (mpp_frame_get_info_change(frame)) {
                        ret = mpp_buffer_group_get_internal(&frm_grp, MPP_BUFFER_TYPE_DRM);
                        if (ret) {
                            mydecoder_err("get mpp buffer group failed ret %d\n", ret);
                            break;
                        }
                        mpi->control(mpp_ctx, MPP_DEC_SET_EXT_BUF_GROUP, frm_grp);
                        mpi->control(mpp_ctx, MPP_DEC_SET_INFO_CHANGE_READY, NULL);
                    } else {
                        u32 err_info = mpp_frame_get_errinfo(frame) | mpp_frame_get_discard(frame);
                        if (err_info) {
                            mydecoder_err("decoder_get_frame get err info:%d discard:%d.\n",
                                    mpp_frame_get_errinfo(frame), mpp_frame_get_discard(frame));
                        }
                        else {
                            //TBD
                            *got_frame += 1;
                            mydecoder_fill_frame_mpp(frame);
                        }
                    }
                    frm_eos = mpp_frame_get_eos(frame);
                    mpp_frame_deinit(&frame);
                    frame = NULL;
                    get_frm = 1;
                }
                
                if (frm_eos) {
                    mydecoder_info("found last frame\n");
                    break;
                }

                if (!get_frm)
                    break;
            } while (1);

            if (pkt_done)
                break;

            /*
             * why sleep here:
             * mpi->decode_put_packet will failed when packet in internal queue is
             * full,waiting the package is consumed .
             */
            msleep(MPP_H264_DECODE_TIMEOUT);
        } while (1);
}
#endif

s32 mydecoder_decode(MyPacket packet, MyFrame frame, s32 *got_frame)
{
#ifdef RK_PLAT
    if (use_rkmpp)
        mydecoder_decode_rkmpp(packet, got_frame);
    else
#endif
        mydecoder_decode_avcodec((AVFrame *)frame, packet, got_frame);

    return 0;
}

s32 mydecoder_retrieve_frame_yuv420p(AVFrame *frame, u8 *bgr_data)
{
    AVFrame bgr_frame;

    av_image_fill_arrays(bgr_frame.data, bgr_frame.linesize, bgr_data, 
                         AV_PIX_FMT_BGR24, frame->width, frame->height, 1);

    if (img_convert_ctx == NULL || frame->data == NULL) {
        img_convert_ctx = sws_getCachedContext(
                img_convert_ctx,
                frame->width, frame->height,
                AV_PIX_FMT_YUV420P,
                frame->width, frame->height,
                AV_PIX_FMT_BGR24,
                SWS_BICUBIC,
                NULL, NULL, NULL);
    }

    if (img_convert_ctx) {
        sws_scale(
                img_convert_ctx,
                (const uint8_t * const*)frame->data,
                frame->linesize,
                0, frame->height,
                bgr_frame.data,
                bgr_frame.linesize);
    }
    
    return 0;
}

s32 mydecoder_retrieve_frame_nv12(AVFrame *frame, u8 *bgr_data)
{
    AVFrame yuv_frame, bgr_frame;
    u8 *yuv_data = (u8 *)malloc(frame->width * frame->height * 3 / 2);
    
    av_image_fill_arrays(yuv_frame.data, yuv_frame.linesize, yuv_data,
                         AV_PIX_FMT_YUV420P, frame->width, frame->height, 1);
    av_image_fill_arrays(bgr_frame.data, bgr_frame.linesize, bgr_data, 
                         AV_PIX_FMT_BGR24, frame->width, frame->height, 1);

    /* NV12 to YUV420P */
    if (img_convert_ctx == NULL || frame->data == NULL) {
        img_convert_ctx = sws_getCachedContext(
                img_convert_ctx,
                frame->width, frame->height,
                AV_PIX_FMT_NV12,
                frame->width, frame->height,
                AV_PIX_FMT_YUV420P,
                SWS_BICUBIC,
                NULL, NULL, NULL);
    }

    if (img_convert_ctx) {
        sws_scale(
                img_convert_ctx,
                (const uint8_t * const*)frame->data,
                frame->linesize,
                0, frame->height,
                yuv_frame.data,
                yuv_frame.linesize);
    }

    /* YUV420P to BGR */
    if (img_convert_ctx2 == NULL || yuv_frame.data == NULL) {
        img_convert_ctx2 = sws_getCachedContext(
                img_convert_ctx2,
                frame->width, frame->height,
                AV_PIX_FMT_YUV420P,
                frame->width, frame->height,
                AV_PIX_FMT_BGR24,
                SWS_BICUBIC,
                NULL, NULL, NULL);
    }

    if (img_convert_ctx2) {
        sws_scale(
                img_convert_ctx2,
                (const uint8_t * const*)yuv_frame.data,
                yuv_frame.linesize,
                0, frame->height,
                bgr_frame.data,
                bgr_frame.linesize);
    }
    
    free(yuv_data);
    return 0;
}

#ifdef RK_PLAT
s32 mydecoder_retrieve_frame_nv12_mpp(u8 *bgr_data)
{
    AVFrame frame, yuv_frame, bgr_frame;
    u8 *yuv_data;

    frame.width = width;
    frame.height = height;
    
    yuv_data = (u8 *)malloc(width * height * 3 / 2);

    mydecoder_dbg("read idx: %d\n", r_idx);

    av_image_fill_arrays(frame.data, frame.linesize, frames[r_idx], 
                         AV_PIX_FMT_NV12, frame.width, frame.height, 1);
    av_image_fill_arrays(yuv_frame.data, yuv_frame.linesize, yuv_data,
                         AV_PIX_FMT_YUV420P, frame.width, frame.height, 1);
    av_image_fill_arrays(bgr_frame.data, bgr_frame.linesize, bgr_data, 
                         AV_PIX_FMT_BGR24, frame.width, frame.height, 1);

    /* NV12 to YUV420P */
    if (img_convert_ctx == NULL || frame.data == NULL) {
        img_convert_ctx = sws_getCachedContext(
                img_convert_ctx,
                frame.width, frame.height,
                AV_PIX_FMT_NV12,
                frame.width, frame.height,
                AV_PIX_FMT_YUV420P,
                SWS_BICUBIC,
                NULL, NULL, NULL);
    }

    if (img_convert_ctx) {
        sws_scale(
                img_convert_ctx,
                (const uint8_t * const*)frame.data,
                frame.linesize,
                0, frame.height,
                yuv_frame.data,
                yuv_frame.linesize);
    }

    /* YUV420P to BGR */
    if (img_convert_ctx2 == NULL || yuv_frame.data == NULL) {
        img_convert_ctx2 = sws_getCachedContext(
                img_convert_ctx2,
                frame.width, frame.height,
                AV_PIX_FMT_YUV420P,
                frame.width, frame.height,
                AV_PIX_FMT_BGR24,
                SWS_BICUBIC,
                NULL, NULL, NULL);
    }

    if (img_convert_ctx2) {
        sws_scale(
                img_convert_ctx2,
                (const uint8_t * const*)yuv_frame.data,
                yuv_frame.linesize,
                0, frame.height,
                bgr_frame.data,
                bgr_frame.linesize);
    }

    free(yuv_data);
    free(frames[r_idx]);
    r_idx++;
    if (r_idx >= MAX_BUFFER_FRAMES)
        r_idx = 0;
    
    return 0;
}
#endif

s32 mydecoder_retrieve_frame_drmprime(AVFrame *frame, u8 *bgr_data)
{
    //TBD
    return 0;
}

s32 mydecoder_retrieve_frame(MyFrame frame, u8 *bgr_data)
{
#ifdef RK_PLAT
    if (use_rkmpp) {
        return mydecoder_retrieve_frame_nv12_mpp(bgr_data);
    } else 
#endif
    {
        if (AV_PIX_FMT_YUV420P == dec_ctx->pix_fmt)
            return mydecoder_retrieve_frame_yuv420p((AVFrame *)frame, bgr_data);
        if (AV_PIX_FMT_NV12 == dec_ctx->pix_fmt)
            return mydecoder_retrieve_frame_nv12((AVFrame *)frame, bgr_data);
        if (AV_PIX_FMT_DRM_PRIME == dec_ctx->pix_fmt)
            return mydecoder_retrieve_frame_drmprime((AVFrame *)frame, bgr_data);
    }
    return 0;
}

s32 mydecoder_close(MyFrame frame, MyPacket packet)
{
#ifdef RK_PLAT
    if (use_rkmpp) {
        mpi->reset(mpp_ctx);
        mpp_destroy(mpp_ctx);
        mpp_ctx = NULL;
    } else 
#endif
    {
        AVFrame *avfrm = (AVFrame *)frame;
        avcodec_free_context(&dec_ctx);
        av_frame_free(&avfrm);
        avfrm = NULL;
        frame = NULL;
    }
    AVPacket *avpkt = (AVPacket *)packet;
    av_packet_free(&avpkt);
    sws_freeContext(img_convert_ctx);
    img_convert_ctx = NULL;
    sws_freeContext(img_convert_ctx2);
    img_convert_ctx2 = NULL;

    return 0;
}

