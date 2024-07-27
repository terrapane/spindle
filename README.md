# Spindle

Spindle is a library of utility classes useful for multi-threaded applications,
including the following:

* ThreadControl object
* ThreadPool object
* Timer

Each will be described further in the subsequent sections.

## ThreadControl

This object is useful in thread synchronization.  It is used by the ThreadPool
to facilitate invoking threads and ensuring threads are preventing from
executing a callback function or conveying the fact a thread has completed work.
However, its utility extends beyond just the ThreadPool.

At the heart of the ThreadControl object is the `BeginWork()` function.  The
idea is that a thread that wants to begin performing a task (e.g, in the
case of the `ThreadPool`, calling a function when a thread is invoked),
it will call the `BeginWork()` function to inform the ThreadControl object that
it is preparing to work.

The `BeginWork()` function will return `true` if it is allowed to proceed.
Once the thread has completed work, it will call `FinishWork()` to inform
the `ThreadControl` object that the thread has finished its task.

There would be some other thread that holds a shared pointer to this same
`ThreadControl` object.  If that other thread wishes to inform the `ThreadPool`
thread to halt, it would call the `Halt()` function.  The `Halt()` function
does not actually stop a thread once it is performing its task, but what it
will do is cause `BeginWork()` to return `false` so that the ThreadPool thread
would not even start.  If that ThreadPool thread has already called
`BeginWork()` and is executing a task, calling `Halt()` causes the thread to
block waiting for that ThreadPool thread to signal that it has finished its task
(via `FinishWork()`).  One can use this technique to ensure the `ThreadPool`
thread is no longer running in foreign code.

Multiple threads can be controlled using the same ThreadControl object.

Outside of the `ThreadPool`, one may use `ThreadControl` in cases where there
is a thread that awakens on some event (e.g., waiting on a condition variable).
Perhaps when the condition variable is signaled via `notify_one()` or
`notify_all()`, the thread can check to see if the `ThreadControl` object
is halted.  It can either query the object (`IsHalted()`) or is may attempt
to call `BeginWork()` and receive a `false` return value.

For the previous use case, one could just set a local boolean in a class
and perform much of the same logic, so `ThreadControl` may not offer more
value in that case.  However, it can be used to provide some consistency
so that the intent is obvious.

It is also possible for long-running threads to periodically check whether
a `ThreadControl` object has been halted.  Suppose there is a function
that is copying a large file that needs several minutes to complete.  If
the user interrupts the process, a `ThreadControl` object associated with
this file operation could be halted and the copy routine could check
periodically to see if it halted.

## ThreadPool

The `ThreadPool` object allows one to create a pool of worker threads
that may be called upon to perform tasks.  Tasks are placed in a queue
and then executed serially using all of the available threads.

While one can easily create a thread for a specific task in C++ easily,
managing threads does require a bit of work.  The `ThreadPool` does the
repetitive work and ensures proper handling of threads.  Coupled with
the `ThreadControl` object, it is very easy to initiate tasks and wait
for tasks to complete.  Importantly, too, is that spinning up new threads
is taxing, so having a pool of available threads that can be shared through
the system or some part of the system can improve performance.  When not
active, threads are essentially paused waiting on a mutex associated
with a condition variable.

## Timer

The `Timer` object performs tasks at a specified time and, optionally,
on a specified interval.  If one wishes to have a task performed
every 3 seconds, for example, the `Timer` object is perfect for that job.

Sometimes, one wants a task to rigidly start on a given interval and sometimes
there is a desire to not count the time required to execute the timer
Say, for example, a timer fire every 3 seconds, but it takes between 200ms
and 500ms for the timer to complete its task.  One may specify that the
timer should rigidly fire every 3s or 3s after the last task completes.

The `Timer` uses the `ThreadPool` to fire timers.  A `ThreadPool` may be
provided as a parameter in order to use a common pool.  If one is not
provided, the `Timer` object will create a `ThreadPool` with a single
serving thread.

Be mindful that the `Timer` performance will vary based on how many
timers are set, how long they run, how many threads are available, etc.
If there is a critical task that must be performed with a high degree of
accuracy, it may be best to create a Timer object with its own `ThreadPool`
that is separate from other general timers.

Note, also, that most systems do not awaken threads with a high degree
of accuracy.  This varies by platform.  On Windows, for example, it is
possible to get better performance when using `CreateWaitableTimerExW()`,
and so that is used when one indicates to use high resolution timers
in the constructor.  In general, one should only set that parameter to true
when timer accuracy to the ms or microsecond is critical.

Timers may be stopped by calling the `Stop()` function.  Alternatively,
if a `ThreadControl` pointer was passed in at the time of creation of the
timer, one may call `ThreadControl::Halt()` to halt any timer associated
with that `ThreadControl` object.  Be mindful that this will block the
thread as it waits for any actively running timers to complete their tasks.
