/*
 * Copyright (c) 2015 - 2016, Freescale Semiconductor, Inc.
 * Copyright 2016 - 2017,2019 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "usb_device_config.h"
#include "usb.h"
#include "usb_device.h"

#include "usb_device_class.h"
#include "usb_device_audio.h"
#include "usb_audio_config.h"
#include "usb_device_ch9.h"
#include "usb_device_descriptor.h"

#include "tdm2usb.h"
#include "audio_data_i2s.h"

#include "fsl_device_registers.h"
#include "fsl_debug_console.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "board.h"
#if (defined(FSL_FEATURE_SOC_SYSMPU_COUNT) && (FSL_FEATURE_SOC_SYSMPU_COUNT > 0U))
#include "fsl_sysmpu.h"
#endif /* FSL_FEATURE_SOC_SYSMPU_COUNT */

#if ((defined FSL_FEATURE_SOC_USBPHY_COUNT) && (FSL_FEATURE_SOC_USBPHY_COUNT > 0U))
#include "usb_phy.h"
#endif

#include "fsl_power.h"
#if defined(USB_DEVICE_AUDIO_USE_SYNC_MODE) && (USB_DEVICE_AUDIO_USE_SYNC_MODE > 0U)
#include "fsl_sctimer.h"
#endif
/*******************************************************************************
 * Definitions
 ******************************************************************************/

/*******************************************************************************
 * Prototypes
 ******************************************************************************/
#if USB_DEVICE_CONFIG_USE_TASK
void USB_DeviceTaskFn(void *deviceHandle);
#endif

usb_status_t USB_DeviceAudioCallback(class_handle_t handle, uint32_t event, void *param);
usb_status_t USB_DeviceCallback(usb_device_handle handle, uint32_t event, void *param);

#if defined(USB_DEVICE_AUDIO_USE_SYNC_MODE) && (USB_DEVICE_AUDIO_USE_SYNC_MODE > 0U)
extern void SCTIMER_CaptureInit(void);
#endif
/*******************************************************************************
 * Variables
 ******************************************************************************/
#if defined(USB_DEVICE_AUDIO_USE_SYNC_MODE) && (USB_DEVICE_AUDIO_USE_SYNC_MODE > 0U)
static uint32_t eventCounterU = 0;
static uint32_t captureRegisterNumber;
static sctimer_config_t sctimerInfo;
#endif

extern usb_audio_device_struct_t g_audioDevice;
extern usb_device_class_struct_t g_UsbDeviceAudioClass;

/* Default value of audio device struct */
USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
usb_audio_device_struct_t g_audioDevice = {
    .streamInPacketSize               = FS_ISO_IN_ENDP_PACKET_SIZE,
    .streamOutPacketSize              = FS_ISO_OUT_ENDP_PACKET_SIZE,
    .deviceHandle                     = NULL,
    .audioHandle                      = NULL,
    .applicationTaskHandle            = NULL,
    .deviceTaskHandle                 = NULL,
    .copyProtect                      = 0x01U,
    .curMute                          = 0x01U,
    .curVolume                        = {0x00U, 0x80U},
    .minVolume                        = {0x00U, 0x80U},
    .maxVolume                        = {0xFFU, 0x7FU},
    .resVolume                        = {0x01U, 0x00U},
    .curBass                          = 0x00U,
    .minBass                          = 0x80U,
    .maxBass                          = 0x7FU,
    .resBass                          = 0x01U,
    .curMid                           = 0x00U,
    .minMid                           = 0x80U,
    .maxMid                           = 0x7FU,
    .resMid                           = 0x01U,
    .curTreble                        = 0x01U,
    .minTreble                        = 0x80U,
    .maxTreble                        = 0x7FU,
    .resTreble                        = 0x01U,
    .curAutomaticGain                 = 0x01U,
    .curDelay                         = {0x00U, 0x40U},
    .minDelay                         = {0x00U, 0x00U},
    .maxDelay                         = {0xFFU, 0xFFU},
    .resDelay                         = {0x00U, 0x01U},
    .curLoudness                      = 0x01U,
    .curSamplingFrequency             = {0x00U, 0x00U, 0x01U},
    .minSamplingFrequency             = {0x00U, 0x00U, 0x01U},
    .maxSamplingFrequency             = {0x00U, 0x00U, 0x01U},
    .resSamplingFrequency             = {0x00U, 0x00U, 0x01U},
    .curMute20                        = 0U,
    .curClockValid                    = 1U,
    .curVolume20                      = {0x00U, 0x1FU},
    .curSampleFrequency               = 48000U,
    .freqControlRange                 = {1U, 48000U, 48000U, 0U},
    .volumeControlRange               = {1U, 0x8001U, 0x7FFFU, 1U},
    .currentConfiguration             = 0,
    .currentInterfaceAlternateSetting = {0, 0, 0},
    .speed                            = USB_SPEED_FULL,
    .attach                           = 0U,
#if defined(USB_DEVICE_AUDIO_USE_SYNC_MODE) && (USB_DEVICE_AUDIO_USE_SYNC_MODE > 0U)
    .generatorIntervalCount           = 0,
    .curAudioPllFrac                  = 0,
    .audioPllTicksPrev                = 0,
    .audioPllTicksDiff                = 0,
    .audioPllTicksEma                 = AUDIO_PLL_USB1_SOF_INTERVAL_COUNT,
    .audioPllTickEmaFrac              = 0,
    .audioPllStep                     = AUDIO_PLL_FRACTIONAL_CHANGE_STEP,
#endif
};

