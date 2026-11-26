/*
 * Wi-Fi station: saved-network join with paced retries, candidate tests and scans (Report 12, opcodes 9-12).
 *
 * Uses Zephyr net_mgmt. Driver events are queued by the callback and applied in zf_wifi_poll(); driver requests
 * run on a dedicated queue so scans and joins never block the application loop or hold its lock.
 */
#include "wifi/wifi.h"
#include "protocol/report_codec.h"
#include "settings/settings.h"
#include "wifi/wifi_driver.h"
#include "wifi/wifi_policy.h"
#include "wifi/wifi_scan_table.h"
#include "app/runtime.h"

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
#define VERIFY_RETRY_MS 1000
#define LEAVE_TIMEOUT_MS 5000
#define SCAN_TIMEOUT_MS (IS_ENABLED(CONFIG_WIFI_AIROC) ? 15000 : 10000)

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
	EVENT_LINK_VERIFIED, /**< The interface is associated and has a preferred IPv4 address. */
	EVENT_DISCONNECT_VERIFIED, /**< Interface status after an ambiguous disconnect result. */
};

/** Driver lifecycle for a requested scan; timed-out work stays quarantined until completion. */
enum scan_driver_state {
	SCAN_DRIVER_IDLE, /**< No scan request is pending in or active on the driver. */
	SCAN_DRIVER_PENDING, /**< Admitted; waiting for the link to be released before dispatch. */
	SCAN_DRIVER_ACTIVE, /**< The driver owns the scan request. */
	SCAN_DRIVER_QUARANTINED, /**< Timed out locally, but the driver may still be scanning. */
};

/** A deferred driver event; status and generation survive the queue boundary. */
struct wifi_event {
	uint32_t generation; /**< Connection generation for verification results. */
	int status; /**< Driver status or verification outcome. */
	uint8_t type; /**< One event value. */
};

K_MSGQ_DEFINE(events, sizeof(struct wifi_event), 16, 4);
static atomic_t event_overflow_pending = ATOMIC_INIT(0);
static atomic_t scan_done_pending = ATOMIC_INIT(0);
static atomic_t scan_done_status;
#if defined(CONFIG_WIFI_AIROC)
static atomic_t interface_down_pending;
#endif

static struct net_mgmt_event_callback event_cb;
#if defined(CONFIG_WIFI_AIROC)
static struct net_mgmt_event_callback interface_cb;
static struct net_mgmt_event_callback dhcp_cb;
#endif
static struct net_if *iface;
K_MUTEX_DEFINE(wifi_lock);

static bool saved_enabled;
static uint8_t saved_ssid[ZF_SSID_MAX_BYTES];
static uint8_t saved_ssid_length;
static uint8_t wifi_state;
static uint8_t saved_detail;
static int64_t retry_at;
static int64_t retry_delay = RETRY_DELAY_MS;

static enum link link;
static zf_wifi_owner_t owner;
static bool connect_quarantined;
static int64_t connect_deadline;
static int64_t leave_deadline;
static uint32_t connection_generation;
static uint32_t verification_generation;
static uint32_t disconnect_generation;
static zf_wifi_link_work_t link_work;
static int64_t link_verify_at;
static bool interface_cleanup_pending;

static zf_wifi_command_t command;
static uint8_t join_detail;
static zf_wifi_network_t test;
static uint8_t test_generation;

static uint8_t scan_state;
static uint16_t scan_generation;
static enum scan_driver_state scan_driver;
static int64_t scan_deadline;
static zf_wifi_scan_table_t scan;

static bool scan_driver_busy(void)
{
	return scan_driver != SCAN_DRIVER_IDLE;
}

static void schedule_retry(int64_t now)
{
	retry_at = now + retry_delay;
	retry_delay = MIN(retry_delay * 2, RETRY_MAX_MS);
}

