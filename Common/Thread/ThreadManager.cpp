#include <cstdio>
#include <algorithm>
#include <thread>
#include <deque>
#include <condition_variable>
#include <mutex>
#include <vector>
#include <atomic>

#include "Common/Log.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/Thread/ThreadManager.h"

// Threads and task scheduling
//
// * The threadpool should contain a number of threads that's the the number of cores,
//   plus a fixed number more for I/O-limited background tasks.
// * Parallel compute-limited loops should use as many threads as there are cores.
//   They should always be scheduled to the first N threads.
// * For some tasks, splitting the input values up linearly between the threads
//   is not fair. However, we ignore that for now.

const int MAX_CORES_TO_USE = 16;
const int MIN_IO_BLOCKING_THREADS = 4;
static constexpr size_t TASK_PRIORITY_COUNT = (size_t)TaskPriority::COUNT;

ThreadManager g_threadManager;

struct GlobalThreadContext {
	std::mutex mutex;
	std::deque<Task *> compute_queue[TASK_PRIORITY_COUNT];
	std::atomic<int> compute_queue_size;
	std::deque<Task *> io_queue[TASK_PRIORITY_COUNT];
	std::atomic<int> io_queue_size;
	std::vector<TaskThreadContext *> threads_;

	std::atomic<int> roundRobin;
};

struct TaskThreadContext {
	std::atomic<int> queue_size;
	std::deque<Task *> private_queue[TASK_PRIORITY_COUNT];
	std::thread thread; // the worker thread
	std::condition_variable cond; // used to signal new work
	std::mutex mutex; // protects the local queue.
	int index;
	TaskType type;
	std::atomic<bool> cancelled;
	char name[16];
};

ThreadManager::ThreadManager() : global_(new GlobalThreadContext()) {
	global_->compute_queue_size = 0;
	global_->io_queue_size = 0;
	global_->roundRobin = 0;
	numComputeThreads_ = 1;
	numThreads_ = 1;
}

ThreadManager::~ThreadManager() {
	delete global_;
}

void ThreadManager::Teardown() {
}

bool ThreadManager::TeardownTask(Task *task, bool enqueue) {
	return true;
}

void ThreadManager::Init(int numRealCores, int numLogicalCoresPerCpu) {
}

void ThreadManager::EnqueueTask(Task *task) {
	// Serialization: run task directly, without invoking a thread
	task->Run();
	task->Release();
	return;
}

void ThreadManager::EnqueueTaskOnThread(int threadNum, Task *task) {
	// Serialization: run task directly, without invoking a thread
	task->Run();
	task->Release();
	return;
}

int ThreadManager::GetNumLooperThreads() const {
	return numComputeThreads_;
}

void ThreadManager::TryCancelTask(uint64_t taskID) {
	// Do nothing for now, just let it finish.
}

bool ThreadManager::IsInitialized() const {
	return !global_->threads_.empty();
}
