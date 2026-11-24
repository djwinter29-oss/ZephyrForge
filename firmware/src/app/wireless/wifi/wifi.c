/*
 * Wi-Fi station: saved-network join with paced retries, candidate tests and scans (Report 12, opcodes 9-12).
 *
 * Uses Zephyr net_mgmt. Driver events are queued by the callback and applied in zf_wifi_poll().
 * AIROC requests run on a dedicated queue because scans and joins block; no request holds the application lock.
 */
#include "app/zephyrforge.h"
#include "app/usb/hid_reports.h"
#include "platform/boot_config.h"
#include "app/control/control.h"
#include "platform/zf_platform.h"
#include "wireless_limits.h"
#include "wifi_driver.h"

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(zf_wifi, LOG_LEVEL_INF);

#define CONNECT_TIMEOUT_MS 30000
#define RETRY_DELAY_MS 5000
#define RETRY_MAX_MS 60000
#define LEAVE_TIMEOUT_MS 5000
#define SCAN_TIMEOUT_MS (IS_ENABLED(CONFIG_WIFI_AIROC) ? 15000 : 10000)
#define SCAN_MAX 18u

#define STATE_OFF 0u
#define STATE_CONNECTING 1u
#define STATE_CONNECTED 2u
#define STATE_FAILED 3u

#define DETAIL_NONE 0u
#define DETAIL_RADIO 1u
#define DETAIL_TIMEOUT 5u
#define DETAIL_LINK_LOST 6u

#define SECURITY_PRIVACY 0x01u
#define SECURITY_WPA 0x02u
#define SECURITY_RSN 0x04u

#define EVENT_MASK                                                                                            \
	(NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT | NET_EVENT_WIFI_SCAN_RESULT |         \
	 NET_EVENT_WIFI_SCAN_DONE)

/** Link-machine state of the managed Wi-Fi interface. */
enum link {
	LINK_DOWN, /**< Associated with nothing. */
	LINK_CONNECTING, /**< A connect request is outstanding. */
	LINK_UP, /**< Joined, address pending or bound. */
	LINK_LEAVING, /**< A disconnect is outstanding. */
};

/** Which profile the link machine is currently serving. */
enum owner {
	OWNER_NONE, /**< Nothing is being served. */
	OWNER_SAVED, /**< The saved network with retry backoff. */
	OWNER_TEST, /**< A staged candidate; the saved network resumes when it ends. */
};

/** Command phase reported through Feature 12 wifi_state. */
enum command {
	COMMAND_IDLE, /**< No command result to report. */
	COMMAND_TESTING, /**< A candidate join is in progress (wifi_state 4). */
	COMMAND_JOINED, /**< The last command joined successfully (wifi_state 5). */
	COMMAND_FAILED, /**< The last candidate test failed; carries the join detail (wifi_state 6). */
};

/** Deferred work item queued for the poll loop. */
enum action {
	ACTION_NONE, /**< Nothing queued. */
	ACTION_DISCONNECT, /**< Leave the current network. */
	ACTION_CONNECT, /**< Join the owner's profile. */
	ACTION_SCAN, /**< Refresh the scan table. */
	ACTION_VERIFY, /**< Confirm the active interface has a usable connection. */
	ACTION_VERIFY_DISCONNECT, /**< Confirm the driver has left the current connection attempt. */
};

/** Event-queue messages consumed by the poll loop. */
enum event {
	EVENT_UP, /**< Link established (post-DHCP for AIROC, driver-confirmed otherwise). */
	EVENT_DOWN_CONNECTED, /**< Link lost after a successful join. */
	EVENT_DOWN_CONNECTING, /**< A connect attempt failed or was dropped. */
	EVENT_DISCONNECT_FAILED, /**< A requested disconnect did not complete. */
	EVENT_SCAN_DONE, /**< The scan finished; publish the collected table. */
	EVENT_LINK_VERIFIED, /**< The interface is associated and has a preferred IPv4 address. */
	EVENT_DISCONNECT_VERIFIED, /**< Interface status after an ambiguous disconnect result. */
};

