// SPDX-License-Identifier: GPL-2.0-only
/* Xiaomi MiSight event format; preserve the published header and exports. */
#include "mievent.h"
#include <linux/ktime.h>
#include <linux/limits.h>
#include "common.h"

/* Byte count after JSON escaping, excluding the surrounding quotes. */
static int escaped_size(const char *str)
{
	unsigned int len = 0, i;
	unsigned char c;

	if (!str)
		return -EINVAL;
	for (i = 0; i < BUF_MAX_SIZE; i++) {
		c = str[i];
		if (!c)
			return len;
		len += c < 0x20 ? 6 : (c == '"' || c == '\\' ? 2 : 1);
		if (len >= BUF_MAX_SIZE)
			return -EMSGSIZE;
	}
	return -EMSGSIZE;
}

static char *escape_copy(char *out, const char *str)
{
	static const char hex[] = "0123456789abcdef";
	unsigned char c;

	while ((c = *str++)) {
		if (c < 0x20) {
			*out++ = '\\'; *out++ = 'u'; *out++ = '0'; *out++ = '0';
			*out++ = hex[c >> 4]; *out++ = hex[c & 15];
		} else {
			if (c == '"' || c == '\\')
				*out++ = '\\';
			*out++ = c;
		}
	}
	return out;
}

struct misight_mievent *cdev_tevent_alloc(unsigned int eventid)
{
	struct misight_mievent *event;
	char **buf = miev_get_work_msg();
	int len, available = BUF_MAX_SIZE - sizeof(*event);

	if (!buf)
		return NULL;
	event = (struct misight_mievent *)*buf;
	event->buf_ptr = buf;
	event->eventid = eventid;
	event->time = ktime_get_real_seconds();
	event->para_cnt = 0;
	len = snprintf((char *)(event + 1), available,
		       "EventId %u -t %lld -paraList {", eventid, event->time);
	if (len < 0 || len >= available - 1) {
		miev_release_work_msg(buf);
		return NULL;
	}
	event->used_size = sizeof(*event) + len;
	return event;
}
EXPORT_SYMBOL_GPL(cdev_tevent_alloc);

static int add_field(struct misight_mievent *event, const char *key,
		     const char *value, bool string)
{
	int key_len, value_len, needed;
	char *out;

	if (!miev_event_writable(event))
		return -EINVAL;
	if (event->used_size < sizeof(*event) || event->used_size > BUF_MAX_SIZE - 2)
		return -EINVAL;
	key_len = escaped_size(key);
	value_len = string ? escaped_size(value) : strlen(value);
	if (key_len < 0 || value_len < 0)
		return key_len < 0 ? key_len : value_len;
	needed = key_len + value_len + 3 + (string ? 2 : 0) + !!event->para_cnt;
	/* Reserve both the final closing brace and NUL. Fail without mutation. */
	if (needed > BUF_MAX_SIZE - 2 - event->used_size)
		return -EMSGSIZE;
	out = (char *)event + event->used_size;
	if (event->para_cnt)
		*out++ = ',';
	*out++ = '"';
	out = escape_copy(out, key);
	*out++ = '"'; *out++ = ':';
	if (string) {
		*out++ = '"';
		out = escape_copy(out, value);
		*out++ = '"';
	} else {
		memcpy(out, value, value_len);
		out += value_len;
	}
	*out = '\0';
	event->used_size += needed;
	event->para_cnt++;
	return 0;
}

int cdev_tevent_add_int(struct misight_mievent *event, const char *key, long value)
{
	char number[24];

	snprintf(number, sizeof(number), "%ld", value);
	return add_field(event, key, number, false);
}
EXPORT_SYMBOL_GPL(cdev_tevent_add_int);

int cdev_tevent_add_str(struct misight_mievent *event, const char *key, const char *value)
{
	return add_field(event, key, value, true);
}
EXPORT_SYMBOL_GPL(cdev_tevent_add_str);

int cdev_tevent_write(struct misight_mievent *event)
{
	char *end;

	if (!miev_event_writable(event) || !event->para_cnt ||
	    event->used_size < sizeof(*event) || event->used_size > BUF_MAX_SIZE - 2)
		return -EINVAL;
	end = (char *)event + event->used_size;
	end[0] = '}'; end[1] = '\0';
	return write_kbuf(event->buf_ptr, sizeof(*event),
			  event->used_size - sizeof(*event) + 1);
}
EXPORT_SYMBOL_GPL(cdev_tevent_write);

void cdev_tevent_destroy(struct misight_mievent *event)
{
	/* A queued event is not writable, but it must still release its owner. */
	if (event)
		miev_release_work_msg(event->buf_ptr);
}
EXPORT_SYMBOL_GPL(cdev_tevent_destroy);
