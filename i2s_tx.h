/*
 * Copyright (c) 2023, Carlo Caione <ccaione@baylibre.com>
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __I2S_TX_H__
#define __I2S_TX_H__ 1

#include "fsl_dma.h"

void USB_AudioUsb2I2sBuffer(uint8_t *buffer, uint32_t size);
void BOARD_I2S_TxInit(void);

extern uint8_t g_usbBuffOut[];

/**
 * I2S HW controllers.
 */
#define I2S_TX_0 (I2S4) /* FLEXCOMM4 */
#define I2S_TX_1 (I2S6) /* FLEXCOMM6 */

/**
 * I2S DMA channels.
 */
#define I2S_TX_0_DMA_CH (9)  /* Flexcomm Interface 4 TX */
#define I2S_TX_1_DMA_CH (13) /* Flexcomm Interface 6 TX */

/**
 * I2S DMA chennels priority.
 */
#define I2S_TX_0_DMA_CH_PRIO (kDMA_ChannelPriority2)
#define I2S_TX_1_DMA_CH_PRIO (kDMA_ChannelPriority2)

#endif /* __I2S_TX_H__ */