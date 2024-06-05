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

#include "i2s.h"
#include "i2s_tx.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define AUDIO_UPDATE_FEEDBACK_DATA(s, m, n)               \
    if (USB_SPEED_HIGH == (s))                            \
    {                                                     \
        m[0] = (((n & 0x00001FFFu) << 3) & 0xFFu);        \
        m[1] = ((((n & 0x00001FFFu) << 3) >> 8) & 0xFFu); \
        m[2] = (((n & 0x01FFE000u) >> 13) & 0xFFu);       \
        m[3] = (((n & 0x01FFE000u) >> 21) & 0xFFu);       \
    }                                                     \
    else                                                  \
    {                                                     \
        m[0] = ((n << 4) & 0xFFU);                        \
        m[1] = (((n << 4) >> 8U) & 0xFFU);                \
        m[2] = (((n << 4) >> 16U) & 0xFFU);               \
    }                                                     \

#define I2S_TX_FEEDBACK_TH_UP       (9U)
#define I2S_TX_FEEDBACK_TH_DOWN     (4U)

#define I2S_TX_FEEDBACK_TH_STEP     (1U)
#define I2S_TX_FEEDBACK_NORMAL      \
    ((HS_ISO_OUT_ENDP_PACKET_SIZE / (AUDIO_FORMAT_CHANNELS * AUDIO_FORMAT_SIZE)) << 13)

/*******************************************************************************
 * Variables
 ******************************************************************************/
USB_DMA_NONINIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE) uint8_t g_usbBuffOut[USB_MAX_PACKET_OUT_SIZE];

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

SDK_ALIGN(static uint8_t s_i2sTxBuff[I2S_INST_NUM][I2S_TX_BUFF_SIZE_PER_INST * I2S_TX_BUFF_NUM], sizeof(uint32_t));

static i2s_transfer_t s_i2sTxTransfer[I2S_INST_NUM][I2S_TX_BUFF_NUM];
static i2s_dma_handle_t s_i2sDmaTxHandle[I2S_INST_NUM];
static dma_handle_t s_dmaTxHandle[I2S_INST_NUM];
static uint32_t s_txAudioPos[I2S_INST_NUM];

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
static struct {
    volatile uint8_t vs_txNextBufIndex;
    volatile uint8_t vs_txI2sStarted;
    volatile uint8_t vs_txUsbStarted;
    volatile uint8_t vs_txDataIsValid;
    volatile uint64_t vs_txReadDataCount;
    volatile uint64_t vs_txWriteDataCount;
    volatile uint32_t vs_txFeedback;
} usb_ctx;

USB_RAM_ADDRESS_ALIGNMENT(4) uint8_t audioFeedBackBuffer[4];

/*******************************************************************************
 * Code
 ******************************************************************************/
/*!
 * @brief Function to print debug info
 */
void USB_OutPrintInfo(void)
{
    uint32_t diff;

    diff = (usb_ctx.vs_txWriteDataCount - usb_ctx.vs_txReadDataCount) / HS_ISO_OUT_ENDP_PACKET_SIZE;
    usb_echo("diff: %ld, feedback: 0x%08x\n\r", diff, usb_ctx.vs_txFeedback);
}

/*!
 * @brief Function to retrieve the feedback value
 *
 * This function is called by the USB core code to retrieve the feedback value to
 * send through the feedback endpoint.
 */
uint32_t USB_GetFeedback(uint8_t speed)
{
    AUDIO_UPDATE_FEEDBACK_DATA(speed, audioFeedBackBuffer, usb_ctx.vs_txFeedback);

    return *((uint32_t *) &audioFeedBackBuffer[0]);
}

/*!
 * @brief Logic to set the feedback data endpoint
 */
