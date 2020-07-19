/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/ApplyThriftConfig.h"
#include "fboss/agent/packet/Ethertype.h"
#include "fboss/agent/state/AggregatePort.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/PortDescriptor.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/ResourceLibUtil.h"
#include "fboss/agent/test/TrunkUtils.h"

#include "fboss/agent/Platform.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/hw/test/HwLinkStateDependentTest.h"
#include "fboss/agent/hw/test/HwTestLearningUpdateObserver.h"
#include "fboss/agent/hw/test/HwTestMacUtils.h"
#include "fboss/agent/hw/test/HwTestPacketUtils.h"

#include "fboss/agent/hw/test/ConfigFactory.h"

#include <folly/IPAddress.h>
#include <folly/Optional.h>

#include <memory>
#include <set>
#include <vector>

using facebook::fboss::L2EntryThrift;
using folly::IPAddress;
using folly::IPAddressV4;
using folly::IPAddressV6;

// Even when running the same test repeatedly could result in different
// learning counts based on hash insertion  order.
// Maximum theortical is 8k for TH ..but practically we hit numbers below it
// Putting the value ot 7K should give enough buffer
int constexpr L2_LEARN_MAX_MAC_COUNT = 7000;

namespace {
std::set<folly::MacAddress>
getMacsForPort(const facebook::fboss::HwSwitch* hw, int port, bool isTrunk) {
  std::set<folly::MacAddress> macs;
  std::vector<L2EntryThrift> l2Entries;
  hw->fetchL2Table(&l2Entries);
  for (auto& l2Entry : l2Entries) {
    if ((isTrunk && l2Entry.trunk_ref().value_or({}) == port) ||
        *l2Entry.port_ref() == port) {
      macs.insert(folly::MacAddress(*l2Entry.mac_ref()));
    }
  }
  return macs;
}

} // namespace

namespace facebook::fboss {

using utility::addAggPort;
using utility::enableTrunkPorts;

class HwMacLearningTest : public HwLinkStateDependentTest {
 protected:
  void SetUp() override {
    HwLinkStateDependentTest::SetUp();
    l2LearningObserver_.startObserving(getHwSwitchEnsemble());
  }

  void TearDown() override {
    l2LearningObserver_.stopObserving();
  }

  cfg::SwitchConfig initialConfig() const override {
    return utility::oneL3IntfTwoPortConfig(
        getHwSwitch(),
        masterLogicalPortIds()[0],
        masterLogicalPortIds()[1],
        cfg::PortLoopbackMode::MAC);
  }

  MacAddress kSourceMac() const {
    return MacAddress("02:00:00:00:00:05");
  }

  void sendL2Pkts(int vlanId, int port) {
    auto originalStats =
        getHwSwitchEnsemble()->getLatestPortStats(masterLogicalPortIds());
    auto allSent = [port, &originalStats](const auto& newStats) {
      auto originalOut = getPortOutPkts(originalStats.at(PortID(port)));
      auto newOut = getPortOutPkts(newStats.at(PortID(port)));
      auto expectedOut = originalOut + L2_LEARN_MAX_MAC_COUNT;
      XLOGF(
          INFO,
          "Checking packets sent. Old: {}, New: {}, Expected: {}",
          originalOut,
          newOut,
          expectedOut);
      return newOut == expectedOut;
    };
    auto generator = utility::MacAddressGenerator();
    // start with fixed address and increment deterministically
    // evaluate total learnt l2 entries
    generator.startOver(0x200000005);
    for (auto i = 0; i < L2_LEARN_MAX_MAC_COUNT; ++i) {
      auto txPacket = utility::makeEthTxPacket(
          getHwSwitch(),
          facebook::fboss::VlanID(vlanId),
          generator.getNext(),
          folly::MacAddress::BROADCAST,
          facebook::fboss::ETHERTYPE::ETHERTYPE_LLDP);
      getHwSwitch()->sendPacketOutOfPortSync(
          std::move(txPacket), facebook::fboss::PortID(port));
    }
    getHwSwitchEnsemble()->waitPortStatsCondition(allSent);
  }

