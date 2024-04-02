/*
 * Copyright (c) 2013 - 2015, Freescale Semiconductor, Inc.
 * Copyright 2016-2017 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fsl_device_registers.h"
#include "fsl_debug_console.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "board.h"

#include "fsl_i2s.h"
#include "fsl_i2s_dma.h"
#include "fsl_dma.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define I2S_RX (I2S5)
#define I2S_RX_DATA_LEN (24U)
#define I2S_RX_FRAME_LEN (32U)
#define I2S_BUFFER_SIZE (600)

#define DMA_RX (DMA0)
#define DMA_RX_CHANNEL (10)
#define DMA_RX_CHANNEL_PRIO (kDMA_ChannelPriority2)

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

/*******************************************************************************
 * Variables
 ******************************************************************************/
SDK_ALIGN(static uint8_t s_Buffer[2][I2S_BUFFER_SIZE], sizeof(uint32_t));
SDK_ALIGN(static dma_descriptor_t s_rxDmaDescriptors[2U], FSL_FEATURE_DMA_LINK_DESCRIPTOR_ALIGN_SIZE);

static i2s_config_t s_RxConfig;
static i2s_dma_handle_t s_RxHandle;

static dma_handle_t s_DmaRxHandle;

static i2s_transfer_t s_rxTransfer[2] = {
    {
        .data = s_Buffer[0],
        .dataSize = I2S_BUFFER_SIZE,
    },
    {
        .data = s_Buffer[1],
        .dataSize = I2S_BUFFER_SIZE,
    },
};

/*******************************************************************************
 * Code
 ******************************************************************************/
static void RxCallback(I2S_Type *base, i2s_dma_handle_t *handle, status_t completionStatus, void *userData)
{
    __NOP();
}

/*!
 * @brief I2S and DMA setup function
 */
void I2S_DMA_Setup(void)
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
    I2S_RxGetDefaultConfig(&s_RxConfig);

    s_RxConfig.masterSlave = kI2S_MasterSlaveNormalSlave; /* Normal Slave */
    s_RxConfig.mode = kI2S_ModeDspWsShort;                /* DSP mode, WS having one clock long pulse */
    s_RxConfig.leftJust = true;                           /* Left Justified */
    s_RxConfig.divider = 1;                               /* As slave, divider need to set to 1 according to the spec. */
    s_RxConfig.oneChannel = true;                         /* I2S data for this channel pair is treated as a single channel */
    s_RxConfig.dataLength = I2S_RX_DATA_LEN;
    s_RxConfig.frameLength = I2S_RX_FRAME_LEN;

    I2S_RxInit(I2S_RX, &s_RxConfig);

    DMA_Init(DMA_RX);
    DMA_EnableChannel(DMA_RX, DMA_RX_CHANNEL);
    DMA_SetChannelPriority(DMA_RX, DMA_RX_CHANNEL, DMA_RX_CHANNEL_PRIO);
    DMA_CreateHandle(&s_DmaRxHandle, DMA_RX, DMA_RX_CHANNEL);

    I2S_RxTransferCreateHandleDMA(I2S_RX, &s_RxHandle, &s_DmaRxHandle, RxCallback, (void *)&s_rxTransfer);
    I2S_TransferInstallLoopDMADescriptorMemory(&s_RxHandle, s_rxDmaDescriptors, 2U);

    if (I2S_TransferReceiveLoopDMA(I2S_RX, &s_RxHandle, &s_rxTransfer[0], 2U) != kStatus_Success)
    {
        assert(false);
    }
}

/*!
 * @brief Main function
 */
int main(void)
{
    char ch;

    /* Init board hardware. */
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();

    I2S_DMA_Setup();

    PRINTF("hello world.\r\n");

    while (1)
    {
        ch = GETCHAR();
        PUTCHAR(ch);
    }
}