/** A deferred driver event; status and generation survive the queue boundary. */
struct wifi_event {
	uint32_t generation; /**< Connection generation for verification results. */
	int status; /**< Driver status or verification outcome. */
	uint8_t type; /**< One event value. */
};

/** One collected scan result, deduplicated to the strongest entry per SSID. */
struct scan_entry {
	uint8_t ssid[ZF_SSID_MAX_BYTES]; /**< Raw SSID bytes. */
	uint8_t ssid_length; /**< SSID length, clamped to ZF_SSID_MAX_BYTES. */
	int8_t rssi; /**< Signal strength for weakest-slot eviction and sorting. */
	uint8_t security; /**< Driver security bitmask. */
};

K_MSGQ_DEFINE(events, sizeof(struct wifi_event), 16, 4);
static atomic_t event_overflow_pending = ATOMIC_INIT(0);

static struct net_mgmt_event_callback event_cb;
#ifdef CONFIG_WIFI_AIROC
static struct net_mgmt_event_callback dhcp_cb;
#endif
static struct net_if *iface;

static bool saved_enabled;
static zf_wifi_network_t saved;
static uint8_t wifi_state;
static uint8_t saved_detail;
static int64_t retry_at;
static int64_t retry_delay = RETRY_DELAY_MS;

static enum link link;
static enum owner owner;
static bool leave_requested;
static bool connect_quarantined;
static bool disconnect_verify_pending;
static int64_t connect_deadline;
static int64_t leave_deadline;
static uint32_t connection_generation;
static uint32_t verification_generation;
static uint32_t disconnect_generation;
static bool link_verify_pending;

static enum command command;
static uint8_t join_detail;
static zf_wifi_network_t test;
static bool test_join_pending;
static uint8_t test_generation;

static uint8_t scan_state;
static uint16_t scan_generation;
static bool scan_pending;
static bool scan_driver_active;
static int64_t scan_deadline;
static uint8_t scan_count;
static struct scan_entry scan[SCAN_MAX];

static bool driver_operation_available(void)
{
	return !scan_driver_active && !connect_quarantined;
}

static void schedule_retry(int64_t now)
{
	retry_at = now + retry_delay;
	retry_delay = MIN(retry_delay * 2, RETRY_MAX_MS);
}

/* Drop the current link; its disconnect event is then ignored as an expected leave. */
static void release_link(void)
{
	owner = OWNER_NONE;
	link_verify_pending = false;
	if (link != LINK_DOWN && link != LINK_LEAVING) {
		leave_requested = true;
	}
}

static void wipe(zf_wifi_network_t *network)
{
	memset(network, 0, sizeof(*network));
}

static void fail_test(uint8_t detail, int64_t now)
{
	if (link != LINK_DOWN) {
		connect_quarantined = true;
		if (link != LINK_LEAVING) {
			leave_deadline = 0;
		}
	}
	command = COMMAND_FAILED;
	join_detail = detail;
	test_join_pending = false;
	memset(test.password, 0, sizeof(test.password));
	release_link();
	retry_delay = RETRY_DELAY_MS;
	retry_at = now;
	wifi_state = saved_enabled ? STATE_CONNECTING : STATE_OFF;
	LOG_WRN("Wi-Fi test failed (detail %u)", detail);
}

static void saved_failed(uint8_t detail, int64_t now)
{
	owner = OWNER_NONE;
	wifi_state = STATE_FAILED;
	saved_detail = detail;
	schedule_retry(now);
}

static void link_up(int64_t now)
{
	if (link == LINK_LEAVING) {
		return;
	}
	link = LINK_UP;
	if (owner == OWNER_TEST && command == COMMAND_TESTING) {
		command = COMMAND_JOINED;
		join_detail = DETAIL_NONE;
		zf_control_test_joined(now);
		LOG_INF("Wi-Fi test joined");
	} else if (owner == OWNER_SAVED) {
		wifi_state = STATE_CONNECTED;
		saved_detail = DETAIL_NONE;
		retry_delay = RETRY_DELAY_MS;
		LOG_INF("Wi-Fi connected");
	} else {
		leave_requested = true;
	}
}

