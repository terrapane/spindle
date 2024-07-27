/*
 *  thread_control.h
 *
 *  Copyright (C) 2024
 *  Terrapane Corporation
 *  All Rights Reserved
 *
 *  Author:
 *      Paul E. Jones <paulej@packetizer.com>
 *
 *  Description:
 *      This file defines the class ThreadControl, which controls threads
 *      of execution through code.  Threads can call into an instance of this
 *      object to ensure it is safe to carry out a task, indicate it is
 *      currently running, and indicate that it has completed a task.
 *
 *  Portability Issues:
 *      None.
 */

#pragma once

#include <mutex>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <cstdint>

namespace Terra::Spindle
{

// Define the ThreadControl class
class ThreadControl
{
    public:
        ThreadControl();
        ~ThreadControl();
        bool BeginWork();
        void FinishWork();
        void Halt();
        void Halt(std::unique_lock<std::mutex> &foreign_lock);
        void Resume();
        bool IsHalted();
        std::uint64_t RunningThreads();

    protected:
        void NotifyThreads();

        // Count of running threads
        std::atomic<std::uint64_t> running_threads;

        // True is the object is in a halted state
        std::atomic<bool> halted;

        // Mutex to facilitate thread synchronization
        std::mutex thread_control_mutex;

        // Condition variable to facilitate communication between threads
        std::condition_variable cv;
};

// Define a shared pointer type for this class
using ThreadControlPointer = std::shared_ptr<ThreadControl>;

} // namespace Terra::Spindle
