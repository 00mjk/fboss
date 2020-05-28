/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/switch/SaiSwitch.h"

#include "fboss/agent/Constants.h"
#include "fboss/agent/Utils.h"
#include "fboss/agent/hw/sai/api/AdapterKeySerializers.h"
#include "fboss/agent/hw/sai/api/FdbApi.h"
#include "fboss/agent/hw/sai/api/HostifApi.h"
#include "fboss/agent/hw/sai/api/LoggingUtil.h"
#include "fboss/agent/hw/sai/api/SaiApiTable.h"
#include "fboss/agent/hw/sai/api/SaiObjectApi.h"
#include "fboss/agent/hw/sai/api/Types.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/ConcurrentIndices.h"
#include "fboss/agent/hw/sai/switch/SaiAclTableManager.h"
#include "fboss/agent/hw/sai/switch/SaiHashManager.h"
#include "fboss/agent/hw/sai/switch/SaiHostifManager.h"
#include "fboss/agent/hw/sai/switch/SaiInSegEntryManager.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiNeighborManager.h"
#include "fboss/agent/hw/sai/switch/SaiPortManager.h"
#include "fboss/agent/hw/sai/switch/SaiRouteManager.h"
#include "fboss/agent/hw/sai/switch/SaiRouterInterfaceManager.h"
#include "fboss/agent/hw/sai/switch/SaiRxPacket.h"
#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"
#include "fboss/agent/hw/sai/switch/SaiTxPacket.h"
#include "fboss/agent/hw/sai/switch/SaiVlanManager.h"
#include "fboss/agent/packet/EthHdr.h"
#include "fboss/agent/packet/PktUtil.h"
#include "fboss/agent/platforms/sai/SaiPlatform.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/state/SwitchState.h"

#include "fboss/agent/hw/HwSwitchWarmBootHelper.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"

#include <folly/logging/xlog.h>

#include <optional>

extern "C" {
#include <sai.h>
}

DEFINE_bool(enable_sai_debug_log, false, "Turn on SAI debugging logging");
DEFINE_bool(flexports, false, "Load the agent with flexport support enabled");

