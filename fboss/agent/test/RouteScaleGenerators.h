/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <memory>
#include <vector>

#include "fboss/agent/test/RouteDistributionGenerator.h"

namespace facebook::fboss {
class SwitchState;
}
namespace facebook::fboss::utility {

constexpr unsigned int kDefaultChunkSize = 4000;
constexpr unsigned int kDefaulEcmpWidth = 4;

/*
 * Each of the following generators take a input state and chunk size
 * to generate a sequence of switch states that can be used to program
 * the required route distribution.
 */
class RSWRouteScaleGenerator : public RouteDistributionGenerator {
 public:
  explicit RSWRouteScaleGenerator(
      const std::shared_ptr<SwitchState>& startingState,
      unsigned int chunkSize = kDefaultChunkSize,
      unsigned int ecmpWidth = kDefaulEcmpWidth,
      RouterID routerId = RouterID(0));
};

class FSWRouteScaleGenerator : public RouteDistributionGenerator {
 public:
  explicit FSWRouteScaleGenerator(
      const std::shared_ptr<SwitchState>& startingState,
      unsigned int chunkSize = kDefaultChunkSize,
      unsigned int ecmpWidth = kDefaulEcmpWidth,
      RouterID routerId = RouterID(0));
};

class THAlpmRouteScaleGenerator : public RouteDistributionGenerator {
 public:
  explicit THAlpmRouteScaleGenerator(
      const std::shared_ptr<SwitchState>& startingState,
      unsigned int chunkSize = kDefaultChunkSize,
      unsigned int ecmpWidth = kDefaulEcmpWidth,
      RouterID routerId = RouterID(0));
};

class HgridDuRouteScaleGenerator : public RouteDistributionGenerator {
 public:
  explicit HgridDuRouteScaleGenerator(
      const std::shared_ptr<SwitchState>& startingState,
      unsigned int chunkSize = kDefaultChunkSize,
      unsigned int ecmpWidth = kDefaulEcmpWidth,
      RouterID routerId = RouterID(0));
};

class HgridUuRouteScaleGenerator : public RouteDistributionGenerator {
 public:
  explicit HgridUuRouteScaleGenerator(
      const std::shared_ptr<SwitchState>& startingState,
      unsigned int chunkSize = kDefaultChunkSize,
      unsigned int ecmpWidth = kDefaulEcmpWidth,
      RouterID routerId = RouterID(0));
};

class TurboFSWRouteScaleGenerator : public RouteDistributionGenerator {
 public:
  explicit TurboFSWRouteScaleGenerator(
      const std::shared_ptr<SwitchState>& startingState,
      unsigned int chunkSize = kDefaultChunkSize,
      // in reality, 33 are mesh links and 36 are ssw links, giving total of 69,
      // but approaximating to 64 for now, as total number of ports in test
      // platform on minimpack are 64.
      // TODO: extend it beyond 64 and then can be 69
      unsigned int ecmpWidth = 64,
      RouterID routerId = RouterID(0));

  const SwitchStates& getSwitchStates() const;
  bool isSupported(PlatformMode mode) const;
};

} // namespace facebook::fboss::utility
