#ifdef CONFIG_SOC_FAMILY_RPI_PICO
#include <hardware/flash.h>
#else
#include <esp_flash.h>
#if CONFIG_ESP_SPIRAM
#include <esp_psram.h>
#endif
#endif

#include "app/zephyrforge.h"
#include "app/protocol/hid_reports.h"
#include "platform/boot_config.h"
#include "platform/zf_platform.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/linker/linker-defs.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

#ifdef CONFIG_SOC_FAMILY_RPI_PICO
#include <zephyr/dt-bindings/pinctrl/rpi-pico-pinctrl-common.h>

#define RAM_TOTAL_BYTES DT_REG_SIZE(DT_CHOSEN(zephyr_sram))
/* Report 1 UID: the eight-byte unique ID of the board flash. */
#define UID_SOURCE_SIZE ZF_UID_SIZE
#define PIN_FROM_MUX(mux) (((mux) >> RP2_PIN_NUM_POS) & RP2_PIN_NUM_MASK)
#else
#include <zephyr/dt-bindings/pinctrl/esp-pinctrl-common.h>

#define RAM_TOTAL_BYTES (512u * 1024u)
/* Report 1 UID prefix: the six-byte eFuse base MAC; the remaining UID bytes stay zero. */
#define UID_SOURCE_SIZE 6u
#define PIN_FROM_MUX(mux) ((mux) & ESP32_PIN_NUM_MASK)

#if CONFIG_ESP_SPIRAM
extern char _ext_ram_start[];
extern char _ext_ram_end[];
#endif
#endif
#if ZF_ENABLE_STORAGE
#define SD_PINCTRL DT_PHANDLE_BY_IDX(DT_ALIAS(zephyrforge_sd), pinctrl_0, 0)
#define SD_SPI_PINCTRL DT_PHANDLE_BY_IDX(DT_PARENT(DT_ALIAS(zephyrforge_sd)), pinctrl_0, 0)
#endif
#define GPIO_FROM_MUX(node, index) PIN_FROM_MUX(DT_PROP_BY_IDX(node, pinmux, index))

/* Report 2 monitor UART: the chosen console's controller index and TX/RX pins, read from the same
 * pinctrl the drivers apply (group1 is TX, group2 is RX), so reported facts cannot drift from hardware. */
#define CONSOLE_NODE DT_CHOSEN(zephyr_console)
#define CONSOLE_PINCTRL DT_PHANDLE_BY_IDX(CONSOLE_NODE, pinctrl_0, 0)
#if DT_SAME_NODE(CONSOLE_NODE, DT_NODELABEL(uart0))
#define CONSOLE_UART_INDEX 0u
#elif DT_SAME_NODE(CONSOLE_NODE, DT_NODELABEL(uart1))
#define CONSOLE_UART_INDEX 1u
#else
#define CONSOLE_UART_INDEX ZF_GPIO_UNASSIGNED
#endif

LOG_MODULE_REGISTER(zf_board, LOG_LEVEL_INF);

K_MUTEX_DEFINE(app_lock);

static uint8_t uid[ZF_UID_SIZE];
static char serial[2 * ZF_UID_SIZE + 1];
static uint8_t reset_cause;
static zf_memory_inventory_t memory_inventory;

void zf_lock(void)
{
	(void)k_mutex_lock(&app_lock, K_FOREVER);
}

void zf_unlock(void)
{
	(void)k_mutex_unlock(&app_lock);
}

#ifdef CONFIG_SOC_FAMILY_RPI_PICO
static void read_memory_inventory(void)
{
	/* JEDEC Read ID returns the manufacturer, memory type and log2 of the capacity in bytes. */
	const uint8_t command[4] = {0x9Fu};
	uint8_t id[4] = {0};
	/* The command suspends XIP; no interrupt handler may execute from flash until it returns. */
	unsigned int key = irq_lock();

	flash_do_cmd(command, id, sizeof(id));
	irq_unlock(key);
	memory_inventory.flash_available = id[1] != 0x00u && id[1] != 0xFFu && id[3] >= 16u && id[3] <= 31u;
	if (memory_inventory.flash_available) {
		memory_inventory.flash_total_bytes = 1u << id[3];
	} else {
		LOG_ERR("Flash capacity read failed: JEDEC ID %02x %02x %02x", id[1], id[2], id[3]);
	}
}
#else
static void read_memory_inventory(void)
{
#if CONFIG_ESP_SPIRAM
	memory_inventory.psram_available = esp_psram_is_initialized();
	if (memory_inventory.psram_available) {
		memory_inventory.psram_total_bytes = esp_psram_get_size();
		memory_inventory.psram_reserved_bytes = (uintptr_t)_ext_ram_end - (uintptr_t)_ext_ram_start;
	} else {
		LOG_ERR("PSRAM inventory unavailable: external RAM is not initialized");
	}
#endif
	int err = esp_flash_get_physical_size(esp_flash_default_chip, &memory_inventory.flash_total_bytes);

	memory_inventory.flash_available = err == ESP_OK;
	if (err != ESP_OK) {
		memory_inventory.flash_total_bytes = 0;
		LOG_ERR("Module flash capacity read failed: %d", err);
	}
}
#endif

