#include "app/runtime.h"

#include <zephyr/kernel.h>

K_MUTEX_DEFINE(app_lock);
K_SEM_DEFINE(service_wakeup, 0, 1);

void zf_app_lock(void)
{
	(void)k_mutex_lock(&app_lock, K_FOREVER);
}

void zf_app_unlock(void)
{
	(void)k_mutex_unlock(&app_lock);
}

void zf_app_wakeup(void)
{
	k_sem_give(&service_wakeup);
}

void zf_app_wait(int64_t timeout_ms)
{
	k_timeout_t timeout = timeout_ms == INT64_MAX ? K_FOREVER : K_MSEC(timeout_ms);

	(void)k_sem_take(&service_wakeup, timeout);
}
