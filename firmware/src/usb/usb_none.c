#include "usb/usb.h"

#include <errno.h>

int zf_usb_init(void)
{
	return 0;
}

bool zf_usb_configured(void)
{
	return false;
}

void zf_usb_poll(void)
{
}

int zf_usb_refresh_for_storage(void)
{
	return -ENOTSUP;
}
