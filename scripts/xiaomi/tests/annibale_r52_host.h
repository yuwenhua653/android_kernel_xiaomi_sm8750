/* SPDX-License-Identifier: GPL-2.0-only */
#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#ifdef _WIN32
#include <windows.h>
typedef intptr_t ssize_t;
typedef SRWLOCK test_lock;
#define TEST_LOCK_INIT SRWLOCK_INIT
static void lock_write(test_lock *l) { AcquireSRWLockExclusive(l); }
static void unlock_write(test_lock *l) { ReleaseSRWLockExclusive(l); }
static void lock_read(test_lock *l) { AcquireSRWLockShared(l); }
static void unlock_read(test_lock *l) { ReleaseSRWLockShared(l); }
typedef HANDLE test_thread;
typedef HANDLE test_event;
static void event_init(test_event *e) { *e = CreateEvent(NULL, TRUE, FALSE, NULL); assert(*e); }
static void event_set(test_event *e) { assert(SetEvent(*e)); }
static void event_wait(test_event *e) { assert(WaitForSingleObject(*e, 5000) == WAIT_OBJECT_0); }
static bool event_ready(test_event *e) { return WaitForSingleObject(*e, 0) == WAIT_OBJECT_0; }
static void event_destroy(test_event *e) { assert(CloseHandle(*e)); }
struct thread_start { void (*fn)(void *); void *arg; };
static DWORD WINAPI thread_entry(void *p) {
	struct thread_start start = *(struct thread_start *)p;
	free(p); start.fn(start.arg); return 0;
}
static void thread_create(test_thread *t, void (*fn)(void *), void *arg) {
	struct thread_start *p = malloc(sizeof(*p)); assert(p);
	p->fn = fn; p->arg = arg; *t = CreateThread(NULL, 0, thread_entry, p, 0, NULL); assert(*t);
}
static void thread_join(test_thread t) { assert(WaitForSingleObject(t, 5000) == WAIT_OBJECT_0); assert(CloseHandle(t)); }
#else
#include <pthread.h>
#include <sys/types.h>
typedef pthread_rwlock_t test_lock;
#define TEST_LOCK_INIT PTHREAD_RWLOCK_INITIALIZER
static void lock_write(test_lock *l) { assert(!pthread_rwlock_wrlock(l)); }
static void unlock_write(test_lock *l) { assert(!pthread_rwlock_unlock(l)); }
static void lock_read(test_lock *l) { assert(!pthread_rwlock_rdlock(l)); }
static void unlock_read(test_lock *l) { assert(!pthread_rwlock_unlock(l)); }
typedef pthread_t test_thread;
typedef struct { pthread_mutex_t lock; pthread_cond_t cond; bool ready; } test_event;
static void event_init(test_event *e) { assert(!pthread_mutex_init(&e->lock, NULL)); assert(!pthread_cond_init(&e->cond, NULL)); e->ready = false; }
static void event_set(test_event *e) { pthread_mutex_lock(&e->lock); e->ready = true; pthread_cond_broadcast(&e->cond); pthread_mutex_unlock(&e->lock); }
static void event_wait(test_event *e) { pthread_mutex_lock(&e->lock); while (!e->ready) pthread_cond_wait(&e->cond, &e->lock); pthread_mutex_unlock(&e->lock); }
static bool event_ready(test_event *e) { bool ready; pthread_mutex_lock(&e->lock); ready = e->ready; pthread_mutex_unlock(&e->lock); return ready; }
static void event_destroy(test_event *e) { pthread_cond_destroy(&e->cond); pthread_mutex_destroy(&e->lock); }
static void thread_create(test_thread *t, void (*fn)(void *), void *arg);
struct thread_start { void (*fn)(void *); void *arg; };
static void *thread_entry(void *p) { struct thread_start start = *(struct thread_start *)p; free(p); start.fn(start.arg); return NULL; }
static void thread_create(test_thread *t, void (*fn)(void *), void *arg) { struct thread_start *p = malloc(sizeof(*p)); assert(p); p->fn = fn; p->arg = arg; assert(!pthread_create(t, NULL, thread_entry, p)); }
static void thread_join(test_thread t) { assert(!pthread_join(t, NULL)); }
#endif
#define EXPORT_SYMBOL(x)
#define EXPORT_SYMBOL_GPL(x)
#define __init
#define __exit
#define ARRAY_SIZE(x) ((int)(sizeof(x) / sizeof((x)[0])))
#ifndef min
#define min(a,b) ((a) < (b) ? (a) : (b))
#endif
#define pr_warn(...) ((void)0)
#define pr_err(...) ((void)0)
#define pr_info(...) ((void)0)
#define PAGE_SIZE 4096
#define DECLARE_RWSEM(x) test_lock x = TEST_LOCK_INIT
#define down_read lock_read
#define up_read unlock_read
#define down_write lock_write
#define up_write unlock_write
#define DEFINE_MUTEX(x) test_lock x = TEST_LOCK_INIT
static int mutex_depth;
static void mutex_lock(test_lock *l) { lock_write(l); mutex_depth++; }
static void mutex_unlock(test_lock *l) { mutex_depth--; unlock_write(l); }
#define GFP_KERNEL 0
#define ERR_PTR(x) ((void *)(intptr_t)(x))
#define PTR_ERR(x) ((int)(intptr_t)(x))
#define IS_ERR(x) ((uintptr_t)(x) >= (uintptr_t)-4095)
#define IS_ERR_OR_NULL(x) (!(x) || IS_ERR(x))
#define KERNEL_VERSION(a,b,c) (((a)<<16)|((b)<<8)|(c))
#define LINUX_VERSION_CODE KERNEL_VERSION(6,6,144)
