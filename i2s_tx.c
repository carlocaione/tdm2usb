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

SDK_ALIGN(static dma_descriptor_t s_dmaTxDesc[I2S_INST_NUM][I2S_TX_BUFF_NUM], FSL_FEATURE_DMA_LINK_DESCRIPTOR_ALIGN_SIZE);
SDK_ALIGN(static uint8_t s_i2sTxBuff[I2S_INST_NUM][I2S_TX_BUFF_SIZE_PER_INST * I2S_TX_BUFF_NUM], sizeof(uint32_t));

static i2s_transfer_t s_i2sTxTransfer[I2S_INST_NUM][I2S_TX_BUFF_NUM];
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
            *pos = (*pos + I2S_FRAME_LEN_PER_INST) % (I2S_TX_BUFF_SIZE_PER_INST * I2S_TX_BUFF_NUM);
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

        I2S_TxTransferCreateHandleDMA(s_i2sTxBase[inst], &s_i2sDmaTxHandle[inst], &s_dmaTxHandle[inst], I2S_TxCallback, (void *) s_i2sTxTransfer[inst]);
        I2S_TransferInstallLoopDMADescriptorMemory(&s_i2sDmaTxHandle[inst], s_dmaTxDesc[inst], I2S_TX_BUFF_NUM);
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

    for (size_t inst = 0; inst < I2S_INST_NUM; inst++)
    {
        I2S_TransferSendLoopDMA(s_i2sTxBase[inst], &s_i2sDmaTxHandle[inst], s_i2sTxTransfer[inst], I2S_TX_BUFF_NUM);
    }
}
