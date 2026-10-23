/*
 * Copyright (c) 2021-2024, Jacques Gagnon
 * SPDX-License-Identifier: Apache-2.0
 */

#include "wired.h"
#include "xadapter.h"

int32_t wired_meta_init(struct wired_ctrl *ctrl_data) {
    xadapter_meta_init(ctrl_data);
    return 0;
}

void IRAM_ATTR wired_init_buffer(int32_t dev_mode, struct wired_data *wired_data) {
    xadapter_init_buffer(dev_mode, wired_data);
}

void wired_from_generic(int32_t dev_mode, struct wired_ctrl *ctrl_data, struct wired_data *wired_data) {
    xadapter_from_generic(dev_mode, ctrl_data, wired_data);
}

void wired_fb_to_generic(int32_t dev_mode, struct raw_fb *raw_fb_data, struct generic_fb *fb_data) {
    xadapter_fb_to_generic(dev_mode, raw_fb_data, fb_data);
}
