// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

/*
 * Native SD host on one RP2040/RP2350 PIO block. The command state machine shifts command and response frames on CMD
 * and drives CLK from its side-set, also while idle. The data state machine moves DAT0-DAT3 nibbles through one DMA
 * channel. The command and receive programs fill the 32-instruction memory, so writes swap them for a free-running
 * clock and the transmit program, then restore them. The SD stack serializes all calls into this driver.
 */

#define DT_DRV_COMPAT zephyrforge_rpi_pico_pio_sdhc

#include "platform/rpi_pico/sdio_codec.h"

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/misc/pio_rpi_pico/pio_rpi_pico.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/sdhc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sd/sd_spec.h>
#include <zephyr/sys/barrier.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include <hardware/clocks.h>
#include <hardware/dma.h>
#include <hardware/gpio.h>
#include <hardware/pio.h>

#include "sdio_pio.pio.h"

LOG_MODULE_REGISTER(zf_sdhc_pio, CONFIG_SDHC_LOG_LEVEL);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1, "exactly one PIO SD host is supported");

#define CLK_PIN DT_INST_RPI_PICO_PIO_PIN_BY_NAME(0, default, 0, clock, 0)
#define CMD_PIN DT_INST_RPI_PICO_PIO_PIN_BY_NAME(0, default, 0, command_data, 0)
#define D0_PIN DT_INST_RPI_PICO_PIO_PIN_BY_NAME(0, default, 0, command_data, 1)
#define MAX_BUS_HZ DT_INST_PROP(0, max_bus_freq)

BUILD_ASSERT(DT_INST_RPI_PICO_PIO_PIN_BY_NAME(0, default, 0, command_data, 2) == D0_PIN + 1 &&
		     DT_INST_RPI_PICO_PIO_PIN_BY_NAME(0, default, 0, command_data, 3) == D0_PIN + 2 &&
		     DT_INST_RPI_PICO_PIO_PIN_BY_NAME(0, default, 0, command_data, 4) == D0_PIN + 3,
	     "DAT0-DAT3 must follow CMD on four consecutive GPIOs");
BUILD_ASSERT(D0_PIN + 3 < 32 && CMD_PIN < 32 && CLK_PIN == (D0_PIN + SDIO_CLK_PIN_D0_OFFSET) % 32,
	     "CLK must be the GPIO two below DAT0, and all bus GPIOs below GPIO32");
BUILD_ASSERT(CMD_PIN != CLK_PIN && (CMD_PIN < D0_PIN || CMD_PIN > D0_PIN + 3), "CMD needs its own GPIO");
BUILD_ASSERT(MAX_BUS_HZ <= ZF_SDIO_MAX_CLOCK_HZ, "max-bus-freq is at most 50 MHz");

/* PIO interrupt flags raised by the command, receive and transmit programs. */
#define CMD_IRQ 0u
#define RX_IRQ 1u
#define TX_IRQ 2u
#define INIT_CLOCK_HZ 400000u
/* Sixteen four-bit 512-byte receive images, or two transmit halves that each hold a one-bit 512-byte image. */
#define WIRE_WORDS 2080u
/* Spin this many 1 us polls before sleeping, so long card waits do not starve lower-priority threads. */
#define POLL_SPIN_US 200u
/* CRC status token as shifted in by the transmit program: three status bits and the end bit. */
#define TOKEN_MASK 0x0fu
#define TOKEN_ACCEPTED 0x05u
#define TOKEN_CRC_ERROR 0x0bu
/* R1 errors caused by the command just sent; ILLEGAL_CMD and COM_CRC_ERR describe the previous command. */
#define R1_ERRORS ((uint32_t)SD_R1_ERR_FLAGS & ~(uint32_t)(SD_R1_ILLEGAL_CMD | SD_R1_CRC_ERR))

