#include <stdint.h>
#include "scrcpy_video_decoder.h"
#include <stdlib.h>
#include "opencv2/imgcodecs.hpp"
#include <direct.h>
#include <utils.h>
#include <mutex>
#include "logging.h"

extern "C" {
#include "libavutil/timestamp.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
}
using namespace std;
using namespace cv;

#ifndef SCRCPY_DEVICE_INFO_SIZE
#define SCRCPY_DEVICE_INFO_SIZE 68
#define SCRCPY_DEIVCE_ID_LENGTH 64
#define H264_HEAD_BUFFER_SIZE 12
#define PACKET_CHUNK_BUFFER_SIZE 32*1024
#define PNG_IMG_BUFFER 1024 * 1024 * 4
#endif
typedef struct VideoHeader {
    uint64_t pts;
    int length;
} VideoHeader;

typedef struct PacketStat {
    int64_t pts;
    int64_t dts;
    int flags;
}PacketStat;

class VideoDecoder {
    private:
        char device_id[SCRCPY_DEIVCE_ID_LENGTH];
        connection_buffer_config *buffer_cfg = NULL;
        image_size *img_size = NULL;
        boost::shared_ptr<tcp::socket> socket = NULL;
        video_decode_callback *callback = NULL;
        char header_buffer[H264_HEAD_BUFFER_SIZE];
        struct AVCodec *codec = NULL;
        AVCodecContext *codec_ctx = NULL;
        AVCodecParserContext *codec_parser_context = NULL;
        AVPacket *active_packet = NULL;
        AVFrame *frame = NULL;
        struct PacketStat packet_stat;
        char *active_data = NULL;
        char *packet_buffer = NULL;
        char packet_chunk[PACKET_CHUNK_BUFFER_SIZE];
        int pending_data_length = 0;
        BOOL has_pending = FALSE;
        int width = 0;
        int height = 0;
        int *keep_running = NULL;
        int *disconnect_flag = NULL;
        std::vector<uchar> *img_buffer = NULL;
        std::mutex img_buffer_lock;
        std::mutex decoder_lock;
        /*
         * 读取设备信息
         */
        int read_device_info();
        /*
         * 初始化缓存区
         */
        int init_decoder();
        /*
         * 读取视频头信息
         * @param header 头信息
         * @return 成功读取的头大小
         */
        int read_video_header(struct VideoHeader *header);
        /*
         * 视频解码
         * @param pts
         * @param length
         * @return 状态码
         */
        int decode_frames(uint64_t pts, int length);
        /*
         * 从网络接收数据
         * @param length
         * @return 状态码
         */
        int recv_network_buffer(int length, char* buffer, char* chunk);
        /*
         * 准备好packet用于解码
         * @param pts
         * @param length
         * @return 状态码
         */
        int prepare_packet(uint64_t pts, int length);

        int rgb_frame_and_callback(AVCodecContext* dec_ctx, AVFrame* frame);

        image_size* get_image_size();

