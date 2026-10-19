/**
 * @file test_image_source.cpp
 * @brief Unit tests for ImageModuleInfoSource: images per position and by default, the
 *        fallback for nodes without one, and reads of the image a node was identified with.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <vector>

#include "a2b/image_source.h"
#include "test_harness.h"

namespace a2b {
namespace {

/** An image of @p len bytes, each its offset plus @p seed. */
std::vector<uint8_t> MakeBytes(size_t len, uint8_t seed) {
  std::vector<uint8_t> image(len);
  for (size_t i = 0; i < len; ++i) {
    image[i] = static_cast<uint8_t>(i + seed);
  }
  return image;
}

/** A node at @p position. */
SubNodeIdentity At(NodeAddr position) {
  SubNodeIdentity node;
  node.position = position;
  return node;
}

/** A source that gives every node the same few bytes and counts the calls. */
class Fallback : public ModuleInfoSource {
 public:
  size_t GetModuleInfo(const SubNodeIdentity& node, uint8_t* buf, size_t bufLen) override {
    (void)node;
    ++gets;
    if (bufLen > 0) {
      buf[0] = 0x5A;
    }
    return 1;
  }
  Result ReadModuleInfo(const SubNodeIdentity& node, uint32_t offset, uint8_t* buf,
                        size_t len) override {
    (void)node;
    (void)offset;
    (void)buf;
    (void)len;
    ++reads;
    return Result::kI2cError;
  }
  int gets = 0;
  int reads = 0;
};

A2B_TEST(PositionImageThenDefaultThenNone) {
  const std::vector<uint8_t> own = MakeBytes(0x900, 1);
  const std::vector<uint8_t> fallbackImage = MakeBytes(0x200, 7);
  ImageModuleInfoSource source;
  source.SetImage(2, own.data(), own.size());
  uint8_t buf[kModuleInfoLen] = {};
  A2B_CHECK_EQ(source.GetModuleInfo(At(2), buf, sizeof(buf)), kModuleInfoLen);
  A2B_CHECK_EQ(buf[0x10], own[0x10]);
  A2B_CHECK(source.FromImage(2));
  A2B_CHECK_EQ(source.GetModuleInfo(At(3), buf, sizeof(buf)), 0u);  // No image, no fallback.
  A2B_CHECK(!source.FromImage(3));
  A2B_CHECK(source.ReadModuleInfo(At(3), 0, buf, 1) == Result::kNotSupported);
  source.SetDefault(fallbackImage.data(), fallbackImage.size());
  A2B_CHECK_EQ(source.GetModuleInfo(At(3), buf, sizeof(buf)), kModuleInfoLen);
  A2B_CHECK_EQ(buf[0], fallbackImage[0]);
  A2B_CHECK(source.FromImage(3));
}

A2B_TEST(ReadsTheImageTheNodeWasIdentifiedWith) {
  const std::vector<uint8_t> first = MakeBytes(0x900, 1);
  const std::vector<uint8_t> second = MakeBytes(0x900, 9);
  ImageModuleInfoSource source;
  source.SetImage(0, first.data(), first.size());
  uint8_t buf[kModuleInfoLen] = {};
  A2B_CHECK_EQ(source.GetModuleInfo(At(0), buf, sizeof(buf)), kModuleInfoLen);
  source.SetImage(0, second.data(), second.size());  // For the next identification.
  uint8_t unit[4] = {};
  A2B_CHECK(source.ReadModuleInfo(At(0), 0x800, unit, sizeof(unit)) == Result::kOk);
  A2B_CHECK_EQ(unit[0], first[0x800]);
  A2B_CHECK(source.ReadModuleInfo(At(0), 0x8FE, unit, 2) == Result::kOk);  // To the end.
  A2B_CHECK(source.ReadModuleInfo(At(0), 0x8FF, unit, 2) == Result::kInvalidArgument);
  A2B_CHECK(source.ReadModuleInfo(At(0), 0x901, unit, 1) == Result::kInvalidArgument);
  A2B_CHECK_EQ(source.GetModuleInfo(At(0), buf, sizeof(buf)), kModuleInfoLen);
  A2B_CHECK_EQ(buf[0], second[0]);
}

A2B_TEST(ShortImageGivesWhatItHas) {
  const std::vector<uint8_t> shortImage = MakeBytes(0x20, 0);
  ImageModuleInfoSource source;
  source.SetImage(1, shortImage.data(), shortImage.size());
  uint8_t buf[kModuleInfoLen] = {};
  A2B_CHECK_EQ(source.GetModuleInfo(At(1), buf, sizeof(buf)), 0x20u);  // ParseModuleInfo refuses.
}

A2B_TEST(NodesWithoutAnImageGoToTheFallback) {
  Fallback fallback;
  const std::vector<uint8_t> own = MakeBytes(0x100, 3);
  ImageModuleInfoSource source(&fallback);
  source.SetImage(4, own.data(), own.size());
  uint8_t buf[kModuleInfoLen] = {};
  A2B_CHECK_EQ(source.GetModuleInfo(At(5), buf, sizeof(buf)), 1u);
  A2B_CHECK_EQ(buf[0], 0x5A);
  A2B_CHECK(source.ReadModuleInfo(At(5), 0, buf, 1) == Result::kI2cError);
  A2B_CHECK_EQ(fallback.gets, 1);
  A2B_CHECK_EQ(fallback.reads, 1);
  A2B_CHECK_EQ(source.GetModuleInfo(At(4), buf, sizeof(buf)), kModuleInfoLen);
  A2B_CHECK_EQ(fallback.gets, 1);
  source.SetImage(4, nullptr, 0);  // Cleared: the fallback again.
  A2B_CHECK_EQ(source.GetModuleInfo(At(4), buf, sizeof(buf)), 1u);
  A2B_CHECK_EQ(source.GetModuleInfo(At(16), buf, sizeof(buf)), 0u);  // Not a sub-node.
}

}  // namespace
}  // namespace a2b

A2B_TEST_MAIN()
