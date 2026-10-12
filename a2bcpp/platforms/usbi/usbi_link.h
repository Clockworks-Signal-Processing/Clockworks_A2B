/**
 * @file usbi_link.h
 * @brief The USB side of the ADI USBi: control transfers and a sleep, kept apart from
 *        libusb so the USBi framing (UsbiPlatform) can be tested without the adapter.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_PLATFORMS_USBI_USBI_LINK_H_
#define A2BCPP_PLATFORMS_USBI_USBI_LINK_H_

#include <cstdint>

namespace a2b {

/**
 * @brief A USB device reached by control transfers on endpoint 0, and a way to wait.
 *
 * LibusbLink is the real one (libusb-1.0); the unit tests use a fake that records each
 * call. UsbiPlatform builds the USBi's I2C transfers from these calls.
 */
class UsbiLink {
 public:
  virtual ~UsbiLink() = default;

  /**
   * @brief Runs one control transfer, as libusb_control_transfer() does.
   * @param requestType bmRequestType: direction, type and recipient (e.g. 0x40, vendor
   *        request to the device; 0xC0, the same from the device).
   * @param request bRequest: the USBi command.
   * @param value wValue.
   * @param index wIndex.
   * @param data The data stage: sent for an OUT transfer, filled for an IN transfer.
   * @param length wLength: bytes in the data stage.
   * @param timeoutMs How long to wait for the transfer.
   * @return Bytes transferred in the data stage, or a negative error code.
   */
  virtual int ControlTransfer(uint8_t requestType, uint8_t request, uint16_t value, uint16_t index,
                              uint8_t* data, uint16_t length, unsigned timeoutMs) = 0;

  /**
   * @brief Waits at least @p ms milliseconds.
   * @param ms The wait.
   */
  virtual void SleepMs(unsigned ms) = 0;
};

}  // namespace a2b

#endif  // A2BCPP_PLATFORMS_USBI_USBI_LINK_H_