static void link_down(int64_t now)
{
	enum link previous = link;

	link = LINK_DOWN;
	if (previous == LINK_LEAVING) {
		connect_quarantined = false;
		disconnect_verify_pending = false;
		leave_requested = false;
		link_verify_pending = false;
	}
	if (owner == OWNER_TEST) {
		owner = OWNER_NONE;
		if (command == COMMAND_JOINED) {
			command = COMMAND_FAILED;
			join_detail = DETAIL_LINK_LOST;
			wifi_state = saved_enabled ? STATE_FAILED : STATE_OFF;
			schedule_retry(now);
			LOG_WRN("Test Wi-Fi link lost");
		} else if (command == COMMAND_TESTING) {
			/* Failed joins without a specific reason share detail 0. */
			fail_test(DETAIL_NONE, now);
		}
	} else if (owner == OWNER_SAVED) {
		saved_failed(DETAIL_NONE, now);
		LOG_WRN("Wi-Fi link down");
	}
}

static void finish_scan(int status)
{
	if (!scan_driver_active) {
		return;
	}
	scan_driver_active = false;
	if (scan_state != ZF_SCAN_RUNNING) {
		return;
	}
	if (status != 0) {
		scan_count = 0;
		scan_state = ZF_SCAN_FAILED;
		LOG_WRN("Wi-Fi scan failed: %d", status);
		return;
	}
	for (uint8_t i = 1; i < scan_count; ++i) {
		struct scan_entry entry = scan[i];
		uint8_t j = i;

		for (; j > 0 && scan[j - 1].rssi < entry.rssi; --j) {
			scan[j] = scan[j - 1];
		}
		scan[j] = entry;
	}
	scan_state = ZF_SCAN_COMPLETE;
}

static uint8_t security_bits(enum wifi_security_type security)
{
	switch (security) {
	case WIFI_SECURITY_TYPE_NONE:
		return 0;
	case WIFI_SECURITY_TYPE_WEP:
		return SECURITY_PRIVACY;
	case WIFI_SECURITY_TYPE_WPA_PSK:
		return SECURITY_PRIVACY | SECURITY_WPA;
	case WIFI_SECURITY_TYPE_PSK:
	case WIFI_SECURITY_TYPE_SAE:
	case WIFI_SECURITY_TYPE_EAP:
		return SECURITY_PRIVACY | SECURITY_RSN;
	default:
		return SECURITY_PRIVACY;
	}
}

/* Keep the strongest result per SSID and the strongest SCAN_MAX SSIDs overall. */
static void add_scan_result(const struct wifi_scan_result *result)
{
	uint8_t length = MIN(result->ssid_length, ZF_SSID_MAX_BYTES);
	struct scan_entry *slot = NULL;

	if (scan_state != ZF_SCAN_RUNNING || length == 0) {
		return;
	}
	for (uint8_t i = 0; i < scan_count; ++i) {
		if (scan[i].ssid_length == length && memcmp(scan[i].ssid, result->ssid, length) == 0) {
			if (result->rssi <= scan[i].rssi) {
				return;
			}
			slot = &scan[i];
			break;
		}
	}
	if (slot == NULL && scan_count < SCAN_MAX) {
		slot = &scan[scan_count++];
	}
	if (slot == NULL) {
		slot = &scan[0];
		for (uint8_t i = 1; i < scan_count; ++i) {
			if (scan[i].rssi < slot->rssi) {
				slot = &scan[i];
			}
		}
		if (result->rssi <= slot->rssi) {
			return;
		}
	}
	memset(slot, 0, sizeof(*slot));
	memcpy(slot->ssid, result->ssid, length);
	slot->ssid_length = length;
	slot->rssi = (int8_t)CLAMP(result->rssi, INT8_MIN, INT8_MAX);
	slot->security = security_bits(result->security);
}

static void post(uint8_t type, int status, uint32_t generation)
{
	struct wifi_event event = {.generation = generation, .status = status, .type = type};

	if (k_msgq_put(&events, &event, K_NO_WAIT) != 0) {
		atomic_set(&event_overflow_pending, 1);
		LOG_ERR("Wi-Fi event queue full");
	}
}

