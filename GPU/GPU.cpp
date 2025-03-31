// Copyright (c) 2015- PPSSPP Project.

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

#include "Common/TimeUtil.h"
#include "Common/GraphicsContext.h"
#include "Core/Core.h"
#include "Core/System.h"

#include "GPU/GPU.h"
#include "GPU/GPUCommon.h"

#if PPSSPP_API(ANY_GL)
// #include "GPU/GLES/GPU_GLES.h"
#endif
// #include "GPU/Vulkan/GPU_Vulkan.h"
#include "GPU/Software/SoftGpu.h"

#if PPSSPP_API(D3D9)
#include "GPU/Directx9/GPU_DX9.h"
#endif

#if PPSSPP_API(D3D11)
#include "GPU/D3D11/GPU_D3D11.h"
#endif

GPUStatistics gpuStats;
GPUCommon *gpu;
GPUDebugInterface *gpuDebug;

template <typename T>
static void SetGPU(T *obj) {
	gpu = obj;
	gpuDebug = obj;
}

#ifdef USE_CRT_DBG
#undef new
#endif

bool GPU_IsReady() {
	return gpu != nullptr;
}

bool GPU_IsStarted() {
	if (gpu)
		return gpu->IsStarted();
	return false;
}

bool GPU_Init(GraphicsContext *ctx, Draw::DrawContext *draw) {
	const auto &gpuCore = PSP_CoreParameter().gpuCore;

	if (gpu && !gpu->IsStarted())
		SetGPU<SoftGPU>(nullptr);

	return gpu != nullptr;
}
#ifdef USE_CRT_DBG
#define new DBG_NEW
#endif

void GPU_Shutdown() {

	delete gpu;
	gpu = nullptr;
}

const char *RasterChannelToString(RasterChannel channel) {
	return channel == RASTER_COLOR ? "COLOR" : "DEPTH";
}