/* Drop the current link; its disconnect event is then ignored as an expected leave. */
static void release_link(void)
{
	owner = ZF_WIFI_OWNER_NONE;
	if (link_work == ZF_WIFI_LINK_WORK_VERIFY_UP) {
		link_work = ZF_WIFI_LINK_WORK_IDLE;
	}
	if (link != LINK_DOWN && link != LINK_LEAVING) {
		link_work = ZF_WIFI_LINK_WORK_LEAVE;
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
	command = ZF_WIFI_COMMAND_FAILED;
	join_detail = detail;
	memset(test.password, 0, sizeof(test.password));
	release_link();
	retry_delay = RETRY_DELAY_MS;
	retry_at = now;
	wifi_state = saved_enabled ? ZF_WIFI_CONNECTING : ZF_WIFI_OFF;
	LOG_WRN("Wi-Fi test failed (detail %u)", detail);
}

static void saved_failed(uint8_t detail, int64_t now)
{
	owner = ZF_WIFI_OWNER_NONE;
	wifi_state = ZF_WIFI_FAILED;
	saved_detail = detail;
	schedule_retry(now);
}

static void link_up(int64_t now)
{
	if (link == LINK_LEAVING) {
		return;
	}
	link = LINK_UP;
	if (owner == ZF_WIFI_OWNER_TEST && command == ZF_WIFI_COMMAND_TESTING) {
		command = ZF_WIFI_COMMAND_JOINED;
		join_detail = ZF_WIRELESS_DETAIL_NONE;
		LOG_INF("Wi-Fi test joined");
	} else if (owner == ZF_WIFI_OWNER_SAVED) {
		wifi_state = ZF_WIFI_CONNECTED;
		saved_detail = ZF_WIRELESS_DETAIL_NONE;
		retry_delay = RETRY_DELAY_MS;
		LOG_INF("Wi-Fi connected");
	} else {
		link_work = ZF_WIFI_LINK_WORK_LEAVE;
	}
}

static void link_down(int64_t now)
{
	enum link previous = link;

	link = LINK_DOWN;
	if (previous == LINK_LEAVING) {
		connect_quarantined = false;
		link_work = ZF_WIFI_LINK_WORK_IDLE;
	}
	switch (zf_wifi_policy_link_down(owner, command)) {
	case ZF_WIFI_POLICY_FAIL_TEST:
		fail_test(ZF_WIRELESS_DETAIL_NONE, now);
		break;
	case ZF_WIFI_POLICY_TEST_LINK_LOST:
		owner = ZF_WIFI_OWNER_NONE;
		command = ZF_WIFI_COMMAND_FAILED;
		join_detail = ZF_WIRELESS_DETAIL_LINK_LOST;
		wifi_state = saved_enabled ? ZF_WIFI_FAILED : ZF_WIFI_OFF;
		schedule_retry(now);
		LOG_WRN("Test Wi-Fi link lost");
		break;
	case ZF_WIFI_POLICY_RETRY_SAVED:
		owner = ZF_WIFI_OWNER_NONE;
		saved_failed(ZF_WIRELESS_DETAIL_NONE, now);
		LOG_WRN("Wi-Fi link down");
		break;
	case ZF_WIFI_POLICY_RELEASE:
		owner = ZF_WIFI_OWNER_NONE;
		break;
	}
}

static void finish_scan(int status)
{
	if (scan_driver != SCAN_DRIVER_ACTIVE && scan_driver != SCAN_DRIVER_QUARANTINED) {
		return;
	}
	scan_driver = SCAN_DRIVER_IDLE;
	if (scan_state != ZF_SCAN_RUNNING) {
		return;
	}
	if (status != 0) {
		scan.count = 0;
		scan_state = ZF_SCAN_FAILED;
		LOG_WRN("Wi-Fi scan failed: %d", status);
		return;
	}
	zf_wifi_scan_table_sort(&scan);
	scan_state = ZF_SCAN_COMPLETE;
}

static uint8_t security_bits(enum wifi_security_type security)
{
	switch (security) {
	case WIFI_SECURITY_TYPE_NONE:
		return 0;
	case WIFI_SECURITY_TYPE_WEP:
		return ZF_WIFI_SECURITY_PRIVACY;
	case WIFI_SECURITY_TYPE_WPA_PSK:
		return ZF_WIFI_SECURITY_PRIVACY | ZF_WIFI_SECURITY_WPA;
	case WIFI_SECURITY_TYPE_PSK:
	case WIFI_SECURITY_TYPE_SAE:
	case WIFI_SECURITY_TYPE_EAP:
		return ZF_WIFI_SECURITY_PRIVACY | ZF_WIFI_SECURITY_RSN;
	default:
		return ZF_WIFI_SECURITY_PRIVACY;
	}
}

static void add_scan_result(const struct wifi_scan_result *result)
{
	if (scan_state == ZF_SCAN_RUNNING) {
		zf_wifi_scan_table_add(&scan, result->ssid, result->ssid_length, result->rssi,
				  security_bits(result->security));
	}
}

static void post(uint8_t type, int status, uint32_t generation)
{
	struct wifi_event event = {.generation = generation, .status = status, .type = type};

	if (k_msgq_put(&events, &event, K_NO_WAIT) != 0) {
		atomic_set(&event_overflow_pending, 1);
		LOG_ERR("Wi-Fi event queue full");
	}
	zf_app_wakeup();
}

static void post_scan_done(int status)
{
	atomic_set(&scan_done_status, status);
	atomic_set(&scan_done_pending, 1);
	zf_app_wakeup();
}

#if defined(CONFIG_WIFI_AIROC)
static void post_interface_down(void)
{
	atomic_set(&interface_down_pending, 1);
	zf_app_wakeup();
}
#endif

static void event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event, struct net_if *event_iface)
{
	const struct wifi_status *status = cb->info;

	if (event_iface != iface) {
		return;
	}
#if defined(CONFIG_WIFI_AIROC)
	if (mgmt_event == NET_EVENT_IF_DOWN) {
		post_interface_down();
		return;
	}
#endif
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
			k_mutex_lock(&wifi_lock, K_FOREVER);
			add_scan_result(cb->info);
			k_mutex_unlock(&wifi_lock);
		}
		break;
	case NET_EVENT_WIFI_SCAN_DONE:
		post_scan_done(status != NULL && cb->info_length >= sizeof(*status) ? status->status : -EIO);
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
			if (link == LINK_CONNECTING && owner != ZF_WIFI_OWNER_NONE) {
				link_work = ZF_WIFI_LINK_WORK_VERIFY_UP;
				link_verify_at = 0;
			}
			break;
		case EVENT_DOWN_CONNECTED:
			if (link == LINK_LEAVING && interface_cleanup_pending) {
				interface_cleanup_pending = false;
			}
			link_down(now);
			break;
		case EVENT_DOWN_CONNECTING:
			if (link == LINK_LEAVING) {
				break;
			} else if (owner == ZF_WIFI_OWNER_TEST && command == ZF_WIFI_COMMAND_TESTING) {
				fail_test(ZF_WIRELESS_DETAIL_NONE, now);
			} else {
				link_down(now);
			}
			break;
		case EVENT_DISCONNECT_FAILED:
			link_work = ZF_WIFI_LINK_WORK_VERIFY_DOWN;
			if (link != LINK_LEAVING) {
				link = LINK_LEAVING;
			}
			leave_deadline = now + LEAVE_TIMEOUT_MS;
			break;
		case EVENT_LINK_VERIFIED:
			if (event.generation == connection_generation && owner != ZF_WIFI_OWNER_NONE) {
				if (event.status == 0 && link == LINK_CONNECTING) {
					link_work = ZF_WIFI_LINK_WORK_IDLE;
					link_up(now);
				} else if (event.status == ZF_WIFI_DRIVER_LINK_DOWN && link == LINK_UP) {
					link_work = ZF_WIFI_LINK_WORK_IDLE;
					link_down(now);
				} else if (event.status != 0 &&
					   (link == LINK_CONNECTING || link == LINK_UP)) {
					link_work = ZF_WIFI_LINK_WORK_VERIFY_UP;
					link_verify_at = now + VERIFY_RETRY_MS;
				}
			}
			break;
		case EVENT_DISCONNECT_VERIFIED:
			if (event.generation == disconnect_generation && link == LINK_LEAVING) {
				link_work = ZF_WIFI_LINK_WORK_IDLE;
				if (event.status == 0) {
					if (interface_cleanup_pending) {
						link_work = ZF_WIFI_LINK_WORK_LEAVE;
						leave_deadline = now + LEAVE_TIMEOUT_MS;
					} else {
						link_down(now);
					}
				} else {
					link_work = ZF_WIFI_LINK_WORK_LEAVE;
					leave_deadline = now + LEAVE_TIMEOUT_MS;
					LOG_WRN("Wi-Fi disconnect state is uncertain; retrying");
				}
			}
			break;
		default:
			break;
		}
	}
	if (atomic_cas(&scan_done_pending, 1, 0)) {
		finish_scan(atomic_get(&scan_done_status));
	}
