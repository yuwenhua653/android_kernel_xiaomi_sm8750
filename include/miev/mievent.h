// MIUI ADD: MiSight_LogEnhance
#ifndef _EVENT_MIEVENT_H_
#define _EVENT_MIEVENT_H_

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>

#define FORMAT_COMMA_SIZE 1
#define FORMAT_COLON_SIZE 1
#define FORMAT_QUOTES_SIZE 2


enum DATA_TYPE { INT_T = 0, STR_T };

/* mievent struct */
struct misight_mievent {
	unsigned int eventid;
	unsigned int para_cnt;
	unsigned int used_size;
	long long time;
	char **buf_ptr;
};

struct mievent_payload {
	int type;
	char *key;
	char *value;
};

#if IS_REACHABLE(CONFIG_MIEV)
/* export symbol */
struct misight_mievent *cdev_tevent_alloc(unsigned int eventid);
int cdev_tevent_add_int(struct misight_mievent *event, const char *key,
			long value);
int cdev_tevent_add_str(struct misight_mievent *event, const char *key,
			const char *value);
int cdev_tevent_write(struct misight_mievent *event);
void cdev_tevent_destroy(struct misight_mievent *event);
#else
static inline struct misight_mievent *cdev_tevent_alloc(unsigned int eventid)
{

	return NULL; 
}
static inline int cdev_tevent_add_int(struct misight_mievent *event, const char *key,
                        long value)
{
	return -EOPNOTSUPP;
}
static inline int cdev_tevent_add_str(struct misight_mievent *event, const char *key,
                        const char *value)
{
	return -EOPNOTSUPP;
}
static inline int cdev_tevent_write(struct misight_mievent *event)
{
	return -EOPNOTSUPP;
}
static inline void cdev_tevent_destroy(struct misight_mievent *event)
{
    return;
}
#endif
#endif // _EVENT_MIEVENT_H_
// END MiSight_LogEnhance
