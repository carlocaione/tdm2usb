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
 * Number of I2S instances. Each I2S instance (controller) supports at maximum 8
 * channels, so we need 2 instances for 16-channels [2 instances]
 */
#define I2S_INST_NUM (2U)

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