/* USB device class information */
static usb_device_class_config_struct_t s_audioConfig[1] = {{
    USB_DeviceAudioCallback,
    (class_handle_t)NULL,
    &g_UsbDeviceAudioClass,
}};

/* USB device class configuration information */
static usb_device_class_config_list_struct_t s_audioConfigList = {
    s_audioConfig,
    USB_DeviceCallback,
    1U,
};

/*******************************************************************************
 * Code
 ******************************************************************************/

#if defined(USB_DEVICE_AUDIO_USE_SYNC_MODE) && (USB_DEVICE_AUDIO_USE_SYNC_MODE > 0U)
uint32_t pllIsChanged = 0;
void SCTIMER_SOF_TOGGLE_HANDLER()
{
    uint32_t currentSctCap = 0, pllCountPeriod = 0, pll_change = 0;
    static int32_t pllCount = 0, pllDiff = 0;
    static int32_t err, abs_err;
    if (SCTIMER_GetStatusFlags(SCT0) & (1 << eventCounterU))
    {
        /* Clear interrupt flag.*/
        SCTIMER_ClearStatusFlags(SCT0, (1 << eventCounterU));
    }

    if (g_audioDevice.generatorIntervalCount != 100)
    {
        g_audioDevice.generatorIntervalCount++;
        return;
    }
    g_audioDevice.generatorIntervalCount = 1;
    currentSctCap                           = SCT0->CAP[0];
    pllCountPeriod                          = currentSctCap - g_audioDevice.audioPllTicksPrev;
    g_audioDevice.audioPllTicksPrev      = currentSctCap;
    pllCount                                = pllCountPeriod;
    if (g_audioDevice.attach)
    {
        if (abs(pllCount - AUDIO_PLL_USB1_SOF_INTERVAL_COUNT) < (AUDIO_PLL_USB1_SOF_INTERVAL_COUNT >> 7))
        {
            pllDiff = pllCount - g_audioDevice.audioPllTicksEma;
            g_audioDevice.audioPllTickEmaFrac += (pllDiff % 8);
            g_audioDevice.audioPllTicksEma += (pllDiff / 8) + g_audioDevice.audioPllTickEmaFrac / 8;
            g_audioDevice.audioPllTickEmaFrac = (g_audioDevice.audioPllTickEmaFrac % 8);

            err     = g_audioDevice.audioPllTicksEma - AUDIO_PLL_USB1_SOF_INTERVAL_COUNT;
            abs_err = abs(err);
            if (abs_err > g_audioDevice.audioPllStep)
            {
                if (err > 0)
                {
                    g_audioDevice.curAudioPllFrac -= abs_err / g_audioDevice.audioPllStep;
                }
                else
                {
                    g_audioDevice.curAudioPllFrac += abs_err / g_audioDevice.audioPllStep;
                }
                pll_change = 1;
            }

            if (pll_change)
            {
                CLKCTL1->AUDIOPLL0NUM = g_audioDevice.curAudioPllFrac;
                pllIsChanged          = 1;
            }
        }
    }
}

void SCTIMER_CaptureInit(void)
{
    INPUTMUX->SCT0_IN_SEL[eventCounterU] = 0xFU; /* 0xFU for USB1.*/
    SCTIMER_GetDefaultConfig(&sctimerInfo);

    /* Switch to 16-bit mode */
    sctimerInfo.clockMode   = kSCTIMER_Input_ClockMode;
    sctimerInfo.clockSelect = kSCTIMER_Clock_On_Rise_Input_7;

    /* Initialize SCTimer module */
    SCTIMER_Init(SCT0, &sctimerInfo);

    if (SCTIMER_SetupCaptureAction(SCT0, kSCTIMER_Counter_U, &captureRegisterNumber, eventCounterU) == kStatus_Fail)
    {
        usb_echo("SCT Setup Capture failed!\r\n");
    }
    SCT0->EV[0].STATE = 0x1;
    SCT0->EV[0].CTRL  = (0x01 << 10) | (0x2 << 12);

    /* Enable interrupt flag for event associated with out 4, we use the interrupt to update dutycycle */
    SCTIMER_EnableInterrupts(SCT0, (1 << eventCounterU));

    /* Receive notification when event is triggered */
    SCTIMER_SetCallback(SCT0, SCTIMER_SOF_TOGGLE_HANDLER, eventCounterU);

    /* Enable at the NVIC */
    EnableIRQ(SCT0_IRQn);

    /* Start the L counter */
    SCTIMER_StartTimer(SCT0, kSCTIMER_Counter_U);
}
#endif