/** @brief Runtime state of the PIO SD host. */
struct zf_sdhc_data {
	PIO pio;                        /**< PIO block owned by the host. */
	uint32_t system_hz;             /**< PIO input clock. */
	uint cmd_sm;                    /**< Command state machine; runs the clock program while transmitting. */
	uint data_sm;                   /**< Data receive or transmit state machine. */
	uint dma;                       /**< DMA channel between the data FIFOs and wire images. */
	const pio_program_t *loaded[2]; /**< Loaded command or clock program, then receive or transmit program. */
	uint offset[2];                 /**< Instruction offsets of the loaded programs. */
	uint32_t ticks;                 /**< Bus clock divider in 1/256 system-clock cycles. */
	uint32_t clock_hz;              /**< Bus clock produced by ticks. */
	uint8_t bus_width;              /**< Data lines in use: 1 or 4. */
	bool fast;                      /**< High Speed sampling and the cycle-locked transmit program are selected. */
	bool running;                   /**< The SD stack has enabled the bus clock. */
	bool hs_capable;                /**< The PIO clock can time High Speed at max-bus-freq. */
	bool high_capacity;             /**< The card takes block rather than byte addresses. */
	uint32_t wire[WIRE_WORDS];      /**< DMA wire images: one receive batch, or two transmit halves. */
};

static uint32_t sm_mask(const struct zf_sdhc_data *data)
{
	return BIT(data->cmd_sm) | BIT(data->data_sm);
}

static void clear_fdebug(PIO pio, uint sm)
{
	pio->fdebug = (BIT(PIO_FDEBUG_TXSTALL_LSB) | BIT(PIO_FDEBUG_TXOVER_LSB) | BIT(PIO_FDEBUG_RXUNDER_LSB) |
		       BIT(PIO_FDEBUG_RXSTALL_LSB))
		      << sm;
}

static k_timepoint_t deadline_after(int timeout_ms)
{
	return sys_timepoint_calc(timeout_ms < 0 ? K_FOREVER : K_MSEC(MAX(timeout_ms, 1)));
}

static void poll_wait(unsigned int *spins)
{
	if (*spins < POLL_SPIN_US) {
		++*spins;
		k_busy_wait(1);
	} else {
		k_sleep(K_TICKS(1));
	}
}

static int wait_d0_high(k_timepoint_t deadline)
{
	unsigned int spins = 0;

	while (!gpio_get(D0_PIN)) {
		if (sys_timepoint_expired(deadline)) {
			return -ETIMEDOUT;
		}
		poll_wait(&spins);
	}
	return 0;
}

/** @brief Stop both state machines and DMA, then free the loaded programs. Bus pin directions are unchanged. */
static void unload_programs(struct zf_sdhc_data *data)
{
	pio_set_sm_mask_enabled(data->pio, sm_mask(data), false);
	if (dma_channel_is_busy(data->dma)) {
		dma_channel_abort(data->dma);
	}
	for (size_t i = 0; i < ARRAY_SIZE(data->loaded); i++) {
		if (data->loaded[i] != NULL) {
			pio_remove_program(data->pio, data->loaded[i], data->offset[i]);
			data->loaded[i] = NULL;
		}
	}
}

static int load_programs(struct zf_sdhc_data *data, const pio_program_t *first, const pio_program_t *second)
{
	const pio_program_t *programs[] = {first, second};

	unload_programs(data);
	for (size_t i = 0; i < ARRAY_SIZE(programs); i++) {
		int offset = pio_add_program(data->pio, programs[i]);

		if (offset < 0) {
			unload_programs(data);
			return -ENOMEM;
		}
		data->loaded[i] = programs[i];
		data->offset[i] = (uint)offset;
	}
	return 0;
}

/** @brief Release CMD and DAT0-DAT3 to their pull-ups and drive CLK low. */
static void release_pins(struct zf_sdhc_data *data)
{
	pio_sm_set_consecutive_pindirs(data->pio, data->cmd_sm, CMD_PIN, 1, false);
	pio_sm_set_consecutive_pindirs(data->pio, data->data_sm, D0_PIN, 4, false);
	pio_sm_set_consecutive_pindirs(data->pio, data->cmd_sm, CLK_PIN, 1, true);
	pio_sm_exec(data->pio, data->cmd_sm, pio_encode_nop());
}

