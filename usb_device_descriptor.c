/*
 * Copyright (c) 2015 - 2016, Freescale Semiconductor, Inc.
 * Copyright 2016, 2019 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "usb_device_config.h"
#include "usb.h"
#include "usb_device.h"

#include "usb_device_class.h"
#include "usb_device_audio.h"
#include "usb_audio_config.h"
#include "usb_device_descriptor.h"
#include "tdm2usb.h"

#include "usb_device_strings.h"

/**
 *                                                           +-------------------+
 *              <- EP 2 IN (data) [SOURCE]              +----+ CLOCK_SOURCE  (6) +----+
 *             +--------------+                         |    +-------------------+    |
 *        USB  |              |  I2S                    |                             |
 * PC  <-------+      IN      <-------+ DEV   +---------v----------+       +----------v----------+
 *             |              |  RX           | INPUT_TERMINAL (2) +------>+ OUTPUT_TERMINAL (4) |
 *             +--------------+               | Microphone         |       | USB Streaming       |
 *              GENERATOR                     +--------------------+       +---------------------+
 *
 *
 *              <- EP 1 IN (feedback)                        +-------------------+
 *              -> EP 1 OUT (data) [SINK]               +----+ CLOCK_SOURCE  (5) +----+
 *             +--------------+                         |    +-------------------+    |
 *        USB  |              |  I2S                    |                             |
 * PC  +------->     OUT      +-------> DEV   +---------v----------+       +----------v----------+
 *             |              |  TX           | INPUT_TERMINAL (1) +------>+ OUTPUT_TERMINAL (3) |
 *             +--------------+               | USB Streaming      |       | Speaker             |
 *              SPEAKER                       +--------------------+       +---------------------+
 *
 * ...................................................................................................
 *
 *                +------------------------------+------------------------------+
 *                |                              |                              |
 *                | Source                       | Sink                         |
 *                |                              |                              |
 * +----------------------------------------------------------------------------+
 * |              |                              |                              |
 * | Asynchronous | Free running Fs              | Free running Fs              |
 * |              |                              |                              |
 * |              | Provides implicit            | Provides explicit            |
 * |              | feedforward (data stream)    | feedback (isochronous pipe)  |
 * |              |                              |                              |
 * +----------------------------------------------------------------------------+
 * |              |                              |                              |
 * |  Synchronous | Fs locked to SOF             | Fs locked to SOF             |
 * |              |                              |                              | <--- (with programmable PLL)
 * |              | Uses implicit feedback (SOF) | Uses implicit feedback (SOF) |
 * |              |                              |                              |
 * +----------------------------------------------------------------------------+
 * |              |                              |                              |
 * |     Adaptive | Fs locked to sink            | Fs locked to data flow       |
 * |              |                              |                              |
 * |              | Uses explicit feedback       | Uses implicit feedforward    |
 * |              | (isochronous pipe)           | (data stream)                |
 * |              |                              |                              |
 * +--------------+------------------------------+------------------------------+
 *
 * GENERATOR [source]: - Synch EP
 *                     - USB_DEVICE_AUDIO_USE_SYNC_MODE not set
 *                     - No feedback EP
 *
 * SPEAKER [sink]: - Asynch EP
 *                 - USB_DEVICE_AUDIO_USE_SYNC_MODE set by default
 *                 - Feedback EP only when USB_DEVICE_AUDIO_USE_SYNC_MODE is NOT set
 */

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

/*******************************************************************************
 * Variables
 ******************************************************************************/
/* Audio generator stream endpoint information */
usb_device_endpoint_struct_t g_UsbDeviceAudioGeneratorInEndpoints[USB_AUDIO_STREAM_IN_ENDPOINT_COUNT] = {
    /* Audio generator ISO IN pipe */
    {
        USB_AUDIO_STREAM_IN_ENDPOINT | (USB_IN << USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_SHIFT),
        USB_ENDPOINT_ISOCHRONOUS,
        FS_ISO_IN_ENDP_PACKET_SIZE,
        FS_ISO_IN_ENDP_INTERVAL,
    },
};

usb_device_endpoint_struct_t g_UsbDeviceAudioGeneratorOutEndpoints[USB_AUDIO_STREAM_OUT_ENDPOINT_COUNT] = {
    /* Audio generator ISO OUT pipe */
    {
        USB_AUDIO_STREAM_OUT_ENDPOINT | (USB_OUT << USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_SHIFT),
        USB_ENDPOINT_ISOCHRONOUS,
        FS_ISO_OUT_ENDP_PACKET_SIZE,
        FS_ISO_OUT_ENDP_INTERVAL,
    },
    {
        USB_AUDIO_STREAM_OUT_FEEDBACK_ENDPOINT | (USB_IN << USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_SHIFT),
        USB_ENDPOINT_ISOCHRONOUS,
        FS_ISO_OUT_FEEDBACK_ENDP_PACKET_SIZE,
        FS_ISO_OUT_FEEDBACK_ENDP_INTERVAL,
    },
};

/* Audio generator control endpoint information */
usb_device_endpoint_struct_t g_UsbDeviceAudioControlEndpoints[USB_AUDIO_CONTROL_ENDPOINT_COUNT] = {
    {
        USB_AUDIO_CONTROL_ENDPOINT | (USB_IN << USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_SHIFT),
        USB_ENDPOINT_INTERRUPT,
        FS_INTERRUPT_IN_PACKET_SIZE,
        FS_INTERRUPT_IN_INTERVAL,
    },
};

/* Audio generator entity struct */
usb_device_audio_entity_struct_t g_UsbDeviceAudioEntity[] = {
    {
        USB_AUDIO_IN_CONTROL_CLOCK_SOURCE_ENTITY_ID,
        USB_DESCRIPTOR_SUBTYPE_AUDIO_CONTROL_CLOCK_SOURCE_UNIT,
        0U,
    },
    {
        USB_AUDIO_OUT_CONTROL_CLOCK_SOURCE_ENTITY_ID,
        USB_DESCRIPTOR_SUBTYPE_AUDIO_CONTROL_CLOCK_SOURCE_UNIT,
        0U,
    },
    {
        USB_AUDIO_IN_CONTROL_INPUT_TERMINAL_ID,
        USB_DESCRIPTOR_SUBTYPE_AUDIO_CONTROL_INPUT_TERMINAL,
        0U,
    },
    {
        USB_AUDIO_OUT_CONTROL_INPUT_TERMINAL_ID,
        USB_DESCRIPTOR_SUBTYPE_AUDIO_CONTROL_INPUT_TERMINAL,
        0U,
    },
    {
        USB_AUDIO_IN_CONTROL_OUTPUT_TERMINAL_ID,
        USB_DESCRIPTOR_SUBTYPE_AUDIO_CONTROL_OUTPUT_TERMINAL,
        0U,
    },
    {
        USB_AUDIO_OUT_CONTROL_OUTPUT_TERMINAL_ID,
        USB_DESCRIPTOR_SUBTYPE_AUDIO_CONTROL_OUTPUT_TERMINAL,
        0U,
    },
};

/* Audio generator entity information */
usb_device_audio_entities_struct_t g_UsbDeviceAudioEntities = {
    g_UsbDeviceAudioEntity,
    sizeof(g_UsbDeviceAudioEntity) / sizeof(usb_device_audio_entity_struct_t),
};

/* Audio generator control interface information */
usb_device_interface_struct_t g_UsbDeviceAudioControInterface[] = {{
    USB_AUDIO_CONTROL_INTERFACE_ALTERNATE_0,
    {
        USB_AUDIO_CONTROL_ENDPOINT_COUNT,
        g_UsbDeviceAudioControlEndpoints,
    },
    &g_UsbDeviceAudioEntities,
}};

