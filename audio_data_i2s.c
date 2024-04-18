/*
 * Copyright (c) 2016, Freescale Semiconductor, Inc.
 * Copyright 2016,2019 NXP
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
#include "fsl_i2s_bridge.h"

#include "audio_data_i2s.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define TO_BITS(n) ((n) << 3U)

#define AUDIO_ENDPOINT_MAX_PACKET_SIZE MAX(FS_ISO_IN_ENDP_PACKET_SIZE, HS_ISO_IN_ENDP_PACKET_SIZE)
#define I2S_BUFF_SIZE (AUDIO_ENDPOINT_MAX_PACKET_SIZE * 4)

#define CH_NUM (16) /** Channels number [16] */
#define CH_NUM_PER_PAIR (2) /** Channels number per I2S pair (L/R) [2] */
#define CH_LEN_DATA (4) /** Bytes per channel [4 bytes] */
#define INST_NUM (2) /** Number of I2S instances [2] */

#define CH_NUM_PER_INST (CH_NUM / INST_NUM) /** Channels number per I2S instance [8] */
#define CH_LEN_PER_PAIR (CH_LEN_DATA * CH_NUM_PER_PAIR) /** Bytes per I2S pair [8 bytes] */

#define FRAME_LEN (CH_NUM * CH_LEN_DATA) /** Bytes in an audio frame [64 bytes] */
#define FRAME_LEN_PER_INST (CH_NUM_PER_INST * CH_LEN_DATA) /** Bytes of the frame managed by an I2S instance [32 bytes] */

#define CH_POS(off, n) (TO_BITS(off) + (TO_BITS(CH_LEN_PER_PAIR) * (n)))

/*******************************************************************************
 * Variables
 ******************************************************************************/
USB_DMA_NONINIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE) uint8_t s_wavBuff[AUDIO_ENDPOINT_MAX_PACKET_SIZE];
SDK_ALIGN(static uint8_t s_i2sBuff[I2S_BUFF_SIZE], sizeof(uint32_t));

static i2s_transfer_t s_RxTransfer_0_7;
static i2s_transfer_t s_RxTransfer_8_15;

static i2s_handle_t s_RxHandle_0_7;
static i2s_handle_t s_RxHandle_8_15;

static uint32_t s_audioPosition = 0U;
volatile unsigned int first_int = 0;

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
    uint32_t k;

    for (k = 0U; k < size; k++)
    {
        if (s_audioPosition > I2S_BUFF_SIZE)
        {
            s_audioPosition = 0U;
        }

        *(buffer + k) = s_i2sBuff[s_audioPosition];
        s_audioPosition++;
    }
}

static void RxCallback(I2S_Type *base, i2s_handle_t *handle, status_t completionStatus, void *userData)
{
    i2s_transfer_t *transfer = (i2s_transfer_t *)userData;

    if (first_int == 0U)
    {
        s_audioPosition = 0U;
        first_int = 1U;
    }

    transfer->data = &s_i2sBuff[((transfer->data + FRAME_LEN) - s_i2sBuff) % I2S_BUFF_SIZE];

    I2S_RxTransferNonBlocking(base, handle, *transfer);
}

void Board_I2S_Init(void)
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
    s_RxConfig.dataLength = TO_BITS(CH_LEN_DATA);
    s_RxConfig.frameLength = TO_BITS(FRAME_LEN);

    I2S_RxInit(I2S_RX_0_7, &s_RxConfig);
    I2S_EnableSecondaryChannel(I2S_RX_0_7, kI2S_SecondaryChannel1, false, CH_POS(0, 1));
    I2S_EnableSecondaryChannel(I2S_RX_0_7, kI2S_SecondaryChannel2, false, CH_POS(0, 2));
    I2S_EnableSecondaryChannel(I2S_RX_0_7, kI2S_SecondaryChannel3, false, CH_POS(0, 3));

    s_RxTransfer_0_7.data = &s_i2sBuff[0];
    s_RxTransfer_0_7.dataSize = FRAME_LEN_PER_INST;

    I2S_RxTransferCreateHandle(I2S_RX_0_7, &s_RxHandle_0_7, RxCallback, (void *)&s_RxTransfer_0_7);

    s_RxConfig.position = TO_BITS(FRAME_LEN_PER_INST);

    I2S_RxInit(I2S_RX_8_15, &s_RxConfig);
    I2S_EnableSecondaryChannel(I2S_RX_8_15, kI2S_SecondaryChannel1, false, CH_POS(FRAME_LEN_PER_INST, 1));
    I2S_EnableSecondaryChannel(I2S_RX_8_15, kI2S_SecondaryChannel2, false, CH_POS(FRAME_LEN_PER_INST, 2));
    I2S_EnableSecondaryChannel(I2S_RX_8_15, kI2S_SecondaryChannel3, false, CH_POS(FRAME_LEN_PER_INST, 3));

    s_RxTransfer_8_15.data = &s_i2sBuff[FRAME_LEN_PER_INST];
    s_RxTransfer_8_15.dataSize = FRAME_LEN_PER_INST;

    I2S_RxTransferCreateHandle(I2S_RX_8_15, &s_RxHandle_8_15, RxCallback, (void *)&s_RxTransfer_8_15);

    I2S_RxTransferNonBlocking(I2S_RX_0_7, &s_RxHandle_0_7, s_RxTransfer_0_7);
    I2S_RxTransferNonBlocking(I2S_RX_8_15, &s_RxHandle_8_15, s_RxTransfer_8_15);
}