/**
 * @file test_sim_events.cpp
 * @brief Simulator scenarios for events and the node review: the events and their order,
 *        14 (API calls from every event), 7 (the power budget), and
 *        NetworkListener::OnNodeIdentified().
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>
#include <vector>

#include "sim_bench.h"
#include "test_harness.h"

namespace a2b {
namespace {

using test::Bench;
using test::CountingNode;
using test::kBusAddr;
using test::MakeImage;

/** The states of the kStateChanged events, in order. */
std::vector<NetworkState> States(const std::vector<Event>& events) {
  std::vector<NetworkState> states;
  for (const Event& event : events) {
    if (event.type == EventType::kStateChanged) {
      states.push_back(event.state);
    }
  }
  return states;
}

// The events.

A2B_TEST(StartUpEventsInOrder) {
  Bench bench;
  bench.PlugChain(2);
  A2B_CHECK(bench.StartWith(2));
  const std::vector<Event>& e = bench.listener.events;
  A2B_CHECK_EQ(e.size(), 6);
  if (e.size() != 6) {
    return;
  }
  A2B_CHECK(e[0].type == EventType::kStateChanged && e[0].state == NetworkState::kStarting);
  A2B_CHECK(e[1].type == EventType::kStateChanged && e[1].state == NetworkState::kConfiguring);
  A2B_CHECK(e[2].type == EventType::kStateChanged && e[2].state == NetworkState::kDiscovering);
  A2B_CHECK(e[3].type == EventType::kNodeAdded && e[3].node == 0 && e[3].chipId[5] == 0x10);
  A2B_CHECK(e[4].type == EventType::kNodeAdded && e[4].node == 1 && e[4].chipId[5] == 0x11);
  A2B_CHECK(e[5].type == EventType::kStateChanged && e[5].state == NetworkState::kRunning);
}

A2B_TEST(EventsComeAtTheEndOfPollWhenTheBusIsBetweenSteps) {
  // A kNodeAdded handler sees the node in the bus, with its object, and may reach it.
  Bench bench;
  bench.PlugChain(1);
  bench.bus.Sub(0).AddEeprom(0x50).Load(MakeImage());
  bool reached = false;
  bench.listener.onEvent = [&](Network& network, const Event& event) {
    if (event.type != EventType::kNodeAdded) {
      return;
    }
    A2B_CHECK_EQ(network.SubNodeCount(), static_cast<size_t>(event.node) + 1);
    A2B_CHECK(network.SubNode(event.node) != nullptr && network.NodeObject(event.node) != nullptr);
    uint8_t marker = 0;
    const uint8_t address[2] = {0x00, 0x00};
    reached = network.PeripheralWriteRead(event.node, PeripheralAddress::I2c(0x50), address, 2,
                                          &marker, 1) == Result::kOk &&
              marker == 0xAA;
  };
  A2B_CHECK(bench.StartWith(1));
  A2B_CHECK(reached);
}

A2B_TEST(PollFromAHandlerReturnsAtOnce) {
  Bench bench;
  bench.PlugChain(1);
  int transfersInPoll = -1;
  bench.listener.onEvent = [&](Network& network, const Event& event) {
    if (event.type == EventType::kNodeAdded) {
      const size_t before = bench.sim.TransferCount();
      bench.sim.Advance(100);
      network.Poll(bench.sim.NowMs());
      transfersInPoll = static_cast<int>(bench.sim.TransferCount() - before);
    }
  };
  A2B_CHECK(bench.StartWith(1));
  A2B_CHECK_EQ(transfersInPoll, 0);
}

A2B_TEST(WithoutAListenerEventsAreDropped) {
  Bench bench;
  bench.network.SetListener(nullptr);
  bench.PlugChain(1);
  A2B_CHECK(bench.StartWith(1));
  bench.network.SetListener(&bench.listener);
  bench.Run(10);
  A2B_CHECK(bench.listener.events.empty());  // None kept from before.
}