#if defined(CONFIG_WIFI_AIROC)
	if (atomic_clear(&interface_down_pending) != 0 &&
	    (link == LINK_UP || link == LINK_CONNECTING)) {
		link = LINK_LEAVING;
		link_work = ZF_WIFI_LINK_WORK_LEAVE;
		connect_quarantined = true;
		leave_deadline = now;
		interface_cleanup_pending = true;
	}
#endif
}

static void reconcile_event_overflow(void)
{
	if (atomic_clear(&event_overflow_pending) == 0) {
		return;
	}

	LOG_ERR("Wi-Fi event queue overflow; reconciling interface state");
	if (link == LINK_LEAVING) {
		if (interface_cleanup_pending) {
			return;
		}
		link_work = ZF_WIFI_LINK_WORK_VERIFY_DOWN;
	} else if (link == LINK_CONNECTING || link == LINK_UP) {
		link_work = ZF_WIFI_LINK_WORK_VERIFY_UP;
		link_verify_at = 0;
	}
}

static void apply_timeouts(int64_t now)
{
	if (link == LINK_LEAVING && zf_wifi_policy_link_work_action(link_work) != ZF_WIFI_LINK_ACTION_LEAVE &&
	    now >= leave_deadline) {
		LOG_WRN("Wi-Fi disconnect not confirmed; checking interface state");
		link_work = ZF_WIFI_LINK_WORK_VERIFY_DOWN;
		leave_deadline = now + LEAVE_TIMEOUT_MS;
	}
	if (link == LINK_CONNECTING && now >= connect_deadline) {
		if (owner == ZF_WIFI_OWNER_TEST && command == ZF_WIFI_COMMAND_TESTING) {
			fail_test(ZF_WIRELESS_DETAIL_TIMEOUT, now);
		} else if (owner == ZF_WIFI_OWNER_SAVED) {
			saved_failed(ZF_WIRELESS_DETAIL_TIMEOUT, now);
			link_work = ZF_WIFI_LINK_WORK_LEAVE;
			LOG_WRN("Wi-Fi connect timed out");
		}
	}
	if (scan_state == ZF_SCAN_RUNNING && scan_driver == SCAN_DRIVER_ACTIVE && now >= scan_deadline) {
		scan_state = ZF_SCAN_FAILED;
		scan_driver = SCAN_DRIVER_QUARANTINED;
		scan.count = 0;
		LOG_ERR("Wi-Fi scan timed out; driver remains quarantined until completion or reboot");
	}
}

