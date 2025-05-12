/*
 *  test_timer.cpp
 *
 *  Copyright (C) 2024
 *  Terrapane Corporation
 *  All Rights Reserved
 *
 *  Author:
 *      Paul E. Jones <paulej@packetizer.com>
 *
 *  Description:
 *      This module contains tests for the Timer object.
 *
 *  Portability Issues:
 *      None.
 */

#include <atomic>
#include <cstdint>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <map>
#include <terra/spindle/timer.h>
#include <terra/stf/stf.h>

using namespace Terra::Spindle;

// Define a class to facilitate access to protected members
class Timer_ : public Timer
{
    public:
        Timer_(const ThreadPoolPointer &thread_pool = {},
               bool high_resolution = false) :
            Timer(thread_pool, high_resolution)
        {
            // Nothing more to do
        }
        ~Timer_() = default;

        std::size_t GetAvailableThreadCount() { return available_threads; }
        std::size_t GetPendingTimerCount()
        {
            std::lock_guard<std::mutex> lock(timer_mutex);
            return pending_list.size();
        }
        std::size_t GetRunningTimerCount()
        {
            std::lock_guard<std::mutex> lock(timer_mutex);
            return running_list.size();
        }

        using Timer::Default_Thread_Pool_Size;
};

class TestObject
{
    public:
        TestObject() = default;
        ~TestObject() = default;
        void TestTimerFunc(TimerID timer_id)
        {
            // Sleep for the specified time
            if (thread_pause_time > 0)
            {
                std::this_thread::sleep_for(
                    std::chrono::microseconds(thread_pause_time));
            }

            std::unique_lock<std::mutex> lock(test_mutex);

            // Update timer firing data
            timer_fired++;
            timer_counts[timer_id]++;
            std::thread::id thread = std::this_thread::get_id();
            thread_ids[thread]++;

            // Cause the timer to block if block_thread is true
            cv.wait(lock, [&]() { return block_thread.load() == false; });

            // Should the next call to this function block?
            block_thread.store(auto_block_thread.load());
        }
        unsigned TimerFired() { return timer_fired.load(); }
        void UnblockThread()
        {
            std::lock_guard<std::mutex> lock(test_mutex);
            block_thread.store(false);
            cv.notify_one();
        }
        void DisableAutoBlock() { auto_block_thread.store(false); }
        void UnblockAllThreads()
        {
            std::lock_guard<std::mutex> lock(test_mutex);
            block_thread.store(false);
            cv.notify_all();
        }
        unsigned GetTimerCount(TimerID id)
        {
            std::lock_guard<std::mutex> lock(test_mutex);
            return timer_counts[id];
        }
        void SetPauseTime(unsigned time)
        {
            std::lock_guard<std::mutex> lock(test_mutex);
            thread_pause_time = time;
        }
        std::size_t GetThreadUsedCount()
        {
            std::lock_guard<std::mutex> lock(test_mutex);
            return thread_ids.size();
        }
        std::size_t GetNumberOfTimers()
        {
            std::lock_guard<std::mutex> lock(test_mutex);
            return timer_counts.size();
        }

    private:
        std::atomic<unsigned> timer_fired{0};
        std::mutex test_mutex;
        std::atomic<bool> block_thread{true};
        std::atomic<bool> auto_block_thread{true};
        std::condition_variable cv;
        std::map<TimerID, unsigned> timer_counts;
        std::map<std::thread::id, unsigned> thread_ids;
        unsigned thread_pause_time{0};
};

STF_TEST(Timer, DefaultConstructor)
{
    Timer_ timer;

    STF_ASSERT_EQ(Timer_::Default_Thread_Pool_Size,
                  timer.GetAvailableThreadCount());
}

STF_TEST_TIMEOUT(Timer, StartAndStop1, 10)
{
    Timer_ timer;
    TestObject object;

    // Create a timer (far out into the future)
    auto timer_id = timer.Start(std::bind(&TestObject::TestTimerFunc,
                                          &object,
                                          std::placeholders::_1),
                                std::chrono::seconds(3600));

    // Ensure the result is not 0
    STF_ASSERT_GT(timer_id, 0);

    // Check the number of pending / running timers
    STF_ASSERT_EQ(1, timer.GetPendingTimerCount());
    STF_ASSERT_EQ(0, timer.GetRunningTimerCount());

    // Stop the timer
    timer.Stop(timer_id);

    // Ensure the timer actually did not fire
    STF_ASSERT_EQ(0, object.TimerFired());

    // Check the number of pending and running timers is zero
    STF_ASSERT_EQ(0, timer.GetPendingTimerCount());
    STF_ASSERT_EQ(0, timer.GetRunningTimerCount());
}

