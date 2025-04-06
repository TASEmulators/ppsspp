// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#if defined(_MSC_VER)
#pragma warning(disable:4091)  // workaround bug in VS2015 headers
#ifndef UNICODE
#error Win32 build requires a unicode build
#endif
#else
#define _POSIX_SOURCE
#define _LARGE_TIME_API
#endif

#include "ppsspp_config.h"

#include "android/jni/app-android.h"

#include <cstring>
#include <ctime>
#include <memory>

#include <sys/types.h>

#include "Common/Log.h"
#include "Common/LogReporting.h"
#include "Common/File/FileUtil.h"
#include "Common/StringUtils.h"
#include "Common/TimeUtil.h"
#include "Common/SysError.h"

#if defined(__DragonFly__) || defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || defined(__NetBSD__)
#include <sys/sysctl.h>		// KERN_PROC_PATHNAME
#endif

#if defined(__APPLE__)
#include <CoreFoundation/CFString.h>
#include <CoreFoundation/CFURL.h>
#include <CoreFoundation/CFBundle.h>
#if !PPSSPP_PLATFORM(IOS)
#include <mach-o/dyld.h>
#endif  // !PPSSPP_PLATFORM(IOS)
#endif  // __APPLE__

#include "Common/Data/Encoding/Utf8.h"
#include <sys/stat.h>


// NOTE: There's another one in DirListing.cpp.
#ifdef _WIN32
constexpr bool SIMULATE_SLOW_IO = false;
#else
constexpr bool SIMULATE_SLOW_IO = false;
#endif
constexpr bool LOG_IO = false;

#ifndef S_ISDIR
#define S_ISDIR(m)  (((m)&S_IFMT) == S_IFDIR)
#endif

#if !defined(__linux__) && !defined(_WIN32) && !defined(__QNX__)
#define stat64 stat
#define fstat64 fstat
#endif

#define DIR_SEP "/"
#ifdef _WIN32
#define DIR_SEP_CHRS "/\\"
#else
#define DIR_SEP_CHRS "/"
#endif

// This namespace has various generic functions related to files and paths.
// The code still needs a ton of cleanup.
// REMEMBER: strdup considered harmful!
namespace File {

	jaffarCommon::file::MemoryFile* OpenCFile(const Path& path, const char* mode) {
		return _memFileDirectory.fopen(path.c_str(), mode);
	}

	static std::string OpenFlagToString(OpenFlag flags) {
		std::string s;
		if (flags & OPEN_READ)
			s += "READ|";
		if (flags & OPEN_WRITE)
			s += "WRITE|";
		if (flags & OPEN_APPEND)
			s += "APPEND|";
		if (flags & OPEN_CREATE)
			s += "CREATE|";
		if (flags & OPEN_TRUNCATE)
			s += "TRUNCATE|";
		if (!s.empty()) {
			s.pop_back();  // Remove trailing separator.
		}
		return s;
	}

	int OpenFD(const Path& path, OpenFlag flags) {

		return -1;
	}

	std::string ResolvePath(const std::string& path) {
		return path;
	}

	static int64_t RecursiveSize(const Path& path) {
		// TODO: Some file systems can optimize this.
		std::vector<FileInfo> fileInfo;
		if (!GetFilesInDir(path, &fileInfo, nullptr, GETFILES_GETHIDDEN)) {
			return -1;
		}
		int64_t sizeSum = 0;
		for (const auto& file : fileInfo) {
			if (file.isDirectory) {
				sizeSum += RecursiveSize(file.fullName);
			}
			else {
				sizeSum += file.size;
			}
		}
		return sizeSum;
	}

	uint64_t ComputeRecursiveDirectorySize(const Path& path) {
		if (path.Type() == PathType::CONTENT_URI) {
			return Android_ComputeRecursiveDirectorySize(path.ToString());
		}

		// Generic solution.
		return RecursiveSize(path);
	}

	// Returns true if file filename exists. Will return true on directories.
	bool ExistsInDir(const Path& path, const std::string& filename) {
		return Exists(path / filename);
	}

	bool Exists(const Path& path) {
		return true;
	}

	// Returns true if filename exists and is a directory
	bool IsDirectory(const Path& path) {
		return false;
	}

	// Deletes a given filename, return true on success
	// Doesn't supports deleting a directory
	bool Delete(const Path& filename) {

		return false;
	}