static int load_command(struct zf_sdhc_data *data)
{
	PIO pio = data->pio;
	pio_sm_config config;
	int ret = load_programs(data, &zf_sdio_pio_command_program,
				data->fast ? &zf_sdio_pio_data_rx_fast_program : &zf_sdio_pio_data_rx_program);

	if (ret != 0) {
		return ret;
	}
	config = zf_sdio_pio_command_program_get_default_config(data->offset[0]);
	sm_config_set_out_pins(&config, CMD_PIN, 1);
	sm_config_set_set_pins(&config, CMD_PIN, 1);
	sm_config_set_in_pins(&config, CMD_PIN);
	sm_config_set_jmp_pin(&config, CMD_PIN);
	sm_config_set_sideset_pins(&config, CLK_PIN);
	sm_config_set_out_shift(&config, false, true, 32);
	sm_config_set_in_shift(&config, false, true, 32);
	sm_config_set_mov_status(&config, STATUS_TX_LESSTHAN, 2);
	sm_config_set_clkdiv_int_frac8(&config, data->ticks >> 8, data->ticks & 0xffu);
	pio_sm_init(pio, data->cmd_sm, data->offset[0], &config);

	config = data->fast ? zf_sdio_pio_data_rx_fast_program_get_default_config(data->offset[1])
			    : zf_sdio_pio_data_rx_program_get_default_config(data->offset[1]);
	sm_config_set_in_pins(&config, D0_PIN);
	sm_config_set_jmp_pin(&config, D0_PIN);
	sm_config_set_in_shift(&config, false, true, 32);
	sm_config_set_out_shift(&config, false, false, 32);
	pio_sm_init(pio, data->data_sm, data->offset[1], &config);
	return 0;
}

/** @brief Return to idle command mode with the bus released; CLK runs only while the SD stack enables it. */
static int restore_command(struct zf_sdhc_data *data)
{
	int ret = load_command(data);

	release_pins(data);
	for (uint irq = CMD_IRQ; irq <= TX_IRQ; irq++) {
		pio_interrupt_clear(data->pio, irq);
	}
	if (ret == 0) {
		pio_clkdiv_restart_sm_mask(data->pio, sm_mask(data));
		pio_sm_set_enabled(data->pio, data->cmd_sm, data->running);
	}
	return ret;
}

/** @brief Swap in the free-running clock and the transmit program; both state machines stay disabled. */
static int load_transmit(struct zf_sdhc_data *data)
{
	PIO pio = data->pio;
	pio_sm_config config;
	int ret = load_programs(data, &zf_sdio_pio_clock_program,
				data->fast ? &zf_sdio_pio_data_tx_fast_program : &zf_sdio_pio_data_tx_program);

	if (ret != 0) {
		return ret;
	}
	config = zf_sdio_pio_clock_program_get_default_config(data->offset[0]);
	sm_config_set_sideset_pins(&config, CLK_PIN);
	sm_config_set_clkdiv_int_frac8(&config, data->ticks >> 8, data->ticks & 0xffu);
	pio_sm_init(pio, data->cmd_sm, data->offset[0], &config);
	pio_sm_set_consecutive_pindirs(pio, data->cmd_sm, CLK_PIN, 1, true);

	config = data->fast ? zf_sdio_pio_data_tx_fast_program_get_default_config(data->offset[1])
			    : zf_sdio_pio_data_tx_program_get_default_config(data->offset[1]);
	if (data->fast) {
		/* Cycle-locked to the clock program: four cycles per bus period. */
		sm_config_set_clkdiv_int_frac8(&config, data->ticks >> 9, (data->ticks >> 1) & 0xffu);
	}
	sm_config_set_out_pins(&config, D0_PIN, 4);
	sm_config_set_set_pins(&config, D0_PIN, 4);
	sm_config_set_in_pins(&config, D0_PIN);
	sm_config_set_out_shift(&config, false, true, 32);
	sm_config_set_in_shift(&config, false, false, 32);
	pio_sm_init(pio, data->data_sm, data->offset[1], &config);
	pio_clkdiv_restart_sm_mask(pio, sm_mask(data));
	return 0;
}

