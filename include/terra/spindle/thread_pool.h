/*
 *  thread_pool.h
 *
 *  Copyright (C) 2024
 *  Terrapane Corporation
 *  All Rights Reserved
 *
 *  Author:
 *      Paul E. Jones <paulej@packetizer.com>
 *
 *  Description:
 *      This file defines a thread pool class that allows an application
 *      create a number of threads that can then be invoked to perform
 *      asynchronous tasks while sharing threads from this common pool.
 *      An application may create any number of thread pools.
 *
 *      In order to invoke a thread, one calls the Invoke() function with a
 *      ThreadControl pointer and a routine to call.  Use of a ThreadControl
 *      object is not strictly required.  If the shared pointer doesn't point
 *      at an instance of ThreadControl, then it will not be used.
 *
 *      If the thread pool has an available thread, a thread will be dispatched
 *      immediately when requested.  If there are no threads available, the
 *      request will be queued and serviced once a thread is available.
 *      If one desires to cancel that invocation request before a thread
 *      services the request, one uses the ThreadControl object.  This would
 *      generally be when the invoking object is being destructed.  Note
 *      that another benefit of using the ThreadControl object is that
 *      it will ensure there are no threads still running in the invoking
 *      object.
 *
 *      If a thread invocation results in an exception, it will be caught
 *      and silently ignored.  The called function is expected to handle all
 *      exceptions, as such errors would otherwise go unnoticed.
 *
 *      Spawning new threads can be somewhat expensive, which is one reason
 *      for having a thread pool.  Application should not create and
 *      destroy thread pools frequently, as doing so goes through the expense
 *      of spawning and terminating threads repeated and, thus, defeats the
 *      purpose of using a thread pool.
 *
 *  Portability Issues:
 *      None.
 */

#pragma once

#include <cstddef>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <functional>
#include <vector>
#include <deque>
#include <tuple>
#include <cstdint>
#include <memory>
#include "thread_control.h"

namespace Terra::Spindle
{

// Thread entry point function definition
using ThreadEntryPoint = std::function<void()>;

// Define the ThreadPool class
class ThreadPool
{
    public:
        static constexpr std::size_t Default_Thread_Count{5};

        ThreadPool(std::size_t thread_count = Default_Thread_Count);
        virtual ~ThreadPool();

        bool Invoke(const ThreadControlPointer &thread_control,
                    const ThreadEntryPoint &entry_point);

        std::size_t ThreadCount();
        std::size_t Invoked();
        std::uint64_t TotalInvocations();

    protected:
        void Loop();

        using InvocationList =
            std::deque<std::tuple<ThreadControlPointer, ThreadEntryPoint>>;

        bool terminate;                         // Terminate threads?
        std::size_t invoked;                    // Threads currently invoked
        std::uint64_t total_invocations;        // Total invocations
        std::vector<std::thread> threads;       // Launched threads
        InvocationList invocations;             // Invocation list
        std::mutex thread_pool_mutex;           // Controls protected data
        std::condition_variable cv;             // For signaling threads
};

// Define a shared pointer type for this class
using ThreadPoolPointer = std::shared_ptr<ThreadPool>;

} // namespace Terra::Spindle
