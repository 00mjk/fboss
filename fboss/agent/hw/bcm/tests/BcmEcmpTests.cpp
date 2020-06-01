/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/bcm/tests/BcmLinkStateDependentTests.h"
#include "fboss/agent/hw/bcm/tests/BcmTest.h"

#include "fboss/agent/ApplyThriftConfig.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/bcm/BcmEcmpUtils.h"
#include "fboss/agent/hw/bcm/BcmError.h"
#include "fboss/agent/hw/bcm/BcmHost.h"
#include "fboss/agent/hw/bcm/BcmMultiPathNextHop.h"
#include "fboss/agent/hw/bcm/BcmRoute.h"
#include "fboss/agent/hw/bcm/BcmSwitch.h"
#include "fboss/agent/hw/bcm/BcmTrunkTable.h"
#include "fboss/agent/platforms/tests/utils/BcmTestPlatform.h"
#include "fboss/agent/test/EcmpSetupHelper.h"

#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/hw/test/HwTestEcmpUtils.h"

#include <folly/IPAddress.h>

#include <boost/container/flat_set.hpp>

#include <memory>
#include <numeric>
#include <set>

extern "C" {
#include <bcm/l3.h>
#include <bcm/port.h>
}

DECLARE_uint32(ecmp_width);

using boost::container::flat_set;
using facebook::fboss::utility::getEcmpGroupInHw;
using facebook::fboss::utility::getEcmpSizeInHw;
using folly::IPAddress;
using folly::IPAddressV4;
using folly::IPAddressV6;

namespace {
facebook::fboss::RoutePrefixV6 kDefaultRoute{IPAddressV6(), 0};
folly::CIDRNetwork kDefaultRoutePrefix{folly::IPAddress("::"), 0};
}
namespace facebook::fboss {

class BcmEcmpTest : public BcmLinkStateDependentTests {
 public:
 protected:
  const RouterID kRid{0};
  constexpr static uint8_t numNextHops_{8};
  std::unique_ptr<utility::EcmpSetupAnyNPorts<folly::IPAddressV6>> ecmpHelper_;
  std::vector<NextHopWeight> swSwitchWeights_ = {ECMP_WEIGHT,
                                                 ECMP_WEIGHT,
                                                 ECMP_WEIGHT,
                                                 ECMP_WEIGHT,
                                                 ECMP_WEIGHT,
                                                 ECMP_WEIGHT,
                                                 ECMP_WEIGHT,
                                                 ECMP_WEIGHT};
  std::vector<NextHopWeight> hwSwitchWeights_ = {UCMP_DEFAULT_WEIGHT,
                                                 UCMP_DEFAULT_WEIGHT,
                                                 UCMP_DEFAULT_WEIGHT,
                                                 UCMP_DEFAULT_WEIGHT,
                                                 UCMP_DEFAULT_WEIGHT,
                                                 UCMP_DEFAULT_WEIGHT,
                                                 UCMP_DEFAULT_WEIGHT,
                                                 UCMP_DEFAULT_WEIGHT};
  void SetUp() override;
  cfg::SwitchConfig initialConfig() const override;