/** @brief Send one command frame and collect @p size response bytes, starting with the start bit. */
static int exchange(struct zf_sdhc_data *data, const uint8_t frame[ZF_SDIO_COMMAND_SIZE], uint8_t *response,
		    size_t size, k_timepoint_t deadline)
{
	PIO pio = data->pio;
	uint sm = data->cmd_sm;
	uint64_t bits = sys_get_be48(frame);
	size_t words = DIV_ROUND_UP(size, sizeof(uint32_t));
	size_t received = 0;
	unsigned int spins = 0;

	pio_sm_clear_fifos(pio, sm);
	pio_interrupt_clear(pio, CMD_IRQ);
	pio_sm_put(pio, sm, (47u << 26) | (uint32_t)(bits >> 22));
	pio_sm_put(pio, sm, (uint32_t)(bits & 0x3fffffu) << 10);
	pio_sm_put(pio, sm, size == 0 ? 0 : (uint32_t)(size * 8 - 2) << 24);
	for (;;) {
		/* The program pushes the last response word before raising its flag. */
		bool done = pio_interrupt_get(pio, CMD_IRQ);

		while (words > 0 && !pio_sm_is_rx_fifo_empty(pio, sm)) {
			uint32_t word = pio_sm_get(pio, sm);
			size_t bytes = MIN(size - received, sizeof(word));

			for (size_t i = 0; i < bytes; i++) {
				response[received + i] = (uint8_t)(word >> ((bytes - i - 1) * 8));
			}
			received += bytes;
			words--;
		}
		if (done && words == 0) {
			break;
		}
		if (sys_timepoint_expired(deadline)) {
			pio_sm_clear_fifos(pio, sm);
			pio_sm_restart(pio, sm);
			pio_sm_exec(pio, sm, pio_encode_jmp(data->offset[0]));
			return -ETIMEDOUT;
		}
		poll_wait(&spins);
	}
	pio_interrupt_clear(pio, CMD_IRQ);
	/* At least eight clocks before the next command. */
	k_busy_wait(DIV_ROUND_UP(8000000u, data->clock_hz));
	return 0;
}

/**
 * @brief Run one command and check its response frame; R1b also waits for the end of busy.
 * @return 0, -ETIMEDOUT when no response arrived, -EIO on a malformed response, or -ENOTSUP for SDIO responses.
 *         Card status errors are left to the caller.
 */
static int command(struct zf_sdhc_data *data, uint8_t opcode, uint32_t arg, uint32_t type, uint32_t response[4],
		   int timeout_ms, int busy_ms)
{
	uint8_t frame[ZF_SDIO_COMMAND_SIZE];
	uint8_t reply[ZF_SDIO_LONG_RESPONSE_SIZE];
	zf_sdio_response_t shape;
	size_t size;
	int ret;

	switch (type & SDHC_NATIVE_RESPONSE_MASK) {
	case SD_RSP_TYPE_NONE:
		shape = ZF_SDIO_RESPONSE_NONE;
		size = 0;
		break;
	case SD_RSP_TYPE_R1:
	case SD_RSP_TYPE_R1b:
	case SD_RSP_TYPE_R6:
	case SD_RSP_TYPE_R7:
		shape = ZF_SDIO_RESPONSE_SHORT;
		size = ZF_SDIO_SHORT_RESPONSE_SIZE;
		break;
	case SD_RSP_TYPE_R2:
		shape = ZF_SDIO_RESPONSE_LONG;
		size = ZF_SDIO_LONG_RESPONSE_SIZE;
		break;
	case SD_RSP_TYPE_R3:
		shape = ZF_SDIO_RESPONSE_OCR;
		size = ZF_SDIO_SHORT_RESPONSE_SIZE;
		break;
	default:
		return -ENOTSUP;
	}
	zf_sdio_build_command(opcode, arg, frame);
	ret = exchange(data, frame, reply, size, deadline_after(timeout_ms));
	if (ret == 0 && !zf_sdio_parse_response(shape, opcode, reply, response)) {
		ret = -EIO;
	}
	if (ret == 0 && (type & SDHC_NATIVE_RESPONSE_MASK) == SD_RSP_TYPE_R1b) {
		ret = wait_d0_high(deadline_after(busy_ms));
	}
	if (ret != 0) {
		LOG_DBG("CMD%u failed: %d", opcode, ret);
	}
	return ret;
}

/**
 * @brief End an open multi-block transfer with CMD12.
 *
 * A read that reaches the last card block may legitimately report OUT_OF_RANGE. Write errors are reported here
 * because the card clears them when it answers this status.
 */
static int stop_transmission(struct zf_sdhc_data *data, bool read, int timeout_ms, int busy_ms)
{
	uint32_t errors = R1_ERRORS & ~(read ? (uint32_t)SD_R1_OUT_OF_RANGE : 0u);
	uint32_t response[4];
	int ret = command(data, SD_STOP_TRANSMISSION, 0, SD_RSP_TYPE_R1b, response, timeout_ms, busy_ms);

	return ret == 0 && (response[0] & errors) != 0 ? -EIO : ret;
}

