/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/platforms/common/PlatformMapping.h"

#include <thrift/lib/cpp/util/EnumUtils.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "fboss/agent/FbossError.h"

namespace facebook {
namespace fboss {
PlatformMapping::PlatformMapping(const std::string& jsonPlatformMappingStr) {
  auto mapping =
      apache::thrift::SimpleJSONSerializer::deserialize<cfg::PlatformMapping>(
          jsonPlatformMappingStr);
  platformPorts_ = std::move(mapping.ports);
  supportedProfiles_ = std::move(mapping.supportedProfiles);
  for (auto chip : mapping.chips) {
    chips_[chip.name] = chip;
  }
}

void PlatformMapping::merge(PlatformMapping* mapping) {
  for (auto port : mapping->platformPorts_) {
    platformPorts_.emplace(port.first, std::move(port.second));
  }
  mapping->platformPorts_.clear();

  for (auto profile : mapping->supportedProfiles_) {
    supportedProfiles_.emplace(profile.first, std::move(profile.second));
  }
  mapping->supportedProfiles_.clear();

  for (auto chip : mapping->chips_) {
    chips_.emplace(chip.first, std::move(chip.second));
  }
  mapping->chips_.clear();
}

cfg::PortProfileID PlatformMapping::getPortMaxSpeedProfile(
    PortID portID) const {
  auto itPlatformPort = platformPorts_.find(portID);
  if (itPlatformPort == platformPorts_.end()) {
    throw FbossError("Unrecoganized port:", portID);
  }

  cfg::PortProfileID maxProfile{cfg::PortProfileID::PROFILE_DEFAULT};
  cfg::PortSpeed maxSpeed{cfg::PortSpeed::DEFAULT};
  for (auto profile : itPlatformPort->second.supportedProfiles) {
    if (auto itProfileCfg = supportedProfiles_.find(profile.first);
        itProfileCfg != supportedProfiles_.end() &&
        static_cast<int>(maxSpeed) <
            static_cast<int>(itProfileCfg->second.speed)) {
      maxSpeed = itProfileCfg->second.speed;
      maxProfile = itProfileCfg->first;
    }
  }
  return maxProfile;
}

cfg::PortSpeed PlatformMapping::getPortMaxSpeed(PortID portID) const {
  auto maxProfile = getPortMaxSpeedProfile(portID);
  if (auto itProfileCfg = supportedProfiles_.find(maxProfile);
      itProfileCfg != supportedProfiles_.end()) {
    return itProfileCfg->second.speed;
  }
  return cfg::PortSpeed::DEFAULT;
}

const std::vector<phy::PinConfig>& PlatformMapping::getPortIphyPinConfigs(
    PortID id,
    cfg::PortProfileID profileID) const {
  auto itPlatformPort = platformPorts_.find(id);
  if (itPlatformPort == platformPorts_.end()) {
    throw FbossError("No PlatformPortEntry found for port ", id);
  }

  auto supportedProfiles = itPlatformPort->second.supportedProfiles;
  auto platformPortConfig = supportedProfiles.find(profileID);
  if (platformPortConfig == supportedProfiles.end()) {
    throw FbossError(
        "No speed profile with id ",
        apache::thrift::util::enumNameSafe(profileID),
        " found in PlatformPortEntry for port ",
        id);
  }

  return platformPortConfig->second.pins.iphy;
}
} // namespace fboss
} // namespace facebook