  void sendPkt() {
    auto txPacket = utility::makeEthTxPacket(
        getHwSwitch(),
        VlanID(*initialConfig().vlanPorts[0].vlanID_ref()),
        kSourceMac(),
        MacAddress::BROADCAST,
        ETHERTYPE::ETHERTYPE_LLDP);

    getHwSwitchEnsemble()->ensureSendPacketOutOfPort(
        std::move(txPacket), PortID(masterLogicalPortIds()[0]));
  }

  bool wasMacLearnt(PortDescriptor portDescr, bool shouldExist = true) const {
    /***
     * shouldExist - if set to true (default), retry until mac is found.
     *             - if set to false, retry until mac is no longer learned
     * @return true if the desired condition occurs before timeout, else false
     */
    auto l2LearningMode =
        getProgrammedState()->getSwitchSettings()->getL2LearningMode();

    /*
     * For HwMacLearningTest.VerifyHwAgingForPort:
     *  - On SDK 6.5.16, the test PASS'ed across several (100+) iterations.
     *  - On SDK 6.5.17, the test fails intermittently as at times, the L2
     *    entry is aged out, albeit, with delay.
     *
     *  CSP CS10327477 reports this regression to Broadcom. In the meantime,
     *  we bump up the retries to 10 (for all tests using this util function,
     *  and all devices).
     */

    int retries = 10;
    while (retries--) {
      if ((l2LearningMode == cfg::L2LearningMode::SOFTWARE &&
           wasMacLearntInSwitchState(shouldExist)) ||
          (l2LearningMode == cfg::L2LearningMode::HARDWARE &&
           wasMacLearntInHw(portDescr, shouldExist))) {
        return true;
      }

      // State udpate that will add/remove MacEntry happens asynchronously in
      // Event base. Give it chance to run.
      // Typically the MAC learning is immediate post a packet sent, but retries
      // help avoid test noise.
      sleep(1);
    }
    return false;
  }

  void verifyL2TableCallback(
      std::pair<L2Entry, L2EntryUpdateType> l2EntryAndUpdateType,
      PortDescriptor portDescr,
      L2EntryUpdateType expectedL2EntryUpdateType,
      L2Entry::L2EntryType expectedL2EntryType) {
    auto [l2Entry, l2EntryUpdateType] = l2EntryAndUpdateType;

    EXPECT_EQ(l2Entry.getMac(), kSourceMac());
    EXPECT_EQ(
        l2Entry.getVlanID(),
        VlanID(*initialConfig().vlanPorts[0].vlanID_ref()));
    EXPECT_EQ(l2Entry.getPort(), portDescr);
    EXPECT_EQ(l2Entry.getType(), expectedL2EntryType);
    EXPECT_EQ(l2EntryUpdateType, expectedL2EntryUpdateType);
  }
  void setL2LearningMode(cfg::L2LearningMode l2LearningMode) {
    if (getProgrammedState()->getSwitchSettings()->getL2LearningMode() ==
        l2LearningMode) {
      return;
    }
    auto newState = getProgrammedState()->clone();
    auto newSwitchSettings = newState->getSwitchSettings()->clone();
    newSwitchSettings->setL2LearningMode(l2LearningMode);
    newState->resetSwitchSettings(newSwitchSettings);
    applyNewState(newState);
  }
  void setupHelper(
      cfg::L2LearningMode l2LearningMode,
      PortDescriptor portDescr) {
    auto newCfg{initialConfig()};
    *newCfg.switchSettings_ref()->l2LearningMode_ref() = l2LearningMode;

    if (portDescr.isAggregatePort()) {
      *newCfg.ports[0].state_ref() = cfg::PortState::ENABLED;
      addAggPort(
          std::numeric_limits<AggregatePortID>::max(),
          {masterLogicalPortIds()[0]},
          &newCfg);
      auto state = applyNewConfig(newCfg);
      applyNewState(enableTrunkPorts(state));
    } else {
      applyNewConfig(newCfg);
    }
    bringDownPort(masterLogicalPortIds()[1]);
  }

