#include "report/report.h"
#include "storage/storage.h"

#include <errno.h>

void zf_storage_init(void)
{
}

void zf_storage_report(uint8_t sequence, uint8_t *out)
{
	zf_storage_t storage = {.sequence = sequence};

	zf_report_storage(&storage, out);
}

void zf_storage_result_read(uint8_t transaction)
{
	(void)transaction;
}

bool zf_storage_busy(void)
{
	return false;
}

int zf_storage_request(bool usb, uint8_t transaction)
{
	(void)usb;
	(void)transaction;
	return -ENOTSUP;
}