void USB_IRQHandler(void)
{
    USB_DeviceLpcIp3511IsrFunction(g_audioDevice.deviceHandle);
}

void USB_DeviceClockInit(void)
{
    uint8_t usbClockDiv = 1;
    uint32_t usbClockFreq;
    usb_phy_config_struct_t phyConfig = {
        BOARD_USB_PHY_D_CAL,
        BOARD_USB_PHY_TXCAL45DP,
        BOARD_USB_PHY_TXCAL45DM,
    };

    /* enable USB IP clock */
    CLOCK_SetClkDiv(kCLOCK_DivPfc1Clk, 5);
    CLOCK_AttachClk(kXTALIN_CLK_to_USB_CLK);
    CLOCK_SetClkDiv(kCLOCK_DivUsbHsFclk, usbClockDiv);
    CLOCK_EnableUsbhsDeviceClock();
    RESET_PeripheralReset(kUSBHS_PHY_RST_SHIFT_RSTn);
    RESET_PeripheralReset(kUSBHS_DEVICE_RST_SHIFT_RSTn);
    RESET_PeripheralReset(kUSBHS_HOST_RST_SHIFT_RSTn);
    RESET_PeripheralReset(kUSBHS_SRAM_RST_SHIFT_RSTn);
    /*Make sure USDHC ram buffer has power up*/
    POWER_DisablePD(kPDRUNCFG_APD_USBHS_SRAM);
    POWER_DisablePD(kPDRUNCFG_PPD_USBHS_SRAM);
    POWER_ApplyPD();

    /* save usb ip clock freq*/
    usbClockFreq = g_xtalFreq / usbClockDiv;
    /* enable USB PHY PLL clock, the phy bus clock (480MHz) source is same with USB IP */
    CLOCK_EnableUsbHs0PhyPllClock(kXTALIN_CLK_to_USB_CLK, usbClockFreq);

#if defined(FSL_FEATURE_USBHSD_USB_RAM) && (FSL_FEATURE_USBHSD_USB_RAM)
    for (int i = 0; i < FSL_FEATURE_USBHSD_USB_RAM; i++)
    {
        ((uint8_t *)FSL_FEATURE_USBHSD_USB_RAM_BASE_ADDRESS)[i] = 0x00U;
    }
#endif
    USB_EhciPhyInit(CONTROLLER_ID, BOARD_XTAL_SYS_CLK_HZ, &phyConfig);

    /* the following code should run after phy initialization and should wait some microseconds to make sure utmi clock
     * valid */
    /* enable usb1 host clock */
    CLOCK_EnableClock(kCLOCK_UsbhsHost);
    /*  Wait until host_needclk de-asserts */
    while (SYSCTL0->USBCLKSTAT & SYSCTL0_USBCLKSTAT_HOST_NEED_CLKST_MASK)
    {
        __ASM("nop");
    }
    /*According to reference mannual, device mode setting has to be set by access usb host register */
    USBHSH->PORTMODE |= USBHSH_PORTMODE_DEV_ENABLE_MASK;
    /* disable usb1 host clock */
    CLOCK_DisableClock(kCLOCK_UsbhsHost);
}
void USB_DeviceIsrEnable(void)
{
    uint8_t irqNumber;

    uint8_t usbDeviceIP3511Irq[] = USBHSD_IRQS;
    irqNumber                    = usbDeviceIP3511Irq[CONTROLLER_ID - kUSB_ControllerLpcIp3511Hs0];

    /* Install isr, set priority, and enable IRQ. */
    NVIC_SetPriority((IRQn_Type)irqNumber, USB_DEVICE_INTERRUPT_PRIORITY);
    EnableIRQ((IRQn_Type)irqNumber);
}
#if USB_DEVICE_CONFIG_USE_TASK
void USB_DeviceTaskFn(void *deviceHandle)
{
    USB_DeviceLpcIp3511TaskFunction(deviceHandle);
}
#endif
/*!
 * @brief Audio class specific request function.
 *
 * This function handles the Audio class specific requests.
 *
 * @param handle           The Audio class handle.
 * @param event            The Audio class event type.
 * @param param            The parameter of the class specific request.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceAudioRequest(class_handle_t handle, uint32_t event, void *param)
{
    usb_device_control_request_struct_t *request = (usb_device_control_request_struct_t *)param;
    usb_status_t error                           = kStatus_USB_Success;

    switch (event)
    {
        case USB_DEVICE_AUDIO_FU_GET_CUR_MUTE_CONTROL:
            request->buffer = (uint8_t *)&g_audioDevice.curMute20;
            request->length = sizeof(g_audioDevice.curMute20);
            break;
        case USB_DEVICE_AUDIO_FU_GET_CUR_VOLUME_CONTROL:
            request->buffer = (uint8_t *)&g_audioDevice.curVolume20;
            request->length = sizeof(g_audioDevice.curVolume20);
            break;
        case USB_DEVICE_AUDIO_FU_GET_CUR_BASS_CONTROL:
            request->buffer = &g_audioDevice.curBass;
            request->length = sizeof(g_audioDevice.curBass);
            break;
        case USB_DEVICE_AUDIO_FU_GET_CUR_MID_CONTROL:
            request->buffer = &g_audioDevice.curMid;
            request->length = sizeof(g_audioDevice.curMid);
            break;
        case USB_DEVICE_AUDIO_FU_GET_CUR_TREBLE_CONTROL:
            request->buffer = &g_audioDevice.curTreble;
            request->length = sizeof(g_audioDevice.curTreble);
            break;
        case USB_DEVICE_AUDIO_FU_GET_CUR_AUTOMATIC_GAIN_CONTROL:
            request->buffer = &g_audioDevice.curAutomaticGain;
            request->length = sizeof(g_audioDevice.curAutomaticGain);
            break;
        case USB_DEVICE_AUDIO_FU_GET_CUR_DELAY_CONTROL:
            request->buffer = g_audioDevice.curDelay;
            request->length = sizeof(g_audioDevice.curDelay);
            break;
        case USB_DEVICE_AUDIO_FU_GET_MIN_VOLUME_CONTROL:
            request->buffer = g_audioDevice.minVolume;
            request->length = sizeof(g_audioDevice.minVolume);
            break;
        case USB_DEVICE_AUDIO_FU_GET_MIN_BASS_CONTROL:
            request->buffer = &g_audioDevice.minBass;
            request->length = sizeof(g_audioDevice.minBass);
            break;
        case USB_DEVICE_AUDIO_FU_GET_MIN_MID_CONTROL:
            request->buffer = &g_audioDevice.minMid;
            request->length = sizeof(g_audioDevice.minMid);
            break;
        case USB_DEVICE_AUDIO_FU_GET_MIN_TREBLE_CONTROL:
            request->buffer = &g_audioDevice.minTreble;
            request->length = sizeof(g_audioDevice.minTreble);
            break;
        case USB_DEVICE_AUDIO_FU_GET_MIN_DELAY_CONTROL:
            request->buffer = g_audioDevice.minDelay;
            request->length = sizeof(g_audioDevice.minDelay);
            break;
        case USB_DEVICE_AUDIO_FU_GET_MAX_VOLUME_CONTROL:
            request->buffer = g_audioDevice.maxVolume;
            request->length = sizeof(g_audioDevice.maxVolume);
            break;
        case USB_DEVICE_AUDIO_FU_GET_MAX_BASS_CONTROL:
            request->buffer = &g_audioDevice.maxBass;
            request->length = sizeof(g_audioDevice.maxBass);
            break;
        case USB_DEVICE_AUDIO_FU_GET_MAX_MID_CONTROL:
            request->buffer = &g_audioDevice.maxMid;
            request->length = sizeof(g_audioDevice.maxMid);
            break;
        case USB_DEVICE_AUDIO_FU_GET_MAX_TREBLE_CONTROL:
            request->buffer = &g_audioDevice.maxTreble;
            request->length = sizeof(g_audioDevice.maxTreble);
            break;
        case USB_DEVICE_AUDIO_FU_GET_MAX_DELAY_CONTROL:
            request->buffer = g_audioDevice.maxDelay;
            request->length = sizeof(g_audioDevice.maxDelay);
            break;
        case USB_DEVICE_AUDIO_FU_GET_RES_VOLUME_CONTROL:
            request->buffer = g_audioDevice.resVolume;
            request->length = sizeof(g_audioDevice.resVolume);
            break;
        case USB_DEVICE_AUDIO_FU_GET_RES_BASS_CONTROL:
            request->buffer = &g_audioDevice.resBass;
            request->length = sizeof(g_audioDevice.resBass);
            break;
        case USB_DEVICE_AUDIO_FU_GET_RES_MID_CONTROL:
            request->buffer = &g_audioDevice.resMid;
            request->length = sizeof(g_audioDevice.resMid);
            break;
        case USB_DEVICE_AUDIO_FU_GET_RES_TREBLE_CONTROL:
            request->buffer = &g_audioDevice.resTreble;
            request->length = sizeof(g_audioDevice.resTreble);
            break;
        case USB_DEVICE_AUDIO_FU_GET_RES_DELAY_CONTROL:
            request->buffer = g_audioDevice.resDelay;
            request->length = sizeof(g_audioDevice.resDelay);
            break;
        case USB_DEVICE_AUDIO_CS_GET_CUR_SAMPLING_FREQ_CONTROL:
            request->buffer = (uint8_t *)&g_audioDevice.curSampleFrequency;
            request->length = sizeof(g_audioDevice.curSampleFrequency);
            break;
        case USB_DEVICE_AUDIO_CS_SET_CUR_SAMPLING_FREQ_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = (uint8_t *)&g_audioDevice.curSampleFrequency;
                request->length = sizeof(g_audioDevice.curSampleFrequency);
            }
            break;
        case USB_DEVICE_AUDIO_CS_GET_CUR_CLOCK_VALID_CONTROL:
            request->buffer = &g_audioDevice.curClockValid;
            request->length = sizeof(g_audioDevice.curClockValid);
            break;
        case USB_DEVICE_AUDIO_CS_SET_CUR_CLOCK_VALID_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = &g_audioDevice.curClockValid;
                request->length = sizeof(g_audioDevice.curClockValid);
            }
            break;
        case USB_DEVICE_AUDIO_CS_GET_RANGE_SAMPLING_FREQ_CONTROL:
            request->buffer = (uint8_t *)&g_audioDevice.freqControlRange;
            request->length = sizeof(g_audioDevice.freqControlRange);
            break;
        case USB_DEVICE_AUDIO_FU_GET_RANGE_VOLUME_CONTROL:
            request->buffer = (uint8_t *)&g_audioDevice.volumeControlRange;
            request->length = sizeof(g_audioDevice.volumeControlRange);
            break;
        case USB_DEVICE_AUDIO_FU_SET_CUR_VOLUME_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = g_audioDevice.curVolume20;
                request->length = sizeof(g_audioDevice.curVolume20);
            }
            else
            {
                uint16_t volume = (uint16_t)((uint16_t)g_audioDevice.curVolume20[1] << 8U);
                volume |= (uint8_t)(g_audioDevice.curVolume20[0]);
            }
            break;
        case USB_DEVICE_AUDIO_FU_SET_CUR_MUTE_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = &g_audioDevice.curMute;
                request->length = sizeof(g_audioDevice.curMute);
            }
            else
            {
            }
            break;
        case USB_DEVICE_AUDIO_FU_SET_CUR_BASS_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = &g_audioDevice.curBass;
                request->length = sizeof(g_audioDevice.curBass);
            }
            break;
        case USB_DEVICE_AUDIO_FU_SET_CUR_MID_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = &g_audioDevice.curMid;
                request->length = sizeof(g_audioDevice.curMid);
            }
            break;
        case USB_DEVICE_AUDIO_FU_SET_CUR_TREBLE_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = &g_audioDevice.curTreble;
                request->length = sizeof(g_audioDevice.curTreble);
            }
            break;
        case USB_DEVICE_AUDIO_FU_SET_CUR_AUTOMATIC_GAIN_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = &g_audioDevice.curAutomaticGain;
                request->length = sizeof(g_audioDevice.curAutomaticGain);
            }
            break;
        case USB_DEVICE_AUDIO_FU_SET_CUR_DELAY_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = g_audioDevice.curDelay;
                request->length = sizeof(g_audioDevice.curDelay);
            }
            break;
        case USB_DEVICE_AUDIO_FU_SET_MIN_VOLUME_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = g_audioDevice.minVolume;
                request->length = sizeof(g_audioDevice.minVolume);
            }
            break;
        case USB_DEVICE_AUDIO_FU_SET_MIN_BASS_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = &g_audioDevice.minBass;
                request->length = sizeof(g_audioDevice.minBass);
            }
            break;
        case USB_DEVICE_AUDIO_FU_SET_MIN_MID_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = &g_audioDevice.minMid;
                request->length = sizeof(g_audioDevice.minMid);
            }
            break;
        case USB_DEVICE_AUDIO_FU_SET_MIN_TREBLE_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = &g_audioDevice.minTreble;
                request->length = sizeof(g_audioDevice.minTreble);
            }
            break;
        case USB_DEVICE_AUDIO_FU_SET_MIN_DELAY_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = g_audioDevice.minDelay;
                request->length = sizeof(g_audioDevice.minDelay);
            }
            break;
        case USB_DEVICE_AUDIO_FU_SET_MAX_VOLUME_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = g_audioDevice.maxVolume;
                request->length = sizeof(g_audioDevice.maxVolume);
            }
            break;
        case USB_DEVICE_AUDIO_FU_SET_MAX_BASS_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = &g_audioDevice.maxBass;
                request->length = sizeof(g_audioDevice.maxBass);
            }
            break;
        case USB_DEVICE_AUDIO_FU_SET_MAX_MID_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = &g_audioDevice.maxMid;
                request->length = sizeof(g_audioDevice.maxMid);
            }
            break;
        case USB_DEVICE_AUDIO_FU_SET_MAX_TREBLE_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = &g_audioDevice.maxTreble;
                request->length = sizeof(g_audioDevice.maxTreble);
            }
            break;
        case USB_DEVICE_AUDIO_FU_SET_MAX_DELAY_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = g_audioDevice.maxDelay;
                request->length = sizeof(g_audioDevice.maxDelay);
            }
            break;
        case USB_DEVICE_AUDIO_FU_SET_RES_VOLUME_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = g_audioDevice.resVolume;
                request->length = sizeof(g_audioDevice.resVolume);
            }
            break;
        case USB_DEVICE_AUDIO_FU_SET_RES_BASS_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = &g_audioDevice.resBass;
                request->length = sizeof(g_audioDevice.resBass);
            }
            break;
        case USB_DEVICE_AUDIO_FU_SET_RES_MID_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = &g_audioDevice.resMid;
                request->length = sizeof(g_audioDevice.resMid);
            }
            break;
        case USB_DEVICE_AUDIO_FU_SET_RES_TREBLE_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = &g_audioDevice.resTreble;
                request->length = sizeof(g_audioDevice.resTreble);
            }
            break;
        case USB_DEVICE_AUDIO_FU_SET_RES_DELAY_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = g_audioDevice.resDelay;
                request->length = sizeof(g_audioDevice.resDelay);
            }
            break;
        default:
            error = kStatus_USB_InvalidRequest;
            break;
    }
    return error;
}

/*!
 * @brief Audio class specific callback function.
 *
 * This function handles the Audio class specific requests.
 *
 * @param handle            The Audio class handle.
 * @param event             The Audio class event type.
 * @param param             The parameter of the class specific request.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceAudioCallback(class_handle_t handle, uint32_t event, void *param)
{
    usb_status_t error = kStatus_USB_InvalidRequest;
    usb_device_endpoint_callback_message_struct_t *ep_cb_param;
    ep_cb_param = (usb_device_endpoint_callback_message_struct_t *)param;

    switch (event)
    {
        case kUSB_DeviceAudioEventStreamSendResponse:
            if ((0U != g_audioDevice.attach) && (ep_cb_param->length == g_audioDevice.streamInPacketSize))
            {
                USB_AudioI2s2UsbBuffer(g_usbBuffIn, g_audioDevice.streamInPacketSize);
                error = USB_DeviceAudioSend(handle, USB_AUDIO_STREAM_IN_ENDPOINT,
                                            g_usbBuffIn, g_audioDevice.streamInPacketSize);
            }
            break;

        case kUSB_DeviceAudioEventStreamRecvResponse:
            if ((0U != g_audioDevice.attach) && (ep_cb_param->length != (USB_CANCELLED_TRANSFER_LENGTH)))
            {
                USB_AudioUsb2I2sBuffer(g_usbBuffOut, ep_cb_param->length);
                error = USB_DeviceAudioRecv(handle, USB_AUDIO_STREAM_OUT_ENDPOINT,
                                            g_usbBuffOut, g_audioDevice.streamOutPacketSize);
            }
            break;

        default:
            if ((NULL != param) && (event > 0xFFU))
            {
                error = USB_DeviceAudioRequest(handle, event, param);
            }
            break;
    }
    return error;
}

/*!
 * @brief USB device callback function.
 *
 * This function handles the usb device specific requests.
 *
 * @param handle           The USB device handle.
 * @param event            The USB device event type.
 * @param param            The parameter of the device specific request.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceCallback(usb_device_handle handle, uint32_t event, void *param)
{
    usb_status_t error = kStatus_USB_InvalidRequest;
    uint8_t *temp8     = (uint8_t *)param;
    uint16_t *temp16   = (uint16_t *)param;
    uint8_t count      = 0U;

    switch (event)
    {
        case kUSB_DeviceEventBusReset:
        {
            /* The device BUS reset signal detected */
            for (count = 0U; count < USB_AUDIO_INTERFACE_COUNT; count++)
            {
                g_audioDevice.currentInterfaceAlternateSetting[count] = 0U;
            }
            g_audioDevice.attach               = 0U;
            g_audioDevice.currentConfiguration = 0U;
            error                                 = kStatus_USB_Success;