	// Returns true if successful, or path already exists.
	bool CreateDir(const Path& path) {
		return false;
	}

	// Creates the full path of fullPath returns true on success
	bool CreateFullPath(const Path& path) {
		if (File::Exists(path)) {
			DEBUG_LOG(Log::Common, "CreateFullPath: path exists %s", path.ToVisualString().c_str());
			return true;
		}

		switch (path.Type()) {
		case PathType::NATIVE:
		case PathType::CONTENT_URI:
			break; // OK
		default:
			ERROR_LOG(Log::Common, "CreateFullPath(%s): Not yet supported", path.ToVisualString().c_str());
			return false;
		}

		// The below code is entirely agnostic of path format.

		Path root = path.GetRootVolume();

		std::string diff;
		if (!root.ComputePathTo(path, diff)) {
			return false;
		}

		std::vector<std::string_view> parts;
		SplitString(diff, '/', parts);

		// Probably not necessary sanity check, ported from the old code.
		if (parts.size() > 100) {
			ERROR_LOG(Log::Common, "CreateFullPath: directory structure too deep");
			return false;
		}

		Path curPath = root;
		for (auto part : parts) {
			curPath /= part;
			File::CreateDir(curPath);
		}

		return true;
	}

	// renames file srcFilename to destFilename, returns true on success
	bool Rename(const Path& srcFilename, const Path& destFilename) {
		if (LOG_IO) {
			INFO_LOG(Log::System, "Rename %s -> %s", srcFilename.c_str(), destFilename.c_str());
		}
		if (SIMULATE_SLOW_IO) {
			sleep_ms(100, "slow-io-sim");
		}

		if (srcFilename.Type() != destFilename.Type()) {
			// Impossible. You're gonna need to make a copy, and delete the original. Not the responsibility
			// of Rename.
			return false;
		}

		// We've already asserted that they're the same Type, so only need to check either src or dest.
		switch (srcFilename.Type()) {
		case PathType::NATIVE:
			// OK, proceed with the regular code.
			break;
		case PathType::CONTENT_URI:
			// Content URI: Can only rename if in the same folder.
			// TODO: Fallback to move + rename? Or do we even care about that use case? We have MoveIfFast for such tricks.
			if (srcFilename.GetDirectory() != destFilename.GetDirectory()) {
				INFO_LOG(Log::Common, "Content URI rename: Directories not matching, failing. %s --> %s", srcFilename.c_str(), destFilename.c_str());
				return false;
			}
			INFO_LOG(Log::Common, "Content URI rename: %s --> %s", srcFilename.c_str(), destFilename.c_str());
			return Android_RenameFileTo(srcFilename.ToString(), destFilename.GetFilename()) == StorageError::SUCCESS;
		default:
			return false;
		}

		INFO_LOG(Log::Common, "Rename: %s --> %s", srcFilename.c_str(), destFilename.c_str());

#if defined(_WIN32) && defined(UNICODE)
#if PPSSPP_PLATFORM(UWP)
		if (MoveFileFromAppW(srcFilename.ToWString().c_str(), destFilename.ToWString().c_str()))
			return true;
#else
		std::wstring srcw = srcFilename.ToWString();
		std::wstring destw = destFilename.ToWString();
		if (_wrename(srcw.c_str(), destw.c_str()) == 0)
			return true;
#endif
#else
		if (rename(srcFilename.c_str(), destFilename.c_str()) == 0)
			return true;
#endif

		ERROR_LOG(Log::Common, "Rename: failed %s --> %s: %s",
			srcFilename.c_str(), destFilename.c_str(), GetLastErrorMsg().c_str());
		return false;
	}