/* Audio generator stream interface information */
usb_device_interface_struct_t g_UsbDeviceAudioStreamInInterface[] = {
    {
        USB_AUDIO_STREAM_INTERFACE_ALTERNATE_0,
        {
            0U,
            NULL,
        },
        NULL,
    },
    {
        USB_AUDIO_STREAM_INTERFACE_ALTERNATE_1,
        {
            USB_AUDIO_STREAM_IN_ENDPOINT_COUNT,
            g_UsbDeviceAudioGeneratorInEndpoints,
        },
        NULL,
    },
};

/* Audio generator stream interface information */
usb_device_interface_struct_t g_UsbDeviceAudioStreamOutInterface[] = {
    {
        USB_AUDIO_STREAM_INTERFACE_ALTERNATE_0,
        {
            0U,
            NULL,
        },
        NULL,
    },
    {
        USB_AUDIO_STREAM_INTERFACE_ALTERNATE_1,
        {
            USB_AUDIO_STREAM_OUT_ENDPOINT_COUNT,
            g_UsbDeviceAudioGeneratorOutEndpoints,
        },
        NULL,
    },
};

/* Define interfaces for audio generator */
usb_device_interfaces_struct_t g_UsbDeviceAudioInterfaces[USB_AUDIO_INTERFACE_COUNT] = {
    {
        USB_AUDIO_INTERFACE_CONTROL,
        USB_AUDIO_CLASS,                   /* Audio class code */
        USB_SUBCLASS_AUDIOCONTROL,         /* Audio control subclass code */
        USB_AUDIO_PROTOCOL,                /* Audio protocol code */
        USB_AUDIO_CONTROL_INTERFACE_INDEX, /* The interface number of the Audio control */
        g_UsbDeviceAudioControInterface,   /* The handle of Audio control */
        sizeof(g_UsbDeviceAudioControInterface) / sizeof(usb_device_interface_struct_t),
    },
    {
        USB_AUDIO_INTERFACE_STREAM_IN,
        USB_AUDIO_CLASS,                  /* Audio class code */
        USB_SUBCLASS_AUDIOSTREAM,         /* Audio stream subclass code */
        USB_AUDIO_PROTOCOL,               /* Audio protocol code */
        USB_AUDIO_STREAM_IN_INTERFACE_INDEX, /* The interface number of the Audio control */
        g_UsbDeviceAudioStreamInInterface,  /* The handle of Audio stream */
        sizeof(g_UsbDeviceAudioStreamInInterface) / sizeof(usb_device_interface_struct_t),
    },
    {
        USB_AUDIO_INTERFACE_STREAM_OUT,
        USB_AUDIO_CLASS,                  /* Audio class code */
        USB_SUBCLASS_AUDIOSTREAM,         /* Audio stream subclass code */
        USB_AUDIO_PROTOCOL,               /* Audio protocol code */
        USB_AUDIO_STREAM_OUT_INTERFACE_INDEX, /* The interface number of the Audio control */
        g_UsbDeviceAudioStreamOutInterface,  /* The handle of Audio stream */
        sizeof(g_UsbDeviceAudioStreamOutInterface) / sizeof(usb_device_interface_struct_t),
    }
};

/* Define configurations for audio generator */
usb_device_interface_list_t g_UsbDeviceAudioInterfaceList[USB_DEVICE_CONFIGURATION_COUNT] = {
    {
        USB_AUDIO_INTERFACE_COUNT,
        g_UsbDeviceAudioInterfaces,
    },
};

/* Define class information for audio generator */
usb_device_class_struct_t g_UsbDeviceAudioClass = {
    g_UsbDeviceAudioInterfaceList,
    kUSB_DeviceClassTypeAudio,
    USB_DEVICE_CONFIGURATION_COUNT,
};

/**
 * Device Descriptor:
 * bLength                18
 * bDescriptorType         1
 * bcdUSB               2.00
 * bDeviceClass            0
 * bDeviceSubClass         0
 * bDeviceProtocol         0
 * bMaxPacketSize0        64
 * idVendor           0x1fc9 NXP Semiconductors
 * idProduct          0x0097
 * bcdDevice            1.01
 * iManufacturer           1 NXP SEMICONDUCTORS
 * iProduct                2 USB AUDIO DEMO
 * iSerial                 0
 * bNumConfigurations      1
 */
USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceDescriptor[] = {
    USB_DESCRIPTOR_LENGTH_DEVICE, /* Size of this descriptor in bytes */
    USB_DESCRIPTOR_TYPE_DEVICE,   /* DEVICE Descriptor Type */
    USB_SHORT_GET_LOW(USB_DEVICE_SPECIFIC_BCD_VERSION),
    USB_SHORT_GET_HIGH(USB_DEVICE_SPECIFIC_BCD_VERSION), /* USB Specification Release Number in
                                                            Binary-Coded Decimal (i.e., 2.10 is 210H). */
    USB_DEVICE_CLASS,                                    /* Class code (assigned by the USB-IF). */
    USB_DEVICE_SUBCLASS,                                 /* Subclass code (assigned by the USB-IF). */
    USB_DEVICE_PROTOCOL,                                 /* Protocol code (assigned by the USB-IF). */
    USB_CONTROL_MAX_PACKET_SIZE,                         /* Maximum packet size for endpoint zero
                                                            (only 8, 16, 32, or 64 are valid) */
    USB_SHORT_GET_LOW(USB_DEVICE_VID),
    USB_SHORT_GET_HIGH(USB_DEVICE_VID), /* Vendor ID (assigned by the USB-IF) */
    USB_SHORT_GET_LOW(USB_DEVICE_PID),
    USB_SHORT_GET_HIGH(USB_DEVICE_PID), /* Product ID (assigned by the manufacturer) */
    USB_SHORT_GET_LOW(USB_DEVICE_DEMO_BCD_VERSION),
    USB_SHORT_GET_HIGH(USB_DEVICE_DEMO_BCD_VERSION), /* Device release number in binary-coded decimal */
    0x01U,                                           /* Index of string descriptor describing manufacturer */
    0x02U,                                           /* Index of string descriptor describing product */
    0x03U,                                           /* Index of string descriptor describing the
                                                        device's serial number */
    USB_DEVICE_CONFIGURATION_COUNT,                  /* Number of possible configurations */
};

