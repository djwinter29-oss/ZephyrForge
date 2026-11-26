#include "benchmark/benchmark.h"
#include "report/report.h"

#include <errno.h>

int zf_benchmark_request(bool start, uint32_t size_mib)
{
	(void)start;
	(void)size_mib;
	return -ENOTSUP;
}

void zf_benchmark_report(uint8_t sequence, uint8_t *out)
{
	zf_benchmark_t status = {.sequence = sequence, .state = ZF_BENCHMARK_IDLE};

	zf_report_benchmark(&status, out);
}
