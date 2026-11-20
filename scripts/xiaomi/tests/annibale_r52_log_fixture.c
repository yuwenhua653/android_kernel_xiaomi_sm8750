/* SPDX-License-Identifier: GPL-2.0-only */
static struct mca_charge_log_ops_data g_mca_charge_log_data[MCA_CHARGE_LOG_ID_MAX];
static DECLARE_RWSEM(g_mca_charge_log_sem);
static test_event entered, release_callback, unregister_started, unregister_finished;
static int callback_count;
static int provider, intruder;
/* PRODUCTION */
static int context_only(void *data, char *buf, int size) {
	assert(data == &provider); callback_count++;
	return snprintf(buf,(size_t)size,"context");
}
static int head_only(void *data, char *buf, int size) {
	assert(data == &provider); callback_count++;
	return snprintf(buf,(size_t)size,"head");
}
static int negative(void *data, char *buf, int size) { (void)data; (void)buf; (void)size; return -EIO; }
static int oversized(void *data, char *buf, int size) {
	(void)data; assert(size > 0); memset(buf,'X',(size_t)size-1); buf[size-1] = 0; return INT_MAX;
}
static int blocked(void *data, char *buf, int size) {
	assert(data == &provider); event_set(&entered); event_wait(&release_callback);
	return snprintf(buf,(size_t)size,"alive");
}
static void reader_thread(void *arg) {
	char buf[PAGE_SIZE]; (void)arg;
	assert(mca_log_charge_log_dump_log_context(buf) == 6); assert(!strcmp(buf,"alive\n"));
}
static void remover_thread(void *arg) {
	(void)arg; event_set(&unregister_started);
	assert(!mca_log_charge_log_unregister(MCA_CHARGE_LOG_ID_FG_MASTER_IC,&provider));
	event_set(&unregister_finished);
}
int main(void) {
	struct { unsigned char before[16]; char buf[PAGE_SIZE]; unsigned char after[16]; } output;
	struct mca_log_charge_log_ops context = {NULL,context_only}, head = {head_only,NULL};
	struct mca_log_charge_log_ops fail = {negative,negative}, big = {oversized,oversized};
	struct mca_log_charge_log_ops wait = {NULL,blocked};
	test_thread reader, remover;
	int n;
	memset(&output,0xA5,sizeof(output));
	assert(mca_log_charge_log_dump(NULL,true) == -EINVAL);
	mca_log_charge_log_register((enum mca_charge_log_id_ele)-1,&context,&provider);
	mca_log_charge_log_register(MCA_CHARGE_LOG_ID_MAX,&context,&provider);
	mca_log_charge_log_register(MCA_CHARGE_LOG_ID_FG_MASTER_IC,NULL,&provider);
	mca_log_charge_log_register(MCA_CHARGE_LOG_ID_FG_MASTER_IC,&context,NULL);
	assert(mca_log_charge_log_dump_log_context(output.buf) == 1);
	mca_log_charge_log_register(MCA_CHARGE_LOG_ID_FG_MASTER_IC,&context,&provider);
	assert(mca_log_charge_log_dump_log_context(output.buf) == 8);
	assert(!strcmp(output.buf,"context\n") && callback_count == 1);
	assert(mca_log_charge_log_dump_log_head(output.buf) == 1);
	/* A duplicate owner must not replace a live provider. */
	mca_log_charge_log_register(MCA_CHARGE_LOG_ID_FG_MASTER_IC,&head,&intruder);
	assert(mca_log_charge_log_unregister(MCA_CHARGE_LOG_ID_FG_MASTER_IC,&intruder) == -ENOENT);
	assert(mca_log_charge_log_dump_log_context(output.buf) == 8);
	assert(!mca_log_charge_log_unregister(MCA_CHARGE_LOG_ID_FG_MASTER_IC,&provider));
	mca_log_charge_log_register(MCA_CHARGE_LOG_ID_FG_MASTER_IC,&head,&provider);
	assert(mca_log_charge_log_dump_log_context(output.buf) == 1);
	assert(mca_log_charge_log_dump_log_head(output.buf) == 5);
	assert(!strcmp(output.buf,"head\n"));
	assert(!mca_log_charge_log_unregister(MCA_CHARGE_LOG_ID_FG_MASTER_IC,&provider));
	mca_log_charge_log_register(MCA_CHARGE_LOG_ID_BUSINESS_CHG,&fail,&provider);
	mca_log_charge_log_register(MCA_CHARGE_LOG_ID_THERMAL,&big,&provider);
	assert(mca_log_charge_log_dump_log_context(output.buf) == PAGE_SIZE-1);
	assert(output.buf[PAGE_SIZE-2] == '\n' && !output.buf[PAGE_SIZE-1]);
	for(n=0;n<16;n++) assert(output.before[n]==0xA5 && output.after[n]==0xA5);
	assert(!mca_log_charge_log_unregister(MCA_CHARGE_LOG_ID_BUSINESS_CHG,&provider));
	assert(!mca_log_charge_log_unregister(MCA_CHARGE_LOG_ID_THERMAL,&provider));
	assert(mca_log_charge_log_unregister((enum mca_charge_log_id_ele)-1,&provider)==-EINVAL);
	for(n=0;n<32;n++) {
		event_init(&entered); event_init(&release_callback);
		event_init(&unregister_started); event_init(&unregister_finished);
		mca_log_charge_log_register(MCA_CHARGE_LOG_ID_FG_MASTER_IC,&wait,&provider);
		thread_create(&reader,reader_thread,NULL); event_wait(&entered);
		thread_create(&remover,remover_thread,NULL); event_wait(&unregister_started);
		assert(!event_ready(&unregister_finished));
		event_set(&release_callback); thread_join(reader); thread_join(remover);
		assert(event_ready(&unregister_finished));
		assert(mca_log_charge_log_dump_log_context(output.buf) == 1);
		assert(mca_log_charge_log_unregister(MCA_CHARGE_LOG_ID_FG_MASTER_IC,&provider)==-ENOENT);
		event_destroy(&entered); event_destroy(&release_callback);
		event_destroy(&unregister_started); event_destroy(&unregister_finished);
	}
	puts("PASS: missing/negative IDs, absent callbacks, duplicate owners, negative/oversized results, buffer guards and 32 concurrent unregister/drain cycles");
	return 0;
}