#define TOTAL_LENGHT (USB_DESCRIPTOR_LENGTH_CONFIGURE + \
                      USB_AUDIO_INTERFACE_ASSOCIATION_DESC_LENGTH + \
                      USB_DESCRIPTOR_LENGTH_INTERFACE + \
                      USB_AUDIO_CONTROL_INTERFACE_HEADER_LENGTH + \
                      (2 * USB_AUDIO_CLOCK_SOURCE_DESC_LENGTH) + \
                      (2 * USB_AUDIO_INPUT_TERMINAL_DESC_LENGTH) + \
                      (2 * USB_AUDIO_OUTPUT_TERMINAL_DESC_LENGTH) + \
                      USB_DESCRIPTOR_LENGTH_INTERFACE + \
                      USB_DESCRIPTOR_LENGTH_INTERFACE + \
                      USB_AUDIO_AS_INTERFACE_DESC_LENGTH + \
                      USB_AUDIO_TYPE_I_FORMAT_TYPE_DESC_LENGTH + \
                      USB_AUDIO_STANDARD_AS_ISO_DATA_ENDPOINT_LENGTH + \
                      USB_AUDIO_CLASS_SPECIFIC_ENDPOINT_LENGTH + \
                      USB_DESCRIPTOR_LENGTH_INTERFACE + \
                      USB_DESCRIPTOR_LENGTH_INTERFACE + \
                      USB_AUDIO_AS_INTERFACE_DESC_LENGTH + \
                      USB_AUDIO_TYPE_I_FORMAT_TYPE_DESC_LENGTH + \
                      USB_AUDIO_STANDARD_AS_ISO_DATA_ENDPOINT_LENGTH + \
                      USB_AUDIO_STANDARD_AS_ISO_DATA_ENDPOINT_LENGTH + \
                      USB_AUDIO_CLASS_SPECIFIC_ENDPOINT_LENGTH)

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceConfigurationDescriptor[] = {

    /**
     * Configuration Descriptor:
     * bLength                 9
     * bDescriptorType         2
     * wTotalLength       0x00e9
     * bNumInterfaces          3
     * bConfigurationValue     1
     * iConfiguration          0
     * bmAttributes         0xc0
     *   Self Powered
     * MaxPower              500mA
     */
    USB_DESCRIPTOR_LENGTH_CONFIGURE, /* Size of this descriptor in bytes */
    USB_DESCRIPTOR_TYPE_CONFIGURE,   /* CONFIGURATION Descriptor Type */
    USB_SHORT_GET_LOW(TOTAL_LENGHT),
    USB_SHORT_GET_HIGH(TOTAL_LENGHT), /* Total length of data returned for this configuration. */
    USB_AUDIO_INTERFACE_COUNT,           /* Number of interfaces supported by this configuration */
    USB_AUDIO_CONFIGURE_INDEX,           /* Value to use as an argument to the
                                                      SetConfiguration() request to select this configuration */
    0x00U,                                         /* Index of string descriptor describing this configuration */
    (USB_DESCRIPTOR_CONFIGURE_ATTRIBUTE_D7_MASK) |
        (USB_DEVICE_CONFIG_SELF_POWER << USB_DESCRIPTOR_CONFIGURE_ATTRIBUTE_SELF_POWERED_SHIFT) |
        (USB_DEVICE_CONFIG_REMOTE_WAKEUP << USB_DESCRIPTOR_CONFIGURE_ATTRIBUTE_REMOTE_WAKEUP_SHIFT),
    /* Configuration characteristics
       D7: Reserved (set to one)
       D6: Self-powered
       D5: Remote Wakeup
       D4...0: Reserved (reset to zero)
    */
    0xFAU, /** Maximum power consumption of the USB
            * device from the bus in this specific
            * configuration when the device is fully
            * operational. Expressed in 2 mA units
            *  (i.e., 50 = 100 mA).
            */
    /**
     * Interface Association:
     * bLength                 8
     * bDescriptorType        11
     * bFirstInterface         0
     * bInterfaceCount         3
     * bFunctionClass          1 Audio
     * bFunctionSubClass       0
     * bFunctionProtocol      32
     * iFunction               4
     */
    USB_AUDIO_INTERFACE_ASSOCIATION_DESC_LENGTH, /* Descriptor size is 8 bytes  */
    USB_DESCRIPTOR_TYPE_INTERFACE_ASSOCIATION, /* INTERFACE_ASSOCIATION Descriptor Type   */
    0x00U,                                     /* The first interface number associated with this function is 0   */
    0x03U,              /* The number of contiguous interfaces associated with this function is 3   */
    USB_AUDIO_CLASS,    /* The function belongs to the Audio Interface Class  */
    0x00U,              /* The function belongs to the SUBCLASS_UNDEFINED Subclass   */
    USB_AUDIO_PROTOCOL, /* Protocol code = 32   */
    0x04U,              /* The Function string descriptor index is 4  */

    /**
     * Interface Descriptor:
     * bLength                 9
     * bDescriptorType         4
     * bInterfaceNumber        0
     * bAlternateSetting       0
     * bNumEndpoints           0
     * bInterfaceClass         1 Audio
     * bInterfaceSubClass      1 Control Device
     * bInterfaceProtocol     32
     * iInterface              5
     */
    USB_DESCRIPTOR_LENGTH_INTERFACE, /* Size of the descriptor, in bytes  */
    USB_DESCRIPTOR_TYPE_INTERFACE,   /* INTERFACE Descriptor Type   */
    USB_AUDIO_CONTROL_INTERFACE_INDEX, /* The number of this interface is 0 */
    USB_AUDIO_CONTROL_INTERFACE_ALTERNATE_0, /* The value used to select the alternate setting for this interface is 0   */
    0x00U,                     /* The number of endpoints used by this interface is 0 (excluding endpoint zero)   */
    USB_AUDIO_CLASS,           /* The interface implements the Audio Interface class   */
    USB_SUBCLASS_AUDIOCONTROL, /* The interface implements the AUDIOCONTROL Subclass  */
    USB_AUDIO_PROTOCOL,        /* The Protocol code is 32  */
    0x05U,                     /* The interface string descriptor index is 5  */

    /**
     * AudioControl Interface Descriptor:
     * bLength                 9
     * bDescriptorType        36
     * bDescriptorSubtype      1 (HEADER)
     * bcdADC               2.00
     * bCategory               8
     * wTotalLength       0x0061
     * bmControls           0x00
     */
    USB_AUDIO_CONTROL_INTERFACE_HEADER_LENGTH,   /* Size of the descriptor, in bytes  */
    USB_DESCRIPTOR_TYPE_AUDIO_CS_INTERFACE,      /* CS_INTERFACE Descriptor Type   */
    USB_DESCRIPTOR_SUBTYPE_AUDIO_CONTROL_HEADER, /* HEADER descriptor subtype  */
    0x00U,
    0x02U, /* Audio Device compliant to the USB Audio specification version 2.00  */
    0x08U, /* IO_BOX(0x08) : Indicating the primary use of this audio function   */
    0x61U,
    0x00U, /* Total number of bytes returned for the class-specific AudioControl interface descriptor. Includes
              the combined length of this descriptor header and all Unit and Terminal descriptors.   */
    0x00U, /* D1..0: Latency Control  */

    /**
     * AudioControl Interface Descriptor:
     * bLength                 8
     * bDescriptorType        36
     * bDescriptorSubtype     10 (CLOCK_SOURCE)
     * bClockID               16
     * bmAttributes            1 Internal fixed clock
     * bmControls           0x07
     *   Clock Frequency Control (read/write)
     *   Clock Validity Control (read-only)
     * bAssocTerminal          0
     * iClockSource            6
     */
    USB_AUDIO_CLOCK_SOURCE_DESC_LENGTH,                     /* Size of the descriptor, in bytes  */
    USB_DESCRIPTOR_TYPE_AUDIO_CS_INTERFACE,                 /* CS_INTERFACE Descriptor Type  */
    USB_DESCRIPTOR_SUBTYPE_AUDIO_CONTROL_CLOCK_SOURCE_UNIT, /* CLOCK_SOURCE descriptor subtype  */
    USB_AUDIO_IN_CONTROL_CLOCK_SOURCE_ENTITY_ID, /* Constant uniquely identifying the Clock Source Entity within
                                                          the audio funcion */
    0x01U,                                             /* D1..0: 01: Internal Fixed Clock
                                                          D2: 0 Clock is not synchronized to SOF
                                                          D7..3: Reserved, should set to 0   */
    0x07U, /* D1..0: Clock Frequency Control is present and Host programmable
              D3..2: Clock Validity Control is present but read-only
              D7..4: Reserved, should set to 0 */
    0x00U, /* This Clock Source has no association   */
    0x06U, /* Index of a string descriptor, describing the Clock Source Entity  */

    /**
     * AudioControl Interface Descriptor:
     * bLength                 8
     * bDescriptorType        36
     * bDescriptorSubtype     10 (CLOCK_SOURCE)
     * bClockID               17
     * bmAttributes            1 Internal fixed clock
     * bmControls           0x07
     *   Clock Frequency Control (read/write)
     *   Clock Validity Control (read-only)
     * bAssocTerminal          0
     * iClockSource            6
     */
    USB_AUDIO_CLOCK_SOURCE_DESC_LENGTH,                     /* Size of the descriptor, in bytes  */
    USB_DESCRIPTOR_TYPE_AUDIO_CS_INTERFACE,                 /* CS_INTERFACE Descriptor Type  */
    USB_DESCRIPTOR_SUBTYPE_AUDIO_CONTROL_CLOCK_SOURCE_UNIT, /* CLOCK_SOURCE descriptor subtype  */
    USB_AUDIO_OUT_CONTROL_CLOCK_SOURCE_ENTITY_ID, /* Constant uniquely identifying the Clock Source Entity within
                                                          the audio funcion */
    0x01U,                                             /* D1..0: 01: Internal Fixed Clock
                                                          D2: 0 Clock is not synchronized to SOF
                                                          D7..3: Reserved, should set to 0   */
    0x07U, /* D1..0: Clock Frequency Control is present and Host programmable
              D3..2: Clock Validity Control is present but read-only
              D7..4: Reserved, should set to 0 */
    0x00U, /* This Clock Source has no association   */
    0x06U, /* Index of a string descriptor, describing the Clock Source Entity  */

    /**
     * AudioControl Interface Descriptor:
     * bLength                17
     * bDescriptorType        36
     * bDescriptorSubtype      2 (INPUT_TERMINAL)
     * bTerminalID             1
     * wTerminalType      0x0201 Microphone
     * bAssocTerminal          0
     * bCSourceID             16
     * bNrChannels            16
     * bmChannelConfig    0x00000000
     * iChannelNames           0
     * bmControls         0x0000
     * iTerminal               9
     */
    USB_AUDIO_INPUT_TERMINAL_DESC_LENGTH,                /* Size of the descriptor, in bytes  */
    USB_DESCRIPTOR_TYPE_AUDIO_CS_INTERFACE,              /* CS_INTERFACE Descriptor Type   */
    USB_DESCRIPTOR_SUBTYPE_AUDIO_CONTROL_INPUT_TERMINAL, /* INPUT_TERMINAL descriptor subtype   */
    USB_AUDIO_IN_CONTROL_INPUT_TERMINAL_ID,        /* Constant uniquely identifying the Terminal within the audio
                     function. This value is used in all requests        to address this Terminal.   */
    0x01U,
    0x02U, /* A generic microphone that does not fit under any of the other classifications.  */
    0x00U, /* This Input Terminal has no association   */
    USB_AUDIO_IN_CONTROL_CLOCK_SOURCE_ENTITY_ID, /* ID of the Clock Entity to which this Input Terminal is
                                                          connected.  */
    0x10U, /* This Terminal's output audio channel cluster has 16 logical output channels   */
    0x00U,
    0x00U,
    0x00U,
    0x00U, /* Describes the spatial location of the logical channels:: Mono, no spatial location */
    0x00U, /* Index of a string descriptor, describing the name of the first logical channel.  */
    0x00U,
    0x00U, /* bmControls D1..0: Copy Protect Control is not present
              D3..2: Connector Control is not present
              D5..4: Overload Control is not present
              D7..6: Cluster Control is not present
              D9..8: Underflow Control is not present
              D11..10: Overflow Control is not present
              D15..12: Reserved, should set to 0*/
    0x09U, /* Index of a string descriptor, describing the Input Terminal.  */

    /**
     * AudioControl Interface Descriptor:
     * bLength                17
     * bDescriptorType        36
     * bDescriptorSubtype      2 (INPUT_TERMINAL)
     * bTerminalID             4
     * wTerminalType      0x0101 (USB Streaming)
     * bAssocTerminal          0
     * bCSourceID             17
     * bNrChannels            16
     * bmChannelConfig    0x00000000
     * iChannelNames           0
     * bmControls         0x0000
     * iTerminal               7
     */
    USB_AUDIO_INPUT_TERMINAL_DESC_LENGTH,                /* Size of the descriptor, in bytes  */
    USB_DESCRIPTOR_TYPE_AUDIO_CS_INTERFACE,              /* CS_INTERFACE Descriptor Type   */
    USB_DESCRIPTOR_SUBTYPE_AUDIO_CONTROL_INPUT_TERMINAL, /* INPUT_TERMINAL descriptor subtype   */
    USB_AUDIO_OUT_CONTROL_INPUT_TERMINAL_ID,        /* Constant uniquely identifying the Terminal within the audio
                     function. This value is used in all requests        to address this Terminal.   */
    0x01U,
    0x01U, /* USB Streaming.  */
    0x00U, /* This Input Terminal has no association   */
    USB_AUDIO_OUT_CONTROL_CLOCK_SOURCE_ENTITY_ID, /* ID of the Clock Entity to which this Input Terminal is
                                                          connected.  */
    0x10U, /* This Terminal's output audio channel cluster has 16 logical output channels   */
    0x00U,
    0x00U,
    0x00U,
    0x00U, /* Describes the spatial location of the logical channels:: Mono, no spatial location */
    0x00U, /* Index of a string descriptor, describing the name of the first logical channel.  */
    0x00U,
    0x00U, /* bmControls D1..0: Copy Protect Control is not present
              D3..2: Connector Control is not present
              D5..4: Overload Control is not present
              D7..6: Cluster Control is not present
              D9..8: Underflow Control is not present
              D11..10: Overflow Control is not present
              D15..12: Reserved, should set to 0*/
    0x07U, /* Index of a string descriptor, describing the Input Terminal.  */

    /**
     * AudioControl Interface Descriptor:
     * bLength                12
     * bDescriptorType        36
     * bDescriptorSubtype      3 (OUTPUT_TERMINAL)
     * bTerminalID             3
     * wTerminalType      0x0101 USB Streaming
     * bAssocTerminal          0
     * bSourceID               2
     * bCSourceID             16
     * bmControls         0x0000
     * iTerminal               8
     */
    USB_AUDIO_OUTPUT_TERMINAL_DESC_LENGTH,                /* Size of the descriptor, in bytes   */
    USB_DESCRIPTOR_TYPE_AUDIO_CS_INTERFACE,               /* CS_INTERFACE Descriptor Type  */
    USB_DESCRIPTOR_SUBTYPE_AUDIO_CONTROL_OUTPUT_TERMINAL, /* OUTPUT_TERMINAL descriptor subtype   */
    USB_AUDIO_IN_CONTROL_OUTPUT_TERMINAL_ID,        /* Constant uniquely identifying the Terminal within the audio
                     function. This value is used in all requests        to address this Terminal.   */
    0x01U,
    0x01U, /* A Terminal dealing with a signal carried over an endpoint in an AudioStreaming interface. The
         AudioStreaming interface descriptor points to the associated Terminal through the bTerminalLink field.  */
    0x00U, /* This Output Terminal has no association  */
    USB_AUDIO_IN_CONTROL_INPUT_TERMINAL_ID, /* ID of the Unit or Terminal to which this Terminal is connected.  */
    USB_AUDIO_IN_CONTROL_CLOCK_SOURCE_ENTITY_ID, /* ID of the Clock Entity to which this Output Terminal is
                                                          connected  */
    0x00U,
    0x00U, /* bmControls:   D1..0: Copy Protect Control is not present
              D3..2: Connector Control is not present
              D5..4: Overload Control is not present
              D7..6: Underflow Control is not present
              D9..8: Overflow Control is not present
              D15..10: Reserved, should set to 0   */
    0x08U, /* Index of a string descriptor, describing the Output Terminal.  */

    /**
     * AudioControl Interface Descriptor:
     * bLength                12
     * bDescriptorType        36
     * bDescriptorSubtype      3 (OUTPUT_TERMINAL)
     * bTerminalID             6
     * wTerminalType      0x0301 Speaker
     * bAssocTerminal          0
     * bSourceID               4
     * bCSourceID             17
     * bmControls         0x0000
     * iTerminal              10
     */
    USB_AUDIO_OUTPUT_TERMINAL_DESC_LENGTH,                /* Size of the descriptor, in bytes   */
    USB_DESCRIPTOR_TYPE_AUDIO_CS_INTERFACE,               /* CS_INTERFACE Descriptor Type  */
    USB_DESCRIPTOR_SUBTYPE_AUDIO_CONTROL_OUTPUT_TERMINAL, /* OUTPUT_TERMINAL descriptor subtype   */
    USB_AUDIO_OUT_CONTROL_OUTPUT_TERMINAL_ID,        /* Constant uniquely identifying the Terminal within the audio
                     function. This value is used in all requests        to address this Terminal.   */
    0x01U,
    0x03U, /* Speaker */
    0x00U, /* This Output Terminal has no association  */
    USB_AUDIO_OUT_CONTROL_INPUT_TERMINAL_ID, /* ID of the Unit or Terminal to which this Terminal is connected.  */
    USB_AUDIO_OUT_CONTROL_CLOCK_SOURCE_ENTITY_ID, /* ID of the Clock Entity to which this Output Terminal is
                                                          connected  */
    0x00U,
    0x00U, /* bmControls:   D1..0: Copy Protect Control is not present
              D3..2: Connector Control is not present
              D5..4: Overload Control is not present
              D7..6: Underflow Control is not present
              D9..8: Overflow Control is not present
              D15..10: Reserved, should set to 0   */
    0x0AU, /* Index of a string descriptor, describing the Output Terminal.  */

    /**
     * Interface Descriptor:
     * bLength                 9
     * bDescriptorType         4
     * bInterfaceNumber        1
     * bAlternateSetting       0
     * bNumEndpoints           0
     * bInterfaceClass         1 Audio
     * bInterfaceSubClass      2 Streaming
     * bInterfaceProtocol     32
     * iInterface             13
     */
    USB_DESCRIPTOR_LENGTH_INTERFACE,  /* Descriptor size is 9 bytes  */
    USB_DESCRIPTOR_TYPE_INTERFACE,    /* INTERFACE Descriptor Type   */
    USB_AUDIO_STREAM_IN_INTERFACE_INDEX, /* The number of this interface is 1.  */
    USB_AUDIO_STREAM_INTERFACE_ALTERNATE_0, /* The value used to select the alternate setting for this interface is 0   */
    0x00U,                    /* The number of endpoints used by this interface is 0 (excluding endpoint zero)   */
    USB_AUDIO_CLASS,          /* The interface implements the Audio Interface class  */
    USB_SUBCLASS_AUDIOSTREAM, /* The interface implements the AUDIOSTREAMING Subclass  */
    USB_AUDIO_PROTOCOL,       /* The Protocol code is 32   */
    0x0DU,                    /* Index of a string descriptor */

    /**
     * Interface Descriptor:
     * bLength                 9
     * bDescriptorType         4
     * bInterfaceNumber        1
     * bAlternateSetting       1
     * bNumEndpoints           1
     * bInterfaceClass         1 Audio
     * bInterfaceSubClass      2 Streaming
     * bInterfaceProtocol     32
     * iInterface             14
     */
    USB_DESCRIPTOR_LENGTH_INTERFACE,  /* Descriptor size is 9 bytes  */
    USB_DESCRIPTOR_TYPE_INTERFACE,    /* INTERFACE Descriptor Type  */
    USB_AUDIO_STREAM_IN_INTERFACE_INDEX, /*The number of this interface is 1.  */
    USB_AUDIO_STREAM_INTERFACE_ALTERNATE_1, /* The value used to select the alternate setting for this interface is 1  */
    USB_AUDIO_STREAM_IN_ENDPOINT_COUNT,     /* The number of endpoints used by this interface is 1 (excluding endpoint zero)    */
    USB_AUDIO_CLASS,          /* The interface implements the Audio Interface class  */
    USB_SUBCLASS_AUDIOSTREAM, /* The interface implements the AUDIOSTREAMING Subclass  */
    USB_AUDIO_PROTOCOL,       /* The Protocol code is 32   */
    0x0EU,                    /* Index of a string descriptor */

    /**
     * AudioStreaming Interface Descriptor:
     * bLength                16
     * bDescriptorType        36
     * bDescriptorSubtype      1 (AS_GENERAL)
     * bTerminalLink           3
     * bmControls           0x00
     * bFormatType             1
     * bmFormats          0x00000001
     *   PCM
     * bNrChannels            16
     * bmChannelConfig    0x00000000
     * iChannelNames           0
     */
    USB_AUDIO_AS_INTERFACE_DESC_LENGTH,                /* Size of the descriptor, in bytes   */
    USB_DESCRIPTOR_TYPE_AUDIO_CS_INTERFACE,            /* CS_INTERFACE Descriptor Type  */
    USB_DESCRIPTOR_SUBTYPE_AUDIO_STREAMING_AS_GENERAL, /* AS_GENERAL descriptor subtype   */
    USB_AUDIO_IN_CONTROL_OUTPUT_TERMINAL_ID,     /* The Terminal ID of the terminal to which this interface is
                                                          connected   */
    0x00U,                   /* bmControls : D1..0: Active Alternate Setting Control is not present
                                D3..2: Valid Alternate Settings Control is not present
                                D7..4: Reserved, should set to 0   */
    USB_AUDIO_FORMAT_TYPE_I, /* The format type AudioStreaming interfae using is FORMAT_TYPE_I (0x01)   */
    0x01U,
    0x00U,
    0x00U,
    0x00U,                 /* The Audio Data Format that can be Used to communicate with this interface */
    AUDIO_FORMAT_CHANNELS, /* Number of physical channels in the AS Interface audio channel cluster */
    0x00U,
    0x00U,
    0x00U,
    0x00U, /* Describes the spatial location of the logical channels: */
    0x00U, /* Index of a string descriptor, describing the name of the first physical channel   */

    /**
     * AudioStreaming Interface Descriptor:
     * bLength                 6
     * bDescriptorType        36
     * bDescriptorSubtype      2 (FORMAT_TYPE)
     * bFormatType             1 (FORMAT_TYPE_I)
     * bSubslotSize            4
     * bBitResolution         32
     */
    USB_AUDIO_TYPE_I_FORMAT_TYPE_DESC_LENGTH,           /* Size of the descriptor, in bytes   */
    USB_DESCRIPTOR_TYPE_AUDIO_CS_INTERFACE,             /* CS_INTERFACE Descriptor Type   */
    USB_DESCRIPTOR_SUBTYPE_AUDIO_STREAMING_FORMAT_TYPE, /* FORMAT_TYPE descriptor subtype   */
    USB_AUDIO_FORMAT_TYPE_I, /* The format type AudioStreaming interfae using is FORMAT_TYPE_I (0x01)   */
    0x04U, /* The number of bytes occupied by one audio subslot. Can be 1, 2, 3 or 4.  */
    0x20U, /* The number of effectively used bits from the available bits in an audio subslot   */

    /**
     * Endpoint Descriptor:
     * bLength                 7
     * bDescriptorType         5
     * bEndpointAddress     0x82  EP 2 IN
     * bmAttributes            5
     *   Transfer Type            Isochronous
     *   Synch Type               Asynchronous
     *   Usage Type               Data
     * wMaxPacketSize     0x0180  1x 384 bytes
     * bInterval               1
     */
    /* ENDPOINT Descriptor */
    USB_AUDIO_STANDARD_AS_ISO_DATA_ENDPOINT_LENGTH, /* Descriptor size is 7 bytes  */
    USB_DESCRIPTOR_TYPE_ENDPOINT,                   /* ENDPOINT Descriptor Type   */
    USB_AUDIO_STREAM_IN_ENDPOINT | (USB_IN << 7),      /* This is an IN endpoint with endpoint number 2   */
    0x05U,                                          /* Types -
                                                       Transfer: ISOCHRONOUS
                                                       Sync: Async
                                                       Usage: Data EP  */
    USB_SHORT_GET_LOW(FS_ISO_IN_ENDP_PACKET_SIZE),
    USB_SHORT_GET_HIGH(FS_ISO_IN_ENDP_PACKET_SIZE), /* Maximum packet size for this endpoint */
    FS_ISO_IN_ENDP_INTERVAL, /* The polling interval value is every 1 Frames. If Hi-Speed, every 1 uFrames   */

    /**
     * AudioStreaming Endpoint Descriptor:
     * bLength                 8
     * bDescriptorType        37
     * bDescriptorSubtype      1 (EP_GENERAL)
     * bmAttributes         0x00
     * bmControls           0x00
     * bLockDelayUnits         0 Undefined
     * wLockDelay         0x0000
     */
    USB_AUDIO_CLASS_SPECIFIC_ENDPOINT_LENGTH, /*  Size of the descriptor, in bytes  */
    USB_AUDIO_STREAM_ENDPOINT_DESCRIPTOR,     /* CS_ENDPOINT Descriptor Type  */
    USB_AUDIO_EP_GENERAL_DESCRIPTOR_SUBTYPE,  /* AUDIO_EP_GENERAL descriptor subtype  */
    0x00U,
    0x00U,
    0x00U,
    0x00U,
    0x00U,

    /**
     * Interface Descriptor:
     * bLength                 9
     * bDescriptorType         4
     * bInterfaceNumber        2
     * bAlternateSetting       0
     * bNumEndpoints           0
     * bInterfaceClass         1 Audio
     * bInterfaceSubClass      2 Streaming
     * bInterfaceProtocol     32
     * iInterface             11
     */
    USB_DESCRIPTOR_LENGTH_INTERFACE,  /* Descriptor size is 9 bytes  */
    USB_DESCRIPTOR_TYPE_INTERFACE,    /* INTERFACE Descriptor Type   */
    USB_AUDIO_STREAM_OUT_INTERFACE_INDEX, /* The number of this interface is 2.  */
    USB_AUDIO_STREAM_INTERFACE_ALTERNATE_0, /* The value used to select the alternate setting for this interface is 0   */
    0x00U,                    /* The number of endpoints used by this interface is 0 (excluding endpoint zero)   */
    USB_AUDIO_CLASS,          /* The interface implements the Audio Interface class  */
    USB_SUBCLASS_AUDIOSTREAM, /* The interface implements the AUDIOSTREAMING Subclass  */
    USB_AUDIO_PROTOCOL,       /* The Protocol code is 32   */
    0x0BU,                    /* Index of a string descriptor */

    /**
     * Interface Descriptor:
     * bLength                 9
     * bDescriptorType         4
     * bInterfaceNumber        2
     * bAlternateSetting       1
     * bNumEndpoints           2
     * bInterfaceClass         1 Audio
     * bInterfaceSubClass      2 Streaming
     * bInterfaceProtocol     32
     * iInterface             12
     */
    USB_DESCRIPTOR_LENGTH_INTERFACE,  /* Descriptor size is 9 bytes  */
    USB_DESCRIPTOR_TYPE_INTERFACE,    /* INTERFACE Descriptor Type  */
    USB_AUDIO_STREAM_OUT_INTERFACE_INDEX, /*The number of this interface is 2.  */
    USB_AUDIO_STREAM_INTERFACE_ALTERNATE_1, /* The value used to select the alternate setting for this interface is 1  */
    USB_AUDIO_STREAM_OUT_ENDPOINT_COUNT,    /* The number of endpoints used by this interface */
    USB_AUDIO_CLASS,          /* The interface implements the Audio Interface class  */
    USB_SUBCLASS_AUDIOSTREAM, /* The interface implements the AUDIOSTREAMING Subclass  */
    USB_AUDIO_PROTOCOL,       /* The Protocol code is 32   */
    0x0CU,                    /* Index of a string descriptor */

    /**
     * AudioStreaming Interface Descriptor:
     * bLength                16
     * bDescriptorType        36
     * bDescriptorSubtype      1 (AS_GENERAL)
     * bTerminalLink           4
     * bmControls           0x00
     * bFormatType             1
     * bmFormats          0x00000001
     *   PCM
     * bNrChannels            16
     * bmChannelConfig    0x00000000
     * iChannelNames           0
     */
    USB_AUDIO_AS_INTERFACE_DESC_LENGTH,                /* Size of the descriptor, in bytes   */
    USB_DESCRIPTOR_TYPE_AUDIO_CS_INTERFACE,            /* CS_INTERFACE Descriptor Type  */
    USB_DESCRIPTOR_SUBTYPE_AUDIO_STREAMING_AS_GENERAL, /* AS_GENERAL descriptor subtype   */
    USB_AUDIO_OUT_CONTROL_INPUT_TERMINAL_ID,     /* The Terminal ID of the terminal to which this interface is
                                                          connected   */
    0x00U,                   /* bmControls : D1..0: Active Alternate Setting Control is not present
                                D3..2: Valid Alternate Settings Control is not present
                                D7..4: Reserved, should set to 0   */
    USB_AUDIO_FORMAT_TYPE_I, /* The format type AudioStreaming interfae using is FORMAT_TYPE_I (0x01)   */
    0x01U,
    0x00U,
    0x00U,
    0x00U,                 /* The Audio Data Format that can be Used to communicate with this interface */
    AUDIO_FORMAT_CHANNELS, /* Number of physical channels in the AS Interface audio channel cluster */
    0x00U,
    0x00U,
    0x00U,
    0x00U, /* Describes the spatial location of the logical channels: */
    0x00U, /* Index of a string descriptor, describing the name of the first physical channel   */

    /**
     * AudioStreaming Interface Descriptor:
     * bLength                 6
     * bDescriptorType        36
     * bDescriptorSubtype      2 (FORMAT_TYPE)
     * bFormatType             1 (FORMAT_TYPE_I)
     * bSubslotSize            4
     * bBitResolution         32
     */
    USB_AUDIO_TYPE_I_FORMAT_TYPE_DESC_LENGTH,           /* Size of the descriptor, in bytes   */
    USB_DESCRIPTOR_TYPE_AUDIO_CS_INTERFACE,             /* CS_INTERFACE Descriptor Type   */
    USB_DESCRIPTOR_SUBTYPE_AUDIO_STREAMING_FORMAT_TYPE, /* FORMAT_TYPE descriptor subtype   */
    USB_AUDIO_FORMAT_TYPE_I, /* The format type AudioStreaming interfae using is FORMAT_TYPE_I (0x01)   */
    0x04U, /* The number of bytes occupied by one audio subslot. Can be 1, 2, 3 or 4.  */
    0x20U, /* The number of effectively used bits from the available bits in an audio subslot   */

    /**
     * Endpoint Descriptor:
     * bLength                 7
     * bDescriptorType         5
     * bEndpointAddress     0x01  EP 1 OUT
     * bmAttributes            5
     *   Transfer Type            Isochronous
     *   Synch Type               Asynchronous
     *   Usage Type               Data
     * wMaxPacketSize     0x0180  1x 384 bytes
     * bInterval               1
     */
    /* ENDPOINT Descriptor */
    USB_AUDIO_STANDARD_AS_ISO_DATA_ENDPOINT_LENGTH, /* Descriptor size is 7 bytes  */
    USB_DESCRIPTOR_TYPE_ENDPOINT,                   /* ENDPOINT Descriptor Type   */
    USB_AUDIO_STREAM_OUT_ENDPOINT | (USB_OUT << 7),      /* This is an IN endpoint with endpoint number 2   */
    0x05U,                                          /* Types -
                                                       Transfer: ISOCHRONOUS
                                                       Sync: Async
                                                       Usage: Data EP  */
    USB_SHORT_GET_LOW(FS_ISO_OUT_ENDP_PACKET_SIZE),
    USB_SHORT_GET_HIGH(FS_ISO_OUT_ENDP_PACKET_SIZE), /* Maximum packet size for this endpoint */
    FS_ISO_OUT_ENDP_INTERVAL, /* The polling interval value is every 1 Frames. If Hi-Speed, every 1 uFrames   */

    /**
     * Endpoint Descriptor:
     * bLength                 7
     * bDescriptorType         5
     * bEndpointAddress     0x81  EP 1 IN
     * bmAttributes           15
     *   Transfer Type            Isochronous
     *   Synch Type               Asynchronous
     *   Usage Type               Feedback
     * wMaxPacketSize     0x0004  1x 4 bytes
     * bInterval               4
     */
    /* ENDPOINT Descriptor */
    USB_AUDIO_STANDARD_AS_ISO_DATA_ENDPOINT_LENGTH, /* Descriptor size is 7 bytes  */
    USB_DESCRIPTOR_TYPE_ENDPOINT,                   /* ENDPOINT Descriptor Type   */
    USB_AUDIO_STREAM_OUT_ENDPOINT | (USB_IN << 7),      /* This is an IN endpoint with endpoint number 2   */
    0x15U,                                          /* Types -
                                                       Transfer: ISOCHRONOUS
                                                       Sync: Async
                                                       Usage: Data EP  */
    USB_SHORT_GET_LOW(FS_ISO_OUT_FEEDBACK_ENDP_PACKET_SIZE),
    USB_SHORT_GET_HIGH(FS_ISO_OUT_FEEDBACK_ENDP_PACKET_SIZE), /* Maximum packet size for this endpoint */
    FS_ISO_OUT_FEEDBACK_ENDP_INTERVAL, /* The polling interval value */

    /**
     * AudioStreaming Endpoint Descriptor:
     * bLength                 8
     * bDescriptorType        37
     * bDescriptorSubtype      1 (EP_GENERAL)
     * bmAttributes         0x00
     * bmControls           0x00
     * bLockDelayUnits         0 Undefined
     * wLockDelay         0x0000
     */
    USB_AUDIO_CLASS_SPECIFIC_ENDPOINT_LENGTH, /*  Size of the descriptor, in bytes  */
    USB_AUDIO_STREAM_ENDPOINT_DESCRIPTOR,     /* CS_ENDPOINT Descriptor Type  */
    USB_AUDIO_EP_GENERAL_DESCRIPTOR_SUBTYPE,  /* AUDIO_EP_GENERAL descriptor subtype  */
    0x00U,
    0x00U,
    0x00U,
    0x00U,
    0x00U,
};

