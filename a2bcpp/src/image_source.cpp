/**
 * @file image_source.cpp
 * @brief ImageModuleInfoSource: module info from images in memory.
 *
 * An image is the EEPROM's contents from address 0 (ADI's PnP layout, as the image tool
 * tools/a2b_eeprom writes a .dat file): GetModuleInfo() copies its start and
 * ReadModuleInfo() any later part, as EepromModuleInfoSource reads them from an EEPROM.
 * Nothing is copied when an image is set: the app keeps it valid while nodes are found
 * and set up (a const array in flash on the RP2040, or a buffer the app owns). The image a
 * node was identified with is the one its later reads use, even if the app sets another
 * meanwhile. Nodes without an image go to the fallback source, if there is one; with none,
 * they have no module info and are rejected.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "a2b/image_source.h"

#include <cstring>

namespace a2b {

/**
 * Makes a source with no images.
 * @param fallback Where the nodes without an image get their module info (it must outlive
 *        this source); nullptr: they get none.
 */
ImageModuleInfoSource::ImageModuleInfoSource(ModuleInfoSource* fallback) : m_Fallback(fallback) {}

/**
 * Sets the image for a position, for nodes identified there from then on.
 * @param position 0-15; others are ignored.
 * @param image The image from EEPROM address 0, kept by the app while it is in use;
 *        nullptr clears the position's image.
 * @param len Its length.
 */
void ImageModuleInfoSource::SetImage(NodeAddr position, const uint8_t* image, size_t len) {
  if (!IsSubNode(position)) {
    return;
  }
  m_Images[position].data = image;
  m_Images[position].len = (image != nullptr) ? len : 0;
}

/**
 * Sets the image for every position without its own.
 * @param image The image, kept by the app while it is in use; nullptr: none.
 * @param len Its length.
 */
void ImageModuleInfoSource::SetDefault(const uint8_t* image, size_t len) {
  m_Default.data = image;
  m_Default.len = (image != nullptr) ? len : 0;
}

/**
 * Sets the source for the nodes with no image.
 * @param fallback The source, which must outlive this one; nullptr: none.
 */
void ImageModuleInfoSource::SetFallback(ModuleInfoSource* fallback) { m_Fallback = fallback; }

/**
 * Tells whether a node's module info came from an image, at its last identification.
 * @param position The node.
 * @return True if from an image; false if from the fallback, or not identified.
 */
bool ImageModuleInfoSource::FromImage(NodeAddr position) const {
  return IsSubNode(position) && m_Using[position].data != nullptr;
}

/**
 * Gives the start of a node's image: its position's, else the default, else the
 * fallback's.
 * @param node The node, discovered and identified.
 * @param buf Buffer for the image.
 * @param bufLen Its size; up to #kModuleInfoLen bytes are copied.
 * @return Bytes copied: fewer than #kModuleInfoLen if the image is shorter (the stack then
 *         rejects it as bad module info); 0 if there is no image and no fallback.
 */
size_t ImageModuleInfoSource::GetModuleInfo(const SubNodeIdentity& node, uint8_t* buf,
                                            size_t bufLen) {
  if (!IsSubNode(node.position) || buf == nullptr) {
    return 0;
  }
  const Image& own = m_Images[node.position];
  const Image& image = (own.data != nullptr) ? own : m_Default;
  m_Using[node.position] = image;
  if (image.data == nullptr) {
    return (m_Fallback != nullptr) ? m_Fallback->GetModuleInfo(node, buf, bufLen) : 0;
  }
  size_t len = (bufLen < kModuleInfoLen) ? bufLen : kModuleInfoLen;
  len = (image.len < len) ? image.len : len;
  std::memcpy(buf, image.data, len);
  return len;
}

/**
 * Gives more of the image the node was identified with, or asks the fallback.
 * @param node The node.
 * @param offset Offset in the image.
 * @param buf Buffer for the bytes.
 * @param len Bytes to read.
 * @return Result::kOk; Result::kInvalidArgument past the image's end; for a node without
 *         an image, the fallback's answer, or Result::kNotSupported without one.
 */
Result ImageModuleInfoSource::ReadModuleInfo(const SubNodeIdentity& node, uint32_t offset,
                                             uint8_t* buf, size_t len) {
  if (!IsSubNode(node.position)) {
    return Result::kInvalidArgument;
  }
  const Image& image = m_Using[node.position];
  if (image.data == nullptr) {
    return (m_Fallback != nullptr) ? m_Fallback->ReadModuleInfo(node, offset, buf, len)
                                   : Result::kNotSupported;
  }
  if (buf == nullptr || offset > image.len || len > image.len - offset) {
    return Result::kInvalidArgument;
  }
  std::memcpy(buf, image.data + offset, len);
  return Result::kOk;
}

}  // namespace a2b
