/*
 * Copyright (c) 2025, 8BitGeist
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <esp32/rom/ets_sys.h>
#include "zephyr/types.h"
#include "tools/util.h"
#include "adapter/config.h"
#include "adapter/wired/wired.h"
#include "adapter/adapter.h"
#include "xadapter.h"

#define XADAPTER_JOY_MAX_AXES  4
#define XADAPTER_TRIG_MAX_AXES 2

enum {
    XADAPTER_UP = 0,
    XADAPTER_DOWN,
    XADAPTER_LEFT,
    XADAPTER_RIGHT,
    XADAPTER_A,
    XADAPTER_B,
    XADAPTER_X,
    XADAPTER_Y,
    XADAPTER_L3,
    XADAPTER_R3,
    XADAPTER_LB,
    XADAPTER_RB,
    XADAPTER_LT,
    XADAPTER_RT,
    XADAPTER_BACK,
    XADAPTER_START,
    XADAPTER_SYS,
    XADAPTER_MISC,
};

typedef struct __packed {
    uint32_t buttons;
    union {
        struct {
            int16_t joystick_lx;
            int16_t joystick_ly;
            int16_t joystick_rx;
            int16_t joystick_ry;
        };
        int16_t joysticks[XADAPTER_JOY_MAX_AXES];
    };
    union {
        struct {
            uint8_t trigger_l;
            uint8_t trigger_r;
        };
        uint8_t triggers[XADAPTER_TRIG_MAX_AXES];
    };
} xadapter_in_t;

typedef struct __packed {
    uint8_t rumble_l;
    uint8_t rumble_r;
} xadapter_out_t;

static DRAM_ATTR const struct ctrl_meta xadapter_axes_meta[ADAPTER_MAX_AXES] =
{
    {.size_min = -32768, .size_max = 32767, .neutral = 0, .abs_max = 0x7FFF, .abs_min = 0x8000},
    {.size_min = -32768, .size_max = 32767, .neutral = 0, .abs_max = 0x7FFF, .abs_min = 0x8000},
    {.size_min = -32768, .size_max = 32767, .neutral = 0, .abs_max = 0x7FFF, .abs_min = 0x8000},
    {.size_min = -32768, .size_max = 32767, .neutral = 0, .abs_max = 0x7FFF, .abs_min = 0x8000},
    {.size_min = 0, .size_max = 255, .neutral = 0, .abs_max = 255, .abs_min = 0},
    {.size_min = 0, .size_max = 255, .neutral = 0, .abs_max = 255, .abs_min = 0},
};

static DRAM_ATTR const uint8_t xadapter_joysticks_idx[XADAPTER_JOY_MAX_AXES] = {
/*  AXIS_LX, AXIS_LY, AXIS_RX, AXIS_RY */
    0,       1,       2,       3,
};

static DRAM_ATTR const uint8_t xadapter_trig_axes_idx[XADAPTER_TRIG_MAX_AXES] = {
/*  AXIS_L, AXIS_R  */
    0,       1,
};

static const uint32_t xadapter_mask[4] = {0xBBFF0FFF, 0x00000000, 0x00000000, BR_COMBO_MASK};
static const uint32_t xadapter_desc[4] = {0x110000FF, 0x00000000, 0x00000000, 0x00000000};
static DRAM_ATTR const uint32_t xadapter_btns_mask[32] = {
    0,                      0,                      0,                      0,
    0,                      0,                      0,                      0,
    BIT(XADAPTER_LEFT),     BIT(XADAPTER_RIGHT),    BIT(XADAPTER_DOWN),     BIT(XADAPTER_UP),
    0,                      0,                      0,                      0,
    BIT(XADAPTER_X),        BIT(XADAPTER_B),        BIT(XADAPTER_A),        BIT(XADAPTER_Y),
    BIT(XADAPTER_START),    BIT(XADAPTER_BACK),     BIT(XADAPTER_SYS),      BIT(XADAPTER_MISC),
    0,                      BIT(XADAPTER_RB),       0,                      BIT(XADAPTER_L3),
    0,                      BIT(XADAPTER_LB),       0,                      BIT(XADAPTER_R3),
};

void IRAM_ATTR xadapter_init_buffer(int32_t dev_mode, struct wired_data *wired_data) {
    memset(wired_data->output, 0, sizeof(wired_data->output));
    memset(wired_data->output_mask, 0x00, sizeof(wired_data->output_mask));
}

void xadapter_meta_init(struct wired_ctrl *ctrl_data) {
    memset((void *)ctrl_data, 0, sizeof(*ctrl_data)*WIRED_MAX_DEV);
    for (uint32_t i = 0; i < WIRED_MAX_DEV; i++) {
        for (uint32_t j = 0; j < ARRAY_SIZE(xadapter_axes_meta); j++) {
            ctrl_data[i].mask = xadapter_mask;
            ctrl_data[i].desc = xadapter_desc;
            ctrl_data[i].axes[j].meta = &xadapter_axes_meta[j];
        }
    }
}

