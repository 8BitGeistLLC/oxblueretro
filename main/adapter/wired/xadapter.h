/*
 * Copyright (c) 2025, 8BitGeist
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _XADAPTER_H_
#define _XADAPTER_H_
#include "adapter/adapter.h"

void xadapter_meta_init(struct wired_ctrl *ctrl_data);
void xadapter_init_buffer(int32_t dev_mode, struct wired_data *wired_data);
void xadapter_from_generic(int32_t dev_mode, struct wired_ctrl *ctrl_data, struct wired_data *wired_data);
void xadapter_fb_to_generic(int32_t dev_mode, struct raw_fb *raw_fb_data, struct generic_fb *fb_data);
void xadapter_gen_turbo_mask(struct wired_data *wired_data);

#endif /* _XADAPTER_H_ */