static void event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event, struct net_if *event_iface)
{
	const struct wifi_status *status = cb->info;

	if (event_iface != iface) {
		return;
	}
	switch (mgmt_event) {
	case NET_EVENT_WIFI_CONNECT_RESULT:
#ifdef CONFIG_WIFI_AIROC
		/* AIROC reports association before DHCP; only the bound event completes the join. */
		if (status != NULL && status->status != 0) {
			post(EVENT_DOWN_CONNECTING, status->status, 0);
		}
#else
		/* Failed connects are followed by a disconnect event; success is raised after DHCP binds. */
		if (status != NULL && status->status == 0) {
			post(EVENT_UP, 0, 0);
		}
#endif
		break;
	case NET_EVENT_IPV4_DHCP_BOUND:
		post(EVENT_UP, 0, 0);
		break;
	case NET_EVENT_WIFI_DISCONNECT_RESULT:
		if (status == NULL) {
			post(EVENT_DISCONNECT_FAILED, -EIO, 0);
		} else {
			post(status->status == 0 ? EVENT_DOWN_CONNECTED : EVENT_DISCONNECT_FAILED, status->status, 0);
		}
		break;
	case NET_EVENT_WIFI_SCAN_RESULT:
		if (cb->info != NULL && cb->info_length >= sizeof(struct wifi_scan_result)) {
			zf_lock();
			add_scan_result(cb->info);
			zf_unlock();
		}
		break;
	case NET_EVENT_WIFI_SCAN_DONE:
		post(EVENT_SCAN_DONE, status != NULL && cb->info_length >= sizeof(*status) ? status->status : -EIO, 0);
		break;
	default:
		break;
	}
}

static void apply_events(int64_t now)
{
	struct wifi_event event;

	while (k_msgq_get(&events, &event, K_NO_WAIT) == 0) {
		switch (event.type) {
		case EVENT_UP:
			if (link == LINK_CONNECTING && owner != OWNER_NONE) {
				link_verify_pending = true;
			}
			break;
		case EVENT_DOWN_CONNECTED:
			link_down(now);
			break;
		case EVENT_DOWN_CONNECTING:
			if (link == LINK_LEAVING) {
				break;
			} else if (owner == OWNER_TEST && command == COMMAND_TESTING) {
				fail_test(DETAIL_NONE, now);
			} else {
				link_down(now);
			}
			break;
		case EVENT_DISCONNECT_FAILED:
			disconnect_verify_pending = true;
			if (link != LINK_LEAVING) {
				link = LINK_LEAVING;
			}
			leave_deadline = now + LEAVE_TIMEOUT_MS;
			break;
		case EVENT_SCAN_DONE:
			finish_scan(event.status);
			break;
		case EVENT_LINK_VERIFIED:
			if (event.generation == connection_generation && owner != OWNER_NONE) {
				if (event.status == 0 && link == LINK_CONNECTING) {
					link_verify_pending = false;
					link_up(now);
				} else if (event.status == ZF_WIFI_DRIVER_LINK_DOWN && link == LINK_UP) {
					link_verify_pending = false;
					link_down(now);
				}
			}
			break;
		case EVENT_DISCONNECT_VERIFIED:
			if (event.generation == disconnect_generation && link == LINK_LEAVING) {
				disconnect_verify_pending = false;
				if (event.status == 0) {
					link_down(now);
				} else {
					leave_requested = true;
					leave_deadline = now + LEAVE_TIMEOUT_MS;
					LOG_WRN("Wi-Fi disconnect state is uncertain; retrying");
				}
			}
			break;
		default:
			break;
		}
	}
}

static void reconcile_event_overflow(void)
{
	if (atomic_clear(&event_overflow_pending) == 0) {
		return;
	}

	LOG_ERR("Wi-Fi event queue overflow; reconciling interface state");
	if (link == LINK_LEAVING) {
		disconnect_verify_pending = true;
	} else if (link == LINK_CONNECTING || link == LINK_UP) {
		link_verify_pending = true;
	}
}

