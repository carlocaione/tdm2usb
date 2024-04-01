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
#include "fsl_i2s_bridge.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define I2S_RX_0_7 (I2S5)
#define I2S_RX_8_16 (I2S4)

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

/*******************************************************************************
 * Variables
 ******************************************************************************/
static i2s_config_t s_RxConfig;

static uint8_t s_Buffer[1024] __attribute__((aligned(4)));

static i2s_transfer_t s_RxTransfer_0_7;
static i2s_transfer_t s_RxTransfer_8_16;

static i2s_handle_t s_RxHandle_0_7;
static i2s_handle_t s_RxHandle_8_16;

/*******************************************************************************
 * Code
 ******************************************************************************/
static void RxCallback_0_7(I2S_Type *base, i2s_handle_t *handle, status_t completionStatus, void *userData)
{
    i2s_transfer_t *transfer = (i2s_transfer_t *)userData;

    transfer->dataSize = (8 * 4);

    if ((handle->transferCount % 512) == 0) {
        transfer->data = &s_Buffer[0];
    } else {
        transfer->data += (transfer->dataSize * 2);
    }

    I2S_RxTransferNonBlocking(base, handle, *transfer);
}

static void RxCallback_8_16(I2S_Type *base, i2s_handle_t *handle, status_t completionStatus, void *userData)
{
    i2s_transfer_t *transfer = (i2s_transfer_t *)userData;

    transfer->dataSize = (8 * 4);

    if ((handle->transferCount % 512) == 0) {
        transfer->data = &s_Buffer[transfer->dataSize];
    } else {
        transfer->data += (transfer->dataSize * 2);
    }

    I2S_RxTransferNonBlocking(base, handle, *transfer);
}

/*!
 * @brief I2S setup function
 */
void I2S_Setup(void)
{
    I2S_BRIDGE_SetShareSignalSrc(kI2S_BRIDGE_ShareSet0, kI2S_BRIDGE_SignalSCK, kI2S_BRIDGE_Flexcomm5);
    I2S_BRIDGE_SetShareSignalSrc(kI2S_BRIDGE_ShareSet0, kI2S_BRIDGE_SignalWS, kI2S_BRIDGE_Flexcomm5);
    I2S_BRIDGE_SetShareSignalSrc(kI2S_BRIDGE_ShareSet0, kI2S_BRIDGE_SignalDataIn, kI2S_BRIDGE_Flexcomm5);

    I2S_BRIDGE_SetFlexcommSignalShareSet(kI2S_BRIDGE_Flexcomm4, kI2S_BRIDGE_SignalSCK, kI2S_BRIDGE_ShareSet0);
    I2S_BRIDGE_SetFlexcommSignalShareSet(kI2S_BRIDGE_Flexcomm4, kI2S_BRIDGE_SignalWS, kI2S_BRIDGE_ShareSet0);
    I2S_BRIDGE_SetFlexcommSignalShareSet(kI2S_BRIDGE_Flexcomm4, kI2S_BRIDGE_SignalDataIn, kI2S_BRIDGE_ShareSet0);

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
    s_RxConfig.dataLength = 32;
    s_RxConfig.frameLength = 32 * 16U;

    I2S_RxInit(I2S_RX_0_7, &s_RxConfig);
    I2S_EnableSecondaryChannel(I2S_RX_0_7, kI2S_SecondaryChannel1, false, (64 * 1));
    I2S_EnableSecondaryChannel(I2S_RX_0_7, kI2S_SecondaryChannel2, false, (64 * 2));
    I2S_EnableSecondaryChannel(I2S_RX_0_7, kI2S_SecondaryChannel3, false, (64 * 3));

    s_RxTransfer_0_7.data = &s_Buffer[0];
    s_RxTransfer_0_7.dataSize = 8 * 4;

    I2S_RxTransferCreateHandle(I2S_RX_0_7, &s_RxHandle_0_7, RxCallback_0_7, (void *)&s_RxTransfer_0_7);

    s_RxConfig.position = 256;

    I2S_RxInit(I2S_RX_8_16, &s_RxConfig);
    I2S_EnableSecondaryChannel(I2S_RX_8_16, kI2S_SecondaryChannel1, false, 256 + (64 * 1));
    I2S_EnableSecondaryChannel(I2S_RX_8_16, kI2S_SecondaryChannel2, false, 256 + (64 * 2));
    I2S_EnableSecondaryChannel(I2S_RX_8_16, kI2S_SecondaryChannel3, false, 256 + (64 * 3));

    s_RxTransfer_8_16.data = &s_Buffer[8 * 4];
    s_RxTransfer_8_16.dataSize = 8 * 4;

    I2S_RxTransferCreateHandle(I2S_RX_8_16, &s_RxHandle_8_16, RxCallback_8_16, (void *)&s_RxTransfer_8_16);

    I2S_RxTransferNonBlocking(I2S_RX_0_7, &s_RxHandle_0_7, s_RxTransfer_0_7);
    I2S_RxTransferNonBlocking(I2S_RX_8_16, &s_RxHandle_8_16, s_RxTransfer_8_16);
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
