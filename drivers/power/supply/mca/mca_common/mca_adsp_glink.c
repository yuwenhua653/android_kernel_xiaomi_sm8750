// SPDX-License-Identifier: GPL-2.0-only
/* Annibale MCA transport. Wire layout checked against K90 stock mca_adsp_glink. */
#include <linux/completion.h>
#include <linux/hwid.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/soc/qcom/qti_pmic_glink.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <mca/common/mca_adsp_glink.h>
#include "mca_glink_protocol.h"

#define MCA_TIMEOUT_MS 1000
#define MCA_MAX_EVENTS 64

struct mca_glink;
struct mca_channel {
	struct mca_glink *glink;
	struct pmic_glink_client *client;
	u32 owner;
};
struct mca_glink {
	struct device *dev;
	struct mca_channel channel[2];
	struct completion ack;
	spinlock_t state_lock;
	bool up, ready;
	bool pending_down, pending_up;
	struct work_struct state_work;
	struct mca_pending pending;
	struct workqueue_struct *event_wq;
	atomic_t queued_events;
};
struct mca_listener {
	struct list_head node;
	struct mca_adsp_call_back cb;
	const struct mca_adsp_call_back *key;
	void *data;
};
struct mca_event {
	struct work_struct work;
	struct mca_glink *glink;
	u32 owner, notification;
	int state; /* -1: notification; otherwise PMIC_GLINK_STATE_* */
	u8 data[MCA_GLINK_DATA_MAX];
};

/* Serializes requests with publication/removal; callbacks never take it. */
static DEFINE_MUTEX(mca_device_lock);
static struct mca_glink *active_glink;
/* Preserve sequence across platform unbind/rebind while the module is loaded. */
static u32 request_sequence;
static DECLARE_RWSEM(listener_lock);
static LIST_HEAD(charge_listeners);
static LIST_HEAD(qbg_listeners);

static int mca_register(struct list_head *head,
			struct mca_adsp_call_back *cb, void *data)
{
	struct mca_listener *node, *pos;

	if (!cb || (!cb->glink_down_cb && !cb->sync_data_cb && !cb->notify_cb))
		return -EINVAL;
	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;
	node->cb = *cb;
	node->key = cb;
	node->data = data;
	down_write(&listener_lock);
	list_for_each_entry(pos, head, node) {
		if (pos->key == cb && pos->data == data) {
			up_write(&listener_lock);
			kfree(node);
			return -EEXIST;
		}
	}
	list_add_tail(&node->node, head);
	up_write(&listener_lock);
	return 0;
}

static int mca_unregister(struct list_head *head,
			  struct mca_adsp_call_back *cb, void *data)
{
	struct mca_listener *node;

	down_write(&listener_lock);
	list_for_each_entry(node, head, node) {
		if (node->key == cb && node->data == data) {
			list_del(&node->node);
			up_write(&listener_lock);
			kfree(node);
			return 0;
		}
	}
	up_write(&listener_lock);
	return -ENOENT;
}

int mca_adsp_glink_resister_ops(struct mca_adsp_call_back *cb, void *data)
{
	return mca_register(&charge_listeners, cb, data);
}
EXPORT_SYMBOL(mca_adsp_glink_resister_ops);
int mca_adsp_glink_qbg_resister_ops(struct mca_adsp_call_back *cb, void *data)
{
	return mca_register(&qbg_listeners, cb, data);
}
EXPORT_SYMBOL(mca_adsp_glink_qbg_resister_ops);
int mca_adsp_glink_unregister_ops(struct mca_adsp_call_back *cb, void *data)
{
	return mca_unregister(&charge_listeners, cb, data);
}
EXPORT_SYMBOL_GPL(mca_adsp_glink_unregister_ops);
int mca_adsp_glink_qbg_unregister_ops(struct mca_adsp_call_back *cb, void *data)
{
	return mca_unregister(&qbg_listeners, cb, data);
}
EXPORT_SYMBOL_GPL(mca_adsp_glink_qbg_unregister_ops);

