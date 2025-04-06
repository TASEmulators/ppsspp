// Copyright 2009 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#ifndef MOBILE_DEVICE

#if defined(__FreeBSD__)
#define __STDC_CONSTANT_MACROS 1
#endif

#include <string>
#include <cstdint>
#include <sstream>

#ifdef USE_FFMPEG

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
#include <libswscale/swscale.h>
}

#endif

#include "Common/Data/Convert/ColorConv.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"

#include "Core/Config.h"
#include "Core/AVIDump.h"
#include "Core/System.h"
#include "Core/Screenshot.h"

#include "GPU/Common/GPUDebugInterface.h"

#include "Core/ELF/ParamSFO.h"
#include "Core/HLE/sceKernelTime.h"
#include "StringUtils.h"

#ifdef USE_FFMPEG

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(55, 28, 1)
#define av_frame_alloc avcodec_alloc_frame
#define av_frame_free avcodec_free_frame
#endif

#include "FFMPEGCompat.h"

static AVFormatContext *s_format_context = nullptr;
static AVCodecContext *s_codec_context = nullptr;
static AVStream *s_stream = nullptr;
static AVFrame *s_src_frame = nullptr;
static AVFrame *s_scaled_frame = nullptr;
static SwsContext *s_sws_context = nullptr;

#endif

static int s_bytes_per_pixel;
static int s_width;
static int s_height;
static bool s_start_dumping = false;
static int s_current_width;
static int s_current_height;
static int s_file_index = 0;
static GPUDebugBuffer buf;

static void InitAVCodec() {
	static bool first_run = true;
	if (first_run) {
#ifdef USE_FFMPEG
#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(58, 12, 100)
		av_register_all();
#endif
#endif
		first_run = false;
	}
}

bool AVIDump::Start(int w, int h)
{
	s_width = w;
	s_height = h;
	s_current_width = w;
	s_current_height = h;

	InitAVCodec();
	bool success = CreateAVI();
	if (!success)
		CloseFile();
	return success;
}

bool AVIDump::CreateAVI() {
#ifdef USE_FFMPEG
	AVCodec *codec = nullptr;

	// Use gameID_EmulatedTimestamp for filename
	std::string discID = g_paramSFO.GetDiscID();
	Path video_file_name = GetSysDirectory(DIRECTORY_VIDEO) / StringFromFormat("%s_%s.avi", discID.c_str(), KernelTimeNowFormatted().c_str());

	s_format_context = avformat_alloc_context();

#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(58, 7, 0)
	char *filename = av_strdup(video_file_name.c_str());
	// Freed when the context is freed.
	s_format_context->url = filename;
#else
	const char *filename = s_format_context->filename;
	snprintf(s_format_context->filename, sizeof(s_format_context->filename), "%s", video_file_name.c_str());
#endif
	INFO_LOG(Log::Common, "Recording Video to: %s", video_file_name.ToVisualString().c_str());

	// Make sure that the path exists
	if (!File::Exists(GetSysDirectory(DIRECTORY_VIDEO)))
		File::CreateDir(GetSysDirectory(DIRECTORY_VIDEO));

	if (File::Exists(video_file_name))
		File::Delete(video_file_name);

	s_format_context->oformat = av_guess_format("avi", nullptr, nullptr);
	if (!s_format_context->oformat)
		return false;
	s_stream = avformat_new_stream(s_format_context, codec);
	if (!s_stream)
		return false;

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(57, 48, 101)
	s_codec_context = s_stream->codec;
#else
	s_codec_context = avcodec_alloc_context3(codec);
#endif
	s_codec_context->codec_id = g_Config.bUseFFV1 ? AV_CODEC_ID_FFV1 : s_format_context->oformat->video_codec;
	if (!g_Config.bUseFFV1)
		s_codec_context->codec_tag = MKTAG('X', 'V', 'I', 'D');  // Force XVID FourCC for better compatibility
	s_codec_context->codec_type = AVMEDIA_TYPE_VIDEO;
	s_codec_context->bit_rate = 400000;
	s_codec_context->width = s_width;
	s_codec_context->height = s_height;
	s_codec_context->time_base.num = 1001;
	s_codec_context->time_base.den = 60000;
	s_codec_context->gop_size = 12;
	s_codec_context->pix_fmt = g_Config.bUseFFV1 ? AV_PIX_FMT_BGRA : AV_PIX_FMT_YUV420P;

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 48, 101)
	if (avcodec_parameters_from_context(s_stream->codecpar, s_codec_context) < 0)
		return false;
#endif

	codec = avcodec_find_encoder(s_codec_context->codec_id);
	if (!codec)
		return false;
	if (avcodec_open2(s_codec_context, codec, nullptr) < 0)
		return false;

	s_src_frame = av_frame_alloc();
	s_scaled_frame = av_frame_alloc();

	s_scaled_frame->format = s_codec_context->pix_fmt;
	s_scaled_frame->width = s_width;
	s_scaled_frame->height = s_height;

#if LIBAVCODEC_VERSION_MAJOR >= 55
	if (av_frame_get_buffer(s_scaled_frame, 1))
		return false;
#else
	if (avcodec_default_get_buffer(s_codec_context, s_scaled_frame))
		return false;
#endif

	NOTICE_LOG(Log::G3D, "Opening file %s for dumping", filename);
	if (avio_open(&s_format_context->pb, filename, AVIO_FLAG_WRITE) < 0 || avformat_write_header(s_format_context, nullptr)) {
		WARN_LOG(Log::G3D, "Could not open %s", filename);
		return false;
	}

	return true;
#else
	return false;
#endif
}

#ifdef USE_FFMPEG

static void PreparePacket(AVPacket* pkt) {
	av_init_packet(pkt);
	pkt->data = nullptr;
	pkt->size = 0;
}

#endif

void AVIDump::AddFrame() {

}

void AVIDump::Stop() {
#ifdef USE_FFMPEG

	av_write_trailer(s_format_context);
	CloseFile();
	s_file_index = 0;
#endif
	NOTICE_LOG(Log::G3D, "Stopping frame dump");
}

void AVIDump::CloseFile() {
#ifdef USE_FFMPEG
	if (s_codec_context) {
#if LIBAVCODEC_VERSION_MAJOR < 55
		avcodec_default_release_buffer(s_codec_context, s_src_frame);
#endif
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 48, 101)
		avcodec_free_context(&s_codec_context);
#else
		avcodec_close(s_codec_context);
		s_codec_context = nullptr;
#endif
	}
	av_freep(&s_stream);

	av_frame_free(&s_src_frame);
	av_frame_free(&s_scaled_frame);

	if (s_format_context)
	{
		if (s_format_context->pb)
			avio_close(s_format_context->pb);
		av_freep(&s_format_context);
	}

	if (s_sws_context)
	{
		sws_freeContext(s_sws_context);
		s_sws_context = nullptr;
	}
#endif
}

void AVIDump::CheckResolution(int width, int height) {
#ifdef USE_FFMPEG
	// We check here to see if the requested width and height have changed since the last frame which
	// was dumped, then create a new file accordingly. However, is it possible for the width and height
	// to have a value of zero. If this is the case, simply keep the last known resolution of the video
	// for the added frame.
	if ((width != s_current_width || height != s_current_height) && (width > 0 && height > 0))
	{
		int temp_file_index = s_file_index;
		Stop();
		s_file_index = temp_file_index + 1;
		Start(width, height);
		s_current_width = width;
		s_current_height = height;
	}
#endif // USE_FFMPEG
}
#endif