    public:
        VideoDecoder(boost::shared_ptr<tcp::socket> socket, video_decode_callback *callback, connection_buffer_config* buffer_cfg,
                int *keep_running, std::vector<uchar>* img_buffer, int *disconnect_flag);
        ~VideoDecoder(void);
        int decode();
        void free_resources();
        void on_img_size_configured(char *device_id, scrcpy_rect img_size);
};
VideoDecoder::VideoDecoder(boost::shared_ptr<tcp::socket> socket, video_decode_callback *callback, connection_buffer_config* buffer_cfg,
        int* keep_running, std::vector<uchar>* img_buffer, int *disconnect_flag) {
    this->socket = socket;
    this->callback = callback;
    this->buffer_cfg = buffer_cfg;
    this->keep_running = keep_running;
    this->img_buffer = img_buffer;
    this->disconnect_flag = disconnect_flag;
}
void VideoDecoder::free_resources() {
}
int VideoDecoder::read_device_info() {
    int buf_size = SCRCPY_DEVICE_INFO_SIZE;
    char device_info_data[SCRCPY_DEVICE_INFO_SIZE];
    memset(device_info_data, 0, buf_size);
    SPDLOG_TRACE("Trying to read device info from socket {} ", con_addr(this->socket));
    try {
        auto bytes_read = this->socket->receive((boost::asio::buffer(device_info_data, buf_size)));
        SPDLOG_TRACE("{} bytes received from socket {} ", bytes_read, con_addr(this->socket));
        if (bytes_read < SCRCPY_DEVICE_INFO_SIZE) {
            return 1;
        }
    } catch(boost::system::system_error &e) {
        SPDLOG_ERROR("Failed to read device info from {}: {}", con_addr(this->socket), e.what());
        return 1;
    }
    // device id is 64 bytes in total
    int device_size_bytes = 2;
    device_info_data[SCRCPY_DEIVCE_ID_LENGTH - 1] = '\0';
    array_copy_to(device_info_data, this->device_id, 0, SCRCPY_DEIVCE_ID_LENGTH);
    this->width = to_int(device_info_data, SCRCPY_DEVICE_INFO_SIZE, SCRCPY_DEIVCE_ID_LENGTH, device_size_bytes);
    this->height = to_int(device_info_data, SCRCPY_DEVICE_INFO_SIZE, SCRCPY_DEIVCE_ID_LENGTH + device_size_bytes, device_size_bytes);
    SPDLOG_INFO("Device {} connected, width: {}, height: {}, data callback is {}", this->device_id, this->width, 
            this->height, (uintptr_t)this->callback);
    // callback for device info
    if (this->callback) {
        this->callback->on_device_info(device_id, this->width, this->height);
        // adding image size callback for device
        auto image_size_config_callback = std::bind(&VideoDecoder::on_img_size_configured, this, 
                std::placeholders::_1, std::placeholders::_2);
        SPDLOG_INFO("Add image size configured callback for device {}", device_id);
        this->callback->add_frame_img_size_cfg_callback(device_id, image_size_config_callback);
    }
    return 0;
}
void VideoDecoder::on_img_size_configured(char *device_id, scrcpy_rect img_size) {
    std::lock_guard<std::mutex> locker(this->decoder_lock);
    auto codec_ctx = this->codec_ctx;
    auto frame = this->frame;
    bool has_frame = codec_ctx && frame;
    SPDLOG_DEBUG("Frame image size configured to {} x {} for device {}, has_frame ? {}", img_size.width, img_size.height, 
            device_id, has_frame ? "yes":"no");
    // resend last frame
    if(NULL == codec_ctx || NULL == frame) {
        SPDLOG_WARN("Could not call rgb_frame_and_callback while codec_ctx/frame is null");
        return;
    }
    SPDLOG_DEBUG("Trying to invoke rgb_frame_and_callback for device {} when frame image size reconfigured", this->device_id);
    log_flush();
    this->rgb_frame_and_callback(codec_ctx, frame);
}
VideoDecoder::~VideoDecoder() {
    SPDLOG_INFO("Cleaning video decoder");
    std::lock_guard<std::mutex> lock_guard{ this->img_buffer_lock };
    if (this->frame) {
        SPDLOG_DEBUG("Removing frame");
        av_frame_free(&this->frame);
        this->frame = NULL;
    }
    if (this->codec_ctx) {
        SPDLOG_DEBUG("Removing codec_ctx");
        avcodec_free_context(&this->codec_ctx);
        this->codec_ctx = NULL;
    }
    if (this->codec_parser_context) {
        SPDLOG_DEBUG("Removing codec_parser_context");
        av_parser_close(this->codec_parser_context);
        this->codec_parser_context = NULL;
    }
    if (this->active_packet) {
        SPDLOG_DEBUG("Removing active_packet");
        av_packet_free(&this->active_packet);
        this->active_packet = NULL;
    }
    if (this->packet_buffer) {
        SPDLOG_DEBUG("Removing packet_buffer");
        free(this->packet_buffer);
        this->packet_buffer = NULL;
    }
    if (this->active_data) {
        SPDLOG_DEBUG("Removing active_data");
        free(this->active_data);
        this->active_data = NULL;
    }
    log_flush();
}
int VideoDecoder::init_decoder() {
    int result = 0;
    connection_buffer_config* cfg = this->buffer_cfg;
    //分配内存
    this->packet_buffer = (char *)malloc(cfg->video_packet_buffer_size_kb * 1024);
    if (!this->packet_buffer) {
        SPDLOG_ERROR("No enough memory for packet buffer");
        return -1;
    }
    this->active_data = (char*)malloc(cfg->video_packet_buffer_size_kb * 1024);
    if (!this->active_data) {
        SPDLOG_ERROR("No enough memory for active_data");
        return -1;
    }
    enum AVCodecID h264 = AV_CODEC_ID_H264;
    const AVCodec *codec = (AVCodec *)avcodec_find_decoder(h264);
    if (!codec) {
        SPDLOG_ERROR("Could not find h264 codec");
        return -1;
    }
    AVCodecContext* codec_context = avcodec_alloc_context3(codec);
    if (!codec_context) {
        SPDLOG_ERROR("No enough memory for codec_context");
        return -1;
    }
    if (avcodec_open2(codec_context, codec, NULL) != 0) {
        SPDLOG_ERROR("Failed to open codec");
        avcodec_free_context(&codec_context);
        return -1;
    }
    AVCodecParserContext* codec_parser_context = av_parser_init(h264);
    if (!codec_parser_context) {
        SPDLOG_ERROR("Failed to init paser context");
        avcodec_free_context(&codec_context);
        return -1;
    }
    codec_parser_context->flags = codec_parser_context->flags | PARSER_FLAG_COMPLETE_FRAMES;
    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        SPDLOG_ERROR("Failed to init packet");
        avcodec_free_context(&codec_context);
        av_parser_close(codec_parser_context);
        return -1;
    }
    this->codec = const_cast<AVCodec*>(codec);
    this->codec_ctx = codec_context;
    this->codec_parser_context = codec_parser_context;
    this->active_packet = packet;
    return result;
}
int VideoDecoder::read_video_header(struct VideoHeader* header) {
    char* header_buffer = this->header_buffer;
    SPDLOG_DEBUG("Trying to read video header({} bytes) from {} into {} ", H264_HEAD_BUFFER_SIZE, con_addr(this->socket), (uintptr_t)header_buffer);
    int bytes_received = 0;
    try {
        bytes_received = (int)this->socket->receive(boost::asio::buffer(header_buffer, H264_HEAD_BUFFER_SIZE));
        if (bytes_received != H264_HEAD_BUFFER_SIZE) {
            SPDLOG_ERROR("Error, Read {}/{} for video header", bytes_received, H264_HEAD_BUFFER_SIZE);
            return 1;
        }
    }catch(boost::system::system_error& e) {
        SPDLOG_ERROR("Could not read video header {}", e.what());
        return 1;
    }
    uint64_t pts = to_long(header_buffer, bytes_received, 0, 8);
    int length = to_int(header_buffer, bytes_received, 8, 4);
    header->length = length;
    header->pts = pts;
    SPDLOG_DEBUG("header.length={} header.pts={}", length, pts);
    return bytes_received;
}
int VideoDecoder::recv_network_buffer(int length, char* buffer, char* chunk) {
    int result = 0;
    int read_total = 0;
    int max_chunk = PACKET_CHUNK_BUFFER_SIZE;
    while (read_total < length) {
        int chunk_read_plan = min(max_chunk, length - read_total);
        int read_length = 0;
        try {
            read_length = (int)this->socket->receive(boost::asio::buffer(chunk, chunk_read_plan));
        } catch(boost::system::system_error& e) {
            SPDLOG_ERROR("Failed to recv_network_buffer {}", e.what());
            return 1;
        }
        if (read_length != chunk_read_plan) {
            SPDLOG_DEBUG("Planned to read {} bytes, got {} bytes instead from socket={}", chunk_read_plan, read_length, con_addr(this->socket));
        }
        else if (read_length <= 0) {
            SPDLOG_ERROR("Connection may be closed for device {}", this->device_id);
            result = -1;
            break;
        }
        int fill_start_index = read_total;
        read_total += read_length;
        SPDLOG_TRACE("Receiving {}/{} from network for socket {}", read_total, length, con_addr(this->socket));
        array_copy_to(chunk, buffer, fill_start_index, read_length);
    }
    return result;
}
int VideoDecoder::prepare_packet(uint64_t pts, int length) {
    int result = 0;
    AVPacket* active_packet = this->active_packet;
    BOOL has_pending = this->has_pending;

    active_packet->size = length;
    active_packet->pts = (pts == -1 ? AV_NOPTS_VALUE : pts);

    BOOL is_config = active_packet->pts == AV_NOPTS_VALUE;

    this->packet_stat.pts = active_packet->pts;
    this->packet_stat.dts = active_packet->dts;
    this->packet_stat.flags = active_packet->flags;

    SPDLOG_TRACE("is_config = {}, has_pending = {} for socket {}", is_config ? "yes" : "no", has_pending ? "yes" : "no", con_addr(this->socket));
    if (is_config || has_pending) {
        int offset = 0;
        if (has_pending) {
            SPDLOG_TRACE("Detected pending, offset will be {} ", this->pending_data_length);
            offset = this->pending_data_length;
        }
        else {
            SPDLOG_TRACE("no pending data, saving received data to pending buffer for socket {}", con_addr(this->socket));
            array_copy_to(this->packet_buffer, this->active_data, 0, length);
            this->pending_data_length = length;
            this->has_pending = TRUE;
        }
        if (offset > 0) {
            int new_size = this->pending_data_length + length;
            SPDLOG_TRACE("Existed pending data size = {}, current pending size = {}, final size={}, socket={}", this->pending_data_length, length, 
                    new_size, con_addr(this->socket));
            array_copy_to(this->packet_buffer, this->active_data, this->pending_data_length, length);
            this->pending_data_length = 0;
            active_packet->data = (uint8_t *)this->active_data;
            active_packet->size = new_size;
            this->has_pending = FALSE;
        }
        if (!is_config) {
            SPDLOG_TRACE("Preparing active packet");
            active_packet->data = NULL;
            int old_size = active_packet->size;

            av_packet_unref(active_packet);

            active_packet->data = (uint8_t*)this->active_data;
            active_packet->dts = this->packet_stat.dts;
            active_packet->flags = this->packet_stat.flags;
            active_packet->pts = this->packet_stat.pts;
            active_packet->size = old_size;
        }
    } else {
        this->pending_data_length = 0;
        active_packet->data = (uint8_t*)this->packet_buffer;
    }
    if (is_config) {
        SPDLOG_TRACE("In configuring, will not call decoder for socket {}", con_addr(this->socket));
        active_packet->data = NULL;
        av_packet_unref(active_packet);
        result = 1;
    }
    return result;
}
image_size* VideoDecoder::get_image_size() {
    if (NULL == this->callback) {
        SPDLOG_TRACE("no image size provider configured");
        return NULL;
    }
    if (0 == strlen(this->device_id)) {
        SPDLOG_TRACE("no device id provided");
        return NULL;
    }
    return this->callback->get_configured_img_size(this->device_id);
}
int frame_count = 1;
int VideoDecoder::rgb_frame_and_callback(AVCodecContext* dec_ctx, AVFrame* frame) {
    struct SwsContext *sws_ctx = NULL;
    int width = frame->width;
    int height = frame->height;

    int cv_line_size[1];
    int target_width = width;
    int target_height = height;

    image_size* configrued_size = this->get_image_size();
    if (NULL != configrued_size && configrued_size->width > 0 && configrued_size->height > 0) {
        target_width = configrued_size->width;
        target_height = configrued_size->height;
        SPDLOG_TRACE("Resizing image from {}x{} to {}x{}", width, height, target_width, target_height);
    }

    cv::Mat image(target_height, target_width, CV_8UC4);
    cv_line_size[0] = (int)image.step1();

    sws_ctx = sws_getContext(dec_ctx->width,
            dec_ctx->height,
            dec_ctx->pix_fmt,
            target_width,
            target_height,
            AV_PIX_FMT_RGB32,
            SWS_BICUBIC,
            NULL,
            NULL,
            NULL);
    if (NULL == sws_ctx) {
        return 1;
    }
    sws_scale(sws_ctx, frame->data, frame->linesize, 0, height, &image.data, cv_line_size);
    sws_freeContext(sws_ctx);
    std::lock_guard<std::mutex> lock_guard{ this->img_buffer_lock };
    SPDLOG_TRACE("Encoding image to png format");
    if (cv::imencode(".png", image, *this->img_buffer)) {
        image.release();
        int img_size = (int)this->img_buffer->size();
        SPDLOG_TRACE("sending {} bytes to callback", img_size);
        uint8_t* img_data = (uint8_t*)this->img_buffer->data();
        this->callback->on_video_callback(device_id, img_data, (int)this->img_buffer->size(), target_width, target_height, 
                this->width, this->height);
    } else {
        SPDLOG_ERROR("Failed to encode a png file");
    }
    return 0;
}
int VideoDecoder::decode_frames(uint64_t pts, int length) {
    std::lock_guard<std::mutex> locker(this->decoder_lock);
    int result = 0;
    BOOL reset_has_pending = FALSE;
    int status = 0;
    AVFrame* frame = NULL;
    SPDLOG_DEBUG("decode_frames pts={} length={} socket={}", pts, length, con_addr(this->socket));
    result = this->recv_network_buffer(length, this->packet_buffer, this->packet_chunk);
    // failed to receiving data
    if (result != 0) {
        return -1;
    }
    result = this->prepare_packet(pts, length);
    // no need to do decoding
    if (result == 1) {
        return result;
    }
    SPDLOG_DEBUG("Fetching codec parser context for socekt {}", con_addr(this->socket));
    AVCodecParserContext* parser_context = this->codec_parser_context;
    if (parser_context->key_frame == 1) {
        active_packet->flags = (active_packet->flags | AV_PKT_FLAG_KEY);
        SPDLOG_DEBUG("Confiuring flags for socket {}", con_addr(this->socket));
    }
    SPDLOG_DEBUG("Fetching codec context for socket {}", con_addr(this->socket));
    AVCodecContext* codec_context = this->codec_ctx;
    SPDLOG_DEBUG("Sending packet for decoding, data pointer address is {} size={} socket={}", (uintptr_t)active_packet->data,
            active_packet->size, con_addr(this->socket));
    result = avcodec_send_packet(codec_context, active_packet);
    if (result != 0) {
        reset_has_pending = TRUE;
        SPDLOG_ERROR("Could not invoke avcodec_send_packet: {} socket={}", result, con_addr(this->socket));
        active_packet->data = NULL;
        av_packet_unref(active_packet);
        goto end;
    }
    if (NULL == this->frame) {
        this->frame = av_frame_alloc();
        if (!this->frame) {
            result = -1;
            goto end;
        }
    }
    frame = this->frame;
    while (status >= 0) {
        status = avcodec_receive_frame(codec_context, frame);
        if (status == 0) {
            SPDLOG_DEBUG("Got frame with width={} height={} socket={} ", frame->width, frame->height, con_addr(this->socket));
            this->rgb_frame_and_callback(codec_context, frame);
        }
        else if (status == AVERROR(EAGAIN)) {
            active_packet->data = NULL;
            av_packet_unref(active_packet);
            reset_has_pending = TRUE;
            goto end;
        }
        else if (status == AVERROR_EOF) {
            reset_has_pending = TRUE;
            break;
        }
    }
end:
    if (has_pending && reset_has_pending) {
        SPDLOG_DEBUG("Reset has_pending=false for socket {}", con_addr(this->socket));
        this->has_pending = FALSE;
    }
    return result;
}
int VideoDecoder::decode() {
    if (this->read_device_info()) {
        SPDLOG_ERROR("Failed to read device info for socket {} ", con_addr(this->socket));
        log_flush();
        return 1;
    }
    if (this->init_decoder() != 0) {
        SPDLOG_ERROR("Failed to init decoder for socket {} ", con_addr(this->socket));
        log_flush();
        return 1;
    }
    struct VideoHeader header;
    int keep_connection = 1;
    int status = 0;
    SPDLOG_DEBUG("Trying to run a loop for receiving video data from {} keep_running = {} keep_connection = {}", 
            con_addr(this->socket), *this->keep_running, keep_connection);
    log_flush();
    while (*this->keep_running == 1 && keep_connection == 1 && !*disconnect_flag) {
        int header_size = this->read_video_header(&header);
        if (header_size <= 0) {
            keep_connection = 0;
            status = 1;
            SPDLOG_ERROR("Failed to read header info from {}", con_addr(this->socket));
            break;
        }
        if (header_size != H264_HEAD_BUFFER_SIZE) {
            status = 1;
            SPDLOG_ERROR("Failed to read header info from {}", con_addr(this->socket));
            break;
        }
        int decode_status = this->decode_frames(header.pts, header.length);
        if (decode_status == -1) {
            SPDLOG_ERROR("Bad status for decoding video from {}, will not continue", con_addr(this->socket));
            break;
        }
    }
    SPDLOG_DEBUG("Decoder loop was stopped for {} ", con_addr(this->socket));
    log_flush();
    if (strlen(this->device_id) > 0) {
        SPDLOG_DEBUG("Removing all frame image size callback for device {}", this->device_id);
        log_flush();
        this->callback->remove_frame_img_size_cfg_callback(this->device_id);
    }
    return status;
}
int socket_decode(boost::shared_ptr<tcp::socket> socket, video_decode_callback *callback, connection_buffer_config* buffer_cfg,
        int *keep_running, int *disconnect_flag) {
    SPDLOG_INFO("socket_decode {}", con_addr(socket));
    log_flush();
    auto buffer_size = buffer_cfg->video_packet_buffer_size_kb * 1024 * 2;
    std::vector<uchar> * image_buffer = new std::vector<uchar>(buffer_size);
    int result_code = 0;
    VideoDecoder *decoder = new VideoDecoder(socket, callback, 
            buffer_cfg, keep_running, 
            image_buffer, disconnect_flag);
    int result = decoder->decode();
    SPDLOG_INFO("Video decoder is shutting down");
    log_flush();
    delete image_buffer;
    SPDLOG_INFO("Video decoder img_buffer cleared");
    log_flush();
    delete decoder;
    SPDLOG_INFO("Video decoder deleted");
    log_flush();
    return result;
}