	// copies file srcFilename to destFilename, returns true on success
	bool Copy(const Path& srcFilename, const Path& destFilename) {
		if (LOG_IO) {
			INFO_LOG(Log::System, "Copy %s -> %s", srcFilename.c_str(), destFilename.c_str());
		}
		if (SIMULATE_SLOW_IO) {
			sleep_ms(100, "slow-io-sim");
		}
		switch (srcFilename.Type()) {
		case PathType::NATIVE:
			break; // OK
		case PathType::CONTENT_URI:
			if (destFilename.Type() == PathType::CONTENT_URI && destFilename.CanNavigateUp()) {
				Path destParent = destFilename.NavigateUp();
				// Use native file copy.
				if (Android_CopyFile(srcFilename.ToString(), destParent.ToString()) == StorageError::SUCCESS) {
					return true;
				}
				INFO_LOG(Log::Common, "Android_CopyFile failed, falling back.");
				// Else fall through, and try using file I/O.
			}
			break;
		default:
			return false;
		}

		INFO_LOG(Log::Common, "Copy by OpenCFile: %s --> %s", srcFilename.c_str(), destFilename.c_str());

		// buffer size
#define BSIZE 16384

		char buffer[BSIZE];

		// Open input file
		auto input = OpenCFile(srcFilename, "rb");
		if (!input) {
			ERROR_LOG(Log::Common, "Copy: input failed %s --> %s: %s",
				srcFilename.c_str(), destFilename.c_str(), GetLastErrorMsg().c_str());
			return false;
		}

		// open output file
		auto output = OpenCFile(destFilename, "wb");
		if (!output) {
			_memFileDirectory.fclose(input);
			ERROR_LOG(Log::Common, "Copy: output failed %s --> %s: %s",
				srcFilename.c_str(), destFilename.c_str(), GetLastErrorMsg().c_str());
			return false;
		}

		int bytesWritten = 0;

		// copy loop
		while (!jaffarCommon::file::MemoryFile::feof(input)) {
			// read input
			int rnum = jaffarCommon::file::MemoryFile::fread(buffer, sizeof(char), BSIZE, input);
			// if (rnum != BSIZE) {
			// 	if (jaffarCommon::file::MemoryFile:::ferror(input) != 0) {
			// 		ERROR_LOG(Log::Common,
			// 				"Copy: failed reading from source, %s --> %s: %s",
			// 				srcFilename.c_str(), destFilename.c_str(), GetLastErrorMsg().c_str());
			// 		_memFileDirectory.fclose(input);
			// 		_memFileDirectory.fclose(output);
			// 		return false;
			// 	}
			// }

			// write output
			int wnum = jaffarCommon::file::MemoryFile::fwrite(buffer, sizeof(char), rnum, output);
			if (wnum != rnum) {
				ERROR_LOG(Log::Common,
					"Copy: failed writing to output, %s --> %s: %s",
					srcFilename.c_str(), destFilename.c_str(), GetLastErrorMsg().c_str());
				_memFileDirectory.fclose(input);
				_memFileDirectory.fclose(output);
				return false;
			}

			bytesWritten += wnum;
		}

		if (bytesWritten == 0) {
			WARN_LOG(Log::Common, "Copy: No bytes written (must mean that input was empty)");
		}

		// close flushes
		_memFileDirectory.fclose(input);
		_memFileDirectory.fclose(output);
		return true;
	}

	// Will overwrite the target.
	bool Move(const Path& srcFilename, const Path& destFilename) {
		if (SIMULATE_SLOW_IO) {
			sleep_ms(100, "slow-io-sim");
			INFO_LOG(Log::System, "Move %s -> %s", srcFilename.c_str(), destFilename.c_str());
		}
		bool fast = MoveIfFast(srcFilename, destFilename);
		if (fast) {
			return true;
		}
		// OK, that failed, so fall back on a copy.
		if (Copy(srcFilename, destFilename)) {
			return Delete(srcFilename);
		}
		else {
			return false;
		}
	}

	bool MoveIfFast(const Path& srcFilename, const Path& destFilename) {
		if (srcFilename.Type() != destFilename.Type()) {
			// No way it's gonna work.
			return false;
		}

		// Only need to check one type here, due to the above check.
		if (srcFilename.Type() == PathType::CONTENT_URI && srcFilename.CanNavigateUp() && destFilename.CanNavigateUp()) {
			if (srcFilename.GetFilename() == destFilename.GetFilename()) {
				Path srcParent = srcFilename.NavigateUp();
				Path dstParent = destFilename.NavigateUp();
				return Android_MoveFile(srcFilename.ToString(), srcParent.ToString(), dstParent.ToString()) == StorageError::SUCCESS;
				// If failed, fall through and try other ways.
			}
			else {
				// We do not handle simultaneous renames here.
				return false;
			}
		}

		// Try a traditional rename operation.
		return Rename(srcFilename, destFilename);
	}

	// Returns the size of file (64bit)
	// TODO: Add a way to return an error.
	uint64_t GetFileSize(const Path& filename) {
		return 0;
	}

