/*
 *  timer.h
 *
 *  Copyright (C) 2024
 *  Terrapane Corporation
 *  All Rights Reserved
 *
 *  Author:
 *      Paul E. Jones <paulej@packetizer.com>
 *
 *  Description:
 *      This file defines the Timer object used to initiate function calls
 *      at a specified time and/or interval.
 *
 *      The Timer will utilize the given ThreadPool, if one is given.  If one
 *      is not specified, the Timer will create its own ThreadPool having
 *      just one thread to service all timers.
 *
 *      To start a timer, call Start().  This will return the TimerID of the
 *      timer.  A zero Timer ID indicates creation failed.  TimerIDs are
 *      monotonically increasing values, though in the very unlikely event that
 *      timer values roll over, zero will be skipped as that value is reserved
 *      to indicate an invalid Timer ID value.  The timer may be one-time timer
 *      or a recurring timer.  Recurring timers are given an interval parameter
 *      that is non-zero.  Recurring timers also utilize a "rigid_interval"
 *      indicator.  This indicator controls whether the timer repeats at the
 *      given interval (regardless of how long the callback function takes, for
 *      example) or if the next iteration starts with respect to the
 *      then-current time.
 *
 *      To stop a timer, call Stop() with the previously returned Timer ID.
 *      It is important that if the timer was created to call into a function
 *      that locks a mutex that the thread calling Stop() is not holding a lock
 *      to that same mutex, as doing so can cause deadlock.  Calling Stop()
 *      on a timer multiple times or on a timer that has completed is
 *      not treated as an error.  If the timer is running (i.e., it is
 *      currently calling the requested function), the thread calling Stop()
 *      will be blocked until the timer completes.  Upon return, the caller
 *      can be guaranteed the timer has been fully stopped and there are no
 *      execution threads running in the TimerEntryPoint function.
 *
 *      If the Timer object is used for real-time applications, one may need
 *      more accurate timing that is afforded by the system's default timing
 *      behavior.  For such applications, set the high_resolution variable
 *      to true in the constructor.  Since use of such timers are rare and
 *      generally have a very specific purpose (e.g., sampling audio every
 *      10ms), it may be best to construct dedicated Timer object with
 *      nullptr passed in the "thread_pool" (a single thread will then
 *      be created to service this Timer) and "high_resolution" set to true.
 *      Testing was performed on a variety of platforms with results proving
 *      to generally be within 0.1ms of accuracy.  Some systems have even
 *      higher levels of accuracy.  Regardless, if one needs a high resolution
 *      timer, it may be best to perform test using the Timer object to see
 *      how well it meets your needs.
 *
 *      Note that it is incumbent on the caller to ensure that calls to
 *      TimerEntryPoint functions do not throw an exception.  Errors should
 *      be caught and handled by that function.  If an exception is thrown, it
 *      will be caught and silently ignored by the Timer object.
 *
 *  Portability Issues:
 *      None.
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <utility>
#ifdef _WIN32
#include <Windows.h>
#endif
#include "thread_pool.h"
#include "thread_control.h"

namespace Terra::Spindle
{

// Define a TimerID type used to identify each set timer
using TimerID = std::uint64_t;

// Timer entry point function definition
using TimerEntryPoint = std::function<void(TimerID timer_id)>;

// The Timer object
class Timer
{
    protected:
        // Number of threads in the default thread pool
        static constexpr std::size_t Default_Thread_Pool_Size{1};

        // When attempting to operate with higher precision, the wait time
        // is reduced iteratively by this factor; higher values generally
        // yield better accuracy, while also increasing CPU usage
        static constexpr double Time_Divisor{1.5};

    public:
        Timer(const ThreadPoolPointer &thread_pool = {},
              bool high_resolution = false);
        virtual ~Timer();

        TimerID Start(const TimerEntryPoint &entry_point,
                      const std::chrono::nanoseconds &delay,
                      const std::chrono::nanoseconds &interval = {},
                      const bool rigid_interval = true,
                      ThreadControlPointer thread_control = {});

        // Used with std::chrono::seconds, milliseconds, etc
        template<typename T, typename U>
        TimerID Start(const TimerEntryPoint &entry_point,
                      const std::chrono::duration<T, U> &delay,
                      const std::chrono::duration<T, U> &interval = {},
                      const bool rigid_interval = true,
                      ThreadControlPointer thread_control = {})
        {
            return Start(entry_point,
                         std::chrono::nanoseconds(delay),
                         std::chrono::nanoseconds(interval),
                         rigid_interval,
                         std::move(thread_control));
        }

        void Stop(TimerID timer_id);

        bool UsingHighResolution() const noexcept;

    protected:
        // Structure to hold requested timer details
        struct TimerDetails
        {
            TimerID timer_id;                   // Timer ID
            std::chrono::steady_clock::time_point next_time;
                                                // Next time timer fires
            std::chrono::nanoseconds interval;  // Timer interval (recurring)
            bool rigid_interval;                // Rigid timer interval
            TimerEntryPoint entry_point;        // Entry point for timer
            ThreadControlPointer timer_control; // Thread control for this timer
        };
        using TimerList = std::deque<TimerDetails>;

        void SortPendingTimers();
        void InvokeThread();
        void ServiceLoop();
        bool RemoveTimer(TimerID timer_id);
        void AwakenWaitingThread();
        bool TestHighAccuracy();
        std::pair<TimerList &,
                  TimerList::iterator> FindTimer(TimerID timer_id);

        bool terminate;                         // Termination flag
        bool high_resolution;                   // Use high resolution timers
#ifdef _WIN32
        HANDLE waitable_timer;                  // Win32 Waitable Timer Handle
#endif
        TimerID last_timer_id;                  // Last TimerID used
        ThreadPoolPointer thread_pool;          // Thread pool to use
        ThreadControlPointer pool_control;      // Controls from thread pool
        std::size_t available_threads;          // Number of threads in pool
        std::size_t invoked_threads;            // How many threads invoked?
        bool waiting_thread;                    // Thread is waiting
        TimerList pending_list;                 // List of pending timers
        TimerList running_list;                 // List of running timers
        std::mutex timer_mutex;                 // Protect object data
        std::condition_variable cv;             // Facilitate thread control
};

} // namespace Terra::Spindle