/* Define string descriptor */
USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceStringNull0[] = {
    2U + 2U,
    USB_DESCRIPTOR_TYPE_STRING,
    0x09U,
    0x04U,
};

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceStringManufacturer1[] = {
    USB_STRING_MANUFACTURER
};

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceStringProduct2[] = {
    USB_STRING_PRODUCT
};

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceStringSerial3[] = {
    USB_STRING_SERIAL
};

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceStringSourceSink4[] = {
    USB_STRING_SOURCE_SINK
};

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceStringTopologyControl5[] = {
    USB_STRING_TOPOLOGY_CONTROL
};

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceString48000Hz6[] = {
    USB_STRING_48000HZ
};

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceStringUsbhOut7[] = {
    USB_STRING_USBH_OUT
};

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceStringUsbhIn8[] = {
    USB_STRING_USBH_IN
};

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceStringUsbdOut9[] = {
    USB_STRING_USBD_OUT
};

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceStringUsbdIn10[] = {
    USB_STRING_USBD_IN
};

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceStringPlaybackInactive11[] = {
    USB_STRING_PLAYBACK_INACTIVE
};

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceStringPlaybackActive12[] = {
    USB_STRING_PLAYBACK_ACTIVE
};

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceStringCaptureInactive13[] = {
    USB_STRING_CAPTURE_INACTIVE
};

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceStringCaptureActive14[] = {
    USB_STRING_CAPTURE_ACTIVE
};

