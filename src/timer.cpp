/*
 *  timer.cpp
 *
 *  Copyright (C) 2024
 *  Terrapane Corporation
 *  All Rights Reserved
 *
 *  Author:
 *      Paul E. Jones <paulej@packetizer.com>
 *
 *  Description:
 *      This file implements the Timer object used to initiate function calls
 *      at a specified time and/or interval.
 *
 *      Note that timers are stored in a std::deque and are sorted with the
 *      addition or removal of each timer.  This works well for a reasonable
 *      number of timers, but would exhibit performance issues with too many
 *      simultaneous timers created.  What those limits are would vary, but
 *      if one is seeing issues then timers are likely overused and one
 *      should rethink the application design.
 *
 *      Internally, the Timer object uses a condition variable for timing on
 *      most platforms and the standard waitable timer on Windows platforms.
 *      This works well generally, but it is not highly accurate.  This is
 *      a function of the operating system, as it tries to balance CPU
 *      utilization against awakening waiting threads precisely.  However,
 *      if one needs a higher precision timer (e.g., something that awakens
 *      every 10ms with highest precision possible), set the high_resolution
 *      parameter in the constructor to true. This will be more accurate, but
 *      at the expense of CPU usage.  If one needs both high-resolution
 *      and general timers, it is best to use different Timer objects to reduce
 *      impact on CPU utilization.  The Timer object will at least optimize
 *      CPU impact within a single Timer object by sorting timers and only
 *      actively working on the first timer expected to fire.
 *
 *      Most timers fire with an acceptable level of accuracy, though one may
 *      need high-resolution timers for real-time applications (specified
 *      via the Timer constructor).
 *
 *      Windows typically uses a 15ms system clock tick and, as a result, has
 *      pretty poor performance for regular timers.  This may result in
 *      repeating with a small frequency (e.g., 10ms) firing back-to-back since
 *      the clock doesn't wake up the waiting thread quickly.  The high
 *      resolution timer option will work far better.  See:
 *      https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/high-resolution-timers
 *
 *      Some Linux kernels have very good performance when waiting on
 *      condition variables.  Some of the newer Linux kernels, for example,
 *      have a margin of error less than 100 microseconds on a 10ms timer.
 *      Since systems vary, if the user requests a high resolution timer a
 *      test is performed on construction to measure the accuracy of the
 *      "normal" timers.  If they are sufficient, the high-resolution behavior
 *      will be disabled, even if requested, as they are not needed.
 *
 *  Portability Issues:
 *      None.
 */

#include <algorithm>
#include <limits>
#include <climits>
#include <terra/spindle/timer.h>
#include <terra/spindle/thread_control.h>