	uint64_t GetFileSize(auto f) {
		uint64_t pos = jaffarCommon::file::MemoryFile::ftell(f);
		if (jaffarCommon::file::MemoryFile::fseek(f, 0, SEEK_END) != 0) {
			return 0;
		}
		uint64_t size = jaffarCommon::file::MemoryFile::ftell(f);
		// Reset the seek position to where it was when we started.
		if (size != pos && jaffarCommon::file::MemoryFile::fseek(f, pos, SEEK_SET) != 0) {
			// Should error here.
			return 0;
		}
		if (size == -1)
			return 0;
		return size;
	}

	// creates an empty file filename, returns true on success
	bool CreateEmptyFile(const Path& filename) {
		INFO_LOG(Log::Common, "CreateEmptyFile: %s", filename.c_str());
		auto pFile = OpenCFile(filename, "wb");
		if (!pFile) {
			ERROR_LOG(Log::Common, "CreateEmptyFile: failed to create '%s': %s", filename.c_str(), GetLastErrorMsg().c_str());
			return false;
		}
		_memFileDirectory.fclose(pFile);
		return true;
	}

	// Deletes an empty directory, returns true on success
	// WARNING: On Android with content URIs, it will delete recursively!
	bool DeleteDir(const Path& path) {
		return false;
	}

	// Deletes the given directory and anything under it. Returns true on success.
	bool DeleteDirRecursively(const Path& path) {
		switch (path.Type()) {
		case PathType::NATIVE:
			break;
		case PathType::CONTENT_URI:
			// We make use of the dangerous auto-recursive property of Android_RemoveFile.
			return Android_RemoveFile(path.ToString()) == StorageError::SUCCESS;
		default:
			ERROR_LOG(Log::Common, "DeleteDirRecursively: Path type not supported");
			return false;
		}

		std::vector<FileInfo> files;
		GetFilesInDir(path, &files, nullptr, GETFILES_GETHIDDEN);
		for (const auto& file : files) {
			if (file.isDirectory) {
				DeleteDirRecursively(file.fullName);
			}
			else {
				Delete(file.fullName);
			}
		}
		return DeleteDir(path);
	}

	bool OpenFileInEditor(const Path& fileName) {
		return false;
	}

	const Path GetCurDirectory() {
		return Path(".");
	}

	const Path& GetExeDirectory() {
		static Path ExePath;


		return ExePath;
	}


	IOFile::IOFile(const Path& filename, const char openmode[]) {
		Open(filename, openmode);
	}

	IOFile::~IOFile() {
		Close();
	}

	bool IOFile::Open(const Path& filename, const char openmode[])
	{
		Close();
		m_file = File::OpenCFile(filename, openmode);
		m_good = IsOpen();
		return m_good;
	}

	bool IOFile::Close()
	{
		if (!IsOpen() || 0 != _memFileDirectory.fclose(m_file))
			m_good = false;

		m_file = NULL;
		return m_good;
	}

	jaffarCommon::file::MemoryFile* IOFile::ReleaseHandle()
	{
		jaffarCommon::file::MemoryFile* const ret = m_file;
		m_file = NULL;
		return ret;
	}

	void IOFile::SetHandle(jaffarCommon::file::MemoryFile* file)
	{
		Close();
		Clear();
		m_file = file;
	}

	uint64_t IOFile::GetSize()
	{
		if (IsOpen())
			return File::GetFileSize(m_file);
		else
			return 0;
	}

	bool IOFile::Seek(int64_t off, int origin)
	{
		if (!IsOpen() || 0 != jaffarCommon::file::MemoryFile::fseek(m_file, off, origin))
			m_good = false;

		return m_good;
	}

	uint64_t IOFile::Tell()
	{
		if (IsOpen())
			return jaffarCommon::file::MemoryFile::ftell(m_file);
		else
			return -1;
	}

	bool IOFile::Flush()
	{
		if (!IsOpen() || 0 != jaffarCommon::file::MemoryFile::fflush(m_file))
			m_good = false;

		return m_good;
	}

	bool IOFile::Resize(uint64_t size)
	{
		// 	if (!IsOpen() || 0 !=
		// #ifdef _WIN32
		// 		// ector: _chsize sucks, not 64-bit safe
		// 		// F|RES: changed to _chsize_s. i think it is 64-bit safe
		// 		_chsize_s(_fileno(m_file), size)
		// #else
		// 		// TODO: handle 64bit and growing
		// 		jaffarCommon::file::MemoryFile::ftruncate(m_file, size)
		// #endif
		// 	)
		m_good = false;

		return m_good;
	}

