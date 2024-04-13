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
#define BUF_SIZE (1024)

#define CH_NUM (16)
#define CH_NUM_PER_INST (8)
#define CH_NUM_PER_PAIR (2) /* 2 channels (L/R) per pair */

#define CH_LEN_DATA (sizeof(uint32_t)) /* 4 bytes per channel */
#define CH_LEN_DATA_BITS (CH_LEN_DATA * 8) /* 32 bits per channel */
#define CH_LEN_DATA_PER_PAIR_BITS (CH_NUM_PER_PAIR * CH_LEN_DATA_BITS) /* 64 bits per pair */

#define FRAME_LEN (CH_NUM * CH_LEN_DATA) /* 64 bytes in one frame */
#define FRAME_LEN_BITS (FRAME_LEN * 8) /* 512 bits in one frame */

#define FRAME_LEN_PER_INST (CH_NUM_PER_INST * CH_LEN_DATA) /* 32 bytes for half-frame */
#define FRAME_LEN_PER_INST_BITS (FRAME_LEN_PER_INST * 8) /* 256 bits for half-frame */

#define CH_POS(off, n) (off + (CH_LEN_DATA_PER_PAIR_BITS * n))

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

/*******************************************************************************
 * Variables
 ******************************************************************************/
static uint8_t s_Buffer[BUF_SIZE] __attribute__((aligned(4)));

static i2s_transfer_t s_RxTransfer_0_7;
static i2s_transfer_t s_RxTransfer_8_16;

static i2s_handle_t s_RxHandle_0_7;
static i2s_handle_t s_RxHandle_8_16;

/*******************************************************************************
 * Code
 ******************************************************************************/
static void RxCallback(I2S_Type *base, i2s_handle_t *handle, status_t completionStatus, void *userData)
{
    i2s_transfer_t *transfer = (i2s_transfer_t *)userData;

    transfer->data = &s_Buffer[((transfer->data + FRAME_LEN) - s_Buffer) % BUF_SIZE];

    I2S_RxTransferNonBlocking(base, handle, *transfer);
}

/*!
 * @brief I2S setup function
 */
void I2S_Setup(void)
{
    i2s_config_t s_RxConfig = { 0 };

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
    s_RxConfig.dataLength = CH_LEN_DATA_BITS;
    s_RxConfig.frameLength = FRAME_LEN_BITS;

    I2S_RxInit(I2S_RX_0_7, &s_RxConfig);
    I2S_EnableSecondaryChannel(I2S_RX_0_7, kI2S_SecondaryChannel1, false, CH_POS(0, 1));
    I2S_EnableSecondaryChannel(I2S_RX_0_7, kI2S_SecondaryChannel2, false, CH_POS(0, 2));
    I2S_EnableSecondaryChannel(I2S_RX_0_7, kI2S_SecondaryChannel3, false, CH_POS(0, 3));

    s_RxTransfer_0_7.data = &s_Buffer[0];
    s_RxTransfer_0_7.dataSize = FRAME_LEN_PER_INST;

    I2S_RxTransferCreateHandle(I2S_RX_0_7, &s_RxHandle_0_7, RxCallback, (void *)&s_RxTransfer_0_7);

    s_RxConfig.position = FRAME_LEN_PER_INST_BITS;

    I2S_RxInit(I2S_RX_8_16, &s_RxConfig);
    I2S_EnableSecondaryChannel(I2S_RX_8_16, kI2S_SecondaryChannel1, false, CH_POS(FRAME_LEN_PER_INST_BITS, 1));
    I2S_EnableSecondaryChannel(I2S_RX_8_16, kI2S_SecondaryChannel2, false, CH_POS(FRAME_LEN_PER_INST_BITS, 2));
    I2S_EnableSecondaryChannel(I2S_RX_8_16, kI2S_SecondaryChannel3, false, CH_POS(FRAME_LEN_PER_INST_BITS, 3));

    s_RxTransfer_8_16.data = &s_Buffer[FRAME_LEN_PER_INST];
    s_RxTransfer_8_16.dataSize = FRAME_LEN_PER_INST;

    I2S_RxTransferCreateHandle(I2S_RX_8_16, &s_RxHandle_8_16, RxCallback, (void *)&s_RxTransfer_8_16);

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