#if (defined(USB_DEVICE_CONFIG_EHCI) && (USB_DEVICE_CONFIG_EHCI > 0U)) || \
    (defined(USB_DEVICE_CONFIG_LPCIP3511HS) && (USB_DEVICE_CONFIG_LPCIP3511HS > 0U))
            /* Get USB speed to configure the device, including max packet size and interval of the endpoints. */
            if (kStatus_USB_Success == USB_DeviceClassGetSpeed(CONTROLLER_ID, &g_audioDevice.speed))
            {
                USB_DeviceSetSpeed(handle, g_audioDevice.speed);
            }
            if (USB_SPEED_HIGH == g_audioDevice.speed)
            {
                g_audioDevice.streamInPacketSize = HS_ISO_IN_ENDP_PACKET_SIZE;
                g_audioDevice.streamOutPacketSize = HS_ISO_OUT_ENDP_PACKET_SIZE;
            }
#endif
        }
        break;
        case kUSB_DeviceEventSetConfiguration:
            if (0U == (*temp8))
            {
                g_audioDevice.attach               = 0U;
                g_audioDevice.currentConfiguration = 0U;
                error                                 = kStatus_USB_Success;
            }
            else if (USB_AUDIO_CONFIGURE_INDEX == (*temp8))
            {
                /* Set the configuration request */
                g_audioDevice.attach               = 1U;
                g_audioDevice.currentConfiguration = *temp8;
                error                                 = kStatus_USB_Success;
            }
            else
            {
                /* no action */
            }
            break;
        case kUSB_DeviceEventSetInterface:
            if (0U != g_audioDevice.attach)
            {
                /* Set alternateSetting of the interface request */
                uint8_t interface        = (uint8_t)((*temp16 & 0xFF00U) >> 0x08U);
                uint8_t alternateSetting = (uint8_t)(*temp16 & 0x00FFU);

                if (USB_AUDIO_CONTROL_INTERFACE_INDEX == interface)
                {
                    if (alternateSetting < USB_AUDIO_CONTROL_INTERFACE_ALTERNATE_COUNT)
                    {
                        g_audioDevice.currentInterfaceAlternateSetting[interface] = alternateSetting;
                        error                                                        = kStatus_USB_Success;
                    }
                }
                else if (USB_AUDIO_STREAM_IN_INTERFACE_INDEX == interface)
                {
                    if (alternateSetting < USB_AUDIO_STREAM_INTERFACE_ALTERNATE_COUNT)
                    {
                        g_audioDevice.currentInterfaceAlternateSetting[interface] = alternateSetting;
                        error                                                        = kStatus_USB_Success;
                        if (USB_AUDIO_STREAM_INTERFACE_ALTERNATE_1 == alternateSetting)
                        {
                            USB_AudioI2s2UsbBuffer(g_usbBuffIn, g_audioDevice.streamInPacketSize);
                            error = USB_DeviceAudioSend(g_audioDevice.audioHandle, USB_AUDIO_STREAM_IN_ENDPOINT,
                                                        g_usbBuffIn, g_audioDevice.streamInPacketSize);
                        }
                    }
                }
                else if (USB_AUDIO_STREAM_OUT_INTERFACE_INDEX == interface)
                {
                    if (alternateSetting < USB_AUDIO_STREAM_INTERFACE_ALTERNATE_COUNT)
                    {
                        g_audioDevice.currentInterfaceAlternateSetting[interface] = alternateSetting;
                        error                                                        = kStatus_USB_Success;
                        if (USB_AUDIO_STREAM_INTERFACE_ALTERNATE_1 == alternateSetting)
                        {
                            error = USB_DeviceAudioRecv(g_audioDevice.audioHandle, USB_AUDIO_STREAM_OUT_ENDPOINT,
                                                        g_usbBuffOut, g_audioDevice.streamOutPacketSize);
                        }
                    }
                }
                else
                {
                    /* no action */
                }
            }
            break;
        case kUSB_DeviceEventGetConfiguration:
            if (NULL != param)
            {
                /* Get the current configuration request */
                *temp8 = g_audioDevice.currentConfiguration;
                error  = kStatus_USB_Success;
            }
            break;
        case kUSB_DeviceEventGetInterface:
            if (NULL != param)
            {
                uint8_t interface = (uint8_t)((*temp16 & 0xFF00U) >> 0x08U);
                if (interface < USB_AUDIO_INTERFACE_COUNT)
                {
                    *temp16 = (*temp16 & 0xFF00U) | g_audioDevice.currentInterfaceAlternateSetting[interface];
                    error   = kStatus_USB_Success;
                }
            }
            break;
        case kUSB_DeviceEventGetDeviceDescriptor:
            if (NULL != param)
            {
                /* Get the device descriptor request */
                error = USB_DeviceGetDeviceDescriptor(handle, (usb_device_get_device_descriptor_struct_t *)param);
            }
            break;
        case kUSB_DeviceEventGetConfigurationDescriptor:
            if (NULL != param)
            {
                /* Get the configuration descriptor request */
                error = USB_DeviceGetConfigurationDescriptor(handle,
                                                             (usb_device_get_configuration_descriptor_struct_t *)param);
            }
            break;
        case kUSB_DeviceEventGetStringDescriptor:
            if (NULL != param)
            {
                /* Get the string descriptor request */
                error = USB_DeviceGetStringDescriptor(handle, (usb_device_get_string_descriptor_struct_t *)param);
            }
            break;
        default:
            /* no action */
            break;
    }

    return error;
}

