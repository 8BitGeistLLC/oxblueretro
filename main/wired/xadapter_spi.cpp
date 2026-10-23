/*
 * Copyright (c) 2025, 8BitGeist
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sdkconfig.h"
#include <stdint.h>
#include "wired/xadapter_spi.h"

#if defined(CONFIG_BLUERETRO_SYSTEM_XADAPTER)
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "gamepad/gamepad.hpp"
#include "gamepad/protocol.hpp"

extern "C" {
#include "soc/io_mux_reg.h"
#include "esp_private/periph_ctrl.h"
#include <soc/spi_periph.h>
#include <esp32/rom/ets_sys.h>
#include <esp32/rom/gpio.h>
#include "hal/clk_gate_ll.h"
#include "driver/gpio.h"
#include "system/intr.h"
#include "system/gpio.h"
#include "system/delay.h"
#include "system/led.h"
#include "zephyr/atomic.h"
#include "zephyr/types.h"
#include "tools/util.h"
#include "adapter/adapter.h"
#include "adapter/config.h"
#include "adapter/memory_card.h"
#include "adapter/kb_monitor.h"
#include "wired_bare.h"
#include "hal/spi_slave_hal.h"
#include "hal/uart_ll.h"
#include "esp_heap_caps.h"
#include "esp_log_timestamp.h"
}

#define SPI3_VSPI_INTR_NUM  20
#define GPIO_INTR_NUM       21

#define SPI3_VSPI_INTR_MASK (1 << SPI3_VSPI_INTR_NUM)
#define GPIO_INTR_MASK      (1 << GPIO_INTR_NUM)

static constexpr std::size_t XADAPTER_PACKET_SIZE = SpiProtocol::FrameSize;

#ifdef CONFIG_BLUERETRO_ESP32_MINI
static constexpr gpio_num_t XADAPTER_SPI_MISO_PIN = GPIO_NUM_19;
static constexpr gpio_num_t XADAPTER_SPI_MOSI_PIN = GPIO_NUM_23;
static constexpr gpio_num_t XADAPTER_SPI_SCK_PIN = GPIO_NUM_18;
static constexpr gpio_num_t XADAPTER_SPI_CSN_PIN = GPIO_NUM_5;
#else
static constexpr gpio_num_t XADAPTER_SPI_CSN_PIN = GPIO_NUM_5;
static constexpr gpio_num_t XADAPTER_SPI_SCK_PIN = GPIO_NUM_18;
static constexpr gpio_num_t XADAPTER_SPI_MISO_PIN = GPIO_NUM_19;
static constexpr gpio_num_t XADAPTER_SPI_MOSI_PIN = GPIO_NUM_23;
#endif

struct xadapter_spi_t {
    uint8_t id;
    spi_slave_hal_context_t ctx;
    uint8_t tx_buf[2][XADAPTER_PACKET_SIZE];
    uint8_t rx_buf[2][XADAPTER_PACKET_SIZE];
    volatile uint8_t xfer_idx;
    struct raw_fb feedback;
    uint16_t tx_sequence;
    GamepadOut last_rumble;
    bool rumble_initialized;
};

static xadapter_spi_t xadapter{};

static void xadapter_spi_prepare_tx(uint8_t* buffer) {
    GamepadIn state{};
    std::memcpy(&state, wired_adapter.data[xadapter.id].output, sizeof(state));

    SpiProtocol::Frame frame{};
    SpiProtocol::encodeGamepadState(state, xadapter.tx_sequence++, frame);
    std::memcpy(buffer, frame.bytes, sizeof(frame.bytes));
}

static void xadapter_spi_queue_xfer(void) {
    spi_slave_hal_context_t *ctx = &xadapter.ctx;
    spi_ll_infifo_full_clr(ctx->hw);
    spi_ll_outfifo_empty_clr(ctx->hw);
    spi_ll_write_buffer(ctx->hw, static_cast<const uint8_t *>(ctx->tx_buffer), ctx->bitlen);
    spi_ll_slave_reset(ctx->hw);
    spi_ll_clear_int_stat(ctx->hw);
    spi_ll_user_start(ctx->hw);
}

static unsigned xadapter_spi_irq(unsigned cause) {
    if (!(cause & SPI3_VSPI_INTR_MASK)) {
        return 0;
    }
    spi_slave_hal_context_t *ctx = &xadapter.ctx;
    if (!spi_ll_usr_is_done(ctx->hw)) {
        return 0;
    }
    spi_ll_clear_int_stat(ctx->hw);

    ctx->rcv_bitlen = spi_ll_slave_get_rcv_bitlen(ctx->hw);
    if (ctx->rcv_bitlen == (ctx->bitlen - 1)) {
        ctx->rcv_bitlen++;
    }
    if (!ctx->use_dma && ctx->rx_buffer) {
        spi_ll_read_buffer(ctx->hw, static_cast<uint8_t *>(ctx->rx_buffer),
            (ctx->rcv_bitlen > ctx->bitlen) ? ctx->bitlen : ctx->rcv_bitlen);
    }

    uint32_t bytes = (ctx->rcv_bitlen + 7) / 8;
    uint8_t cmplt_idx = xadapter.xfer_idx;
    xadapter.xfer_idx ^= 1;
    ctx->tx_buffer = xadapter.tx_buf[xadapter.xfer_idx];
    ctx->rx_buffer = xadapter.rx_buf[xadapter.xfer_idx];
    xadapter_spi_queue_xfer();

    xadapter_spi_prepare_tx(xadapter.tx_buf[cmplt_idx]);

    if (bytes != XADAPTER_PACKET_SIZE) {
        ets_printf("XAdapter SPI invalid len %u\r\n", static_cast<unsigned>(bytes));
        return 0;
    }

    SpiProtocol::Frame rx_frame{};
    std::memcpy(rx_frame.bytes, xadapter.rx_buf[cmplt_idx], sizeof(rx_frame.bytes));
    GamepadOut rumble{};
    uint16_t sequence = 0;
    const SpiProtocol::FrameError frame_error = SpiProtocol::decodeRumble(rx_frame, rumble, sequence);
    if (frame_error != SpiProtocol::FrameError::None) {
        ets_printf("XAdapter SPI invalid rumble frame (%u)\r\n", static_cast<unsigned>(frame_error));
        return 0;
    }

    if (!xadapter.rumble_initialized
        || rumble.Left != xadapter.last_rumble.Left
        || rumble.Right != xadapter.last_rumble.Right) {
        std::memset(&xadapter.feedback, 0, sizeof(xadapter.feedback));
        xadapter.feedback.header.wired_id = xadapter.id;
        xadapter.feedback.header.type = FB_TYPE_RUMBLE;
        xadapter.feedback.header.data_len = sizeof(GamepadOut);
        SpiProtocol::writeU16(&xadapter.feedback.data[0], rumble.Left);
        SpiProtocol::writeU16(&xadapter.feedback.data[2], rumble.Right);
        adapter_q_fb(&xadapter.feedback);
        xadapter.last_rumble = rumble;
        xadapter.rumble_initialized = true;
    }

    std::memset(xadapter.rx_buf[cmplt_idx], 0, XADAPTER_PACKET_SIZE);
    return 0;
}

void xadapter_spi_init(uint32_t package) {
    (void)package;
    config_set_rst_bare_core(true);

    /* Setup SPI */
    /* CS */
    gpio_set_pull_mode_iram(XADAPTER_SPI_CSN_PIN, GPIO_PULLUP_ONLY);
    gpio_set_direction_iram(XADAPTER_SPI_CSN_PIN, GPIO_MODE_INPUT);
    PIN_FUNC_SELECT(GPIO_PIN_MUX_REG_IRAM[XADAPTER_SPI_CSN_PIN], PIN_FUNC_GPIO);
    gpio_matrix_in(XADAPTER_SPI_CSN_PIN, VSPICS0_IN_IDX, false);

    /* SCK */
    gpio_set_pull_mode_iram(XADAPTER_SPI_SCK_PIN, GPIO_PULLUP_ONLY);
    gpio_set_direction_iram(XADAPTER_SPI_SCK_PIN, GPIO_MODE_INPUT);
    PIN_FUNC_SELECT(GPIO_PIN_MUX_REG_IRAM[XADAPTER_SPI_SCK_PIN], PIN_FUNC_GPIO);
    gpio_matrix_in(XADAPTER_SPI_SCK_PIN, VSPICLK_IN_IDX, false);

    /* MOSI */
    gpio_set_pull_mode_iram(XADAPTER_SPI_MOSI_PIN, GPIO_PULLUP_ONLY);
    gpio_set_direction_iram(XADAPTER_SPI_MOSI_PIN, GPIO_MODE_INPUT);
    PIN_FUNC_SELECT(GPIO_PIN_MUX_REG_IRAM[XADAPTER_SPI_MOSI_PIN], PIN_FUNC_GPIO);
    gpio_matrix_in(XADAPTER_SPI_MOSI_PIN, VSPID_IN_IDX, false);

    /* MISO */
    gpio_set_direction_iram(XADAPTER_SPI_MISO_PIN, GPIO_MODE_OUTPUT);
    PIN_FUNC_SELECT(GPIO_PIN_MUX_REG_IRAM[XADAPTER_SPI_MISO_PIN], PIN_FUNC_GPIO);
    gpio_matrix_out(XADAPTER_SPI_MISO_PIN, VSPIQ_OUT_IDX, false, false);

    /* Peripheral clock */
    periph_ll_enable_clk_clear_rst(PERIPH_VSPI_MODULE);

    /* ctx init */
    spi_slave_hal_context_t *ctx = &xadapter.ctx;
    ctx->hw = &SPI3;
    ctx->use_dma = false;
    ctx->rx_lsbfirst = false;
    ctx->tx_lsbfirst = false;
    ctx->mode = 0;

    spi_ll_slave_init(ctx->hw);
    spi_ll_disable_int(ctx->hw);
    spi_ll_clear_int_stat(ctx->hw);
    spi_ll_set_rx_lsbfirst(ctx->hw, ctx->rx_lsbfirst);
    spi_ll_set_tx_lsbfirst(ctx->hw, ctx->tx_lsbfirst);
    spi_ll_slave_set_mode(ctx->hw, ctx->mode, ctx->use_dma);

    xadapter.xfer_idx = 0;
    xadapter.tx_sequence = 0;
    xadapter.rumble_initialized = false;
    xadapter.last_rumble = {};
    ctx->bitlen = XADAPTER_PACKET_SIZE * 8;
    ctx->tx_buffer = xadapter.tx_buf[xadapter.xfer_idx];
    ctx->rx_buffer = xadapter.rx_buf[xadapter.xfer_idx];
    xadapter_spi_prepare_tx(xadapter.tx_buf[xadapter.xfer_idx]);
    xadapter_spi_prepare_tx(xadapter.tx_buf[xadapter.xfer_idx ^ 1]);

    spi_ll_dma_rx_fifo_reset(ctx->hw);
    spi_ll_infifo_full_clr(ctx->hw);
    spi_ll_dma_rx_enable(ctx->hw, ctx->use_dma);
    spi_ll_dma_tx_fifo_reset(ctx->hw);
    spi_ll_outfifo_empty_clr(ctx->hw);
    spi_ll_dma_tx_enable(ctx->hw, ctx->use_dma);
    spi_ll_slave_set_rx_bitlen(ctx->hw, ctx->bitlen);
    spi_ll_slave_set_tx_bitlen(ctx->hw, ctx->bitlen);
    spi_ll_enable_mosi(ctx->hw, true);
    spi_ll_enable_miso(ctx->hw, true);

    xadapter_spi_port_cfg(1);

    intexc_alloc_iram(ETS_SPI3_INTR_SOURCE, SPI3_VSPI_INTR_NUM, xadapter_spi_irq);
}

void xadapter_spi_port_cfg(uint16_t mask) {
    spi_slave_hal_context_t *ctx = &xadapter.ctx;
    if (mask) {
        spi_ll_clear_int_stat(ctx->hw);
        spi_ll_enable_int(ctx->hw);
        xadapter_spi_queue_xfer();
    } else {
        spi_ll_disable_int(ctx->hw);
        spi_ll_clear_int_stat(ctx->hw);
    }
}

#else // CONFIG_BLUERETRO_SYSTEM_XADAPTER

void xadapter_spi_init(uint32_t package) {
    (void)package;
}

void xadapter_spi_port_cfg(uint16_t mask) {
    (void)mask;
}

#endif // CONFIG_BLUERETRO_SYSTEM_XADAPTER
