/**
 * @file wifi_policy.h
 * @brief Deterministic Wi-Fi lifecycle decisions independent of Zephyr scheduling.
 */
#ifndef ZEPHYRFORGE_WIFI_POLICY_H
#define ZEPHYRFORGE_WIFI_POLICY_H

#include <stdbool.h>

/** Profile currently owning the managed link. */
typedef enum {
	ZF_WIFI_OWNER_NONE, /**< No operation owns the link. */
	ZF_WIFI_OWNER_SAVED, /**< The saved network is being joined or maintained. */
	ZF_WIFI_OWNER_TEST, /**< A candidate network test owns the link. */
} zf_wifi_owner_t;

/** Candidate command phase used to select a confirmed-down outcome. */
typedef enum {
	ZF_WIFI_COMMAND_IDLE, /**< No candidate command result is active. */
	ZF_WIFI_COMMAND_QUEUED, /**< A candidate join is waiting for its interface to be released. */
	ZF_WIFI_COMMAND_TESTING, /**< A candidate join is in progress. */
	ZF_WIFI_COMMAND_JOINED, /**< The candidate join succeeded. */
	ZF_WIFI_COMMAND_FAILED, /**< The candidate join failed. */
} zf_wifi_command_t;

/** Exclusive pending work phase for the managed link. */
typedef enum {
	ZF_WIFI_LINK_WORK_IDLE, /**< No deferred link operation is pending. */
	ZF_WIFI_LINK_WORK_LEAVE, /**< A disconnect should be issued or retried. */
	ZF_WIFI_LINK_WORK_VERIFY_UP, /**< The current connection should be verified. */
	ZF_WIFI_LINK_WORK_VERIFY_DOWN, /**< The disconnect result should be verified. */
} zf_wifi_link_work_t;

/** Work selected from the exclusive pending link phase. */
typedef enum {
	ZF_WIFI_LINK_ACTION_NONE, /**< No link work is ready. */
	ZF_WIFI_LINK_ACTION_LEAVE, /**< Issue a disconnect request. */
	ZF_WIFI_LINK_ACTION_VERIFY_UP, /**< Verify the current connection. */
	ZF_WIFI_LINK_ACTION_VERIFY_DOWN, /**< Verify that the interface is down. */
} zf_wifi_link_action_t;

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
 * @brief Check whether a candidate command is queued or joining.
 * @param command Current candidate command phase.
 * @return True while the candidate command is in progress.
 */
bool zf_wifi_policy_test_in_progress(zf_wifi_command_t command);

/**
 * @brief Select work represented by the exclusive pending link phase.
 * @param work Current pending link phase.
 * @return The work operation represented by that phase.
 */
zf_wifi_link_action_t zf_wifi_policy_link_work_action(zf_wifi_link_work_t work);

/**
 * @brief Check whether an independent driver operation may be admitted.
 * @param scan_busy A scan is pending, active or quarantined until driver completion.
 * @param connect_quarantined A possibly active connection must be resolved first.
 * @return True when neither condition blocks a new operation.
 */
bool zf_wifi_policy_driver_operation_available(bool scan_busy, bool connect_quarantined);

#endif