namespace facebook::fboss {

// We need this global SaiSwitch* to support registering SAI callbacks
// which can then use SaiSwitch to do their work. The current callback
// facility in SAI does not support passing user data to come back
// with the callback.
// N.B., if we want to have multiple SaiSwitches in a device with multiple
// cards being managed by one instance of FBOSS, this will need to be
// extended, presumably into an array keyed by switch id.
static SaiSwitch* __gSaiSwitch;

// Free functions to register as callbacks
void __gPacketRxCallback(
    sai_object_id_t switch_id,
    sai_size_t buffer_size,
    const void* buffer,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  __gSaiSwitch->packetRxCallbackTopHalf(
      SwitchSaiId{switch_id}, buffer_size, buffer, attr_count, attr_list);
}

void __glinkStateChangedNotification(
    uint32_t count,
    const sai_port_oper_status_notification_t* data) {
  __gSaiSwitch->linkStateChangedCallbackTopHalf(count, data);
}

void __gFdbEventCallback(
    uint32_t count,
    const sai_fdb_event_notification_data_t* data) {
  __gSaiSwitch->fdbEventCallback(count, data);
}

SaiSwitch::SaiSwitch(SaiPlatform* platform, uint32_t featuresDesired)
    : HwSwitch(featuresDesired), platform_(platform) {
  utilCreateDir(platform_->getVolatileStateDir());
  utilCreateDir(platform_->getPersistentStateDir());
}

SaiSwitch::~SaiSwitch() {
  stopNonCallbackThreads();
}

HwInitResult SaiSwitch::init(Callback* callback) noexcept {
  HwInitResult ret;
  {
    std::lock_guard<std::mutex> lock(saiSwitchMutex_);
    ret = initLocked(lock, callback);
  }
  // N.B., state changed will be locking/unlocking in a more fine grained manner
  // and expects the mutex to be unlocked
  if (bootType_ == BootType::WARM_BOOT) {
    stateChanged(StateDelta(std::make_shared<SwitchState>(), ret.switchState));
  } else {
    /*
     * TODO(skhare)
     * SwitchState does not carry AclTable today, and thus a single table is
     * created explicitly and every AclEntry is added to the same table.
     * Extend SwitchState to carry AclTable, and then let stateChanged()
     * AclTable Delta processing handle the AclTable creation.
     */
    managerTable_->aclTableManager().addAclTable();
  }
  return ret;
}

void SaiSwitch::unregisterCallbacks() noexcept {
  // after unregistering there could still be a single packet in our
  // pipeline. To fully shut down rx, we need to stop the thread and
  // let the possible last packet get processed. Since processing a
  // packet takes the saiSwitchMutex_, before calling join() on the thread
  // we need to release the lock.
  {
    std::lock_guard<std::mutex> lock(saiSwitchMutex_);
    unregisterCallbacksLocked(lock);
  }

  // linkscan is turned off and the evb loop is set to break
  // just need to block until the last event is processed
  if (getFeaturesDesired() & FeaturesDesired::LINKSCAN_DESIRED) {
    linkStateBottomHalfEventBase_.terminateLoopSoon();
    linkStateBottomHalfThread_->join();
    // link scan is completely shut-off
  }
  // rx is turned off and the evb loop is set to break
  // just need to block until the last packet is processed
  if (getFeaturesDesired() & FeaturesDesired::PACKET_RX_DESIRED) {
    rxBottomHalfEventBase_.terminateLoopSoon();
    rxBottomHalfThread_->join();
    // rx is completely shut-off
  }
}

std::shared_ptr<SwitchState> SaiSwitch::stateChanged(const StateDelta& delta) {
  processDelta(
      delta.getPortsDelta(),
      managerTable_->portManager(),
      &SaiPortManager::changePort,
      &SaiPortManager::addPort,
      &SaiPortManager::removePort);
  processDelta(
      delta.getVlansDelta(),
      managerTable_->vlanManager(),
      &SaiVlanManager::changeVlan,
      &SaiVlanManager::addVlan,
      &SaiVlanManager::removeVlan);

  auto qosDelta = delta.getDefaultDataPlaneQosPolicyDelta();
  if ((qosDelta.getOld() != qosDelta.getNew()) &&
      platform_->getAsic()->isSupported(HwAsic::Feature::QOS_MAP_GLOBAL)) {
    // Only handle the global default QoS policy.
    auto lock = std::lock_guard<std::mutex>(saiSwitchMutex_);
    if (qosDelta.getOld() && qosDelta.getNew()) {
      managerTable_->switchManager().changeDefaultDataPlaneQosPolicy(
          qosDelta.getOld(), qosDelta.getNew());
    } else if (qosDelta.getNew()) {
      managerTable_->switchManager().addDefaultDataPlaneQosPolicy(
          qosDelta.getNew());
    } else if (qosDelta.getOld()) {
      managerTable_->switchManager().addDefaultDataPlaneQosPolicy(
          qosDelta.getOld());
    }
  } else {
    XLOG(WARNING)
        << "Skip programming default qos map; ASIC doesn't support it";
  }

  processDelta(
      delta.getIntfsDelta(),
      managerTable_->routerInterfaceManager(),
      &SaiRouterInterfaceManager::changeRouterInterface,
      &SaiRouterInterfaceManager::addRouterInterface,
      &SaiRouterInterfaceManager::removeRouterInterface);

  for (const auto& vlanDelta : delta.getVlansDelta()) {
    processDelta(
        vlanDelta.getArpDelta(),
        managerTable_->neighborManager(),
        &SaiNeighborManager::changeNeighbor<ArpEntry>,
        &SaiNeighborManager::addNeighbor<ArpEntry>,
        &SaiNeighborManager::removeNeighbor<ArpEntry>);

    processDelta(
        vlanDelta.getNdpDelta(),
        managerTable_->neighborManager(),
        &SaiNeighborManager::changeNeighbor<NdpEntry>,
        &SaiNeighborManager::addNeighbor<NdpEntry>,
        &SaiNeighborManager::removeNeighbor<NdpEntry>);
  }

  for (const auto& routeDelta : delta.getRouteTablesDelta()) {
    auto routerID = routeDelta.getOld() ? routeDelta.getOld()->getID()
                                        : routeDelta.getNew()->getID();
    processDelta(
        routeDelta.getRoutesV4Delta(),
        managerTable_->routeManager(),
        &SaiRouteManager::changeRoute<folly::IPAddressV4>,
        &SaiRouteManager::addRoute<folly::IPAddressV4>,
        &SaiRouteManager::removeRoute<folly::IPAddressV4>,
        routerID);

    processDelta(
        routeDelta.getRoutesV6Delta(),
        managerTable_->routeManager(),
        &SaiRouteManager::changeRoute<folly::IPAddressV6>,
        &SaiRouteManager::addRoute<folly::IPAddressV6>,
        &SaiRouteManager::removeRoute<folly::IPAddressV6>,
        routerID);
  }

  {
    auto controlPlaneDelta = delta.getControlPlaneDelta();
    if (controlPlaneDelta.getOld() != controlPlaneDelta.getNew()) {
      auto lock = std::lock_guard<std::mutex>(saiSwitchMutex_);
      managerTable_->hostifManager().processHostifDelta(controlPlaneDelta);
    }
  }

  processDelta(
      delta.getLabelForwardingInformationBaseDelta(),
      managerTable_->inSegEntryManager(),
      &SaiInSegEntryManager::processChangedInSegEntry,
      &SaiInSegEntryManager::processAddedInSegEntry,
      &SaiInSegEntryManager::processRemovedInSegEntry);
  processDelta(
      delta.getLoadBalancersDelta(),
      managerTable_->switchManager(),
      &SaiSwitchManager::changeLoadBalancer,
      &SaiSwitchManager::addOrUpdateLoadBalancer,
      &SaiSwitchManager::removeLoadBalancer);

  processDelta(
      delta.getAclsDelta(),
      managerTable_->aclTableManager(),
      &SaiAclTableManager::changedAclEntry,
      &SaiAclTableManager::addAclEntry,
      &SaiAclTableManager::removeAclEntry);

  return delta.newState();
}

bool SaiSwitch::isValidStateUpdate(const StateDelta& delta) const {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  return isValidStateUpdateLocked(lock, delta);
}

std::unique_ptr<TxPacket> SaiSwitch::allocatePacket(uint32_t size) const {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  return allocatePacketLocked(lock, size);
}

bool SaiSwitch::sendPacketSwitchedAsync(
    std::unique_ptr<TxPacket> pkt) noexcept {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  return sendPacketSwitchedAsyncLocked(lock, std::move(pkt));
}

bool SaiSwitch::sendPacketOutOfPortAsync(
    std::unique_ptr<TxPacket> pkt,
    PortID portID,
    std::optional<uint8_t> queue) noexcept {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  return sendPacketOutOfPortAsyncLocked(lock, std::move(pkt), portID, queue);
}

bool SaiSwitch::sendPacketSwitchedSync(std::unique_ptr<TxPacket> pkt) noexcept {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  return sendPacketSwitchedSyncLocked(lock, std::move(pkt));
}

bool SaiSwitch::sendPacketOutOfPortSync(
    std::unique_ptr<TxPacket> pkt,
    PortID portID,
    std::optional<uint8_t> /* queueId */) noexcept {
  // TODO: Implement once hostif supports pakcet tx with queue ID.
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  return sendPacketOutOfPortSyncLocked(lock, std::move(pkt), portID);
}

void SaiSwitch::updateStats(SwitchStats* switchStats) {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  updateStatsLocked(lock, switchStats);
}

void SaiSwitch::fetchL2Table(std::vector<L2EntryThrift>* l2Table) const {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  fetchL2TableLocked(lock, l2Table);
}

void SaiSwitch::gracefulExit(folly::dynamic& switchState) {
  if (!platform_->getAsic()->isSupported(HwAsic::Feature::WARM_BOOT)) {
    XLOG(ERR) << " Asic does not support warm boot, skipping graceful exit";
    return;
  }
  /*
   Callback threads need to be stopped without holding the lock.
   Reason being that these threads themselves acquire the mutex while
   doing their work. So an example deadlock scenario would be
    (T0 = main event base,T1 = asyncTxEventBase)
    T0: SaiSwitch::sendPacketSwitchedAsync()
    T0: enqueue a lambda on asyncTxEventBase_;
    T0: receive shutdown signal, take mutex, call gracefulExitLocked
    T1: lambda runs, blocks trying to lock the mutex
    T0: in stopThreadsLocked, call asyncTxEventBase_.terminateLoopSoon
    T1: (still blocked on the mutex, can't make progress)
    T0: call asyncTxThread_->join, block on T1
    T1: (still blocked on the mutex)
    which is a deadlock.
  */
  stopNonCallbackThreads();
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  gracefulExitLocked(switchState, lock);
}

void SaiSwitch::gracefulExitLocked(
    folly::dynamic& switchState,
    const std::lock_guard<std::mutex>& lock) {
  SaiSwitchTraits::Attributes::SwitchRestartWarm restartWarm{true};
  SaiApiTable::getInstance()->switchApi().setAttribute(switchId_, restartWarm);
  switchState[kHwSwitch] = toFollyDynamicLocked(lock);
  platform_->getWarmBootHelper()->storeWarmBootState(switchState);
  platform_->getWarmBootHelper()->setCanWarmBoot();
  managerTable_->switchManager().gracefulExit();
}

folly::dynamic SaiSwitch::toFollyDynamic() const {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  return toFollyDynamicLocked(lock);
}

void SaiSwitch::switchRunStateChanged(SwitchRunState newState) {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  switchRunStateChangedLocked(lock, newState);
}

void SaiSwitch::exitFatal() const {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  exitFatalLocked(lock);
}

bool SaiSwitch::isPortUp(PortID port) const {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  return isPortUpLocked(lock, port);
}

bool SaiSwitch::getAndClearNeighborHit(RouterID vrf, folly::IPAddress& ip) {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  return getAndClearNeighborHitLocked(lock, vrf, ip);
}

void SaiSwitch::clearPortStats(
    const std::unique_ptr<std::vector<int32_t>>& ports) {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  clearPortStatsLocked(lock, ports);
}

cfg::PortSpeed SaiSwitch::getPortMaxSpeed(PortID port) const {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  return getPortMaxSpeedLocked(lock, port);
}

void SaiSwitch::packetRxCallbackTopHalf(
    SwitchSaiId switch_id,
    sai_size_t buffer_size,
    const void* buffer,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  std::vector<sai_attribute_t> attrList;
  attrList.resize(attr_count);
  std::copy(attr_list, attr_list + attr_count, attrList.data());
  std::unique_ptr<folly::IOBuf> ioBuf =
      folly::IOBuf::copyBuffer(buffer, buffer_size);
  rxBottomHalfEventBase_.runInEventBaseThread(
      [this,
       switch_id,
       ioBufTmp = std::move(ioBuf),
       attrListTmp = std::move(attrList)]() mutable {
        packetRxCallbackBottomHalf(switch_id, std::move(ioBufTmp), attrListTmp);
      });
}

void SaiSwitch::linkStateChangedCallbackTopHalf(
    uint32_t count,
    const sai_port_oper_status_notification_t* operStatus) {
  std::vector<sai_port_oper_status_notification_t> operStatusTmp;
  operStatusTmp.resize(count);
  std::copy(operStatus, operStatus + count, operStatusTmp.data());
  linkStateBottomHalfEventBase_.runInEventBaseThread(
      [this, operStatus = std::move(operStatusTmp)]() mutable {
        linkStateChangedCallbackBottomHalf(std::move(operStatus));
      });
}

void SaiSwitch::linkStateChangedCallbackBottomHalf(
    std::vector<sai_port_oper_status_notification_t> operStatus) {
  std::map<PortID, bool> swPortId2Status;
  for (auto i = 0; i < operStatus.size(); i++) {
    bool up = operStatus[i].port_state == SAI_PORT_OPER_STATUS_UP;

    // Look up SwitchState PortID by port sai id in ConcurrentIndices
    const auto portItr =
        concurrentIndices_->portIds.find(PortSaiId(operStatus[i].port_id));
    if (portItr == concurrentIndices_->portIds.cend()) {
      XLOG(WARNING)
          << "received port notification for port with unknown sai id: "
          << operStatus[i].port_id;
      continue;
    }
    PortID swPortId = portItr->second;

    XLOGF(
        INFO,
        "Link state changed {} ({}): {}",
        swPortId,
        PortSaiId{operStatus[i].port_id},
        up ? "up" : "down");

    if (!up) {
      /*
       * Only link down are handled in the fast path. We let the
       * link up processing happen via the regular state change
       * mechanism. Reason for that is, post a link down
       * - We signal FDB entry, neighbor entry, next hop and next hop group
       *   that a link went down.
       * - Next hop group then shrinks the group based on which next hops are
       * affected.
       * - We now signal the callback (SwSwitch for wedge_agent, HwTest for hw
       * tests) for this link down state
       *    - SwSwitch in turn schedules a non coalescing port down state update
       *    - Schedules a neighbor remove state update
       * - Meanwhile, if we get a port up event, we will just signal this upto
       * the SwSwitch and not handle this is in the fast path. Reason being,
       * post a link up the link is not immediately ready for packet handling,
       * so if we expand ECMP groups in the fast path, we will see some ms of
       * traffic loss. So we let the link up processing happen via switch
       * updates, which means that it will be queued behind the link down and
       * neighbor purge. So a ECMP group reexpansion would need both a link up
       * and neighbor add state update for expansion. At this point we are
       * guaranteed to have the link be ready for packet transmission, since we
       * already resolved neighbors over that link.
       */
      std::lock_guard<std::mutex> lock{saiSwitchMutex_};
      managerTable_->fdbManager().handleLinkDown(swPortId);
    }
    swPortId2Status[swPortId] = up;
  }
  // Issue callbacks in a separate loop so fast link status change
  // processing is not at the mercy of what the callback (SwSwitch, HwTest)
  // does with the callback notification.
  for (auto swPortIdAndStatus : swPortId2Status) {
    callback_->linkStateChanged(
        swPortIdAndStatus.first, swPortIdAndStatus.second);
  }
}

BootType SaiSwitch::getBootType() const {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  return getBootTypeLocked(lock);
}

const SaiManagerTable* SaiSwitch::managerTable() const {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  return managerTableLocked(lock);
}

SaiManagerTable* SaiSwitch::managerTable() {
  std::lock_guard<std::mutex> lock(saiSwitchMutex_);
  return managerTableLocked(lock);
}

// Begin Locked functions with actual SaiSwitch functionality

std::shared_ptr<SwitchState> SaiSwitch::getColdBootSwitchState() {
  auto state = std::make_shared<SwitchState>();

  if (platform_->getAsic()->isSupported(HwAsic::Feature::QUEUE)) {
    // get cpu queue settings
    auto cpu = std::make_shared<ControlPlane>();
    auto cpuQueues = managerTable_->hostifManager().getQueueSettings();
    cpu->resetQueues(cpuQueues);
    state->resetControlPlane(cpu);
    state->publish();
  }

  return state;
}

HwInitResult SaiSwitch::initLocked(
    const std::lock_guard<std::mutex>& lock,
    Callback* callback) noexcept {
  HwInitResult ret;

  auto wbHelper = platform_->getWarmBootHelper();
  bootType_ =
      wbHelper->canWarmBoot() ? BootType::WARM_BOOT : BootType::COLD_BOOT;
  ret.bootType = bootType_;
  std::unique_ptr<folly::dynamic> adapterKeysJson;
  std::unique_ptr<folly::dynamic> adapterKeys2AdapterHostKeysJson;

  std::optional<SwitchSaiId> existingSwitchId;

  sai_api_initialize(0, platform_->getServiceMethodTable());
  if (bootType_ == BootType::WARM_BOOT) {
    auto switchStateJson = wbHelper->getWarmBootState();
    ret.switchState = SwitchState::fromFollyDynamic(switchStateJson[kSwSwitch]);
    ret.switchState->publish();
    if (platform_->getAsic()->needsObjectKeyCache()) {
      adapterKeysJson = std::make_unique<folly::dynamic>(
          switchStateJson[kHwSwitch][kAdapterKeys]);
      if (switchStateJson[kHwSwitch].find(kAdapterKey2AdapterHostKey) !=
          switchStateJson[kHwSwitch].items().end()) {
        adapterKeys2AdapterHostKeysJson = std::make_unique<folly::dynamic>(
            switchStateJson[kHwSwitch][kAdapterKey2AdapterHostKey]);
      }
      const auto& switchKeysJson = (*adapterKeysJson)[saiObjectTypeToString(
          SaiSwitchTraits::ObjectType)];
      CHECK_EQ(1, switchKeysJson.size());
      existingSwitchId =
          fromFollyDynamic<SaiSwitchTraits>(*switchKeysJson.begin());
    } else {
      // TODO - check if we can call get_object_keys to retrieve
      // switch object key before having called create_switch first.
      //(get_object_keys takes a switch id parameter)
      existingSwitchId = 0;
    }
  }
  SaiApiTable::getInstance()->queryApis();
  concurrentIndices_ = std::make_unique<ConcurrentIndices>();
  managerTable_ =
      std::make_unique<SaiManagerTable>(platform_, existingSwitchId);
  switchId_ = managerTable_->switchManager().getSwitchSaiId();
  // TODO(borisb): find a cleaner solution to this problem.
  // perhaps reload fixes it?
  auto saiStore = SaiStore::getInstance();
  saiStore->setSwitchId(switchId_);
  if (platform_->getObjectKeysSupported()) {
    saiStore->reload(
        adapterKeysJson.get(), adapterKeys2AdapterHostKeysJson.get());
  }
  managerTable_->createSaiTableManagers(platform_, concurrentIndices_.get());
  callback_ = callback;
  __gSaiSwitch = this;
  if (FLAGS_enable_sai_debug_log) {
    SaiApiTable::getInstance()->enableDebugLogging();
  }
  if (bootType_ != BootType::WARM_BOOT) {
    ret.switchState = getColdBootSwitchState();
  }
  return ret;
}

void SaiSwitch::initLinkScanLocked(
    const std::lock_guard<std::mutex>& /* lock */) {
  linkStateBottomHalfThread_ = std::make_unique<std::thread>([this]() {
    initThread("fbossSaiLnkScnBH");
    linkStateBottomHalfEventBase_.loopForever();
  });
  auto& switchApi = SaiApiTable::getInstance()->switchApi();
  switchApi.registerPortStateChangeCallback(
      switchId_, __glinkStateChangedNotification);
}

void SaiSwitch::initRxLocked(const std::lock_guard<std::mutex>& /* lock */) {
  rxBottomHalfThread_ = std::make_unique<std::thread>([this]() {
    initThread("fbossSaiRxBH");
    rxBottomHalfEventBase_.loopForever();
  });
  auto& switchApi = SaiApiTable::getInstance()->switchApi();
  switchApi.registerRxCallback(switchId_, __gPacketRxCallback);
}

void SaiSwitch::initAsyncTxLocked(
    const std::lock_guard<std::mutex>& /* lock */) {
  asyncTxThread_ = std::make_unique<std::thread>([this]() {
    initThread("fbossSaiAsyncTx");
    asyncTxEventBase_.loopForever();
  });
}

void SaiSwitch::packetRxCallbackBottomHalf(
    SwitchSaiId /* unused */,
    std::unique_ptr<folly::IOBuf> ioBuf,
    std::vector<sai_attribute_t> attrList) {
  std::optional<PortSaiId> portSaiIdOpt;
  for (auto attr : attrList) {
    switch (attr.id) {
      case SAI_HOSTIF_PACKET_ATTR_INGRESS_PORT:
        portSaiIdOpt = attr.value.oid;
        break;
      case SAI_HOSTIF_PACKET_ATTR_INGRESS_LAG:
      case SAI_HOSTIF_PACKET_ATTR_HOSTIF_TRAP_ID:
        break;
      default:
        XLOG(INFO) << "invalid attribute received";
    }
  }
  CHECK(portSaiIdOpt);
  PortSaiId portSaiId{portSaiIdOpt.value()};

  const auto portItr = concurrentIndices_->portIds.find(portSaiId);
  if (portItr == concurrentIndices_->portIds.cend()) {
    XLOG(WARNING) << "RX packet had port with unknown sai id: 0x" << std::hex
                  << portSaiId;
    return;
  }
  PortID swPortId = portItr->second;

  const auto vlanItr = concurrentIndices_->vlanIds.find(portSaiId);
  if (vlanItr == concurrentIndices_->vlanIds.cend()) {
    XLOG(WARNING) << "RX packet had port in no known vlan: 0x" << std::hex
                  << portSaiId;
    return;
  }
  VlanID swVlanId = vlanItr->second;

  auto rxPacket = std::make_unique<SaiRxPacket>(
      ioBuf->length(), ioBuf->writableData(), swPortId, swVlanId);
  callback_->packetReceived(std::move(rxPacket));
}

void SaiSwitch::unregisterCallbacksLocked(
    const std::lock_guard<std::mutex>& /* lock */) noexcept {
  auto& switchApi = SaiApiTable::getInstance()->switchApi();
  if (getFeaturesDesired() & FeaturesDesired::LINKSCAN_DESIRED) {
    switchApi.unregisterPortStateChangeCallback(switchId_);
  }
  if (getFeaturesDesired() & FeaturesDesired::PACKET_RX_DESIRED) {
    switchApi.unregisterRxCallback(switchId_);
  }
  switchApi.unregisterFdbEventCallback(switchId_);
}

bool SaiSwitch::isValidStateUpdateLocked(
    const std::lock_guard<std::mutex>& /* lock */,
    const StateDelta& /* delta */) const {
  return true;
}

std::unique_ptr<TxPacket> SaiSwitch::allocatePacketLocked(
    const std::lock_guard<std::mutex>& /* lock */,
    uint32_t size) const {
  return std::make_unique<SaiTxPacket>(size);
}

bool SaiSwitch::sendPacketSwitchedAsyncLocked(
    const std::lock_guard<std::mutex>& lock,
    std::unique_ptr<TxPacket> pkt) noexcept {
  asyncTxEventBase_.runInEventBaseThread(
      [this, pkt = std::move(pkt)]() mutable {
        std::lock_guard<std::mutex> lock(saiSwitchMutex_);
        sendPacketSwitchedSyncLocked(lock, std::move(pkt));
      });
  return true;
}

bool SaiSwitch::sendPacketOutOfPortAsyncLocked(
    const std::lock_guard<std::mutex>& lock,
    std::unique_ptr<TxPacket> pkt,
    PortID portID,
    std::optional<uint8_t> /* queue */) noexcept {
  asyncTxEventBase_.runInEventBaseThread(
      [this, pkt = std::move(pkt), portID]() mutable {
        std::lock_guard<std::mutex> lock(saiSwitchMutex_);
        sendPacketOutOfPortSyncLocked(lock, std::move(pkt), portID);
      });
  return true;
}

bool SaiSwitch::sendPacketSwitchedSyncLocked(
    const std::lock_guard<std::mutex>& lock,
    std::unique_ptr<TxPacket> pkt) noexcept {
  /*
  TODO: remove this hack when difference in src and dst mac is no longer
  required pipe line look up causes packet to pass through pipeline and
  subjected to forwarding as if normal packet.in such a case having same source
  & destination mac address, may cause drop. so change destination mac address
  */
  folly::io::Cursor cursor(pkt->buf());
  EthHdr ethHdr{cursor};
  if (ethHdr.getSrcMac() == ethHdr.getDstMac()) {
    auto* pktData = pkt->buf()->writableData();
    /* pktData[6]...pktData[11] is src mac */
    folly::MacAddress hackedMac{"fa:ce:b0:00:00:0c"};
    for (auto i = 0; i < folly::MacAddress::SIZE; i++) {
      pktData[folly::MacAddress::SIZE + i] = hackedMac.bytes()[i];
    }
    XLOG(DBG5) << "hacked packet as source and destination mac are same, "
               << "hacked packet as follows :";
    folly::io::Cursor dump(pkt->buf());
    XLOG(DBG5) << PktUtil::hexDump(dump);
  }

  SaiTxPacketTraits::Attributes::TxType txType(
      SAI_HOSTIF_TX_TYPE_PIPELINE_LOOKUP);
  SaiTxPacketTraits::TxAttributes attributes{txType, 0};
  SaiHostifApiPacket txPacket{
      reinterpret_cast<void*>(pkt->buf()->writableData()),
      pkt->buf()->length()};
  auto& hostifApi = SaiApiTable::getInstance()->hostifApi();
  hostifApi.send(attributes, switchId_, txPacket);
  return true;
}

bool SaiSwitch::sendPacketOutOfPortSyncLocked(
    const std::lock_guard<std::mutex>& lock,
    std::unique_ptr<TxPacket> pkt,
    PortID portID) noexcept {
  auto portHandle =
      managerTableLocked(lock)->portManager().getPortHandle(portID);
  if (!portHandle) {
    XLOG(ERR) << "Failed to send packet on invalid port: " << portID;
    return false;
  }
  /* TODO: this hack is required, sending packet out of port with with pipeline
  bypass, doesn't cause vlan tag stripping. fix this once a pipeline bypass with
  vlan stripping is available. */

  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::TX_VLAN_STRIPPING_ON_PORT)) {
    folly::io::Cursor cursor(pkt->buf());
    EthHdr ethHdr{cursor};
    if (!ethHdr.getVlanTags().empty()) {
      CHECK_EQ(ethHdr.getVlanTags().size(), 1)
          << "found more than one vlan tags while sending packet";
      /* hack to strip vlans as pipeline bypass doesn't handle this */
      cursor.reset(pkt->buf());
      XLOG(DBG5) << "strip vlan for packet";
      XLOG(DBG5) << PktUtil::hexDump(cursor);

      constexpr auto kVlanTagSize = 4;
      auto ethPayload = pkt->buf()->clone();
      // trim DA(6), SA(6) & vlan (4)
      ethPayload->trimStart(
          folly::MacAddress::SIZE + folly::MacAddress::SIZE + kVlanTagSize);

      // trim rest of packet except DA(6), SA(6)
      auto totalLength = pkt->buf()->length();
      pkt->buf()->trimEnd(
          totalLength - folly::MacAddress::SIZE - folly::MacAddress::SIZE);

      // append to trimmed ethernet header remaining payload
      pkt->buf()->appendChain(std::move(ethPayload));
      pkt->buf()->coalesce();
      cursor.reset(pkt->buf());
      XLOG(DBG5) << "stripped vlan, new packet";
      XLOG(DBG5) << PktUtil::hexDump(cursor);
    }
  }

  SaiHostifApiPacket txPacket{
      reinterpret_cast<void*>(pkt->buf()->writableData()),
      pkt->buf()->length()};

  SaiTxPacketTraits::Attributes::TxType txType(
      SAI_HOSTIF_TX_TYPE_PIPELINE_BYPASS);
  SaiTxPacketTraits::Attributes::EgressPortOrLag egressPort(
      portHandle->port->adapterKey());
  SaiTxPacketTraits::TxAttributes attributes{txType, egressPort};
  auto& hostifApi = SaiApiTable::getInstance()->hostifApi();
  hostifApi.send(attributes, switchId_, txPacket);
  return true;
}

