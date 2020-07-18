/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/sai/api/SwitchApi.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"

#include <folly/logging/xlog.h>

#include <gtest/gtest.h>

using namespace facebook::fboss;

class SwitchApiTest : public ::testing::Test {
 public:
  void SetUp() override {
    fs = FakeSai::getInstance();
    sai_api_initialize(0, nullptr);
    switchApi = std::make_unique<SwitchApi>();
    switchId = SwitchSaiId{fs->switchManager.create(FakeSwitch())};
  }
  std::shared_ptr<FakeSai> fs;
  std::unique_ptr<SwitchApi> switchApi;
  SwitchSaiId switchId;
};

TEST_F(SwitchApiTest, setGetInit) {
  SaiSwitchTraits::Attributes::InitSwitch init{true};
  switchApi->setAttribute(switchId, init);
  SaiSwitchTraits::Attributes::InitSwitch blank{false};
  EXPECT_TRUE(switchApi->getAttribute(switchId, blank));
}

TEST_F(SwitchApiTest, getNumPorts) {
  SaiSwitchTraits::Attributes::PortNumber pn;
  // expect the one global cpu port
  EXPECT_EQ(switchApi->getAttribute(switchId, pn), 1);
  fs->portManager.create(FakePort{{0}, 100000});
  fs->portManager.create(FakePort{{1}, 25000});
  fs->portManager.create(FakePort{{2}, 25000});
  fs->portManager.create(FakePort{{3}, 25000});
  // expect 4 created ports plus global cpu port
  EXPECT_EQ(switchApi->getAttribute(switchId, pn), 5);
}

TEST_F(SwitchApiTest, setNumPorts) {
  SaiSwitchTraits::Attributes::PortNumber pn{100};
  EXPECT_THROW(switchApi->setAttribute(switchId, pn), SaiApiError);
}

TEST_F(SwitchApiTest, testGetPortIds) {
  fs->portManager.create(FakePort{{0}, 100000});
  fs->portManager.create(FakePort{{1}, 25000});
  fs->portManager.create(FakePort{{2}, 25000});
  fs->portManager.create(FakePort{{3}, 25000});
  SaiSwitchTraits::Attributes::PortNumber pn;
  auto numPorts = switchApi->getAttribute(
      switchId, SaiSwitchTraits::Attributes::PortNumber());
  std::vector<sai_object_id_t> v;
  v.resize(numPorts);
  SaiSwitchTraits::Attributes::PortList pl(v);
  auto portIds = switchApi->getAttribute(switchId, pl);
  EXPECT_EQ(portIds.size(), numPorts);
}

TEST_F(SwitchApiTest, setPortList) {
  SaiSwitchTraits::Attributes::PortList portList{
      std::vector<sai_object_id_t>{}};
  EXPECT_THROW(switchApi->setAttribute(switchId, portList), SaiApiError);
}

TEST_F(SwitchApiTest, testSetMac) {
  folly::MacAddress newSrcMac("DE:AD:BE:EF:42:42");
  SaiSwitchTraits::Attributes::SrcMac ma(newSrcMac);
  switchApi->setAttribute(switchId, ma);
  SaiSwitchTraits::Attributes::SrcMac blank;
  EXPECT_EQ(switchApi->getAttribute(switchId, blank), newSrcMac);
}

TEST_F(SwitchApiTest, testSetHwInfo) {
  SaiSwitchTraits::Attributes::HwInfo hw = std::vector<int8_t>(1, 41);
  switchApi->setAttribute(switchId, hw);
  SaiSwitchTraits::Attributes::HwInfo blank;
  auto hwGot = switchApi->getAttribute(switchId, blank);
  EXPECT_EQ(hwGot.size(), 1);
}
TEST_F(SwitchApiTest, getDefaultVlanId) {
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::DefaultVlanId()),
      0);
}

TEST_F(SwitchApiTest, setDefaultVlanId) {
  EXPECT_THROW(
      try {
        switchApi->setAttribute(
            switchId, SaiSwitchTraits::Attributes::DefaultVlanId(42));
      } catch (const SaiApiError& e) {
        EXPECT_EQ(e.getSaiStatus(), SAI_STATUS_INVALID_PARAMETER);
        throw;
      },
      SaiApiError);
}

TEST_F(SwitchApiTest, getCpuPort) {
  auto cpuPort =
      switchApi->getAttribute(switchId, SaiSwitchTraits::Attributes::CpuPort{});
  EXPECT_EQ(cpuPort, 0);
}

TEST_F(SwitchApiTest, setCpuPort) {
  SaiSwitchTraits::Attributes::CpuPort cpuPort{1};
  EXPECT_THROW(switchApi->setAttribute(switchId, cpuPort), SaiApiError);
}