A2B_TEST(StopAndStartQueueTheirEvents) {
  Bench bench;
  bench.PlugChain(2);
  A2B_CHECK(bench.StartWith(2));
  bench.listener.events.clear();
  bench.network.Stop();
  A2B_CHECK_EQ(CountingNode::removed, 2);  // At once...
  A2B_CHECK(bench.listener.events.empty());
  bench.Run(1);  // ... the events at the next Poll().
  const std::vector<Event> removed = bench.listener.Of(EventType::kNodeRemoved);
  A2B_CHECK(removed.size() == 2 && removed[0].removal == RemovalReason::kStopped);
  A2B_CHECK(States(bench.listener.events) == std::vector<NetworkState>{NetworkState::kStopped});
  bench.listener.events.clear();
  A2B_CHECK(bench.StartWith(2));
  A2B_CHECK(bench.listener.Of(EventType::kNodeRemoved).empty());  // Nothing was left.
  A2B_CHECK(bench.network.Start() == Result::kOk);                // Starting again while running.
  bench.Run(1);
  const std::vector<Event> restarted = bench.listener.Of(EventType::kNodeRemoved);
  A2B_CHECK(restarted.size() == 2 && restarted[0].removal == RemovalReason::kRestart);
}

A2B_TEST(PllRelockRemovesTheNodesWithEvents) {
  Bench bench;
  bench.PlugChain(1);
  A2B_CHECK(bench.StartWith(1));
  bench.listener.events.clear();
  bench.bus.SetSync(false);
  bench.Run(10);
  bench.bus.SetSync(true);
  A2B_CHECK(bench.RunUntil(NetworkState::kStarting, 100));
  bench.Run(1);
  const std::vector<Event> removed = bench.listener.Of(EventType::kNodeRemoved);
  A2B_CHECK(removed.size() == 1 && removed[0].removal == RemovalReason::kRestart);
  A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 3000));
}

A2B_TEST(RejectionEvent) {
  Bench bench;
  bench.PlugChain(1);
  sim::SubNodeSpec ad2410 = test::Ad2437(0x77);
  ad2410.chip.productId = 0x10;  // Not supported.
  bench.bus.Plug(ad2410);
  A2B_CHECK(bench.StartWith(1));
  const std::vector<Event> rejected = bench.listener.Of(EventType::kNodeRejected);
  A2B_CHECK(rejected.size() == 1 && rejected[0].node == 1 &&
            rejected[0].rejection == RejectReason::kUnsupportedChip &&
            rejected[0].chipId[5] == 0x77);
  // Probed again later, it is switched off again without a new event.
  bench.Run(2 * bench.board.timings.rejectedProbeMs + 100);
  A2B_CHECK_EQ(bench.listener.Of(EventType::kNodeRejected).size(), 1);
}

// Scenario 14: API calls from every event.

A2B_TEST(Scenario14RejectNodeFromNodeAdded) {
  // The app rejects sub-node 1 as soon as it is added: it is taken out at the next Poll(),
  // and the chain stays at one node.
  Bench bench;
  bench.PlugChain(3);
  bench.listener.onEvent = [](Network& network, const Event& event) {
    if (event.type == EventType::kNodeAdded && event.node == 1) {
      A2B_CHECK(network.RejectNode(event.node) == Result::kOk);
    }
  };
  A2B_CHECK(bench.network.Start() == Result::kOk);
  bench.Run(3000);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
  A2B_CHECK(bench.network.LastRejection() == RejectReason::kByApp);
  const std::vector<Event> removed = bench.listener.Of(EventType::kNodeRemoved);
  A2B_CHECK(!removed.empty() && removed[0].node == 1 &&
            removed[0].removal == RemovalReason::kRejected);
  A2B_CHECK(bench.network.State() == NetworkState::kRunning);
}

A2B_TEST(Scenario14StopAndStartFromStateChanges) {
  // Stop() when the chain is first up, Start() again from the kStopped event.
  Bench bench;
  bench.PlugChain(2);
  int stops = 0;
  int starts = 0;
  bench.listener.onEvent = [&](Network& network, const Event& event) {
    if (event.type != EventType::kStateChanged) {
      return;
    }
    if (event.state == NetworkState::kRunning && stops == 0) {
      ++stops;
      network.Stop();
    } else if (event.state == NetworkState::kStopped && starts == 0) {
      ++starts;
      A2B_CHECK(network.Start() == Result::kOk);
    }
  };
  A2B_CHECK(bench.network.Start() == Result::kOk);
  bench.Run(4000);
  A2B_CHECK_EQ(stops, 1);
  A2B_CHECK_EQ(starts, 1);
  A2B_CHECK(bench.network.State() == NetworkState::kRunning);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 2);
}