void SaiSwitch::updateStatsLocked(
    const std::lock_guard<std::mutex>& /* lock */,
    SwitchStats* /* switchStats */) {
  managerTable_->portManager().updateStats();
  managerTable_->hostifManager().updateStats();
}

void SaiSwitch::fetchL2TableLocked(
    const std::lock_guard<std::mutex>& /* lock */,
    std::vector<L2EntryThrift>* l2Table) const {
  auto fdbEntries = getObjectKeys<SaiFdbTraits>(switchId_);
  l2Table->resize(fdbEntries.size());
  for (const auto& fdbEntry : fdbEntries) {
    L2EntryThrift entry;
    // SwitchState's VlanID is an attribute we store in the vlan, so
    // we can get it via SaiApi
    auto& vlanApi = SaiApiTable::getInstance()->vlanApi();
    VlanID swVlanId{vlanApi.getAttribute(
        VlanSaiId{fdbEntry.bridgeVlanId()},
        SaiVlanTraits::Attributes::VlanId{})};
    *entry.vlanID_ref() = swVlanId;

    // To get the PortID, we get the bridgePortId from the fdb entry,
    // then get that Bridge Port's PortId attribute. We can lookup the
    // PortID for a sai port id in ConcurrentIndices
    auto& fdbApi = SaiApiTable::getInstance()->fdbApi();
    auto bridgePortSaiId =
        fdbApi.getAttribute(fdbEntry, SaiFdbTraits::Attributes::BridgePortId());
    auto& bridgeApi = SaiApiTable::getInstance()->bridgeApi();
    auto portSaiId = bridgeApi.getAttribute(
        BridgePortSaiId{bridgePortSaiId},
        SaiBridgePortTraits::Attributes::PortId{});
    const auto portItr = concurrentIndices_->portIds.find(PortSaiId{portSaiId});
    if (portItr == concurrentIndices_->portIds.cend()) {
      XLOG(WARNING) << "l2 table entry had unknown port sai id: " << portSaiId;
      continue;
    }
    *entry.port_ref() = portItr->second;

    // entry is filled out; push it onto the L2 table
    l2Table->push_back(entry);
  }
}

