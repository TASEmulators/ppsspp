// Copyright (c) 2020- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.
#include "ppsspp_config.h"
#include "Camera.h"
#include "Core/Config.h"

#ifdef USE_FFMPEG
void convert_frame(int inw, int inh, unsigned char *inData, AVPixelFormat inFormat,
					int outw, int outh, unsigned char **outData, int *outLen) {
	struct SwsContext *sws_context = sws_getContext(
				inw, inh, inFormat,
				outw, outh, AV_PIX_FMT_RGB24,
				SWS_BICUBIC, NULL, NULL, NULL);

	// resize
	uint8_t *src[4] = {0};
	uint8_t *dst[4] = {0};
	int srcStride[4], dstStride[4];

	unsigned char *rgbData = (unsigned char*)malloc(outw * outh * 4);

	av_image_fill_linesizes(srcStride, inFormat,         inw);
	av_image_fill_linesizes(dstStride, AV_PIX_FMT_RGB24, outw);

	av_image_fill_pointers(src, inFormat,         inh,  inData,  srcStride);
	av_image_fill_pointers(dst, AV_PIX_FMT_RGB24, outh, rgbData, dstStride);

	sws_scale(sws_context,
		src, srcStride, 0, inh,
		dst, dstStride);

	// compress jpeg
	*outLen = outw * outh * 2;
	*outData = (unsigned char*)malloc(*outLen);

	jpge::params params;
	params.m_quality = 60;
	params.m_subsampling = jpge::H2V2;
	params.m_two_pass_flag = false;
	jpge::compress_image_to_jpeg_file_in_memory(
		*outData, *outLen, outw, outh, 3, rgbData, params);
	free(rgbData);
}
#endif //USE_FFMPEG


void __cameraDummyImage(int width, int height, unsigned char** outData, int* outLen) {
#ifdef USE_FFMPEG
	unsigned char *rgbData = (unsigned char *)malloc(3 * width * height);
	if (!rgbData) {
		*outData = nullptr;
		return;
	}
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			rgbData[3 * (y * width + x) + 0] = x*255/width;
			rgbData[3 * (y * width + x) + 1] = x*255/width;
			rgbData[3 * (y * width + x) + 2] = y*255/height;
		}
	}

	*outLen = width * height * 2;
	*outData = (unsigned char*)malloc(*outLen);

	jpge::params params;
	params.m_quality = 60;
	params.m_subsampling = jpge::H2V2;
	params.m_two_pass_flag = false;
	jpge::compress_image_to_jpeg_file_in_memory(
		*outData, *outLen, width, height, 3, rgbData, params);
	free(rgbData);
#endif //USE_FFMPEG
}


#if defined(USING_QT_UI)

std::vector<std::string> __qt_getDeviceList() {
	std::vector<std::string> deviceList;
	const QList<QCameraInfo> cameras = QCameraInfo::availableCameras();
	for (const QCameraInfo &cameraInfo : cameras) {
		deviceList.push_back(cameraInfo.deviceName().toStdString()
			+ " (" + cameraInfo.description().toStdString() + ")");
	}
	return deviceList;
}

QList<QVideoFrame::PixelFormat> MyViewfinder::supportedPixelFormats(QAbstractVideoBuffer::HandleType handleType) const {
	Q_UNUSED(handleType);
	// Return the formats you will support
	return QList<QVideoFrame::PixelFormat>()
		<< QVideoFrame::Format_RGB24
		<< QVideoFrame::Format_YUYV
		;
}

bool MyViewfinder::present(const QVideoFrame &frame) {
#ifdef USE_FFMPEG
	if (frame.isValid()) {
		QVideoFrame cloneFrame(frame);
		cloneFrame.map(QAbstractVideoBuffer::ReadOnly);

		unsigned char *jpegData = nullptr;
		int jpegLen = 0;

		QVideoFrame::PixelFormat frameFormat = cloneFrame.pixelFormat();
		if (frameFormat == QVideoFrame::Format_RGB24) {
			convert_frame(cloneFrame.size().width(), cloneFrame.size().height(),
				(unsigned char*)cloneFrame.bits(), AV_PIX_FMT_RGB24,
				qtc_ideal_width, qtc_ideal_height, &jpegData, &jpegLen);

		} else if (frameFormat == QVideoFrame::Format_YUYV) {
			convert_frame(cloneFrame.size().width(), cloneFrame.size().height(),
				(unsigned char*)cloneFrame.bits(), AV_PIX_FMT_YUYV422,
				qtc_ideal_width, qtc_ideal_height, &jpegData, &jpegLen);
		}

		if (jpegData) {
			Camera::pushCameraImage(jpegLen, jpegData);
			free(jpegData);
			jpegData = nullptr;
		}

		cloneFrame.unmap();
		return true;
	}
#endif //USE_FFMPEG
	return false;
}

int __qt_startCapture(int width, int height) {
	if (qt_camera != nullptr) {
		ERROR_LOG(Log::HLE, "camera already started");
		return -1;
	}

	char selectedCamera[80];
	sscanf(g_Config.sCameraDevice.c_str(), "%80s ", &selectedCamera[0]);

	const QList<QCameraInfo> availableCameras = QCameraInfo::availableCameras();
	if (availableCameras.size() < 1) {
		delete qt_camera;
		qt_camera = nullptr;
		ERROR_LOG(Log::HLE, "no camera found");
		return -1;
	}
	for (const QCameraInfo &cameraInfo : availableCameras) {
		if (cameraInfo.deviceName() == selectedCamera) {
			qt_camera = new QCamera(cameraInfo);
		}
	}
	if (qt_camera == nullptr) {
		qt_camera = new QCamera();
		if (qt_camera == nullptr) {
			ERROR_LOG(Log::HLE, "cannot open camera");
			return -1;
		}
	}

	qtc_ideal_width = width;
	qtc_ideal_height = height;

	qt_viewfinder = new MyViewfinder;

	QCameraViewfinderSettings viewfinderSettings = qt_camera->viewfinderSettings();
	viewfinderSettings.setResolution(640, 480);
	viewfinderSettings.setMinimumFrameRate(15.0);
	viewfinderSettings.setMaximumFrameRate(15.0);

	qt_camera->setViewfinderSettings(viewfinderSettings);
	qt_camera->setViewfinder(qt_viewfinder);
	qt_camera->start();

	return 0;
}

int __qt_stopCapture() {
	if (qt_camera != nullptr) {
		qt_camera->stop();
		qt_camera->unload();
		delete qt_camera;
		delete qt_viewfinder;
		qt_camera = nullptr;
	}
	return 0;
}

//endif defined(USING_QT_UI)
#elif PPSSPP_PLATFORM(LINUX) && !PPSSPP_PLATFORM(ANDROID)

std::vector<std::string> __v4l_getDeviceList() {
	std::vector<std::string> deviceList;
	return deviceList;
}

void *v4l_loop(void *data) {
	return nullptr;
}

int __v4l_startCapture(int ideal_width, int ideal_height) {
	return 0;
}

int __v4l_stopCapture() {
	return 0;
}

#endif // PPSSPP_PLATFORM(LINUX) && !PPSSPP_PLATFORM(ANDROID)