void xadapter_from_generic(int32_t dev_mode, struct wired_ctrl *ctrl_data, struct wired_data *wired_data) {
    if (!ctrl_data || !wired_data) {
        return;
    }
    xadapter_in_t in_tmp = {0};
    // Buttons
    for (uint32_t i = 0; i < ARRAY_SIZE(generic_btns_mask); i++) {
        if (ctrl_data->map_mask[0] & BIT(i)) {
            if (ctrl_data->btns[0].value & generic_btns_mask[i]) {
                in_tmp.buttons |= xadapter_btns_mask[i];
                wired_data->cnt_mask[i] = ctrl_data->btns[0].cnt_mask[i];
            }
            else {
                in_tmp.buttons &= ~xadapter_btns_mask[i];
                wired_data->cnt_mask[i] = 0;
            }
        }
    }
    // Joysticks
    for (uint32_t i = 0; i < XADAPTER_JOY_MAX_AXES; i++) {
        if (ctrl_data->map_mask[0] & (axis_to_btn_mask(i) & ctrl_data->desc[0])) {
            if (ctrl_data->axes[i].value > ctrl_data->axes[i].meta->size_max) {
                in_tmp.joysticks[xadapter_joysticks_idx[i]] = xadapter_axes_meta[i].size_max;
            } else if (ctrl_data->axes[i].value < ctrl_data->axes[i].meta->size_min) {
                in_tmp.joysticks[xadapter_joysticks_idx[i]] = xadapter_axes_meta[i].size_min;
            } else {
                in_tmp.joysticks[xadapter_joysticks_idx[i]] = ctrl_data->axes[i].value;
            }
        }
        wired_data->cnt_mask[axis_to_btn_id(i)] = ctrl_data->axes[i].cnt_mask;
    }
    // Triggers
    for (uint32_t i = 0; i < XADAPTER_TRIG_MAX_AXES; i++) {
        if (ctrl_data->map_mask[0] & (axis_to_btn_mask(i + XADAPTER_JOY_MAX_AXES) & ctrl_data->desc[0])) {
            if (ctrl_data->axes[i + XADAPTER_JOY_MAX_AXES].value > ctrl_data->axes[i + XADAPTER_JOY_MAX_AXES].meta->size_max) {
                in_tmp.triggers[xadapter_trig_axes_idx[i]] = xadapter_axes_meta[i + XADAPTER_JOY_MAX_AXES].size_max;
            } else if (ctrl_data->axes[i + XADAPTER_JOY_MAX_AXES].value < ctrl_data->axes[i + XADAPTER_JOY_MAX_AXES].meta->size_min) {
                in_tmp.triggers[xadapter_trig_axes_idx[i]] = xadapter_axes_meta[i + XADAPTER_JOY_MAX_AXES].size_min;
            } else {
                in_tmp.triggers[xadapter_trig_axes_idx[i]] = ctrl_data->axes[i + XADAPTER_JOY_MAX_AXES].value;
            }
        }
        wired_data->cnt_mask[axis_to_btn_id(i + XADAPTER_JOY_MAX_AXES)] = ctrl_data->axes[i + XADAPTER_JOY_MAX_AXES].cnt_mask;
    }

    memcpy(wired_data->output, (void *)&in_tmp, sizeof(in_tmp));

#ifdef CONFIG_BLUERETRO_RAW_OUTPUT
    printf("{\"log_type\": \"wired_output\", \"axes\": [%d, %d, %d, %d], \"btns\": %lu, \"triggers\": [%d, %d]}\n",
        in_tmp.joysticks[xadapter_joysticks_idx[0]], in_tmp.joysticks[xadapter_joysticks_idx[1]], in_tmp.joysticks[xadapter_joysticks_idx[2]],
        in_tmp.joysticks[xadapter_joysticks_idx[3]], in_tmp.buttons, 
        in_tmp.triggers[xadapter_trig_axes_idx[0]], in_tmp.triggers[xadapter_trig_axes_idx[1]]);
#endif
}

void xadapter_fb_to_generic(int32_t dev_mode, struct raw_fb *raw_fb_data, struct generic_fb *fb_data) {
    fb_data->wired_id = raw_fb_data->header.wired_id;
    fb_data->type = raw_fb_data->header.type;
    switch (fb_data->type) {
        case FB_TYPE_RUMBLE:
            fb_data->state = ((raw_fb_data->data[0] || raw_fb_data->data[1]) ? 1 : 0);
            {
            uint16_t lf = *(uint16_t*)&raw_fb_data->data[0];
            uint16_t hf = *(uint16_t*)&raw_fb_data->data[2];
            fb_data->lf_pwr = (uint8_t)(((float)lf / (float)UINT16_MAX) * (float)UINT8_MAX);
            fb_data->hf_pwr = (uint8_t)(((float)hf / (float)UINT16_MAX) * (float)UINT8_MAX);
            }
            break;
        case FB_TYPE_STATUS_LED:
            break;
    }
}

void IRAM_ATTR xadapter_gen_turbo_mask(struct wired_data *wired_data) {
    // I don't understand this
}