/* SPDX-License-Identifier: GPL-2.0-only */
/* Serialized request/response injection; does not simulate kernel locking. */
#define MCA_TIMEOUT_MS 1000
struct mca_glink {
	bool up;
	struct mca_pending pending;
	struct { void *client; } channel[2];
	int ack, state_lock;
};
static struct mca_glink *active_glink;
static u32 request_sequence;
static int mca_device_lock, locked, sends, scenario;
static void mutex_lock(int *lock) { assert(!locked); locked = 1; }
static void mutex_unlock(int *lock) { assert(locked); locked = 0; }
#define spin_lock_irqsave(lock, flags) do { (flags) = 0; } while (0)
#define spin_unlock_irqrestore(lock, flags) ((void)(flags))
static void reinit_completion(int *ack) { *ack = 0; }
#define msecs_to_jiffies(x) (x)
static struct mca_request last_request;
static int pmic_glink_write(void *client, void *buf, size_t len)
{
	assert(locked && len == 276);
	memcpy(&last_request, buf, len);
	sends++;
	return scenario == 1 ? -EIO : 0;
}
static unsigned long wait_for_completion_timeout(int *ack, unsigned long timeout)
{
	struct mca_pending *p = &active_glink->pending;
	struct mca_response msg = {
		.hdr = last_request.hdr,
		.property = last_request.property,
		.sequence = last_request.sequence,
	};
	assert(timeout == 1000);
	memset(msg.data, 0x42, sizeof(msg.data));
	if (scenario == 2) /* timeout */
		return 0;
	if (scenario == 3) { /* late error from previous request, then timeout */
		msg.sequence--;
		msg.result = 7;
		assert(!mca_accept_response(p, &msg, sizeof(msg)));
		return 0;
	}
	if (scenario == 4) {
		assert(mca_abort_pending(p, -ENOTCONN));
		return 1;
	}
	if (scenario == 5)
		msg.result = 7;
	assert(mca_accept_response(p, &msg, scenario == 6 ? 24 : sizeof(msg)));
	return 1;
}
/* PRODUCTION */
int main(void)
{
	struct mca_glink gd = { .up = true };
	struct mca_pending p;
	struct mca_response msg;
	u8 data[256];
	int expected[] = {0, -EIO, -ETIMEDOUT, -ETIMEDOUT, -ENOTCONN, 7, -EPROTO};

	_Static_assert(sizeof(struct mca_request) == 276, "request layout");
	_Static_assert(sizeof(struct mca_response) == 280, "response layout");
	_Static_assert(offsetof(struct mca_response, data) == 24, "data offset");
	assert(!mca_valid_length(NULL, 0));
	assert(!mca_valid_length(data, -1));
	assert(!mca_valid_length(data, 256));
	assert(mca_valid_length(data, 0) && mca_valid_length(data, 255));
	assert(mca_transfer(MCA_OWNER, MCA_READ, 10, data, 1) == -ENODEV);
	active_glink = &gd;
	gd.up = false;
	assert(mca_transfer(MCA_OWNER, MCA_READ, 10, data, 1) == -ENOTCONN);
	assert(!sends);
	gd.up = true;
	assert(mca_transfer(MCA_OWNER, MCA_READ, 10, data, 256) == -EINVAL);
	for (scenario = 0; scenario < 7; scenario++) {
		memset(data, 0xa5, sizeof(data));
		assert(mca_transfer(MCA_QBG_OWNER, MCA_READ, 0x11004, data, 255) == expected[scenario]);
		assert(last_request.hdr.owner == MCA_QBG_OWNER);
		assert(last_request.property == 0x11004);
		assert(!gd.pending.active);
		for (unsigned int i = 0; i < 255; i++)
			assert(data[i] == (scenario ? 0xa5 : 0x42));
		assert(data[255] == 0xa5);
	}
	scenario = 0;
	assert(!mca_transfer(MCA_OWNER, MCA_WRITE, 12, data, 255));
	assert(!memcmp(last_request.data, data, 255));
	assert(last_request.data[255] == 0);
	/* Response identity must match even when the firmware reports an error. */
	for (int field = 0; field < 5; field++) {
		p = (struct mca_pending){ .active = true, .owner = MCA_OWNER,
			.opcode = MCA_READ, .property = 12, .sequence = 99 };
		msg = (struct mca_response){ .hdr = {MCA_OWNER, MCA_RESPONSE, MCA_READ},
			.property = 12, .sequence = 99, .result = 4 };
		switch (field) {
		case 0: msg.hdr.owner++; break;
		case 1: msg.hdr.type++; break;
		case 2: msg.hdr.opcode++; break;
		case 3: msg.property++; break;
		case 4: msg.sequence++; break;
		}
		assert(!mca_accept_response(&p, &msg, sizeof(msg)) && p.active);
	}
	msg = (struct mca_response){ .hdr = {MCA_OWNER, MCA_RESPONSE, MCA_READ},
		.property = 12, .sequence = 99, .result = 4 };
	for (size_t n = 0; n < 24; n++)
		assert(!mca_accept_response(&p, &msg, n) && p.active);
	assert(mca_accept_response(&p, &msg, 24) && p.result == 4);
	assert(!mca_accept_response(&p, &msg, sizeof(msg))); /* duplicate */
	p.active = true;
	assert(mca_accept_response(&p, &msg, sizeof(msg) + 1) && p.result == -EPROTO);
	p.active = true;
	assert(mca_abort_pending(&p, -ENOTCONN));
	assert(!mca_abort_pending(&p, -ENOTCONN));
	puts("PASS: MCA wire identity, lengths, timeout, stale errors, link loss, unchanged failed reads");
	return 0;
}