uint32_t g_UsbDeviceStringDescriptorLength[USB_DEVICE_STRING_COUNT] = {
    sizeof(g_UsbDeviceStringNull0),
    sizeof(g_UsbDeviceStringManufacturer1),
    sizeof(g_UsbDeviceStringProduct2),
    sizeof(g_UsbDeviceStringSerial3),
    sizeof(g_UsbDeviceStringSourceSink4),
    sizeof(g_UsbDeviceStringTopologyControl5),
    sizeof(g_UsbDeviceString48000Hz6),
    sizeof(g_UsbDeviceStringUsbhOut7),
    sizeof(g_UsbDeviceStringUsbhIn8),
    sizeof(g_UsbDeviceStringUsbdOut9),
    sizeof(g_UsbDeviceStringUsbdIn10),
    sizeof(g_UsbDeviceStringPlaybackInactive11),
    sizeof(g_UsbDeviceStringPlaybackActive12),
    sizeof(g_UsbDeviceStringCaptureInactive13),
    sizeof(g_UsbDeviceStringCaptureActive14),
};

uint8_t *g_UsbDeviceStringDescriptorArray[USB_DEVICE_STRING_COUNT] = {
    g_UsbDeviceStringNull0,
    g_UsbDeviceStringManufacturer1,
    g_UsbDeviceStringProduct2,
    g_UsbDeviceStringSerial3,
    g_UsbDeviceStringSourceSink4,
    g_UsbDeviceStringTopologyControl5,
    g_UsbDeviceString48000Hz6,
    g_UsbDeviceStringUsbhOut7,
    g_UsbDeviceStringUsbhIn8,
    g_UsbDeviceStringUsbdOut9,
    g_UsbDeviceStringUsbdIn10,
    g_UsbDeviceStringPlaybackInactive11,
    g_UsbDeviceStringPlaybackActive12,
    g_UsbDeviceStringCaptureInactive13,
    g_UsbDeviceStringCaptureActive14,
};