static void apply_timeouts(int64_t now)
{
	if (link == LINK_LEAVING && !leave_requested && now >= leave_deadline) {
		LOG_WRN("Wi-Fi disconnect not confirmed; checking interface state");
		disconnect_verify_pending = true;
		leave_deadline = now + LEAVE_TIMEOUT_MS;
	}
	if (link == LINK_CONNECTING && now >= connect_deadline) {
		if (owner == OWNER_TEST && command == COMMAND_TESTING) {
			fail_test(DETAIL_TIMEOUT, now);
		} else if (owner == OWNER_SAVED) {
			saved_failed(DETAIL_TIMEOUT, now);
			leave_requested = true;
			LOG_WRN("Wi-Fi connect timed out");
		}
	}
	if (scan_state == ZF_SCAN_RUNNING && !scan_pending && now >= scan_deadline) {
		scan_state = ZF_SCAN_FAILED;
		scan_count = 0;
		LOG_ERR("Wi-Fi scan timed out; driver remains quarantined until completion or reboot");
	}
}

static void start_connect(enum owner next, int64_t now)
{
	if (++connection_generation == 0) {
		++connection_generation;
	}
	owner = next;
	link = LINK_CONNECTING;
	connect_deadline = now + CONNECT_TIMEOUT_MS;
}

static enum action next_action(int64_t now, zf_wifi_network_t *network)
{
	if (link == LINK_LEAVING) {
		if (disconnect_verify_pending) {
			disconnect_verify_pending = false;
			if (++disconnect_generation == 0) {
				++disconnect_generation;
			}
			return ACTION_VERIFY_DISCONNECT;
		}
		if (leave_requested && now >= leave_deadline) {
			leave_requested = false;
			leave_deadline = now + LEAVE_TIMEOUT_MS;
			return ACTION_DISCONNECT;
		}
		return ACTION_NONE;
	}
	if (leave_requested) {
		if (link != LINK_DOWN) {
			leave_requested = false;
			link_verify_pending = false;
			link = LINK_LEAVING;
			leave_deadline = now + LEAVE_TIMEOUT_MS;
			return ACTION_DISCONNECT;
		}
		if (!connect_quarantined) {
			leave_requested = false;
		}
	}
	if (connect_quarantined) {
		if (now < leave_deadline) {
			return ACTION_NONE;
		}
		link_verify_pending = false;
		link = LINK_LEAVING;
		leave_deadline = now + LEAVE_TIMEOUT_MS;
		return ACTION_DISCONNECT;
	}
	if (link_verify_pending) {
		link_verify_pending = false;
		verification_generation = connection_generation;
		return ACTION_VERIFY;
	}
	if (scan_pending) {
		if (link == LINK_CONNECTING && owner == OWNER_SAVED) {
			/* A join in progress blocks scanning; resume the saved network once the scan ends. */
			owner = OWNER_NONE;
			retry_at = now;
			link = LINK_LEAVING;
			leave_deadline = now + LEAVE_TIMEOUT_MS;
			return ACTION_DISCONNECT;
		}
		scan_pending = false;
		scan_deadline = now + SCAN_TIMEOUT_MS;
		scan_driver_active = true;
		return ACTION_SCAN;
	}
	if (test_join_pending) {
		if (link != LINK_DOWN) {
			owner = OWNER_NONE;
			link_verify_pending = false;
			link = LINK_LEAVING;
			leave_deadline = now + LEAVE_TIMEOUT_MS;
			return ACTION_DISCONNECT;
		}
		test_join_pending = false;
		start_connect(OWNER_TEST, now);
		*network = test;
		return ACTION_CONNECT;
	}
	if (saved_enabled && iface != NULL && owner == OWNER_NONE && link == LINK_DOWN &&
	    command != COMMAND_TESTING && command != COMMAND_JOINED && scan_state != ZF_SCAN_RUNNING &&
	    driver_operation_available() && retry_at != 0 && now >= retry_at) {
		retry_at = 0;
		wifi_state = STATE_CONNECTING;
		start_connect(OWNER_SAVED, now);
		*network = saved;
		return ACTION_CONNECT;
	}
	return ACTION_NONE;
}

