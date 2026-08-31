/*----------------------------------------------------------------------
| gwthread.h
|
| This file contains the headers and definitions that are used to implement
| multi-threading.  Specifically, threads, mutexes, and events.
|
| Gwnum math routines use these routines to implement multi-threaded
| multiplications.  However, you can use these routines without using
| the rest of the gwnum package if you so desire.
|
| NOTE:  MFC users may not be able to use the thread creation routines
| because we call the C runtime library thread create rather than the
| MFC thread create routine. 
|
|  Copyright 2006-2023 Mersenne Research, Inc.  All rights reserved.
+---------------------------------------------------------------------*/

#ifndef _GWTHREAD_H
#define _GWTHREAD_H

/* This is a C library.  If used in a C++ program, don't let the C++ compiler mangle names. */

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
*                            Atomic Int Routines                              *
******************************************************************************/

// Hack to workaround MSVC's appalling lack of support for atomics in C, gcc 4.8's missing stdatomic.h, and gcc 8's inability to mix _Atomic in C++ code.
// Instead we use C++11 std::atomic in gwthread.cpp and export the few routines we need in a simple C interface.

typedef int64_t gwatomic;	/* Definition of an atomic int */

#define atomic_set(x,v)		gwatomic_set(&(x), v)				// Equivalent to x = v
#define atomic_get(x)		gwatomic_get(&(x))				// Equivalent to x
#define atomic_incr(x)		((void) gwatomic_fetch_increment(&(x)))		// Increment x, do not return a value
#define atomic_decr(x)		((void) gwatomic_fetch_decrement(&(x)))		// Decrement x, do not return a value
#define atomic_fetch_incr(x)	(gwatomic_fetch_increment(&(x)))		// Equivalent to x++
#define atomic_incr_fetch(x)	(gwatomic_fetch_increment(&(x)) + 1)		// Equivalent to ++x
#define atomic_fetch_decr(x)	(gwatomic_fetch_decrement(&(x)))		// Equivalent to x--
#define atomic_decr_fetch(x)	(gwatomic_fetch_decrement(&(x)) - 1)		// Equivalent to --x
#define atomic_fetch_addin(x,v)	(gwatomic_fetch_add(&(x), v))			// Equivalent to { tmp = x; x += v; return (x); }
#define atomic_spinwait(x,v)	gwatomic_spinwait(&(x), v)			// Equivalent to while (x != v)

void gwatomic_set (gwatomic *x, int64_t val);
int64_t gwatomic_get (gwatomic *x);
int64_t gwatomic_fetch_increment (gwatomic *x);
int64_t gwatomic_fetch_decrement (gwatomic *x);
int64_t gwatomic_fetch_add (gwatomic *x, int64_t val);
void gwatomic_spinwait (gwatomic *x, int64_t val);

/******************************************************************************
*                         Mutex and Events Routines                           *
******************************************************************************/

typedef void *gwmutex;			/* Definition of a mutex */

void gwmutex_init (gwmutex *mutex);	/* Mutex to init */
void gwmutex_lock (gwmutex *mutex);	/* Mutex to lock.  This is a no-op if *mutex is null. */
void gwmutex_unlock (gwmutex *mutex);	/* Mutex to unlock.  This is a no-op if *mutex is null. */
void gwmutex_destroy (gwmutex *mutex);	/* Mutex to destroy.  This is a no-op if *mutex is null. */

typedef void *gwevent;			/* Definition of an event handle */

void gwevent_init (gwevent *event);	/* Event to init */
#define GWEVENT_TIMED_OUT	1
#define GWEVENT_SIGNALED	2
int gwevent_wait (gwevent *event, int seconds);
void gwevent_signal (gwevent *event);	/* Event to signal */
void gwevent_reset (gwevent *event);	/* Event to reset */
void gwevent_destroy (gwevent *event);	/* Event to destroy */

/******************************************************************************
*                           Thread Routines                                   *
******************************************************************************/

typedef void *gwthread;			/* Definition of a thread handle */

void gwthread_create (gwthread *thread_id, void (*thread_proc)(void *), void *arg);
void gwthread_create_waitable (gwthread *thread_id, void (*thread_proc)(void *), void *arg);
void gwthread_wait_for_exit (gwthread *thread_id);

#ifdef __cplusplus
}
#endif

#endif