usb_language_t g_UsbDeviceLanguage[USB_DEVICE_LANGUAGE_COUNT] = {{
    g_UsbDeviceStringDescriptorArray,
    g_UsbDeviceStringDescriptorLength,
    (uint16_t)0x0409U,
}};

usb_language_list_t g_UsbDeviceLanguageList = {
    g_UsbDeviceStringNull0,
    sizeof(g_UsbDeviceStringNull0),
    g_UsbDeviceLanguage,
    USB_DEVICE_LANGUAGE_COUNT,
};

/*******************************************************************************
 * Code
 ******************************************************************************/
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
                                           usb_device_get_device_descriptor_struct_t *deviceDescriptor)
{
    deviceDescriptor->buffer = g_UsbDeviceDescriptor;
    deviceDescriptor->length = USB_DESCRIPTOR_LENGTH_DEVICE;
    return kStatus_USB_Success;
}

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
    usb_device_handle handle, usb_device_get_configuration_descriptor_struct_t *configurationDescriptor)
{
    if (USB_AUDIO_CONFIGURE_INDEX > configurationDescriptor->configuration)
    {
        configurationDescriptor->buffer = g_UsbDeviceConfigurationDescriptor;
        configurationDescriptor->length = USB_DESCRIPTOR_LENGTH_CONFIGURATION_ALL;
        return kStatus_USB_Success;
    }
    return kStatus_USB_InvalidRequest;
}

