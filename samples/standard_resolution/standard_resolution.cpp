/*
 *  standard_resolution.cpp
 *
 *  Copyright (C) 2024
 *  Terrapane Corporation
 *  All Rights Reserved
 *
 *  Author:
 *      Paul E. Jones <paulej@packetizer.com>
 *
 *  Description:
 *      This is a sample program that demonstrates the use of the Timer
 *      object using standard resolution timing (lower CPU usage) with
 *      a 10ms timer.
 *
 *  Portability Issues:
 *      None.
 */

#include <iostream>
#include <thread>
#include <chrono>
#include <terra/spindle/timer.h>

void TimerCallback([[maybe_unused]] Terra::Spindle::TimerID timer_id)
{
    static bool initial = true;
    static std::chrono::time_point<std::chrono::steady_clock> last_time;

    auto current_time = std::chrono::steady_clock::now();

    if (initial)
    {
        last_time = current_time;
        initial = false;
        return;
    }

    std::chrono::nanoseconds delta = current_time - last_time;

    std::cout << "Delta: " << (delta.count() / 1'000'000.0) << std::endl;

    last_time = current_time;
}

int main()
{
    Terra::Spindle::Timer timer{nullptr, false};

    // Start a 10ms repeating time that starts immediately
    auto timer_id = timer.Start(TimerCallback,
                                std::chrono::microseconds(0),
                                std::chrono::microseconds(10'000),
                                true);

    // Wait about 10 seconds
    std::this_thread::sleep_for(std::chrono::seconds(10));

    // Stop the timer (though not strictly required since exiting)
    timer.Stop(timer_id);
}