  void runSimpleTest(
      const std::vector<NextHopWeight>& swWs,
      const std::vector<NextHopWeight>& hwWs,
      // TODO: Fix warm boot for ECMP and enable warmboot for these tests -
      // T29840275
      bool warmboot = false);
  void runVaryOneNextHopFromHundredTest(
      size_t routeNumNextHops,
      NextHopWeight value,
      const std::vector<NextHopWeight>& hwWs);
  void resolveNhops(int numNhops);
  void resolveNhops(const std::vector<PortDescriptor>& portDescs);
  void programRouteWithUnresolvedNhops(size_t numRouteNextHops = 0);
  const BcmEcmpEgress* getEcmpEgress() const;
  const BcmMultiPathNextHop* getBcmMultiPathNextHop() const;
};
void BcmEcmpTest::SetUp() {
  BcmLinkStateDependentTests::SetUp();
  ecmpHelper_ = std::make_unique<utility::EcmpSetupAnyNPorts6>(
      getProgrammedState(), kRid);
}

cfg::SwitchConfig BcmEcmpTest::initialConfig() const {
  std::vector<PortID> ports;
  for (uint8_t i = 0; i < numNextHops_; ++i) {
    ports.push_back(masterLogicalPortIds()[i]);
  }
  return utility::oneL3IntfNPortConfig(
      getHwSwitch(), ports, cfg::PortLoopbackMode::MAC);
}

void BcmEcmpTest::resolveNhops(int numNhops) {
  applyNewState(ecmpHelper_->resolveNextHops(getProgrammedState(), numNhops));
}

void BcmEcmpTest::resolveNhops(const std::vector<PortDescriptor>& portDescs) {
  applyNewState(ecmpHelper_->resolveNextHops(
      getProgrammedState(),
      flat_set<PortDescriptor>(portDescs.begin(), portDescs.end())));
}

void BcmEcmpTest::programRouteWithUnresolvedNhops(size_t numRouteNextHops) {
  if (!numRouteNextHops) {
    numRouteNextHops = numNextHops_;
  }
  applyNewState(ecmpHelper_->setupECMPForwarding(
      getProgrammedState(),
      numRouteNextHops,
      {kDefaultRoute},
      std::vector<NextHopWeight>(
          swSwitchWeights_.begin(),
          swSwitchWeights_.begin() + numRouteNextHops)));
}

void BcmEcmpTest::runSimpleTest(
    const std::vector<NextHopWeight>& swWs,
    const std::vector<NextHopWeight>& hwWs,
    bool warmboot) {
  ASSERT_EQ(swWs.size(), hwWs.size());
  ASSERT_LE(swWs.size(), numNextHops_);
  auto setup = [=]() {
    for (uint8_t i = 0; i < swWs.size(); ++i) {
      swSwitchWeights_[i] = swWs[i];
      hwSwitchWeights_[i] = hwWs[i];
    }
    programRouteWithUnresolvedNhops(swWs.size());
    resolveNhops(swWs.size());
  };
  auto verify = [=]() {
    auto pathsInHw = utility::getEcmpMembersInHw(
        getHwSwitch(), kDefaultRoutePrefix, kRid, FLAGS_ecmp_width);
    std::set<uint64_t> uniquePaths(pathsInHw.begin(), pathsInHw.end());
    // This check assumes that egress ids grow as you add more egresses
    // That assumption could prove incorrect, in which case we would
    // need to map ips to egresses, somehow.
    auto expectedCountIter = hwWs.begin();
    for (const auto& path : uniquePaths) {
      auto count = pathsInHw.count(path);
      ASSERT_EQ(count, *expectedCountIter);
      expectedCountIter++;
    }
    auto totalHwWeight =
        std::accumulate(hwWs.begin(), hwWs.end(), NextHopWeight(0));
    auto pathsInHwCount = pathsInHw.size();
    ASSERT_EQ(totalHwWeight, pathsInHwCount);
    ASSERT_LE(pathsInHwCount, FLAGS_ecmp_width);
  };
  if (warmboot) {
    verifyAcrossWarmBoots(setup, verify);
  } else {
    setup();
    verify();
  }
}

void BcmEcmpTest::runVaryOneNextHopFromHundredTest(
    size_t routeNumNextHops,
    NextHopWeight value,
    const std::vector<NextHopWeight>& hwWs) {
  std::vector<NextHopWeight> swWs;
  for (size_t i = 0; i < routeNumNextHops - 1; ++i) {
    swWs.push_back(100);
  }
  swWs.push_back(value);
  runSimpleTest(swWs, hwWs);
}

const BcmMultiPathNextHop* BcmEcmpTest::getBcmMultiPathNextHop() const {
  auto routeTable = getProgrammedState()->getRouteTables()->getRouteTable(kRid);
  auto resolvedRoute = routeTable->getRibV6()->exactMatch(kDefaultRoute);
  const auto multiPathTable = getHwSwitch()->getMultiPathNextHopTable();
  RouteNextHopSet nhops;
  std::unordered_map<IPAddress, NextHopWeight> ws;
  for (uint8_t i = 0; i < numNextHops_; ++i) {
    ws[ecmpHelper_->ip(i)] = hwSwitchWeights_[i];
  }
  for (const auto& nhop : resolvedRoute->getForwardInfo().getNextHopSet()) {
    nhops.insert(ResolvedNextHop(nhop.addr(), nhop.intf(), ws[nhop.addr()]));
  }
  return multiPathTable->getNextHop(BcmMultiPathNextHopKey(kRid, nhops));
}

const BcmEcmpEgress* BcmEcmpTest::getEcmpEgress() const {
  return getBcmMultiPathNextHop()->getEgress();
}

TEST_F(BcmEcmpTest, SearchMissingEgressInECMP) {
  auto setup = [=]() { programRouteWithUnresolvedNhops(); };
  auto verify = [=]() {
    bcm_l3_egress_ecmp_t ecmpObj;
    bcm_l3_egress_ecmp_t_init(&ecmpObj);
    auto ecmpEgress = getEcmpEgress();
    auto egressIdsInSw = ecmpEgress->paths();
    ASSERT_EQ(numNextHops_, egressIdsInSw.size());
    ecmpObj.ecmp_intf = ecmpEgress->getID();
    for (const auto& egressId : egressIdsInSw) {
      ASSERT_EQ(
          BCM_E_NOT_FOUND,
          bcm_l3_egress_ecmp_delete(getUnit(), &ecmpObj, egressId));
    }
    auto pathsInHwCount =
        getEcmpSizeInHw(getUnit(), ecmpEgress->getID(), egressIdsInSw.size());
    ASSERT_EQ(0, pathsInHwCount);
  };
  verifyAcrossWarmBoots(setup, verify);
}

// Test what happens when totalWeight > 64 in UCMP and some of the
// weights are too low, resulting in them going to zero when
// multiplied by 64/W (where W is the total weight of the nexthops).
// TODO(borisb): Think of a better algorithm for this case than wi*(64/W)
TEST_F(BcmEcmpTest, UcmpOverflowZero) {
  runSimpleTest({50, 50, 1, 1}, {31, 31, 1, 1});
}
TEST_F(BcmEcmpTest, UcmpOverflowZeroNotEnoughToRoundUp) {
  runSimpleTest({50, 50, 1, 1, 1, 1, 1, 1}, {29, 29, 1, 1, 1, 1, 1, 1});
}

// Test link down in UCMP scenario
TEST_F(BcmEcmpTest, L2ResolveAllNhopsInUcmpThenLinkDown) {
  runSimpleTest({3, 1, 1, 1, 1, 1, 1, 1}, {3, 1, 1, 1, 1, 1, 1, 1}, false);
  bringDownPort(ecmpHelper_->nhop(0).portDesc.phyPortID());
  auto pathsInHwCount = utility::getEcmpSizeInHw(
      getHwSwitch(), kDefaultRoutePrefix, kRid, FLAGS_ecmp_width);
  ASSERT_EQ(7, pathsInHwCount);
}

// Test link flap in UCMP scenario
TEST_F(BcmEcmpTest, L2ResolveBothNhopsInUcmpThenLinkFlap) {
  runSimpleTest({3, 1, 1, 1, 1, 1, 1, 1}, {3, 1, 1, 1, 1, 1, 1, 1}, false);
  auto nhop = ecmpHelper_->nhop(0);
  bringDownPort(nhop.portDesc.phyPortID());
  auto pathsInHwCount = utility::getEcmpSizeInHw(
      getHwSwitch(), kDefaultRoutePrefix, kRid, FLAGS_ecmp_width);
  ASSERT_EQ(7, pathsInHwCount);
  bringUpPort(nhop.portDesc.phyPortID());
  pathsInHwCount = utility::getEcmpSizeInHw(
      getHwSwitch(), kDefaultRoutePrefix, kRid, FLAGS_ecmp_width);
  ASSERT_EQ(7, pathsInHwCount);
  resolveNhops(1);
  pathsInHwCount = utility::getEcmpSizeInHw(
      getHwSwitch(), kDefaultRoutePrefix, kRid, FLAGS_ecmp_width);
  ASSERT_EQ(10, pathsInHwCount);
}

// Tests for some simple cases we expect to see with the lbw community

TEST_F(BcmEcmpTest, FourLinksHundred) {
  runVaryOneNextHopFromHundredTest(4, 100, {1, 1, 1, 1});
}
TEST_F(BcmEcmpTest, EightLinksHundred) {
  runVaryOneNextHopFromHundredTest(8, 100, {1, 1, 1, 1, 1, 1, 1, 1});
}

TEST_F(BcmEcmpTest, FourLinksNinety) {
  runVaryOneNextHopFromHundredTest(4, 90, {10, 10, 10, 9});
}
TEST_F(BcmEcmpTest, EightLinksNinety) {
  runVaryOneNextHopFromHundredTest(8, 90, {8, 8, 8, 8, 8, 8, 8, 7});
}

TEST_F(BcmEcmpTest, FourLinksEighty) {
  runVaryOneNextHopFromHundredTest(4, 80, {5, 5, 5, 4});
}
TEST_F(BcmEcmpTest, EightLinksEighty) {
  runVaryOneNextHopFromHundredTest(8, 80, {5, 5, 5, 5, 5, 5, 5, 4});
}

TEST_F(BcmEcmpTest, FourLinksSeventy) {
  runVaryOneNextHopFromHundredTest(4, 70, {10, 10, 10, 7});
}
TEST_F(BcmEcmpTest, EightLinksSeventy) {
  runVaryOneNextHopFromHundredTest(8, 70, {8, 8, 8, 8, 8, 8, 8, 5});
}

TEST_F(BcmEcmpTest, FourLinksSixty) {
  runVaryOneNextHopFromHundredTest(4, 60, {5, 5, 5, 3});
}
TEST_F(BcmEcmpTest, EightLinksSixty) {
  runVaryOneNextHopFromHundredTest(8, 60, {5, 5, 5, 5, 5, 5, 5, 3});
}

TEST_F(BcmEcmpTest, FourLinksFifty) {
  runVaryOneNextHopFromHundredTest(4, 50, {2, 2, 2, 1});
}
TEST_F(BcmEcmpTest, EightLinksFifty) {
  runVaryOneNextHopFromHundredTest(8, 50, {2, 2, 2, 2, 2, 2, 2, 1});
}

TEST_F(BcmEcmpTest, FourLinksForty) {
  runVaryOneNextHopFromHundredTest(4, 40, {5, 5, 5, 2});
}
TEST_F(BcmEcmpTest, EightLinksForty) {
  runVaryOneNextHopFromHundredTest(8, 40, {5, 5, 5, 5, 5, 5, 5, 2});
}

TEST_F(BcmEcmpTest, FourLinksThirty) {
  runVaryOneNextHopFromHundredTest(4, 30, {10, 10, 10, 3});
}
TEST_F(BcmEcmpTest, EightLinksThirty) {
  runVaryOneNextHopFromHundredTest(8, 30, {8, 8, 8, 8, 8, 8, 8, 2});
}

TEST_F(BcmEcmpTest, FourLinksTwenty) {
  runVaryOneNextHopFromHundredTest(4, 20, {5, 5, 5, 1});
}
TEST_F(BcmEcmpTest, EightLinksTwenty) {
  runVaryOneNextHopFromHundredTest(8, 20, {5, 5, 5, 5, 5, 5, 5, 1});
}

TEST_F(BcmEcmpTest, FourLinksTen) {
  runVaryOneNextHopFromHundredTest(4, 10, {10, 10, 10, 1});
}
TEST_F(BcmEcmpTest, EightLinksTen) {
  runVaryOneNextHopFromHundredTest(8, 10, {9, 9, 9, 9, 9, 9, 9, 1});
}

TEST_F(BcmEcmpTest, ResolvePendingResolveNexthop) {
  auto setup = [=]() {
    resolveNhops(2);
    std::map<PortDescriptor, std::shared_ptr<NdpEntry>> entries;

    // mark neighbors connected over ports pending
    auto state0 = getProgrammedState();
    for (auto i = 0; i < 2; i++) {
      const auto& ecmpNextHop = ecmpHelper_->nhop(i);
      auto port = ecmpNextHop.portDesc;
      auto vlanId = ecmpHelper_->getVlan(port);
      auto ntable = state0->getVlans()->getVlan(*vlanId)->getNdpTable()->modify(
          *vlanId, &state0);
      auto entry = ntable->getEntry(ecmpNextHop.ip);
      auto intfId = entry->getIntfID();
      ntable->removeEntry(ecmpNextHop.ip);
      ntable->addPendingEntry(ecmpNextHop.ip, intfId);
      entries[port] = std::move(entry);
    }
    applyNewState(state0);

    // mark neighbors connected over ports reachable
    auto state1 = getProgrammedState();
    for (auto i = 0; i < 2; i++) {
      const auto& ecmpNextHop = ecmpHelper_->nhop(i);
      auto port = ecmpNextHop.portDesc;
      auto vlanId = ecmpHelper_->getVlan(port);
      auto ntable = state1->getVlans()->getVlan(*vlanId)->getNdpTable()->modify(
          *vlanId, &state1);

      auto entry = entries[port];
      auto* fields = entry->getFields();
      ntable->updateEntry(*fields);
    }
    applyNewState(state1);
    applyNewState(ecmpHelper_->setupECMPForwarding(getProgrammedState(), 2));
  };
  auto verify = [=]() {
    /* route is programmed */
    auto* bcmRoute = getHwSwitch()->routeTable()->getBcmRoute(
        0, kDefaultRoute.network, kDefaultRoute.mask);
    EXPECT_NE(bcmRoute, nullptr);
    auto egressId = bcmRoute->getEgressId();
    EXPECT_NE(egressId, BcmEgressBase::INVALID);

    /* ecmp is resolved */
    EXPECT_EQ(getEcmpSizeInHw(getHwSwitch()->getUnit(), egressId, 2), 2);
  };
  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
