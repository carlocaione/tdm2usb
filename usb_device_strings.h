/*
 * Copyright (c) 2023, Carlo Caione <ccaione@baylibre.com>
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __USB_STRINGS_H__
#define __USB_STRINGS_H__ 1

#define USB_STRING_MANUFACTURER               \
    2U + 2U * 3U, USB_DESCRIPTOR_TYPE_STRING, \
        'F', 0x00U,                           \
        'R', 0x00U,                           \
        'L', 0x00U,

#define USB_STRING_PRODUCT                    \
    2U + 2U * 7U, USB_DESCRIPTOR_TYPE_STRING, \
        'T', 0x00U,                           \
        'D', 0x00U,                           \
        'M', 0x00U,                           \
        '2', 0x00U,                           \
        'U', 0x00U,                           \
        'S', 0x00U,                           \
        'B', 0x00u,

#define USB_STRING_SERIAL                     \
    2U + 2U * 8U, USB_DESCRIPTOR_TYPE_STRING, \
        'D', 0x00U,                           \
        'E', 0x00U,                           \
        'A', 0x00U,                           \
        'D', 0x00U,                           \
        'B', 0x00U,                           \
        'E', 0x00U,                           \
        'E', 0x00U,                           \
        'F', 0x00U,

#define USB_STRING_SOURCE_SINK                 \
    2U + 2U * 11U, USB_DESCRIPTOR_TYPE_STRING, \
        'S', 0x00U,                            \
        'o', 0x00U,                            \
        'u', 0x00U,                            \
        'r', 0x00U,                            \
        'c', 0x00U,                            \
        'e', 0x00U,                            \
        '/', 0x00U,                            \
        'S', 0x00U,                            \
        'i', 0x00U,                            \
        'n', 0x00U,                            \
        'k', 0x00U,

#define USB_STRING_TOPOLOGY_CONTROL            \
    2U + 2U * 16U, USB_DESCRIPTOR_TYPE_STRING, \
        'T', 0x00U,                            \
        'o', 0x00U,                            \
        'p', 0x00U,                            \
        'o', 0x00U,                            \
        'l', 0x00U,                            \
        'o', 0x00U,                            \
        'g', 0x00U,                            \
        'y', 0x00U,                            \
        ' ', 0x00U,                            \
        'C', 0x00U,                            \
        'o', 0x00U,                            \
        'n', 0x00U,                            \
        't', 0x00U,                            \
        'r', 0x00U,                            \
        'o', 0x00U,                            \
        'l', 0x00U,

#define USB_STRING_48000HZ                    \
    2U + 2U * 7U, USB_DESCRIPTOR_TYPE_STRING, \
        '4', 0x00U,                           \
        '8', 0x00U,                           \
        '0', 0x00U,                           \
        '0', 0x00U,                           \
        '0', 0x00U,                           \
        'H', 0x00U,                           \
        'z', 0x00U,

#define USB_STRING_USBH_OUT                   \
    2U + 2U * 8U, USB_DESCRIPTOR_TYPE_STRING, \
        'U', 0x00U,                           \
        'S', 0x00U,                           \
        'B', 0x00U,                           \
        'H', 0x00U,                           \
        ' ', 0x00U,                           \
        'O', 0x00U,                           \
        'u', 0x00U,                           \
        't', 0x00U,

#define USB_STRING_USBD_OUT                   \
    2U + 2U * 8U, USB_DESCRIPTOR_TYPE_STRING, \
        'U', 0x00U,                           \
        'S', 0x00U,                           \
        'B', 0x00U,                           \
        'D', 0x00U,                           \
        ' ', 0x00U,                           \
        'O', 0x00U,                           \
        'u', 0x00U,                           \
        't', 0x00U,

#define USB_STRING_USBH_IN                    \
    2U + 2U * 7U, USB_DESCRIPTOR_TYPE_STRING, \
        'U', 0x00U,                           \
        'S', 0x00U,                           \
        'B', 0x00U,                           \
        'H', 0x00U,                           \
        ' ', 0x00U,                           \
        'I', 0x00U,                           \
        'n', 0x00U,

#define USB_STRING_USBD_IN                    \
    2U + 2U * 7U, USB_DESCRIPTOR_TYPE_STRING, \
        'U', 0x00U,                           \
        'S', 0x00U,                           \
        'B', 0x00U,                           \
        'D', 0x00U,                           \
        ' ', 0x00U,                           \
        'I', 0x00U,                           \
        'n', 0x00U,

#define USB_STRING_PLAYBACK_INACTIVE           \
    2U + 2U * 17U, USB_DESCRIPTOR_TYPE_STRING, \
        'P', 0x00U,                            \
        'l', 0x00U,                            \
        'a', 0x00U,                            \
        'y', 0x00U,                            \
        'b', 0x00U,                            \
        'a', 0x00U,                            \
        'c', 0x00U,                            \
        'k', 0x00U,                            \
        ' ', 0x00U,                            \
        'I', 0x00U,                            \
        'n', 0x00U,                            \
        'a', 0x00U,                            \
        'c', 0x00U,                            \
        't', 0x00U,                            \
        'i', 0x00U,                            \
        'v', 0x00U,                            \
        'e', 0x00U,

#define USB_STRING_PLAYBACK_ACTIVE             \
    2U + 2U * 15U, USB_DESCRIPTOR_TYPE_STRING, \
        'P', 0x00U,                            \
        'l', 0x00U,                            \
        'a', 0x00U,                            \
        'y', 0x00U,                            \
        'b', 0x00U,                            \
        'a', 0x00U,                            \
        'c', 0x00U,                            \
        'k', 0x00U,                            \
        ' ', 0x00U,                            \
        'A', 0x00U,                            \
        'c', 0x00U,                            \
        't', 0x00U,                            \
        'i', 0x00U,                            \
        'v', 0x00U,                            \
        'e', 0x00U,

#define USB_STRING_CAPTURE_INACTIVE            \
    2U + 2U * 16U, USB_DESCRIPTOR_TYPE_STRING, \
        'C', 0x00U,                            \
        'a', 0x00U,                            \
        'p', 0x00U,                            \
        't', 0x00U,                            \
        'u', 0x00U,                            \
        'r', 0x00U,                            \
        'e', 0x00U,                            \
        ' ', 0x00U,                            \
        'I', 0x00U,                            \
        'n', 0x00U,                            \
        'a', 0x00U,                            \
        'c', 0x00U,                            \
        't', 0x00U,                            \
        'i', 0x00U,                            \
        'v', 0x00U,                            \
        'e', 0x00U,

#define USB_STRING_CAPTURE_ACTIVE              \
    2U + 2U * 14U, USB_DESCRIPTOR_TYPE_STRING, \
        'C', 0x00U,                            \
        'a', 0x00U,                            \
        'p', 0x00U,                            \
        't', 0x00U,                            \
        'u', 0x00U,                            \
        'r', 0x00U,                            \
        'e', 0x00U,                            \
        ' ', 0x00U,                            \
        'A', 0x00U,                            \
        'c', 0x00U,                            \
        't', 0x00U,                            \
        'i', 0x00U,                            \
        'v', 0x00U,                            \
        'e', 0x00U,

#endif /* __USB_STRINGS_H__ */