/** @brief Arm the receive program and DMA for @p blocks wire images; call before sending the read command. */
static void rx_start(struct zf_sdhc_data *data, uint32_t blocks, size_t image)
{
	PIO pio = data->pio;
	uint sm = data->data_sm;
	dma_channel_config dma = dma_channel_get_default_config(data->dma);

	pio_sm_set_enabled(pio, sm, false);
	clear_fdebug(pio, sm);
	pio_sm_clear_fifos(pio, sm);
	pio_interrupt_clear(pio, RX_IRQ);
	pio_sm_restart(pio, sm);
	pio_sm_exec(pio, sm, pio_encode_jmp(data->offset[1]));
	if (blocks > 1) {
		pio_sm_put(pio, sm, blocks - 1);
		pio_sm_exec(pio, sm, pio_encode_pull(false, true));
		pio_sm_exec(pio, sm, pio_encode_mov(pio_y, pio_osr));
	} else {
		pio_sm_exec(pio, sm, pio_encode_set(pio_y, 0));
	}
	pio_sm_put(pio, sm, image * 2 - 1);
	channel_config_set_transfer_data_size(&dma, DMA_SIZE_32);
	channel_config_set_bswap(&dma, true);
	channel_config_set_read_increment(&dma, false);
	channel_config_set_write_increment(&dma, true);
	channel_config_set_dreq(&dma, pio_get_dreq(pio, sm, false));
	dma_channel_configure(data->dma, &dma, data->wire, &pio->rxf[sm],
			      dma_encode_transfer_count(blocks * image / sizeof(uint32_t)), true);
	pio_sm_set_enabled(pio, sm, true);
}

/** @brief Decode received blocks as they land, until all @p blocks pass their data CRC. */
static int rx_finish(struct zf_sdhc_data *data, uint8_t *out, uint32_t blocks, size_t size, size_t image,
		     k_timepoint_t deadline)
{
	size_t block_words = image / sizeof(uint32_t);
	uint32_t decoded = 0;
	unsigned int spins = 0;

	for (;;) {
		bool complete = pio_interrupt_get(data->pio, RX_IRQ) && !dma_channel_is_busy(data->dma);
		size_t words = complete ? blocks * block_words
					: (dma_channel_hw_addr(data->dma)->write_addr - (uintptr_t)data->wire) /
						  sizeof(uint32_t);

		barrier_dmem_fence_full();
		for (; decoded < words / block_words; decoded++) {
			if (!zf_sdio_decode((const uint8_t *)&data->wire[decoded * block_words], out + decoded * size,
					    size, data->bus_width)) {
				LOG_DBG("read CRC error in block %u", decoded);
				return -EIO;
			}
		}
		if (complete) {
			return 0;
		}
		if ((data->pio->fdebug & BIT(PIO_FDEBUG_RXSTALL_LSB + data->data_sm)) != 0) {
			LOG_DBG("receive FIFO overflow");
			return -EIO;
		}
		if (sys_timepoint_expired(deadline)) {
			return -ETIMEDOUT;
		}
		poll_wait(&spins);
	}
}

static void rx_cleanup(struct zf_sdhc_data *data)
{
	PIO pio = data->pio;
	uint sm = data->data_sm;

	pio_sm_set_enabled(pio, sm, false);
	if (dma_channel_is_busy(data->dma)) {
		dma_channel_abort(data->dma);
	}
	clear_fdebug(pio, sm);
	pio_sm_clear_fifos(pio, sm);
	pio_interrupt_clear(pio, RX_IRQ);
	pio_sm_restart(pio, sm);
	pio_sm_exec(pio, sm, pio_encode_jmp(data->offset[1]));
}

