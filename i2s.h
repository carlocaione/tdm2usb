/*
 * Copyright (c) 2023, Carlo Caione <ccaione@baylibre.com>
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __I2S_H__
#define __I2S_H__ 1

void BOARD_I2S_Init(void);

/**
 * Case for 16ch / 32bits:
 *
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 * |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *  CH
 * +--+ 4B
 *  PAIR
 * +-----+ 2CH / 8B
 *  FRAME
 * +-----------------------------------------------+ 16CH / 64B
 *  FRAME PER INSTANCE
 * +-----------------------+ 8CH / 32B
 *
 *
 *
 *               +                        +
 *               | I2S_BUFF_SIZE_PER_INST |
 *    CH0-CH7    +------------------------+ +-------+ +-------+ +---+---+
 *               | I2S_BUFF_SIZE_PER_INST |                         |      +---> I2S_BUFF_NUM = 4
 *    CH8-CH15   +------------------------+ +-------+ +-------+ +---+---+
 *               |                        |                         |
 *       +       |                        |                         |
 *       |       +->   I2S_BUFF_SIZE   <--+      +----------+       v
 *       |                                       |          |
 *       v                                       v        +----X----X----X----+
 *                                            6 FRAMES    +----X----X----X----+
 * I2S_INST_NUM = 2                                         U    U     U    U
 *                                                          S    S     S    S
 *                                                          B    B     B    B
 *                                                          P    P     P    P
 *                                                          K    K     K    K
 *                                                          T    T     T    T
 *
 */

/*******************************************************************************
 * Definitions
 ******************************************************************************/
/**
 * Helper macro to set the offset for the secondary channels.
 */
#define CH_OFF(off, n) (TO_BITS(off) + (TO_BITS(I2S_CH_LEN_PER_PAIR) * (n)))

/**
 * Silly macro for byte -> bits conversion.
 */
#define TO_BITS(n) ((n) << 3U)

/**
 * DMA controller.
 */
#define DMA (DMA0)

/**
 * USB max packet size. We default to High-Speed [384 bytes]
 */
#define USB_MAX_PACKET_SIZE (HS_ISO_IN_ENDP_PACKET_SIZE)

/**
 * Number of I2S instances. Each I2S instance (controller) supports at maximum 8
 * channels, so we need 2 instances for 16-channels [2 instances]
 */
#define I2S_INST_NUM (2U)

/**
 * Number of buffers for I2S DMA ping-pong [4]
 */
#define I2S_BUFF_NUM (4U)

/**
 * Size of each I2S DMA instance buffer. We use 4 times the size of the USB
 * packet divided by the number of I2S instances [768 bytes]
 */
#define I2S_BUFF_SIZE_PER_INST ((USB_MAX_PACKET_SIZE * 4U) / I2S_INST_NUM)

/**
 * Size of the I2S DMA buffer considering all the instances [3072 bytes]
 */
#define I2S_BUFF_SIZE (I2S_INST_NUM * I2S_BUFF_SIZE_PER_INST)

/**
 * Number of total channels [16 channels]
 */
#define I2S_CH_NUM (16U)

/**
 * Number of channels per I2S pair (L/R) [2 channels]
 */
#define I2S_CH_NUM_PER_PAIR (2U)

/**
 * Data bytes per channels [4 bytes / 32 bits]
 */
#define I2S_CH_LEN_DATA (4U)

/**
 *  Number of channels per I2S instance [8 channels]
 */
#define I2S_CH_NUM_PER_INST (I2S_CH_NUM / I2S_INST_NUM)

/**
 * Data bytes per I2S pair [8 bytes]
 */
#define I2S_CH_LEN_PER_PAIR (I2S_CH_LEN_DATA * I2S_CH_NUM_PER_PAIR)

/**
 * Total frame length [64 bytes]
 */
#define I2S_FRAME_LEN (I2S_CH_NUM * I2S_CH_LEN_DATA)

/**
 * Frame length per I2S instance [32 bytes]
 */
#define I2S_FRAME_LEN_PER_INST (I2S_CH_NUM_PER_INST * I2S_CH_LEN_DATA)

#endif /* __I2S_H__ */
