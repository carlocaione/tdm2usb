/*
 * Copyright (c) 2023, Carlo Caione <ccaione@baylibre.com>
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __I2S_RX_H__
#define __I2S_RX_H__ 1

/**
 *
 *
 *               +                           +
 *               | I2S_RX_BUFF_SIZE_PER_INST |
 *    CH0-CH7    +---------------------------+ +-------+ +-------+ +---+---+
 *               | I2S_RX_BUFF_SIZE_PER_INST |                         |      +---> I2S_RX_BUFF_NUM = 4
 *    CH8-CH15   +---------------------------+ +-------+ +-------+ +---+---+
 *               |                           |                         |
 *       +       |                           |                         |
 *       |       +->   I2S_RX_BUFF_SIZE   <--+      +----------+       v
 *       |                                          |          |
 *       v                                          v        +----X----X----X----+
 *                                               6 FRAMES    +----X----X----X----+
 * I2S_INST_NUM = 2                                            U    U     U    U
 *                                                             S    S     S    S
 *                                                             B    B     B    B
 *                                                             P    P     P    P
 *                                                             K    K     K    K
 *                                                             T    T     T    T
 *
 */

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

/**
 * USB max packet size. We default to High-Speed [384 bytes]
 */
#define USB_MAX_PACKET_IN_SIZE (HS_ISO_IN_ENDP_PACKET_SIZE)

/**
 * Number of buffers for I2S DMA ping-pong [4]
 */
#define I2S_RX_BUFF_NUM (4U)

/**
 * Size of each I2S DMA instance buffer. We use 4 times the size of the USB
 * packet divided by the number of I2S instances [768 bytes]
 */
#define I2S_RX_BUFF_SIZE_PER_INST ((USB_MAX_PACKET_IN_SIZE * 4U) / I2S_INST_NUM)

/**
 * Size of the I2S DMA buffer considering all the instances [1536 bytes]
 */
#define I2S_RX_BUFF_SIZE (I2S_INST_NUM * I2S_RX_BUFF_SIZE_PER_INST)

#endif /* __I2S_RX_H__ */