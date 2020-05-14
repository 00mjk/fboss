/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/LacpTypes.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"

#include <folly/Conv.h>
#include <folly/Range.h>

namespace facebook::fboss {

LACPDU::LACPDU(ParticipantInfo actor, ParticipantInfo partner)
    : actorInfo(actor), partnerInfo(partner) {}

ParticipantInfo ParticipantInfo::defaultParticipantInfo() {
  return ParticipantInfo();
}

bool ParticipantInfo::operator==(const ParticipantInfo& rhs) const {
  return systemPriority == rhs.systemPriority && systemID == rhs.systemID &&
      key == rhs.key && portPriority == rhs.portPriority && port == rhs.port &&
      state == rhs.state;
}

bool ParticipantInfo::operator!=(const ParticipantInfo& rhs) const {
  return !(*this == rhs);
}

std::string ParticipantInfo::describe() const {
  auto systemAsMacAddr = folly::MacAddress::fromBinary(
      folly::ByteRange(systemID.begin(), systemID.end()));

  return folly::to<std::string>(
      "(SystemPriority ",
      systemPriority,
      ", SystemID ",
      systemAsMacAddr.toString(),
      ", Key ",
      key,
      ", PortPriority ",
      portPriority,
      ", Port ",
      port,
      ", State ",
      state,
      ")");
}

void ParticipantInfo::populate(LacpEndpoint& endpoint) const {
  auto systemAsMacAddr = folly::MacAddress::fromBinary(
      folly::ByteRange(systemID.begin(), systemID.end()));

  // All conversions to int32_t (the type underlying Thrift's "i32") are from
  // uint16_t
  *endpoint.systemPriority_ref() = static_cast<int32_t>(systemPriority);
  *endpoint.systemID_ref() = systemAsMacAddr.toString();
  *endpoint.key_ref() = static_cast<int32_t>(key);
  *endpoint.portPriority_ref() = static_cast<int32_t>(portPriority);
  *endpoint.port_ref() = static_cast<int32_t>(port);

  *endpoint.state_ref()->active_ref() = state & LacpState::ACTIVE;
  *endpoint.state_ref()->shortTimeout_ref() = state & LacpState::SHORT_TIMEOUT;
  *endpoint.state_ref()->aggregatable_ref() = state & LacpState::AGGREGATABLE;
  *endpoint.state_ref()->inSync_ref() = state & LacpState::IN_SYNC;
  *endpoint.state_ref()->collecting_ref() = state & LacpState::COLLECTING;
  *endpoint.state_ref()->distributing_ref() = state & LacpState::DISTRIBUTING;
  *endpoint.state_ref()->defaulted_ref() = state & LacpState::DEFAULTED;
  *endpoint.state_ref()->expired_ref() = state & LacpState::EXPIRED;
}

bool LACPDU::isValid() const {
  // TODO(samank): validate frame
  return true;
}

std::string LACPDU::describe() const {
  return folly::to<std::string>(
      "version=",
      version,
      " actorInfo=",
      actorInfo.describe(),
      " partnerInfo=",
      partnerInfo.describe(),
      " maxDelay=",
      maxDelay);
}

LinkAggregationGroupID LinkAggregationGroupID::from(
    const ParticipantInfo& actorInfo,
    const ParticipantInfo& partnerInfo) {
  LinkAggregationGroupID lagID;

  lagID.actorSystemID = actorInfo.systemID;
  lagID.actorSystemPriority = actorInfo.systemPriority;
  lagID.actorKey = actorInfo.key;
  if (!(actorInfo.state & LacpState::AGGREGATABLE)) {
    lagID.actorPort = actorInfo.port;
    lagID.actorPortPriority = actorInfo.portPriority;
  }

  lagID.partnerSystemID = partnerInfo.systemID;
  lagID.partnerSystemPriority = partnerInfo.systemPriority;
  lagID.partnerKey = partnerInfo.key;
  if (!(partnerInfo.state & LacpState::AGGREGATABLE)) {
    lagID.partnerPort = partnerInfo.port;
    lagID.partnerPortPriority = partnerInfo.portPriority;
  }

  return lagID;
}

std::string LinkAggregationGroupID::describe() const {
  // TODO(samank): put smaller of S and T first
  std::string description;

  auto actorSystemMacAddr = folly::MacAddress::fromBinary(
      folly::ByteRange(actorSystemID.begin(), actorSystemID.end()));
  auto partnerSystemMacAddr = folly::MacAddress::fromBinary(
      folly::ByteRange(partnerSystemID.begin(), partnerSystemID.end()));

  // TODO(samank): write out as hex
  return folly::to<std::string>(
      "[(",
      actorSystemPriority,
      ",",
      actorSystemMacAddr.toString(),
      ",",
      actorKey,
      ",",
      actorPortPriority,
      ",",
      actorPort,
      "),(",
      partnerSystemPriority,
      ",",
      partnerSystemMacAddr.toString(),
      ",",
      partnerKey,
      ",",
      partnerPortPriority,
      ",",
      partnerPort,
      ")]");
}

bool LinkAggregationGroupID::operator==(
    const LinkAggregationGroupID& rhs) const {
  return rhs.actorSystemID == actorSystemID &&
      rhs.partnerSystemID == partnerSystemID &&
      rhs.actorSystemPriority == actorSystemPriority &&
      rhs.partnerSystemPriority == partnerSystemPriority &&
      rhs.actorKey == actorKey && rhs.partnerKey == partnerKey &&
      rhs.actorPort == actorPort && rhs.partnerPort == partnerPort &&
      rhs.actorPortPriority == actorPortPriority &&
      rhs.partnerPortPriority == partnerPortPriority;
}

} // namespace facebook::fboss
