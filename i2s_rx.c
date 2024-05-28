/*
 * Copyright (c) 2023, Carlo Caione <ccaione@baylibre.com>
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

// TODO: Manage reset
// TODO: The endpoint should advertise a wMaxPacketSize of 448 (6+1 frames) to accomodate for adjustments (+/- 1)
// TODO: Use a context struct to hold the vs_* variables (maybe)
// TODO: We should only stay +/- 1 with respect to the 6 frames, not going lower than that

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
#include "i2s_rx.h"

/**
 * Some considerations about the channels offsetting.
 *
 * The basic idea is that we start from scratch (that means I2S filling the DMA
 * buffers from scratch, waiting for them to be half-filled before streaming out
 * through USB, etc..) every time we start either I2S or USB.
 *
 *  I2S) On the I2S side this is guaranteed by I2S_RxCheckReset() that is resetting
 *       the RX path every time a new I2S streaming is started after having been
 *       previously stopped.
 *
 *  USB) On the USB side this is guaranteed by calls to I2S_RxStart() /
 *       I2S_RxStop() when the streaming interface is opened / closed.
 *
 * So in general every time we have USB and I2S sides running at the same time
 * we can be sure to have started from a known starting point (reset).
 *
 * Some considerations about the SCK persistence (or not) when the I2S streaming
 * is interrupted.
 *
 *  A) The SCK is stopped. In this case we stop receveing RX callbacks and the
 *     DMA buffers dry out. The USB callbacks deals with not having enough samples
 *     to send by sending zeroed packets.
 *
 *  B) The SCK is not stopped. In this case the streaming keeps going as usual but
 *      we are filling the DMA buffers with zeros.
 */

/*******************************************************************************
 * Definitions
 ******************************************************************************/
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

SDK_ALIGN(static uint8_t s_i2sRxBuff[I2S_INST_NUM][I2S_BUFF_SIZE_PER_INST * I2S_BUFF_NUM], sizeof(uint32_t));

static i2s_transfer_t s_i2sRxTransfer[I2S_INST_NUM][I2S_BUFF_NUM];
static i2s_dma_handle_t s_i2sDmaRxHandle[I2S_INST_NUM];
static dma_handle_t s_dmaRxHandle[I2S_INST_NUM];
static uint32_t s_rxAudioPos[I2S_INST_NUM];

volatile static uint8_t vs_rxNextBufIndex = 0;
volatile static uint32_t vs_rxRecvSize = 0;
volatile static uint8_t vs_rxFirstInt = 0;
volatile static uint8_t vs_rxFirstGet = 0;

/*******************************************************************************
 * Code
 ******************************************************************************/
/*!
 * @brief Audio wav data prepare function.
 *
 * This function prepare audio wav data before send through USB.
 */
uint32_t USB_AudioI2s2UsbBuffer(uint8_t *usbBuffer, uint32_t size)
{
    assert(size % I2S_FRAME_LEN == 0);

    /**
     * This should always be true. If this asserts that means that the I2S is
     * RX-ing data that the USB is not picking up (DMA buffer overflow).
     */
    assert(vs_rxRecvSize != (I2S_BUFF_NUM * I2S_BUFF_SIZE));

    if (vs_rxFirstGet == 0)
    {
        /**
         * We bail out in two cases:
         *
         * 1. If the DMA buffers are not half full yet (this usually happens if we
         *    start the USB recv before the I2S side so the USB side must wait for
         *    the I2S buffers to be filled up).
         *
         * 2. If the DMA buffers are half full already but we wait for the RX
         *    pointer to be pointing to the middle of the RX DMA buffers before
         *    starting (this usually happens when we start I2S RX before USB
         *    streaming or when the USB is a bit slow to ramp-up and I2S is catching
         *    up too quickly)
         */
        if ((vs_rxFirstInt == 0) || (vs_rxNextBufIndex != (I2S_BUFF_NUM / 2)))
        {
            return size;
        }

        /**
         * We reset the recv counter in case we alrady rolled over. This usually
         * happens if we start the I2S RX before the USB.
         */
        vs_rxRecvSize = (I2S_BUFF_NUM / 2) * I2S_BUFF_SIZE;
        vs_rxFirstGet = 1;
    }

    /**
     * This is the case when we are running out of I2S data to stream out
     * because we are sending data through USB at a faster rate than we are able
     * to read those from I2S (for example when we kill the I2S RX side).
     *
     * NOTE: a corner case is when we stop the I2S data sending, for this case
     *       we have to differentiate between the SCK being killed or not.
     *
     *       A) If the SCK is not being killed, we basically keep receiving zeroed
     *          data from I2S, so we never reach the size == 0 condition.
     *
     *       B) If the SCK is being killed, we run out of I2S data so the size == 0
     *          condition is interpreted as a Transfer Delimiter and this is
     *          killing the receiving side (usually `arecord`) with an
     *          Input/Output error.
     */
    size = MIN(size, vs_rxRecvSize);

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
            *pos = (*pos + I2S_FRAME_LEN_PER_INST) % (I2S_BUFF_SIZE_PER_INST * I2S_BUFF_NUM);
        }
    }

    vs_rxRecvSize -= size;

    return size;
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
 * This function is called when at least one of the ping-pong buffers is full (I2S_BUFF_SIZE bytes).
 */