static void mca_dispatch(struct list_head *head, struct mca_event *event)
{
	struct mca_listener *node;

	list_for_each_entry(node, head, node) {
		if (event->state == PMIC_GLINK_STATE_DOWN && node->cb.glink_down_cb)
			node->cb.glink_down_cb(node->data);
		else if (event->state == PMIC_GLINK_STATE_UP && node->cb.sync_data_cb)
			node->cb.sync_data_cb(node->data);
		else if (event->state == -1 && node->cb.notify_cb)
			node->cb.notify_cb(event->notification, event->data,
					   sizeof(event->data), node->data);
	}
}

static void mca_event_work(struct work_struct *work)
{
	struct mca_event *event = container_of(work, struct mca_event, work);

	down_read(&listener_lock);
	if (event->state != -1 || event->owner == MCA_OWNER)
		mca_dispatch(&charge_listeners, event);
	if (event->state != -1 || event->owner == MCA_QBG_OWNER)
		mca_dispatch(&qbg_listeners, event);
	up_read(&listener_lock);
	atomic_dec(&event->glink->queued_events);
	kfree(event);
}

/* Coalesce restarts without allocating: DOWN must precede the latest UP. */
static void mca_state_work(struct work_struct *work)
{
	struct mca_glink *gd = container_of(work, struct mca_glink, state_work);
	struct mca_event event = { 0 };
	unsigned long flags;
	bool down, up;

	spin_lock_irqsave(&gd->state_lock, flags);
	down = gd->pending_down;
	up = gd->pending_up;
	gd->pending_down = false;
	gd->pending_up = false;
	spin_unlock_irqrestore(&gd->state_lock, flags);
	down_read(&listener_lock);
	if (down) {
		event.state = PMIC_GLINK_STATE_DOWN;
		mca_dispatch(&charge_listeners, &event);
		mca_dispatch(&qbg_listeners, &event);
	}
	if (up) {
		event.state = PMIC_GLINK_STATE_UP;
		mca_dispatch(&charge_listeners, &event);
		mca_dispatch(&qbg_listeners, &event);
	}
	up_read(&listener_lock);
}

/* Called under state_lock; independent of the bounded notification queue. */
static void mca_queue_state(struct mca_glink *gd)
{
	if (!gd->ready)
		return;
	if (!gd->up)
		gd->pending_down = true;
	gd->pending_up = gd->up;
	queue_work(gd->event_wq, &gd->state_work);
}

/* Called under state_lock. Bound memory use even if a listener stalls. */
static void mca_queue_event(struct mca_glink *gd, int state, u32 owner,
			    const struct mca_notification *msg)
{
	struct mca_event *event;

	if (!gd->ready)
		return;
	if (atomic_read(&gd->queued_events) >= MCA_MAX_EVENTS)
		goto dropped;
	event = kzalloc(sizeof(*event), GFP_ATOMIC);
	if (!event)
		goto dropped;
	event->glink = gd;
	event->state = state;
	event->owner = owner;
	if (msg) {
		event->notification = le32_to_cpu(msg->notification);
		memcpy(event->data, msg->data, sizeof(event->data));
	}
	INIT_WORK(&event->work, mca_event_work);
	atomic_inc(&gd->queued_events);
	queue_work(gd->event_wq, &event->work);
	return;
dropped:
	dev_warn_ratelimited(gd->dev, "MCA event dropped: queue full or allocation failed\n");
}

static int mca_rx(void *priv, void *buf, size_t len)
{
	struct mca_channel *channel = priv;
	struct mca_glink *gd = channel->glink;
	struct mca_wire_header hdr;
	struct mca_notification msg = { 0 };
	unsigned long flags;

	if (len < sizeof(hdr))
		return -EINVAL;
	memcpy(&hdr, buf, sizeof(hdr));
	if (le32_to_cpu(hdr.owner) != channel->owner)
		return -EINVAL;
	if (le32_to_cpu(hdr.type) == MCA_NOTIFY) {
		if (len < offsetof(struct mca_notification, data) || len > sizeof(msg))
			return -EINVAL;
		memcpy(&msg, buf, len);
		spin_lock_irqsave(&gd->state_lock, flags);
		if (gd->up)
			mca_queue_event(gd, -1, channel->owner, &msg);
		spin_unlock_irqrestore(&gd->state_lock, flags);
	} else {
		spin_lock_irqsave(&gd->state_lock, flags);
		if (mca_accept_response(&gd->pending, buf, len))
			complete(&gd->ack);
		spin_unlock_irqrestore(&gd->state_lock, flags);
	}
	return 0;
}