static void start_connect(zf_wifi_owner_t next, int64_t now)
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
		zf_wifi_link_action_t work = zf_wifi_policy_link_work_action(link_work);

		if (work == ZF_WIFI_LINK_ACTION_VERIFY_DOWN) {
			link_work = ZF_WIFI_LINK_WORK_IDLE;
			if (++disconnect_generation == 0) {
				++disconnect_generation;
			}
			return ACTION_VERIFY_DISCONNECT;
		}
		if (work == ZF_WIFI_LINK_ACTION_LEAVE && now >= leave_deadline) {
			link_work = ZF_WIFI_LINK_WORK_IDLE;
			leave_deadline = now + LEAVE_TIMEOUT_MS;
			return ACTION_DISCONNECT;
		}
		return ACTION_NONE;
	}
	if (zf_wifi_policy_link_work_action(link_work) == ZF_WIFI_LINK_ACTION_LEAVE) {
		if (link != LINK_DOWN) {
			link_work = ZF_WIFI_LINK_WORK_IDLE;
			link = LINK_LEAVING;
			leave_deadline = now + LEAVE_TIMEOUT_MS;
			return ACTION_DISCONNECT;
		}
		if (!connect_quarantined) {
			link_work = ZF_WIFI_LINK_WORK_IDLE;
		}
	}
	if (connect_quarantined) {
		if (now < leave_deadline) {
			return ACTION_NONE;
		}
		if (link_work != ZF_WIFI_LINK_WORK_LEAVE) {
			link_work = ZF_WIFI_LINK_WORK_IDLE;
		}
		link = LINK_LEAVING;
		leave_deadline = now + LEAVE_TIMEOUT_MS;
		return ACTION_DISCONNECT;
	}
	if (zf_wifi_policy_link_work_action(link_work) == ZF_WIFI_LINK_ACTION_VERIFY_UP) {
		if (now < link_verify_at) {
			return ACTION_NONE;
		}
		link_work = ZF_WIFI_LINK_WORK_IDLE;
		verification_generation = connection_generation;
		return ACTION_VERIFY;
	}
	if (scan_driver == SCAN_DRIVER_PENDING) {
		if (link == LINK_CONNECTING && owner == ZF_WIFI_OWNER_SAVED) {
			/* A join in progress blocks scanning; resume the saved network once the scan ends. */
			owner = ZF_WIFI_OWNER_NONE;
			retry_at = now;
			link = LINK_LEAVING;
			leave_deadline = now + LEAVE_TIMEOUT_MS;
			return ACTION_DISCONNECT;
		}
		scan_deadline = now + SCAN_TIMEOUT_MS;
		scan_driver = SCAN_DRIVER_ACTIVE;
		return ACTION_SCAN;
	}
	if (command == ZF_WIFI_COMMAND_QUEUED) {
		if (link != LINK_DOWN) {
			owner = ZF_WIFI_OWNER_NONE;
			if (link_work == ZF_WIFI_LINK_WORK_VERIFY_UP) {
				link_work = ZF_WIFI_LINK_WORK_IDLE;
			}
			link = LINK_LEAVING;
			leave_deadline = now + LEAVE_TIMEOUT_MS;
			return ACTION_DISCONNECT;
		}
		start_connect(ZF_WIFI_OWNER_TEST, now);
		command = ZF_WIFI_COMMAND_TESTING;
		*network = test;
		return ACTION_CONNECT;
	}
	if (saved_enabled && iface != NULL && owner == ZF_WIFI_OWNER_NONE && link == LINK_DOWN &&
	    !zf_wifi_policy_test_in_progress(command) && command != ZF_WIFI_COMMAND_JOINED &&
	    scan_state != ZF_SCAN_RUNNING &&
	    zf_wifi_policy_driver_operation_available(scan_driver_busy(), connect_quarantined) &&
	    retry_at != 0 && now >= retry_at) {
		retry_at = 0;
		wifi_state = ZF_WIFI_CONNECTING;
		start_connect(ZF_WIFI_OWNER_SAVED, now);
		network->ssid_length = saved_ssid_length;
		memcpy(network->ssid, saved_ssid, saved_ssid_length);
		return ACTION_CONNECT;
	}
	return ACTION_NONE;
}

