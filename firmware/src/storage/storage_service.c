#include "storage/storage.h"
#include "platform/board.h"
#include "app/runtime.h"
#include <zephyr/sys/util.h>
#include "protocol/report_codec.h"
#include "storage/storage_export.h"
#include "storage/storage_policy.h"

#include <errno.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(zf_storage, LOG_LEVEL_INF);

#define STORAGE_POLL_MS 50
/* Far below the host's 30 s SCSI command timeout; the worker holds the lock only briefly while the card is exported. */
#define USB_LOCK_WAIT_MS 1000
#define OWNER_HANDOFF_DELAY_MS 1000

K_MUTEX_DEFINE(media_lock);
K_MUTEX_DEFINE(snapshot_lock);
K_SEM_DEFINE(storage_wakeup, 0, 1);
K_SEM_DEFINE(storage_start, 0, 1);

static const struct gpio_dt_spec detect = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), sd_detect_gpios);
static struct gpio_callback detect_callback;
static bool detect_irq;
static zf_storage_t snapshot;
static bool request_pending;
static uint8_t request_transaction;
static int64_t request_ready_at;
static zf_storage_request_t request_action;
static uint32_t media_generation;
static uint32_t request_generation;
static bool last_presence;
static bool media_removed;
static bool storage_thread_started;
static zf_storage_exclusive_job_t exclusive_job;
static void *exclusive_context;

static void detect_changed(const struct device *port, struct gpio_callback *callback, gpio_port_pins_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(callback);
	ARG_UNUSED(pins);
	k_sem_give(&storage_wakeup);
}

/** Latch removal before an exclusive media job can touch replacement media. */
bool zf_storage_media_present(void)
{
	int present = gpio_pin_get_dt(&detect);
	bool removed_now = false;
	bool available;

	k_mutex_lock(&snapshot_lock, K_FOREVER);
	if (present <= 0 && !media_removed) {
		media_removed = true;
		++media_generation;
		last_presence = false;
		removed_now = true;
	}
	available = !media_removed && present > 0;
	k_mutex_unlock(&snapshot_lock);
	if (removed_now) {
		LOG_ERR("Exclusive media job lost card: card detection %d", present);
	}
	return available;
}

void zf_storage_service_publish(const zf_storage_t *storage)
{
#if IS_ENABLED(CONFIG_ZF_USB_MSC)
	bool msc_changed;
#endif

	k_mutex_lock(&snapshot_lock, K_FOREVER);
#if IS_ENABLED(CONFIG_ZF_USB_MSC)
	msc_changed = snapshot.msc_enabled != storage->msc_enabled;
#endif
	snapshot = *storage;
	if (request_pending) {
		snapshot.busy = true;
		snapshot.sample_valid = false;
		snapshot.msc_transition_pending = true;
	}
	k_mutex_unlock(&snapshot_lock);
#if IS_ENABLED(CONFIG_ZF_USB_MSC)
	if (msc_changed) {
		zf_app_wakeup();
	}
#endif
}

void zf_storage_snapshot(zf_storage_t *out)
{
	k_mutex_lock(&snapshot_lock, K_FOREVER);
	*out = snapshot;
	k_mutex_unlock(&snapshot_lock);
}

/** Snapshot-only admission; SD I/O is never performed under the application lock. */
int zf_storage_request(bool usb, uint8_t transaction)
{
	zf_storage_request_t action = usb ? ZF_STORAGE_USB_OWNER : ZF_STORAGE_FIRMWARE_OWNER;
	int err = 0;

	/* Protocol admission also checks this; retain the service guard for direct callers. */
	if (!IS_ENABLED(CONFIG_ZF_USB_MSC)) {
		return -ENOTSUP;
	}
	k_mutex_lock(&snapshot_lock, K_FOREVER);
	if (request_pending || snapshot.busy) {
		err = -EBUSY;
	} else if (usb == snapshot.msc_enabled) {
		/* Repeating a confirmed owner request is a no-op, not a replayed block write. */
		err = 0;
	} else if (usb && (!snapshot.sample_valid || !snapshot.initialized || !snapshot.block_count)) {
		err = -ENOMEDIUM;
	} else {
		request_generation = media_generation;
		request_transaction = transaction;
		request_ready_at = k_uptime_get() + OWNER_HANDOFF_DELAY_MS;
		request_pending = true;
		snapshot.busy = true;
		snapshot.sample_valid = false;
		snapshot.msc_transition_pending = true;
		request_action = action;
		k_sem_give(&storage_wakeup);
	}
	k_mutex_unlock(&snapshot_lock);
	return err;
}