  PortDescriptor physPortDescr() const {
    return PortDescriptor(PortID(masterLogicalPortIds()[0]));
  }

  PortDescriptor aggPortDescr() const {
    return PortDescriptor(
        AggregatePortID(std::numeric_limits<AggregatePortID>::max()));
  }

  int kMinAgeInSecs() {
    return 1;
  }

  L2Entry::L2EntryType expectedL2EntryTypeOnAdd() const {
    /*
     * TD2 and TH learn the entry as PENDING, TH3 learns the entry as VALIDATED
     */
    return (getAsic()->getAsicType() != HwAsic::AsicType::ASIC_TYPE_TOMAHAWK3)
        ? L2Entry::L2EntryType::L2_ENTRY_TYPE_PENDING
        : L2Entry::L2EntryType::L2_ENTRY_TYPE_VALIDATED;
  }

  void testHwLearningHelper(PortDescriptor portDescr) {
    auto setup = [this, portDescr]() {
      setupHelper(cfg::L2LearningMode::HARDWARE, portDescr);
      // Disable aging, so entry stays in L2 table when we verify.
      utility::setMacAgeTimerSeconds(getHwSwitch(), 0);
      sendPkt();
    };

    auto verify = [this, portDescr]() { EXPECT_TRUE(wasMacLearnt(portDescr)); };

    // MACs learned should be preserved across warm boot
    verifyAcrossWarmBoots(setup, verify);
  }

  void testHwAgingHelper(PortDescriptor portDescr) {
    auto setup = [this, portDescr]() {
      setupHelper(cfg::L2LearningMode::HARDWARE, portDescr);
    };

    auto verify = [this, portDescr]() {
      // Disable aging, so entry stays in L2 table when we verify.
      utility::setMacAgeTimerSeconds(getHwSwitch(), 0);
      sendPkt();
      EXPECT_TRUE(wasMacLearnt(portDescr));

      // Force MAC aging to as fast a possible but min is still 1 second
      utility::setMacAgeTimerSeconds(getHwSwitch(), kMinAgeInSecs());
      EXPECT_TRUE(wasMacLearnt(portDescr, false /* MAC aged */));
    };

    verifyAcrossWarmBoots(setup, verify);
  }

  void testSwLearningHelper(PortDescriptor portDescr) {
    auto setup = [this, portDescr]() {
      setupHelper(cfg::L2LearningMode::SOFTWARE, portDescr);
      // Disable aging, so entry stays in L2 table when we verify.
      utility::setMacAgeTimerSeconds(getHwSwitch(), 0);

      l2LearningObserver_.reset();
      sendPkt();

      verifyL2TableCallback(
          l2LearningObserver_.waitForLearningUpdates().front(),
          portDescr,
          L2EntryUpdateType::L2_ENTRY_UPDATE_TYPE_ADD,
          expectedL2EntryTypeOnAdd());
    };

    auto verify = [this, portDescr]() { EXPECT_TRUE(wasMacLearnt(portDescr)); };

    // MACs learned should be preserved across warm boot
    verifyAcrossWarmBoots(setup, verify);
  }

  void testSwAgingHelper(PortDescriptor portDescr) {
    auto setup = [this, portDescr]() {
      setupHelper(cfg::L2LearningMode::SOFTWARE, portDescr);
    };

    auto verify = [this, portDescr]() {
      // Disable aging, so entry stays in L2 table when we verify.
      utility::setMacAgeTimerSeconds(getHwSwitch(), 0);

      l2LearningObserver_.reset();
      sendPkt();

      /*
       * Verify if we get ADD (learn) callback for PENDING entry for TD2, TH
       * and VALIDATED entry for TH3.
       */
      verifyL2TableCallback(
          l2LearningObserver_.waitForLearningUpdates().front(),
          portDescr,
          L2EntryUpdateType::L2_ENTRY_UPDATE_TYPE_ADD,
          expectedL2EntryTypeOnAdd());
      EXPECT_TRUE(wasMacLearnt(portDescr));

      // Force MAC aging to as fast a possible but min is still 1 second
      l2LearningObserver_.reset();
      utility::setMacAgeTimerSeconds(getHwSwitch(), kMinAgeInSecs());

      // Verify if we get DELETE (aging) callback for VALIDATED entry
      verifyL2TableCallback(
          l2LearningObserver_.waitForLearningUpdates().front(),
          portDescr,
          L2EntryUpdateType::L2_ENTRY_UPDATE_TYPE_DELETE,
          L2Entry::L2EntryType::L2_ENTRY_TYPE_VALIDATED);
      EXPECT_TRUE(wasMacLearnt(portDescr, false /* MAC aged */));
    };

    verifyAcrossWarmBoots(setup, verify);
  }