/** @brief Start shifting one wire image out on DAT0-DAT3 while the clock program drives CLK. */
static void tx_start(struct zf_sdhc_data *data, const uint32_t *image, size_t words)
{
	PIO pio = data->pio;
	uint sm = data->data_sm;
	dma_channel_config dma = dma_channel_get_default_config(data->dma);

	clear_fdebug(pio, sm);
	pio_sm_clear_fifos(pio, sm);
	pio_interrupt_clear(pio, TX_IRQ);
	pio_sm_restart(pio, sm);
	pio_sm_exec(pio, sm, pio_encode_jmp(data->offset[1]));
	pio_sm_put(pio, sm, words * 8 - 1);
	channel_config_set_transfer_data_size(&dma, DMA_SIZE_32);
	channel_config_set_bswap(&dma, true);
	channel_config_set_read_increment(&dma, true);
	channel_config_set_write_increment(&dma, false);
	channel_config_set_dreq(&dma, pio_get_dreq(pio, sm, true));
	dma_channel_configure(data->dma, &dma, &pio->txf[sm], image, dma_encode_transfer_count(words), true);
	if (data->fast) {
		pio_sm_set_enabled(pio, data->cmd_sm, false);
		pio_sm_restart(pio, data->cmd_sm);
		pio_sm_restart(pio, sm);
		pio_sm_exec(pio, data->cmd_sm, pio_encode_jmp(data->offset[0]));
		pio_clkdiv_restart_sm_mask(pio, sm_mask(data));
		pio_enable_sm_mask_in_sync(pio, sm_mask(data));
	} else {
		pio_sm_set_enabled(pio, data->cmd_sm, true);
		pio_sm_set_enabled(pio, sm, true);
	}
}

/** @brief Wait for the CRC status token and the end of card busy, then check the token. */
static int tx_finish(struct zf_sdhc_data *data, k_timepoint_t deadline)
{
	PIO pio = data->pio;
	uint32_t stall = BIT(PIO_FDEBUG_TXSTALL_LSB + data->data_sm);
	unsigned int spins = 0;
	uint32_t token;
	int ret;

	for (;;) {
		if ((pio->fdebug & stall) != 0) {
			ret = -EIO;
			break;
		}
		if (pio_interrupt_get(pio, TX_IRQ) && !dma_channel_is_busy(data->dma)) {
			ret = (pio->fdebug & stall) != 0 ? -EIO : 0;
			break;
		}
		if (sys_timepoint_expired(deadline)) {
			ret = -ETIMEDOUT;
			break;
		}
		poll_wait(&spins);
	}
	pio_sm_set_enabled(pio, data->data_sm, false);
	if (dma_channel_is_busy(data->dma)) {
		dma_channel_abort(data->dma);
	}
	if (ret != 0) {
		LOG_DBG("write data phase failed: %d", ret);
		return ret;
	}
	if (pio_sm_is_rx_fifo_empty(pio, data->data_sm)) {
		return -EIO;
	}
	token = pio_sm_get(pio, data->data_sm) & TOKEN_MASK;
	if (token != TOKEN_ACCEPTED) {
		LOG_DBG("write rejected: %s", token == TOKEN_CRC_ERROR ? "CRC error" : "write error");
		return -EIO;
	}
	return 0;
}

/** @brief Read data blocks; CMD18 runs in batches that fit the wire buffer, each ended by CMD12. */
static int read_blocks(struct zf_sdhc_data *data, struct sdhc_command *cmd, struct sdhc_data *transfer)
{
	size_t size = transfer->block_size;
	size_t image = zf_sdio_image_size(size, data->bus_width);
	bool multi = cmd->opcode == SD_READ_MULTIPLE_BLOCK;
	uint8_t *out = transfer->data;
	uint32_t done = 0;

	if (image == 0 || transfer->blocks == 0 || (!multi && transfer->blocks != 1)) {
		return -ENOTSUP;
	}
	transfer->bytes_xfered = 0;
	while (done < transfer->blocks) {
		uint32_t count = MIN(transfer->blocks - done, sizeof(data->wire) / image);
		uint32_t arg = cmd->arg + (data->high_capacity ? done : done * size);
		int ret, stop;

		rx_start(data, count, image);
		ret = command(data, cmd->opcode, arg, cmd->response_type, cmd->response, cmd->timeout_ms,
			      cmd->timeout_ms);
		if (ret == 0 && (cmd->response[0] & R1_ERRORS) != 0) {
			ret = -EIO;
		}
		if (ret == 0) {
			ret = rx_finish(data, out + done * size, count, size, image,
					deadline_after(transfer->timeout_ms));
		}
		rx_cleanup(data);
		if (multi) {
			stop = stop_transmission(data, true, cmd->timeout_ms, transfer->timeout_ms);
			ret = ret != 0 ? ret : stop;
		}
		if (ret != 0) {
			return ret;
		}
		done += count;
		transfer->bytes_xfered = done * size;
	}
	return 0;
}

/**
 * @brief Write data blocks, encoding the next block while the current one is on the bus.
 *
 * CMD25 always ends with CMD12. A failed CMD24 also sends CMD12, ignoring its result, in case the card is still
 * waiting for data.
 */
