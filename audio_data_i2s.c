/*
 * Copyright (c) 2016, Freescale Semiconductor, Inc.
 * Copyright 2016,2019 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fsl_dma.h"
#include "fsl_i2s.h"
#include "fsl_i2s_dma.h"
#include "usb_device_config.h"
#include "usb.h"
#include "usb_device.h"
#include "usb_device_class.h"
#include "usb_audio_config.h"
#include "usb_device_descriptor.h"
#include "fsl_device_registers.h"
#include "audio_data_i2s.h"
/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define BUFFER_SIZE (600U)
#define BUFFER_NUM  (2U)

#if defined(USB_DEVICE_CONFIG_EHCI) && (USB_DEVICE_CONFIG_EHCI > 0U) || \
    (defined(USB_DEVICE_CONFIG_LPCIP3511HS) && (USB_DEVICE_CONFIG_LPCIP3511HS > 0U))
#define AUDIO_ENDPOINT_MAX_PACKET_SIZE \
    (FS_ISO_IN_ENDP_PACKET_SIZE > HS_ISO_IN_ENDP_PACKET_SIZE ? FS_ISO_IN_ENDP_PACKET_SIZE : HS_ISO_IN_ENDP_PACKET_SIZE)
#endif

#if defined(USB_DEVICE_CONFIG_KHCI) && (USB_DEVICE_CONFIG_KHCI > 0U)
#define AUDIO_ENDPOINT_MAX_PACKET_SIZE (FS_ISO_IN_ENDP_PACKET_SIZE)
#endif

#if defined(USB_DEVICE_CONFIG_LPCIP3511FS) && (USB_DEVICE_CONFIG_LPCIP3511FS > 0U)
#define AUDIO_ENDPOINT_MAX_PACKET_SIZE (FS_ISO_IN_ENDP_PACKET_SIZE)
#endif
/*******************************************************************************
 * Variables
 ******************************************************************************/
volatile unsigned int first_int = 0;

USB_DMA_NONINIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE) uint8_t s_wavBuff[AUDIO_ENDPOINT_MAX_PACKET_SIZE];
uint32_t audioPosition = 0U;

USB_DMA_NONINIT_DATA_ALIGN(4) static uint8_t s_buffer[BUFFER_SIZE * BUFFER_NUM];

static i2s_dma_handle_t s_i2sDmaHandle;
static dma_handle_t s_i2sRxDmaHandle;
SDK_ALIGN(static dma_descriptor_t s_rxDmaDescriptors[BUFFER_NUM], FSL_FEATURE_DMA_LINK_DESCRIPTOR_ALIGN_SIZE);

static i2s_transfer_t s_receiveXfer[BUFFER_NUM] = {
    {
        .data = s_buffer,
        .dataSize = BUFFER_SIZE,
    },
    {
        .data = &s_buffer[BUFFER_SIZE],
        .dataSize = BUFFER_SIZE,
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
    uint8_t k;
    /* copy audio wav data from flash to buffer */
    for (k = 0U; k < size; k++)
    {
        if (audioPosition > (BUFFER_SIZE * BUFFER_NUM - 1U))
        {
            audioPosition = 0U;
        }
        *(buffer + k) = s_buffer[audioPosition];
        audioPosition++;
    }
}

/*!
 * @brief Application task function.
 *
 * This function runs the task for application.
 *
 * @return None.
 */
void i2s_Callback(I2S_Type *base, i2s_dma_handle_t *handle, status_t status, void *userData)
{
    if (first_int == 0U)
    {
        audioPosition = 0U;
        first_int     = 1U;
    }
}

void Board_I2S_DMA_Init(void)
{
    i2s_config_t i2s_cfg = { 0 };

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
    I2S_RxGetDefaultConfig(&i2s_cfg);

    i2s_cfg.masterSlave = kI2S_MasterSlaveNormalSlave; /* Normal Slave */
    i2s_cfg.mode = kI2S_ModeDspWsShort;                /* DSP mode, WS having one clock long pulse */
    i2s_cfg.leftJust = true;                           /* Left Justified */
    i2s_cfg.divider = 1;                               /* As slave, divider need to set to 1 according to the spec. */
    i2s_cfg.oneChannel = true;                         /* I2S data for this channel pair is treated as a single channel */
    i2s_cfg.dataLength = I2S_RX_DATA_LEN;
    i2s_cfg.frameLength = I2S_RX_FRAME_LEN;

    I2S_RxInit(I2S_RX, &i2s_cfg);

    DMA_Init(DMA_RX);
    DMA_EnableChannel(DMA_RX, DMA_RX_CHANNEL);
    DMA_SetChannelPriority(DMA_RX, DMA_RX_CHANNEL, DMA_RX_CHANNEL_PRIO);
    DMA_CreateHandle(&s_i2sRxDmaHandle, DMA_RX, DMA_RX_CHANNEL);

    I2S_RxTransferCreateHandleDMA(I2S_RX, &s_i2sDmaHandle, &s_i2sRxDmaHandle, i2s_Callback, (void *)&s_receiveXfer);
    I2S_TransferInstallLoopDMADescriptorMemory(&s_i2sDmaHandle, s_rxDmaDescriptors, 2U);

    if (I2S_TransferReceiveLoopDMA(I2S_RX, &s_i2sDmaHandle, &s_receiveXfer[0], 2U) != kStatus_Success)
    {
        assert(false);
    }

}
