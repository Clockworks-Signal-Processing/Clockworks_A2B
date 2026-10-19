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
 * @file libusb_link.cpp
 * @brief LibusbLink: finds and opens the ADI USBi through libusb-1.0.
 *
 * Ported from pal_i2cInit(), pal_i2cOpen(), pal_i2cClose() and pal_i2cShutdown() of
 * ADI's Windows PAL (pal_i2c_usbi.c): libusb at warning
 * verbosity, the USBi found by its USB ID, opened without claiming an interface (
 * ADI's PAL's claim is commented out; the USBi's requests go to the device), and the same
 * hints when it can't be found or opened.
 *
 * On Windows the timer resolution is raised to 1 ms while the USBi is open
 * (timeBeginPeriod(1), as ADI's Windows app does): every USBi transfer
 * sleeps 2 ms before its status check, which Windows' default 15.6 ms tick would stretch
 * to 16 ms.
 *
 * libusb is cross-platform, so this builds on Linux too. A USBi on a Linux host is
 * not supported (the roadmap): it may need the USBi's firmware loaded and a udev rule.
 *
 * C++ form by ClockWorks Signal Processing LLC.
 */

#include "libusb_link.h"

#include <libusb.h>

#include <cstdio>

#ifdef _WIN32
#include <windows.h>  // Sleep()
// windows.h first: mmsystem.h needs its types.
#include <mmsystem.h>  // timeBeginPeriod(), in winmm
#else
#include <time.h>  // nanosleep()
#endif

namespace a2b {

// The USBi framing (usbi_platform.cpp) spells the request types as numbers, so that it
// builds without libusb; they are libusb's vendor requests to the device.
static_assert((LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE) == 0x40,
              "host-to-device vendor request");
static_assert((LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE) == 0xC0,
              "device-to-host vendor request");

/** Closes the USBi if it is open. */
LibusbLink::~LibusbLink() { Close(); }

/**
 * Starts libusb and opens a USBi.
 * @param which Which USBi, counting from 0 in libusb's device order, when several are
 *        plugged in.
 * @return Result::kOk; Result::kNotSupported if libusb can't start, or Result::kI2cError
 *         if no USBi was found or it can't be opened (Error() says which, with a hint).
 */
Result LibusbLink::Open(int which) {
  Close();
  m_Error[0] = '\0';
  if (libusb_init(&m_Context) != 0) {
    m_Context = nullptr;
    std::snprintf(m_Error, sizeof(m_Error), "libusb could not be started");
    return Result::kNotSupported;
  }
  libusb_set_option(m_Context, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_WARNING);

  libusb_device** devices = nullptr;
  const ssize_t count = libusb_get_device_list(m_Context, &devices);
  int found = 0;
  int openError = 0;
  for (ssize_t i = 0; i < count && m_Handle == nullptr; ++i) {
    libusb_device_descriptor desc = {};
    if (libusb_get_device_descriptor(devices[i], &desc) != 0 || desc.idVendor != kVendorId ||
        desc.idProduct != kProductId) {
      continue;
    }
    if (found++ == which) {
      openError = libusb_open(devices[i], &m_Handle);
      if (openError != 0) {
        m_Handle = nullptr;
      }
    }
  }
  if (devices != nullptr) {
    libusb_free_device_list(devices, 1);
  }

  if (m_Handle == nullptr) {
    if (found <= which) {
      std::snprintf(m_Error, sizeof(m_Error),
                    "No ADI USBi found (USB ID %04x:%04x%s). Is the USBi connected to this "
                    "PC, and does it use the WinUSB driver? (a2bcpp/docs/setup_windows.md)",
                    kVendorId, kProductId, (which > 0) ? ", or not that many" : "");
    } else {
      std::snprintf(m_Error, sizeof(m_Error),
                    "The ADI USBi was found but can't be opened (%s). Is it using the WinUSB "
                    "driver, and is no other program (e.g. SigmaStudio) using it?",
                    libusb_error_name(openError));
    }
    libusb_exit(m_Context);
    m_Context = nullptr;
    return Result::kI2cError;
  }
#ifdef _WIN32
  timeBeginPeriod(1);
#endif
  return Result::kOk;
}

/** Closes the USBi and libusb. */
void LibusbLink::Close() {
  if (m_Handle != nullptr) {
    libusb_close(m_Handle);
    m_Handle = nullptr;
#ifdef _WIN32
    timeEndPeriod(1);
#endif
  }
  if (m_Context != nullptr) {
    libusb_exit(m_Context);
    m_Context = nullptr;
  }
}

/**
 * Runs one control transfer with libusb_control_transfer().
 * @param requestType bmRequestType.
 * @param request bRequest.
 * @param value wValue.
 * @param index wIndex.
 * @param data The data stage.
 * @param length wLength.
 * @param timeoutMs The USB timeout.
 * @return Bytes transferred, or a negative libusb error (LIBUSB_ERROR_NO_DEVICE if the
 *         USBi is not open).
 */
int LibusbLink::ControlTransfer(uint8_t requestType, uint8_t request, uint16_t value,
                                uint16_t index, uint8_t* data, uint16_t length,
                                unsigned timeoutMs) {
  if (m_Handle == nullptr) {
    return LIBUSB_ERROR_NO_DEVICE;
  }
  return libusb_control_transfer(m_Handle, requestType, request, value, index, data, length,
                                 timeoutMs);
}

/**
 * Sleeps: Sleep() on Windows (1 ms resolution while the USBi is open), nanosleep()
 * elsewhere.
 * @param ms The wait.
 */
void LibusbLink::SleepMs(unsigned ms) {
#ifdef _WIN32
  Sleep(ms);
#else
  timespec wait = {static_cast<time_t>(ms / 1000), static_cast<long>(ms % 1000) * 1000000L};
  nanosleep(&wait, nullptr);
#endif
}

}  // namespace a2b
