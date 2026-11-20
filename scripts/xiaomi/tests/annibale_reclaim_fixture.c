#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>
#define __read_mostly
#define HZ 100
#define DEF_PRIORITY 12
#define DECLARE_WAIT_QUEUE_HEAD(x) int x
#define DEFINE_SPINLOCK(n) pthread_mutex_t n=PTHREAD_MUTEX_INITIALIZER
#define spin_lock_irqsave(p,f) do{(f)=0;pthread_mutex_lock(p);}while(0)
#define spin_unlock_irqrestore(p,f) do{(void)(f);pthread_mutex_unlock(p);}while(0)
#define READ_ONCE(x) (x)
#define max(a,b) ((a)>(b)?(a):(b))
#define time_before(a,b) ((long)((a)-(b))<0)
typedef unsigned int gfp_t;
typedef int8_t s8;
typedef uint64_t u64;
struct task_struct {unsigned int flags;};struct kobject {int unused;};
struct css {int refs;bool online;};struct mem_cgroup {struct css css;};
struct folio {int mapcount;unsigned int pages;};
static unsigned long jiffies=100;
static int wakes;
static bool css_tryget_online(struct css *c) {if(!c->online)return false;c->refs++;return true;}
static void css_put(struct css *c) {assert(c->refs>0);c->refs--;}
static void wake_up_interruptible(int *p) {wakes++;}
static int folio_mapcount(struct folio *f) {return f->mapcount;}
static unsigned int folio_nr_pages(struct folio *f) {return f->pages;}
#define PF_MEMALLOC 1
#define PF_KSWAPD 2
#define wait_event_freezable(q,c) assert(c)
static struct task_struct worker_task;
#define current (&worker_task)
static bool stopping;
static int shrinks;
static bool kthread_should_stop(void) {return stopping;}
static void set_freezable(void) {}
static void set_async_slabd_cpus(void) {}
static unsigned long shrink_slab(gfp_t mask,int nid,struct mem_cgroup *cg,int priority) {
 assert(current->flags==(PF_MEMALLOC|PF_KSWAPD));
 if(cg) assert(cg->css.refs==1);
 shrinks++;stopping=true;return 1;
}
/* PRODUCTION */
static void *submit(void *p) {wakeup_shrink_slabd(1,0,p,4);return NULL;}
int main(void) {
 struct mem_cgroup cg={{0,true}};
 (void)kobj; (void)shrink_slabd_tsk;
 assert(!wakeup_shrink_slabd(1,0,&cg,4) && !cg.css.refs);
 async_shrink_slabd_setup=true;
 pthread_t th[16];for(int i=0;i<16;i++)assert(!pthread_create(&th[i],NULL,submit,&cg));
 for(int i=0;i<16;i++)pthread_join(th[i],NULL);
 assert(wakes==1 && cg.css.refs==1 && asp.pending);
 css_put(&cg.css);asp.pending=false;asp.memcg=NULL;
 assert(!wakeup_shrink_slabd(1,0,&cg,4));jiffies+=HZ;
 cg.css.online=false;assert(!wakeup_shrink_slabd(1,0,&cg,4) && !cg.css.refs);
 assert(wakeup_shrink_slabd(1,0,NULL,4));asp.pending=false;jiffies+=HZ;
 async_shrink_slabd_setup=false;cg.css.online=true;assert(!wakeup_shrink_slabd(1,0,&cg,4));
 async_shrink_slabd_setup=true;
 assert(wakeup_shrink_slabd(1,0,&cg,4));stopping=true;
 assert(!kshrink_slabd_func(NULL) && !cg.css.refs && !asp.pending && !shrinks && !current->flags);
 jiffies+=HZ;assert(wakeup_shrink_slabd(1,0,&cg,4));stopping=false;
 assert(!kshrink_slabd_func(NULL) && !cg.css.refs && !asp.pending && shrinks==1 && !current->flags);
 struct folio f={32,4};u64 skipped=0;int control=0;
 mi_folio_check_mapcount(NULL,&f,0,DEF_PRIORITY,&skipped,&control);assert(control==MI_FOLIOREF_ACTIVATE && skipped==4);
 f.mapcount=8;control=0;skipped=0;mi_folio_check_mapcount(NULL,&f,64,6,&skipped,&control);assert(control==MI_FOLIOREF_KEEP);
 f.mapcount=1;control=0;mi_folio_check_mapcount(NULL,&f,64,6,&skipped,&control);assert(control==MI_FOLIOREF_RECLAIM);
 f.mapcount=100;control=0;skipped=10;mi_folio_check_mapcount(NULL,&f,64,6,&skipped,&control);assert(control==MI_FOLIOREF_RECLAIM);
 skipped=100;control=0;mi_folio_check_mapcount(NULL,&f,1,6,&skipped,&control);assert(control==MI_FOLIOREF_ACTIVATE && skipped==4);
 mi_rmap_efficiency_setup=false;control=MI_FOLIOREF_KEEP;mi_folio_check_mapcount(NULL,&f,1,6,&skipped,&control);assert(control==MI_FOLIOREF_KEEP);
 mi_rmap_efficiency_setup=true;control=MI_FOLIOREF_KEEP;mi_folio_check_mapcount(NULL,&f,1,6,&skipped,&control);assert(control==MI_FOLIOREF_KEEP);
 puts("Reclaim: concurrent admission, memcg reference, offline/busy fallback and rmap thresholds passed");
}