/*!
 * @brief USB device get string descriptor function.
 *
 * This function gets the string descriptor of the USB device.
 *
 * @param handle The USB device handle.
 * @param stringDescriptor The pointer to the string descriptor structure.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceGetStringDescriptor(usb_device_handle handle,
                                           usb_device_get_string_descriptor_struct_t *stringDescriptor)
{
    if (stringDescriptor->stringIndex == 0U)
    {
        stringDescriptor->buffer = (uint8_t *)g_UsbDeviceLanguageList.languageString;
        stringDescriptor->length = g_UsbDeviceLanguageList.stringLength;
    }
    else
    {
        uint8_t languageId    = 0U;
        uint8_t languageIndex = USB_DEVICE_STRING_COUNT;

        for (; languageId < USB_DEVICE_LANGUAGE_COUNT; languageId++)
        {
            if (stringDescriptor->languageId == g_UsbDeviceLanguageList.languageList[languageId].languageId)
            {
                if (stringDescriptor->stringIndex < USB_DEVICE_STRING_COUNT)
                {
                    languageIndex = stringDescriptor->stringIndex;
                }
                break;
            }
        }

        if (USB_DEVICE_STRING_COUNT == languageIndex)
        {
            return kStatus_USB_InvalidRequest;
        }
        stringDescriptor->buffer = (uint8_t *)g_UsbDeviceLanguageList.languageList[languageId].string[languageIndex];
        stringDescriptor->length = g_UsbDeviceLanguageList.languageList[languageId].length[languageIndex];
    }
    return kStatus_USB_Success;
}

/** Due to the difference of HS and FS descriptors, the device descriptors and configurations need to be updated to match
 * current speed.
 * As the default, the device descriptors and configurations are configured by using FS parameters for both EHCI and
 * KHCI.
 * When the EHCI is enabled, the application needs to call this function to update device by using current speed.
 * The updated information includes endpoint max packet size, endpoint interval, etc. */