static void mca_state(void *priv, enum pmic_glink_state state)
{
	struct mca_channel *channel = priv;
	struct mca_glink *gd = channel->glink;
	unsigned long flags;
	bool up = state == PMIC_GLINK_STATE_UP;

	spin_lock_irqsave(&gd->state_lock, flags);
	if (!up && mca_abort_pending(&gd->pending, -ENOTCONN))
		complete(&gd->ack);
	if (gd->up != up) {
		gd->up = up;
		mca_queue_state(gd);
	}
	spin_unlock_irqrestore(&gd->state_lock, flags);
}

static int mca_transfer(u32 owner, u32 opcode, u32 property, void *data, int len)
{
	struct mca_request msg = { 0 };
	struct mca_glink *gd;
	unsigned long flags;
	int ret;

	if (!mca_valid_length(data, len))
		return -EINVAL;
	mutex_lock(&mca_device_lock);
	gd = active_glink;
	if (!gd) {
		ret = -ENODEV;
		goto unlock;
	}
	spin_lock_irqsave(&gd->state_lock, flags);
	if (!gd->up) {
		spin_unlock_irqrestore(&gd->state_lock, flags);
		ret = -ENOTCONN;
		goto unlock;
	}
	reinit_completion(&gd->ack);
	gd->pending.owner = owner;
	gd->pending.opcode = opcode;
	gd->pending.property = property;
	gd->pending.sequence = ++request_sequence;
	gd->pending.result = -ETIMEDOUT;
	gd->pending.active = true;
	msg.hdr.owner = cpu_to_le32(owner);
	msg.hdr.type = cpu_to_le32(MCA_RESPONSE);
	msg.hdr.opcode = cpu_to_le32(opcode);
	msg.property = cpu_to_le32(property);
	msg.sequence = cpu_to_le32(gd->pending.sequence);
	spin_unlock_irqrestore(&gd->state_lock, flags);
	if (opcode == MCA_WRITE)
		memcpy(msg.data, data, len);

	/* Stock uses the charging client for both wire owners. */
	ret = pmic_glink_write(gd->channel[0].client, &msg, sizeof(msg));
	if (!ret)
		wait_for_completion_timeout(&gd->ack, msecs_to_jiffies(MCA_TIMEOUT_MS));
	spin_lock_irqsave(&gd->state_lock, flags);
	if (ret)
		gd->pending.result = ret;
	gd->pending.active = false;
	ret = gd->pending.result;
	if (!ret && opcode == MCA_READ)
		memcpy(data, gd->pending.data, len);
	spin_unlock_irqrestore(&gd->state_lock, flags);
unlock:
	mutex_unlock(&mca_device_lock);
	return ret;
}

int mca_adsp_glink_read_prop(u32 property_id, void *data, int len)
{
	return mca_transfer(MCA_OWNER, MCA_READ, property_id, data, len);
}
EXPORT_SYMBOL(mca_adsp_glink_read_prop);
int mca_adsp_glink_write_prop(u32 property_id, void *data, int len)
{
	return mca_transfer(MCA_OWNER, MCA_WRITE, property_id, data, len);
}
EXPORT_SYMBOL(mca_adsp_glink_write_prop);
int mca_adsp_glink_qbg_read_prop(u32 property_id, void *data, int len)
{
	return mca_transfer(MCA_QBG_OWNER, MCA_READ, property_id, data, len);
}
EXPORT_SYMBOL(mca_adsp_glink_qbg_read_prop);
int mca_adsp_glink_qbg_write_prop(u32 property_id, void *data, int len)
{
	return mca_transfer(MCA_QBG_OWNER, MCA_WRITE, property_id, data, len);
}
EXPORT_SYMBOL(mca_adsp_glink_qbg_write_prop);