void SaiSwitch::stopNonCallbackThreads() {
  asyncTxEventBase_.terminateLoopSoon();
  if (asyncTxThread_) {
    asyncTxThread_->join();
  }
}

folly::dynamic SaiSwitch::toFollyDynamicLocked(
    const std::lock_guard<std::mutex>& /* lock */) const {
  auto adapterKeys = SaiStore::getInstance()->adapterKeysFollyDynamic();
  // Need to provide full namespace scope for toFollyDynamic to disambiguate
  // from member SaiSwitch::toFollyDynamic
  auto switchKeys = folly::dynamic::array(
      facebook::fboss::toFollyDynamic<SaiSwitchTraits>(switchId_));
  adapterKeys[saiObjectTypeToString(SaiSwitchTraits::ObjectType)] = switchKeys;

  folly::dynamic hwSwitch = folly::dynamic::object;
  hwSwitch[kAdapterKeys] = adapterKeys;
  hwSwitch[kAdapterKey2AdapterHostKey] =
      SaiStore::getInstance()->adapterKeys2AdapterHostKeysFollyDynamic();
  return hwSwitch;
}

void SaiSwitch::switchRunStateChangedLocked(
    const std::lock_guard<std::mutex>& lock,
    SwitchRunState newState) {
  switch (newState) {
    case SwitchRunState::INITIALIZED: {
      auto& switchApi = SaiApiTable::getInstance()->switchApi();
      switchApi.registerFdbEventCallback(switchId_, __gFdbEventCallback);
    } break;
    case SwitchRunState::CONFIGURED: {
      if (getFeaturesDesired() & FeaturesDesired::LINKSCAN_DESIRED) {
        initLinkScanLocked(lock);
      }
      // TODO: T56772674: Optimize Rx and Tx init
      initAsyncTxLocked(lock);
      if (getFeaturesDesired() & FeaturesDesired::PACKET_RX_DESIRED) {
        initRxLocked(lock);
      }
    } break;
    default:
      break;
  }
}

