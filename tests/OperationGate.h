// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <chrono>
#include <condition_variable>
#include <mutex>

// Pause an external operation until the test explicitly releases it.
struct OperationGate
{
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false, released = false;
    void block()
    {
        std::unique_lock lock(mutex);
        entered = true;
        changed.notify_all();
        changed.wait(lock, [&] { return released; });
    }
    bool wait()
    {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, std::chrono::seconds(2), [&] { return entered; });
    }
    void release()
    {
        std::lock_guard lock(mutex);
        released = true;
        changed.notify_all();
    }
};