#ifdef CONFIG_WIFI_AIROC
static struct k_work_q request_queue;
K_THREAD_STACK_DEFINE(request_stack, 4096);
static enum action request_action;
static zf_wifi_network_t request_network;
static uint32_t request_generation;
#else
static uint32_t verify_request_generation;
static enum action verify_request_action;
#endif

static void perform_request(enum action action, zf_wifi_network_t *network, uint32_t generation)
{
	int err = 0;

	switch (action) {
	case ACTION_DISCONNECT:
		err = zf_wifi_driver_request(ZF_WIFI_DRIVER_DISCONNECT, iface, NULL);
		break;
	case ACTION_CONNECT:
		err = zf_wifi_driver_request(ZF_WIFI_DRIVER_CONNECT, iface, network);
		memset(network, 0, sizeof(*network));
		break;
	case ACTION_SCAN:
		err = zf_wifi_driver_request(ZF_WIFI_DRIVER_SCAN, iface, NULL);
		break;
	case ACTION_VERIFY:
		err = zf_wifi_driver_request(ZF_WIFI_DRIVER_VERIFY_UP, iface, NULL);
		post(EVENT_LINK_VERIFIED, err, generation);
		return;
	case ACTION_VERIFY_DISCONNECT:
		err = zf_wifi_driver_request(ZF_WIFI_DRIVER_VERIFY_DOWN, iface, NULL);
		post(EVENT_DISCONNECT_VERIFIED, err, generation);
		return;
	default:
		return;
	}
	if (err == 0) {
		return;
	}

	LOG_ERR("Wi-Fi request %d failed: %d", action, err);
	zf_lock();
	if (action == ACTION_DISCONNECT) {
		disconnect_verify_pending = true;
		leave_deadline = k_uptime_get() + LEAVE_TIMEOUT_MS;
	} else if (action == ACTION_CONNECT) {
		link = LINK_DOWN;
		if (owner == OWNER_TEST && command == COMMAND_TESTING
#ifdef CONFIG_WIFI_AIROC
		    && test_generation == (uint8_t)request_generation
#endif
		) {
			fail_test(DETAIL_TIMEOUT, k_uptime_get());
		} else if (owner == OWNER_SAVED) {
			saved_failed(DETAIL_NONE, k_uptime_get());
		}
	} else if (scan_state == ZF_SCAN_RUNNING) {
		scan_driver_active = false;
		scan_state = ZF_SCAN_FAILED;
		scan_count = 0;
	}
	zf_unlock();
}

#ifdef CONFIG_WIFI_AIROC
static void request_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	perform_request(request_action, &request_network, request_generation);
}

K_WORK_DEFINE(request_work, request_handler);
#else
static void verify_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	perform_request(verify_request_action, NULL, verify_request_generation);
}

K_WORK_DEFINE(verify_work, verify_handler);
#endif