static struct k_work_q request_queue;
K_THREAD_STACK_DEFINE(request_stack, 4096);
static enum action request_action;
static zf_wifi_network_t request_network;
static uint32_t request_generation;

static void request_wakeup_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	zf_app_wakeup();
}

K_WORK_DEFINE(request_wakeup_work, request_wakeup_handler);

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
	if (err != 0) {
		LOG_ERR("Wi-Fi request %d failed: %d", action, err);
		k_mutex_lock(&wifi_lock, K_FOREVER);
		if (action == ACTION_DISCONNECT) {
			link_work = ZF_WIFI_LINK_WORK_VERIFY_DOWN;
			leave_deadline = k_uptime_get() + LEAVE_TIMEOUT_MS;
		} else if (action == ACTION_CONNECT && generation == connection_generation &&
			   link == LINK_CONNECTING && owner != ZF_WIFI_OWNER_NONE) {
			link = LINK_DOWN;
			if (owner == ZF_WIFI_OWNER_TEST && command == ZF_WIFI_COMMAND_TESTING) {
				fail_test(ZF_WIRELESS_DETAIL_TIMEOUT, k_uptime_get());
			} else if (owner == ZF_WIFI_OWNER_SAVED) {
				saved_failed(ZF_WIRELESS_DETAIL_NONE, k_uptime_get());
			}
		} else if (action == ACTION_SCAN && scan_state == ZF_SCAN_RUNNING) {
			scan_driver = SCAN_DRIVER_IDLE;
			scan_state = ZF_SCAN_FAILED;
			scan.count = 0;
		}
		k_mutex_unlock(&wifi_lock);
	}
}

