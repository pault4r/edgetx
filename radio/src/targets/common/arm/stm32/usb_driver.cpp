/*
 * Copyright (C) EdgeTX
 *
 * Based on code named
 *   opentx - https://github.com/opentx/opentx
 *   th9x - http://code.google.com/p/th9x
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - http://code.google.com/p/gruvin9x
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "usb_driver.h"

#if defined(USBJ_EX)
#include "usb_joystick.h"
#endif

extern "C" {
#include "usb_conf.h"
#include "usb_dcd_int.h"
#include "usb_bsp.h"
#include "usbd_core.h"
#include "usbd_msc_core.h"
#include "usbd_desc.h"
#include "usbd_usr.h"
#include "usbd_hid_core.h"
#include "usbd_cdc_core.h"
}

#include "board.h"
#include "debug.h"

static bool usbDriverStarted = false;
#if defined(BOOT)
static usbMode selectedUsbMode = USB_MASS_STORAGE_MODE;
#else
static usbMode selectedUsbMode = USB_UNSELECTED_MODE;
#endif

int getSelectedUsbMode()
{
  return selectedUsbMode;
}

void setSelectedUsbMode(int mode)
{
  selectedUsbMode = usbMode(mode);
}

#if defined(USB_GPIO_PIN_VBUS)
int usbPlugged()
{
  static uint8_t debouncedState = 0;
  static uint8_t lastState = 0;

  uint8_t state = GPIO_ReadInputDataBit(USB_GPIO, USB_GPIO_PIN_VBUS);

  if (state == lastState)
    debouncedState = state;
  else
    lastState = state;
  
  return debouncedState;
}
#endif

USB_OTG_CORE_HANDLE USB_OTG_dev;

extern "C" void OTG_FS_IRQHandler()
{
  DEBUG_INTERRUPT(INT_OTG_FS);
  USBD_OTG_ISR_Handler(&USB_OTG_dev);
}

void usbInit()
{
  // Initialize hardware
  USB_OTG_BSP_Init(&USB_OTG_dev);
  usbDriverStarted = false;
}

extern void usbInitLUNs();

void usbStart()
{
  switch (getSelectedUsbMode()) {
#if !defined(BOOT)
    case USB_JOYSTICK_MODE:
      // initialize USB as HID device
#if defined(USBJ_EX)
      setupUSBJoystick();
#endif
      USBD_Init(&USB_OTG_dev, USB_OTG_FS_CORE_ID, &USR_desc, &USBD_HID_cb, &USR_cb);
      break;
#endif
#if defined(USB_SERIAL)
    case USB_SERIAL_MODE:
      // initialize USB as CDC device (virtual serial port)
      USBD_Init(&USB_OTG_dev, USB_OTG_FS_CORE_ID, &USR_desc, &USBD_CDC_cb, &USR_cb);
      break;
#endif
    default:
    case USB_MASS_STORAGE_MODE:
      // initialize USB as MSC device
      usbInitLUNs();
      USBD_Init(&USB_OTG_dev, USB_OTG_FS_CORE_ID, &USR_desc, &USBD_MSC_cb, &USR_cb);
      break;
  }
  usbDriverStarted = true;
}

void usbStop()
{
  usbDriverStarted = false;
  USBD_DeInit(&USB_OTG_dev);
}

#if defined(USBJ_EX)
void usbJoystickRestart()
{
  if (getSelectedUsbMode() != USB_JOYSTICK_MODE) return;

  USBD_DeInit(&USB_OTG_dev);
  DCD_DevDisconnect(&USB_OTG_dev);
  DCD_DevConnect(&USB_OTG_dev);
  USBD_Init(&USB_OTG_dev, USB_OTG_FS_CORE_ID, &USR_desc, &USBD_HID_cb, &USR_cb);
}
#endif

bool usbStarted()
{
  return usbDriverStarted;
}

#if !defined(BOOT)
#include "globals.h"

/*
  Prepare and send new USB data packet

  The format of HID_Buffer is defined by
  USB endpoint description can be found in
  file usb_hid_joystick.c, variable HID_JOYSTICK_ReportDesc
*/
void usbJoystickUpdate()
{
#if !defined(USBJ_EX)
  static uint8_t HID_Buffer[HID_IN_PACKET];

  // test to se if TX buffer is free
  if (USBD_HID_SendReport(&USB_OTG_dev, 0, 0) == USBD_OK) {
    //buttons
    HID_Buffer[0] = 0;
    HID_Buffer[1] = 0;
    HID_Buffer[2] = 0;
    for (int i = 0; i < 8; ++i) {
      if ( channelOutputs[i+8] > 0 ) {
        HID_Buffer[0] |= (1 << i);
      }
      if ( channelOutputs[i+16] > 0 ) {
        HID_Buffer[1] |= (1 << i);
      }
      if ( channelOutputs[i+24] > 0 ) {
        HID_Buffer[2] |= (1 << i);
      }
    }

    //analog values
    //uint8_t * p = HID_Buffer + 1;
    for (int i = 0; i < 8; ++i) {

      int16_t value = channelOutputs[i] + 1024;
      if ( value > 2047 ) value = 2047;
      else if ( value < 0 ) value = 0;
      HID_Buffer[i*2 +3] = static_cast<uint8_t>(value & 0xFF);
      HID_Buffer[i*2 +4] = static_cast<uint8_t>((value >> 8) & 0x07);

    }
    USBD_HID_SendReport(&USB_OTG_dev, HID_Buffer, HID_IN_PACKET);
  }
#else
  // test to se if TX buffer is free
  if (USBD_HID_SendReport(&USB_OTG_dev, 0, 0) == USBD_OK) {
    usbReport_t ret = usbReport();
    USBD_HID_SendReport(&USB_OTG_dev, ret.ptr, ret.size);
  }
#endif
}
#endif