void SaiSwitch::exitFatalLocked(
    const std::lock_guard<std::mutex>& /* lock */) const {}

bool SaiSwitch::isPortUpLocked(
    const std::lock_guard<std::mutex>& /* lock */,
    PortID /* port */) const {
  return true;
}

cfg::PortSpeed SaiSwitch::getPortMaxSpeedLocked(
    const std::lock_guard<std::mutex>& lock,
    PortID port) const {
  auto* managerTable = managerTableLocked(lock);
  return managerTable->portManager().getMaxSpeed(port);
}

bool SaiSwitch::getAndClearNeighborHitLocked(
    const std::lock_guard<std::mutex>& /* lock */,
    RouterID /* vrf */,
    folly::IPAddress& /* ip */) {
  return true;
}

void SaiSwitch::clearPortStatsLocked(
    const std::lock_guard<std::mutex>& /* lock */,
    const std::unique_ptr<std::vector<int32_t>>& ports) {
  managerTable_->portManager().clearStats(ports);
}

BootType SaiSwitch::getBootTypeLocked(
    const std::lock_guard<std::mutex>& /* lock */) const {
  return bootType_;
}

const SaiManagerTable* SaiSwitch::managerTableLocked(
    const std::lock_guard<std::mutex>& /* lock */) const {
  return managerTable_.get();
}