namespace Terra::Spindle
{

/*
 *  Timer::Timer()
 *
 *  Description:
 *      Constructor for the Timer object.
 *
 *  Parameters:
 *      thread_pool [in]
 *          Shared pointer to a thread pool.  If the pointer does not point
 *          to a thread pool, a thread pool with a single servicing thread
 *          will be created.
 *
 *      high_resolution [in]
 *          Use method to achieve the highest timer accuracy as possible at
 *          the (possible) expense of additional CPU utilization.  Most events
 *          where a timer is needed does not need a high-level of accuracy, but
 *          certain tasks (e.g., sampling audio) needs to be accurate
 *          (e.g., precise 10ms repeating event).  If high resolution timing
 *          is requested, a test is performed during construction to determine
 *          if such logic is needed.  Some systems have excellent resolution
 *          without the need for special timing logic or timers.  One should
 *          set this to true only if real-time performance is a requirement.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
Timer::Timer(const ThreadPoolPointer &thread_pool, bool high_resolution) :
    terminate{false},
    high_resolution{false},
#ifdef _WIN32
    waitable_timer{},
#endif
    last_timer_id{0},
    thread_pool{thread_pool},
    pool_control{std::make_shared<ThreadControl>()},
    available_threads{0},
    invoked_threads{0},
    waiting_thread{false}
{
    // If no thread pool provided, create one of Default_Thread_Pool_Size
    if (!thread_pool)
    {
        this->thread_pool =
                    std::make_shared<ThreadPool>(Default_Thread_Pool_Size);
    }

    // Store the number of threads in the thread pool to avoid querying
    available_threads = this->thread_pool->ThreadCount();

    // If the user requested high-resolution timers, test to see if they are
    // actually needed on this platform
    if (high_resolution && !TestHighAccuracy())
    {
        this->high_resolution = high_resolution;
    }

#ifdef _WIN32
    if (high_resolution)
    {
        waitable_timer =
            CreateWaitableTimerExW(nullptr,
                                   nullptr,
                                   CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                   TIMER_ALL_ACCESS);
    }
    else
    {
        waitable_timer = CreateWaitableTimerW(nullptr, false, nullptr);
    }
#endif
}

/*
 *  Timer::~Timer()
 *
 *  Description:
 *      Constructor for the Timer object.
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
Timer::~Timer()
{
    // Lock the mutex
    std::unique_lock<std::mutex> lock(timer_mutex);

    // Set the terminate flag
    terminate = true;

    // Awaken any waiting thread
    AwakenWaitingThread();

#ifdef _WIN32
    // If there is a Windows waitable timer, close it
    if (waitable_timer) CloseHandle(waitable_timer);
#endif

    // Ensure no ThreadPool threads are running in this object
    pool_control->Halt(lock);
}

/*
 *  Timer::Start()
 *
 *  Description:
 *      Start a new timer.
 *
 *  Parameters:
 *      entry_point [in]
 *          The function to call when the timer fires.
 *
 *      delay [in]
 *          How much delay to impose before the timer fires.
 *
 *      interval [in]
 *          The interval with which the timer fires.  If the time to service
 *          the timer is longer than this interval, then the next timer fire
 *          will happen with as close to a zero delay as possible.  One should
 *          take care to not specify an interval that is so short that the timer
 *          constantly fires (i.e., it takes longer to process the timer
 *          callback than the interval value).  An extremely small interval or
 *          a timer that takes more than the interval times to service can
 *          result in the timer being put back on the list ahead of other
 *          timers, effectively preventing other timers from being serviced.
 *          Specifying an interval value of zero indicates the timer should
 *          fire only once.
 *
 *      rigid_interval [in]
 *          By default, the next start time for a recurring timer will be
 *          based on the original start time.  The intent is to ensure, to
 *          the extent possible, that a timer that is supposed to fire every
 *          ten seconds, for example, will fire every ten seconds.  However,
 *          if the time required to service the timer frequently exceeds the
 *          interval time, this can cause the timer to sort to the top and
 *          fire almost immediately again.  To mitigate that, set
 *          rigid_interval to false if servicing the timer may take more
 *          time than the interval value or increase the interval value
 *          higher.  If the interval is very low (e.g., milliseconds),
 *          it might be necessary to use a Timer object configured to use
 *          high resolution timer logic.  (See the "high_resolution" parameter
 *          in the constructor.)
 *
 *      thread_control [in]
 *          This optional parameter allows the caller to specify a ThreadControl
 *          object to associate with the Timer.  This is particularly useful
 *          when creating a number of one-shot timers and it's not convenient
 *          to either call Stop() for each or destroy the Timer object
 *          (which will also wait on all running timers to complete).
 *          If this parameter is not given or is "null", a ThreadControl
 *          object is created and used internally with each timer.
 *
 *  Returns:
 *      The TimerID for the newly created timer or 0 if the timer could not
 *      be started.
 *
 *  Comments:
 *      None.
 */
TimerID Timer::Start(const TimerEntryPoint &entry_point,
                     const std::chrono::nanoseconds &delay,
                     const std::chrono::nanoseconds &interval,
                     const bool rigid_interval,
                     ThreadControlPointer thread_control)
{
    TimerID iterations = 0;

    // Lock the mutex
    std::lock_guard<std::mutex> lock(timer_mutex);

    // Determine a new timer ID
    while (true)
    {
        // Increment the TimerID and ensure is is not zero
        if (++last_timer_id == 0) ++last_timer_id;

        // Try to find a timer having this timer ID
        auto [list, it] = FindTimer(last_timer_id);

        // If not found, use this value
        if (it == list.end()) break;

        // If we exhaust all possible values, fail
        if (++iterations == 0) return {};
    };

    // Get this timer ID
    TimerID new_timer_id = last_timer_id;

    // Populate the timer details structure
    TimerDetails timer_details =
    {
        new_timer_id,
        std::chrono::steady_clock::now() + delay,
        interval,
        rigid_interval,
        entry_point,
        thread_control ? std::move(thread_control) :
                         std::make_shared<ThreadControl>()
    };

    // Put the new timer in the pending timer list
    pending_list.emplace_back(timer_details);

    // Sort the pending timer list if there is more than one element
    SortPendingTimers();

    // If this timer is in the first postion, ensure a thread is serving
    if (pending_list.front().timer_id == new_timer_id) InvokeThread();

    return new_timer_id;
}

/*
 *  Timer::Stop()
 *
 *  Description:
 *      Stop a timer that was previously started.  If the timer has not yet
 *      fired, it will be stopped before it gets a chance to start.  If the
 *      timer does not exist (perhaps because it has completed and purged from
 *      lists), this function will just return without action.  If it has
 *      already fired, this function will block waiting for the timer to
 *      complete to ensure the caller that the timer is destroyed.  Since the
 *      timer may have just fired and may have a thread calling into a function
 *      that contains a mutex, it's important that the caller to this function
 *      does not have a lock on that same mutex.
 *
 *  Parameters:
 *      timer_id [in]
 *          Timer ID of timer to stop.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
void Timer::Stop(TimerID timer_id)
{
    // Just return if the Timer ID is 0
    if (timer_id == 0) return;

    // Lock the mutex
    std::unique_lock<std::mutex> lock(timer_mutex);

    // Try to find the timer
    auto [list, it] = FindTimer(timer_id);

    // If not found, then return
    if (it == list.end()) return;

    // To stop the timer, we use the ThreadControl object
    ThreadControlPointer timer_control = (*it).timer_control;

    // Was the timer found on the pending list?
    if (&list == &pending_list)
    {
        // Move the timer to the running list so that if others call Stop()
        // while timer termination is is progress, those callers will be able
        // to see that the timer still exists
        running_list.push_back(*it);
        pending_list.erase(it);

        // If a thread was waiting, wake it up to check for changes
        AwakenWaitingThread();
    }

    // Halt the ThreadControl object, which causes this thread to block if
    // a timer thread is running; this "lock" will be unlocked if Halt()
    // must wait on a thread
    timer_control->Halt(lock);

    // Erase the timer
    RemoveTimer(timer_id);
}

/*
 *  Timer::UsingHighResolution()
 *
 *  Description:
 *      This function will return true if logic related to high resolution
 *      timers is employed.  When the Timer object is created and high
 *      resolution timers are requested, the system's timing logic is tested.
 *      If it performs well without use of code aimed at improving timing
 *      accuracy, the high_resolution variable will not be set to true and the
 *      Timer will rely on default system behavior that is (generally) less
 *      CPU-intensive.
 *
 *  Parameters:
 *      None.
 *
 *  Returns:
 *      True if high-resolution timers are employed and false if they are not.
 *
 *  Comments:
 *      The caller must ensure that timer_mutex is locked when called.
 */
bool Timer::UsingHighResolution() const noexcept
{
    return high_resolution;
}

/*
 *  Timer::SortPendingTimers()
 *
 *  Description:
 *      This function will sort the pending timers list to ensure the first
 *      timer that should be fired is at the beginning.
 *
 *  Parameters:
 *      None.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      The caller must ensure that timer_mutex is locked when called.
 */
inline void Timer::SortPendingTimers()
{
    // Sort the pending timer list if there is more than one element
    if (pending_list.size() > 1)
    {
        std::sort(pending_list.begin(),
                  pending_list.end(),
                  [](const TimerDetails &t1, const TimerDetails &t2)
                  {
                      return t1.next_time < t2.next_time;
                  });
    }
}

/*
 *  Timer::InvokeThread()
 *
 *  Description:
 *      This function will invoke a thread in the thread pool to service
 *      the pending timers if one is not already waiting.  If one is already
 *      waiting, wake it up so that it will see if it should service
 *      a different timer.
 *
 *  Parameters:
 *      None.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      The caller must ensure that timer_mutex is locked when called.
 */
void Timer::InvokeThread()
{
    // If a thread is already waiting, just wake it up
    if (waiting_thread)
    {
        AwakenWaitingThread();
        return;
    }

    // If there are threads available in the thread pool, invoke one
    if (invoked_threads < available_threads)
    {
        thread_pool->Invoke(pool_control, [this]() { ServiceLoop(); });
        invoked_threads++;
    }
}

/*
 *  Timer::ServiceLoop()
 *
 *  Description:
 *      This is the entry point for threads coming from the thread pool to
 *      service the list of pending timers.  At most one thread will wait
 *      for the next pending timer.  Excess threads will return to the thread
 *      pool.
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
void Timer::ServiceLoop()
{
    // Lock the mutex
    std::unique_lock<std::mutex> lock(timer_mutex);

    // Loop to service the pending timers list
    while (!pending_list.empty())
    {
        // By default, do not use Windows' waitable timers
        bool waitable_timer_set = false;

        // If a thread is already waiting, exit the loop
        if (waiting_thread) break;

        // Indicate this thread is waiting on the next timer
        waiting_thread = true;

        auto next_time = pending_list.front().next_time;
        auto timer_id = pending_list.front().timer_id;
        auto current_time = std::chrono::steady_clock::now();

        // If the timer is set to expire in the future, wait for that time
        if (next_time > current_time)
        {
#ifdef _WIN32
            // Set a waitable timer
            {
                LARGE_INTEGER wait_time{};

                // Need a negative number for delay, so current_time - next_time
                wait_time.QuadPart = duration_cast<std::chrono::nanoseconds>(
                                         current_time - next_time)
                                         .count() /
                                     100; // Must be in hundreds of nanoseconds

                // Function to set a timer differs if high resolution required
                if (high_resolution)
                {
                    waitable_timer_set = SetWaitableTimerEx(waitable_timer,
                                                            &wait_time,
                                                            0,
                                                            nullptr,
                                                            nullptr,
                                                            nullptr,
                                                            0);
                }
                else
                {
                    waitable_timer_set = SetWaitableTimer(waitable_timer,
                                                          &wait_time,
                                                          0,
                                                          nullptr,
                                                          nullptr,
                                                          0);
                }
            }

            // If timer set, wait here
            if (waitable_timer_set)
            {
                // Unlock the mutex while waiting
                lock.unlock();

                // Wait for the timer to expire
                WaitForSingleObject(waitable_timer, INFINITE);

                // Re-lock the mutex
                lock.lock();
            }
#endif

            // Use a condition variable is not using a Windows waitable timer
            if (!waitable_timer_set)
            {
                // By default, wait until the timer's next_time value
                std::chrono::steady_clock::time_point wait_time = next_time;

                // If high_resolution is true, we wait only a fraction
                // of the time and iteratively approach the target time
                if (high_resolution)
                {
                    wait_time =
                        current_time +
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            (next_time - current_time) / Time_Divisor);
                }

                // Wait until on the timer
                cv.wait_until(
                    lock,
                    wait_time,
                    [&]()
                    {
                        return terminate || pending_list.empty() ||
                               (pending_list.front().timer_id != timer_id);
                    });
            }
        }

        // Indicate this thread is no longer waiting
        waiting_thread = false;

        // If terminating or timer list is empty, return
        if (terminate || (pending_list.empty())) break;

        // If the timer on the front is not the monitored timer, loop
        if (pending_list.front().timer_id != timer_id) continue;

        // Ensure we did not stop waiting prematurely
        current_time = std::chrono::steady_clock::now();
        if (next_time > current_time) continue;

        // Copy the current timer details
        auto current_timer = pending_list.front();

        // Remove this timer from the pending list
        pending_list.pop_front();

        // Put this timer on the running list
        running_list.push_back(current_timer);

        // If there is another pending timer, invoke a thread to service it
        if (!pending_list.empty()) InvokeThread();

        // Unlock the mutex while servicing the timer
        lock.unlock();

        // Try to start work; false result means timer was stopped
        if (current_timer.timer_control->BeginWork())
        {
            try
            {
                // Call into the function associated with this timer
                current_timer.entry_point(current_timer.timer_id);
            }
            catch (...)
            {
                // Nothing to do
            }

            // Indicate that the timer has been serviced
            current_timer.timer_control->FinishWork();
        }

        // Re-lock the mutex
        lock.lock();

        // Remove the timer from the running timers list, if it still exists
        // (it may have been removed via a call to Stop())
        if (RemoveTimer(current_timer.timer_id) &&
            (!current_timer.timer_control->IsHalted()))
        {
            // Check to see if this current timer is a recurring timer
            if (current_timer.interval != std::chrono::nanoseconds::zero())
            {
                // Get the current time
                auto current_time = std::chrono::steady_clock::now();

                // Update the next time the timer should fire
                if (current_timer.rigid_interval)
                {
                    current_timer.next_time += current_timer.interval;

                    // If the timer is late, just set it to fire immediately
                    // and skip any missed intervals
                    if (current_timer.next_time < current_time)
                    {
                        current_timer.next_time = current_time;
                    }
                }
                else
                {
                    current_timer.next_time = current_time +
                                              current_timer.interval;
                }

                // Re-add this recurring timer as a pending timer
                pending_list.emplace_back(current_timer);

                // Sort the pending timer list if there is more than one element
                SortPendingTimers();

                // Is this timer now at the head of the list?
                if (pending_list.front().timer_id == current_timer.timer_id)
                {
                    // If a thread is waiting, awaken it
                    AwakenWaitingThread();
                }
            }
        }
    }

    // Thread is finished, so decrement the invoked thread count
    invoked_threads--;
}

/*
 *  Timer::RemoveTimer()
 *
 *  Description:
 *      This function will remove the timer specified by the given TimerID from
 *      the pending and running timer lists.  If the timer is not found,
 *      then no action is taken.  This would be normal if, for example,
 *      the timer ran and was removed at about the same time an explict call
 *      is made to stop the timer.
 *
 *  Parameters:
 *      timer_id [in]
 *          The timer to erase from the internal lists.
 *
 *  Returns:
 *      True if the timer was found and removed or false if the timer was
 *      not found.
 *
 *  Comments:
 *      The caller must ensure that timer_mutex is locked when called.
 */
inline bool Timer::RemoveTimer(TimerID timer_id)
{
    // Find the timer
    auto [list, it] = FindTimer(timer_id);

    // If the timer was not found, return false
    if (it == list.end()) return false;

    // Now remove the timer from the list
    list.erase(it);

    return true;
}

/*
 *  Timer::AwakenWaitingThread()
 *
 *  Description:
 *      This function will awake a thread if one is waiting on the condition
 *      variable or, on Windows, the WaitForSingleObject().
 *
 *  Parameters:
 *      None.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      The caller must ensure that timer_mutex is locked when called.
 */
void Timer::AwakenWaitingThread()
{
    // If no thread it waiting, just return;
    if (!waiting_thread) return;

    // Notify any thread waiting on the condition variable
    cv.notify_one();

#ifdef _WIN32
    LARGE_INTEGER wait_time{};

    // Set a new waitable time to awaken thread in WaitForSingleObject()
    SetWaitableTimer(waitable_timer, &wait_time, 0, nullptr, nullptr, false);
#endif
}

/*
 *  Timer::FindTimer()
 *
 *  Description:
 *      This function will attempt to locate the specified timer, returning
 *      a reference to the container in which the timer sits, and an iterator
 *      referring to the timer within the container.  If the iterator points
 *      to the end of the list it indicates that the timer was not found.
 *
 *  Parameters:
 *      timer_id [in]
 *          The timer to locate.
 *
 *  Returns:
 *      A pair containing a reference to the container holding the timer and
 *      an iterator referring to the timer within the container.  If the
 *      timer was not found, the iterator will point to the end of the
 *      returned list.
 *
 *  Comments:
 *      The caller must ensure that timer_mutex is locked when called.
 */
inline std::pair<Timer::TimerList &, Timer::TimerList::iterator>
Timer::FindTimer(TimerID timer_id)
{
    // Attempt to remove the timer from the running timer list
    auto it = std::find_if(running_list.begin(),
                           running_list.end(),
                           [timer_id](const TimerDetails &timer)
                           {
                               return timer.timer_id == timer_id;
                           });

    // If the timer was found in the running timer list, erase it
    if (it != running_list.end())
    {
        return {running_list, it};
    }

    // Attempt to remove the timer from the pending timer list
    it = std::find_if(pending_list.begin(),
                      pending_list.end(),
                      [timer_id](const TimerDetails &timer)
                      {
                          return timer.timer_id == timer_id;
                      });

    return {pending_list, it};
}

/*
 *  Timer::TestHighAccuracy()
 *
 *  Description:
 *      This function will test the accuracy of the timer and return true
 *      if a repeating 10ms timer is within 0.1ms accuracy or false if not.
 *      During startup, the system might be somewhat loaded and the accuracy
 *      be off.  This test will be repeated several times.
 *
 *  Parameters:
 *      None.
 *
 *  Returns:
 *      True if the default timer behavior is sufficiently accurate.
 *
 *  Comments:
 *      This function is called during construction when the high_resolution
 *      variable is set to false.  It is important that high_resolution is
 *      false during this test.
 */
bool Timer::TestHighAccuracy()
{
    std::condition_variable test_cv;
    std::mutex test_mutex;
    std::atomic<unsigned> ticks = 0;
    unsigned good = 0;

    // Lock the test mutex
    std::unique_lock<std::mutex> lock(test_mutex);

    // Start a timer for testing
    auto test_timer = Start(
        [&](TimerID)
        {
            static std::chrono::time_point<std::chrono::steady_clock> last_time;

            // Get the current time
            auto current_time = std::chrono::steady_clock::now();

            // Comparisons only start after the initial tick
            if (ticks++ == 0)
            {
                last_time = current_time;
                return;
            }

            // Get the difference in time
            std::chrono::microseconds delta =
                std::chrono::duration_cast<std::chrono::microseconds>(
                    current_time - last_time);

            // Store the current time for the next comparison
            last_time = current_time;

            // Delta sufficiently close? (within 100 microseconds)
            if ((delta.count() > 9900) && (delta.count() < 10100)) good++;

            // After 6 iterations, awaken the waiting thread
            if (ticks >= 6) test_cv.notify_one();

        },
        std::chrono::microseconds(0),
        std::chrono::microseconds(10'000));

    // Wait for the timer test to be performed
    test_cv.wait(lock, [&]() { return ticks >= 6; });

    // Stop the timer
    Stop(test_timer);

    // If at least 4 tests show high accuracy, it's good
    return (good >= 4);
}

} // namespace Terra::Spindle
