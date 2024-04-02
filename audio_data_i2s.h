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

#define I2S_RX                  (I2S5)
#define I2S_RX_DATA_LEN         (24U)
#define I2S_RX_FRAME_LEN        (32U)

#define DMA_RX                  (DMA0)
#define DMA_RX_CHANNEL          (10)
#define DMA_RX_CHANNEL_PRIO     (kDMA_ChannelPriority2)

#endif /* __AUDIO_DATA_I2S_H__ */
