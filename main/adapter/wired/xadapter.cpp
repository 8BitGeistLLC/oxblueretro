/*
 * Copyright (c) 2025, 8BitGeist
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "gamepad/gamepad.hpp"
#include "gamepad/protocol.hpp"

extern "C" {
#include <esp32/rom/ets_sys.h>
#include "zephyr/types.h"
#include "tools/util.h"
#include "adapter/config.h"
#include "adapter/wired/wired.h"
#include "adapter/adapter.h"
}

#include "xadapter.h"

#define XADAPTER_JOY_MAX_AXES  4
#define XADAPTER_TRIG_MAX_AXES 2
static void xadapter_set_joy_axis(GamepadIn& state, uint32_t axis, int16_t value) {
    switch (axis) {
    case 0: state.JoyLx = value; break;
    case 1: state.JoyLy = value; break;
    case 2: state.JoyRx = value; break;
    case 3: state.JoyRy = value; break;
    }
}

static DRAM_ATTR const struct ctrl_meta xadapter_axes_meta[ADAPTER_MAX_AXES] =
{
    {0, 0, 0x7FFF, 0x8000, 0, -32768, 32767, 0},
    {0, 0, 0x7FFF, 0x8000, 0, -32768, 32767, 0},
    {0, 0, 0x7FFF, 0x8000, 0, -32768, 32767, 0},
    {0, 0, 0x7FFF, 0x8000, 0, -32768, 32767, 0},
    {0, 0, 255, 0, 0, 0, 255, 0},
    {0, 0, 255, 0, 0, 0, 255, 0},
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
static DRAM_ATTR const uint16_t xadapter_btns_mask[32] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    GamepadButton::DpadLeft, GamepadButton::DpadRight, GamepadButton::DpadDown, GamepadButton::DpadUp,
    0, 0, 0, 0,
    0, 0, 0, 0,
    GamepadButton::Start, GamepadButton::Back, 0, 0,
    0, 0, 0, GamepadButton::L3,
    0, 0, 0, GamepadButton::R3,
};

static DRAM_ATTR const int8_t xadapter_analog_btn_idx[32] = {
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    static_cast<int8_t>(GamepadAnalog::X), static_cast<int8_t>(GamepadAnalog::B),
    static_cast<int8_t>(GamepadAnalog::A), static_cast<int8_t>(GamepadAnalog::Y),
    -1, -1, -1, -1,
    -1, static_cast<int8_t>(GamepadAnalog::White), -1, -1,
    -1, static_cast<int8_t>(GamepadAnalog::Black), -1, -1,
};

void IRAM_ATTR xadapter_init_buffer(int32_t dev_mode, struct wired_data *wired_data) {
    std::memset(wired_data->output, 0, sizeof(wired_data->output));
    std::memset(wired_data->output_mask, 0x00, sizeof(wired_data->output_mask));
}

void xadapter_meta_init(struct wired_ctrl *ctrl_data) {
    std::memset(static_cast<void *>(ctrl_data), 0, sizeof(*ctrl_data) * WIRED_MAX_DEV);
    for (uint32_t i = 0; i < WIRED_MAX_DEV; i++) {
        for (uint32_t j = 0; j < sizeof(xadapter_axes_meta) / sizeof(xadapter_axes_meta[0]); j++) {
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
    GamepadIn in_tmp{};
    // Buttons
    for (uint32_t i = 0; i < sizeof(generic_btns_mask) / sizeof(generic_btns_mask[0]); i++) {
        if (ctrl_data->map_mask[0] & BIT(i)) {
            if (ctrl_data->btns[0].value & generic_btns_mask[i]) {
                if (xadapter_analog_btn_idx[i] >= 0) {
                    in_tmp.AnalogButtons[static_cast<std::size_t>(xadapter_analog_btn_idx[i])] = UINT8_MAX;
                } else {
                    in_tmp.Buttons |= xadapter_btns_mask[i];
                }
                wired_data->cnt_mask[i] = ctrl_data->btns[0].cnt_mask[i];
            }
            else {
                wired_data->cnt_mask[i] = 0;
            }
        }
    }
    // Joysticks
    for (uint32_t i = 0; i < XADAPTER_JOY_MAX_AXES; i++) {
        if (ctrl_data->map_mask[0] & (axis_to_btn_mask(i) & ctrl_data->desc[0])) {
            if (ctrl_data->axes[i].value > ctrl_data->axes[i].meta->size_max) {
                xadapter_set_joy_axis(in_tmp, xadapter_joysticks_idx[i], xadapter_axes_meta[i].size_max);
            } else if (ctrl_data->axes[i].value < ctrl_data->axes[i].meta->size_min) {
                xadapter_set_joy_axis(in_tmp, xadapter_joysticks_idx[i], xadapter_axes_meta[i].size_min);
            } else {
                xadapter_set_joy_axis(in_tmp, xadapter_joysticks_idx[i], ctrl_data->axes[i].value);
            }
        }
        wired_data->cnt_mask[axis_to_btn_id(i)] = ctrl_data->axes[i].cnt_mask;
    }
    // Triggers
    for (uint32_t i = 0; i < XADAPTER_TRIG_MAX_AXES; i++) {
        if (ctrl_data->map_mask[0] & (axis_to_btn_mask(i + XADAPTER_JOY_MAX_AXES) & ctrl_data->desc[0])) {
            if (ctrl_data->axes[i + XADAPTER_JOY_MAX_AXES].value > ctrl_data->axes[i + XADAPTER_JOY_MAX_AXES].meta->size_max) {
                in_tmp.AnalogButtons[static_cast<std::size_t>(GamepadAnalog::Lt) + xadapter_trig_axes_idx[i]] =
                    xadapter_axes_meta[i + XADAPTER_JOY_MAX_AXES].size_max;
            } else if (ctrl_data->axes[i + XADAPTER_JOY_MAX_AXES].value < ctrl_data->axes[i + XADAPTER_JOY_MAX_AXES].meta->size_min) {
                in_tmp.AnalogButtons[static_cast<std::size_t>(GamepadAnalog::Lt) + xadapter_trig_axes_idx[i]] =
                    xadapter_axes_meta[i + XADAPTER_JOY_MAX_AXES].size_min;
            } else {
                in_tmp.AnalogButtons[static_cast<std::size_t>(GamepadAnalog::Lt) + xadapter_trig_axes_idx[i]] =
                    ctrl_data->axes[i + XADAPTER_JOY_MAX_AXES].value;
            }
        }
        wired_data->cnt_mask[axis_to_btn_id(i + XADAPTER_JOY_MAX_AXES)] = ctrl_data->axes[i + XADAPTER_JOY_MAX_AXES].cnt_mask;
    }

    std::memcpy(wired_data->output, &in_tmp, sizeof(in_tmp));

#ifdef CONFIG_BLUERETRO_RAW_OUTPUT
    printf("{\"log_type\": \"wired_output\", \"axes\": [%d, %d, %d, %d], \"btns\": %u, \"triggers\": [%u, %u]}\n",
        in_tmp.JoyLx, in_tmp.JoyLy, in_tmp.JoyRx, in_tmp.JoyRy,
        static_cast<unsigned>(in_tmp.Buttons),
        static_cast<unsigned>(in_tmp.AnalogButtons[static_cast<std::size_t>(GamepadAnalog::Lt)]),
        static_cast<unsigned>(in_tmp.AnalogButtons[static_cast<std::size_t>(GamepadAnalog::Rt)]));
#endif
}

void xadapter_fb_to_generic(int32_t dev_mode, struct raw_fb *raw_fb_data, struct generic_fb *fb_data) {
    fb_data->wired_id = raw_fb_data->header.wired_id;
    fb_data->type = raw_fb_data->header.type;
    switch (fb_data->type) {
        case FB_TYPE_RUMBLE:
            {
            uint16_t lf = SpiProtocol::readU16(&raw_fb_data->data[0]);
            uint16_t hf = SpiProtocol::readU16(&raw_fb_data->data[2]);
            fb_data->state = (lf || hf) ? 1 : 0;
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
