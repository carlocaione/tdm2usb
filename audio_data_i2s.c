/*
 * Copyright (c) 2016, Freescale Semiconductor, Inc.
 * Copyright 2016,2019 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "usb_device_config.h"
#include "usb.h"
#include "usb_device.h"
#include "usb_device_class.h"
#include "usb_audio_config.h"
#include "usb_device_descriptor.h"
#include "fsl_device_registers.h"

#include "fsl_i2s.h"
#include "fsl_i2s_dma.h"
#include "fsl_i2s_bridge.h"
#include "fsl_dma.h"

#include "audio_data_i2s.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/
/**
 * Helper macro to set the offset for the secondary channels.
 */
#define CH_POS(off, n) (TO_BITS(off) + (TO_BITS(I2S_CH_LEN_PER_PAIR) * (n)))

/**
 * Set USE_FILTER_32_DOWN to (1) (and the FILTER_32 accordingly) when you are
 * retrieving 32-bits per channel from I2S but the useful data is encoded in
 * fewer bits (for example when only 24-bits are actually carrying the real
 * audio information out of 32-bits).
 *
 * Note: this only works with 32-bit channels.
 */
#define USE_FILTER_32_DOWN (1)
#define FILTER_32 (0xFFFFFF00)

/*******************************************************************************
 * Variables
 ******************************************************************************/
static I2S_Type *i2s[] = {
    I2S_0,
    I2S_1,
};

static uint32_t i2s_dma_channel[] = {
    I2S_0_DMA_CH,
    I2S_1_DMA_CH,
};

static dma_priority_t i2s_dma_prio[] = {
    I2S_0_DMA_CH_PRIO,
    I2S_1_DMA_CH_PRIO,
};

USB_DMA_NONINIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE) uint8_t s_wavBuff[USB_MAX_PACKET_SIZE];

SDK_ALIGN(static uint8_t s_i2sBuff[I2S_INST_NUM][I2S_BUFF_SIZE * I2S_BUFF_NUM], sizeof(uint32_t));
SDK_ALIGN(static dma_descriptor_t s_rxDmaDescriptors[I2S_INST_NUM][I2S_BUFF_NUM], FSL_FEATURE_DMA_LINK_DESCRIPTOR_ALIGN_SIZE);

static i2s_dma_handle_t s_RxHandle[I2S_INST_NUM];
static dma_handle_t s_DmaRxHandle[I2S_INST_NUM];
static uint32_t s_audioPosition[I2S_INST_NUM] = { 0 };

volatile unsigned int first_int = 0;

static i2s_transfer_t s_rxTransfer[I2S_INST_NUM][I2S_BUFF_NUM] = {
    {
        {
            .data = &s_i2sBuff[I2S_CH_0_7][0],
            .dataSize = I2S_BUFF_SIZE,
        },
        {
            .data = &s_i2sBuff[I2S_CH_0_7][I2S_BUFF_SIZE],
            .dataSize = I2S_BUFF_SIZE,
        }, 
    },
    {
        {
            .data = &s_i2sBuff[I2S_CH_8_15][0],
            .dataSize = I2S_BUFF_SIZE,
        },
        {
            .data = &s_i2sBuff[I2S_CH_8_15][I2S_BUFF_SIZE],
            .dataSize = I2S_BUFF_SIZE,
        }, 
    },
};

/*******************************************************************************
 * Code
 ******************************************************************************/
/*!
 * @brief Audio wav data prepare function.
 *
 * This function prepare audio wav data before send.
 */
void USB_AudioRecorderGetBuffer(uint8_t *buffer, uint32_t size)
{
    assert(size % I2S_FRAME_LEN == 0);

    for (size_t k = 0; k < size; k += I2S_FRAME_LEN)
    {
        for (size_t inst = 0; inst < I2S_INST_NUM; inst++)
        {
            uint32_t *pos = &s_audioPosition[inst];
#if USE_FILTER_32_DOWN
            uint32_t *p_buffer = (uint32_t *) (buffer + k);
            uint32_t *p_i2s_buffer = (uint32_t *) &s_i2sBuff[inst][*pos];

            for (size_t ch = 0; ch < I2S_CH_NUM_PER_INST; ch++)
            {
                p_buffer[ch + (inst * I2S_CH_NUM_PER_INST)] = p_i2s_buffer[ch] & FILTER_32;
            }
#else
            memcpy(buffer + k + (inst * I2S_FRAME_LEN_PER_INST), &s_i2sBuff[inst][*pos], I2S_FRAME_LEN_PER_INST);
#endif
            *pos = (*pos + I2S_FRAME_LEN_PER_INST) % (I2S_BUFF_SIZE * I2S_BUFF_NUM);
        }
    }
}

