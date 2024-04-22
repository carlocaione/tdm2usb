/*
 * Copyright (c) 2023, Carlo Caione <ccaione@baylibre.com>
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __AUDIO_DATA_I2S_H__
#define __AUDIO_DATA_I2S_H__ 1

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
 */

/*******************************************************************************
 * Definitions
 ******************************************************************************/
/**
 * Silly macro for byte -> bits conversion.
 */
#define TO_BITS(n) ((n) << 3U)

/**
 * DMA controller.
 */
#define DMA_RX (DMA0)

/**
 * USB max packet size. We default to High-Speed [384 bytes]
 */
#define USB_MAX_PACKET_SIZE (HS_ISO_IN_ENDP_PACKET_SIZE)

/**
 * I2S HW controllers.
 */
#define I2S_0 (I2S5)
#define I2S_1 (I2S4)

/**
 * I2S DMA channels.
 */
#define I2S_0_DMA_CH (10)
#define I2S_1_DMA_CH (8)

/**
 * Number of I2S instances. Each I2S instance (controller) supports at maximum 8
 * channels, so we need 2 instances for 16-channels [2 instances]
 */
#define I2S_INST_NUM (ARRAY_SIZE(i2s))

/**
 * I2S DMA chennels priority.
 */
#define I2S_0_DMA_CH_PRIO (kDMA_ChannelPriority2)
#define I2S_1_DMA_CH_PRIO (kDMA_ChannelPriority2)

/**
 * Number of buffers for I2S DMA ping-pong [2]
 */
#define I2S_BUFF_NUM (2U)

/**
 * Buffer size for each I2S DMA buffer. We use 4 times the size of the USB packet [1536 bytes]
 */
#define I2S_BUFF_SIZE (USB_MAX_PACKET_SIZE * 4U)

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

#endif /* __AUDIO_DATA_I2S_H__ */
