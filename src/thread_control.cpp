/*
 *  thread_control.cpp
 *
 *  Copyright (C) 2024
 *  Terrapane Corporation
 *  All Rights Reserved
 *
 *  Author:
 *      Paul E. Jones <paulej@packetizer.com>
 *
 *  Description:
 *      This file implements the class ThreadControl, which controls threads
 *      of execution through code.  Threads can call into an instance of this
 *      object to ensure it is safe to carry out a task, indicate it is
 *      currently running, and indicate that it has completed a task.
 *
 *      This object utilizes atomic variables to reduce the time threads might
 *      otherwise be required to wait on a mutex.  However, a mutex is defined
 *      as a part of the class.  It is used only when it is absolutely necessary
 *      to force a thread the pause while waiting for other threads to complete
 *      work.  You can see that in the Halt() function, which is generally
 *      called during object destruction or program termination.
 *
 *  Portability Issues:
 *      None.
 */

#include <terra/spindle/thread_control.h>

namespace Terra::Spindle
{

/*
 *  ThreadControl::ThreadControl()
 *
 *  Description:
 *      Constructor for the ThreadControl object.
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
ThreadControl::ThreadControl() : running_threads{0}, halted{false}
{
}

/*
 *  ThreadControl::~ThreadControl()
 *
 *  Description:
 *      Destructor for the ThreadControl object.
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
ThreadControl::~ThreadControl()
{
    // Ensure all threads have finished
    Halt();
}

/*
 *  ThreadControl::BeginWork()
 *
 *  Description:
 *      This function is called by a thread that wishes to begin work.  The
 *      thread should only proceed if this function returns true, indicating
 *      that the ThreadControl is not in a halted state.
 *
 *  Parameters:
 *      None.
 *
 *  Returns:
 *      True if the thread is allowed to begin work or false if it is denied.
 *
 *  Comments:
 *      None.
 */
bool ThreadControl::BeginWork()
{
    // Indicate there is running thread
    running_threads++;

    // If the object is not in a halted state, return true
    if (!halted.load()) return true;

    // In halted state, notify waiting threads if are no more threads running
    if (--running_threads == 0) NotifyThreads();

    return false;
}

/*
 *  ThreadControl::FinishWork()
 *
 *  Description:
 *      This function is called by a thread that had previously called
 *      BeginWork() to notify the ThreadControl that work has completed.
 *      This function must be called only once and exactly once
 *      for each successful call to BeginWork() (i.e., true was returned).
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
void ThreadControl::FinishWork()
{
    // Indicate that the thread is no longer running
    running_threads--;

    // Notify waiting threads that there are no more threads running
    if ((halted.load()) && (running_threads == 0)) NotifyThreads();
}

/*
 *  ThreadControl::Halt()
 *
 *  Description:
 *      Halt the ThreadControl to prevent threads from beginning new work.  This
 *      function will return only after all threads have finished work.  Threads
 *      cannot successfully call BeginWork() once Halt() is called unless
 *      Resume() is first called.  This function is intended to be called by
 *      the logic that controls worker threads, not by worker threads under
 *      the control of this class.
 *
 *  Parameters:
 *      None.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      If Resume() is called while another thread is in this function waiting
 *      for threads to complete, that waiting thread will exit.  However,
 *      calling Resume() in such a case leaves the caller in a state where
 *      it will not know the true state of this object.  See Resume() for
 *      further discussion.
 */
void ThreadControl::Halt()
{
    // Indicate that the ThreadControl is in the halted state
    halted = true;

    // If there are running threads, wait for those to complete
    if (running_threads > 0)
    {
        // Lock the mutex
        std::unique_lock<std::mutex> lock(thread_control_mutex);

        // Wait for running threads to complete or for Resume() to be called
        cv.wait(lock,
                [&]() { return (running_threads == 0) || (!halted.load()); });
    }
}

/*
 *  ThreadControl::Halt()
 *
 *  Description:
 *      Halt the ThreadControl to prevent threads from beginning new work.  This
 *      function will return only after all threads have finished work.  Threads
 *      cannot successfully call BeginWork() once Halt() is called unless
 *      Resume() is first called.  This function is intended to be called by
 *      the logic that controls worker threads, not by worker threads under
 *      the control of this class.
 *
 *  Parameters:
 *      caller_lock [in]
 *          This is a reference to a unique_lock held by the caller that
 *          should be unlocked if and only if this function must block in
 *          order to wait on on a thread to complete.  This lock will not
 *          be unlocked if it is not necessary to wait.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      If Resume() is called while another thread is in this function waiting
 *      for threads to complete, that waiting thread will exit.  However,
 *      calling Resume() in such a case leaves the caller in a state where
 *      it will not know the true state of this object.  See Resume() for
 *      further discussion.
 */
void ThreadControl::Halt(std::unique_lock<std::mutex> &foreign_lock)
{
    // Indicate that the ThreadControl is in the halted state
    halted = true;

    // If there are running threads, wait for those to complete
    if (running_threads > 0)
    {
        // Unlock the mutex provided by the caller
        foreign_lock.unlock();

        // Lock the mutex
        std::unique_lock<std::mutex> lock(thread_control_mutex);

        // Wait for running threads to complete or for Resume() to be called
        cv.wait(lock,
                [&]() { return (running_threads == 0) || (!halted.load()); });

        // Re-lock the mutex provided by the caller
        foreign_lock.lock();
    }
}

/*
 *  ThreadControl::Resume()
 *
 *  Description:
 *      This function takes the object out of the halted state so that threads
 *      that subsequently call BeginWork() will be able to do so successfully.
 *      This function is intended to be called by the logic that controls worker
 *      threads, not by worker threads under the control of this class.
 *
 *  Parameters:
 *      None.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      Care should be taken when calling this function to ensure that Resume()
 *      does not conflict with a call to Halt().  Generally, Halt() is called
 *      only when a program is terminating or an object is being destroyed.
 *      As such, this function is not used in most use cases for which this
 *      class was written.  However, there are some use cases where an object
 *      (and associated thread activity) is merely paused and this facilitates
 *      those use cases.
 */
void ThreadControl::Resume()
{
    // Take the object out of the halted state
    halted = false;

    // Notify any waiting threads to awaken
    NotifyThreads();
}

/*
 *  ThreadControl::IsHalted()
 *
 *  Description:
 *      This function will return true if the object is in a halted state,
 *      false if it is not.
 *
 *  Parameters:
 *      None.
 *
 *  Returns:
 *      True if the object is in a halted state, false of not.
 *
 *  Comments:
 *      None.
 */
bool ThreadControl::IsHalted()
{
    return halted;
}

/*
 *  ThreadControl::RunningThreads()
 *
 *  Description:
 *      This function will return a count of the number of currently running
 *      threads.
 *
 *  Parameters:
 *      None.
 *
 *  Returns:
 *      Returns a count of the current number of running threads.
 *
 *  Comments:
 *      None.
 */
std::size_t ThreadControl::RunningThreads()
{
    return running_threads;
}

/*
 *  ThreadControl::NotifyThreads()
 *
 *  Description:
 *      Notify any thread waiting on the ThreadControl mutex so that it may
 *      resume.
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
inline void ThreadControl::NotifyThreads()
{
    // Lock the ThreadControl mutex
    std::unique_lock<std::mutex> lock(thread_control_mutex);

    // Notify any waiting threads to awaken
    cv.notify_all();
}

} // namespace Terra::Spindle
