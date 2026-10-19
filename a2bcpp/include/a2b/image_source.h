/**
 * @file image_source.h
 * @brief ImageModuleInfoSource: sub-nodes' module info from images in memory (an array
 *        compiled into the program, or an image file the app has read), so a module is
 *        set up from an image before, or without, its EEPROM holding it.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_A2B_IMAGE_SOURCE_H_
#define A2BCPP_A2B_IMAGE_SOURCE_H_

#include <cstddef>
#include <cstdint>

#include "a2b/module_info.h"
#include "a2b/types.h"

namespace a2b {

/// Gives each position the image set for it, or the default image, else asks a fallback
/// source (the EEPROMs, for instance). The images are the app's: not copied.
class ImageModuleInfoSource : public ModuleInfoSource {
 public:
  explicit ImageModuleInfoSource(
      ModuleInfoSource* fallback = nullptr);  ///< No images; every node to @p fallback.

  void SetImage(NodeAddr position, const uint8_t* image,
                size_t len);                          ///< A position's image; nullptr clears.
  void SetDefault(const uint8_t* image, size_t len);  ///< For positions without their own.
  void SetFallback(ModuleInfoSource* fallback);       ///< For the nodes with no image.
  bool FromImage(NodeAddr position) const;            ///< The node's info is an image's.
  size_t GetModuleInfo(const SubNodeIdentity& node, uint8_t* buf,
                       size_t bufLen) override;  ///< The start of the node's image.
  Result ReadModuleInfo(const SubNodeIdentity& node, uint32_t offset, uint8_t* buf,
                        size_t len) override;  ///< More of it.

 private:
  /** An image: where it is and its length. */
  struct Image {
    const uint8_t* data = nullptr;
    size_t len = 0;
  };

  Image m_Images[kMaxSubNodes];  ///< Each position's image, if set.
  Image m_Default;               ///< For positions without their own.
  Image m_Using[kMaxSubNodes];   ///< The image each node was identified with.
  ModuleInfoSource* m_Fallback;  ///< For the nodes with no image; may be null.
};

}  // namespace a2b

#endif  // A2BCPP_A2B_IMAGE_SOURCE_H_
