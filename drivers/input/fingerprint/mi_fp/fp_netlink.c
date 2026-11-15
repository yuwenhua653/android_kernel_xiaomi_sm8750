// SPDX-License-Identifier: GPL-2.0-only
/* Xiaomi fingerprint netlink ABI; validate registration and zero event payloads. */
#include "fp_driver.h"

#define MAX_NL_MSG_LEN 16
static DEFINE_SPINLOCK(nl_lock);
static DEFINE_MUTEX(nl_lifecycle_lock);
static struct sock *nl_sk;
static u32 peer_portid;

void fp_netlink_send(struct fp_device *fp_dev, const int command)
{
	struct sock *sk;
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	unsigned long flags;
	u32 peer;

	spin_lock_irqsave(&nl_lock, flags);
	sk = nl_sk;
	peer = peer_portid;
	if (sk)
		sock_hold(sk);
	spin_unlock_irqrestore(&nl_lock, flags);
	if (!sk)
		return;
	if (!peer)
		goto put;
	skb = nlmsg_new(MAX_NL_MSG_LEN, GFP_ATOMIC);
	if (!skb)
		goto put;
	nlh = nlmsg_put(skb, 0, 0, 0, MAX_NL_MSG_LEN, 0);
	if (!nlh) {
		kfree_skb(skb);
		goto put;
	}
	memset(nlmsg_data(nlh), 0, MAX_NL_MSG_LEN);
	*(u8 *)nlmsg_data(nlh) = command;
	netlink_unicast(sk, skb, peer, MSG_DONTWAIT);
put:
	sock_put(sk);
}

static void fp_netlink_recv(struct sk_buff *skb)
{
	unsigned long flags;

	if (skb->len < NLMSG_HDRLEN || !nlmsg_ok(nlmsg_hdr(skb), skb->len))
		return;
	spin_lock_irqsave(&nl_lock, flags);
	/* Use the authenticated sender port, not the userspace message header. */
	peer_portid = NETLINK_CB(skb).portid;
	spin_unlock_irqrestore(&nl_lock, flags);
}

int fp_netlink_init(struct fp_device *fp_dev)
{
	struct netlink_kernel_cfg cfg = { .input = fp_netlink_recv };
	struct sock *sk;
	unsigned long flags;
	int ret = 0;

	mutex_lock(&nl_lifecycle_lock);
	if (nl_sk)
		goto unlock;
	sk = netlink_kernel_create(&init_net, fp_dev->fp_netlink_num, &cfg);
	if (!sk) {
		ret = -ENOMEM;
		goto unlock;
	}
	spin_lock_irqsave(&nl_lock, flags);
	peer_portid = 0;
	nl_sk = sk;
	spin_unlock_irqrestore(&nl_lock, flags);
unlock:
	mutex_unlock(&nl_lifecycle_lock);
	return ret;
}

int fp_netlink_destroy(struct fp_device *fp_dev)
{
	struct sock *sk;
	unsigned long flags;

	mutex_lock(&nl_lifecycle_lock);
	spin_lock_irqsave(&nl_lock, flags);
	sk = nl_sk;
	nl_sk = NULL;
	peer_portid = 0;
	spin_unlock_irqrestore(&nl_lock, flags);
	if (sk)
		netlink_kernel_release(sk);
	mutex_unlock(&nl_lifecycle_lock);
	return 0;
}
