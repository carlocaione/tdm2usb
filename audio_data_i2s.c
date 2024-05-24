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
USB_DMA_NONINIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE) uint8_t g_usbBuffIn[USB_MAX_PACKET_SIZE];
USB_DMA_NONINIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE) uint8_t g_usbBuffOut[USB_MAX_PACKET_SIZE];

/* RX */
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

SDK_ALIGN(static uint8_t s_i2sRxBuff[I2S_INST_NUM][I2S_BUFF_SIZE * I2S_BUFF_NUM], sizeof(uint32_t));

static i2s_transfer_t s_i2sRxTransfer[I2S_INST_NUM][I2S_BUFF_NUM];
static i2s_dma_handle_t s_i2sDmaRxHandle[I2S_INST_NUM];
static dma_handle_t s_dmaRxHandle[I2S_INST_NUM];
static uint32_t s_rxAudioPos[I2S_INST_NUM];

volatile unsigned int g_rxFirstInt = 0;
volatile uint8_t g_rxNextBufIndex = 0;

/* TX */
static I2S_Type *s_i2sTxBase[] = {
    I2S_TX_0,
    I2S_TX_1,
};

static uint32_t s_i2sTxDmaChannel[] = {
    I2S_TX_0_DMA_CH,
    I2S_TX_1_DMA_CH,
};

static dma_priority_t s_i2sTxDmaPrio[] = {
    I2S_TX_0_DMA_CH_PRIO,
    I2S_TX_1_DMA_CH_PRIO,
};

SDK_ALIGN(static dma_descriptor_t s_dmaTxDesc[I2S_INST_NUM][I2S_BUFF_NUM], FSL_FEATURE_DMA_LINK_DESCRIPTOR_ALIGN_SIZE);
SDK_ALIGN(static uint8_t s_i2sTxBuff[I2S_INST_NUM][I2S_BUFF_SIZE * I2S_BUFF_NUM], sizeof(uint32_t));

static i2s_transfer_t s_i2sTxTransfer[I2S_INST_NUM][I2S_BUFF_NUM];
static i2s_dma_handle_t s_i2sDmaTxHandle[I2S_INST_NUM];
static dma_handle_t s_dmaTxHandle[I2S_INST_NUM];
static uint32_t s_txAudioPos[I2S_INST_NUM];

/*******************************************************************************
 * Code
 ******************************************************************************/
/*!
 * @brief Audio wav data prepare function.
 *
 * This function prepare audio wav data before send through I2S.
 */

void USB_AudioUsb2I2sBuffer(uint8_t *usbBuffer, uint32_t size)
{
    assert(size % I2S_FRAME_LEN == 0);

    for (size_t k = 0; k < size; k += I2S_FRAME_LEN)
    {
        for (size_t inst = 0; inst < I2S_INST_NUM; inst++)
        {
            uint32_t *pos = &s_txAudioPos[inst];

            memcpy(&s_i2sTxBuff[inst][*pos], usbBuffer + k + (inst * I2S_FRAME_LEN_PER_INST), I2S_FRAME_LEN_PER_INST);
            *pos = (*pos + I2S_FRAME_LEN_PER_INST) % (I2S_BUFF_SIZE * I2S_BUFF_NUM);
        }
    }
}

/*!
 * @brief Audio wav data prepare function.
 *
 * This function prepare audio wav data before send through USB.
 */