static int write_blocks(struct zf_sdhc_data *data, struct sdhc_command *cmd, struct sdhc_data *transfer)
{
	size_t size = transfer->block_size;
	size_t image = zf_sdio_image_size(size, data->bus_width);
	size_t half = ARRAY_SIZE(data->wire) / 2;
	bool multi = cmd->opcode == SD_WRITE_MULTIPLE_BLOCK;
	const uint8_t *in = transfer->data;
	int ret, stop;

	if (image == 0 || image > half * sizeof(uint32_t) || transfer->blocks == 0 ||
	    (!multi && transfer->blocks != 1)) {
		return -ENOTSUP;
	}
	transfer->bytes_xfered = 0;
	zf_sdio_encode(in, size, data->bus_width, (uint8_t *)data->wire);
	ret = command(data, cmd->opcode, cmd->arg, cmd->response_type, cmd->response, cmd->timeout_ms,
		      cmd->timeout_ms);
	if (ret == 0 && (cmd->response[0] & R1_ERRORS) != 0) {
		ret = -EIO;
	}
	if (ret == 0) {
		ret = load_transmit(data);
		for (uint32_t i = 0; ret == 0 && i < transfer->blocks; i++) {
			tx_start(data, &data->wire[(i & 1) * half], image / sizeof(uint32_t));
			if (i + 1 < transfer->blocks) {
				zf_sdio_encode(in + (i + 1) * size, size, data->bus_width,
					       (uint8_t *)&data->wire[((i + 1) & 1) * half]);
			}
			ret = tx_finish(data, deadline_after(transfer->timeout_ms));
			if (ret == 0) {
				transfer->bytes_xfered = (i + 1) * size;
			}
		}
		stop = restore_command(data);
		if (stop != 0) {
			return ret != 0 ? ret : stop;
		}
	}
	if (multi || ret != 0) {
		stop = stop_transmission(data, false, cmd->timeout_ms, transfer->timeout_ms);
		ret = ret != 0 ? ret : stop;
	}
	return ret;
}

static int zf_sdhc_request(const struct device *dev, struct sdhc_command *cmd, struct sdhc_data *transfer)
{
	struct zf_sdhc_data *data = dev->data;
	/* Same attempt counts as Zephyr's SPI host: data retries count attempts, command retries are extra. */
	unsigned int attempts = transfer == NULL ? cmd->retries + 1 : MAX(cmd->retries, 1u);
	int ret;

	if (!data->running) {
		return -EIO;
	}
	if (cmd->opcode == SD_GO_IDLE_STATE) {
		data->high_capacity = false;
	}
	do {
		ret = data->loaded[0] == &zf_sdio_pio_command_program ? 0 : restore_command(data);
		if (ret != 0) {
			break;
		}
		if (transfer == NULL) {
			ret = command(data, cmd->opcode, cmd->arg, cmd->response_type, cmd->response, cmd->timeout_ms,
				      cmd->timeout_ms);
		} else if (cmd->opcode == SD_WRITE_SINGLE_BLOCK || cmd->opcode == SD_WRITE_MULTIPLE_BLOCK) {
			ret = write_blocks(data, cmd, transfer);
		} else {
			ret = read_blocks(data, cmd, transfer);
		}
	} while (ret != 0 && --attempts > 0);

	if (ret == 0 && cmd->opcode == SD_APP_SEND_OP_COND && (cmd->response[0] & SD_OCR_PWR_BUSY_FLAG) != 0) {
		data->high_capacity = (cmd->response[0] & SD_OCR_CARD_CAP_FLAG) != 0;
	}
	return ret;
}