void static inline USB_SetFeedback(void)
{
    uint32_t diff;

    /**
     * Logic here is based on the size of data present in the DMA buffers. That
     * value is converted to the number of regular USB packets and decisions are
     * taken considering and upper and lower threshold.
     */
    diff = (usb_ctx.vs_txWriteDataCount - usb_ctx.vs_txReadDataCount) / HS_ISO_OUT_ENDP_PACKET_SIZE;

    if (diff >= I2S_TX_FEEDBACK_TH_UP)
    {
        /* We need to slow down */
        usb_ctx.vs_txFeedback = I2S_TX_FEEDBACK_NORMAL - I2S_TX_FEEDBACK_TH_STEP;
    }
    else if (diff <= I2S_TX_FEEDBACK_TH_DOWN)
    {
        /* We need to speed up */
        usb_ctx.vs_txFeedback = I2S_TX_FEEDBACK_NORMAL + I2S_TX_FEEDBACK_TH_STEP;
    }
    else
    {
        /* We are just fine */
        usb_ctx.vs_txFeedback = I2S_TX_FEEDBACK_NORMAL;
    }
}

/*!
 * @brief Audio wav data prepare function.
 *
 * This function prepare audio wav data before send through I2S.
 */
void USB_AudioUsb2I2sBuffer(uint8_t *usbBuffer, uint32_t size)
{
    assert(size % I2S_FRAME_LEN == 0);

    /**
     * This is tricky but we we cannot start the I2S TX path and start gathering
     * USB data at the same time. The problem is that it takes a while for the
     * I2S controller to ramp-up and start sending data, so much that while we
     * get the first interrupt we have already rolled over with the data coming
     * from USB.
     *
     * What we do instead is we start the I2S TX path and we wait until the I2S
     * TX pointers are in the middle of the DMA buffers before starting the
     * actual data gathering from USB. In theory assuming the same rate of
     * production and consumption by the time the TX pointers are pointing to
     * the first buffer, we already have filled the half the buffers with data.
     */
    if ((usb_ctx.vs_txUsbStarted == 0) && (usb_ctx.vs_txNextBufIndex != (I2S_TX_BUFF_NUM / 2) + 1))
    {
        return;
    }

    usb_ctx.vs_txUsbStarted = 1;

    for (size_t k = 0; k < size; k += I2S_FRAME_LEN)
    {
        for (size_t inst = 0; inst < I2S_INST_NUM; inst++)
        {
            uint32_t *pos = &s_txAudioPos[inst];

            memcpy(&s_i2sTxBuff[inst][*pos], usbBuffer + k + (inst * I2S_FRAME_LEN_PER_INST), I2S_FRAME_LEN_PER_INST);
            *pos = (*pos + I2S_FRAME_LEN_PER_INST) % (I2S_TX_BUFF_SIZE_PER_INST * I2S_TX_BUFF_NUM);
        }
    }

    usb_ctx.vs_txWriteDataCount += size;

    USB_SetFeedback();
}

/*!
 * @brief I2S TX callback.
 */
static void I2S_TxCallback(I2S_Type *base, i2s_dma_handle_t *handle, status_t completionStatus, void *userData)
{
    for (size_t inst = 0; inst < I2S_INST_NUM; inst++)
    {
        I2S_TxTransferSendDMA(s_i2sTxBase[inst], &s_i2sDmaTxHandle[inst], s_i2sTxTransfer[inst][usb_ctx.vs_txNextBufIndex]);
    }

    usb_ctx.vs_txNextBufIndex = ((usb_ctx.vs_txNextBufIndex + 1) % I2S_TX_BUFF_NUM);

    /**
     * We do not consider data in the buffer to be valid until:
     *
     * - It was already valid before
     *
     * - The USB gathering phase is started (see comment in
     *   USB_AudioUsb2I2sBuffer()) and we are pointing to the start of the DMA
     *   buffers.
     */
    if (usb_ctx.vs_txDataIsValid == 1)
    {
        usb_ctx.vs_txReadDataCount += I2S_TX_BUFF_SIZE;
    }
    else if ((usb_ctx.vs_txUsbStarted == 1) && (usb_ctx.vs_txNextBufIndex == 0))
    {
        usb_ctx.vs_txDataIsValid = 1;
    }
}

/*!
 * @brief I2S TX stop.
 */