A2B_TEST(Scenario14StartFromNodeRemoved) {
  Bench bench;
  bench.PlugChain(2);
  A2B_CHECK(bench.StartWith(2));
  int starts = 0;
  bench.listener.onEvent = [&](Network& network, const Event& event) {
    if (event.type == EventType::kNodeRemoved && event.removal == RemovalReason::kDropped) {
      ++starts;
      A2B_CHECK(network.Start() == Result::kOk);
    }
  };
  bench.bus.Unplug(1);
  bench.Run(bench.board.timings.chainCheckMs + 5);
  A2B_CHECK_EQ(starts, 1);  // Once: the Start() in the handler removes nothing more.
  A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 3000));
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
}

A2B_TEST(Scenario14CallsFromRejectedAndInterruptEvents) {
  // From kNodeRejected: a link profile and pausing; from kInterrupt: RejectNode(),
  // ResumeDiscovery() and a peripheral read of a node that isn't there.
  Bench bench;
  bench.PlugChain(2);
  sim::SubNodeSpec ad2410 = test::Ad2437(0x77);
  ad2410.chip.productId = 0x10;
  bench.bus.Plug(ad2410);
  bench.listener.onEvent = [](Network& network, const Event& event) {
    if (event.type == EventType::kNodeRejected) {
      A2B_CHECK(network.SetLinkProfile(event.node, LinkProfile::Utp()) == Result::kOk);
      network.PauseDiscovery();
    } else if (event.type == EventType::kInterrupt) {
      uint8_t byte = 0;
      A2B_CHECK(network.PeripheralRead(5, PeripheralAddress::I2c(0x50), &byte, 1) ==
                Result::kInvalidArgument);
      A2B_CHECK(network.RejectNode(event.node) == Result::kOk);
      network.ResumeDiscovery();
    }
  };
  A2B_CHECK(bench.StartWith(2));
  A2B_CHECK(bench.network.DiscoveryPaused());
  bench.bus.RaiseInterrupt(1, 16);
  bench.Run(100);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
  A2B_CHECK(!bench.network.DiscoveryPaused());
  A2B_CHECK(bench.network.State() == NetworkState::kRunning);
}

A2B_TEST(Scenario14SetListenerFromAHandler) {
  // A handler that removes the listener: the events left are dropped.
  Bench bench;
  bench.PlugChain(2);
  bench.listener.onEvent = [](Network& network, const Event& event) {
    if (event.type == EventType::kNodeAdded) {
      network.SetListener(nullptr);
    }
  };
  A2B_CHECK(bench.StartWith(2));
  A2B_CHECK_EQ(bench.listener.Of(EventType::kNodeAdded).size(), 1);
}

A2B_TEST(Scenario14HandlerCantHoldPollForEver) {
  // A handler that queues an event for every event it gets (Stop(), Start(), ...): each
  // Poll() still returns, and the rest wait for the next one.
  Bench bench;
  bench.PlugChain(1);
  A2B_CHECK(bench.StartWith(1));
  int calls = 0;
  bench.listener.onEvent = [&](Network& network, const Event& event) {
    ++calls;
    if (event.type == EventType::kStateChanged) {
      if (event.state == NetworkState::kStopped) {
        (void)network.Start();
      } else {
        network.Stop();
      }
    }
  };
  bench.network.Stop();
  bench.Run(1);
  A2B_CHECK(calls > 0 && calls <= 64);
  bench.listener.onEvent = nullptr;
  bench.Run(1);
}

// Scenario 7: the power budget, and the app's review.

A2B_TEST(Scenario7PowerBudget) {
  // Bus-powered modules of 1000 mW and 100 mA each, and a budget of 2500 mW: the third is
  // rejected. Its position is probed again later, without a new event.
  BoardConfig board = test::MakeBoard();
  board.policy.maxBusPowerMw = 2500;
  Bench bench(board, {MakeImage(1000, 100)});
  bench.PlugChain(4);
  A2B_CHECK(bench.StartWith(2));
  A2B_CHECK(bench.network.LastRejection() == RejectReason::kPowerBudget);
  const std::vector<Event> rejected = bench.listener.Of(EventType::kNodeRejected);
  A2B_CHECK(rejected.size() == 1 && rejected[0].node == 2 &&
            rejected[0].rejection == RejectReason::kPowerBudget);
  A2B_CHECK_EQ(bench.listener.reviews.size(), 3);
  if (bench.listener.reviews.size() == 3) {
    const NodeReview& review = bench.listener.reviews[2];
    A2B_CHECK_EQ(review.busPowerMw, 2000);
    A2B_CHECK_EQ(review.busCurrentMa, 200);
    A2B_CHECK(review.policy == RejectReason::kPowerBudget);
    A2B_CHECK(review.node != nullptr && review.node->moduleInfo.maxPowerMw == 1000);
  }
  A2B_CHECK_EQ(bench.bus.DiscoveredCount(), 2);  // Switched off.
  bench.Run(bench.board.timings.rejectedProbeMs + 100);
  A2B_CHECK_EQ(bench.listener.Of(EventType::kNodeRejected).size(), 1);
  A2B_CHECK_EQ(bench.listener.reviews.size(), 3);
}

