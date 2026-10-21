/**
 * @file strings.h
 * @brief The stack's enums as short English text, for an app's log or console: what the
 *        examples print for states, errors, rejections, removals and routing.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_A2B_STRINGS_H_
#define A2BCPP_A2B_STRINGS_H_

#include "a2b/network_listener.h"
#include "a2b/routing.h"
#include "a2b/types.h"

namespace a2b {

const char* ToString(Result result);         ///< "ok", "I2C error", ...
const char* ToString(NetworkState state);    ///< "running", "starting the main node", ...
const char* ToString(NetworkError error);    ///< Why the bus failed.
const char* ToString(RejectReason reason);   ///< Why a node was not taken in.
const char* ToString(RemovalReason reason);  ///< Why a node left the bus.
const char* ToString(EventType type);        ///< "node added", ...
const char* ToString(RoutingError error);    ///< Why a routing plan failed.
const char* ToString(RoutingMute mode);      ///< What a plan mutes while it is written.
const char* InterruptName(uint8_t type);     ///< An INTTYPE value: "SRF missed", ...

}  // namespace a2b

#endif  // A2BCPP_A2B_STRINGS_H_
