/*
 *  test_thread_control.cpp
 *
 *  Copyright (C) 2024
 *  Terrapane Corporation
 *  All Rights Reserved
 *
 *  Author:
 *      Paul E. Jones <paulej@packetizer.com>
 *
 *  Description:
 *      This module contains tests for the ThreadControl object.
 *
 *  Portability Issues:
 *      None.
 */

#include <vector>
#include <thread>
#include <functional>
#include <chrono>
#include <cstdlib>
#include <terra/spindle/thread_control.h>
#include <terra/stf/stf.h>

using namespace Terra::Spindle;

void ThreadFunc(ThreadControlPointer &thread_control,
                std::atomic<unsigned> &launched,
                std::atomic<bool> &released)
{
    auto result = thread_control->BeginWork();

    launched++;

    if (!result) return;

    while (released == false)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    thread_control->FinishWork();
}

void HaltFunc(ThreadControlPointer &thread_control)
{
    thread_control->BeginWork();

    // Just loop until the thread control object is put in a halted state
    while (thread_control->IsHalted() == false)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    thread_control->FinishWork();
}

void HaltFuncHelper(ThreadControlPointer &thread_control,
                    std::atomic<bool> &halted)
{
    // Halt the thread control (this will block if there is a problem)
    thread_control->Halt();

    // Signal that halting finished
    halted = true;
}

STF_TEST(ThreadControl, Initialize)
{
    ThreadControl thread_control;

    STF_ASSERT_EQ(0, thread_control.RunningThreads());
}

STF_TEST(ThreadControl, BeginEnd)
{
    ThreadControl thread_control;

    // Simulate a single thread doing work
    STF_ASSERT_TRUE(thread_control.BeginWork());

    STF_ASSERT_EQ(1, thread_control.RunningThreads());

    // Simulate thread ending work
    thread_control.FinishWork();

    STF_ASSERT_EQ(0, thread_control.RunningThreads());
}

STF_TEST(ThreadControl, BeginEndMany)
{
    ThreadControl thread_control;

    // Simulate a plurality of threads beginning work
    for (auto i = 0; i < 100; i++) thread_control.BeginWork();

    STF_ASSERT_EQ(100, thread_control.RunningThreads());

    // Simulate a plurality of threads ending work
    for (auto i = 0; i < 100; i++) thread_control.FinishWork();

    STF_ASSERT_EQ(0, thread_control.RunningThreads());
}

STF_TEST(ThreadControl, SpawnThreads)
{
    ThreadControlPointer thread_control;
    std::vector<std::thread> threads;
    std::atomic<unsigned> fully_launched{};
    std::atomic<bool> released{false};

    thread_control = std::make_shared<ThreadControl>();

    // Spawn a few threads
    for (std::size_t i = 0; i < 10; i++)
    {
        threads.emplace_back(ThreadFunc,
                             std::ref(thread_control),
                             std::ref(fully_launched),
                             std::ref(released));
    }

    // Wait until all threads report in
    while (fully_launched < 10)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Ensure all threads appear to be running
    STF_ASSERT_EQ(10, thread_control->RunningThreads());

    // Allow threads to end
    released = true;

    // Wait for all threads to end
    for (auto &thread : threads) thread.join();
}

STF_TEST(ThreadControl, Halt)
{
    ThreadControlPointer thread_control;
    thread_control = std::make_shared<ThreadControl>();

    STF_ASSERT_FALSE(thread_control->IsHalted());

    thread_control->Halt();

    STF_ASSERT_TRUE(thread_control->IsHalted());
}

STF_TEST(ThreadControl, BlockOnHalt)
{
    ThreadControlPointer thread_control;
    thread_control = std::make_shared<ThreadControl>();
    std::thread worker_thread(HaltFunc, std::ref(thread_control));
    std::atomic<bool> halted{false};

    // Wait until we know the thread is running
    while (thread_control->RunningThreads() != 1)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Put the call to halt in a separate thread so we can terminate the
    // the program if is doesn't complete
    std::thread halt_thread(HaltFuncHelper,
                            std::ref(thread_control),
                            std::ref(halted));

    // Wait for the halting thread to do its job
    unsigned counter = 0;
    while(halted == false)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // If it takes too long, force a failure
        if (++counter > 1'000) STF_ASSERT_FALSE(true);
    }

    // No threads should be running
    STF_ASSERT_EQ(0, thread_control->RunningThreads());

    // Wait for the halt thread
    halt_thread.join();

    // Allow the worker thread to fully terminate
    worker_thread.join();
}
