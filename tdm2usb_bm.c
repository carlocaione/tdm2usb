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

/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define I2S_RX (I2S5)

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

/*******************************************************************************
 * Variables
 ******************************************************************************/
static uint8_t s_Buffer[600] __attribute__((aligned(4)));
static i2s_config_t s_RxConfig;
static i2s_transfer_t s_RxTransfer;
static i2s_handle_t s_RxHandle;

/*******************************************************************************
 * Code
 ******************************************************************************/
static void RxCallback(I2S_Type *base, i2s_handle_t *handle, status_t completionStatus, void *userData)
{
    i2s_transfer_t *transfer = (i2s_transfer_t *)userData;
    I2S_RxTransferNonBlocking(base, handle, *transfer);
}

/*!
 * @brief I2S setup function
 */
void I2S_Setup(void)
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
    s_RxConfig.divider = 1;                               /*  As slave, divider need to set to 1 according to the spec. */
    s_RxConfig.oneChannel = true;                         /* I2S data for this channel pair is treated as a single channel */
    s_RxConfig.dataLength = 24;
    s_RxConfig.frameLength = 32;

    I2S_RxInit(I2S_RX, &s_RxConfig);

    s_RxTransfer.data = &s_Buffer[0];
    s_RxTransfer.dataSize = sizeof(s_Buffer);

    I2S_RxTransferCreateHandle(I2S_RX, &s_RxHandle, RxCallback, (void *)&s_RxTransfer);
    I2S_RxTransferNonBlocking(I2S_RX, &s_RxHandle, s_RxTransfer);
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

    I2S_Setup();

    PRINTF("hello world.\r\n");

    while (1)
    {
        ch = GETCHAR();
        PUTCHAR(ch);
    }
}
