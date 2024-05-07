/*
 * Copyright (c) 2023, Carlo Caione <ccaione@baylibre.com>
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
#define CH_OFF(off, n) (TO_BITS(off) + (TO_BITS(I2S_CH_LEN_PER_PAIR) * (n)))

/**
 * Set USE_RX_FILTER_32_DOWN to (1) (and the FILTER_32 accordingly) when you are
 * retrieving 32-bits per channel from I2S but the useful data is encoded in
 * fewer bits (for example when only 24-bits are actually carrying the real
 * audio information out of 32-bits).
 *
 * Note: this only works with 32-bit channels.
 */
#define USE_RX_FILTER_32_DOWN (1)
#define FILTER_32 (0xFFFFFF00)

/*******************************************************************************
 * Variables
 ******************************************************************************/
static I2S_Type *s_i2sRxBase[] = {
    I2S_RX_0,
    I2S_RX_1,
};

static uint32_t s_i2sRxDmaChannel[] = {
    I2S_RX_0_DMA_CH,
    I2S_RX_1_DMA_CH,
};

static dma_priority_t s_i2sRxDmaPrio[] = {
    I2S_RX_0_DMA_CH_PRIO,
    I2S_RX_1_DMA_CH_PRIO,
};

USB_DMA_NONINIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE) uint8_t g_usbBuffIn[USB_MAX_PACKET_SIZE];
USB_DMA_NONINIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE) uint8_t g_usbBuffOut[USB_MAX_PACKET_SIZE];

SDK_ALIGN(static dma_descriptor_t s_dmaRxDesc[I2S_INST_NUM][I2S_BUFF_NUM], FSL_FEATURE_DMA_LINK_DESCRIPTOR_ALIGN_SIZE);
SDK_ALIGN(static uint8_t s_i2sRxBuff[I2S_INST_NUM][I2S_BUFF_SIZE * I2S_BUFF_NUM], sizeof(uint32_t));

static i2s_transfer_t s_i2sRxTransfer[I2S_INST_NUM][I2S_BUFF_NUM];
static i2s_dma_handle_t s_i2sDmaRxHandle[I2S_INST_NUM];
static dma_handle_t s_dmaRxHandle[I2S_INST_NUM];
static uint32_t s_rxAudioPos[I2S_INST_NUM];

volatile unsigned int g_rxFirstInt = 0;

/*******************************************************************************
 * Code
 ******************************************************************************/
/*!
 * @brief todo
 */
void USB_AudioRecorderPutBuffer(uint8_t *buffer, uint32_t size)
{
}

/*!
 * @brief Audio wav data prepare function.
 *
 * This function prepare audio wav data before send.
 */
void USB_AudioRecorderGetBuffer(uint8_t *usbBuffer, uint32_t size)
{
    assert(size % I2S_FRAME_LEN == 0);

    for (size_t k = 0; k < size; k += I2S_FRAME_LEN)
    {
        for (size_t inst = 0; inst < I2S_INST_NUM; inst++)
        {
            uint32_t *pos = &s_rxAudioPos[inst];
#if USE_RX_FILTER_32_DOWN
            uint32_t *outBuffer = (uint32_t *) (usbBuffer + k);
            uint32_t *i2sBuffer = (uint32_t *) &s_i2sRxBuff[inst][*pos];

            for (size_t ch = 0; ch < I2S_CH_NUM_PER_INST; ch++)
            {
                outBuffer[ch + (inst * I2S_CH_NUM_PER_INST)] = i2sBuffer[ch] & FILTER_32;
            }
#else
            memcpy(usbBuffer + k + (inst * I2S_FRAME_LEN_PER_INST), &s_i2sRxBuff[inst][*pos], I2S_FRAME_LEN_PER_INST);
#endif
            *pos = (*pos + I2S_FRAME_LEN_PER_INST) % (I2S_BUFF_SIZE * I2S_BUFF_NUM);
        }
    }
}

/*!
 * @brief I2S RX callback.
 *
 * This function is called when at least one of the ping-pong buffers is full.
 */
static void RxCallback(I2S_Type *base, i2s_dma_handle_t *handle, status_t completionStatus, void *userData)
{
    if (g_rxFirstInt == 0)
    {
        for (size_t inst = 0; inst < I2S_INST_NUM; inst++)
        {
            s_rxAudioPos[inst] = 0;
        }

        g_rxFirstInt = 1;
    }
}

/*!
 * @brief I2S parameters setup.
 *
 * Function to setup the I2S parameters.
 */
static void I2S_SetupParams(i2s_config_t *config)
{
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
    I2S_RxGetDefaultConfig(config);

    config->masterSlave = kI2S_MasterSlaveNormalSlave; /** Normal Slave */
    config->mode = kI2S_ModeDspWsShort; /** DSP mode, WS having one clock long pulse */
    config->dataLength = TO_BITS(I2S_CH_LEN_DATA);
    config->frameLength = TO_BITS(I2S_FRAME_LEN);
}

/*!
 * @brief I2S shared signals setup.
 *
 * Function to setup the I2S shared signals. Each I2S instance (controller) is
 * able to work with at maximum 8 channels. If we need more than 8 channels we
 * have to use multiple I2S instances sharing the same SCK, WS and SDIN lines.
 */