void zf_wifi_poll(void)
{
	int64_t now = k_uptime_get();
	zf_wifi_network_t network;
	enum action action;

	zf_lock();
	apply_events(now);
	reconcile_event_overflow();
	apply_timeouts(now);
#ifdef CONFIG_WIFI_AIROC
	if (k_work_busy_get(&request_work) != 0) {
		zf_unlock();
		return;
	}
#endif
	action = iface != NULL ? next_action(now, &network) : ACTION_NONE;
#ifdef CONFIG_WIFI_AIROC
	if (action != ACTION_NONE) {
		request_action = action;
		if (action == ACTION_CONNECT) {
			request_network = network;
			request_generation = test_generation;
			memset(&network, 0, sizeof(network));
		} else if (action == ACTION_VERIFY) {
			request_generation = verification_generation;
		} else if (action == ACTION_VERIFY_DISCONNECT) {
			request_generation = disconnect_generation;
		}
		if (k_work_submit_to_queue(&request_queue, &request_work) < 0) {
			LOG_ERR("Wi-Fi request queue unavailable");
			if (action == ACTION_CONNECT) {
				memset(&request_network, 0, sizeof(request_network));
				link = LINK_DOWN;
				if (owner == OWNER_TEST) {
					fail_test(DETAIL_RADIO, now);
				} else {
					saved_failed(DETAIL_RADIO, now);
				}
			} else if (action == ACTION_SCAN) {
				scan_driver_active = false;
				scan_state = ZF_SCAN_FAILED;
				scan_count = 0;
			} else if (action == ACTION_VERIFY) {
				link_verify_pending = true;
			} else if (action == ACTION_VERIFY_DISCONNECT) {
				disconnect_verify_pending = true;
			} else {
				leave_requested = true;
			}
		}
	}
	zf_unlock();
#else
	if (action == ACTION_VERIFY || action == ACTION_VERIFY_DISCONNECT) {
		if (k_work_busy_get(&verify_work) != 0) {
			if (action == ACTION_VERIFY) {
				link_verify_pending = true;
			} else {
				disconnect_verify_pending = true;
			}
		} else {
			verify_request_action = action;
			verify_request_generation = verification_generation;
			if (action == ACTION_VERIFY_DISCONNECT) {
				verify_request_generation = disconnect_generation;
			}
			if (k_work_submit(&verify_work) < 0) {
				if (action == ACTION_VERIFY) {
					link_verify_pending = true;
				} else {
					disconnect_verify_pending = true;
				}
				LOG_ERR("Wi-Fi verification work unavailable");
			}
		}
	}
	zf_unlock();
	if (action != ACTION_VERIFY && action != ACTION_VERIFY_DISCONNECT) {
		perform_request(action, &network, test_generation);
	}
#endif
}

void zf_wifi_init(bool enabled)
{
	zf_lock();
	atomic_clear(&event_overflow_pending);
	saved.ssid_length = zf_boot_config_wifi_ssid_bytes(saved.ssid);
	zf_boot_config_wifi_password(saved.password);
	saved_enabled = enabled && saved.ssid_length != 0;
	iface = net_if_get_first_wifi();
	if (iface == NULL) {
		wifi_state = enabled ? STATE_FAILED : STATE_OFF;
		saved_detail = enabled ? DETAIL_RADIO : DETAIL_NONE;
		zf_unlock();
		LOG_ERR("No Wi-Fi interface");
		return;
	}
	net_mgmt_init_event_callback(&event_cb, event_handler, EVENT_MASK);
	net_mgmt_add_event_callback(&event_cb);
#ifdef CONFIG_WIFI_AIROC
	net_mgmt_init_event_callback(&dhcp_cb, event_handler, NET_EVENT_IPV4_DHCP_BOUND);
	net_mgmt_add_event_callback(&dhcp_cb);
#endif
	if (saved_enabled) {
		wifi_state = STATE_CONNECTING;
		retry_at = k_uptime_get();
	}
	zf_unlock();
#ifdef CONFIG_WIFI_AIROC
	k_work_queue_start(&request_queue, request_stack, K_THREAD_STACK_SIZEOF(request_stack), 5, NULL);
#endif
}

bool zf_wifi_test_active(void)
{
	return command == COMMAND_TESTING;
}

bool zf_wifi_scan_active(void)
{
	return scan_state == ZF_SCAN_RUNNING || scan_driver_active;
}

int zf_wifi_test_start(const uint8_t *ssid, uint8_t ssid_length, const char *password)
{
	if (!driver_operation_available()) {
		return -EBUSY;
	}
	if (++test_generation == 0) {
		++test_generation;
	}
	wipe(&test);
	memcpy(test.ssid, ssid, MIN(ssid_length, ZF_SSID_MAX_BYTES));
	test.ssid_length = MIN(ssid_length, ZF_SSID_MAX_BYTES);
	strncpy(test.password, password, sizeof(test.password) - 1);
	if (iface == NULL) {
		command = COMMAND_FAILED;
		join_detail = DETAIL_RADIO;
		memset(test.password, 0, sizeof(test.password));
		return -ENODEV;
	}
	command = COMMAND_TESTING;
	join_detail = DETAIL_NONE;
	test_join_pending = true;
	retry_at = 0;
	connect_deadline = k_uptime_get() + CONNECT_TIMEOUT_MS;
	return 0;
}

