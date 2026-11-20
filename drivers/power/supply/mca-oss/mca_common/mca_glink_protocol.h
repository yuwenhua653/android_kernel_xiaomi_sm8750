/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MCA_GLINK_PROTOCOL_H
#define _MCA_GLINK_PROTOCOL_H

#define MCA_OWNER 0x800a
#define MCA_QBG_OWNER 0x8009
#define MCA_RESPONSE 1
#define MCA_NOTIFY 2
#define MCA_READ 1
#define MCA_WRITE 2

struct mca_wire_header {
	__le32 owner, type, opcode;
};
struct mca_request {
	struct mca_wire_header hdr;
	__le32 property, sequence;
	u8 data[MCA_GLINK_DATA_MAX];
};
struct mca_response {
	struct mca_wire_header hdr;
	__le32 property, result, sequence;
	u8 data[MCA_GLINK_DATA_MAX];
};
struct mca_notification {
	struct mca_wire_header hdr;
	__le32 notification;
	u8 data[MCA_GLINK_DATA_MAX];
};
struct mca_pending {
	bool active;
	u32 owner, opcode, property, sequence;
	int result;
	u8 data[MCA_GLINK_DATA_MAX];
};

static bool mca_valid_length(const void *data, int len)
{
	return data && len >= 0 && len < MCA_GLINK_DATA_MAX;
}

/* Called under state_lock. Unmatched or truncated headers never acknowledge. */
static bool mca_accept_response(struct mca_pending *p, const void *buf, size_t len)
{
	struct mca_response msg = { 0 };

	if (!p->active || len < offsetof(struct mca_response, data))
		return false;
	/* memcpy also permits unaligned transport buffers. */
	memcpy(&msg, buf, min(len, sizeof(msg)));
	if (le32_to_cpu(msg.hdr.type) != MCA_RESPONSE ||
	    le32_to_cpu(msg.hdr.owner) != p->owner ||
	    le32_to_cpu(msg.hdr.opcode) != p->opcode ||
	    le32_to_cpu(msg.property) != p->property ||
	    le32_to_cpu(msg.sequence) != p->sequence)
		return false;

	p->active = false;
	/* Short error replies with a complete identity are accepted, never success. */
	p->result = (s32)le32_to_cpu(msg.result);
	if (len > sizeof(msg) || (!p->result && len != sizeof(msg)))
		p->result = -EPROTO;
	if (!p->result && p->opcode == MCA_READ)
		memcpy(p->data, msg.data, sizeof(p->data));
	return true;
}

static bool mca_abort_pending(struct mca_pending *p, int error)
{
	if (!p->active)
		return false;
	p->active = false;
	p->result = error;
	return true;
}
#endif
