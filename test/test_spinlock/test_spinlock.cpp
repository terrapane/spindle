/*
 *  test_spinlock.cpp
 *
 *  Copyright (C) 2025
 *  Terrapane Corporation
 *  All Rights Reserved
 *
 *  Author:
 *      Paul E. Jones <paulej@packetizer.com>
 *
 *  Description:
 *      This module contains tests for the Spinlock object.
 *
 *  Portability Issues:
 *      None.
 */

#include <thread>
#include <mutex>
#include <terra/spindle/spinlock.h>
#include <terra/stf/stf.h>

// Define a basic functionality test, limiting runtime to 10s
STF_TEST_TIMEOUT(Spinlock, Basic, 10)
{
    Terra::Spindle::Spinlock spinlock;
    std::unique_lock<Terra::Spindle::Spinlock> lock(spinlock);

    // Attempt to lock and unlock
    lock.unlock();

    // First lock should work
    STF_ASSERT_TRUE(lock.try_lock());

    // Define a worker thread to test lock failure
    auto worker = [&]()
    {
        std::unique_lock<Terra::Spindle::Spinlock> thread_lock(spinlock,
                                                               std::defer_lock);
        STF_ASSERT_FALSE(thread_lock.try_lock());
    };

    // Start a worker thread, wait for it to complete
    std::thread test_thread(worker);
    test_thread.join();

    // Unlock the mutex
    lock.unlock();

    // This should work again
    STF_ASSERT_TRUE(lock.try_lock());

    // Unlock the mutex
    lock.unlock();

    // Use normal lock
    lock.lock();
}

// Test thread syncronization
STF_TEST_TIMEOUT(Spinlock, ThreadSync, 30)
{
    Terra::Spindle::Spinlock spinlock;
    char resource = 'A';
    unsigned counter = 0;

    auto worker = [&resource, &counter, &spinlock]()
    {
        for(unsigned i = 0; i < 10'000; i++)
        {
            std::lock_guard<Terra::Spindle::Spinlock> lock(spinlock);
            counter++;
            resource ^= 0x40;
        }
    };

    // Start two threads modifying the resource
    std::thread worker1(worker);
    std::thread worker2(worker);

    // Wait for the threads to complete
    worker1.join();
    worker2.join();

    // Resource should be 'A' after an even number of XORs
    STF_ASSERT_EQ('A', resource);

    // Verify the threads did run the expected number of total iterations
    STF_ASSERT_EQ(20'000, counter);
}