static void request_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	perform_request(request_action, &request_network, request_generation);
	k_work_submit_to_queue(&request_queue, &request_wakeup_work);
}

K_WORK_DEFINE(request_work, request_handler);

void zf_wifi_poll(void)
{
	int64_t now = k_uptime_get();
	zf_wifi_network_t network = {0};
	enum action action;
	bool saved_join;
	uint32_t saved_generation = 0;

	k_mutex_lock(&wifi_lock, K_FOREVER);
	apply_events(now);
	reconcile_event_overflow();
	apply_timeouts(now);
	if (k_work_busy_get(&request_work) != 0) {
		k_mutex_unlock(&wifi_lock);
		return;
	}
	action = iface != NULL ? next_action(now, &network) : ACTION_NONE;
	saved_join = action == ACTION_CONNECT && owner == ZF_WIFI_OWNER_SAVED;
	if (saved_join) {
		saved_generation = connection_generation;
		k_mutex_unlock(&wifi_lock);
		zf_app_lock();
		zf_settings_wifi_password(network.password);
		zf_app_unlock();
		k_mutex_lock(&wifi_lock, K_FOREVER);
		if (owner != ZF_WIFI_OWNER_SAVED || link != LINK_CONNECTING || connection_generation != saved_generation) {
			memset(&network, 0, sizeof(network));
			action = ACTION_NONE;
		}
	}
	if (action != ACTION_NONE) {
		request_action = action;
		if (action == ACTION_CONNECT) {
			request_network = network;
			request_generation = connection_generation;
			memset(&network, 0, sizeof(network));
		} else if (action == ACTION_VERIFY) {
			request_generation = verification_generation;
		} else if (action == ACTION_VERIFY_DISCONNECT) {
			request_generation = disconnect_generation;
		} else {
			request_generation = 0;
		}
		if (k_work_submit_to_queue(&request_queue, &request_work) < 0) {
			LOG_ERR("Wi-Fi request queue unavailable");
			if (action == ACTION_CONNECT) {
				memset(&request_network, 0, sizeof(request_network));
				link = LINK_DOWN;
				if (owner == ZF_WIFI_OWNER_TEST) {
					fail_test(ZF_WIRELESS_DETAIL_RADIO, now);
				} else {
					saved_failed(ZF_WIRELESS_DETAIL_RADIO, now);
				}
			} else if (action == ACTION_SCAN) {
				scan_driver = SCAN_DRIVER_IDLE;
				scan_state = ZF_SCAN_FAILED;
				scan.count = 0;
			} else if (action == ACTION_VERIFY) {
				link_work = ZF_WIFI_LINK_WORK_VERIFY_UP;
				link_verify_at = now;
			} else if (action == ACTION_VERIFY_DISCONNECT) {
				link_work = ZF_WIFI_LINK_WORK_VERIFY_DOWN;
			} else {
				link_work = ZF_WIFI_LINK_WORK_LEAVE;
			}
		}
	}
	k_mutex_unlock(&wifi_lock);
	memset(&network, 0, sizeof(network));
}

int64_t zf_wifi_next_deadline(void)
{
	int64_t now = k_uptime_get();
	int64_t deadline = INT64_MAX;

	k_mutex_lock(&wifi_lock, K_FOREVER);
	if (retry_at > now) {
		deadline = retry_at;
	}
	if (link == LINK_CONNECTING && connect_deadline > now && connect_deadline < deadline) {
		deadline = connect_deadline;
	}
	if (link == LINK_LEAVING && leave_deadline > now && leave_deadline < deadline) {
		deadline = leave_deadline;
	}
	if (scan_state == ZF_SCAN_RUNNING && scan_driver == SCAN_DRIVER_ACTIVE && scan_deadline > now &&
	    scan_deadline < deadline) {
		deadline = scan_deadline;
	}
	if (zf_wifi_policy_link_work_action(link_work) == ZF_WIFI_LINK_ACTION_VERIFY_UP && link_verify_at > now &&
	    link_verify_at < deadline) {
		deadline = link_verify_at;
	}
	k_mutex_unlock(&wifi_lock);
	return deadline;
}