/*!
 * @brief Application initialization function.
 *
 * This function initializes the application.
 *
 * @return None.
 */
void USB_DeviceApplicationInit(void)
{
    USB_DeviceClockInit();
#if (defined(FSL_FEATURE_SOC_SYSMPU_COUNT) && (FSL_FEATURE_SOC_SYSMPU_COUNT > 0U))
    SYSMPU_Enable(SYSMPU, 0);
#endif /* FSL_FEATURE_SOC_SYSMPU_COUNT */

#if defined(USB_DEVICE_AUDIO_USE_SYNC_MODE) && (USB_DEVICE_AUDIO_USE_SYNC_MODE > 0U)
    SCTIMER_CaptureInit();
#endif

    if (kStatus_USB_Success != USB_DeviceClassInit(CONTROLLER_ID, &s_audioConfigList, &g_audioDevice.deviceHandle))
    {
        usb_echo("USB device failed\r\n");
        return;
    }
    else
    {
        usb_echo("USB device audio device demo\r\n");
        g_audioDevice.audioHandle = s_audioConfigList.config->classHandle;
    }

    USB_DeviceIsrEnable();

    /*Add one delay here to make the DP pull down long enough to allow host to detect the previous disconnection.*/
    SDK_DelayAtLeastUs(5000, SDK_DEVICE_MAXIMUM_CPU_CLOCK_FREQUENCY);
    USB_DeviceRun(g_audioDevice.deviceHandle);
}

