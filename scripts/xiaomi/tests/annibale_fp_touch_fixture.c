/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define FP_ULTRA_QCOM_P11
#define EV_ABS 3
#define ABS_MT_SLOT 0x2f
#define ABS_MT_POSITION_X 0x35
#define ABS_MT_POSITION_Y 0x36
#define ABS_MT_TRACKING_ID 0x39
struct input_handle { int unused; };
static struct {
 struct { int SLOT_CUR, touch_x[32], touch_y[32], TRACKING_ID[32]; } position;
} data, *fp_dev = &data;
/* PRODUCTION */
int main(void)
{
 xiaomifp_input_event(NULL, EV_ABS, ABS_MT_SLOT, 31);
 xiaomifp_input_event(NULL, EV_ABS, ABS_MT_POSITION_X, 600);
 assert(data.position.touch_x[31] == 6);
 xiaomifp_input_event(NULL, EV_ABS, ABS_MT_SLOT, 32);
 xiaomifp_input_event(NULL, EV_ABS, ABS_MT_POSITION_X, 900);
 assert(data.position.SLOT_CUR == -1 && data.position.touch_x[31] == 6);
 xiaomifp_input_event(NULL, EV_ABS, ABS_MT_SLOT, -10);
 xiaomifp_input_event(NULL, EV_ABS, ABS_MT_TRACKING_ID, 77);
 assert(data.position.SLOT_CUR == -1 && data.position.TRACKING_ID[0] == 0);
 xiaomifp_input_event(NULL, EV_ABS, ABS_MT_SLOT, 0);
 xiaomifp_input_event(NULL, EV_ABS, ABS_MT_POSITION_Y, 800);
 xiaomifp_input_event(NULL, EV_ABS, ABS_MT_TRACKING_ID, -1);
 assert(data.position.touch_y[0] == 8 && data.position.TRACKING_ID[0] == -1);
 puts("PASS: fingerprint negative/overflow touch slots ignored; valid slots recover");
 return 0;
}
