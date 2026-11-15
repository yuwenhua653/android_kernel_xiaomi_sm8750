/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __MCA_ADSP_GLINK_H
#define __MCA_ADSP_GLINK_H

#include <linux/types.h>
#define MCA_GLINK_DATA_MAX 256

/*
 * All callbacks run on a separate ordered worker and may read/write properties.
 * Do not register/unregister listeners from a callback. Unregister synchronously
 * before freeing callback data or unloading a listener. Callback buffers are
 * borrowed for the duration of the call. Registrations do not replay past events.
 *
 * Property/notification numbers are intentionally not defined here: each must
 * be verified against Annibale firmware before a charging policy can use it.
 */
struct mca_adsp_call_back {
	int (*glink_down_cb)(void *data);
	int (*sync_data_cb)(void *data);
	void (*notify_cb)(int notification, void *buf, int len, void *data);
};

/* The historical spelling is part of the exported source interface. */
int mca_adsp_glink_resister_ops(struct mca_adsp_call_back *cb, void *data);
int mca_adsp_glink_qbg_resister_ops(struct mca_adsp_call_back *cb, void *data);
int mca_adsp_glink_unregister_ops(struct mca_adsp_call_back *cb, void *data);
int mca_adsp_glink_qbg_unregister_ops(struct mca_adsp_call_back *cb, void *data);

/* Sleepable; 0..255 bytes, non-NULL data. Returns firmware code or negative errno.
 * Read buffers remain unchanged on failure. No retry of writes on timeout.
 */
int mca_adsp_glink_read_prop(u32 property_id, void *data, int len);
int mca_adsp_glink_write_prop(u32 property_id, void *data, int len);
int mca_adsp_glink_qbg_read_prop(u32 property_id, void *data, int len);
int mca_adsp_glink_qbg_write_prop(u32 property_id, void *data, int len);
#endif
