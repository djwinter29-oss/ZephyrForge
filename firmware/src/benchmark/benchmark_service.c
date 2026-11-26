#include "benchmark/benchmark_engine.h"
#include "storage/storage.h"

#include <errno.h>
#include <zephyr/kernel.h>

static zf_benchmark_t benchmark;
static K_MUTEX_DEFINE(benchmark_lock);
static bool active;
static bool cancelled;

static void run(void *context)
{
	uint32_t size_mib;

	ARG_UNUSED(context);

	k_mutex_lock(&benchmark_lock, K_FOREVER);
	size_mib = benchmark.target_bytes / (1024u * 1024u);
	k_mutex_unlock(&benchmark_lock);
	zf_benchmark_media_reset();
	zf_benchmark_engine_run(size_mib);
	k_mutex_lock(&benchmark_lock, K_FOREVER);
	active = false;
	k_mutex_unlock(&benchmark_lock);
}

int zf_benchmark_request(bool start, uint32_t size_mib)
{
	zf_benchmark_t previous;
	int err;

	if (start ? size_mib == 0 || size_mib > ZF_BENCHMARK_MAX_MIB : size_mib != 0) {
		return ZF_BENCHMARK_ERROR_SIZE;
	}
	k_mutex_lock(&benchmark_lock, K_FOREVER);
	if (!start) {
		cancelled = active;
		k_mutex_unlock(&benchmark_lock);
		return ZF_BENCHMARK_ERROR_NONE;
	}
	if (active) {
		k_mutex_unlock(&benchmark_lock);
		return ZF_BENCHMARK_ERROR_BUSY;
	}
	previous = benchmark;
	benchmark = (zf_benchmark_t){
		.state = ZF_BENCHMARK_QUEUED,
		.target_bytes = size_mib * 1024u * 1024u,
	};
	active = true;
	cancelled = false;
	k_mutex_unlock(&benchmark_lock);

	err = zf_storage_run_exclusive(run, NULL);
	if (err != 0) {
		k_mutex_lock(&benchmark_lock, K_FOREVER);
		active = false;
		benchmark = previous;
		k_mutex_unlock(&benchmark_lock);
		if (err == -EACCES) {
			return ZF_BENCHMARK_ERROR_USB;
		}
		if (err == -ENOMEDIUM) {
			return ZF_BENCHMARK_ERROR_CARD;
		}
		if (err == -ENODEV) {
			return ZF_BENCHMARK_ERROR_MOUNT;
		}
		return err == -EBUSY ? ZF_BENCHMARK_ERROR_BUSY : ZF_BENCHMARK_ERROR_IO;
	}
	return ZF_BENCHMARK_ERROR_NONE;
}

void zf_benchmark_report(uint8_t sequence, uint8_t *out)
{
	zf_benchmark_t status;

	k_mutex_lock(&benchmark_lock, K_FOREVER);
	status = benchmark;
	k_mutex_unlock(&benchmark_lock);
	status.sequence = sequence;
	zf_report_encode_benchmark(&status, out);
}

bool zf_benchmark_service_publish(const zf_benchmark_t *status)
{
	zf_storage_t storage;

	zf_storage_snapshot(&storage);
	k_mutex_lock(&benchmark_lock, K_FOREVER);
	benchmark = *status;
	if (storage.filesystem_format != 0 && storage.present) {
		benchmark.flags |= ZF_BENCHMARK_MOUNTED;
	}
	benchmark.filesystem_result = status->error == ZF_BENCHMARK_ERROR_IO ||
					      status->error == ZF_BENCHMARK_ERROR_CLEANUP
				      ? zf_benchmark_media_result() : 0;
	bool proceed = !cancelled;
	k_mutex_unlock(&benchmark_lock);
	return proceed;
}
