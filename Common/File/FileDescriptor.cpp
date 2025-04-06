#include "ppsspp_config.h"

#include <errno.h>
#include <cmath>
#include <cstdio>

#include "Common/CommonTypes.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/File/FileDescriptor.h"
#include "Common/Log.h"

namespace fd_util {

// Slow as hell and should only be used for prototyping.
// Reads from a socket, up to an '\n'. This means that if the line ends
// with '\r', the '\r' will be returned.
size_t ReadLine(int fd, char *vptr, size_t buf_size) {
	return 0;
}

// Misnamed, it just writes raw data in a retry loop.
size_t WriteLine(int fd, const char *vptr, size_t n) {
	return 0;
}

size_t WriteLine(int fd, const char *buffer) {
	return WriteLine(fd, buffer, strlen(buffer));
}

size_t Write(int fd, const std::string &str) {
	return WriteLine(fd, str.c_str(), str.size());
}

bool WaitUntilReady(int fd, double timeout, bool for_write) {
	return false;
}

void SetNonBlocking(int sock, bool non_blocking) {
}

std::string GetLocalIP(int sock) {
	return "";
}

}  // fd_util
