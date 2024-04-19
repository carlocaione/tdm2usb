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
#define TO_BITS(n) ((n) << 3U)

#define AUDIO_ENDPOINT_MAX_PACKET_SIZE (HS_ISO_IN_ENDP_PACKET_SIZE)

#define I2S_BUFF_NUM (2)
#define I2S_BUFF_SIZE (AUDIO_ENDPOINT_MAX_PACKET_SIZE * 4)

#define CH_NUM (16) /** Channels number [16] */
#define CH_NUM_PER_PAIR (2) /** Channels number per I2S pair (L/R) [2] */
#define CH_LEN_DATA (4) /** Bytes per channel [4 bytes] */
#define INST_NUM (2) /** Number of I2S instances [2] */

#define CH_NUM_PER_INST (CH_NUM / INST_NUM) /** Channels number per I2S instance [8] */
#define CH_LEN_PER_PAIR (CH_LEN_DATA * CH_NUM_PER_PAIR) /** Bytes per I2S pair [8 bytes] */

#define FRAME_LEN (CH_NUM * CH_LEN_DATA) /** Bytes in an audio frame [64 bytes] */
#define FRAME_LEN_PER_INST (CH_NUM_PER_INST * CH_LEN_DATA) /** Bytes of the frame managed by an I2S instance [32 bytes] */

#define CH_POS(off, n) (TO_BITS(off) + (TO_BITS(CH_LEN_PER_PAIR) * (n)))

/*******************************************************************************
 * Variables
 ******************************************************************************/
USB_DMA_NONINIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE) uint8_t s_wavBuff[AUDIO_ENDPOINT_MAX_PACKET_SIZE];

SDK_ALIGN(static uint8_t s_i2sBuff_0_7[I2S_BUFF_SIZE * I2S_BUFF_NUM], sizeof(uint32_t));
SDK_ALIGN(static uint8_t s_i2sBuff_8_15[I2S_BUFF_SIZE * I2S_BUFF_NUM], sizeof(uint32_t));

SDK_ALIGN(static dma_descriptor_t s_rxDmaDescriptors_0_7[2U], FSL_FEATURE_DMA_LINK_DESCRIPTOR_ALIGN_SIZE);
SDK_ALIGN(static dma_descriptor_t s_rxDmaDescriptors_8_15[2U], FSL_FEATURE_DMA_LINK_DESCRIPTOR_ALIGN_SIZE);

static i2s_dma_handle_t s_RxHandle_0_7;
static i2s_dma_handle_t s_RxHandle_8_15;

static dma_handle_t s_DmaRxHandle_0_7;
static dma_handle_t s_DmaRxHandle_8_15;

static uint32_t s_audioPosition_0_7 = 0U;
static uint32_t s_audioPosition_8_15 = 0U;

volatile unsigned int first_int = 0;

static i2s_transfer_t s_rxTransfer_0_7[2] = {
    {
        .data = &s_i2sBuff_0_7[0],
        .dataSize = I2S_BUFF_SIZE,
    },
    {
        .data = &s_i2sBuff_0_7[I2S_BUFF_SIZE],
        .dataSize = I2S_BUFF_SIZE,
    }
};

static i2s_transfer_t s_rxTransfer_8_15[2] = {
    {
        .data = &s_i2sBuff_8_15[0],
        .dataSize = I2S_BUFF_SIZE,
    },
    {
        .data = &s_i2sBuff_8_15[I2S_BUFF_SIZE],
        .dataSize = I2S_BUFF_SIZE,
    }
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
        for (size_t j = 0U; j < FRAME_LEN_PER_INST; j++)
        {
            if (s_audioPosition_0_7 == (I2S_BUFF_SIZE * I2S_BUFF_NUM))
            {
                s_audioPosition_0_7 = 0U;
            }

            if ((j % 4) != 0) {
                *(buffer + k) = s_i2sBuff_0_7[s_audioPosition_0_7];
            } else {
                *(buffer + k) = 0x00;
            }
            s_audioPosition_0_7++;
            k++;
        }

        for (size_t j = 0U; j < FRAME_LEN_PER_INST; j++)
        {
            if (s_audioPosition_8_15 == (I2S_BUFF_SIZE * I2S_BUFF_NUM))
            {
                s_audioPosition_8_15 = 0U;
            }

            if ((j % 4) != 0) {
                *(buffer + k) = s_i2sBuff_8_15[s_audioPosition_8_15];
            } else {
                *(buffer + k) = 0x00;
            }
            s_audioPosition_8_15++;
            k++;
        }
    }
}

