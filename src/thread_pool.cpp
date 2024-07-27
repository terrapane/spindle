/*
 *  thread_pool.cpp
 *
 *  Copyright (C) 2024
 *  Terrapane Corporation
 *  All Rights Reserved
 *
 *  Author:
 *      Paul E. Jones <paulej@packetizer.com>
 *
 *  Description:
 *      This file implements a thread pool class that allows an application
 *      create a number of threads that can then be invoked to perform
 *      asynchronous tasks while sharing threads from this common pool,
 *      insofar as a common pool is desired.  An application may create
 *      any number of thread pools if it makes sense to do so.
 *
 *  Portability Issues:
 *      None.
 */

#include <terra/spindle/thread_pool.h>

namespace Terra::Spindle
{

/*
 *  ThreadPool::ThreadPool()
 *
 *  Description:
 *      Constructor for the ThreadPool object.
 *
 *  Parameters:
 *      thread_count [in]
 *          The number of threads to launch as a part of the pool.  If a value
 *          is not specified, Default_Thread_Count threads will be spawned.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
ThreadPool::ThreadPool(std::size_t thread_count) :
    terminate{false},
    invoked{0},
    total_invocations{0}
{
    // Spawn the requested number of threads
    while (threads.size() < thread_count)
    {
        threads.emplace_back(&ThreadPool::Loop, this);
    }
}

/*
 *  ThreadPool::~ThreadPool()
 *
 *  Description:
 *      Destructor for the ThreadPool object.
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
ThreadPool::~ThreadPool()
{
    // Lock the mutex
    std::unique_lock<std::mutex> lock(thread_pool_mutex);

    // Clear the vector invocations vector to prevent handling more requests
    invocations.clear();

    // Indicate that awoken threads should terminate
    terminate = true;

    // Unlock the mutex
    lock.unlock();

    // Have all the threads wake up
    cv.notify_all();

    // Wait for the threads to terminate
    for (auto &thread : threads) thread.join();
}

/*
 *  ThreadPool::Invoke()
 *
 *  Description:
 *      This function is called to invoke a thread to perform some asynchronous
 *      task.  It will be invoked immediately, if a thread is available, but
 *      my be invoked later once a servicing thread is available.
 *
 *  Parameters:
 *      thread_control [in]
 *          A ThreadControl object pointer that may be empty.  If the pointer
 *          points to a ThreadControl object, a call to BeginWork() will be
 *          made before attempting to call the entry_point function.  This
 *          is to ensure threads do not run when terminating or destroying
 *          objects.  Further, FinishWork() will be called upon completion
 *          so that the controller of the ThreadControl object an be
 *          assured that no threads are left outstanding.
 *
 *      entry_point [in]
 *          The function the thread should call.
 *
 *  Returns:
 *      True if the request is accepted, false if the request failed.  The
 *      request is only rejected if the function is not callable.
 *
 *  Comments:
 *      None.
 */
bool ThreadPool::Invoke(const ThreadControlPointer &thread_control,
                        const std::function<void()> &entry_point)
{
    // Ensure the function is callable
    if (!entry_point) return false;

    // Lock the mutex
    std::lock_guard<std::mutex> lock(thread_pool_mutex);

    // Put this request in the invocation deque
    invocations.emplace_back(thread_control, entry_point);

    // Notify a thread to service the request
    cv.notify_one();

    return true;
}

/*
 *  ThreadPool::ThreadCount()
 *
 *  Description:
 *      Returns the number of threads in the thread pool.
 *
 *  Parameters:
 *      None.
 *
 *  Returns:
 *      The number of threads in the thread pool.
 *
 *  Comments:
 *      This function primarily exists for unit testing, but may be useful
 *      to the application.
 */
std::size_t ThreadPool::ThreadCount()
{
    // Lock the mutex
    std::lock_guard<std::mutex> lock(thread_pool_mutex);

    return threads.size();
}

/*
 *  ThreadPool::Invoked()
 *
 *  Description:
 *      Returns how many threads in the pool are currently invoked.
 *
 *  Parameters:
 *      None.
 *
 *  Returns:
 *      The number of threads in the thread pool currently invoked.
 *
 *  Comments:
 *      This function primarily exists for unit testing, but may be useful
 *      to the application.
 */
std::size_t ThreadPool::Invoked()
{
    // Lock the mutex
    std::lock_guard<std::mutex> lock(thread_pool_mutex);

    return invoked;
}

/*
 *  ThreadPool::TotalInvocations()
 *
 *  Description:
 *      Returns the total number of invocations.
 *
 *  Parameters:
 *      None.
 *
 *  Returns:
 *      The total number of thread invocations.
 *
 *  Comments:
 *      This function primarily exists for unit testing, but may be useful
 *      to the application.
 */
std::uint64_t ThreadPool::TotalInvocations()
{
    // Lock the mutex
    std::lock_guard<std::mutex> lock(thread_pool_mutex);

    return total_invocations;
}

/*
 *  ThreadPool::Loop()
 *
 *  Description:
 *      This function is where idle threads sit until invoked.  When later
 *      invoked, those threads will handle the invocation request directly
 *      from within this function.
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
void ThreadPool::Loop()
{
    // Lock the mutex
    std::unique_lock<std::mutex> lock(thread_pool_mutex);

    // Loop until told to terminate
    while (!terminate)
    {
        // Thread waits here until released
        cv.wait(lock, [&]() { return terminate || !invocations.empty(); });

        // Keep looping if either terminate is set or there is nothing to do
        if (terminate || invocations.empty()) continue;

        // Copy the first item from the deque
        auto [thread_control, entry_point] = invocations.front();

        // Remove this element from the deque
        invocations.pop_front();

        // If using thread control, verify it was not halted
        if (thread_control && !thread_control->BeginWork()) continue;

        // Update the invocation count and total invocations
        invoked++;
        total_invocations++;

        // Unlock the mutex
        lock.unlock();

        try
        {
            // Invoke function; exceptions should handled by the called function
            entry_point();
        }
        catch (...)
        {
            // Catch and ignore all exceptions; those should be handled by the
            // called function
        }

        // Re-lock the mutex
        lock.lock();

        // Indicate that the call has completed
        if (thread_control) thread_control->FinishWork();

        // This thread is no longer invoked
        invoked--;
    }
}

} // namespace Terra::Spindle
