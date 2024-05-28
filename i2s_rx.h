/*
 * Copyright (c) 2023, Carlo Caione <ccaione@baylibre.com>
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __I2S_RX_H__
#define __I2S_RX_H__ 1

#include "fsl_dma.h"

uint32_t USB_AudioI2s2UsbBuffer(uint8_t *buffer, uint32_t size);
void BOARD_I2S_RxInit(void);
void I2S_RxStart(void);
void I2S_RxStop(void);

extern uint8_t g_usbBuffIn[];

/**
 * I2S HW controllers.
 */
#define I2S_RX_0 (I2S5) /* FLEXCOMM5 */
#define I2S_RX_1 (I2S7) /* FLEXCOMM7 */

/**
 * I2S DMA channels.
 */
#define I2S_RX_0_DMA_CH (10) /* Flexcomm Interface 5 RX */
#define I2S_RX_1_DMA_CH (14) /* Flexcomm Interface 7 RX */

/**
 * I2S DMA chennels priority.
 */
#define I2S_RX_0_DMA_CH_PRIO (kDMA_ChannelPriority2)
#define I2S_RX_1_DMA_CH_PRIO (kDMA_ChannelPriority2)

#endif /* __I2S_RX_H__ */