usb_status_t USB_DeviceSetSpeed(usb_device_handle handle, uint8_t speed)
{
    usb_descriptor_union_t *descriptorHead;
    usb_descriptor_union_t *descriptorTail;

    descriptorHead = (usb_descriptor_union_t *)&g_UsbDeviceConfigurationDescriptor[0];
    descriptorTail =
        (usb_descriptor_union_t *)(&g_UsbDeviceConfigurationDescriptor[USB_DESCRIPTOR_LENGTH_CONFIGURATION_ALL - 1U]);

    while (descriptorHead < descriptorTail)
    {
        if (descriptorHead->common.bDescriptorType == USB_DESCRIPTOR_TYPE_ENDPOINT)
        {
            if (USB_SPEED_HIGH == speed)
            {
                if ((USB_AUDIO_STREAM_IN_ENDPOINT == (descriptorHead->endpoint.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK)) &&
                    ((descriptorHead->endpoint.bEndpointAddress >> USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_SHIFT) == USB_IN))
                {
                    descriptorHead->endpoint.bInterval = HS_ISO_IN_ENDP_INTERVAL;
                    USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(HS_ISO_IN_ENDP_PACKET_SIZE, descriptorHead->endpoint.wMaxPacketSize);
                }

                if ((USB_AUDIO_STREAM_OUT_ENDPOINT == (descriptorHead->endpoint.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK)) &&
                    ((descriptorHead->endpoint.bEndpointAddress >> USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_SHIFT) == USB_OUT))
                {
                    descriptorHead->endpoint.bInterval = HS_ISO_OUT_ENDP_INTERVAL;
                    USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(HS_ISO_OUT_ENDP_PACKET_SIZE, descriptorHead->endpoint.wMaxPacketSize);
                }

                if ((USB_AUDIO_STREAM_OUT_FEEDBACK_ENDPOINT == (descriptorHead->endpoint.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK)) &&
                    ((descriptorHead->endpoint.bEndpointAddress >> USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_SHIFT) == USB_IN))
                {
                    descriptorHead->endpoint.bInterval = HS_ISO_OUT_FEEDBACK_ENDP_INTERVAL;
                    USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(HS_ISO_OUT_FEEDBACK_ENDP_PACKET_SIZE, descriptorHead->endpoint.wMaxPacketSize);
                }

            }
            else
            {
                if ((USB_AUDIO_STREAM_IN_ENDPOINT == (descriptorHead->endpoint.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK)) &&
                    ((descriptorHead->endpoint.bEndpointAddress >> USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_SHIFT) == USB_IN))
                {
                    descriptorHead->endpoint.bInterval = FS_ISO_IN_ENDP_INTERVAL;
                    USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(FS_ISO_IN_ENDP_PACKET_SIZE, descriptorHead->endpoint.wMaxPacketSize);
                }

                if ((USB_AUDIO_STREAM_OUT_ENDPOINT == (descriptorHead->endpoint.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK)) &&
                    ((descriptorHead->endpoint.bEndpointAddress >> USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_SHIFT) == USB_OUT))
                {
                    descriptorHead->endpoint.bInterval = FS_ISO_OUT_ENDP_INTERVAL;
                    USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(FS_ISO_OUT_ENDP_PACKET_SIZE, descriptorHead->endpoint.wMaxPacketSize);
                }

                if ((USB_AUDIO_STREAM_OUT_FEEDBACK_ENDPOINT == (descriptorHead->endpoint.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK)) &&
                    ((descriptorHead->endpoint.bEndpointAddress >> USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_SHIFT) == USB_IN))
                {
                    descriptorHead->endpoint.bInterval = FS_ISO_OUT_FEEDBACK_ENDP_INTERVAL;
                    USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(FS_ISO_OUT_FEEDBACK_ENDP_PACKET_SIZE, descriptorHead->endpoint.wMaxPacketSize);
                }

            }
        }
        descriptorHead = (usb_descriptor_union_t *)((uint8_t *)descriptorHead + descriptorHead->common.bLength);
    }

    if (USB_SPEED_HIGH == speed)
    {
        g_UsbDeviceAudioGeneratorInEndpoints[0].maxPacketSize = HS_ISO_IN_ENDP_PACKET_SIZE;
        g_UsbDeviceAudioGeneratorInEndpoints[0].interval      = HS_ISO_IN_ENDP_INTERVAL;

        g_UsbDeviceAudioGeneratorOutEndpoints[0].maxPacketSize = HS_ISO_OUT_ENDP_PACKET_SIZE;
        g_UsbDeviceAudioGeneratorOutEndpoints[0].interval      = HS_ISO_OUT_ENDP_INTERVAL;

        g_UsbDeviceAudioGeneratorOutEndpoints[1].maxPacketSize = HS_ISO_OUT_FEEDBACK_ENDP_PACKET_SIZE;
        g_UsbDeviceAudioGeneratorOutEndpoints[1].interval      = HS_ISO_OUT_FEEDBACK_ENDP_INTERVAL;
    }
    else
    {
        g_UsbDeviceAudioGeneratorInEndpoints[0].maxPacketSize = FS_ISO_IN_ENDP_PACKET_SIZE;
        g_UsbDeviceAudioGeneratorInEndpoints[0].interval      = FS_ISO_IN_ENDP_INTERVAL;

        g_UsbDeviceAudioGeneratorOutEndpoints[0].maxPacketSize = FS_ISO_OUT_ENDP_PACKET_SIZE;
        g_UsbDeviceAudioGeneratorOutEndpoints[0].interval      = FS_ISO_OUT_ENDP_INTERVAL;

        g_UsbDeviceAudioGeneratorOutEndpoints[1].maxPacketSize = FS_ISO_OUT_FEEDBACK_ENDP_PACKET_SIZE;
        g_UsbDeviceAudioGeneratorOutEndpoints[1].interval      = FS_ISO_OUT_FEEDBACK_ENDP_INTERVAL;
    }

    return kStatus_USB_Success;
}
