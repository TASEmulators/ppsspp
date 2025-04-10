// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

// Reference : https://stackoverflow.com/questions/6121792/how-to-check-if-a-cpu-supports-the-sse3-instruction-set
#include "ppsspp_config.h"
#if (PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)) && !defined(__EMSCRIPTEN__)

#if defined(CPU_FEATURES_OS_FREEBSD) || defined(CPU_FEATURES_OS_LINUX) || defined(CPU_FEATURES_OS_ANDROID) || defined(CPU_FEATURES_OS_MACOS) || defined(CPU_FEATURES_OS_WINDOWS)
#define USE_CPU_FEATURES 1
#endif

#ifdef __ANDROID__
#include <sys/stat.h>
#include <fcntl.h>
#elif PPSSPP_PLATFORM(MAC)
#include <sys/sysctl.h>
#endif

#include <cstdint>
#include <memory.h>
#include <set>
#include <algorithm>

#include "Common/Common.h"
#include "Common/CPUDetect.h"
#include "Common/File/FileUtil.h"
#include "Common/StringUtils.h"

#if defined(_WIN32)
#include "Common/CommonWindows.h"

#define _interlockedbittestandset workaround_ms_header_bug_platform_sdk6_set
#define _interlockedbittestandreset workaround_ms_header_bug_platform_sdk6_reset
#define _interlockedbittestandset64 workaround_ms_header_bug_platform_sdk6_set64
#define _interlockedbittestandreset64 workaround_ms_header_bug_platform_sdk6_reset64
#include <intrin.h>
#undef _interlockedbittestandset
#undef _interlockedbittestandreset
#undef _interlockedbittestandset64
#undef _interlockedbittestandreset64

void do_cpuidex(u32 regs[4], u32 cpuid_leaf, u32 ecxval) {
	// __cpuidex((int *)regs, cpuid_leaf, ecxval);
}
void do_cpuid(u32 regs[4], u32 cpuid_leaf) {
	// __cpuid((int *)regs, cpuid_leaf);
}

#define do_xgetbv _xgetbv

#else  // _WIN32

#ifdef _M_SSE
#include <emmintrin.h>

static uint64_t do_xgetbv(unsigned int index) {
	unsigned int eax, edx;
	__asm__ __volatile__("xgetbv" : "=a"(eax), "=d"(edx) : "c"(index));
	return ((uint64_t)edx << 32) | eax;
}
#endif  // _M_SSE

#if !PPSSPP_ARCH(MIPS)

void do_cpuidex(u32 regs[4], u32 cpuid_leaf, u32 ecxval) {
}
void do_cpuid(u32 regs[4], u32 cpuid_leaf)
{
}

#endif // !PPSSPP_ARCH(MIPS)

#endif  // !win32

#ifndef _XCR_XFEATURE_ENABLED_MASK
#define _XCR_XFEATURE_ENABLED_MASK 0
#endif

CPUInfo cpu_info;

CPUInfo::CPUInfo() {
	Detect();
}

#if PPSSPP_PLATFORM(LINUX)
static std::vector<int> ParseCPUList(const std::string &filename) {
	std::string data;
	std::vector<int> results;

	if (File::ReadSysTextFileToString(Path(filename), &data)) {
		std::vector<std::string> ranges;
		SplitString(data, ',', ranges);
		for (auto range : ranges) {
			int low = 0, high = 0;
			int parts = sscanf(range.c_str(), "%d-%d", &low, &high);
			if (parts == 1) {
				high = low;
			}
			for (int i = low; i <= high; ++i) {
				results.push_back(i);
			}
		}
	}

	return results;
}
#endif

// Detects the various cpu features
void CPUInfo::Detect() {
		logical_cpu_count = 1;
}

std::vector<std::string> CPUInfo::Features() {
	std::vector<std::string> features;

	struct Flag {
		bool &flag;
		const char *str;
	};
	const Flag list[] = {
		{ bSSE, "SSE" },
		{ bSSE2, "SSE2" },
		{ bSSE3, "SSE3" },
		{ bSSSE3, "SSSE3" },
		{ bSSE4_1, "SSE4.1" },
		{ bSSE4_2, "SSE4.2" },
		{ bSSE4A, "SSE4A" },
		{ HTT, "HTT" },
		{ bAVX, "AVX" },
		{ bAVX2, "AVX2" },
		{ bFMA3, "FMA3" },
		{ bFMA4, "FMA4" },
		{ bAES, "AES" },
		{ bSHA, "SHA" },
		{ bXOP, "XOP" },
		{ bRTM, "TSX" },
		{ bF16C, "F16C" },
		{ bBMI1, "BMI1" },
		{ bBMI2, "BMI2" },
		{ bPOPCNT, "POPCNT" },
		{ bMOVBE, "MOVBE" },
		{ bLZCNT, "LZCNT" },
		{ bLongMode, "64-bit support" },
	};

	for (auto &item : list) {
		if (item.flag) {
			features.push_back(item.str);
		}
	}

	return features;
}

// Turn the cpu info into a string we can show
std::string CPUInfo::Summarize() {
	std::string sum;
	if (num_cores == 1) {
		sum = StringFromFormat("%s, %d core", cpu_string, num_cores);
	} else {
		sum = StringFromFormat("%s, %d cores", cpu_string, num_cores);
		if (HTT)
			sum += StringFromFormat(" (%i logical threads per physical core)", logical_cpu_count);
	}

	auto features = Features();
	for (std::string &feature : features) {
		sum += ", " + feature;
	}
	return sum;
}

#endif // PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)

const char *GetCompilerABI() {
#if PPSSPP_ARCH(ARMV7)
	return "armeabi-v7a";
#elif PPSSPP_ARCH(ARM64)
	return "arm64";
#elif PPSSPP_ARCH(X86)
	return "x86";
#elif PPSSPP_ARCH(AMD64)
	return "x86-64";
#elif PPSSPP_ARCH(RISCV64)
    //https://github.com/riscv/riscv-toolchain-conventions#cc-preprocessor-definitions
    //https://github.com/riscv/riscv-c-api-doc/blob/master/riscv-c-api.md#abi-related-preprocessor-definitions
    #if defined(__riscv_float_abi_single)
        return "lp64f";
    #elif defined(__riscv_float_abi_double)
        return "lp64d";
    #elif defined(__riscv_float_abi_quad)
        return "lp64q";
    #elif defined(__riscv_float_abi_soft)
        return "lp64";
    #endif
#else
	return "other";
#endif
}
