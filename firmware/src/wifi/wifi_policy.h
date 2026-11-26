/**
 * @file wifi_policy.h
 * @brief Deterministic Wi-Fi lifecycle decisions independent of Zephyr scheduling.
 */
#ifndef ZEPHYRFORGE_WIFI_POLICY_H
#define ZEPHYRFORGE_WIFI_POLICY_H

#include <stdbool.h>

/** Profile currently owning the managed link. */
typedef enum {
	OWNER_NONE, /**< No operation owns the link. */
	OWNER_SAVED, /**< The saved network is being joined or maintained. */
	OWNER_TEST, /**< A candidate network test owns the link. */
} zf_wifi_owner_t;

/** Candidate command phase used to select a confirmed-down outcome. */
typedef enum {
	COMMAND_IDLE, /**< No candidate command result is active. */
	COMMAND_TESTING, /**< A candidate join is in progress. */
	COMMAND_JOINED, /**< The candidate join succeeded. */
	COMMAND_FAILED, /**< The candidate join failed. */
} zf_wifi_command_t;

/** Action the lifecycle adapter applies after a confirmed interface-down observation. */
typedef enum {
	ZF_WIFI_POLICY_RELEASE, /**< Release ownership without publishing a failure. */
	ZF_WIFI_POLICY_FAIL_TEST, /**< Finish an in-progress candidate test as failed. */
	ZF_WIFI_POLICY_TEST_LINK_LOST, /**< Publish loss of a previously joined candidate. */
	ZF_WIFI_POLICY_RETRY_SAVED, /**< Release a saved-network attempt and schedule its retry. */
} zf_wifi_policy_down_action_t;

/**
 * @brief Select the lifecycle outcome after the driver is confirmed down.
 * @param owner Current owner of the link.
 * @param command Current candidate command phase.
 * @return The single terminal action the Zephyr adapter must apply.
 */
zf_wifi_policy_down_action_t zf_wifi_policy_link_down(zf_wifi_owner_t owner, zf_wifi_command_t command);

/**
 * @brief Check whether an independent driver operation may be admitted.
 * @param scan_active A scan request is still active in the driver.
 * @param connect_quarantined A possibly active connection must be resolved first.
 * @return True when neither condition blocks a new operation.
 */
bool zf_wifi_policy_driver_operation_available(bool scan_active, bool connect_quarantined);

#endif