A2B_TEST(Scenario7CurrentBudget) {
  BoardConfig board = test::MakeBoard();
  board.policy.maxBusCurrentMa = 150;
  Bench bench(board, {MakeImage(1000, 100)});
  bench.PlugChain(2);
  A2B_CHECK(bench.StartWith(1));
  A2B_CHECK(bench.network.LastRejection() == RejectReason::kPowerBudget);
}

A2B_TEST(Scenario7LocallyPoweredModulesDontCount) {
  BoardConfig board = test::MakeBoard();
  board.policy.maxBusPowerMw = 1500;
  // Sub-node 1 has its own supply; 0 and 2 are bus-powered.
  Bench bench(board, {MakeImage(1000), MakeImage(5000, 0, true), MakeImage(400)});
  bench.PlugChain(3);
  A2B_CHECK(bench.StartWith(3));
  A2B_CHECK(bench.network.LastRejection() == RejectReason::kNone);
  A2B_CHECK_EQ(bench.listener.reviews.size(), 3);
  if (bench.listener.reviews.size() == 3) {
    A2B_CHECK_EQ(bench.listener.reviews[2].busPowerMw, 1000);
  }
}

A2B_TEST(Scenario7BudgetFreedByADrop) {
  // The third node is over budget; when the second drops and the chain is found again from
  // there, the third is reviewed again (the rejected chip ID is forgotten with the drop).
  BoardConfig board = test::MakeBoard();
  board.policy.maxBusPowerMw = 2000;
  Bench bench(board, {MakeImage(1000)});
  bench.PlugChain(3);
  A2B_CHECK(bench.StartWith(2));
  bench.bus.ResetSub(1);
  A2B_CHECK(bench.RunUntilCount(1, 1000));
  A2B_CHECK(bench.RunUntilCount(2, 2000));
  bench.Run(500);
  A2B_CHECK_EQ(bench.listener.Of(EventType::kNodeRejected).size(), 2);
}

A2B_TEST(ListenerOverrulesThePolicy) {
  BoardConfig board = test::MakeBoard();
  board.policy.maxBusPowerMw = 1500;
  Bench bench(board, {MakeImage(1000)});
  bench.listener.decide = [](const NodeReview& review) {
    (void)review;
    return RejectReason::kNone;  // Take everything.
  };
  bench.PlugChain(3);
  A2B_CHECK(bench.StartWith(3));
}

A2B_TEST(ListenerRejectsByChipId) {
  Bench bench;
  bench.listener.decide = [](const NodeReview& review) {
    return (review.node->identity.chipId[5] == 0x11) ? RejectReason::kByApp : review.policy;
  };
  bench.PlugChain(3);
  A2B_CHECK(bench.StartWith(1));
  A2B_CHECK(bench.network.LastRejection() == RejectReason::kByApp);
  const std::vector<Event> rejected = bench.listener.Of(EventType::kNodeRejected);
  A2B_CHECK(rejected.size() == 1 && rejected[0].rejection == RejectReason::kByApp);
  A2B_CHECK_EQ(CountingNode::removed, 0);  // No object was made for it.
  A2B_CHECK(bench.network.NodeObject(1) == nullptr);
  A2B_CHECK_EQ(bench.bus.Sub(1).Reg(reg::kGpioien), 0x00);  // Not configured.
}

A2B_TEST(DefaultPolicyWithoutAListener) {
  BoardConfig board = test::MakeBoard();
  board.policy.maxBusPowerMw = 1500;
  Bench bench(board, {MakeImage(1000)});
  bench.network.SetListener(nullptr);
  bench.PlugChain(2);
  A2B_CHECK(bench.StartWith(1));
  A2B_CHECK(bench.network.LastRejection() == RejectReason::kPowerBudget);
}

}  // namespace
}  // namespace a2b

A2B_TEST_MAIN()