// On Windows, this will almost certainly exercise the functions that set
// and remove a high-resolution timer (just ensuring code coverage)
STF_TEST_TIMEOUT(Timer, StartAndStop2, 10)
{
    Timer_ timer(nullptr, true); // Set the high resolution timer flag
    TestObject object;

    // Create a timer (far out into the future)
    auto timer_id = timer.Start(std::bind(&TestObject::TestTimerFunc,
                                          &object,
                                          std::placeholders::_1),
                                std::chrono::seconds(3600));

    // Ensure the result is not 0
    STF_ASSERT_GT(timer_id, 0);

    // Check the number of pending / running timers
    STF_ASSERT_EQ(1, timer.GetPendingTimerCount());
    STF_ASSERT_EQ(0, timer.GetRunningTimerCount());

    // Stop the timer
    timer.Stop(timer_id);

    // Ensure the timer actually did not fire
    STF_ASSERT_EQ(0, object.TimerFired());

    // Check the number of pending and running timers is zero
    STF_ASSERT_EQ(0, timer.GetPendingTimerCount());
    STF_ASSERT_EQ(0, timer.GetRunningTimerCount());
}

STF_TEST_TIMEOUT(Timer, SingleTimer, 10)
{
    Timer_ timer;
    TestObject object;

    // Create a one-time timer
    auto timer_id = timer.Start(std::bind(&TestObject::TestTimerFunc,
                                          &object,
                                          std::placeholders::_1),
                                std::chrono::milliseconds(0));

    // Ensure the result is not 0
    STF_ASSERT_GT(timer_id, 0);

    // Wait for the timer to fire
    unsigned iterations = 0;
    while(object.TimerFired() < 1)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // If it takes too long, force a failure
        if (++iterations > 1'000) STF_ASSERT_FALSE(true);
    }

    // Timer should be blocked waiting on the condition variable

    // Check the number of pending / running timers
    STF_ASSERT_EQ(0, timer.GetPendingTimerCount());
    STF_ASSERT_EQ(1, timer.GetRunningTimerCount());

    // Release the timer thread
    object.UnblockThread();

    // Stop the timer
    timer.Stop(timer_id);

    // Ensure the timer fired exactly once
    STF_ASSERT_EQ(1, object.TimerFired());

    // Check the number of pending and running timers is zero
    STF_ASSERT_EQ(0, timer.GetPendingTimerCount());
    STF_ASSERT_EQ(0, timer.GetRunningTimerCount());
}

STF_TEST_TIMEOUT(Timer, SingleRecurringTimer, 10)
{
    Timer_ timer(nullptr, true);
    TestObject object;

    // Create a recurring timer
    auto timer_id = timer.Start(std::bind(&TestObject::TestTimerFunc,
                                          &object,
                                          std::placeholders::_1),
                                std::chrono::milliseconds(10),
                                std::chrono::milliseconds(10));

    // Ensure the result is not 0
    STF_ASSERT_GT(timer_id, 0);

    // Wait for the timer to fire
    unsigned iterations = 0;
    unsigned timer_fires = 0;
    while(object.TimerFired() < 3)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

        // Release the blocked thread if the timer fire increased
        if (object.TimerFired() > timer_fires)
        {
            // Timer should be blocked waiting on the condition variable
            timer_fires++;
            object.UnblockThread();
        }

        // If it takes too long, force a failure
        if (++iterations > 1'000) STF_ASSERT_FALSE(true);
    }

    // Timer should be blocked waiting on the condition variable

    // Check the number of pending / running timers
    STF_ASSERT_EQ(0, timer.GetPendingTimerCount());
    STF_ASSERT_EQ(1, timer.GetRunningTimerCount());

    // Release the timer thread
    object.UnblockThread();

    // Stop the timer
    timer.Stop(timer_id);

    // Ensure the timer fired a least 3 times
    STF_ASSERT_GE(object.TimerFired(), 3);

    // Check the number of pending and running timers is zero
    STF_ASSERT_EQ(0, timer.GetPendingTimerCount());
    STF_ASSERT_EQ(0, timer.GetRunningTimerCount());
}

STF_TEST_TIMEOUT(Timer, MultipleRecurringTimers, 10)
{
    Timer_ timer;
    TestObject object;

    // Create two recurring timers
    auto timer_id_1 = timer.Start(std::bind(&TestObject::TestTimerFunc,
                                            &object,
                                            std::placeholders::_1),
                                  std::chrono::milliseconds(10),
                                  std::chrono::milliseconds(10));
    auto timer_id_2 = timer.Start(std::bind(&TestObject::TestTimerFunc,
                                            &object,
                                            std::placeholders::_1),
                                  std::chrono::milliseconds(10),
                                  std::chrono::milliseconds(10));

    // Ensure each timer did get created
    STF_ASSERT_GT(timer_id_1, 0);
    STF_ASSERT_GT(timer_id_2, 0);

    // Wait for the timer to fire
    unsigned iterations = 0;
    unsigned timer_fires = 0;
    unsigned timer_1_fires = 0;
    unsigned timer_2_fires = 0;
    while (timer_1_fires < 6 || timer_2_fires < 6)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

        // Release the blocked thread if the timer fire increased
        if (object.TimerFired() > timer_fires)
        {
            timer_1_fires = object.GetTimerCount(timer_id_1);
            timer_2_fires = object.GetTimerCount(timer_id_2);

            // Timer should be blocked waiting on "block_thread"
            timer_fires++;
            object.UnblockAllThreads();
        }

        // If it takes too long, force a failure
        if (++iterations > 1'000) STF_ASSERT_FALSE(true);
    }

    // Release the timer thread
    object.DisableAutoBlock();
    object.UnblockAllThreads();

    // Stop the timers
    timer.Stop(timer_id_1);
    timer.Stop(timer_id_2);

    // Ensure the timers fired a least 3 times
    STF_ASSERT_GE(object.TimerFired(), 6);

    // Ensure each timer fired at least twice
    STF_ASSERT_GE(object.GetTimerCount(timer_id_1), 5);
    STF_ASSERT_GE(object.GetTimerCount(timer_id_2), 5);

    // Check the number of pending and running timers is zero
    STF_ASSERT_EQ(0, timer.GetPendingTimerCount());
    STF_ASSERT_EQ(0, timer.GetRunningTimerCount());
}

