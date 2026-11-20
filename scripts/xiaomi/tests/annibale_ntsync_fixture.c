#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>
#define U64_MAX UINT64_MAX
#define NTSYNC_WAIT_REALTIME 1
#define ERESTARTSYS 512
#define TASK_INTERRUPTIBLE 1
#define TASK_RUNNING 0
#define HRTIMER_MODE_ABS 1
#define current NULL
typedef int64_t ktime_t;
struct ntsync_q {int signaled;};
struct ntsync_wait_args {uint64_t timeout;unsigned int flags;};
static int signals, state, calls;
static clockid_t observed_clock;
static bool infinite;
static ktime_t observed_timeout;
static ktime_t ns_to_ktime(uint64_t v) {return (ktime_t)v;}
static bool signal_pending(void *p) {return signals;}
static int atomic_read(const int *p) {return *p;}
static void set_current_state(int v) {state=v;}
static void __set_current_state(int v) {state=v;}
static int schedule_hrtimeout_range_clock(ktime_t *t,uint64_t delta,int mode,clockid_t clock) {
 assert(state==TASK_INTERRUPTIBLE && delta==0 && mode==HRTIMER_MODE_ABS);
 calls++;observed_clock=clock;infinite=!t;if(t)observed_timeout=*t;return 0;
}
/* PRODUCTION */
int main(void) {
 struct ntsync_q q={-1};struct ntsync_wait_args a={1234,0};
 assert(ntsync_schedule(&q,&a)==0 && calls==1 && observed_clock==CLOCK_MONOTONIC && observed_timeout==1234 && state==TASK_RUNNING);
 a.flags=NTSYNC_WAIT_REALTIME;a.timeout=U64_MAX;
 assert(ntsync_schedule(&q,&a)==0 && calls==2 && observed_clock==CLOCK_REALTIME && infinite);
 signals=1;assert(ntsync_schedule(&q,&a)==-ERESTARTSYS && calls==2 && state==TASK_RUNNING);
 signals=0;q.signaled=0;assert(ntsync_schedule(&q,&a)==0 && calls==2 && state==TASK_RUNNING);
 puts("NTSYNC: direct timer provider, absolute/realtime clocks, infinite wait, signal and early wake passed");
}
