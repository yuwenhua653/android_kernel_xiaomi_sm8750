#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
typedef int64_t time64_t;
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define DEFINE_SPINLOCK(n) pthread_mutex_t n=PTHREAD_MUTEX_INITIALIZER
#define spin_lock_irqsave(p,f) do{(f)=0;pthread_mutex_lock(p);}while(0)
#define spin_unlock_irqrestore(p,f) do{(void)(f);pthread_mutex_unlock(p);}while(0)
#define EXPORT_SYMBOL_GPL(n)
#define mca_log_err(...) ((void)0)
#define mca_log_jirabot(...) ((void)0)
/* CONTRACT */
struct misight_mievent { int id; };
struct mca_event_notify_data {const char *event;int event_len;};
static int allocations, frees, writes, notices, field_error;
static bool allocation_fail;
static time64_t now=1000;
static time64_t ktime_get_boottime_seconds(void) {return now;}
static struct misight_mievent *cdev_tevent_alloc(unsigned int id) {
 if(allocation_fail)return NULL;
 struct misight_mievent *e=malloc(sizeof(*e));assert(e);e->id=id;allocations++;return e;
}
static int cdev_tevent_add_str(struct misight_mievent *e,const char *k,const char *v) {assert(k&&v);return field_error;}
static int cdev_tevent_add_int(struct misight_mievent *e,const char *k,long v) {assert(k);return field_error;}
static int cdev_tevent_write(struct misight_mievent *e) {writes++;return 0;}
static void cdev_tevent_destroy(struct misight_mievent *e) {assert(e);free(e);frees++;}
static void mca_event_report_uevent(const struct mca_event_notify_data *n) {assert(n->event_len==18);notices++;}
/* PRODUCTION */
static int claims;
static pthread_mutex_t count_lock=PTHREAD_MUTEX_INITIALIZER;
static void *claim(void *p) {
 bool won=mca_mievent_claim(&g_charge_mievent_info[CHARGE_DFX_PD_AUTH_FAILED]);
 if(won){pthread_mutex_lock(&count_lock);claims++;pthread_mutex_unlock(&count_lock);}return NULL;
}
int main(void) {
 int value=7;
 mca_charge_mievent_report(-1,&value,1);mca_charge_mievent_report(CHARGE_DFX_MAX_NUM,&value,1);
 mca_charge_mievent_report(CHARGE_DFX_PD_AUTH_FAILED,NULL,1);
 mca_charge_mievent_report(CHARGE_DFX_PD_AUTH_FAILED,&value,-1);assert(!allocations);
 allocation_fail=true;mca_charge_mievent_report(CHARGE_DFX_PD_AUTH_FAILED,&value,1);
 allocation_fail=false;assert(!g_upload_type_plug_info[PLUG_TYPE_PD_AUTH_FAILED].count);
 field_error=-EMSGSIZE;mca_charge_mievent_report(CHARGE_DFX_PD_AUTH_FAILED,&value,1);
 field_error=0;assert(!writes && !notices && allocations==frees);
 mca_charge_mievent_report(CHARGE_DFX_PD_AUTH_FAILED,&value,1);
 mca_charge_mievent_report(CHARGE_DFX_PD_AUTH_FAILED,&value,1);assert(writes==1 && notices==1 && allocations==frees);
 mca_charge_mievent_set_state(MIEVENT_STATE_END,CHARGE_DFX_PD_AUTH_FAILED);
 pthread_t threads[16];for(int i=0;i<16;i++)assert(!pthread_create(&threads[i],NULL,claim,NULL));
 for(int i=0;i<16;i++) {pthread_join(threads[i],NULL);}
 assert(claims==1);
 mca_charge_mievent_set_state(MIEVENT_STATE_PLUG,0);assert(!g_upload_type_plug_info[PLUG_TYPE_PD_AUTH_FAILED].count);
 assert(mca_mievent_claim(&g_charge_mievent_info[CHARGE_DFX_FG_IIC_ERR]));
 assert(!mca_mievent_claim(&g_charge_mievent_info[CHARGE_DFX_FG_IIC_ERR]));now+=MINOR_TIMER_S;
 assert(mca_mievent_claim(&g_charge_mievent_info[CHARGE_DFX_FG_IIC_ERR]));
 char *bad[]={NULL};int before=allocations;
 mca_charge_mievent_report(CHARGE_DFX_WLS_FW_UPGRADE_FAIL,bad,1);assert(allocations==before);
 mca_charge_mievent_set_state(MIEVENT_STATE_END,-1);mca_charge_mievent_set_state(MIEVENT_STATE_END,CHARGE_DFX_MAX_NUM);
 puts("MCA events: invalid inputs, allocation/format failure, concurrent limits and reset passed");
}