STF_TEST_TIMEOUT(Timer, MultipleRecurringTimersAndLargeThreadPool, 10)
{
    constexpr std::size_t TimerCount{100};
    ThreadPoolPointer thread_pool = std::make_shared<ThreadPool>(10);
    Timer_ timer(thread_pool);
    TestObject object;
    std::vector<TimerID> timers;

    // Threads should not block in this test, but should pause
    object.DisableAutoBlock();
    object.UnblockAllThreads();
    object.SetPauseTime(1'000); // 1ms

    // Create a bunch of timers
    for (std::size_t i = 0; i < TimerCount; i++)
    {
        timers.push_back(timer.Start(std::bind(&TestObject::TestTimerFunc,
                                               &object,
                                               std::placeholders::_1),
                                     std::chrono::milliseconds(0),
                                     std::chrono::milliseconds(10),
                                     false));
    }

    // Ensure all timers were created
    for (auto timer_id : timers) STF_ASSERT_NE(timer_id, 0);

    // Ensure each timer fires at least 6 times
    unsigned iterations = 0;
    while (true)
    {
        // Sleep to give the timers a chance to start
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // Ensure that all timers have been serviced at least 6 times
        unsigned count = 0;
        for (auto timer_id : timers)
        {
            if (object.GetTimerCount(timer_id) >= 6) count++;
        }

        // Break out if count >= TimerCount
        if (count >= TimerCount) break;

        // If it takes too long, force a failure
        if (++iterations > 1'000) STF_ASSERT_FALSE(true);
    }

    // Stop the timers
    for (auto timer_id : timers) timer.Stop(timer_id);

    // It is expected that at least 3 threads would have been used
    STF_ASSERT_GE(object.GetThreadUsedCount(), 3);

    // Check the number of pending and running timers is zero
    STF_ASSERT_EQ(0, timer.GetPendingTimerCount());
    STF_ASSERT_EQ(0, timer.GetRunningTimerCount());
}

STF_TEST_TIMEOUT(Timer, MultipleRecurringTimersWithThreadControl, 10)
{
    constexpr std::size_t TimerCount{100};
    ThreadPoolPointer thread_pool = std::make_shared<ThreadPool>(10);
    Timer_ timer(thread_pool);
    TestObject object;
    ThreadControlPointer thread_control = std::make_shared<ThreadControl>();

    // Threads should not block in this test, but should pause
    object.DisableAutoBlock();
    object.UnblockThread();
    object.SetPauseTime(1'000); // 1ms

    // Create a bunch of timers
    for (std::size_t i = 0; i < TimerCount; i++)
    {
        auto timer_id = timer.Start(std::bind(&TestObject::TestTimerFunc,
                                              &object,
                                              std::placeholders::_1),
                                    std::chrono::milliseconds(0),
                                    std::chrono::milliseconds(10),
                                    false,
                                    thread_control);
        // Ensure timer was created
        STF_ASSERT_NE(timer_id, 0);
    }

    // Ensure there is some timer activity
    unsigned iterations = 0;
    while (true)
    {
        // Sleep to give the timers a chance to start
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // Ensure all timers fired at least once
        if (object.GetNumberOfTimers() >= TimerCount) break;

        // If it takes too long, force a failure
        if (++iterations > 1'000) STF_ASSERT_FALSE(true);
    }

    // Stop the timers via the ThreadControl object; this does not immediately
    // stop timers, but it will prevent them from running again
    thread_control->Halt();

    // The pending and running timer lists should go to zero
    iterations = 0;
    while (true)
    {
        // Sleep to give the timers a chance to start
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // Check that the timers have been removed
        if ((timer.GetPendingTimerCount() == 0) &&
            (timer.GetRunningTimerCount() == 0))
        {
            break;
        }

        // If it takes too long, force a failure
        if (++iterations > 1'000) STF_ASSERT_FALSE(true);
    }
}