void I2S_TxStop(void)
{
    usb_ctx.vs_txI2sStarted = 0;

    for (size_t inst = 0; inst < I2S_INST_NUM; inst++)
    {
        I2S_TransferAbortDMA(s_i2sTxBase[inst], &s_i2sDmaTxHandle[inst]);
    }
}

/*!
 * @brief I2S TX start.
 */
 void I2S_TxStart(void)
{
    usb_ctx.vs_txNextBufIndex = 0;
    usb_ctx.vs_txReadDataCount = 0;
    usb_ctx.vs_txDataIsValid = 0;
    usb_ctx.vs_txUsbStarted = 0;

    usb_ctx.vs_txReadDataCount = 0;
    usb_ctx.vs_txWriteDataCount = 0;

    usb_ctx.vs_txFeedback = I2S_TX_FEEDBACK_NORMAL;

    for (size_t inst = 0; inst < I2S_INST_NUM; inst++)
    {
        s_txAudioPos[inst] = 0;
        bzero(s_i2sTxBuff[inst], I2S_TX_BUFF_NUM * I2S_TX_BUFF_SIZE_PER_INST);
    }

    for (size_t inst = 0; inst < I2S_INST_NUM; inst++)
    {
        for (size_t buf = 0; buf < I2S_TX_BUFF_NUM; buf++)
        {
            I2S_TxTransferSendDMA(s_i2sTxBase[inst], &s_i2sDmaTxHandle[inst], s_i2sTxTransfer[inst][buf]);
        }
    }

    usb_ctx.vs_txI2sStarted = 1;
}

/*!
 * @brief I2S parameters setup.
 *
 * Function to setup the I2S parameters.
 */
static void I2S_TxSetupParams(i2s_config_t *txConfig)
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
 * @brief DMA channels setup.
 *
 * Function to setup the DMA channels (one for each I2S instance).
 */
static void DMA_TxSetupChannels(void)
{
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
static void I2S_DMA_TxSetup(i2s_config_t *txConfig)
{
    for (size_t inst = 0; inst < I2S_INST_NUM; inst++)
    {
        for (size_t buf = 0; buf < I2S_TX_BUFF_NUM; buf++)
        {
            s_i2sTxTransfer[inst][buf].data = &s_i2sTxBuff[inst][buf * I2S_TX_BUFF_SIZE_PER_INST];
            s_i2sTxTransfer[inst][buf].dataSize = I2S_TX_BUFF_SIZE_PER_INST;
        }

        txConfig->position = (inst * TO_BITS(I2S_FRAME_LEN_PER_INST));

        I2S_TxInit(s_i2sTxBase[inst], txConfig);

        I2S_EnableSecondaryChannel(s_i2sTxBase[inst], kI2S_SecondaryChannel1, false, CH_OFF((inst * I2S_FRAME_LEN_PER_INST), 1));
        I2S_EnableSecondaryChannel(s_i2sTxBase[inst], kI2S_SecondaryChannel2, false, CH_OFF((inst * I2S_FRAME_LEN_PER_INST), 2));
        I2S_EnableSecondaryChannel(s_i2sTxBase[inst], kI2S_SecondaryChannel3, false, CH_OFF((inst * I2S_FRAME_LEN_PER_INST), 3));

        if (inst == (I2S_INST_NUM - 1))
        {
            I2S_TxTransferCreateHandleDMA(s_i2sTxBase[inst], &s_i2sDmaTxHandle[inst], &s_dmaTxHandle[inst], I2S_TxCallback, (void *) s_i2sTxTransfer[inst]);
        }
        else
        {
            I2S_TxTransferCreateHandleDMA(s_i2sTxBase[inst], &s_i2sDmaTxHandle[inst], &s_dmaTxHandle[inst], NULL, NULL);
        }
    }
}

/*!
 * @brief Board setup funcion for I2S.
 *
 * Entry point function for I2S and DMA setup.
 */
void BOARD_I2S_TxInit(void)
{
    i2s_config_t txConfig = { 0 };

    I2S_TxSetupParams(&txConfig);
    DMA_TxSetupChannels();
    I2S_DMA_TxSetup(&txConfig);
}