	bool ReadFileToStringOptions(bool textFile, bool allowShort, const Path& filename, std::string* str) {
		auto f = File::OpenCFile(filename, textFile ? "r" : "rb");
		if (!f)
			return false;
		// Warning: some files, like in /sys/, may return a fixed size like 4096.
		size_t len = (size_t)File::GetFileSize(f);
		bool success;
		if (len == 0) {
			// Just read until we can't read anymore.
			size_t totalSize = 1024;
			size_t totalRead = 0;
			do {
				totalSize *= 2;
				str->resize(totalSize);
				totalRead += jaffarCommon::file::MemoryFile::fread(&(*str)[totalRead], 1, totalSize - totalRead, f);
			} while (totalRead == totalSize);
			str->resize(totalRead);
			success = true;
		}
		else {
			str->resize(len);
			size_t totalRead = jaffarCommon::file::MemoryFile::fread(&(*str)[0], 1, len, f);
			str->resize(totalRead);
			// Allow less, because some system files will report incorrect lengths.
			// Also, when reading text with CRLF, the read length may be shorter.
			if (textFile) {
				// totalRead doesn't take \r into account since they might be skipped in this mode.
				// So let's just ask how far the cursor got.
				totalRead = jaffarCommon::file::MemoryFile::ftell(f);
			}
			success = allowShort ? (totalRead <= len) : (totalRead == len);
		}
		_memFileDirectory.fclose(f);
		return success;
	}

	uint8_t* ReadLocalFile(const Path& filename, size_t* size) {
		printf("READ LOCAL FILE A\n");
		auto file = File::OpenCFile(filename, "rb");
		printf("READ LOCAL FILE A2\n");
		if (!file) {
			*size = 0;
			printf("READ LOCAL FILE A3\n");
			return nullptr;
		}
		printf("READ LOCAL FILE B\n");
		jaffarCommon::file::MemoryFile::fseek(file, 0, SEEK_END);
		size_t f_size = jaffarCommon::file::MemoryFile::ftell(file);
		if ((long)f_size < 0) {
			*size = 0;
			_memFileDirectory.fclose(file);
			return nullptr;
		}
		printf("READ LOCAL FILE C\n");
		jaffarCommon::file::MemoryFile::fseek(file, 0, SEEK_SET);
		// NOTE: If you find ~10 memory leaks from here, with very varying sizes, it might be the VFPU LUTs.
		uint8_t* contents = new uint8_t[f_size + 1];
		if (jaffarCommon::file::MemoryFile::fread(contents, 1, f_size, file) != f_size) {
			delete[] contents;
			contents = nullptr;
			*size = 0;
			printf("READ LOCAL FILE D\n");
		}
		else {
			contents[f_size] = 0;
			*size = f_size;
			printf("READ LOCAL FILE E\n");
		}
		printf("READ LOCAL FILE F\n");
		_memFileDirectory.fclose(file);
		printf("READ LOCAL FILE G\n");
		return contents;
	}

	bool WriteStringToFile(bool text_file, const std::string& str, const Path& filename) {
		auto f = File::OpenCFile(filename, text_file ? "w" : "wb");
		if (!f)
			return false;
		size_t len = str.size();
		if (len != jaffarCommon::file::MemoryFile::fwrite(str.data(), 1, str.size(), f))
		{
			_memFileDirectory.fclose(f);
			return false;
		}
		_memFileDirectory.fclose(f);
		return true;
	}

	bool WriteDataToFile(bool text_file, const void* data, size_t size, const Path& filename) {
		auto f = File::OpenCFile(filename, text_file ? "w" : "wb");
		if (!f)
			return false;
		if (size != jaffarCommon::file::MemoryFile::fwrite(data, 1, size, f))
		{
			_memFileDirectory.fclose(f);
			return false;
		}
		_memFileDirectory.fclose(f);
		return true;
	}

	void ChangeMTime(const Path& path, time_t mtime) {
	}

	bool IsProbablyInDownloadsFolder(const Path& filename) {
		INFO_LOG(Log::Common, "IsProbablyInDownloadsFolder: Looking at %s (%s)...", filename.c_str(), filename.ToVisualString().c_str());
		return filename.FilePathContainsNoCase("download");
	}

}  // namespace File
