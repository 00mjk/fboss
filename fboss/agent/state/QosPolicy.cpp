/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/state/QosPolicy.h"
#include <folly/Conv.h>
#include "fboss/agent/state/NodeBase-defs.h"
#include "fboss/agent/state/StateUtils.h"

namespace {
constexpr auto kQueueId = "queueId";
constexpr auto kDscp = "dscp";
constexpr auto kExp = "exp";
constexpr auto kRules = "rules";
constexpr auto kName = "name";
constexpr auto kTrafficClass = "trafficClass";
constexpr auto kDscpMap = "dscpMap";
constexpr auto kExpMap = "expMap";
constexpr auto kTrafficClassToQueueId = "trafficClassToQueueId";
constexpr auto kFrom = "from";
constexpr auto kTo = "to";
} // namespace

namespace facebook {
namespace fboss {

folly::dynamic QosRule::toFollyDynamic() const {
  folly::dynamic qosRule = folly::dynamic::object;
  qosRule[kQueueId] = queueId;
  qosRule[kDscp] = dscp;
  return qosRule;
}

QosRule QosRule::fromFollyDynamic(const folly::dynamic& qosRuleJson) {
  if (qosRuleJson.find(kQueueId) == qosRuleJson.items().end()) {
    throw FbossError("QosRule must have a queueId set");
  }
  if (qosRuleJson.find(kDscp) == qosRuleJson.items().end()) {
    throw FbossError("QosRule must have a dscp value");
  }
  return QosRule(qosRuleJson[kQueueId].asInt(), qosRuleJson[kDscp].asInt());
}

folly::dynamic QosPolicyFields::toFollyDynamic() const {
  folly::dynamic qosPolicy = folly::dynamic::object;
  qosPolicy[kName] = name;
  folly::dynamic qosPolicys = folly::dynamic::array;
  for (const auto& rule : rules) {
    qosPolicys.push_back(rule.toFollyDynamic());
  }
  qosPolicy[kRules] = qosPolicys;
  qosPolicy[kDscpMap] = dscpMap.toFollyDynamic();
  qosPolicy[kExpMap] = expMap.toFollyDynamic();
  qosPolicy[kTrafficClassToQueueId] = folly::dynamic::array;

  for (auto entry : trafficClassToQueueId) {
    folly::dynamic jsonEntry = folly::dynamic::object;
    jsonEntry[kTrafficClass] = static_cast<uint16_t>(entry.first);
    jsonEntry[kQueueId] = entry.second;
    qosPolicy[kTrafficClassToQueueId].push_back(jsonEntry);
  }
  return qosPolicy;
}

QosPolicyFields QosPolicyFields::fromFollyDynamic(const folly::dynamic& json) {
  auto name = json[kName].asString();
  std::set<QosRule> rules;
  for (const auto& rule : json[kRules]) {
    rules.emplace(QosRule::fromFollyDynamic(rule));
  }
  TrafficClassToQosAttributeMap<DSCP> dscpMap;
  TrafficClassToQosAttributeMap<EXP> expMap;
  TrafficClassToQueueId trafficClassToQueueId;

  if (json.find(kDscpMap) != json.items().end()) {
    dscpMap =
        TrafficClassToQosAttributeMap<DSCP>::fromFollyDynamic(json[kDscpMap]);
  }
  if (json.find(kExpMap) != json.items().end()) {
    expMap =
        TrafficClassToQosAttributeMap<EXP>::fromFollyDynamic(json[kExpMap]);
  }
  if (json.find(kTrafficClassToQueueId) != json.items().end()) {
    for (const auto& entry : json[kTrafficClassToQueueId]) {
      trafficClassToQueueId.emplace(
          static_cast<TrafficClass>(entry[kTrafficClass].asInt()),
          entry[kQueueId].asInt());
    }
  }
  return QosPolicyFields(
      name, rules, DscpMap(dscpMap), ExpMap(expMap), trafficClassToQueueId);
}

DscpMap::DscpMap(std::vector<cfg::DscpQosMap> cfg)
    : TrafficClassToQosAttributeMap<DSCP>() {
  for (auto map : cfg) {
    auto trafficClass = map.internalTrafficClass;

    for (auto dscp : map.fromDscpToTrafficClass) {
      addFromEntry(
          static_cast<TrafficClass>(trafficClass), static_cast<DSCP>(dscp));
    }
    if (auto fromTrafficClassToDscp = map.fromTrafficClassToDscp_ref()) {
      addToEntry(
          static_cast<TrafficClass>(trafficClass),
          static_cast<DSCP>(fromTrafficClassToDscp.value()));
    }
  }
}

ExpMap::ExpMap(std::vector<cfg::ExpQosMap> cfg)
    : TrafficClassToQosAttributeMap<EXP>() {
  for (auto map : cfg) {
    auto trafficClass = map.internalTrafficClass;

    for (auto dscp : map.fromExpToTrafficClass) {
      addFromEntry(
          static_cast<TrafficClass>(trafficClass), static_cast<EXP>(dscp));
    }
    if (auto fromTrafficClassToExp = map.fromTrafficClassToExp_ref()) {
      addToEntry(
          static_cast<TrafficClass>(trafficClass),
          static_cast<EXP>(fromTrafficClassToExp.value()));
    }
  }
}

template <>
folly::dynamic TrafficClassToQosAttributeMapEntry<DSCP>::toFollyDynamic()
    const {
  folly::dynamic object = folly::dynamic::object;
  object[kTrafficClass] = folly::to<std::string>(trafficClass_);
  object[kDscp] = folly::to<std::string>(attr_);
  return object;
}

template <>
folly::dynamic TrafficClassToQosAttributeMapEntry<EXP>::toFollyDynamic() const {
  folly::dynamic object = folly::dynamic::object;
  object[kTrafficClass] = folly::to<std::string>(trafficClass_);
  object[kExp] = folly::to<std::string>(attr_);
  return object;
}

template <>
TrafficClassToQosAttributeMapEntry<DSCP>
TrafficClassToQosAttributeMapEntry<DSCP>::fromFollyDynamic(
    folly::dynamic json) {
  if (json.find(kTrafficClass) == json.items().end() ||
      json.find(kDscp) == json.items().end()) {
    throw FbossError("");
  }

  return TrafficClassToQosAttributeMapEntry<DSCP>(
      static_cast<TrafficClass>(json[kTrafficClass].asInt()),
      static_cast<DSCP>(json[kDscp].asInt()));
}

template <>
TrafficClassToQosAttributeMapEntry<EXP>
TrafficClassToQosAttributeMapEntry<EXP>::fromFollyDynamic(folly::dynamic json) {
  if (json.find(kTrafficClass) == json.items().end() ||
      json.find(kExp) == json.items().end()) {
    throw FbossError("");
  }

  return TrafficClassToQosAttributeMapEntry<EXP>(
      static_cast<TrafficClass>(json[kTrafficClass].asInt()),
      static_cast<EXP>(json[kExp].asInt()));
}

template <typename QosAttrT>
folly::dynamic TrafficClassToQosAttributeMap<QosAttrT>::toFollyDynamic() const {
  folly::dynamic object = folly::dynamic::object;
  folly::dynamic entries = folly::dynamic::array;
  for (const auto& entry : from_) {
    entries.push_back(entry.toFollyDynamic());
  }
  object[kFrom] = entries;
  object[kTo] = to_ ? to_->toFollyDynamic() : nullptr;
  return object;
}

template <typename QosAttrT>
TrafficClassToQosAttributeMap<QosAttrT>
TrafficClassToQosAttributeMap<QosAttrT>::fromFollyDynamic(folly::dynamic json) {
  TrafficClassToQosAttributeMap<QosAttrT> map;
  for (const auto& entry : json[kFrom]) {
    map.from_.insert(
        TrafficClassToQosAttributeMapEntry<QosAttrT>::fromFollyDynamic(entry));
  }
  if (!json["to"].isNull()) {
    map.to_.emplace(
        TrafficClassToQosAttributeMapEntry<QosAttrT>::fromFollyDynamic(
            json[kTo]));
  }
  return map;
}

template class TrafficClassToQosAttributeMap<DSCP>;
template class TrafficClassToQosAttributeMap<EXP>;
template class NodeBaseT<QosPolicy, QosPolicyFields>;

} // namespace fboss
} // namespace facebook
