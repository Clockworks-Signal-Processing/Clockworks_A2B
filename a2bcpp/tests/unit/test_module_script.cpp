/**
 * @file test_module_script.cpp
 * @brief Unit tests for ModuleScript: the peripheral scripts of a module-info image, read
 *        from a source as they run, against the recording fake platform.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <vector>

#include "a2b/module_info.h"
#include "fake_platform.h"
#include "module_script.h"
#include "sim/module_info.h"
#include "test_harness.h"
#include "transport.h"

namespace a2b {
namespace {

constexpr uint8_t kMainAddr = 0x68;
constexpr uint8_t kBusAddr = 0x69;

/** A source that serves an image from memory and counts its reads. */
class ImageSource : public ModuleInfoSource {
 public:
  explicit ImageSource(const std::vector<uint8_t>& image) : m_Image(image) {}

  /** The start of the image. */
  size_t GetModuleInfo(const SubNodeIdentity& node, uint8_t* buf, size_t bufLen) override {
    (void)node;
    size_t i = 0;
    for (; i < bufLen && i < m_Image.size(); ++i) {
      buf[i] = m_Image[i];
    }
    return i;
  }

  /** Any part of the image. */
  Result ReadModuleInfo(const SubNodeIdentity& node, uint32_t offset, uint8_t* buf,
                        size_t len) override {
    (void)node;
    ++reads;
    if (offset + len > m_Image.size()) {
      return Result::kInvalidArgument;
    }
    for (size_t i = 0; i < len; ++i) {
      buf[i] = m_Image[offset + i];
    }
    return Result::kOk;
  }

  int reads = 0;

 private:
  std::vector<uint8_t> m_Image;
};

/** An image with one I2C peripheral block holding @p units. */
std::vector<uint8_t> Image(const std::vector<sim::ConfigUnit>& units) {
  std::vector<uint8_t> image = sim::MakeModuleInfoImage(sim::ModuleInfoHeader());
  sim::PeripheralBlock block;
  block.name = "Test";
  block.units = units;
  sim::AddPeripheralBlocks(image, {block});
  return image;
}

/** The fake bus, a transport on it, and a script. */
struct Fixture {
  test::FakePlatform platform;
  Transport transport{platform, kMainAddr, kBusAddr};
  ModuleScript script{transport};
  SubNodeIdentity node;
  uint8_t buf[96] = {};  ///< Holds a peripheral block (83 bytes), not a 100-byte unit.

  /** Runs the script to its end, one poll per millisecond, for at most @p ms. */
  ModuleScript::State Run(ModuleInfoSource& source, uint8_t blocks, uint32_t ms = 1000) {
    script.Start(source, node, blocks, buf, sizeof(buf));
    ModuleScript::State state = ModuleScript::State::kRunning;
    for (uint32_t now = 0; now < ms && state == ModuleScript::State::kRunning; ++now) {
      platform.SetNowMs(now);
      state = script.Poll(now);
    }
    return state;
  }

  /** The data writes to the bus address with NODEADR.PERI set: the peripheral writes. */
  std::vector<std::vector<uint8_t>> Writes() const {
    std::vector<std::vector<uint8_t>> writes;
    bool peri = false;
    for (const test::FakePlatform::Transfer& t : platform.Transfers()) {
      if (t.addr == kMainAddr && t.op == 'W' && t.written.size() == 2 && t.written[0] == 0x01) {
        peri = (t.written[1] & 0x20) != 0;
      } else if (t.addr == kBusAddr && t.op == 'W' && peri) {
        writes.push_back(t.written);
      }
    }
    return writes;
  }
};

A2B_TEST(NoBlocksIsDoneAtOnce) {
  Fixture f;
  ImageSource source(Image({}));
  A2B_CHECK(f.Run(source, 0) == ModuleScript::State::kDone);
  A2B_CHECK_EQ(source.reads, 0);
}

A2B_TEST(WritesAndWaitsInOrder) {
  Fixture f;
  ImageSource source(Image({sim::ConfigUnit::Write(0x34, {0x01, 0x02}), sim::ConfigUnit::Wait(20),
                            sim::ConfigUnit::Write(0x34, {0x03}, false)}));
  f.script.Start(source, f.node, 1, f.buf, sizeof(f.buf));
  A2B_CHECK(f.script.Poll(0) == ModuleScript::State::kRunning);  // The first write.
  A2B_CHECK_EQ(f.Writes().size(), 1);
  A2B_CHECK(f.script.Poll(1) == ModuleScript::State::kRunning);  // Its wait starts.
  A2B_CHECK(f.script.Poll(20) == ModuleScript::State::kRunning);
  A2B_CHECK_EQ(f.Writes().size(), 1);
  A2B_CHECK(f.script.Poll(21) == ModuleScript::State::kRunning);  // The second write.
  A2B_CHECK(f.script.Poll(22) == ModuleScript::State::kDone);
  const std::vector<std::vector<uint8_t>> writes = f.Writes();
  A2B_CHECK_EQ(writes.size(), 2);
  if (writes.size() == 2) {
    A2B_CHECK(writes[0] == std::vector<uint8_t>({0x01, 0x02}));  // Without its CRC.
    A2B_CHECK(writes[1] == std::vector<uint8_t>({0x03}));
  }
  A2B_CHECK_EQ(f.script.UnitsRun(), 3);
  // CHIP was set to the unit's address before the writes (TRMs: CHIP, then NODEADR.PERI).
  A2B_CHECK(f.platform.Dump().find("W 69 00 34") != std::string::npos);
}