#if USB_DEVICE_CONFIG_USE_TASK
void USBDeviceTask(void *handle)
{
    while (1U)
    {
        USB_DeviceTaskFn(handle);
    }
}
#endif

/*!
 * @brief Application task function.
 *
 * This function runs the task for application.
 *
 * @return None.
 */
void APPTask(void *handle)
{
    USB_DeviceApplicationInit();

#if USB_DEVICE_CONFIG_USE_TASK
    if (g_audioDevice.deviceHandle)
    {
        if (xTaskCreate(USBDeviceTask,                     /* pointer to the task */
                        "usb device task",                 /* task name for kernel awareness debugging */
                        5000L / sizeof(portSTACK_TYPE),    /* task stack size */
                        g_audioDevice.deviceHandle,     /* optional task startup argument */
                        5,                                 /* initial priority */
                        &g_audioDevice.deviceTaskHandle /* optional task handle to create */
                        ) != pdPASS)
        {
            usb_echo("usb device task create failed!\r\n");
            return;
        }
    }
#endif

    while (1)
    {
    }
}

#if defined(__CC_ARM) || (defined(__ARMCC_VERSION)) || defined(__GNUC__)
int main(void)
#else
void main(void)
#endif
{
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();

    CLOCK_EnableClock(kCLOCK_InputMux);

#if defined(USB_DEVICE_AUDIO_USE_SYNC_MODE) && (USB_DEVICE_AUDIO_USE_SYNC_MODE > 0U)
    /* attach AUDIO PLL clock to SCTimer input7. */
    CLOCK_AttachClk(kAUDIO_PLL_to_SCT_CLK);
    CLOCK_SetClkDiv(kCLOCK_DivSctClk, 1);

    g_audioDevice.curAudioPllFrac = CLKCTL1->AUDIOPLL0NUM;
#endif

    BOARD_I2S_Init();

    if (xTaskCreate(APPTask,                                /* pointer to the task */
                    "app task",                             /* task name for kernel awareness debugging */
                    5000L / sizeof(portSTACK_TYPE),         /* task stack size */
                    &g_audioDevice,                      /* optional task startup argument */
                    4,                                      /* initial priority */
                    &g_audioDevice.applicationTaskHandle /* optional task handle to create */
                    ) != pdPASS)
    {
        usb_echo("app task create failed!\r\n");
#if (defined(__CC_ARM) || (defined(__ARMCC_VERSION)) || defined(__GNUC__))
        return 1U;
#else
        return;
#endif
    }
    vTaskStartScheduler();

#if (defined(__CC_ARM) || (defined(__ARMCC_VERSION)) || defined(__GNUC__))
    return 1U;
#endif
}
