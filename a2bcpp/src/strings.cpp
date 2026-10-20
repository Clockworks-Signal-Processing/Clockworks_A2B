/**
 * @file strings.cpp
 * @brief The stack's enums as text (a2b/strings.h). Each function returns "?" for a value
 *        it doesn't know.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "a2b/strings.h"

namespace a2b {

/**
 * A name for a result.
 * @param result The result.
 * @return Its name.
 */
const char* ToString(Result result) {
  switch (result) {
    case Result::kOk:
      return "ok";
    case Result::kI2cError:
      return "I2C error";
    case Result::kInvalidArgument:
      return "invalid argument";
    case Result::kNotSupported:
      return "not supported";
    case Result::kNoSpace:
      return "no space";
    case Result::kInvalidData:
      return "invalid data";
  }
  return "?";
}

/**
 * A name for a bus state.
 * @param state The state.
 * @return Its name.
 */
const char* ToString(NetworkState state) {
  switch (state) {
    case NetworkState::kStopped:
      return "stopped";
    case NetworkState::kStarting:
      return "starting the main node";
    case NetworkState::kConfiguring:
      return "configuring the main node";
    case NetworkState::kDiscovering:
      return "bringing up the chain";
    case NetworkState::kRunning:
      return "running";
    case NetworkState::kFailed:
      return "failed";
  }
  return "?";
}

/**
 * Why a bus failed.
 * @param error The error.
 * @return Its text.
 */
const char* ToString(NetworkError error) {
  switch (error) {
    case NetworkError::kNone:
      return "none";
    case NetworkError::kI2cError:
      return "the main node does not answer";
    case NetworkError::kWrongMainNode:
      return "the main node is not the board's chip";
    case NetworkError::kMainNodeNotRunning:
      return "no MSTR_RUNNING (is SYNC there?)";
    case NetworkError::kHostScriptFailed:
      return "the board's host script failed";
  }
  return "?";
}

/**
 * Why a node was not taken in.
 * @param reason The reason.
 * @return Its text.
 */
const char* ToString(RejectReason reason) {
  switch (reason) {
    case RejectReason::kNone:
      return "none";
    case RejectReason::kUnsupportedChip:
      return "unsupported chip";
    case RejectReason::kNoModuleInfo:
      return "no module info (no EEPROM at 0x50-0x54, or a blank one)";
    case RejectReason::kBadModuleInfo:
      return "bad module info (marker, header CRC or block)";
    case RejectReason::kConfigFailed:
      return "configuration failed";
    case RejectReason::kPeripheralFailed:
      return "peripheral script failed";
    case RejectReason::kPowerBudget:
      return "over the power budget";
    case RejectReason::kByApp:
      return "by the app";
    case RejectReason::kWrongChip:
      return "its image is for another transceiver";
    case RejectReason::kNoHighVoltage:
      return "no 24 V on its link";
  }
  return "?";
}

/**
 * Why a node left the bus.
 * @param reason The reason.
 * @return Its text.
 */
const char* ToString(RemovalReason reason) {
  switch (reason) {
    case RemovalReason::kDropped:
      return "dropped (unplugged, or a node before it was)";
    case RemovalReason::kRejected:
      return "rejected by the app";
    case RemovalReason::kRestart:
      return "the bus starts again";
    case RemovalReason::kStopped:
      return "the bus stopped";
  }
  return "?";
}

/**
 * A name for an event type.
 * @param type The type.
 * @return Its name.
 */
const char* ToString(EventType type) {
  switch (type) {
    case EventType::kStateChanged:
      return "state changed";
    case EventType::kNodeAdded:
      return "node added";
    case EventType::kNodeRejected:
      return "node rejected";
    case EventType::kNodeRemoved:
      return "node removed";
    case EventType::kInterrupt:
      return "interrupt";
    case EventType::kRoutingApplied:
      return "routing applied";
    case EventType::kRoutingFailed:
      return "routing failed";
  }
  return "?";
}

/**
 * Why a routing plan could not be built or written.
 * @param error The error.
 * @return Its text.
 */
const char* ToString(RoutingError error) {
  switch (error) {
    case RoutingError::kNone:
      return "none";
    case RoutingError::kNoSuchGroup:
      return "a connection names a group its node hasn't got";
    case RoutingError::kSameNode:
      return "a connection's source and sink are the same node";
    case RoutingError::kTooManySlots:
      return "more than 32 slots on a link";
    case RoutingError::kTooManyChannels:
      return "a node would play more than 32 channels";
    case RoutingError::kNoResponseCycles:
      return "the slots don't fit the superframe (no response-cycle window)";
    case RoutingError::kI2cError:
      return "I2C error while writing it";
    case RoutingError::kNoSuchChannel:
      return "a connection uses a channel its node's I2S/TDM port doesn't give";
    case RoutingError::kChannelOrder:
      return "a node without a crossbar (AD242x) can't play its channels in that order";
  }
  return "?";
}

/**
 * A name for a routing mute mode.
 * @param mode The mode.
 * @return Its name.
 */
const char* ToString(RoutingMute mode) {
  switch (mode) {
    case RoutingMute::kLocal:
      return "local (the nodes that would play a wrong channel)";
    case RoutingMute::kGlobal:
      return "global (the whole bus)";
    case RoutingMute::kNone:
      return "none";
  }
  return "?";
}

}  // namespace a2b
