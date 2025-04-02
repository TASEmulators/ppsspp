// Copyright (c) 2012- PPSSPP Project.

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

#include "Common/Log.h"
#include "Common/File/FileUtil.h"
#include "Common/File/DirListing.h"
#include "Core/FileLoaders/LocalFileLoader.h"

#if PPSSPP_PLATFORM(ANDROID)
#include "android/jni/app-android.h"
#endif

#ifdef _WIN32
#if PPSSPP_PLATFORM(UWP)
#include <fileapifromapp.h>
#endif
#else
#include <fcntl.h>
#endif

#ifdef HAVE_LIBRETRO_VFS
#include <streams/file_stream.h>
#endif

// #if !defined(_WIN32) && !defined(HAVE_LIBRETRO_VFS)

// void LocalFileLoader::DetectSizeFd() {
//     jaffarCommon::file::MemoryFile::fseek(handle_, 0, SEEK_END);
// 	filesize_ = jaffarCommon::file::MemoryFile::ftell(handle_);
// 	jaffarCommon::file::MemoryFile::fseek(handle_, 0, SEEK_SET);
// }
// #endif

LocalFileLoader::LocalFileLoader(const Path &filename)
	: filesize_(0), filename_(filename) {
	if (filename.empty()) {
		ERROR_LOG(Log::FileSystem, "LocalFileLoader can't load empty filenames");
		return;
	}

	printf("Local File Loader A - Opening: %s\n", filename.c_str());
    handle_ = _memFileDirectory.fopen(filename.c_str(), "r");
	printf("Handle: %p\n", handle_);
	printf("Local File Loader B\n");
    jaffarCommon::file::MemoryFile::fseek(handle_, 0, SEEK_END);
	printf("Local File Loader C\n");
    filesize_ = jaffarCommon::file::MemoryFile::ftell(handle_);
	printf("Local File Loader D\n");
    jaffarCommon::file::MemoryFile::fseek(handle_, 0, SEEK_SET);
	printf("Local File Loader E\n");
    return;
}

LocalFileLoader::~LocalFileLoader() {
    _memFileDirectory.fclose(handle_);
	handle_ = nullptr;
}

bool LocalFileLoader::Exists() {
	// If we opened it for reading, it must exist.  Done.
    return handle_ != 0;

	return File::Exists(filename_);
}

bool LocalFileLoader::IsDirectory() {
	File::FileInfo info;
	if (File::GetFileInfo(filename_, &info)) {
		return info.exists && info.isDirectory;
	}
	return false;
}

s64 LocalFileLoader::FileSize() {
	return filesize_;
}

size_t LocalFileLoader::ReadAt(s64 absolutePos, size_t bytes, size_t count, void *data, Flags flags) {
	if (bytes == 0)
		return 0;

	if (filesize_ == 0) {
		ERROR_LOG(Log::FileSystem, "ReadAt from 0-sized file: %s", filename_.c_str());
		return 0;
	}

	jaffarCommon::file::MemoryFile::fseek(handle_, absolutePos, SEEK_SET);
	return jaffarCommon::file::MemoryFile::fread(data, bytes, count, handle_);
}
