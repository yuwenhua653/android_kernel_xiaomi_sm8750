#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <pthread.h>
#define BUF_MAX_SIZE 4096
#define __user
#define THIS_MODULE NULL
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define DEFINE_RAW_SPINLOCK(n) pthread_mutex_t n = PTHREAD_MUTEX_INITIALIZER
#define raw_spin_lock_irqsave(p,f) do {(f)=0;pthread_mutex_lock(p);} while(0)
#define raw_spin_unlock_irqrestore(p,f) do {(void)(f);pthread_mutex_unlock(p);} while(0)
#define smp_load_acquire(p) (*(p))
#define mutex_lock(p) pthread_mutex_lock(p)
#define mutex_unlock(p) pthread_mutex_unlock(p)
struct file { int unused; };
struct kfifo { unsigned char data[65536]; unsigned int in,out; };
static unsigned int kfifo_len(struct kfifo *f) { return f->in-f->out; }
static unsigned int kfifo_avail(struct kfifo *f) { return sizeof(f->data)-kfifo_len(f); }
static bool kfifo_is_empty(struct kfifo *f) { return f->in==f->out; }
static void kfifo_reset(struct kfifo *f) { f->in=f->out=0; }
static unsigned int kfifo_in(struct kfifo *f, const void *p, unsigned int n) {
 if(n>kfifo_avail(f)) n=kfifo_avail(f);
 for(unsigned int i=0;i<n;i++)f->data[(f->in+i)%sizeof(f->data)]=((const char *)p)[i];
 f->in+=n;return n;
}
static unsigned int kfifo_out_peek(struct kfifo *f, void *p, unsigned int n) {
 if(n>kfifo_len(f)) n=kfifo_len(f);
 for(unsigned int i=0;i<n;i++)((char *)p)[i]=f->data[(f->out+i)%sizeof(f->data)];
 return n;
}
static unsigned int kfifo_out(struct kfifo *f,void *p,unsigned int n) { n=kfifo_out_peek(f,p,n);f->out+=n;return n; }
struct miev_device { struct kfifo fifo; pthread_mutex_t lock; int wait_queue; char cbuf[BUF_MAX_SIZE+sizeof(int)]; };
struct work_struct { void (*fn)(struct work_struct *); bool queued; };
struct workqueue_struct { int unused; };
static bool queue_ok=true, copy_fault;
static int references;
static bool try_module_get(void *p) {references++;return true;}
static void module_put(void *p) {assert(references>0);references--;}
static void wake_up_interruptible(int *p) {}
static bool queue_work(struct workqueue_struct *q,struct work_struct *w) {
 if(!queue_ok||w->queued) return false;
 w->queued=true;return true;
}
static int copy_to_user(void *d,const void *s,size_t n) {if(copy_fault)return 1;memcpy(d,s,n);return 0;}
/* PRODUCTION */
static void drain(void) {
 for(int i=0;i<WORK_MAX_CNT;i++)if(miev_works[i].wk.queued){
  miev_works[i].wk.queued=false;miev_works[i].wk.fn(&miev_works[i].wk);
 }
}
int main(void) {
 struct workqueue_struct queue={0};char data[BUF_MAX_SIZE], out[BUF_MAX_SIZE];
 miev_dev=calloc(1,sizeof(*miev_dev));assert(miev_dev);pthread_mutex_init(&miev_dev->lock,NULL);
 miev_workqueue=&queue;bInited=true;
 for(int i=0;i<WORK_MAX_CNT;i++){miev_works[i].msg=calloc(1,BUF_MAX_SIZE);miev_works[i].wk.fn=do_miev_work;}
 /* No daemon (AOSP): fill well beyond capacity, retain whole recent records. */
 for(int i=0;i<64;i++){memset(data,i,sizeof(data));assert(write_buf_inner(data,sizeof(data))==sizeof(data));}
 assert(kfifo_len(&miev_dev->fifo)==15*(BUF_MAX_SIZE+sizeof(int)));
 unsigned int before=kfifo_len(&miev_dev->fifo);
 char tiny[1]={0};assert(miev_read(NULL,tiny,1,NULL)==-EMSGSIZE);
 assert(kfifo_len(&miev_dev->fifo)==before && tiny[0]==0);
 copy_fault=true;assert(miev_read(NULL,out,sizeof(out),NULL)==-EFAULT);copy_fault=false;
 assert(kfifo_len(&miev_dev->fifo)==before);
 for(int i=49;i<64;i++){assert(miev_read(NULL,out,sizeof(out),NULL)==BUF_MAX_SIZE);assert((unsigned char)out[0]==i && (unsigned char)out[4095]==i);}
 assert(miev_read(NULL,out,sizeof(out),NULL)==0);
 int bad=-1;kfifo_in(&miev_dev->fifo,&bad,sizeof(bad));assert(miev_read(NULL,out,sizeof(out),NULL)==-EIO && kfifo_is_empty(&miev_dev->fifo));
 /* All pool slots pinned; queued work owns bytes after caller destruction. */
 char **slots[WORK_MAX_CNT];for(int i=0;i<WORK_MAX_CNT;i++){slots[i]=miev_get_work_msg();assert(slots[i]);}
 assert(!miev_get_work_msg() && references==WORK_MAX_CNT);
 assert(write_kbuf(slots[0],-1,1)==-EINVAL);
 assert(write_kbuf(slots[0],4095,2)==-EINVAL);
 assert(write_kbuf(slots[0],0,0)==-EINVAL);
 strcpy(*slots[0],"queued");assert(write_kbuf(slots[0],0,6)==0);
 assert(write_kbuf(slots[0],0,6)==-EBUSY);
 assert(!miev_event_writable(*slots[0]));miev_release_work_msg(slots[0]);
 assert(!miev_get_work_msg());drain();
 assert(miev_read(NULL,out,sizeof(out),NULL)==6 && !memcmp(out,"queued",6));
 slots[0]=miev_get_work_msg();assert(slots[0] && **slots[0]==0);
 queue_ok=false;assert(write_kbuf(slots[0],0,1)==-EBUSY && miev_event_writable(*slots[0]));queue_ok=true;
 assert(write_kbuf(slots[0],0,1)==0);drain();assert(!miev_get_work_msg());
 for(int i=0;i<WORK_MAX_CNT;i++) {miev_release_work_msg(slots[i]);}
 assert(!references);
 miev_release_work_msg(slots[0]);assert(!references);
 char **foreign=(char **)&out[1];miev_release_work_msg(foreign);assert(write_kbuf(foreign,0,1)==-EINVAL);
 bInited=false;assert(!miev_get_work_msg());
 for(int i=0;i<WORK_MAX_CNT;i++)free(miev_works[i].msg);
 pthread_mutex_destroy(&miev_dev->lock);free(miev_dev);
 puts("MIEV queue: no reader, short/fault reads, FIFO integrity, pool ownership and shutdown gate passed");
}
