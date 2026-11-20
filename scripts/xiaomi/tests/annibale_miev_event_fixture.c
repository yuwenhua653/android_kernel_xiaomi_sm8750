#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#define BUF_MAX_SIZE 4096
#define EXPORT_SYMBOL_GPL(x)
/* EVENT */
static char *slot;
static char emitted[BUF_MAX_SIZE];
static int active, queued, write_error;
static long long ktime_get_real_seconds(void) { return 1234567; }
static char **miev_get_work_msg(void) {
 if (active) return NULL;
 slot = calloc(1, BUF_MAX_SIZE); assert(slot); active = 1; queued = 0; return &slot;
}
static bool miev_event_writable(const void *p) { return active && !queued && p == slot; }
static void miev_release_work_msg(char **p) { assert(p == &slot && active); free(slot); slot=NULL; active=queued=0; }
static int write_kbuf(char **p, int offset, int size) {
 assert(p == &slot && offset >= 0 && size > 0 && offset + size <= BUF_MAX_SIZE);
 if (write_error) return write_error;
 memcpy(emitted, *p + offset, size); emitted[size] = 0; queued=1; return 0;
}
/* PRODUCTION */
int main(void) {
 struct misight_mievent *e = cdev_tevent_alloc(909001004);
 assert(e && !cdev_tevent_alloc(2));
 assert(cdev_tevent_add_str(e, "k\"\\", "a\n\t\001\\\"中") == 0);
 assert(cdev_tevent_add_int(e, "minimum", LONG_MIN) == 0);
 assert(cdev_tevent_add_str(e, NULL, "x") == -EINVAL);
 assert(cdev_tevent_add_str(e, "x", NULL) == -EINVAL);
 unsigned int before=e->used_size;
 char too_big[BUF_MAX_SIZE]; memset(too_big,'x',sizeof(too_big)-1); too_big[sizeof(too_big)-1]=0;
 assert(cdev_tevent_add_str(e,"long",too_big)==-EMSGSIZE && e->used_size==before);
 write_error=-EBUSY; assert(cdev_tevent_write(e)==-EBUSY && e->used_size==before);
 write_error=0; assert(cdev_tevent_write(e)==0);
 assert(strstr(emitted,"EventId 909001004 -t 1234567 -paraList {"));
 assert(strstr(emitted,"\"k\\\"\\\\\":\"a\\u000a\\u0009\\u0001\\\\\\\"中\""));
 assert(strstr(emitted,"\"minimum\":-9223372036854775808}"));
 assert(cdev_tevent_add_int(e,"late",1)==-EINVAL);
 cdev_tevent_destroy(e); assert(!active);
 e=cdev_tevent_alloc(1); assert(cdev_tevent_write(e)==-EINVAL);
 assert(cdev_tevent_add_str(e,"","")==0);
 while (cdev_tevent_add_int(e,"a",1234)==0) {}
 assert(e->used_size <= BUF_MAX_SIZE-2 && cdev_tevent_write(e)==0);
 cdev_tevent_destroy(e);
 assert(cdev_tevent_write(NULL)==-EINVAL); cdev_tevent_destroy(NULL);
 puts("MIEV serialization: escaping, nulls, LONG_MIN, full buffers, queue failure passed");
}
