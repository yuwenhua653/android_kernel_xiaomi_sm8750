/* SPDX-License-Identifier: GPL-2.0-only */
/* Deterministic worker scheduling; not an SMP stress test. */
#define PMIC_GLINK_STATE_DOWN 0
#define PMIC_GLINK_STATE_UP 1
enum pmic_glink_state { DOWN = 0, UP = 1 };
struct work_struct { bool queued; };
struct mca_glink {
	bool up, ready, pending_down, pending_up, pending;
	int state_lock, ack, error;
	struct work_struct state_work;
	void *event_wq;
};
struct mca_channel { struct mca_glink *glink; };
struct mca_event { int state; };
static int listener_lock, charge_listeners, qbg_listeners;
static int calls[16], used;
#define container_of(ptr,type,member) ((type *)((char *)(ptr) - offsetof(type,member)))
#define spin_lock_irqsave(lock, flags) do { (flags) = 0; } while (0)
#define spin_unlock_irqrestore(lock, flags) ((void)(flags))
static void down_read(int *lock) {}
static void up_read(int *lock) {}
static void complete(int *ack) { (*ack)++; }
static bool mca_abort_pending(bool *pending, int error)
{ bool old = *pending; *pending = false; return old; }
static void queue_work(void *wq, struct work_struct *work) { work->queued = true; }
static void mca_dispatch(int *head, struct mca_event *event)
{ assert(used < 16); calls[used++] = event->state; }
/* PRODUCTION */
static void run_work(struct mca_glink *gd)
{ assert(gd->state_work.queued); gd->state_work.queued = false; mca_state_work(&gd->state_work); }
int main(void)
{
	struct mca_glink gd = { .up = true };
	struct mca_channel ch = { &gd };
	mca_queue_state(&gd);
	assert(!gd.state_work.queued);
	gd.ready = true;
	gd.pending = true;
	mca_state(&ch, DOWN);
	assert(gd.ack == 1 && !gd.pending && !gd.up);
	mca_state(&ch, DOWN); /* two owners report the same state */
	assert(gd.ack == 1);
	mca_state(&ch, UP);
	assert(gd.pending_down && gd.pending_up);
	run_work(&gd);
	assert(used == 4 && calls[0] == DOWN && calls[1] == DOWN);
	assert(calls[2] == UP && calls[3] == UP);
	assert(!gd.pending_down && !gd.pending_up);
	mca_state(&ch, DOWN);
	mca_state(&ch, UP);
	mca_state(&ch, DOWN);
	run_work(&gd);
	assert(used == 6 && calls[4] == DOWN && calls[5] == DOWN);
	gd.ready = false; /* teardown stops new notification production */
	mca_state(&ch, UP);
	assert(!gd.state_work.queued);
	puts("PASS: MCA coalesced DOWN/UP ordering, duplicate state, pending wake and teardown gate");
	return 0;
}
