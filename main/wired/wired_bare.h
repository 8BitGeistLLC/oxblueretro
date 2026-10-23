/*
 * Copyright (c) 2021-2023, Jacques Gagnon
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _WIRED_BARE_H_
#define _WIRED_BARE_H_

void wired_bare_init(uint32_t package);
void wired_bare_port_cfg(uint16_t mask);
const char *wired_get_sys_name(void);

#endif /* _WIRED_BARE_H_ */