SaiManagerTable* SaiSwitch::managerTableLocked(
    const std::lock_guard<std::mutex>& /* lock */) {
  return managerTable_.get();
}

void SaiSwitch::fdbEventCallback(
    uint32_t count,
    const sai_fdb_event_notification_data_t* data) {
  fdbEventCallbackLocked(count, data);
}

void SaiSwitch::fdbEventCallbackLocked(
    uint32_t /*count*/,
    const sai_fdb_event_notification_data_t* /*data*/) {
  // TODO  - program macs from learn events to FDB
}

template <
    typename Delta,
    typename Manager,
    typename... Args,
    typename ChangeFunc,
    typename AddedFunc,
    typename RemovedFunc>
void SaiSwitch::processDelta(
    Delta delta,
    Manager& manager,
    ChangeFunc changedFunc,
    AddedFunc addedFunc,
    RemovedFunc removedFunc,
    Args... args) {
  DeltaFunctions::forEachChanged(
      delta,
      [&](const std::shared_ptr<typename Delta::Node>& removed,
          const std::shared_ptr<typename Delta::Node>& added) {
        auto lock = std::lock_guard<std::mutex>(saiSwitchMutex_);
        (manager.*changedFunc)(removed, added, args...);
      },
      [&](const std::shared_ptr<typename Delta::Node>& added) {
        auto lock = std::lock_guard<std::mutex>(saiSwitchMutex_);
        (manager.*addedFunc)(added, args...);
      },
      [&](const std::shared_ptr<typename Delta::Node>& removed) {
        auto lock = std::lock_guard<std::mutex>(saiSwitchMutex_);
        (manager.*removedFunc)(removed, args...);
      });
}