static void RxCallback(I2S_Type *base, i2s_dma_handle_t *handle, status_t completionStatus, void *userData)
{
    if (first_int == 0U)
    {
        s_audioPosition_0_7 = 0U;
        s_audioPosition_8_15 = 0U;

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
    DMA_EnableChannel(DMA_RX, DMA_RX_CH_0_7);
    DMA_SetChannelPriority(DMA_RX, DMA_RX_CH_0_7, DMA_RX_CH_PRIO_0_7);
    DMA_CreateHandle(&s_DmaRxHandle_0_7, DMA_RX, DMA_RX_CH_0_7);

    DMA_EnableChannel(DMA_RX, DMA_RX_CH_8_15);
    DMA_SetChannelPriority(DMA_RX, DMA_RX_CH_8_15, DMA_RX_CH_PRIO_8_15);
    DMA_CreateHandle(&s_DmaRxHandle_8_15, DMA_RX, DMA_RX_CH_8_15);

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
    s_RxConfig.dataLength = TO_BITS(CH_LEN_DATA);
    s_RxConfig.frameLength = TO_BITS(FRAME_LEN);

    I2S_RxInit(I2S_RX_0_7, &s_RxConfig);
    I2S_EnableSecondaryChannel(I2S_RX_0_7, kI2S_SecondaryChannel1, false, CH_POS(0, 1));
    I2S_EnableSecondaryChannel(I2S_RX_0_7, kI2S_SecondaryChannel2, false, CH_POS(0, 2));
    I2S_EnableSecondaryChannel(I2S_RX_0_7, kI2S_SecondaryChannel3, false, CH_POS(0, 3));

    I2S_RxTransferCreateHandleDMA(I2S_RX_0_7, &s_RxHandle_0_7, &s_DmaRxHandle_0_7, RxCallback, (void *)&s_rxTransfer_0_7);
    I2S_TransferInstallLoopDMADescriptorMemory(&s_RxHandle_0_7, s_rxDmaDescriptors_0_7, 2U);

    s_RxConfig.position = TO_BITS(FRAME_LEN_PER_INST);

    I2S_RxInit(I2S_RX_8_15, &s_RxConfig);
    I2S_EnableSecondaryChannel(I2S_RX_8_15, kI2S_SecondaryChannel1, false, CH_POS(FRAME_LEN_PER_INST, 1));
    I2S_EnableSecondaryChannel(I2S_RX_8_15, kI2S_SecondaryChannel2, false, CH_POS(FRAME_LEN_PER_INST, 2));
    I2S_EnableSecondaryChannel(I2S_RX_8_15, kI2S_SecondaryChannel3, false, CH_POS(FRAME_LEN_PER_INST, 3));

    I2S_RxTransferCreateHandleDMA(I2S_RX_8_15, &s_RxHandle_8_15, &s_DmaRxHandle_8_15, RxCallback, (void *)&s_rxTransfer_8_15);
    I2S_TransferInstallLoopDMADescriptorMemory(&s_RxHandle_8_15, s_rxDmaDescriptors_8_15, 2U);

    if (I2S_TransferReceiveLoopDMA(I2S_RX_0_7, &s_RxHandle_0_7, &s_rxTransfer_0_7[0], 2U) != kStatus_Success)
    {
        assert(false);
    }

    if (I2S_TransferReceiveLoopDMA(I2S_RX_8_15, &s_RxHandle_8_15, &s_rxTransfer_8_15[0], 2U) != kStatus_Success)
    {
        assert(false);
    }
}