void zf_storage_result_read(uint8_t transaction)
{
	k_mutex_lock(&snapshot_lock, K_FOREVER);
	if (request_pending && request_transaction == transaction) {
		int64_t ready_at = k_uptime_get() + 50;

		if (ready_at < request_ready_at) {
			request_ready_at = ready_at;
			k_sem_give(&storage_wakeup);
		}
	}
	k_mutex_unlock(&snapshot_lock);
}

bool zf_storage_busy(void)
{
	bool busy;

	k_mutex_lock(&snapshot_lock, K_FOREVER);
	busy = snapshot.busy || snapshot.msc_enabled;
	k_mutex_unlock(&snapshot_lock);
	return busy;
}

void zf_storage_report(uint8_t sequence, uint8_t *out)
{
	zf_storage_t storage;

	zf_storage_snapshot(&storage);
	storage.sequence = sequence;
	zf_report_encode_storage(&storage, out);
}

int zf_storage_run_exclusive(zf_storage_exclusive_job_t job, void *context)
{
	int err = 0;

	if (job == NULL) {
		return -EINVAL;
	}
	k_mutex_lock(&snapshot_lock, K_FOREVER);
	if (request_pending || snapshot.busy) {
		err = -EBUSY;
	} else if (snapshot.msc_enabled) {
		err = -EACCES;
	} else if (!snapshot.sample_valid || !snapshot.initialized) {
		err = -ENOMEDIUM;
	} else if (snapshot.filesystem_format == 0) {
		err = -ENODEV;
	} else if (exclusive_job != NULL) {
		err = -EBUSY;
	} else {
		exclusive_job = job;
		exclusive_context = context;
		snapshot.busy = true;
		snapshot.sample_valid = false;
		media_removed = false;
		k_sem_give(&storage_wakeup);
	}
	k_mutex_unlock(&snapshot_lock);
	return err;
}

#if IS_ENABLED(CONFIG_ZF_USB_MSC)
bool zf_storage_export_ready(uint32_t *blocks)
{
	bool ready;
	int present = gpio_pin_get_dt(&detect);

	if (present < 0) {
		LOG_ERR("SD card-detect read failed: %d", present);
		return false;
	}
	k_mutex_lock(&snapshot_lock, K_FOREVER);
	ready = present && snapshot.msc_enabled && !snapshot.busy && snapshot.block_count != 0;
	if (blocks != NULL) {
		*blocks = snapshot.block_count;
	}
	k_mutex_unlock(&snapshot_lock);
	return ready;
}

bool zf_storage_export_lock(uint32_t *blocks)
{
	if (k_mutex_lock(&media_lock, K_MSEC(USB_LOCK_WAIT_MS)) != 0) {
		return false;
	}
	if (zf_storage_export_ready(blocks)) {
		return true;
	}
	k_mutex_unlock(&media_lock);
	return false;
}

void zf_storage_export_unlock(void)
{
	k_mutex_unlock(&media_lock);
}
#endif

