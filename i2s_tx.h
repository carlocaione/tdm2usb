/*
 * Copyright (c) 2023, Carlo Caione <ccaione@baylibre.com>
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __I2S_TX_H__
#define __I2S_TX_H__ 1

/**
 *
 *
 *               +                           +
 *               | I2S_TX_BUFF_SIZE_PER_INST |
 *    CH0-CH7    +---------------------------+ +-------+ +-------+ +---+---+
 *               | I2S_TX_BUFF_SIZE_PER_INST |                         |      +---> I2S_TX_BUFF_NUM = 4
 *    CH8-CH15   +---------------------------+ +-------+ +-------+ +---+---+
 *               |                           |                         |
 *       +       |                           |                         |
 *       |       +->   I2S_TX_BUFF_SIZE   <--+      +----------+       v
 *       |                                          |          |
 *       v                                          v        +----X----X----X----+
 *                                           6 FRAMES (+1)   +----X----X----X----+
 * I2S_INST_NUM = 2                                            U    U     U    U
 *                                                             S    S     S    S
 *                                                             B    B     B    B
 *                                                             P    P     P    P
 *                                                             K    K     K    K
 *                                                             T    T     T    T
 *
 */

#include "fsl_dma.h"

void USB_AudioUsb2I2sBuffer(uint8_t *buffer, uint32_t size);
void BOARD_I2S_TxInit(void);

void I2S_TxStart(void);
void I2S_TxStop(void);

uint32_t USB_GetFeedback(uint8_t speed);

void USB_OutPrintInfo(void);

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
#define I2S_TX_0_DMA_CH_PRIO (kDMA_ChannelPriority7)
#define I2S_TX_1_DMA_CH_PRIO (kDMA_ChannelPriority7)

/**
 * USB max packet size. We default to High-Speed [448 bytes]
 */
#define USB_MAX_PACKET_OUT_SIZE (HS_ISO_OUT_ENDP_PACKET_SIZE + (AUDIO_FORMAT_CHANNELS * AUDIO_FORMAT_SIZE))

/**
 * Number of buffers for I2S DMA ping-pong [4]
 */
#define I2S_TX_BUFF_NUM (4U)

/**
 * Size of each I2S DMA instance buffer. We use 4 times the size of the USB
 * packet divided by the number of I2S instances [896 bytes]
 */
#define I2S_TX_BUFF_SIZE_PER_INST ((USB_MAX_PACKET_OUT_SIZE * 4U) / I2S_INST_NUM)

/**
 * Size of the I2S DMA buffer considering all the instances [1792 bytes]
 */
#define I2S_TX_BUFF_SIZE (I2S_INST_NUM * I2S_TX_BUFF_SIZE_PER_INST)

#endif /* __I2S_TX_H__ */