TEST_F(SwitchApiTest, setGetShellEnable) {
  SaiSwitchTraits::Attributes::SwitchShellEnable shell{true};
  switchApi->setAttribute(switchId, shell);
  SaiSwitchTraits::Attributes::SwitchShellEnable blank{false};
  EXPECT_TRUE(switchApi->getAttribute(switchId, blank));
}

TEST_F(SwitchApiTest, getHashIds) {
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::EcmpHash{}),
      1234);
  EXPECT_EQ(
      switchApi->getAttribute(switchId, SaiSwitchTraits::Attributes::LagHash{}),
      1234);
}

TEST_F(SwitchApiTest, setHashIds) {
  SaiSwitchTraits::Attributes::EcmpHash ecmpHash{1};
  EXPECT_THROW(switchApi->setAttribute(switchId, ecmpHash), SaiApiError);
}

TEST_F(SwitchApiTest, setGetHashSeeds) {
  SaiSwitchTraits::Attributes::EcmpDefaultHashSeed ecmpSeed{42};
  switchApi->setAttribute(switchId, ecmpSeed);
  SaiSwitchTraits::Attributes::LagDefaultHashSeed lagSeed{24};
  switchApi->setAttribute(switchId, lagSeed);
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::EcmpDefaultHashSeed{}),
      42);
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::LagDefaultHashSeed{}),
      24);
}

TEST_F(SwitchApiTest, setGetHashAlgorithms) {
  SaiSwitchTraits::Attributes::EcmpDefaultHashAlgorithm ecmpAlgorithm{
      SAI_HASH_ALGORITHM_CRC_CCITT};
  switchApi->setAttribute(switchId, ecmpAlgorithm);
  SaiSwitchTraits::Attributes::LagDefaultHashAlgorithm lagAlgorithm{
      SAI_HASH_ALGORITHM_XOR};
  switchApi->setAttribute(switchId, lagAlgorithm);
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::EcmpDefaultHashAlgorithm{}),
      SAI_HASH_ALGORITHM_CRC_CCITT);
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::LagDefaultHashAlgorithm{}),
      SAI_HASH_ALGORITHM_XOR);
}

TEST_F(SwitchApiTest, setGetEcmpV4Hash) {
  SaiSwitchTraits::Attributes::EcmpHashV4 ecmpHash{42};
  switchApi->setAttribute(switchId, ecmpHash);
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::EcmpHashV4{}),
      42);
}

TEST_F(SwitchApiTest, setGetEcmpV6Hash) {
  SaiSwitchTraits::Attributes::EcmpHashV6 ecmpHash{42};
  switchApi->setAttribute(switchId, ecmpHash);
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::EcmpHashV6{}),
      42);
}

TEST_F(SwitchApiTest, setGetWarmRestart) {
  SaiSwitchTraits::Attributes::SwitchRestartWarm restartWarm{true};
  switchApi->setAttribute(switchId, restartWarm);
  SaiSwitchTraits::Attributes::SwitchRestartWarm blank{false};
  EXPECT_TRUE(switchApi->getAttribute(switchId, blank));
}

TEST_F(SwitchApiTest, setGetSetQosMaps) {
  SaiSwitchTraits::Attributes::QosDscpToTcMap dscpToTc{42};
  SaiSwitchTraits::Attributes::QosTcToQueueMap tcToQueue{43};
  switchApi->setAttribute(switchId, dscpToTc);
  switchApi->setAttribute(switchId, tcToQueue);
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::QosDscpToTcMap{}),
      42);
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::QosTcToQueueMap{}),
      43);
}

TEST_F(SwitchApiTest, getAclEntryMinimumPriority) {
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::AclEntryMinimumPriority()),
      0);
}

TEST_F(SwitchApiTest, setAclEntryMinimumPriority) {
  EXPECT_THROW(
      switchApi->setAttribute(
          switchId, SaiSwitchTraits::Attributes::AclEntryMinimumPriority(42)),
      SaiApiError);
}

TEST_F(SwitchApiTest, getAclEntryMaximumPriority) {
  EXPECT_NE(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::AclEntryMaximumPriority()),
      0);
}

TEST_F(SwitchApiTest, setAclEntryMaximumPriority) {
  EXPECT_THROW(
      switchApi->setAttribute(
          switchId, SaiSwitchTraits::Attributes::AclEntryMaximumPriority(42)),
      SaiApiError);
}

TEST_F(SwitchApiTest, setGetMacAgingTime) {
  switchApi->setAttribute(
      switchId, SaiSwitchTraits::Attributes::MacAgingTime{42});
  EXPECT_EQ(
      switchApi->getAttribute(
          switchId, SaiSwitchTraits::Attributes::MacAgingTime{}),
      42);
}