static void I2S_SetupSharedSignals(void)
{
    /* FLEXCOMM5 sharing SCK, WS and SDIN */
    I2S_BRIDGE_SetShareSignalSrc(kI2S_BRIDGE_ShareSet0, kI2S_BRIDGE_SignalSCK, kI2S_BRIDGE_Flexcomm5);
    I2S_BRIDGE_SetShareSignalSrc(kI2S_BRIDGE_ShareSet0, kI2S_BRIDGE_SignalWS, kI2S_BRIDGE_Flexcomm5);
    I2S_BRIDGE_SetShareSignalSrc(kI2S_BRIDGE_ShareSet0, kI2S_BRIDGE_SignalDataIn, kI2S_BRIDGE_Flexcomm5);

    /* FLEXCOMM7 getting SCK, WS and SDIN from FLEXCOMM5 */
    I2S_BRIDGE_SetFlexcommSignalShareSet(kI2S_BRIDGE_Flexcomm7, kI2S_BRIDGE_SignalSCK, kI2S_BRIDGE_ShareSet0);
    I2S_BRIDGE_SetFlexcommSignalShareSet(kI2S_BRIDGE_Flexcomm7, kI2S_BRIDGE_SignalWS, kI2S_BRIDGE_ShareSet0);
    I2S_BRIDGE_SetFlexcommSignalShareSet(kI2S_BRIDGE_Flexcomm7, kI2S_BRIDGE_SignalDataIn, kI2S_BRIDGE_ShareSet0);
}

/*!
 * @brief DMA channels setup.
 *
 * Function to setup the DMA channels (one for each I2S instance).
 */
static void DMA_SetupChannels(size_t inst)
{
    dma_handle_t *dmaHandle = &s_dmaRxHandle[inst];
    dma_priority_t dmaPrio = s_i2sRxDmaPrio[inst];
    uint32_t dmaChannel = s_i2sRxDmaChannel[inst];

    DMA_EnableChannel(DMA_RX, dmaChannel);
    DMA_SetChannelPriority(DMA_RX, dmaChannel, dmaPrio);
    DMA_CreateHandle(dmaHandle, DMA_RX, dmaChannel);
}

/*!
 * @brief I2S DMA setup.
 *
 * Function to setup I2S (with secondary channels) and DMA.
 */
static void I2S_DMA_Setup(size_t inst, i2s_config_t *config)
{
    dma_descriptor_t *dmaDescriptor = s_dmaRxDesc[inst];
    i2s_dma_handle_t *i2sDmaHandle = &s_i2sDmaRxHandle[inst];
    i2s_transfer_t *i2sTransfer = s_i2sRxTransfer[inst];
    dma_handle_t *dmaHandle = &s_dmaRxHandle[inst];
    uint8_t *i2sBuff = s_i2sRxBuff[inst];
    I2S_Type *i2sBase = s_i2sRxBase[inst];

    for (size_t buf = 0; buf < I2S_BUFF_NUM; buf++)
    {
        i2sTransfer[buf].data = &i2sBuff[buf * I2S_BUFF_SIZE];
        i2sTransfer[buf].dataSize = I2S_BUFF_SIZE;
    }

    config->position = (inst * TO_BITS(I2S_FRAME_LEN_PER_INST));

    I2S_RxInit(i2sBase, config);

    I2S_EnableSecondaryChannel(i2sBase, kI2S_SecondaryChannel1, false, CH_OFF((inst * I2S_FRAME_LEN_PER_INST), 1));
    I2S_EnableSecondaryChannel(i2sBase, kI2S_SecondaryChannel2, false, CH_OFF((inst * I2S_FRAME_LEN_PER_INST), 2));
    I2S_EnableSecondaryChannel(i2sBase, kI2S_SecondaryChannel3, false, CH_OFF((inst * I2S_FRAME_LEN_PER_INST), 3));

    I2S_RxTransferCreateHandleDMA(i2sBase, i2sDmaHandle, dmaHandle, RxCallback, (void *) i2sTransfer);
    I2S_TransferInstallLoopDMADescriptorMemory(i2sDmaHandle, dmaDescriptor, I2S_BUFF_NUM);
}

/*!
 * @brief I2S RX start.
 *
 * Function to start I2S RX. Assert on failure.
 */
static void I2S_StartRx(size_t inst)
{
    i2s_transfer_t *i2sTransfer = s_i2sRxTransfer[inst];
    i2s_dma_handle_t *i2sDmaHandle = &s_i2sDmaRxHandle[inst];
    I2S_Type *i2sBase = s_i2sRxBase[inst];

    if (I2S_TransferReceiveLoopDMA(i2sBase, i2sDmaHandle, i2sTransfer, I2S_BUFF_NUM) != kStatus_Success)
    {
        assert(false);
    }
}

/*!
 * @brief Board setup funcion for I2S.
 *
 * Entry point function for I2S and DMA setup.
 */
void BOARD_I2S_Init(void)
{
    i2s_config_t config = { 0 };

    I2S_SetupSharedSignals();
    I2S_SetupParams(&config);
    DMA_Init(DMA_RX);

    for (size_t inst = 0; inst < I2S_INST_NUM; inst++)
    {
        DMA_SetupChannels(inst);
        I2S_DMA_Setup(inst, &config);
    }

    for (size_t inst = 0; inst < I2S_INST_NUM; inst++)
    {
        I2S_StartRx(inst);
    }
}