static int zf_sdhc_set_io(const struct device *dev, struct sdhc_io *ios)
{
	struct zf_sdhc_data *data = dev->data;
	bool fast = ios->timing == SDHC_TIMING_HS;
	uint32_t limit = fast ? ZF_SDIO_MAX_CLOCK_HZ : ZF_SDIO_DEFAULT_SPEED_MAX_HZ;
	uint32_t ticks, hz;

	if ((ios->bus_width != SDHC_BUS_WIDTH1BIT && ios->bus_width != SDHC_BUS_WIDTH4BIT) ||
	    (!fast && ios->timing != SDHC_TIMING_LEGACY) || ios->signal_voltage != SD_VOL_3_3_V) {
		return -ENOTSUP;
	}
	if (ios->clock == 0) {
		data->running = false;
		pio_sm_set_enabled(data->pio, data->cmd_sm, false);
		pio_sm_exec(data->pio, data->cmd_sm, pio_encode_nop());
		return 0;
	}
	if (!zf_sdio_plan_clock(data->system_hz, MIN(MIN((uint32_t)ios->clock, MAX_BUS_HZ), limit), fast, &ticks,
				&hz)) {
		return -ENOTSUP;
	}
	data->bus_width = ios->bus_width;
	data->fast = fast;
	data->ticks = ticks;
	data->clock_hz = hz;
	data->running = true;
	LOG_DBG("bus %u Hz %s, %u-bit", hz, fast ? "HS" : "DS", data->bus_width);
	return restore_command(data);
}

static int zf_sdhc_get_card_present(const struct device *dev)
{
	ARG_UNUSED(dev);
	/* Card detect is wired to a GPIO that the storage service watches. */
	return 1;
}

static int zf_sdhc_card_busy(const struct device *dev)
{
	ARG_UNUSED(dev);
	return gpio_get(D0_PIN) ? 0 : 1;
}

static int zf_sdhc_get_host_props(const struct device *dev, struct sdhc_host_props *props)
{
	const struct zf_sdhc_data *data = dev->data;

	memset(props, 0, sizeof(*props));
	props->f_max = MAX_BUS_HZ;
	props->f_min = DT_INST_PROP(0, min_bus_freq);
	props->power_delay = DT_INST_PROP(0, power_delay_ms);
	props->host_caps.vol_330_support = true;
	props->host_caps.bus_4_bit_support = true;
	/* Zephyr widens the bus to four bits only after switching the card to High Speed. */
	props->host_caps.high_spd_support = data->hs_capable;
	return 0;
}

PINCTRL_DT_INST_DEFINE(0);

static int zf_sdhc_init(const struct device *dev)
{
	const struct device *piodev = DEVICE_DT_GET(DT_INST_PARENT(0));
	struct zf_sdhc_data *data = dev->data;
	uint32_t ticks, hz;
	size_t sm[2];
	int ret;

	if (!device_is_ready(piodev)) {
		return -ENODEV;
	}
	data->pio = pio_rpi_pico_get_pio(piodev);
	for (size_t i = 0; i < ARRAY_SIZE(sm); i++) {
		ret = pio_rpi_pico_allocate_sm(piodev, &sm[i]);
		if (ret != 0) {
			return ret;
		}
	}
	data->cmd_sm = sm[0];
	data->data_sm = sm[1];
	ret = dma_claim_unused_channel(false);
	if (ret < 0) {
		return -EBUSY;
	}
	data->dma = (uint)ret;
	ret = pinctrl_apply_state(PINCTRL_DT_INST_DEV_CONFIG_GET(0), PINCTRL_STATE_DEFAULT);
	if (ret != 0) {
		return ret;
	}
	/* The programs sample CLK and data edges directly; synchronizer latency would eat High Speed margins. */
	data->pio->input_sync_bypass |= BIT(CLK_PIN) | BIT(CMD_PIN) | (0xfu << D0_PIN);
	data->system_hz = clock_get_hz(clk_sys);
	if (!zf_sdio_plan_clock(data->system_hz, INIT_CLOCK_HZ, false, &data->ticks, &data->clock_hz)) {
		return -EINVAL;
	}
	data->hs_capable = MAX_BUS_HZ > ZF_SDIO_DEFAULT_SPEED_MAX_HZ &&
			   zf_sdio_plan_clock(data->system_hz, MAX_BUS_HZ, true, &ticks, &hz);
	data->bus_width = SDHC_BUS_WIDTH1BIT;
	return restore_command(data);
}

static DEVICE_API(sdhc, zf_sdhc_api) = {
	.request = zf_sdhc_request,
	.set_io = zf_sdhc_set_io,
	.get_card_present = zf_sdhc_get_card_present,
	.card_busy = zf_sdhc_card_busy,
	.get_host_props = zf_sdhc_get_host_props,
};

static struct zf_sdhc_data zf_sdhc_data_0;

DEVICE_DT_INST_DEFINE(0, zf_sdhc_init, NULL, &zf_sdhc_data_0, NULL, POST_KERNEL, CONFIG_SDHC_INIT_PRIORITY,
		      &zf_sdhc_api);
