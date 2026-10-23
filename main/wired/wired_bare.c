/*
 * Copyright (c) 2021-2024, Jacques Gagnon
 * SPDX-License-Identifier: Apache-2.0
 */

#include "adapter/adapter.h"
#include "xadapter_spi.h"
#include "wired_bare.h"

void wired_bare_init(uint32_t package) {
    xadapter_spi_init(package);
}

void wired_bare_port_cfg(uint16_t mask) {
    xadapter_spi_port_cfg(mask);
}

const char *wired_get_sys_name(void) {
    return "XADAPTER";
}