template <
    typename Delta,
    typename Manager,
    typename... Args,
    typename ChangeFunc>
void SaiSwitch::processChangedDelta(
    Delta delta,
    Manager& manager,
    ChangeFunc changedFunc,
    Args... args) {
  DeltaFunctions::forEachChanged(
      delta,
      [&](const std::shared_ptr<typename Delta::Node>& added,
          const std::shared_ptr<typename Delta::Node>& removed) {
        auto lock = std::lock_guard<std::mutex>(saiSwitchMutex_);
        (manager.*changedFunc)(added, removed, args...);
      });
}

template <
    typename Delta,
    typename Manager,
    typename... Args,
    typename AddedFunc>
void SaiSwitch::processAddedDelta(
    Delta delta,
    Manager& manager,
    AddedFunc addedFunc,
    Args... args) {
  DeltaFunctions::forEachAdded(
      delta, [&](const std::shared_ptr<typename Delta::Node>& added) {
        auto lock = std::lock_guard<std::mutex>(saiSwitchMutex_);
        (manager.*addedFunc)(added, args...);
      });
}

template <
    typename Delta,
    typename Manager,
    typename... Args,
    typename RemovedFunc>
void SaiSwitch::processRemovedDelta(
    Delta delta,
    Manager& manager,
    RemovedFunc removedFunc,
    Args... args) {
  DeltaFunctions::forEachRemoved(
      delta, [&](const std::shared_ptr<typename Delta::Node>& removed) {
        auto lock = std::lock_guard<std::mutex>(saiSwitchMutex_);
        (manager.*removedFunc)(removed, args...);
      });
}

} // namespace facebook::fboss
