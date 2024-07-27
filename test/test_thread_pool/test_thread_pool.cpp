/*
 *  test_thread_pool.cpp
 *
 *  Copyright (C) 2024
 *  Terrapane Corporation
 *  All Rights Reserved
 *
 *  Author:
 *      Paul E. Jones <paulej@packetizer.com>
 *
 *  Description:
 *      This module contains tests for the ThreadPool object.
 *
 *  Portability Issues:
 *      None.
 */

#include <cstddef>
#include <cstdint>
#include <functional>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <terra/spindle/thread_pool.h>
#include <terra/spindle/thread_control.h>
#include <terra/stf/stf.h>

using namespace Terra::Spindle;

struct TestObject
{
    void Entry(int argument)
    {
        value = argument;
        counter++;
    }

    std::atomic<int> value{};
    std::atomic<unsigned> counter{0};
};

struct SecondTestObject
{
    void Entry(int argument)
    {
        value = argument;
        counter++;
        std::unique_lock<std::mutex> lock(test_mutex);
        cv.wait(lock, [&]() { return released == true; });
    }

    std::atomic<int> value{};
    std::atomic<unsigned> counter{0};
    std::atomic<bool> released{false};
    std::mutex test_mutex;
    std::condition_variable cv;
};

void HaltFuncHelper(ThreadControlPointer &thread_control)
{
    // Halt the thread control (this will block if there is a problem)
    thread_control->Halt();
}

STF_TEST(ThreadPool, DefaultConstructor)
{
    ThreadPool thread_pool;

    STF_ASSERT_EQ(ThreadPool::Default_Thread_Count, thread_pool.ThreadCount());
    STF_ASSERT_EQ(0, thread_pool.Invoked());
    STF_ASSERT_EQ(0, thread_pool.TotalInvocations());
}

STF_TEST(ThreadPool, CustomPoolSize)
{
    ThreadPool thread_pool(2);

    STF_ASSERT_EQ(2, thread_pool.ThreadCount());
    STF_ASSERT_EQ(0, thread_pool.Invoked());
    STF_ASSERT_EQ(0, thread_pool.TotalInvocations());
}

STF_TEST(ThreadPool, VerifyArgument)
{
    TestObject object;
    ThreadPool thread_pool;
    ThreadControlPointer thread_control = std::make_shared<ThreadControl>();
    int value = 5;

    thread_pool.Invoke(thread_control, [&, value]() { object.Entry(value); });

    unsigned iterations = 0;
    while (object.counter < 1)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // If it takes too long, force a failure
        if (++iterations > 1'000) STF_ASSERT_FALSE(true);
    }

    // Halting the thread control object will ensure thread completion
    thread_control->Halt();

    // There should be none running at the moment
    STF_ASSERT_EQ(0, thread_pool.Invoked());

    // There should have been a total of 1 invocation
    STF_ASSERT_EQ(1, thread_pool.TotalInvocations());

    // Check that the test object value was updated
    STF_ASSERT_EQ(value, object.value);
}

STF_TEST(ThreadPool, ManyInvocations)
{
    TestObject object;
    ThreadPool thread_pool;
    ThreadControlPointer thread_control = std::make_shared<ThreadControl>();
    int value = 10;
    std::size_t total_invokes = ThreadPool::Default_Thread_Count * 2;

    // Ensure there is more than 1 invocation call to make
    STF_ASSERT_GT(total_invokes, 1);

    for (std::size_t i = 0; i < total_invokes; i++)
    {
        thread_pool.Invoke(thread_control,
                           [&, value]() { object.Entry(value); });
    }

    unsigned iterations = 0;
    while (object.counter < total_invokes)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // If it takes too long, force a failure
        if (++iterations > 1'000) STF_ASSERT_FALSE(true);
    }

    // Halting the thread control object will ensure thread completion
    thread_control->Halt();

    // There should be none running at the moment
    STF_ASSERT_EQ(0, thread_pool.Invoked());

    // There should have been a total of 100 invocations
    STF_ASSERT_EQ(total_invokes, thread_pool.TotalInvocations());

    // Check that the test object value was updated
    STF_ASSERT_EQ(value, object.value);
}

