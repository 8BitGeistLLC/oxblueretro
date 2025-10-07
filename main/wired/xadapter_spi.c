/*
 * Copyright (c) 2025, 8BitGeist
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sdkconfig.h"
#if defined(CONFIG_BLUERETRO_SYSTEM_XADAPTER)
#include <string.h>
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
#include "adapter/wired/ps.h"
#include "wired_bare.h"
#include "wired/xadapter_spi.h"
#include "hal/spi_slave_hal.h"
#include "hal/uart_ll.h"
#include "esp_heap_caps.h"
#include "esp_log_timestamp.h"

#define SPI3_VSPI_INTR_NUM  20
#define GPIO_INTR_NUM       21

#define SPI3_VSPI_INTR_MASK (1 << SPI3_VSPI_INTR_NUM)
#define GPIO_INTR_MASK      (1 << GPIO_INTR_NUM)

#define XADAPTER_PACKET_SIZE  32

#ifdef CONFIG_BLUERETRO_ESP32_MINI
#define XADAPTER_SPI_MISO_PIN 19
#define XADAPTER_SPI_MOSI_PIN 23
#define XADAPTER_SPI_SCK_PIN  18
#define XADAPTER_SPI_CSN_PIN  5
#else
#define XADAPTER_SPI_CSN_PIN  5
#define XADAPTER_SPI_SCK_PIN  18
#define XADAPTER_SPI_MISO_PIN 19
#define XADAPTER_SPI_MOSI_PIN 23
#endif

typedef enum {
    XADAPTER_CMD_GAMEPAD = 2,
    XADAPTER_CMD_RUMBLE,
} xadapter_cmd_t;

typedef struct __packed {
    uint16_t fw_magic;
    uint8_t cmd;
    uint8_t spi_magic;
    uint8_t payload[28];
} xadapter_packet_t;

typedef struct {
    uint8_t id;
    spi_slave_hal_context_t ctx;
    uint8_t tx_buf[2][XADAPTER_PACKET_SIZE];
    uint8_t rx_buf[2][XADAPTER_PACKET_SIZE];
    volatile uint8_t xfer_idx;
    struct raw_fb feedback;
} xadapter_spi_t;

static xadapter_spi_t xadapter = {0};

static void xadapter_spi_queue_xfer(void) {
    spi_slave_hal_context_t *ctx = &xadapter.ctx;
    spi_ll_infifo_full_clr(ctx->hw);
    spi_ll_outfifo_empty_clr(ctx->hw);
    spi_ll_write_buffer(ctx->hw, ctx->tx_buffer, ctx->bitlen);
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
        spi_ll_read_buffer(ctx->hw, ctx->rx_buffer, (ctx->rcv_bitlen > ctx->bitlen) ? ctx->bitlen : ctx->rcv_bitlen);
    }

    uint32_t bytes = (ctx->rcv_bitlen + 7) / 8;
    uint8_t cmplt_idx = xadapter.xfer_idx;
    xadapter.xfer_idx ^= 1;
    ctx->tx_buffer = xadapter.tx_buf[xadapter.xfer_idx];
    ctx->rx_buffer = xadapter.rx_buf[xadapter.xfer_idx];
    xadapter_spi_queue_xfer();

    memset(xadapter.tx_buf[cmplt_idx], 0, XADAPTER_PACKET_SIZE);
    xadapter_packet_t* tx_pkt = (xadapter_packet_t*)xadapter.tx_buf[cmplt_idx];
    tx_pkt->fw_magic = FIRMWARE_MAGIC;
    tx_pkt->cmd = XADAPTER_CMD_GAMEPAD;
    tx_pkt->spi_magic = SPI_MAGIC;
    memcpy(tx_pkt->payload, wired_adapter.data[xadapter.id].output, sizeof(tx_pkt->payload));
    tx_pkt->payload[sizeof(tx_pkt->payload) - 2] = 0xDE;
    tx_pkt->payload[sizeof(tx_pkt->payload) - 1] = 0xAD;

    if (bytes != XADAPTER_PACKET_SIZE) {
        ets_printf("XAdapter SPI invalid len %d\r\n", bytes);
        return 0;
    }
    const xadapter_packet_t* rx_pkt = (const xadapter_packet_t*)xadapter.rx_buf[cmplt_idx];
    if (rx_pkt->spi_magic != SPI_MAGIC) {
        ets_printf("XAdapter SPI invalid SPI magic 0x%02X\r\n", rx_pkt->spi_magic);
        return 0;
    }
    if (rx_pkt->cmd == XADAPTER_CMD_RUMBLE) {
        memset(&xadapter.feedback, 0, sizeof(xadapter.feedback));
        xadapter.feedback.header.wired_id = xadapter.id;
        xadapter.feedback.header.type = FB_TYPE_RUMBLE;
        xadapter.feedback.header.data_len = 2 * sizeof(uint16_t);
        memcpy(xadapter.feedback.data, rx_pkt->payload, 2 * sizeof(uint16_t));
        adapter_q_fb(&xadapter.feedback);
    }
    memset(xadapter.rx_buf[cmplt_idx], 0, XADAPTER_PACKET_SIZE);
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
    ctx->bitlen = XADAPTER_PACKET_SIZE * 8;
    ctx->tx_buffer = xadapter.tx_buf[xadapter.xfer_idx];
    ctx->rx_buffer = xadapter.rx_buf[xadapter.xfer_idx];
    
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