void zf_wifi_init(bool enabled)
{
	zf_app_lock();
	k_mutex_lock(&wifi_lock, K_FOREVER);
	atomic_clear(&event_overflow_pending);
	atomic_clear(&scan_done_pending);
#if defined(CONFIG_WIFI_AIROC)
	atomic_clear(&interface_down_pending);
#endif
	saved_ssid_length = zf_settings_wifi_ssid_bytes(saved_ssid);
	saved_enabled = enabled && saved_ssid_length != 0;
	iface = net_if_get_first_wifi();
	if (iface == NULL) {
		wifi_state = enabled ? ZF_WIFI_FAILED : ZF_WIFI_OFF;
		saved_detail = enabled ? ZF_WIRELESS_DETAIL_RADIO : ZF_WIRELESS_DETAIL_NONE;
		k_mutex_unlock(&wifi_lock);
		zf_app_unlock();
		LOG_ERR("No Wi-Fi interface");
		return;
	}
	net_mgmt_init_event_callback(&event_cb, event_handler, EVENT_MASK);
	net_mgmt_add_event_callback(&event_cb);
#if defined(CONFIG_WIFI_AIROC)
	net_mgmt_init_event_callback(&interface_cb, event_handler, NET_EVENT_IF_DOWN);
	net_mgmt_add_event_callback(&interface_cb);
	net_mgmt_init_event_callback(&dhcp_cb, event_handler, NET_EVENT_IPV4_DHCP_BOUND);
	net_mgmt_add_event_callback(&dhcp_cb);
#endif
	if (saved_enabled) {
		wifi_state = ZF_WIFI_CONNECTING;
		retry_at = k_uptime_get();
	}
	k_mutex_unlock(&wifi_lock);
	zf_app_unlock();
	k_work_queue_start(&request_queue, request_stack, K_THREAD_STACK_SIZEOF(request_stack), 5, NULL);
}

bool zf_wifi_test_active(void)
{
	bool active;

	k_mutex_lock(&wifi_lock, K_FOREVER);
	active = zf_wifi_policy_test_in_progress(command);
	k_mutex_unlock(&wifi_lock);
	return active;
}

bool zf_wifi_test_joined(void)
{
	bool joined;

	k_mutex_lock(&wifi_lock, K_FOREVER);
	joined = command == ZF_WIFI_COMMAND_JOINED;
	k_mutex_unlock(&wifi_lock);
	return joined;
}

bool zf_wifi_scan_active(void)
{
	bool active;

	k_mutex_lock(&wifi_lock, K_FOREVER);
	active = scan_state == ZF_SCAN_RUNNING || scan_driver_busy();
	k_mutex_unlock(&wifi_lock);
	return active;
}

int zf_wifi_test_start(const uint8_t *ssid, uint8_t ssid_length, const char *password)
{
	k_mutex_lock(&wifi_lock, K_FOREVER);
	if (!zf_wifi_policy_driver_operation_available(scan_driver_busy(), connect_quarantined)) {
		k_mutex_unlock(&wifi_lock);
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
		command = ZF_WIFI_COMMAND_FAILED;
		join_detail = ZF_WIRELESS_DETAIL_RADIO;
		memset(test.password, 0, sizeof(test.password));
		k_mutex_unlock(&wifi_lock);
		return -ENODEV;
	}
	command = ZF_WIFI_COMMAND_QUEUED;
	join_detail = ZF_WIRELESS_DETAIL_NONE;
	retry_at = 0;
	k_mutex_unlock(&wifi_lock);
	zf_app_wakeup();
	return 0;
}

void zf_wifi_test_cancel(void)
{
	k_mutex_lock(&wifi_lock, K_FOREVER);
	if (owner == ZF_WIFI_OWNER_TEST && link != LINK_DOWN) {
		connect_quarantined = true;
		if (link != LINK_LEAVING) {
			leave_deadline = 0;
		}
	}
	command = ZF_WIFI_COMMAND_IDLE;
	wipe(&test);
	if (owner == ZF_WIFI_OWNER_TEST || (owner == ZF_WIFI_OWNER_NONE && link != LINK_DOWN)) {
		release_link();
	}
	if (owner == ZF_WIFI_OWNER_NONE) {
		wifi_state = saved_enabled ? ZF_WIFI_CONNECTING : ZF_WIFI_OFF;
		retry_delay = RETRY_DELAY_MS;
		retry_at = saved_enabled ? k_uptime_get() : 0;
	}
	k_mutex_unlock(&wifi_lock);
	zf_app_wakeup();
}