void USB_AudioI2s2UsbBuffer(uint8_t *usbBuffer, uint32_t size)
{
    assert(size % I2S_FRAME_LEN == 0);

    if (g_rxFirstInt == 0)
    {
        bzero(usbBuffer, size);
        return;
    }

    for (size_t k = 0; k < size; k += I2S_FRAME_LEN)
    {
        for (size_t inst = 0; inst < I2S_INST_NUM; inst++)
        {
            uint32_t *pos = &s_rxAudioPos[inst];
#if USE_FILTER_32_DOWN
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
 * @brief I2S TX callback.
 */
static void I2S_TxCallback(I2S_Type *base, i2s_dma_handle_t *handle, status_t completionStatus, void *userData)
{
    __NOP();
}

/*!
 * @brief I2S RX start.
 */
static inline void I2S_RxStart(void)
{
    g_rxNextBufIndex = 0;
    g_rxFirstInt = 0;

    for (size_t inst = 0; inst < I2S_INST_NUM; inst++)
    {
        s_rxAudioPos[inst] = 0;
        bzero(s_i2sRxBuff[inst], I2S_BUFF_NUM * I2S_BUFF_SIZE);

        for (size_t buf = 0; buf < I2S_BUFF_NUM; buf++)
        {
            I2S_RxTransferReceiveDMA(s_i2sRxBase[inst], &s_i2sDmaRxHandle[inst], s_i2sRxTransfer[inst][buf]);
        }
    }
}

/*!
 * @brief I2S RX stop.
 */
static inline void I2S_RxStop(void)
{
    for (size_t inst = 0; inst < I2S_INST_NUM; inst++)
    {
        I2S_TransferAbortDMA(s_i2sRxBase[inst], &s_i2sDmaRxHandle[inst]);
    }
}

/*!
 * @brief Check for a previous reset of the I2S stream.
 */
static inline bool I2S_RxCheckReset(void)
{
    for (size_t inst = 0; inst < I2S_INST_NUM; inst++)
    {
        I2S_Type *b = s_i2sRxBase[inst];

        if (b->STAT & I2S_STAT_SLVFRMERR(1))
        {
            I2S_RxStop();

            b->STAT |= I2S_STAT_SLVFRMERR(1);

            I2S_RxStart();

            return true;
        }
    }

    return false;
}

/*!
 * @brief I2S RX callback.
 *
 * This function is called when at least one of the ping-pong buffers is full.
 */
static void I2S_RxCallback(I2S_Type *base, i2s_dma_handle_t *handle, status_t completionStatus, void *userData)
{
    if (I2S_RxCheckReset())
    {
        return;
    }

    for (size_t inst = 0; inst < I2S_INST_NUM; inst++)
    {
        I2S_RxTransferReceiveDMA(s_i2sRxBase[inst], &s_i2sDmaRxHandle[inst], s_i2sRxTransfer[inst][g_rxNextBufIndex]);
    }

    g_rxNextBufIndex = ((g_rxNextBufIndex + 1) % I2S_BUFF_NUM);

    if (g_rxFirstInt == 0)
    {
        g_rxFirstInt = 1;
    }
}

/*!
 * @brief I2S parameters setup.
 *
 * Function to setup the I2S parameters.
 */
static void I2S_SetupParams(i2s_config_t *rxConfig, i2s_config_t *txConfig)
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
    I2S_RxGetDefaultConfig(rxConfig);

    rxConfig->masterSlave = kI2S_MasterSlaveNormalSlave; /** Normal Slave */
    rxConfig->mode = kI2S_ModeDspWsShort; /** DSP mode, WS having one clock long pulse */
    rxConfig->dataLength = TO_BITS(I2S_CH_LEN_DATA);
    rxConfig->frameLength = TO_BITS(I2S_FRAME_LEN);

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
     *   config->txEmptyZero = true;
     *   config->pack48 = false;
     */
    I2S_TxGetDefaultConfig(txConfig);

    txConfig->masterSlave = kI2S_MasterSlaveNormalSlave; /** Normal Slave */
    txConfig->mode = kI2S_ModeDspWsShort; /** DSP mode, WS having one clock long pulse */
    txConfig->dataLength = TO_BITS(I2S_CH_LEN_DATA);
    txConfig->frameLength = TO_BITS(I2S_FRAME_LEN);
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
    /* [RX] FLEXCOMM5 sharing SCK, WS and SDIN */
    I2S_BRIDGE_SetShareSignalSrc(kI2S_BRIDGE_ShareSet0, kI2S_BRIDGE_SignalSCK, kI2S_BRIDGE_Flexcomm5);
    I2S_BRIDGE_SetShareSignalSrc(kI2S_BRIDGE_ShareSet0, kI2S_BRIDGE_SignalWS, kI2S_BRIDGE_Flexcomm5);
    I2S_BRIDGE_SetShareSignalSrc(kI2S_BRIDGE_ShareSet0, kI2S_BRIDGE_SignalDataIn, kI2S_BRIDGE_Flexcomm5);

    /* [RX] FLEXCOMM7 getting SCK, WS and SDIN from FLEXCOMM5 */
    I2S_BRIDGE_SetFlexcommSignalShareSet(kI2S_BRIDGE_Flexcomm7, kI2S_BRIDGE_SignalSCK, kI2S_BRIDGE_ShareSet0);
    I2S_BRIDGE_SetFlexcommSignalShareSet(kI2S_BRIDGE_Flexcomm7, kI2S_BRIDGE_SignalWS, kI2S_BRIDGE_ShareSet0);
    I2S_BRIDGE_SetFlexcommSignalShareSet(kI2S_BRIDGE_Flexcomm7, kI2S_BRIDGE_SignalDataIn, kI2S_BRIDGE_ShareSet0);

    /* [TX] FLEXCOMM4 getting SCK and WS from FLEXCOMM5 */
    I2S_BRIDGE_SetFlexcommSignalShareSet(kI2S_BRIDGE_Flexcomm4, kI2S_BRIDGE_SignalSCK, kI2S_BRIDGE_ShareSet0);
    I2S_BRIDGE_SetFlexcommSignalShareSet(kI2S_BRIDGE_Flexcomm4, kI2S_BRIDGE_SignalWS, kI2S_BRIDGE_ShareSet0);

    /* [TX] FLEXCOMM6 getting SCK and WS from FLEXCOMM5 */
    I2S_BRIDGE_SetFlexcommSignalShareSet(kI2S_BRIDGE_Flexcomm6, kI2S_BRIDGE_SignalSCK, kI2S_BRIDGE_ShareSet0);
    I2S_BRIDGE_SetFlexcommSignalShareSet(kI2S_BRIDGE_Flexcomm6, kI2S_BRIDGE_SignalWS, kI2S_BRIDGE_ShareSet0);

    /* [TX] FLEXCOMM6 and FLEXCOMM4 share the same SDOUT line */
    I2S_BRIDGE_SetShareSignalSrc(kI2S_BRIDGE_ShareSet1, kI2S_BRIDGE_SignalDataOut, kI2S_BRIDGE_Flexcomm4);
    I2S_BRIDGE_SetShareSignalSrc(kI2S_BRIDGE_ShareSet1, kI2S_BRIDGE_SignalDataOut, kI2S_BRIDGE_Flexcomm6);

    /* [TX] SDOUT is from FLEXCOMM4 connector */
    I2S_BRIDGE_SetFlexcommSignalShareSet(kI2S_BRIDGE_Flexcomm4, kI2S_BRIDGE_SignalDataOut, kI2S_BRIDGE_ShareSet1);
}

/*!
 * @brief DMA channels setup.
 *
 * Function to setup the DMA channels (one for each I2S instance).
 */
static void DMA_SetupChannels(void)
{
    /* RX */
    for (size_t inst = 0; inst < I2S_INST_NUM; inst++)
    {
        DMA_EnableChannel(DMA, s_i2sRxDmaChannel[inst]);
        DMA_SetChannelPriority(DMA, s_i2sRxDmaChannel[inst], s_i2sRxDmaPrio[inst]);
        DMA_CreateHandle(&s_dmaRxHandle[inst], DMA, s_i2sRxDmaChannel[inst]);
    }

    /* TX */
    for (size_t inst = 0; inst < I2S_INST_NUM; inst++)
    {
        DMA_EnableChannel(DMA, s_i2sTxDmaChannel[inst]);
        DMA_SetChannelPriority(DMA, s_i2sTxDmaChannel[inst], s_i2sTxDmaPrio[inst]);
        DMA_CreateHandle(&s_dmaTxHandle[inst], DMA, s_i2sTxDmaChannel[inst]);
    }
}

/*!
 * @brief I2S DMA setup.
 *
 * Function to setup I2S (with secondary channels) and DMA.
 */
static void I2S_DMA_Setup(i2s_config_t *rxConfig, i2s_config_t *txConfig)
{
    /* RX */
    for (size_t inst = 0; inst < I2S_INST_NUM; inst++)
    {
        for (size_t buf = 0; buf < I2S_BUFF_NUM; buf++)
        {
            s_i2sRxTransfer[inst][buf].data = &s_i2sRxBuff[inst][buf * I2S_BUFF_SIZE];
            s_i2sRxTransfer[inst][buf].dataSize = I2S_BUFF_SIZE;
        }

        rxConfig->position = (inst * TO_BITS(I2S_FRAME_LEN_PER_INST));

        I2S_RxInit(s_i2sRxBase[inst], rxConfig);

        I2S_EnableSecondaryChannel(s_i2sRxBase[inst], kI2S_SecondaryChannel1, false, CH_OFF((inst * I2S_FRAME_LEN_PER_INST), 1));
        I2S_EnableSecondaryChannel(s_i2sRxBase[inst], kI2S_SecondaryChannel2, false, CH_OFF((inst * I2S_FRAME_LEN_PER_INST), 2));
        I2S_EnableSecondaryChannel(s_i2sRxBase[inst], kI2S_SecondaryChannel3, false, CH_OFF((inst * I2S_FRAME_LEN_PER_INST), 3));

        if (inst == (I2S_INST_NUM - 1))
        {
            I2S_RxTransferCreateHandleDMA(s_i2sRxBase[inst], &s_i2sDmaRxHandle[inst], &s_dmaRxHandle[inst], I2S_RxCallback, (void *) inst);
        }
        else
        {
            I2S_RxTransferCreateHandleDMA(s_i2sRxBase[inst], &s_i2sDmaRxHandle[inst], &s_dmaRxHandle[inst], NULL, NULL);
        }
    }

    /* TX */
    for (size_t inst = 0; inst < I2S_INST_NUM; inst++)
    {
        for (size_t buf = 0; buf < I2S_BUFF_NUM; buf++)
        {
            s_i2sTxTransfer[inst][buf].data = &s_i2sTxBuff[inst][buf * I2S_BUFF_SIZE];
            s_i2sTxTransfer[inst][buf].dataSize = I2S_BUFF_SIZE;
        }

        txConfig->position = (inst * TO_BITS(I2S_FRAME_LEN_PER_INST));

        I2S_TxInit(s_i2sTxBase[inst], txConfig);

        I2S_EnableSecondaryChannel(s_i2sTxBase[inst], kI2S_SecondaryChannel1, false, CH_OFF((inst * I2S_FRAME_LEN_PER_INST), 1));
        I2S_EnableSecondaryChannel(s_i2sTxBase[inst], kI2S_SecondaryChannel2, false, CH_OFF((inst * I2S_FRAME_LEN_PER_INST), 2));
        I2S_EnableSecondaryChannel(s_i2sTxBase[inst], kI2S_SecondaryChannel3, false, CH_OFF((inst * I2S_FRAME_LEN_PER_INST), 3));

        I2S_TxTransferCreateHandleDMA(s_i2sTxBase[inst], &s_i2sDmaTxHandle[inst], &s_dmaTxHandle[inst], I2S_TxCallback, (void *) s_i2sTxTransfer[inst]);
        I2S_TransferInstallLoopDMADescriptorMemory(&s_i2sDmaTxHandle[inst], s_dmaTxDesc[inst], I2S_BUFF_NUM);
    }
}

/*!
 * @brief Board setup funcion for I2S.
 *
 * Entry point function for I2S and DMA setup.
 */
void BOARD_I2S_Init(void)
{
    i2s_config_t rxConfig = { 0 };
    i2s_config_t txConfig = { 0 };

    I2S_SetupSharedSignals();
    I2S_SetupParams(&rxConfig, &txConfig);

    DMA_Init(DMA);
    DMA_SetupChannels();

    I2S_DMA_Setup(&rxConfig, &txConfig);

    I2S_RxStart();

    for (size_t inst = 0; inst < I2S_INST_NUM; inst++)
    {
        I2S_TransferSendLoopDMA(s_i2sTxBase[inst], &s_i2sDmaTxHandle[inst], s_i2sTxTransfer[inst], I2S_BUFF_NUM);
    }
}
