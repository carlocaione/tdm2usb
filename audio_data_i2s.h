/*
 * Copyright (c) 2015 - 2016, Freescale Semiconductor, Inc.
 * Copyright 2016 - 2017 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __AUDIO_DATA_I2S_H__
#define __AUDIO_DATA_I2S_H__ 1

/*******************************************************************************
 * Definitions
 ******************************************************************************/

#define I2S_RX_0_7 (I2S5)
#define I2S_RX_8_15 (I2S4)

#define DMA_RX (DMA0)
#define DMA_RX_CH_0_7 (10)
#define DMA_RX_CH_8_15 (8)
#define DMA_RX_CH_PRIO_0_7 (kDMA_ChannelPriority7)
#define DMA_RX_CH_PRIO_8_15 (kDMA_ChannelPriority7)

#endif /* __AUDIO_DATA_I2S_H__ */
