/* SPDX-License-Identifier: GPL-2.0-only */
/* PRODUCTION */
static pthread_mutex_t callback_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t callback_cond = PTHREAD_COND_INITIALIZER;
static bool entered, release_callback;
static atomic_bool unregister_started, unregister_done;
static int calls;
static int fake_voltage(void *data, int *value)
{
	assert(*(int *)data == 123);
	calls++;
	*value = 4321;
	return 0;
}
static int blocked_voltage(void *data, int *value)
{
	pthread_mutex_lock(&callback_lock);
	entered = true;
	pthread_cond_broadcast(&callback_cond);
	while (!release_callback)
		pthread_cond_wait(&callback_cond, &callback_lock);
	pthread_mutex_unlock(&callback_lock);
	return fake_voltage(data, value);
}
static struct fuelguage_ic_ops operations = { .fg_ic_get_volt = fake_voltage };
static int provider_data = 123;
static void *reader(void *unused)
{
	int value = -9;
	assert(!platform_fg_ops_get_volt(FG_IC_MASTER, &value));
	assert(value == 4321);
	return NULL;
}
static void *unregisterer(void *unused)
{
	atomic_store(&unregister_started, true);
	assert(!platform_fg_ic_ops_unregister(FG_IC_MASTER, &provider_data, &operations));
	atomic_store(&unregister_done, true);
	return NULL;
}
int main(void)
{
	int value = 987, bad_data = 456;
	pthread_t rd, unreg;
	struct timespec pause = { .tv_nsec = 1000000 };

	assert(platform_fg_ops_get_volt(FG_IC_MASTER, &value) == -EOPNOTSUPP && value == 987);
	assert(platform_fg_ops_get_volt((enum fg_ic_role)-1, &value) == -EOPNOTSUPP);
	assert(platform_fg_ic_ops_register((enum fg_ic_role)-1, &provider_data, &operations) == -EINVAL);
	assert(platform_fg_ic_ops_register(FG_IC_MAX, &provider_data, &operations) == -EINVAL);
	assert(platform_fg_ic_ops_register(FG_IC_MASTER, NULL, &operations) == -EINVAL);
	assert(!platform_fg_ic_ops_register(FG_IC_MASTER, &provider_data, &operations));
	assert(platform_fg_ic_ops_register(FG_IC_MASTER, &provider_data, &operations) == -EBUSY);
	assert(platform_fg_ops_get_volt(FG_IC_MASTER, NULL) == -EINVAL);
	assert(!platform_fg_ops_get_volt(FG_IC_MASTER, &value) && value == 4321);
	assert(platform_fg_ops_get_temp(FG_IC_MASTER, &value) == -EOPNOTSUPP);
	platform_fg_ops_get_ui_soh(FG_IC_MASTER, &value);
	assert(value == 4321); /* legacy void getter must not invent a result */
	assert(platform_fg_ic_ops_unregister(FG_IC_MASTER, &bad_data, &operations) == -ENOENT);
	assert(!platform_fg_ic_ops_unregister(FG_IC_MASTER, &provider_data, &operations));
	assert(platform_fg_ops_get_volt(FG_IC_MASTER, &value) == -EOPNOTSUPP);

	operations.fg_ic_get_volt = blocked_voltage;
	assert(!platform_fg_ic_ops_register(FG_IC_MASTER, &provider_data, &operations));
	assert(!pthread_create(&rd, NULL, reader, NULL));
	pthread_mutex_lock(&callback_lock);
	while (!entered) pthread_cond_wait(&callback_cond, &callback_lock);
	pthread_mutex_unlock(&callback_lock);
	assert(pthread_rwlock_trywrlock(&fg_ops_lock) == EBUSY);
	assert(!pthread_create(&unreg, NULL, unregisterer, NULL));
	for (int i = 0; !atomic_load(&unregister_started) && i < 1000; i++) nanosleep(&pause, NULL);
	assert(atomic_load(&unregister_started));
	pause.tv_nsec = 20000000;
	nanosleep(&pause, NULL);
	assert(!atomic_load(&unregister_done));
	pthread_mutex_lock(&callback_lock);
	release_callback = true;
	pthread_cond_broadcast(&callback_cond);
	pthread_mutex_unlock(&callback_lock);
	assert(!pthread_join(rd, NULL) && !pthread_join(unreg, NULL));
	assert(atomic_load(&unregister_done));
	assert(platform_fg_ops_get_volt(FG_IC_MASTER, &value) == -EOPNOTSUPP);
	assert(calls == 2);
	puts("PASS: gauge missing/invalid providers, duplicate registration, NULL output and unregister waits for active callback");
	return 0;
}