static void I2S_RxCallback(I2S_Type *base, i2s_dma_handle_t *handle, status_t completionStatus, void *userData)
{
    if (I2S_RxCheckReset())
    {
        return;
    }

    for (size_t inst = 0; inst < I2S_INST_NUM; inst++)
    {
        I2S_RxTransferReceiveDMA(s_i2sRxBase[inst], &s_i2sDmaRxHandle[inst], s_i2sRxTransfer[inst][vs_rxNextBufIndex]);
    }

    vs_rxNextBufIndex = ((vs_rxNextBufIndex + 1) % I2S_BUFF_NUM);

    /**
     * We start the USB data sending only when at least half of the DMA buffers
     * are full. We also reset the recv counter in case we already rolled over.
     */
    if ((vs_rxFirstInt == 0) && (vs_rxNextBufIndex == (I2S_BUFF_NUM / 2)))
    {
        vs_rxRecvSize = (I2S_BUFF_NUM / 2) * I2S_BUFF_SIZE;
        vs_rxFirstInt = 1;

        return;
    }

    vs_rxRecvSize = MIN((vs_rxRecvSize + I2S_BUFF_SIZE), (I2S_BUFF_SIZE * I2S_BUFF_NUM));
}

/*!
 * @brief I2S RX stop.
 */
void I2S_RxStop(void)
{
    for (size_t inst = 0; inst < I2S_INST_NUM; inst++)
    {
        I2S_TransferAbortDMA(s_i2sRxBase[inst], &s_i2sDmaRxHandle[inst]);
    }

    vs_rxNextBufIndex = 0;
    vs_rxFirstInt = 0;
    vs_rxFirstGet = 0;
    vs_rxRecvSize = 0;

    for (size_t inst = 0; inst < I2S_INST_NUM; inst++)
    {
        s_rxAudioPos[inst] = 0;
        bzero(s_i2sRxBuff[inst], I2S_BUFF_NUM * I2S_BUFF_SIZE_PER_INST);
    }
}

/*!
 * @brief I2S RX start.
 */
 void I2S_RxStart(void)
{
    for (size_t inst = 0; inst < I2S_INST_NUM; inst++)
    {
        for (size_t buf = 0; buf < I2S_BUFF_NUM; buf++)
        {
            I2S_RxTransferReceiveDMA(s_i2sRxBase[inst], &s_i2sDmaRxHandle[inst], s_i2sRxTransfer[inst][buf]);
        }
    }
}

/*!
 * @brief I2S parameters setup.
 *
 * Function to setup the I2S parameters.
 */
static void I2S_RxSetupParams(i2s_config_t *rxConfig)
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
}

/*!
 * @brief DMA channels setup.
 *
 * Function to setup the DMA channel.
 */
static void DMA_RxSetupChannels(void)
{
    for (size_t inst = 0; inst < I2S_INST_NUM; inst++)
    {
        DMA_EnableChannel(DMA, s_i2sRxDmaChannel[inst]);
        DMA_SetChannelPriority(DMA, s_i2sRxDmaChannel[inst], s_i2sRxDmaPrio[inst]);
        DMA_CreateHandle(&s_dmaRxHandle[inst], DMA, s_i2sRxDmaChannel[inst]);
    }
}

/*!
 * @brief I2S DMA setup.
 *
 * Function to setup I2S (with secondary channels) and DMA.
 */
static void I2S_DMA_RxSetup(i2s_config_t *rxConfig)
{
    for (size_t inst = 0; inst < I2S_INST_NUM; inst++)
    {
        for (size_t buf = 0; buf < I2S_BUFF_NUM; buf++)
        {
            s_i2sRxTransfer[inst][buf].data = &s_i2sRxBuff[inst][buf * I2S_BUFF_SIZE_PER_INST];
            s_i2sRxTransfer[inst][buf].dataSize = I2S_BUFF_SIZE_PER_INST;
        }

        rxConfig->position = (inst * TO_BITS(I2S_FRAME_LEN_PER_INST));

        I2S_RxInit(s_i2sRxBase[inst], rxConfig);

        I2S_EnableSecondaryChannel(s_i2sRxBase[inst], kI2S_SecondaryChannel1, false, CH_OFF((inst * I2S_FRAME_LEN_PER_INST), 1));
        I2S_EnableSecondaryChannel(s_i2sRxBase[inst], kI2S_SecondaryChannel2, false, CH_OFF((inst * I2S_FRAME_LEN_PER_INST), 2));
        I2S_EnableSecondaryChannel(s_i2sRxBase[inst], kI2S_SecondaryChannel3, false, CH_OFF((inst * I2S_FRAME_LEN_PER_INST), 3));

        /**
         * Install the callback only on the latest instance so every time the
         * callback is called we are sure to have gathered the whole final frame
         * and not just half of it.
         *
         * When the callback is called we have received I2S_BUFF_SIZE bytes from
         * the two instances.
         */
        if (inst == (I2S_INST_NUM - 1))
        {
            I2S_RxTransferCreateHandleDMA(s_i2sRxBase[inst], &s_i2sDmaRxHandle[inst], &s_dmaRxHandle[inst], I2S_RxCallback, (void *) inst);
        }
        else
        {
            I2S_RxTransferCreateHandleDMA(s_i2sRxBase[inst], &s_i2sDmaRxHandle[inst], &s_dmaRxHandle[inst], NULL, NULL);
        }
    }
}

/*!
 * @brief Board setup funcion for I2S.
 *
 * Entry point function for I2S and DMA setup.
 */
void BOARD_I2S_RxInit(void)
{
    i2s_config_t rxConfig = { 0 };

    I2S_RxSetupParams(&rxConfig);
    DMA_RxSetupChannels();
    I2S_DMA_RxSetup(&rxConfig);
}
