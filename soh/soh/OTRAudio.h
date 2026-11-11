#pragma once
#include <atomic>
#include <thread>
#include <condition_variable>

static struct {
    std::thread thread;
    std::condition_variable cv_to_thread, cv_from_thread;
    std::mutex mutex;
    std::atomic<bool> running{ false };
    bool processing;
} audio;