static void storage_worker(void *a, void *b, void *c)
{
	int last_sample = last_presence ? 1 : 0;
	int64_t debounce_until = 0;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	(void)k_sem_take(&storage_start, K_FOREVER);
	while (true) {
		int present = gpio_pin_get_dt(&detect);
		zf_storage_request_t action = ZF_STORAGE_NO_REQUEST;
		zf_storage_exclusive_job_t job = NULL;
		void *job_context = NULL;
		int64_t now = k_uptime_get();

		if (present < 0) {
			LOG_ERR("SD card-detect read failed: %d", present);
		} else {
			present = present != 0;
			if (last_sample >= 0 && present != last_sample) {
				debounce_until = now + ZF_STORAGE_DEBOUNCE_MS;
			}
			last_sample = present;
		}
		k_mutex_lock(&snapshot_lock, K_FOREVER);
		if (present < 0 || (present != 0) != last_presence) {
			last_presence = present > 0;
			++media_generation;
		}
		if (request_pending && now >= request_ready_at) {
			action = request_action;
			if (request_generation != media_generation) {
				action = ZF_STORAGE_CANCEL_REQUEST;
				LOG_WRN("SD ownership request cancelled: card changed");
			}
			request_pending = false;
		}
		if (!request_pending && exclusive_job != NULL) {
			job = exclusive_job;
			job_context = exclusive_context;
			exclusive_job = NULL;
			exclusive_context = NULL;
		}
		k_mutex_unlock(&snapshot_lock);
		k_mutex_lock(&media_lock, K_FOREVER);
		if (job != NULL) {
			job(job_context);
			k_mutex_lock(&snapshot_lock, K_FOREVER);
			snapshot.busy = false;
			snapshot.sample_valid = false;
			k_mutex_unlock(&snapshot_lock);
			present = gpio_pin_get_dt(&detect);
			now = k_uptime_get();
			action = ZF_STORAGE_NO_REQUEST;
		}
		zf_storage_policy_poll(present, now, action);
		k_mutex_unlock(&media_lock);
		int64_t wait_now = k_uptime_get();
		int64_t wake_at = INT64_MAX;

		if (!detect_irq) {
			wake_at = wait_now + STORAGE_POLL_MS;
		} else if (present < 0) {
			wake_at = wait_now + ZF_STORAGE_RETRY_MS;
		} else if (debounce_until > wait_now) {
			wake_at = debounce_until;
		}
		k_mutex_lock(&snapshot_lock, K_FOREVER);
		if (request_pending && request_ready_at < wake_at) {
			wake_at = request_ready_at;
		}
		if (present > 0 && !snapshot.msc_enabled &&
		    (!snapshot.initialized || snapshot.filesystem_format == 0) &&
		    wait_now + ZF_STORAGE_RETRY_MS < wake_at) {
			wake_at = wait_now + ZF_STORAGE_RETRY_MS;
		}
		k_mutex_unlock(&snapshot_lock);
		k_timeout_t timeout = wake_at == INT64_MAX ? K_FOREVER : K_MSEC(MAX(0, wake_at - wait_now));

		(void)k_sem_take(&storage_wakeup, timeout);
	}
}

K_THREAD_STACK_DEFINE(storage_stack, 4096);
static struct k_thread storage_thread;

void zf_storage_init(void)
{
	int64_t started = k_uptime_get();
	int err = gpio_is_ready_dt(&detect) ? gpio_pin_configure_dt(&detect, GPIO_INPUT) : -ENODEV;
	int present = err == 0 ? gpio_pin_get_dt(&detect) : err;

	if (err == 0) {
		err = present < 0 ? present : 0;
		if (err == 0) {
			gpio_init_callback(&detect_callback, detect_changed, BIT(detect.pin));
			err = gpio_add_callback(detect.port, &detect_callback);
			if (err == 0) {
				err = gpio_pin_interrupt_configure_dt(&detect, GPIO_INT_EDGE_BOTH);
			}
			if (err == 0) {
				detect_irq = true;
			} else {
				LOG_WRN("SD card-detect interrupt unavailable; polling every %d ms", STORAGE_POLL_MS);
				detect_irq = false;
				err = 0;
			}
		}
	}
	if (err != 0) {
		LOG_ERR("SD service setup failed: %d after %lld ms", err,
			(long long)(k_uptime_get() - started));
		zf_storage_policy_init(false, false, k_uptime_get());
		return;
	}
	last_presence = present != 0;
	zf_storage_policy_init(true, last_presence, k_uptime_get());
	k_thread_create(&storage_thread, storage_stack, K_THREAD_STACK_SIZEOF(storage_stack),
			storage_worker, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
	storage_thread_started = true;
	k_thread_name_set(&storage_thread, "sd-storage");
	LOG_INF("SD service setup completed in %lld ms",
		(long long)(k_uptime_get() - started));
}

void zf_storage_start(void)
{
	if (storage_thread_started) {
		k_sem_give(&storage_start);
	}
}