  void testHwToSwLearningHelper(PortDescriptor portDescr) {
    auto setup = [this, portDescr]() {
      setupHelper(cfg::L2LearningMode::HARDWARE, portDescr);

      // Disable aging, so entry stays in L2 table when we verify.
      utility::setMacAgeTimerSeconds(getHwSwitch(), 0);
      sendPkt();
    };

    auto verify = [this, portDescr]() { EXPECT_TRUE(wasMacLearnt(portDescr)); };

    auto setupPostWarmboot = [this, portDescr]() {
      l2LearningObserver_.reset();
      setL2LearningMode(cfg::L2LearningMode::SOFTWARE);
    };

    auto verifyPostWarmboot = [this, portDescr]() {
      verifyL2TableCallback(
          l2LearningObserver_.waitForLearningUpdates().front(),
          portDescr,
          L2EntryUpdateType::L2_ENTRY_UPDATE_TYPE_ADD,
          L2Entry::L2EntryType::L2_ENTRY_TYPE_VALIDATED);
      EXPECT_TRUE(wasMacLearnt(portDescr));
    };

    verifyAcrossWarmBoots(setup, verify, setupPostWarmboot, verifyPostWarmboot);
  }

  void testSwToHwLearningHelper(PortDescriptor portDescr) {
    auto setup = [this, portDescr]() {
      setupHelper(cfg::L2LearningMode::SOFTWARE, portDescr);
      // Disable aging, so entry stays in L2 table when we verify.
      utility::setMacAgeTimerSeconds(getHwSwitch(), 0);

      l2LearningObserver_.reset();
      sendPkt();

      verifyL2TableCallback(
          l2LearningObserver_.waitForLearningUpdates().front(),
          portDescr,
          L2EntryUpdateType::L2_ENTRY_UPDATE_TYPE_ADD,
          expectedL2EntryTypeOnAdd());
    };

    auto verify = [this, portDescr]() { EXPECT_TRUE(wasMacLearnt(portDescr)); };

    auto setupPostWarmboot = [this, portDescr]() {
      setupHelper(cfg::L2LearningMode::HARDWARE, portDescr);
    };

    auto verifyPostWarmboot = [this, portDescr]() {
      /*
       * We only maintain MacTable in the SwitchState in SOFTWARE
       * l2LearningMode.
       *
       * Thus, when we transition from SOFTWRE l2LearningMode to HARDWARE
       * l2Learning:
       * - BCM layer traverses l2Table and calls deleteCb for every entry.
       * - The deleteCb processing removes l2 entries from the switch state.
       * - However, this causes subsequent state update to
       *   'processMacTableChanges' and remove L2 entries programmed in ASIC.
       *
       * If the traffic is flowing, the L2 entries would be immediately
       * relearned (by HARDWARE learning).
       *
       * We could modify processMacTableChanges to omit processing of updates
       * when l2LearningMode is HARDWARE. But, for cleaner design, we chose to
       * maintain the abstraction of HwSwitch just applying switch states
       * passed down to it.
       *
       * Thus, here we ASSERT that the MAC is removed.
       */
      EXPECT_TRUE(wasMacLearnt(portDescr, false /* MAC aged */));
    };

    verifyAcrossWarmBoots(setup, verify, setupPostWarmboot, verifyPostWarmboot);
  }