A2B_TEST(SourceWithOnlyTheStartIsDone) {
  Fixture f;
  sim::ModuleInfoHeader header;
  std::vector<uint8_t> image = sim::MakeModuleInfoImage(header);
  class StartOnly : public ModuleInfoSource {
   public:
    size_t GetModuleInfo(const SubNodeIdentity& node, uint8_t* buf, size_t bufLen) override {
      (void)node;
      (void)buf;
      return bufLen;
    }
  } source;
  A2B_CHECK(f.Run(source, 2) == ModuleScript::State::kDone);
  A2B_CHECK(f.Writes().empty());
}

A2B_TEST(UnitLongerThanTheBufferFails) {
  Fixture f;
  ImageSource source(Image({sim::ConfigUnit::Write(0x34, std::vector<uint8_t>(100, 0x5A))}));
  A2B_CHECK(f.Run(source, 1) == ModuleScript::State::kFailed);
  A2B_CHECK(f.script.Error() == Result::kNoSpace);
  A2B_CHECK(f.Writes().empty());
}

A2B_TEST(BadUnitsFail) {
  // An unknown type.
  {
    Fixture f;
    std::vector<uint8_t> image = Image({sim::ConfigUnit::Write(0x34, {0x01}, false)});
    const size_t unit = 0x808 + 3 + 80;  // After the info block (8 bytes) and the block.
    image[unit] = static_cast<uint8_t>(0x30 | (image[unit] & 0x0F));
    ImageSource source(image);
    A2B_CHECK(f.Run(source, 1) == ModuleScript::State::kFailed);
    A2B_CHECK(f.script.Error() == Result::kInvalidData);
  }
  // An address beyond 7 bits.
  {
    Fixture f;
    ImageSource source(Image({sim::ConfigUnit::Write(0x80, {0x01})}));
    A2B_CHECK(f.Run(source, 1) == ModuleScript::State::kFailed);
    A2B_CHECK(f.script.Error() == Result::kInvalidData);
  }
  // A wait with a bad CRC.
  {
    Fixture f;
    std::vector<uint8_t> image = Image({sim::ConfigUnit::Wait(5)});
    image[0x808 + 3 + 80 + 2] ^= 0x01;
    ImageSource source(image);
    A2B_CHECK(f.Run(source, 1) == ModuleScript::State::kFailed);
    A2B_CHECK(f.script.Error() == Result::kInvalidData);
  }
  // A type A unit with nothing to write.
  {
    Fixture f;
    ImageSource source(Image({sim::ConfigUnit::Write(0x34, {}, false)}));
    A2B_CHECK(f.Run(source, 1) == ModuleScript::State::kFailed);
  }
}

A2B_TEST(BadBlocksFail) {
  // More blocks than ADI's table held.
  {
    Fixture f;
    ImageSource source(Image({}));
    A2B_CHECK(f.Run(source, ModuleScript::kMaxBlocks + 1) == ModuleScript::State::kFailed);
    A2B_CHECK(f.script.Error() == Result::kInvalidData);
  }
  // More blocks than the info block lists.
  {
    Fixture f;
    ImageSource source(Image({}));
    A2B_CHECK(f.Run(source, 2) == ModuleScript::State::kFailed);
  }
  // The info block's type.
  {
    Fixture f;
    std::vector<uint8_t> image = Image({});
    image[0x800] = static_cast<uint8_t>(0x30 | (image[0x800] & 0x0F));
    ImageSource source(image);
    A2B_CHECK(f.Run(source, 1) == ModuleScript::State::kFailed);
    A2B_CHECK(f.script.Error() == Result::kInvalidData);
  }
  // The info block's CRC.
  {
    Fixture f;
    std::vector<uint8_t> image = Image({});
    image[0x807] ^= 0x01;  // Header, one address, then the CRC.
    ImageSource source(image);
    A2B_CHECK(f.Run(source, 1) == ModuleScript::State::kFailed);
  }
}

A2B_TEST(FailedWriteFails) {
  Fixture f;
  ImageSource source(Image({sim::ConfigUnit::Write(0x34, {0x01})}));
  f.script.Start(source, f.node, 1, f.buf, sizeof(f.buf));
  // NODEADR, CHIP, NODEADR.PERI, then the write: the fourth transfer.
  f.platform.FailTransfer(3);
  A2B_CHECK(f.script.Poll(0) == ModuleScript::State::kFailed);
  A2B_CHECK(f.script.Error() == Result::kI2cError);
}

A2B_TEST(StopAbandons) {
  Fixture f;
  ImageSource source(Image({sim::ConfigUnit::Wait(20), sim::ConfigUnit::Write(0x34, {0x01})}));
  f.script.Start(source, f.node, 1, f.buf, sizeof(f.buf));
  A2B_CHECK(f.script.Poll(0) == ModuleScript::State::kRunning);
  f.script.Stop();
  A2B_CHECK(f.script.Poll(50) == ModuleScript::State::kIdle);
  A2B_CHECK(f.Writes().empty());
}

}  // namespace
}  // namespace a2b

A2B_TEST_MAIN()