STF_TEST(ThreadPool, VerifyQueuedInvocations)
{
    SecondTestObject object;
    ThreadPool thread_pool;
    ThreadControlPointer thread_control = std::make_shared<ThreadControl>();
    int value = 15;
    std::size_t total_invokes = ThreadPool::Default_Thread_Count * 2;

    // Ensure there is more than 1 invocation call to make
    STF_ASSERT_GT(total_invokes, 1);

    for (std::size_t i = 0; i < total_invokes; i++)
    {
        thread_pool.Invoke(thread_control,
                           [&, value]() { object.Entry(value); });
    }

    unsigned iterations = 0;
    while (object.counter < ThreadPool::Default_Thread_Count)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // If it takes too long, force a failure
        if (++iterations > 1'000) STF_ASSERT_FALSE(true);
    }

    // There should be ThreadPool::Default_Thread_Count threads running
    STF_ASSERT_EQ(ThreadPool::Default_Thread_Count, thread_pool.Invoked());
    STF_ASSERT_EQ(ThreadPool::Default_Thread_Count,
                  thread_pool.TotalInvocations());

    // Release the threads
    object.released = true;
    object.cv.notify_all();

    // Wait for the rest to complete
    iterations = 0;
    while (object.counter < total_invokes)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // If it takes too long, force a failure
        if (++iterations > 1'000) STF_ASSERT_FALSE(true);
    }

    // Halting the thread control object will ensure thread completion
    thread_control->Halt();

    // There should be none running at the moment
    STF_ASSERT_EQ(0, thread_pool.Invoked());

    // There should have been a total of total_invokes invocations
    STF_ASSERT_EQ(total_invokes, thread_pool.TotalInvocations());

    // Check that the test object value was updated
    STF_ASSERT_EQ(value, object.value);
}

STF_TEST(ThreadPool, HaltInvocations)
{
    SecondTestObject object;
    ThreadPool thread_pool;
    ThreadControlPointer thread_control = std::make_shared<ThreadControl>();
    int value = 20;
    std::size_t total_invokes = ThreadPool::Default_Thread_Count * 2;

    // Ensure there is more than 1 invocation call to make
    STF_ASSERT_GT(total_invokes, 1);

    for (std::size_t i = 0; i < total_invokes; i++)
    {
        thread_pool.Invoke(thread_control,
                           [&, value]() { object.Entry(value); });
    }

    unsigned iterations = 0;
    while (object.counter < ThreadPool::Default_Thread_Count)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // If it takes too long, force a failure
        if (++iterations > 1'000) STF_ASSERT_FALSE(true);
    }

    // There should be ThreadPool::Default_Thread_Count threads running
    STF_ASSERT_EQ(ThreadPool::Default_Thread_Count, thread_pool.Invoked());

    // Now, halt the thread_control object so that threads will not be invoked,
    // even though total_invokes were requested before

    // Put the call to halt in a separate thread so we can terminate the
    // the program if is doesn't complete
    std::thread halt_thread(HaltFuncHelper, std::ref(thread_control));

    // Make sure the halting thread did its job
    iterations = 0;
    while (thread_control->IsHalted() == false)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // If it takes too long, force a failure
        if (++iterations > 1'000) STF_ASSERT_FALSE(true);
    }

    // Release the running threads
    object.released = true;
    object.cv.notify_all();

    // Wait for the halting thread to return
    halt_thread.join();

    // There should be none running at the moment
    STF_ASSERT_EQ(0, thread_pool.Invoked());

    // Total of ThreadPool::Default_Thread_Count invocations
    STF_ASSERT_EQ(ThreadPool::Default_Thread_Count,
                  thread_pool.TotalInvocations());

    // Check that the test object value was updated
    STF_ASSERT_EQ(value, object.value);
}

STF_TEST(ThreadPool, NoThreadControl)
{
    TestObject object;
    ThreadPoolPointer thread_pool = std::make_shared<ThreadPool>();
    int value = 25;

    thread_pool->Invoke(nullptr, [&, value]() { object.Entry(value); });

    unsigned iterations = 0;
    while (object.counter < 1)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // If it takes too long, force a failure
        if (++iterations > 1'000) STF_ASSERT_FALSE(true);
    }

    // Terminate the thread pool
    thread_pool.reset();

    // Check that the test object value was updated
    STF_ASSERT_EQ(value, object.value);
}
