// clang-format off
/*=============================================================================
 *
 * Project: a2bstack
 *
 * Copyright (c) 2015 -2023 Analog Devices Inc. All Rights Reserved.
 * See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
 *
 *=============================================================================
 */
// clang-format on

/**
 * @file libusb_link.h
 * @brief The ADI USBi (USB ID 0456:7031) opened through libusb-1.0, as a UsbiLink.
 *
 * Ported from ADI's Windows PAL, pal_i2c_usbi.c.
 * C++ form by ClockWorks Signal Processing LLC.
 */

#ifndef A2BCPP_PLATFORMS_USBI_LIBUSB_LINK_H_
#define A2BCPP_PLATFORMS_USBI_LIBUSB_LINK_H_

#include <cstdint>

#include "a2b/types.h"
#include "usbi_link.h"

struct libusb_context;
struct libusb_device_handle;

namespace a2b {

/// A USBi through libusb-1.0. On Windows the USBi needs the WinUSB driver (Zadig).
class LibusbLink : public UsbiLink {
 public:
  static constexpr uint16_t kVendorId = 0x0456;   ///< Analog Devices.
  static constexpr uint16_t kProductId = 0x7031;  ///< USBi (programmed).

  LibusbLink() = default;  ///< Not open.
  ~LibusbLink() override;

  LibusbLink(const LibusbLink&) = delete;
  LibusbLink& operator=(const LibusbLink&) = delete;

  Result Open(int which = 0);  ///< Opens the USBi, the first one found by default.
  void Close();                ///< Closes it.
  bool IsOpen() const { return m_Handle != nullptr; }  ///< Open() succeeded.
  const char* Error() const { return m_Error; }        ///< Why Open() failed.

  int ControlTransfer(uint8_t requestType, uint8_t request, uint16_t value, uint16_t index,
                      uint8_t* data, uint16_t length, unsigned timeoutMs) override;
  void SleepMs(unsigned ms) override;

 private:
  libusb_context* m_Context = nullptr;       ///< libusb, while open.
  libusb_device_handle* m_Handle = nullptr;  ///< The USBi, while open.
  char m_Error[256] = {};                    ///< The last Open() failure.
};

}  // namespace a2b

#endif  // A2BCPP_PLATFORMS_USBI_LIBUSB_LINK_H_
