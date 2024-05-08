/*
 * Copyright (c) 2015 - 2016, Freescale Semiconductor, Inc.
 * Copyright 2016 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __USB_DEVICE_DESCRIPTOR_H__
#define __USB_DEVICE_DESCRIPTOR_H__

/*******************************************************************************
 * Definitions
 ******************************************************************************/
/*! @brief Whether USB Audio use syn mode or not, note that some socs may not support sync mode */
#define USB_DEVICE_AUDIO_USE_SYNC_MODE (0U)

#define USB_DEVICE_VID (0x1FC9U)
#define USB_DEVICE_PID (0x0097U)

#define USB_DEVICE_SPECIFIC_BCD_VERSION (0x0200U)
#define USB_DEVICE_DEMO_BCD_VERSION     (0x0101U)

#define USB_DEVICE_MAX_POWER (0x32U)

/* usb descriptor length */
#define USB_DESCRIPTOR_LENGTH_CONFIGURATION_ALL        (sizeof(g_UsbDeviceConfigurationDescriptor))

#define USB_AUDIO_STANDARD_AS_ISO_DATA_ENDPOINT_LENGTH (7U)
#define USB_AUDIO_CLASS_SPECIFIC_ENDPOINT_LENGTH       (8U)
#define USB_AUDIO_CONTROL_INTERFACE_HEADER_LENGTH      (9U)
#define USB_AUDIO_INTERFACE_ASSOCIATION_DESC_LENGTH    (8U)
#define USB_AUDIO_CLOCK_SOURCE_DESC_LENGTH             (8U)
#define USB_AUDIO_INPUT_TERMINAL_DESC_LENGTH           (17U)
#define USB_AUDIO_OUTPUT_TERMINAL_DESC_LENGTH          (12U)
#define USB_AUDIO_FEATURE_UNIT_DESC_LENGTH(ch)         (6 + ((ch) + 1) * 4)
#define USB_AUDIO_AS_INTERFACE_DESC_LENGTH             (16U)
#define USB_AUDIO_TYPE_I_FORMAT_TYPE_DESC_LENGTH       (6U)

/* Configuration, interface and endpoint. */
#define USB_DEVICE_CONFIGURATION_COUNT (1U)
#define USB_DEVICE_STRING_COUNT        (15U)
#define USB_DEVICE_LANGUAGE_COUNT      (1U)
#define USB_INTERFACE_COUNT            (1U)
#define USB_AUDIO_ENDPOINT_COUNT       (1U)

#define USB_AUDIO_CONFIGURE_INDEX (1U)

#define USB_AUDIO_CONTROL_INTERFACE_INDEX    (0U)
#define USB_AUDIO_STREAM_IN_INTERFACE_INDEX  (1U)
#define USB_AUDIO_STREAM_OUT_INTERFACE_INDEX (2U)

#define USB_AUDIO_STREAM_ENDPOINT_COUNT  (1U)
#define USB_AUDIO_CONTROL_ENDPOINT_COUNT (1U)

#define USB_AUDIO_STREAM_IN_ENDPOINT  (2U)
#define USB_AUDIO_STREAM_OUT_ENDPOINT (3U)
#define USB_AUDIO_CONTROL_ENDPOINT    (1U)

#define USB_AUDIO_CONTROL_INTERFACE_COUNT (1U)
#define USB_AUDIO_STREAM_INTERFACE_COUNT  (2U)
#define USB_AUDIO_INTERFACE_COUNT (USB_AUDIO_CONTROL_INTERFACE_COUNT + USB_AUDIO_STREAM_INTERFACE_COUNT)

#define USB_AUDIO_CONTROL_INTERFACE_ALTERNATE_COUNT (1U)
#define USB_AUDIO_STREAM_INTERFACE_ALTERNATE_COUNT  (2U)
#define USB_AUDIO_CONTROL_INTERFACE_ALTERNATE_0     (0U)
#define USB_AUDIO_STREAM_INTERFACE_ALTERNATE_0      (0U)
#define USB_AUDIO_STREAM_INTERFACE_ALTERNATE_1      (1U)

/* Audio data format */
#define AUDIO_SAMPLING_RATE_KHZ (48U)
#define AUDIO_FORMAT_CHANNELS   (0x10U)
#define AUDIO_FORMAT_BITS       (32U)
#define AUDIO_FORMAT_SIZE       (0x04U)

/* Packet size and interval. */
#define HS_INTERRUPT_IN_PACKET_SIZE (8U)
#define FS_INTERRUPT_IN_PACKET_SIZE (8U)

#define HS_INTERRUPT_OUT_PACKET_SIZE (8U)
#define FS_INTERRUPT_OUT_PACKET_SIZE (8U)

/** For a bInterval == 1 (125us) */
#define HS_ISO_IN_ENDP_PACKET_SIZE ((AUDIO_SAMPLING_RATE_KHZ * AUDIO_FORMAT_CHANNELS * AUDIO_FORMAT_SIZE) / 8)
#define FS_ISO_IN_ENDP_PACKET_SIZE (AUDIO_SAMPLING_RATE_KHZ * AUDIO_FORMAT_CHANNELS * AUDIO_FORMAT_SIZE)