void zf_wifi_test_cancel(void)
{
	if (owner == OWNER_TEST && link != LINK_DOWN) {
		connect_quarantined = true;
		if (link != LINK_LEAVING) {
			leave_deadline = 0;
		}
	}
	command = COMMAND_IDLE;
	test_join_pending = false;
	wipe(&test);
	if (owner == OWNER_TEST || (owner == OWNER_NONE && link != LINK_DOWN)) {
		release_link();
	}
	if (owner == OWNER_NONE) {
		wifi_state = saved_enabled ? STATE_CONNECTING : STATE_OFF;
		retry_delay = RETRY_DELAY_MS;
		retry_at = saved_enabled ? k_uptime_get() : 0;
	}
}

int zf_wifi_scan_start(void)
{
	if (iface == NULL) {
		return -ENODEV;
	}
	if (!driver_operation_available()) {
		return -EBUSY;
	}
	if (++scan_generation == 0) {
		++scan_generation;
	}
	memset(scan, 0, sizeof(scan));
	scan_count = 0;
	scan_state = ZF_SCAN_RUNNING;
	scan_pending = true;
	scan_deadline = k_uptime_get() + SCAN_TIMEOUT_MS;
	return 0;
}

uint8_t zf_wifi_scan_state(void)
{
	return scan_state;
}

uint16_t zf_wifi_scan_generation(void)
{
	return scan_generation;
}

uint8_t zf_wifi_scan_count(void)
{
	return scan_state == ZF_SCAN_COMPLETE ? scan_count : 0;
}

int zf_wifi_scan_entry(uint8_t index, uint8_t *ssid, uint8_t *ssid_length, int8_t *rssi, uint8_t *security)
{
	if (scan_state != ZF_SCAN_COMPLETE || index >= scan_count) {
		return -EINVAL;
	}
	memcpy(ssid, scan[index].ssid, ZF_SSID_MAX_BYTES);
	*ssid_length = scan[index].ssid_length;
	*rssi = scan[index].rssi;
	*security = scan[index].security;
	return 0;
}

static void ipv4_address(uint8_t out[4])
{
	const struct in_addr *address = net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);

	if (address != NULL) {
		memcpy(out, address->s4_addr, 4);
	}
}

void zf_wireless_report(uint8_t *out)
{
	zf_wireless_t status = {0};
	const zf_wifi_network_t *shown;
	uint8_t stage_flags = 0;

	zf_lock();
	switch (command) {
	case COMMAND_TESTING:
		status.wifi_state = 4;
		break;
	case COMMAND_JOINED:
		status.wifi_state = 5;
		break;
	case COMMAND_FAILED:
		status.wifi_state = 6;
		status.detail = join_detail;
		break;
	default:
		status.wifi_state = wifi_state;
		status.detail = saved_detail != DETAIL_NONE ? saved_detail : zf_ble_failed() ? DETAIL_RADIO : DETAIL_NONE;
		break;
	}
	status.ble_state = zf_ble_state();
	zf_control_stage_status(&stage_flags, &status.staged_state);
	status.flags = (uint8_t)((saved.ssid_length ? 0x01u : 0u) | (zf_boot_config_ble_key_configured() ? 0x02u : 0u) |
				 (stage_flags & 0xF0u));
	if (link == LINK_UP && iface != NULL) {
		ipv4_address(status.ipv4);
	}
	shown = test.ssid_length ? &test : &saved;
	status.ssid_length = shown->ssid_length;
	memcpy(status.ssid, shown->ssid, shown->ssid_length);
	status.scan_state = scan_state;
	status.scan_generation = scan_generation;
	status.scan_count = zf_wifi_scan_count();
	status.test_generation = test_generation;
	zf_unlock();
	zf_report_wireless(&status, out);
}
