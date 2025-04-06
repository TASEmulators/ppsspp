#include <cstring>
#include <set>

#include "ppsspp_config.h"

#include "Common/File/PathBrowser.h"
#include "Common/File/FileUtil.h"
#include "Common/File/DirListing.h"
#include "Common/StringUtils.h"
#include "Common/TimeUtil.h"
#include "Common/Log.h"
#include "Common/Thread/ThreadUtil.h"

#if PPSSPP_PLATFORM(ANDROID)
#include "android/jni/app-android.h"
#endif

bool LoadRemoteFileList(const Path &url, const std::string &userAgent, bool *cancel, std::vector<File::FileInfo> &files) {
  return false;
}

PathBrowser::~PathBrowser() {
	{
		std::unique_lock<std::mutex> guard(pendingLock_);
		pendingCancel_ = true;
		pendingStop_ = true;
		pendingCond_.notify_all();
	}
	if (pendingThread_.joinable()) {
		pendingThread_.join();
	}
}

void PathBrowser::SetPath(const Path &path) {
	path_ = path;
	ApplyRestriction();
	HandlePath();
}

void PathBrowser::RestrictToRoot(const Path &root) {
	INFO_LOG(Log::System, "Restricting to root: %s", root.c_str());
	restrictedRoot_ = root;
}

void PathBrowser::HandlePath() {
	if (!path_.empty() && path_.ToString()[0] == '!') {
		if (pendingActive_)
			ResetPending();
		ready_ = true;
		return;
	}

	std::lock_guard<std::mutex> guard(pendingLock_);
	ready_ = false;
	pendingActive_ = true;
	pendingCancel_ = false;
	pendingFiles_.clear();
	pendingPath_ = path_;
	pendingCond_.notify_all();

	if (pendingThread_.joinable())
		return;

	pendingThread_ = std::thread([&] {
		SetCurrentThreadName("PathBrowser");

		AndroidJNIThreadContext jniContext;  // destructor detaches

		std::unique_lock<std::mutex> guard(pendingLock_);
		std::vector<File::FileInfo> results;
		Path lastPath("NONSENSE THAT WONT EQUAL A PATH");
		while (!pendingStop_) {
			while (lastPath == pendingPath_ && !pendingCancel_) {
				pendingCond_.wait(guard);
			}
			if (pendingStop_) {
				break;
			}
			lastPath = pendingPath_;
			if (lastPath.Type() == PathType::HTTP) {
				guard.unlock();
				results.clear();
				success_ = LoadRemoteFileList(lastPath, userAgent_, &pendingCancel_, results);
				guard.lock();
			} else if (lastPath.empty()) {
				results.clear();
				success_ = true;
			} else {
				guard.unlock();
				results.clear();
				success_ = File::GetFilesInDir(lastPath, &results, nullptr);
				if (!success_) {
					WARN_LOG(Log::IO, "PathBrowser: Failed to list directory: %s", lastPath.c_str());
				}
				guard.lock();
			}

			if (pendingPath_ == lastPath) {
				if (success_ && !pendingCancel_) {
					pendingFiles_ = results;
				}
				pendingPath_.clear();
				lastPath.clear();
				ready_ = true;
			}
		}
	});
}

void PathBrowser::ResetPending() {
	std::lock_guard<std::mutex> guard(pendingLock_);
	pendingCancel_ = true;
	pendingPath_.clear();
}

std::string PathBrowser::GetFriendlyPath() const {
	// Show relative to memstick root if there.
	if (path_.StartsWith(aliasMatch_)) {
		std::string p;
		if (aliasMatch_.ComputePathTo(path_, p)) {
			return aliasDisplay_ + p;
		}
		std::string str = path_.ToString();
		if (aliasMatch_.size() < str.length()) {
			return aliasDisplay_ + str.substr(aliasMatch_.size());
		} else {
			return aliasDisplay_;
		}
	}

    std::string str = path_.ToString();
#if !PPSSPP_PLATFORM(ANDROID) && (PPSSPP_PLATFORM(LINUX) || PPSSPP_PLATFORM(MAC))
	char *home = getenv("HOME");
	if (home != nullptr && !strncmp(str.c_str(), home, strlen(home))) {
		return std::string("~") + str.substr(strlen(home));
	}
#endif
	return path_.ToVisualString();
}

bool PathBrowser::GetListing(std::vector<File::FileInfo> &fileInfo, const char *extensionFilter, bool *cancel) {
	std::unique_lock<std::mutex> guard(pendingLock_);
	while (!IsListingReady() && (!cancel || !*cancel)) {
		// In case cancel changes, just sleep. TODO: Replace with condition variable.
		guard.unlock();
		sleep_ms(50, "pathbrowser-poll");
		guard.lock();
	}

	fileInfo = ApplyFilter(pendingFiles_, extensionFilter, "");
	return true;
}

void PathBrowser::ApplyRestriction() {
	if (!path_.StartsWith(restrictedRoot_) && !startsWith(path_.ToString(), "!")) {
		WARN_LOG(Log::System, "Applying path restriction: %s (%s didn't match)", restrictedRoot_.c_str(), path_.c_str());
		path_ = restrictedRoot_;
	}
}

bool PathBrowser::CanNavigateUp() {
	if (path_ == restrictedRoot_) {
		return false;
	}
	return path_.CanNavigateUp();
}

void PathBrowser::NavigateUp() {
	_dbg_assert_(CanNavigateUp());
	path_ = path_.NavigateUp();
	ApplyRestriction();
}

// TODO: Support paths like "../../hello"
void PathBrowser::Navigate(const std::string &path) {
	if (path == ".")
		return;
	if (path == "..") {
		NavigateUp();
	} else {
		if (path.size() >= 2 && path[1] == ':' && path_.IsRoot())
			path_ = Path(path);
		else
			path_ = path_ / path;
	}
	HandlePath();
}
