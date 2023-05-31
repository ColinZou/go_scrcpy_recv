#include <stdint.h>
#include "scrcpy_video_decoder.h"
#include <stdlib.h>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
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
#define DECODER_LOGGER "DECODER::"
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
	SOCKET socket = INVALID_SOCKET;
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
	std::vector<uchar> *img_buffer = NULL;
	std::mutex img_buffer_lock;
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
	VideoDecoder(SOCKET socket, video_decode_callback *callback, connection_buffer_config* buffer_cfg,
		int *keep_running, std::vector<uchar>* img_buffer);
	~VideoDecoder(void);
	int decode();
	void free_resources();
};
void show_data(char* data, int length) {
	for (int i = 0; i < length; i++) {
		if (i % 32 == 0 && i > 0) {
			spdlog::debug(DECODER_LOGGER "");
		}
		spdlog::debug(DECODER_LOGGER "{0:#x} ", (uint8_t)data[i]);
	}
	spdlog::debug(DECODER_LOGGER "");
}
VideoDecoder::VideoDecoder(SOCKET socket, video_decode_callback *callback, connection_buffer_config* buffer_cfg,
	int* keep_running, std::vector<uchar>* img_buffer) {
	this->socket = socket;
	this->callback = callback;
	this->buffer_cfg = buffer_cfg;
	this->keep_running = keep_running;
	this->img_buffer = img_buffer;
}
void VideoDecoder::free_resources() {
}
int VideoDecoder::read_device_info() {
	int buf_size = SCRCPY_DEVICE_INFO_SIZE;
	char device_info_data[SCRCPY_DEVICE_INFO_SIZE];
	memset(device_info_data, 0, buf_size);
	spdlog::debug(DECODER_LOGGER "Trying to read device info from socket {} ", this->socket);
	int bytes_read = recv(this->socket, device_info_data, buf_size, 0);
	spdlog::debug(DECODER_LOGGER "{} bytes received from socket {} ", bytes_read, this->socket);
	if (bytes_read < SCRCPY_DEVICE_INFO_SIZE) {
		return 1;
	}
	// device id is 64 bytes in total
	int device_size_bytes = 2;
	device_info_data[SCRCPY_DEIVCE_ID_LENGTH - 1] = '\0';
	array_copy_to(device_info_data, this->device_id, 0, SCRCPY_DEIVCE_ID_LENGTH);
	this->width = to_int(device_info_data, SCRCPY_DEVICE_INFO_SIZE, SCRCPY_DEIVCE_ID_LENGTH, device_size_bytes);
	this->height = to_int(device_info_data, SCRCPY_DEVICE_INFO_SIZE, SCRCPY_DEIVCE_ID_LENGTH + device_size_bytes, device_size_bytes);
	spdlog::debug(DECODER_LOGGER "Device {} connected, width: {}, height: {}, socket: {}", this->device_id, this->width, this->height, this->socket);
	// callback for device info
	if (this->callback) {
		this->callback->on_device_info(device_id, this->width, this->height);
	}
	return 0;
}
VideoDecoder::~VideoDecoder() {
	spdlog::debug(DECODER_LOGGER "Cleaning video decoder for {}", this->socket);
	if (NULL != this->frame) {
		av_frame_free(&this->frame);
	}
	if (NULL != this->codec_ctx) {
		avcodec_free_context(&this->codec_ctx);
	}
	if (NULL != this->codec_parser_context) {
		av_parser_close(this->codec_parser_context);
	}
	if (NULL != this->active_packet) {
		av_packet_free(&this->active_packet);
	}
	if (NULL != this->packet_buffer) {
		free(this->packet_buffer);
	}
	if (NULL != this->active_data) {
		free(this->active_data);
	}
	std::lock_guard<std::mutex> lock_guard{ this->img_buffer_lock };
}
int VideoDecoder::init_decoder() {
	int result = 0;
	connection_buffer_config* cfg = this->buffer_cfg;
	//分配内存
	this->packet_buffer = (char *)malloc(cfg->video_packet_buffer_size_kb * 1024);
	if (!this->packet_buffer) {
		spdlog::debug(DECODER_LOGGER "No enough memory for packet buffer");
		return -1;
	}
	this->active_data = (char*)malloc(cfg->video_packet_buffer_size_kb * 1024);
	if (!this->active_data) {
		spdlog::debug(DECODER_LOGGER "No enough memory for active_data");
		return -1;
	}
	enum AVCodecID h264 = AV_CODEC_ID_H264;
	const AVCodec *codec = (AVCodec *)avcodec_find_decoder(h264);
	if (!codec) {
		spdlog::debug(DECODER_LOGGER "Could not find h264 codec");
		return -1;
	}
	AVCodecContext* codec_context = avcodec_alloc_context3(codec);
	if (!codec_context) {
		spdlog::debug(DECODER_LOGGER "No enough memory for codec_context");
		return -1;
	}
	if (avcodec_open2(codec_context, codec, NULL) != 0) {
		spdlog::debug(DECODER_LOGGER "Failed to open codec");
		avcodec_free_context(&codec_context);
		return -1;
	}
	AVCodecParserContext* codec_parser_context = av_parser_init(h264);
	if (!codec_parser_context) {
		spdlog::debug(DECODER_LOGGER "Failed to init paser context");
		avcodec_free_context(&codec_context);
		return -1;
	}
	codec_parser_context->flags = codec_parser_context->flags | PARSER_FLAG_COMPLETE_FRAMES;
	AVPacket* packet = av_packet_alloc();
	if (!packet) {
		spdlog::debug(DECODER_LOGGER "Failed to init packet");
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
	spdlog::debug(DECODER_LOGGER "Trying to read video header({} bytes) from {} into {} ", H264_HEAD_BUFFER_SIZE, this->socket, (uintptr_t)header_buffer);
	int bytes_received = recv(this->socket, header_buffer, H264_HEAD_BUFFER_SIZE, 0);
	if (bytes_received != H264_HEAD_BUFFER_SIZE) {
		spdlog::debug(DECODER_LOGGER "Error, Read {}/{} for video header", bytes_received, H264_HEAD_BUFFER_SIZE);
		return 1;
	}
	uint64_t pts = to_long(header_buffer, bytes_received, 0, 8);
	int length = to_int(header_buffer, bytes_received, 8, 4);
	header->length = length;
	header->pts = pts;
	return bytes_received;
}
int VideoDecoder::recv_network_buffer(int length, char* buffer, char* chunk) {
	int result = 0;
	int read_total = 0;
	int max_chunk = PACKET_CHUNK_BUFFER_SIZE;
	while (read_total < length) {
		int chunk_read_plan = min(max_chunk, length - read_total);
		int read_length = recv(this->socket, chunk, chunk_read_plan, 0);
		if (read_length != chunk_read_plan) {
			spdlog::debug(DECODER_LOGGER "Planned to read {} bytes, got {} bytes instead socket={}", chunk_read_plan, read_length, this->socket);
		}
		else if (read_length <= 0) {
			result = -1;
			break;
		}
		int fill_start_index = read_total;
		read_total += read_length;
		spdlog::debug(DECODER_LOGGER "Receiving {}/{} from network for socket {}", read_total, length, this->socket);
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
	
	spdlog::debug(DECODER_LOGGER "is_config = {}, has_pending = {} for socket {}", is_config ? "yes" : "no", has_pending ? "yes" : "no", this->socket);
	if (is_config || has_pending) {
		int offset = 0;
		if (has_pending) {
			spdlog::debug(DECODER_LOGGER "Detected pending, offset will be {} ", this->pending_data_length);
			offset = this->pending_data_length;
		}
		else {
			spdlog::debug(DECODER_LOGGER "no pending data, saving received data to pending buffer for socket {}", this->socket);
			array_copy_to(this->packet_buffer, this->active_data, 0, length);
			this->pending_data_length = length;
			this->has_pending = TRUE;
		}
		if (offset > 0) {
			int new_size = this->pending_data_length + length;
			spdlog::debug(DECODER_LOGGER "Existed pending data size = {}, current pending size = {}, final size={}, socket={}", this->pending_data_length, length, 
				new_size, this->socket);
			array_copy_to(this->packet_buffer, this->active_data, this->pending_data_length, length);
			this->pending_data_length = 0;
			active_packet->data = (uint8_t *)this->active_data;
			active_packet->size = new_size;
			this->has_pending = FALSE;
		}
		if (!is_config) {
			spdlog::debug(DECODER_LOGGER "Preparing active packet");
			active_packet->data = NULL;
			int old_size = active_packet->size;

			av_packet_unref(active_packet);

			active_packet->data = (uint8_t*)this->active_data;
			active_packet->dts = this->packet_stat.dts;
			active_packet->flags = this->packet_stat.flags;
			active_packet->pts = this->packet_stat.pts;
			active_packet->size = old_size;
		}
	}
	else {
		this->pending_data_length = 0;
		active_packet->data = (uint8_t*)this->packet_buffer;
	}
	if (is_config) {
		spdlog::debug(DECODER_LOGGER "In configuring, will not call decoder for socket {}", this->socket);
		active_packet->data = NULL;
		av_packet_unref(active_packet);
		result = 1;
	}
	return result;
}
image_size* VideoDecoder::get_image_size() {
	if (NULL == this->callback) {
		spdlog::debug(DECODER_LOGGER "no image size provider configured");
		return NULL;
	}
	if (0 == strlen(this->device_id)) {
		spdlog::debug(DECODER_LOGGER "no device id provided");
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
		spdlog::debug(DECODER_LOGGER "Resizing image from {}x{} to {}x{}", width, height, target_width, target_height);
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
	spdlog::debug(DECODER_LOGGER "Encoding image to png format");
	if (cv::imencode(".png", image, *this->img_buffer)) {
		image.release();
		int img_size = this->img_buffer->size();
		spdlog::debug(DECODER_LOGGER "sending {} bytes to callback", img_size);
		uint8_t* img_data = (uint8_t*)this->img_buffer->data();
		this->callback->on_video_callback(device_id, img_data, this->img_buffer->size(), target_width, target_height, width, height);
	} else {
		spdlog::debug(DECODER_LOGGER "Failed to encode a png file");
	}
	return 0;
}
int VideoDecoder::decode_frames(uint64_t pts, int length) {
	int result = 0;
	BOOL reset_has_pending = FALSE;
	int status = 0;
	AVFrame* frame = NULL;
	spdlog::debug(DECODER_LOGGER "decode_frames pts={} length={} socket={}", pts, length, this->socket);
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
	spdlog::debug(DECODER_LOGGER "Fetching codec parser context for socekt {}", this->socket);
	AVCodecParserContext* parser_context = this->codec_parser_context;
	if (parser_context->key_frame == 1) {
		active_packet->flags = (active_packet->flags | AV_PKT_FLAG_KEY);
		spdlog::debug(DECODER_LOGGER "Confiuring flags for socket {}", this->socket);
	}
	spdlog::debug(DECODER_LOGGER "Fetching codec context for socket {}", this->socket);
	AVCodecContext* codec_context = this->codec_ctx;
	spdlog::debug(DECODER_LOGGER "Sending packet for decoding, data pointer address is {} size={} socket={}", (uintptr_t)active_packet->data,
		active_packet->size, this->socket);
	result = avcodec_send_packet(codec_context, active_packet);
	if (result != 0) {
		reset_has_pending = TRUE;
		spdlog::debug(DECODER_LOGGER "Could not invoke avcodec_send_packet: {} socket={}", result, this->socket);
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
			spdlog::debug(DECODER_LOGGER "Got frame with width={} height={} socket={} ", frame->width, frame->height, this->socket);
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
			spdlog::debug(DECODER_LOGGER "Reset has_pending=false for socket {}", this->socket);
			this->has_pending = FALSE;
		}
		return result;
}
int VideoDecoder::decode() {
	if (this->read_device_info()) {
		spdlog::debug(DECODER_LOGGER "Failed to read device info for socket {} ", this->socket);
		return 1;
	}
	if (this->init_decoder() != 0) {
		spdlog::debug(DECODER_LOGGER "Failed to init decoder for socket {} ", this->socket);
		return 1;
	}
	struct VideoHeader header;
	int keep_connection = 1;
	int status = 0;
	while (*this->keep_running == 1 && keep_connection == 1) {
		int header_size = this->read_video_header(&header);
		if (header_size <= 0) {
			keep_connection = 0;
			status = 1;
			spdlog::debug(DECODER_LOGGER "Failed to read header info from {}", this->socket);
			break;
		}
		if (header_size != H264_HEAD_BUFFER_SIZE) {
			status = 1;
			spdlog::debug(DECODER_LOGGER "Failed to read header info from {}", this->socket);
			break;
		}
		int decode_status = this->decode_frames(header.pts, header.length);
		if (decode_status == -1) {
			spdlog::debug(DECODER_LOGGER "Bad status for decoding video from {}, will not continue", this->socket);
			break;
		}
	}
	return status;
}
int socket_decode(SOCKET socket, video_decode_callback *callback, connection_buffer_config* buffer_cfg,
	int *keep_running) {
    auto buffer_size = buffer_cfg->video_packet_buffer_size_kb * 1024 * 2;
	std::vector<uchar> * image_buffer = new std::vector<uchar>(buffer_size);
	int result_code = 0;
	VideoDecoder *decoder = new VideoDecoder(socket, callback, 
		buffer_cfg, keep_running, 
		image_buffer);
	int result = decoder->decode();

	delete image_buffer;
	delete decoder;
	return result;
}
