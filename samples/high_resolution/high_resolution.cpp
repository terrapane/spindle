/*
 *  high_resolution.cpp
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
 *      object using high resolution timing (possibly higher CPU usage) with
 *      a 10ms timer.
 *
 *      Running this program, you should see a timer callback about every
 *      10ms, though accuracy will vary by platform.  If a timer is late,
 *      you might even observe two timers fired back-to-back due to the fact
 *      that the timer resolution is dependent on the operating system's
 *      default behavior and because "true" is passed as the last parameter
 *      to the Start() function.
 *
 *  Portability Issues:
 *      None.
 */

#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <algorithm>
#include <functional>
#include <terra/spindle/timer.h>

std::uint64_t best_error = -1;
std::uint64_t worst_error = 0;
std::uint64_t total_error  = 0;
std::vector<std::uint64_t> errors;

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

    std::uint64_t error;
    if (delta.count() > 10'000'000) // Expecting 10ms timers
    {
        error = delta.count() - 10'000'000;
    }
    else
    {
        error = 10'000'000 - delta.count();
    }

    // Record statistics
    if (error < best_error) best_error = error;
    if (error > worst_error) worst_error = error;
    total_error += error;
    errors.push_back(error);

    last_time = current_time;
}

int main()
{
    Terra::Spindle::Timer timer{nullptr, true};

    // Report whether the system is using high-resolution timers
    std::cout << "Using high resolution timing logic? ";
    std::cout << (timer.UsingHighResolution() ? "Yes" : "No") << std::endl;

    // Start a 10ms repeating time that starts immediately
    auto timer_id = timer.Start(TimerCallback,
                                std::chrono::microseconds(0),
                                std::chrono::microseconds(10'000),
                                true);

    // Wait about 10 seconds
    std::this_thread::sleep_for(std::chrono::seconds(10));

    // Stop the timer (though not strictly required since exiting)
    timer.Stop(timer_id);

    // Show the best and worst error numbers
    if (!errors.empty())
    {
        std::cout << "Best error: " << (best_error / 1'000'000.0) << "ms"
                  << std::endl;
        std::cout << "Worst error: " << (worst_error / 1'000'000.0) << "ms"
                  << std::endl;
        std::cout << "Average error: "
                  << ((total_error / errors.size()) / 1'000'000.0) << "ms"
                  << std::endl;

        // Sort the times
        std::sort(errors.begin(), errors.end());

        std::uint64_t median_error ;
        if (errors.size() & 0x01)
        {
            median_error  = errors[errors.size() / 2];
        }
        else
        {
            std::uint64_t left = errors[(errors.size() / 2) - 1];
            std::uint64_t right = errors[(errors.size() / 2) + 1];
            median_error  = (left + right) / 2;
        }

        std::cout << "Median error: " << (median_error / 1'000'000.0) << "ms"
                  << std::endl;
    }
}
