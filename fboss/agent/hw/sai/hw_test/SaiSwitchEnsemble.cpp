/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/sai/hw_test/SaiSwitchEnsemble.h"

#include "fboss/agent/SetupThrift.h"
#include "fboss/agent/hw/sai/hw_test/SaiLinkStateToggler.h"
#include "fboss/agent/hw/sai/switch/SaiPortManager.h"

#include "fboss/agent/hw/sai/hw_test/SaiTestHandler.h"
#include "fboss/agent/hw/test/HwLinkStateToggler.h"
#include "fboss/agent/platforms/sai/SaiPlatformInit.h"

#include "fboss/agent/HwSwitch.h"
#include "fboss/agent/SwitchStats.h"

#include <folly/gen/Base.h>

#include <boost/container/flat_set.hpp>

#include <memory>
#include <thread>

DECLARE_int32(thrift_port);
DECLARE_bool(setup_thrift);
DECLARE_string(config);

namespace {
void initFlagDefaults(const std::map<std::string, std::string>& defaults) {
  for (auto item : defaults) {
    gflags::SetCommandLineOptionWithMode(
        item.first.c_str(), item.second.c_str(), gflags::SET_FLAGS_DEFAULT);
  }
}
} // namespace

namespace facebook::fboss {

SaiSwitchEnsemble::SaiSwitchEnsemble(uint32_t featuresDesired)
    : HwSwitchEnsemble(featuresDesired) {
  std::unique_ptr<AgentConfig> agentConfig;
  if (!FLAGS_config.empty()) {
    agentConfig = AgentConfig::fromFile(FLAGS_config);
  } else {
    agentConfig = AgentConfig::fromDefaultFile();
  }
  initFlagDefaults(agentConfig->thrift.defaultCommandLineArgs);
  auto platform = initSaiPlatform(std::move(agentConfig), featuresDesired);
  std::unique_ptr<HwLinkStateToggler> linkToggler;
  if (featuresDesired & HwSwitch::LINKSCAN_DESIRED) {
    linkToggler = std::make_unique<SaiLinkStateToggler>(
        static_cast<SaiSwitch*>(platform->getHwSwitch()),
        [this](const std::shared_ptr<SwitchState>& toApply) {
          applyNewState(toApply);
        },
        cfg::PortLoopbackMode::MAC);
  }
  std::unique_ptr<std::thread> thriftThread;
  if (FLAGS_setup_thrift) {
    thriftThread =
        createThriftThread(static_cast<SaiSwitch*>(platform->getHwSwitch()));
  }
  setupEnsemble(
      std::move(platform), std::move(linkToggler), std::move(thriftThread));
}

std::unique_ptr<std::thread> SaiSwitchEnsemble::createThriftThread(
    const SaiSwitch* hwSwitch) {
  return std::make_unique<std::thread>([hwSwitch] {
    auto handler = std::make_shared<SaiTestHandler>(hwSwitch);
    folly::EventBase eventBase;
    auto server = setupThriftServer(
        eventBase,
        handler,
        FLAGS_thrift_port,
        false /* isDuplex */,
        false /* setupSSL*/,
        true /* isStreaming */);
    // Run the EventBase main loop
    eventBase.loopForever();
  });
}

std::vector<PortID> SaiSwitchEnsemble::logicalPortIds() const {
  // TODO
  return {};
}

std::vector<PortID> SaiSwitchEnsemble::masterLogicalPortIds() const {
  return getPlatform()->masterLogicalPortIds();
}

std::vector<PortID> SaiSwitchEnsemble::getAllPortsInGroup(PortID portID) const {
  return getPlatform()->getAllPortsInGroup(portID);
}

std::vector<FlexPortMode> SaiSwitchEnsemble::getSupportedFlexPortModes() const {
  return getPlatform()->getSupportedFlexPortModes();
}

void SaiSwitchEnsemble::dumpHwCounters() const {
  // TODO once hw shell access is supported
}

std::map<PortID, HwPortStats> SaiSwitchEnsemble::getLatestPortStats(
    const std::vector<PortID>& ports) {
  SwitchStats dummy;
  getHwSwitch()->updateStats(&dummy);
  auto allPortStats =
      getHwSwitch()->managerTable()->portManager().getPortStats();
  boost::container::flat_set<PortID> portIds(ports.begin(), ports.end());
  return folly::gen::from(allPortStats) |
      folly::gen::filter([&portIds](const auto& portIdAndStat) {
           return portIds.find(portIdAndStat.first) != portIds.end();
         }) |
      folly::gen::as<std::map<PortID, HwPortStats>>();
}

void SaiSwitchEnsemble::stopHwCallLogging() const {
  // TODO - if we support cint style h/w call logging
}

} // namespace facebook::fboss
