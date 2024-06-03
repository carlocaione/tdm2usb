/*
 * Copyright (c) 2023, Carlo Caione <ccaione@baylibre.com>
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fsl_i2s_bridge.h"
#include "fsl_dma.h"

#include "i2s.h"

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
 * @brief Board setup funcion for I2S.
 *
 * Entry point function for I2S and DMA setup.
 */
void BOARD_I2S_Init(void)
{
    I2S_SetupSharedSignals();
    DMA_Init(DMA);

    /* Keep this priority in sync with USB_DEVICE_INTERRUPT_PRIORITY */
    NVIC_SetPriority(DMA0_IRQn, 6U);
}