static int mca_probe(struct platform_device *pdev)
{
	struct pmic_glink_client_data client = {
		.msg_cb = mca_rx, .state_cb = mca_state,
	};
	struct mca_glink *gd;
	unsigned long flags;
	int i, ret;

	static_assert(sizeof(struct mca_request) == 276);
	static_assert(sizeof(struct mca_response) == 280);
	static_assert(sizeof(struct mca_notification) == 272);
	if (!xiaomi_hwid_is_ready() || strcmp(product_name_get(), "annibale"))
		return -ENODEV;
	gd = devm_kzalloc(&pdev->dev, sizeof(*gd), GFP_KERNEL);
	if (!gd)
		return -ENOMEM;
	gd->dev = &pdev->dev;
	spin_lock_init(&gd->state_lock);
	init_completion(&gd->ack);
	INIT_WORK(&gd->state_work, mca_state_work);
	/* Registration requires the provider to be UP; it does not send initial UP. */
	gd->up = true;
	gd->event_wq = alloc_ordered_workqueue("mca_glink_events", WQ_MEM_RECLAIM);
	if (!gd->event_wq)
		return -ENOMEM;
	mutex_lock(&mca_device_lock);
	if (active_glink) {
		ret = -EBUSY;
		goto unlock;
	}
	for (i = 0; i < ARRAY_SIZE(gd->channel); i++) {
		gd->channel[i].glink = gd;
		gd->channel[i].owner = i ? MCA_QBG_OWNER : MCA_OWNER;
		client.id = gd->channel[i].owner;
		client.name = i ? "mca_qbg" : "mca_charger";
		client.priv = &gd->channel[i];
		gd->channel[i].client = pmic_glink_register_client(&pdev->dev, &client);
		if (IS_ERR(gd->channel[i].client)) {
			ret = PTR_ERR(gd->channel[i].client);
			while (i--)
				pmic_glink_unregister_client(gd->channel[i].client);
			goto unlock;
		}
	}
	platform_set_drvdata(pdev, gd);
	active_glink = gd;
	spin_lock_irqsave(&gd->state_lock, flags);
	gd->ready = true;
	mca_queue_state(gd);
	spin_unlock_irqrestore(&gd->state_lock, flags);
	mutex_unlock(&mca_device_lock);
	return 0;
unlock:
	mutex_unlock(&mca_device_lock);
	destroy_workqueue(gd->event_wq);
	return ret;
}

static int mca_remove(struct platform_device *pdev)
{
	struct mca_glink *gd = platform_get_drvdata(pdev);
	unsigned long flags;
	int i;

	/* Wait for any bounded request, then prevent new requests/events. */
	mutex_lock(&mca_device_lock);
	active_glink = NULL;
	spin_lock_irqsave(&gd->state_lock, flags);
	gd->ready = false;
	spin_unlock_irqrestore(&gd->state_lock, flags);
	mutex_unlock(&mca_device_lock);
	for (i = 0; i < ARRAY_SIZE(gd->channel); i++)
		pmic_glink_unregister_client(gd->channel[i].client);
	/* Never flush while holding mca_device_lock: listeners may call read/write. */
	destroy_workqueue(gd->event_wq);
	return 0;
}

static const struct of_device_id mca_match[] = {
	{ .compatible = "mca,adsp_glink" },
	{ }
};
MODULE_DEVICE_TABLE(of, mca_match);
static struct platform_driver mca_driver = {
	.probe = mca_probe,
	.remove = mca_remove,
	.driver = {
		.name = "mca_adsp_glink",
		.of_match_table = mca_match,
	},
};
module_platform_driver(mca_driver);
MODULE_DESCRIPTION("Experimental Annibale MCA ADSP transport");
MODULE_LICENSE("GPL");