static void RxCallback(I2S_Type *base, i2s_dma_handle_t *handle, status_t completionStatus, void *userData)
{
    if (first_int == 0)
    {
        s_audioPosition[I2S_CH_0_7] = 0;
        s_audioPosition[I2S_CH_8_15] = 0;

        first_int = 1;
    }
}

void Board_I2S_Init(void)
{
    i2s_config_t s_RxConfig = { 0 };

    I2S_BRIDGE_SetShareSignalSrc(kI2S_BRIDGE_ShareSet0, kI2S_BRIDGE_SignalSCK, kI2S_BRIDGE_Flexcomm5);
    I2S_BRIDGE_SetShareSignalSrc(kI2S_BRIDGE_ShareSet0, kI2S_BRIDGE_SignalWS, kI2S_BRIDGE_Flexcomm5);
    I2S_BRIDGE_SetShareSignalSrc(kI2S_BRIDGE_ShareSet0, kI2S_BRIDGE_SignalDataIn, kI2S_BRIDGE_Flexcomm5);

    I2S_BRIDGE_SetFlexcommSignalShareSet(kI2S_BRIDGE_Flexcomm4, kI2S_BRIDGE_SignalSCK, kI2S_BRIDGE_ShareSet0);
    I2S_BRIDGE_SetFlexcommSignalShareSet(kI2S_BRIDGE_Flexcomm4, kI2S_BRIDGE_SignalWS, kI2S_BRIDGE_ShareSet0);
    I2S_BRIDGE_SetFlexcommSignalShareSet(kI2S_BRIDGE_Flexcomm4, kI2S_BRIDGE_SignalDataIn, kI2S_BRIDGE_ShareSet0);

    DMA_Init(DMA_RX);

    for (size_t inst = 0; inst < I2S_INST_NUM; inst++)
    {
        DMA_EnableChannel(DMA_RX, i2s_dma_channel[inst]);
        DMA_SetChannelPriority(DMA_RX, i2s_dma_channel[inst], i2s_dma_prio[inst]);
        DMA_CreateHandle(&s_DmaRxHandle[inst], DMA_RX, i2s_dma_channel[inst]);
    }

    /**
     * Default values:
     *   config->masterSlave = kI2S_MasterSlaveNormalSlave;
     *   config->mode = kI2S_ModeI2sClassic;
     *   config->rightLow = false;
     *   config->leftJust = false;
     *   config->pdmData = false;
     *   config->sckPol = false;
     *   config->wsPol = false;
     *   config->divider = 1;
     *   config->oneChannel = false;
     *   config->dataLength = 16;
     *   config->frameLength = 32;
     *   config->position = 0;
     *   config->watermark = 4;
     *   config->txEmptyZero = false;
     *   config->pack48 = false;
     */
    I2S_RxGetDefaultConfig(&s_RxConfig);

    for (size_t inst = 0; inst < I2S_INST_NUM; inst++)
    {
        s_RxConfig.masterSlave = kI2S_MasterSlaveNormalSlave; /* Normal Slave */
        s_RxConfig.mode = kI2S_ModeDspWsShort;                /* DSP mode, WS having one clock long pulse */
        s_RxConfig.dataLength = TO_BITS(I2S_CH_LEN_DATA);
        s_RxConfig.frameLength = TO_BITS(I2S_FRAME_LEN);
        s_RxConfig.position = (inst * TO_BITS(I2S_FRAME_LEN_PER_INST));

        I2S_RxInit(i2s[inst], &s_RxConfig);
        I2S_EnableSecondaryChannel(i2s[inst], kI2S_SecondaryChannel1, false, CH_POS((inst * I2S_FRAME_LEN_PER_INST), 1));
        I2S_EnableSecondaryChannel(i2s[inst], kI2S_SecondaryChannel2, false, CH_POS((inst * I2S_FRAME_LEN_PER_INST), 2));
        I2S_EnableSecondaryChannel(i2s[inst], kI2S_SecondaryChannel3, false, CH_POS((inst * I2S_FRAME_LEN_PER_INST), 3));

        I2S_RxTransferCreateHandleDMA(i2s[inst], &s_RxHandle[inst], &s_DmaRxHandle[inst], RxCallback, (void *)&s_rxTransfer[inst]);
        I2S_TransferInstallLoopDMADescriptorMemory(&s_RxHandle[inst], s_rxDmaDescriptors[inst], I2S_BUFF_NUM);
    }

    for (size_t inst = 0; inst < I2S_INST_NUM; inst++)
    {
        if (I2S_TransferReceiveLoopDMA(i2s[inst], &s_RxHandle[inst], &s_rxTransfer[inst][0], I2S_BUFF_NUM) != kStatus_Success)
        {
            assert(false);
        }
    }
}