  HwTestLearningUpdateObserver l2LearningObserver_;

 private:
  bool wasMacLearntInHw(PortDescriptor portDescr, bool shouldExist) const {
    auto isTrunk = portDescr.isAggregatePort();
    int portId = isTrunk ? portDescr.aggPortID() : portDescr.phyPortID();
    auto macs = getMacsForPort(getHwSwitch(), portId, isTrunk);

    return (shouldExist == (macs.find(kSourceMac()) != macs.end()));
  }

  bool wasMacLearntInSwitchState(bool shouldExist) const {
    auto vlanID = VlanID(*initialConfig().vlanPorts[0].vlanID_ref());
    auto state = getProgrammedState();
    auto vlan = state->getVlans()->getVlanIf(vlanID);
    auto* macTable = vlan->getMacTable().get();

    return (shouldExist == (macTable->getNodeIf(kSourceMac()) != nullptr));
  }
};

// Intent of this test is to attempt to learn large number of macs
// (L2_LEARN_MAX_MAC_COUNT) and ensure HW can learn them.
TEST_F(HwMacLearningTest, VerifyMacLearningScale) {
  if (getAsic()->getAsicType() == HwAsic::AsicType::ASIC_TYPE_TOMAHAWK3) {
    // this test is not valid for TH3 as chip supports SW based learning only
    // which is much slower to learn for a scaled test. Also SW introduces
    // variability in results .
    XLOG(INFO) << "Skip the test for TH3 platform";
    return;
  }

  auto portDescr = physPortDescr();
  auto setup = [this, portDescr]() {
    setupHelper(cfg::L2LearningMode::HARDWARE, portDescr);
    // Disable aging, so entry stays in L2 table when we verify.
    utility::setMacAgeTimerSeconds(getHwSwitch(), 0);
    sendL2Pkts(
        *initialConfig().vlanPorts[0].vlanID_ref(), masterLogicalPortIds()[0]);
  };

  auto verify = [this, portDescr]() {
    auto isTrunk = portDescr.isAggregatePort();
    int portId = isTrunk ? portDescr.aggPortID() : portDescr.phyPortID();
    auto macs = getMacsForPort(getHwSwitch(), portId, isTrunk);
    XLOG(INFO) << "Number of l2 entries learnt: " << macs.size();
    EXPECT_EQ(macs.size(), L2_LEARN_MAX_MAC_COUNT);
  };

  // MACs learned should be preserved across warm boot
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(HwMacLearningTest, VerifyHwLearningForPort) {
  testHwLearningHelper(physPortDescr());
}

TEST_F(HwMacLearningTest, VerifyHwLearningForTrunk) {
  testHwLearningHelper(aggPortDescr());
}

TEST_F(HwMacLearningTest, VerifyHwAgingForPort) {
  testHwAgingHelper(physPortDescr());
}

TEST_F(HwMacLearningTest, VerifyHwAgingForTrunk) {
  testHwAgingHelper(aggPortDescr());
}

TEST_F(HwMacLearningTest, VerifySwLearningForPort) {
  testSwLearningHelper(physPortDescr());
}

TEST_F(HwMacLearningTest, VerifySwLearningForTrunk) {
  testSwLearningHelper(aggPortDescr());
}

TEST_F(HwMacLearningTest, VerifySwAgingForPort) {
  testSwAgingHelper(physPortDescr());
}

TEST_F(HwMacLearningTest, VerifySwAgingForTrunk) {
  testSwAgingHelper(aggPortDescr());
}

TEST_F(HwMacLearningTest, VerifyHwToSwLearningForPort) {
  testHwToSwLearningHelper(physPortDescr());
}

TEST_F(HwMacLearningTest, VerifySwToHwLearningForPort) {
  testSwToHwLearningHelper(physPortDescr());
}

class HwMacLearningMacMoveTest : public HwMacLearningTest {
 protected:
  cfg::SwitchConfig initialConfig() const override {
    return utility::oneL3IntfTwoPortConfig(
        getHwSwitch(),
        masterLogicalPortIds()[0],
        masterLogicalPortIds()[1],
        cfg::PortLoopbackMode::MAC);
  }

  void sendPkt2() {
    auto txPacket = utility::makeEthTxPacket(
        getHwSwitch(),
        VlanID(*initialConfig().vlanPorts[0].vlanID_ref()),
        kSourceMac(),
        MacAddress::BROADCAST,
        ETHERTYPE::ETHERTYPE_LLDP);

    getHwSwitchEnsemble()->ensureSendPacketOutOfPort(
        std::move(txPacket), PortID(masterLogicalPortIds()[1]));
  }

  PortDescriptor physPortDescr2() const {
    return PortDescriptor(PortID(masterLogicalPortIds()[1]));
  }

  void testMacMoveHelper() {
    auto setup = [this]() {
      setupHelper(cfg::L2LearningMode::SOFTWARE, physPortDescr());
    };

    auto verify = [this]() {
      auto portDescr = physPortDescr();
      auto portDescr2 = physPortDescr2();

      // One port up, other down
      bringUpPort(portDescr.phyPortID());
      bringDownPort(portDescr2.phyPortID());

      // Disable aging, so entry stays in L2 table when we verify.
      utility::setMacAgeTimerSeconds(getHwSwitch(), 0);

      XLOG(DBG2) << "Send pkt on up port, other port is down";
      l2LearningObserver_.reset();
      sendPkt();

      /*
       * Verify if we get ADD (learn) callback for PENDING entry for TD2, TH
       * and VALIDATED entry for TH3.
       */
      verifyL2TableCallback(
          l2LearningObserver_.waitForLearningUpdates().front(),
          portDescr,
          L2EntryUpdateType::L2_ENTRY_UPDATE_TYPE_ADD,
          expectedL2EntryTypeOnAdd());
      EXPECT_TRUE(wasMacLearnt(portDescr));

      // Bring up port down, and down port up
      bringDownPort(portDescr.phyPortID());
      bringUpPort(portDescr2.phyPortID());

      XLOG(DBG2)
          << "Trigger MAC Move: Bring up port down, down port up, and send pkt";
      l2LearningObserver_.reset();
      sendPkt2();

      // When MAC Moves from port1 to port2, we get DELETE on port1 and ADD on
      // port2
      auto l2EntryAndUpdateTypeList =
          l2LearningObserver_.waitForLearningUpdates(2);
      verifyL2TableCallback(
          l2EntryAndUpdateTypeList.at(0),
          portDescr,
          L2EntryUpdateType::L2_ENTRY_UPDATE_TYPE_DELETE,
          L2Entry::L2EntryType::L2_ENTRY_TYPE_VALIDATED);
      verifyL2TableCallback(
          l2EntryAndUpdateTypeList.at(1),
          portDescr2,
          L2EntryUpdateType::L2_ENTRY_UPDATE_TYPE_ADD,
          L2Entry::L2EntryType::L2_ENTRY_TYPE_VALIDATED);

      EXPECT_TRUE(wasMacLearnt(portDescr2));

      // Aging out MAC prepares for subsequent run of verify()

      // Force MAC aging to as fast a possible but min is still 1 second
      l2LearningObserver_.reset();
      utility::setMacAgeTimerSeconds(getHwSwitch(), kMinAgeInSecs());

      // Verify if we get DELETE (aging) callback for VALIDATED entry
      verifyL2TableCallback(
          l2LearningObserver_.waitForLearningUpdates().front(),
          portDescr2,
          L2EntryUpdateType::L2_ENTRY_UPDATE_TYPE_DELETE,
          L2Entry::L2EntryType::L2_ENTRY_TYPE_VALIDATED);
      EXPECT_TRUE(wasMacLearnt(portDescr2, false /* MAC aged */));
    };

    // MAC Move should work as expected post warmboot as well.
    verifyAcrossWarmBoots(setup, verify);
  }
};

TEST_F(HwMacLearningMacMoveTest, VerifyMacMoveForPort) {
  testMacMoveHelper();
}

} // namespace facebook::fboss
