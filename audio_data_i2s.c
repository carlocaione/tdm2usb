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
 * Silly macro for byte -> bits conversion.
 */
#define TO_BITS(n) ((n) << 3U)

/**
 * USB max packet size. We default to High-Speed [384 bytes]
 */
#define AUDIO_ENDPOINT_MAX_PACKET_SIZE (HS_ISO_IN_ENDP_PACKET_SIZE)

/**
 * Number of I2S instances. Each I2S instance (controller) supports at maximum 8
 * channels, so we need 2 instances [2 instances]
 */
#define I2S_INST_NUM (2U)

/**
 * Indexes for I2S instances.
 */
#define I2S_CH_0_7 (0U)
#define I2S_CH_8_15 (1U)

/**
 * Number of buffers for I2S DMA ping-pong [2]
 */
#define I2S_BUFF_NUM (2U)

/**
 * Buffer size for each I2S DMA buffer. We use 4 times the size of the USB packet [1536 bytes]
 */
#define I2S_BUFF_SIZE (AUDIO_ENDPOINT_MAX_PACKET_SIZE * 4U)

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

/**
 * Helper macro to set the offset for the secondary channels.
 */
#define CH_POS(off, n) (TO_BITS(off) + (TO_BITS(I2S_CH_LEN_PER_PAIR) * (n)))

/*******************************************************************************
 * Variables
 ******************************************************************************/
static I2S_Type *i2s[] = {
    I2S5,
    I2S4,
};

static uint32_t i2s_dma_channel[] = {
    10, /** Flexcomm5 */
    8,  /** Flexcomm4 */
};

static dma_priority_t i2s_dma_prio[] = {
    kDMA_ChannelPriority2,
    kDMA_ChannelPriority2,
};

USB_DMA_NONINIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE) uint8_t s_wavBuff[AUDIO_ENDPOINT_MAX_PACKET_SIZE];

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
    uint32_t k = 0U;

    while (k < size)
    {
        for (size_t j = 0U; j < I2S_FRAME_LEN_PER_INST; j++)
        {
            if (s_audioPosition[I2S_CH_0_7] == (I2S_BUFF_SIZE * I2S_BUFF_NUM))
            {
                s_audioPosition[I2S_CH_0_7] = 0U;
            }

            if ((j % 4) != 0) {
                *(buffer + k) = s_i2sBuff[I2S_CH_0_7][s_audioPosition[I2S_CH_0_7]];
            } else {
                *(buffer + k) = 0x00;
            }
            s_audioPosition[I2S_CH_0_7]++;
            k++;
        }

        for (size_t j = 0U; j < I2S_FRAME_LEN_PER_INST; j++)
        {
            if (s_audioPosition[I2S_CH_8_15] == (I2S_BUFF_SIZE * I2S_BUFF_NUM))
            {
                s_audioPosition[I2S_CH_8_15] = 0U;
            }

            if ((j % 4) != 0) {
                *(buffer + k) = s_i2sBuff[I2S_CH_8_15][s_audioPosition[I2S_CH_8_15]];
            } else {
                *(buffer + k) = 0x00;
            }
            s_audioPosition[I2S_CH_8_15]++;
            k++;
        }
    }
}

static void RxCallback(I2S_Type *base, i2s_dma_handle_t *handle, status_t completionStatus, void *userData)
{
    if (first_int == 0U)
    {
        s_audioPosition[I2S_CH_0_7] = 0U;
        s_audioPosition[I2S_CH_8_15] = 0U;

        first_int = 1U;
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
    DMA_EnableChannel(DMA_RX, i2s_dma_channel[I2S_CH_0_7]);
    DMA_SetChannelPriority(DMA_RX, i2s_dma_channel[I2S_CH_0_7], i2s_dma_prio[I2S_CH_0_7]);
    DMA_CreateHandle(&s_DmaRxHandle[I2S_CH_0_7], DMA_RX, i2s_dma_channel[I2S_CH_0_7]);

    DMA_EnableChannel(DMA_RX, i2s_dma_channel[I2S_CH_8_15]);
    DMA_SetChannelPriority(DMA_RX, i2s_dma_channel[I2S_CH_8_15], i2s_dma_prio[I2S_CH_8_15]);
    DMA_CreateHandle(&s_DmaRxHandle[I2S_CH_8_15], DMA_RX, i2s_dma_channel[I2S_CH_8_15]);

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

    s_RxConfig.masterSlave = kI2S_MasterSlaveNormalSlave; /* Normal Slave */
    s_RxConfig.mode = kI2S_ModeDspWsShort;                /* DSP mode, WS having one clock long pulse */
    s_RxConfig.dataLength = TO_BITS(I2S_CH_LEN_DATA);
    s_RxConfig.frameLength = TO_BITS(I2S_FRAME_LEN);

    I2S_RxInit(i2s[I2S_CH_0_7], &s_RxConfig);
    I2S_EnableSecondaryChannel(i2s[I2S_CH_0_7], kI2S_SecondaryChannel1, false, CH_POS(0, 1));
    I2S_EnableSecondaryChannel(i2s[I2S_CH_0_7], kI2S_SecondaryChannel2, false, CH_POS(0, 2));
    I2S_EnableSecondaryChannel(i2s[I2S_CH_0_7], kI2S_SecondaryChannel3, false, CH_POS(0, 3));

    I2S_RxTransferCreateHandleDMA(i2s[I2S_CH_0_7], &s_RxHandle[I2S_CH_0_7], &s_DmaRxHandle[I2S_CH_0_7], RxCallback, (void *)&s_rxTransfer[I2S_CH_0_7]);
    I2S_TransferInstallLoopDMADescriptorMemory(&s_RxHandle[I2S_CH_0_7], s_rxDmaDescriptors[I2S_CH_0_7], I2S_BUFF_NUM);

    s_RxConfig.position = TO_BITS(I2S_FRAME_LEN_PER_INST);

    I2S_RxInit(i2s[I2S_CH_8_15], &s_RxConfig);
    I2S_EnableSecondaryChannel(i2s[I2S_CH_8_15], kI2S_SecondaryChannel1, false, CH_POS(I2S_FRAME_LEN_PER_INST, 1));
    I2S_EnableSecondaryChannel(i2s[I2S_CH_8_15], kI2S_SecondaryChannel2, false, CH_POS(I2S_FRAME_LEN_PER_INST, 2));
    I2S_EnableSecondaryChannel(i2s[I2S_CH_8_15], kI2S_SecondaryChannel3, false, CH_POS(I2S_FRAME_LEN_PER_INST, 3));

    I2S_RxTransferCreateHandleDMA(i2s[I2S_CH_8_15], &s_RxHandle[I2S_CH_8_15], &s_DmaRxHandle[I2S_CH_8_15], RxCallback, (void *)&s_rxTransfer[I2S_CH_8_15]);
    I2S_TransferInstallLoopDMADescriptorMemory(&s_RxHandle[I2S_CH_8_15], s_rxDmaDescriptors[I2S_CH_8_15], I2S_BUFF_NUM);

    if (I2S_TransferReceiveLoopDMA(i2s[I2S_CH_0_7], &s_RxHandle[I2S_CH_0_7], &s_rxTransfer[I2S_CH_0_7][0], I2S_BUFF_NUM) != kStatus_Success)
    {
        assert(false);
    }

    if (I2S_TransferReceiveLoopDMA(i2s[I2S_CH_8_15], &s_RxHandle[I2S_CH_8_15], &s_rxTransfer[I2S_CH_8_15][0], I2S_BUFF_NUM) != kStatus_Success)
    {
        assert(false);
    }
}