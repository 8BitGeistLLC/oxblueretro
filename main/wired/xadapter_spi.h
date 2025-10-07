/*
 * Copyright (c) 2025, 8BitGeist
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _XADAPTER_SPI_H_
#define _XADAPTER_SPI_H_

#include <stdint.h>

void xadapter_spi_init(uint32_t package);
void xadapter_spi_port_cfg(uint16_t mask);

#endif /* _XADAPTER_SPI_H_ */
