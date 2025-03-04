/*
 *  spinlock.h
 *
 *  Copyright (C) 2025
 *  Terrapane Corporation
 *  All Rights Reserved
 *
 *  Author:
 *      Paul E. Jones <paulej@packetizer.com>
 *
 *  Description:
 *      This file defines a header-only class that implements a spinlock that
 *      can be used as an alternative to using a mutex for thread
 *      synchronization.  A spinlock is designed to be very fast at locking
 *      and unlocking, but the most distinctive attribute is that if it
 *      cannot immediately lock it will sit in a loop "spinning" trying to
 *      get the lock.  This means that for applications where there is a
 *      high probability of resource conflicts, a spinlock is a bad choice.
 *      However, for applications that need a lock where conflicts are
 *      very unlikely and (importantly) when any delay in waiting to acquire a
 *      lock would be extremely brief, a spinlock can be an excellent
 *      alternative to std::mutex.
 *
 *      The Spinlock class implements the C++ named requirement "Lockable"
 *      (https://en.cppreference.com/w/cpp/named_req/Lockable).
 *
 *  Portability Issues:
 *      None.
 */

#pragma once

#include <atomic>
#include <thread>

namespace Terra::Spindle
{

class Spinlock
{
    public:
        Spinlock() = default;
        ~Spinlock() = default;

        // Do now allow copies or moves
        Spinlock(Spinlock &) = delete;
        Spinlock(Spinlock &&) = delete;
        Spinlock &operator=(const Spinlock &) = delete;
        Spinlock &operator=(Spinlock &&) = delete;

        /*
         *  lock()
         *
         *  Description:
         *      Lock the spinlock, spinning until the lock is acquired.
         *
         *  Parameters:
         *      None.
         *
         *  Returns:
         *      Nothing.
         *
         *  Comments:
         *      None.
         */
        void lock()
        {
            // Continuously loop until the lock is acquired
            while (flag.test_and_set(std::memory_order_acquire))
            {
                // Yield to other threads if the lock fails
                std::this_thread::yield();
            }
        }

        /*
         *  try_lock()
         *
         *  Description:
         *      Attempt to lock the spinlock, but try only once and return
         *      a result indicating success or failure.
         *
         *  Parameters:
         *      None.
         *
         *  Returns:
         *      True if the lock was acquired, false if not.
         *
         *  Comments:
         *      None.
         */
        bool try_lock()
        {
            return !flag.test_and_set(std::memory_order_acquire);
        }

        /*
         *  unlock()
         *
         *  Description:
         *      Unlock the spinlock.
         *
         *  Parameters:
         *      None.
         *
         *  Returns:
         *      Nothing.
         *
         *  Comments:
         *      None.
         */
        void unlock() { flag.clear(std::memory_order_release); }

    protected:
        std::atomic_flag flag;
};

} // namespace Terra::Spindle