void zf_board_init(void)
{
	uint32_t cause = 0;
	ssize_t length = hwinfo_get_device_id(uid, UID_SOURCE_SIZE);

	if (length < 0) {
		memset(uid, 0, sizeof(uid));
	}
	zf_uid_hex(uid, serial);
	if (hwinfo_get_reset_cause(&cause) == 0) {
		reset_cause = zf_reset_cause(cause);
		(void)hwinfo_clear_reset_cause();
	}
	read_memory_inventory();
}

const uint8_t *zf_board_uid(void)
{
	return uid;
}

const char *zf_board_serial(void)
{
	return serial;
}

static void parse_version(uint8_t out[3])
{
	const char *text = ZEPHYRFORGE_VERSION;

	for (int i = 0; i < 3; ++i) {
		char *end;
		unsigned long part = strtoul(text, &end, 10);

		out[i] = part > 255 ? 255 : (uint8_t)part;
		text = *end == '.' ? end + 1 : end;
	}
}

void zf_identity_report(uint8_t *out)
{
	zf_identity_t identity = {
		.ram_total_bytes = RAM_TOTAL_BYTES,
		.ram_reserved_bytes = (uint32_t)((uintptr_t)_image_ram_end - (uintptr_t)_image_ram_start),
	};

	memcpy(identity.uid, uid, sizeof(uid));
	parse_version(identity.firmware);
	zf_report_identity(&identity, out);
}

void zf_memory_report(uint8_t *out)
{
	zf_report_memory(&memory_inventory, out);
}

void zf_config_report(uint8_t *out)
{
	zf_config_t config = {
		.reset_cause = reset_cause,
		.monitor_uart_index = CONSOLE_UART_INDEX,
		.monitor_uart_tx_gpio = GPIO_FROM_MUX(DT_CHILD(CONSOLE_PINCTRL, group1), 0),
		.monitor_uart_rx_gpio = GPIO_FROM_MUX(DT_CHILD(CONSOLE_PINCTRL, group2), 0),
		.storage_gpios = {ZF_GPIO_UNASSIGNED, ZF_GPIO_UNASSIGNED, ZF_GPIO_UNASSIGNED,
				  ZF_GPIO_UNASSIGNED, ZF_GPIO_UNASSIGNED, ZF_GPIO_UNASSIGNED},
#if ZF_ENABLE_STORAGE
		.card_detect_available = true,
		.card_detect_gpio = DT_GPIO_PIN(DT_PATH(zephyr_user), sd_detect_gpios),
		.msc_supported = IS_ENABLED(CONFIG_USBD_MSC_CLASS),
#endif
#if DT_NODE_EXISTS(DT_NODELABEL(zephyrforge_led_default))
		.led_available = true,
		.led_gpio = GPIO_FROM_MUX(DT_CHILD(DT_NODELABEL(zephyrforge_led_default), mosi), 0),
#endif
#if ZF_ENABLE_STORAGE
#if ZF_STORAGE_SPI
		.storage_gpios = {
			GPIO_FROM_MUX(DT_CHILD(SD_SPI_PINCTRL, clock), 0),
			GPIO_FROM_MUX(DT_CHILD(SD_SPI_PINCTRL, data), 0),
			GPIO_FROM_MUX(DT_CHILD(SD_SPI_PINCTRL, data), 1),
			DT_GPIO_PIN(DT_PARENT(DT_ALIAS(zephyrforge_sd)), cs_gpios),
			ZF_GPIO_UNASSIGNED, ZF_GPIO_UNASSIGNED,
		},
#else
		.storage_gpios = {
			GPIO_FROM_MUX(DT_CHILD(SD_PINCTRL, clock), 0),
			GPIO_FROM_MUX(DT_CHILD(SD_PINCTRL, command_data), 0),
			GPIO_FROM_MUX(DT_CHILD(SD_PINCTRL, command_data), 1),
			GPIO_FROM_MUX(DT_CHILD(SD_PINCTRL, command_data), 2),
			GPIO_FROM_MUX(DT_CHILD(SD_PINCTRL, command_data), 3),
			GPIO_FROM_MUX(DT_CHILD(SD_PINCTRL, command_data), 4),
		},
#endif
#endif
	};

	zf_lock();
	config.stored_clock_mhz = zf_sysclk_profile_mhz(zf_boot_config_sysclk());
	config.stored_radios = zf_boot_config_radios();
	zf_unlock();
	zf_report_config(&config, out);
}

static void reboot_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	sys_reboot(SYS_REBOOT_COLD);
}

K_WORK_DELAYABLE_DEFINE(reboot_work, reboot_handler);

int zf_platform_schedule_reboot(void)
{
	return k_work_reschedule(&reboot_work, K_MSEC(ZF_REBOOT_DELAY_MS)) < 0 ? -EIO : 0;
}