#define HS_ISO_OUT_ENDP_PACKET_SIZE ((AUDIO_SAMPLING_RATE_KHZ * AUDIO_FORMAT_CHANNELS * AUDIO_FORMAT_SIZE) / 8)
#define FS_ISO_OUT_ENDP_PACKET_SIZE (AUDIO_SAMPLING_RATE_KHZ * AUDIO_FORMAT_CHANNELS * AUDIO_FORMAT_SIZE)

#define HS_ISO_IN_ENDP_INTERVAL  (0x01U) /* 125us */
#define FS_ISO_IN_ENDP_INTERVAL  (0x01U)
#define ISO_IN_ENDP_INTERVAL     (0x01U)
#define HS_INTERRUPT_IN_INTERVAL (0x07U) /* 2^(7-1) = 8ms */
#define FS_INTERRUPT_IN_INTERVAL (0x08U)

#define HS_ISO_OUT_ENDP_INTERVAL  (0x01U) /* 125us */
#define FS_ISO_OUT_ENDP_INTERVAL  (0x01U)
#define ISO_OUT_ENDP_INTERVAL     (0x01U)
#define HS_INTERRUPT_OUT_INTERVAL (0x07U) /* 2^(7-1) = 8ms */
#define FS_INTERRUPT_OUT_INTERVAL (0x08U)

/* String descriptor length. */
#define USB_DESCRIPTOR_LENGTH_STRING0 (sizeof(g_UsbDeviceString0))
#define USB_DESCRIPTOR_LENGTH_STRING1 (sizeof(g_UsbDeviceString1))
#define USB_DESCRIPTOR_LENGTH_STRING2 (sizeof(g_UsbDeviceString2))

/* Class code. */
#define USB_DEVICE_CLASS    (0x00U)
#define USB_DEVICE_SUBCLASS (0x00U)
#define USB_DEVICE_PROTOCOL (0x00U)

#define USB_AUDIO_CLASS           (0x01U)
#define USB_SUBCLASS_AUDIOCONTROL (0x01U)
#define USB_SUBCLASS_AUDIOSTREAM  (0x02U)
#define USB_AUDIO_PROTOCOL        (0x20U)

#define USB_AUDIO_STREAM_ENDPOINT_DESCRIPTOR    (0x25U)
#define USB_AUDIO_EP_GENERAL_DESCRIPTOR_SUBTYPE (0x01U)

#define USB_AUDIO_RECORDER_CONTROL_CLOCK_SOURCE_ENTITY_ID (0x10U)
#define USB_AUDIO_PLAYER_CONTROL_CLOCK_SOURCE_ENTITY_ID   (0x11U)

#define USB_AUDIO_RECORDER_CONTROL_INPUT_TERMINAL_ID (0x01U)
#define USB_AUDIO_PLAYER_CONTROL_INPUT_TERMINAL_ID   (0x04U)

#define USB_AUDIO_RECORDER_CONTROL_FEATURE_UNIT_ID (0x02U)
#define USB_AUDIO_PLAYER_CONTROL_FEATURE_UNIT_ID   (0x05U)

#define USB_AUDIO_RECORDER_CONTROL_OUTPUT_TERMINAL_ID (0x03U)
#define USB_AUDIO_PLAYER_CONTROL_OUTPUT_TERMINAL_ID   (0x06U)

/*******************************************************************************
 * API
 ******************************************************************************/
/*!
 * @brief USB device set speed function.
 *
 * This function sets the speed of the USB device.
 *
 * @param handle The USB device handle.
 * @param speed Speed type. USB_SPEED_HIGH/USB_SPEED_FULL/USB_SPEED_LOW.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
extern usb_status_t USB_DeviceSetSpeed(usb_device_handle handle, uint8_t speed);
/*!
 * @brief USB device get device descriptor function.
 *
 * This function gets the device descriptor of the USB device.
 *
 * @param handle The USB device handle.
 * @param deviceDescriptor The pointer to the device descriptor structure.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceGetDeviceDescriptor(usb_device_handle handle,
                                           usb_device_get_device_descriptor_struct_t *deviceDescriptor);
/*!
 * @brief USB device get configuration descriptor function.
 *
 * This function gets the configuration descriptor of the USB device.
 *
 * @param handle The USB device handle.
 * @param configurationDescriptor The pointer to the configuration descriptor structure.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceGetConfigurationDescriptor(
    usb_device_handle handle, usb_device_get_configuration_descriptor_struct_t *configurationDescriptor);

usb_status_t USB_DeviceGetStringDescriptor(usb_device_handle handle,
                                           usb_device_get_string_descriptor_struct_t *stringDescriptor);

#endif /* __USB_DESCRIPTOR_H__ */