int zf_wifi_scan_start(void)
{
	k_mutex_lock(&wifi_lock, K_FOREVER);
	if (iface == NULL) {
		k_mutex_unlock(&wifi_lock);
		return -ENODEV;
	}
	if (!zf_wifi_policy_driver_operation_available(scan_driver_busy(), connect_quarantined)) {
		k_mutex_unlock(&wifi_lock);
		return -EBUSY;
	}
	if (++scan_generation == 0) {
		++scan_generation;
	}
	memset(&scan, 0, sizeof(scan));
	scan_state = ZF_SCAN_RUNNING;
	scan_driver = SCAN_DRIVER_PENDING;
	scan_deadline = k_uptime_get() + SCAN_TIMEOUT_MS;
	k_mutex_unlock(&wifi_lock);
	zf_app_wakeup();
	return 0;
}

int zf_wifi_scan_entry(uint16_t generation, uint8_t index, uint8_t *ssid, uint8_t *ssid_length, int8_t *rssi,
		       uint8_t *security)
{
	const zf_wifi_scan_entry_t *entry;

	k_mutex_lock(&wifi_lock, K_FOREVER);
	if (scan_state == ZF_SCAN_RUNNING || scan_driver_busy()) {
		k_mutex_unlock(&wifi_lock);
		return -EBUSY;
	}
	if (scan_state != ZF_SCAN_COMPLETE || generation != scan_generation) {
		k_mutex_unlock(&wifi_lock);
		return -ESTALE;
	}
	if (index >= scan.count) {
		k_mutex_unlock(&wifi_lock);
		return -EINVAL;
	}
	entry = &scan.entries[index];
	memcpy(ssid, entry->ssid, ZF_SSID_MAX_BYTES);
	*ssid_length = entry->ssid_length;
	*rssi = entry->rssi;
	*security = entry->security;
	k_mutex_unlock(&wifi_lock);
	return 0;
}

static void ipv4_address(uint8_t out[4])
{
	const struct in_addr *address = net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);

	if (address != NULL) {
		memcpy(out, address->s4_addr, 4);
	}
}

bool zf_wifi_status(zf_wireless_t *status)
{
	k_mutex_lock(&wifi_lock, K_FOREVER);
	const uint8_t *shown_ssid = test.ssid_length ? test.ssid : saved_ssid;
	uint8_t shown_length = test.ssid_length ? test.ssid_length : saved_ssid_length;

	switch (command) {
	case ZF_WIFI_COMMAND_QUEUED:
	case ZF_WIFI_COMMAND_TESTING:
		status->wifi_state = ZF_WIFI_TESTING;
		status->detail = ZF_WIRELESS_DETAIL_NONE;
		break;
	case ZF_WIFI_COMMAND_JOINED:
		status->wifi_state = ZF_WIFI_TEST_JOINED;
		status->detail = ZF_WIRELESS_DETAIL_NONE;
		break;
	case ZF_WIFI_COMMAND_FAILED:
		status->wifi_state = ZF_WIFI_TEST_FAILED;
		status->detail = join_detail;
		break;
	default:
		status->wifi_state = wifi_state;
		if (saved_detail != ZF_WIRELESS_DETAIL_NONE) {
			status->detail = saved_detail;
		}
		break;
	}
	if (link == LINK_UP && iface != NULL) {
		ipv4_address(status->ipv4);
	}
	status->ssid_length = shown_length;
	memcpy(status->ssid, shown_ssid, shown_length);
	status->scan_state = scan_state;
	status->scan_generation = scan_generation;
	status->scan_count = scan_state == ZF_SCAN_COMPLETE ? scan.count : 0;
	status->test_generation = test_generation;
	bool configured = saved_ssid_length != 0;

	k_mutex_unlock(&wifi_lock);
	return configured;
}
