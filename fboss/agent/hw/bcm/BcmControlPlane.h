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

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/bcm/BcmCosQueueManager.h"
#include "fboss/agent/hw/bcm/RxUtils.h"
#include "fboss/agent/state/ControlPlane.h"

extern "C" {
#include <bcm/cosq.h>
#include <bcm/types.h>
}

namespace facebook::fboss {

bcm_rx_reasons_t configRxReasonToBcmReasons(cfg::PacketRxReason reason);
cfg::PacketRxReason bcmReasonsToConfigReason(bcm_rx_reasons_t reasons);

class BcmSwitch;
class PortQueue;

class BcmControlPlane {
 public:
  BcmControlPlane(BcmSwitch* hw);

  ~BcmControlPlane() {}

  /*
   * Getters.
   */
  bcm_gport_t getCPUGPort() const {
    return gport_;
  }
  BcmCosQueueManager* getQueueManager() const {
    return queueManager_.get();
  }

  /**
   * Set up the corresponding cos queue for ControlPlane
   */
  void setupQueue(const PortQueue& queue);

  /**
   * Set up the rx packet reason to queue mapping
   */
  void setupRxReasonToQueue(const ControlPlane::RxReasonToQueue& reasonToQueue);

  ControlPlane::RxReasonToQueue getRxReasonToQueue() const;

  int getMaxCPUQueues() const {
    return maxCPUMappings_;
  }

  void setupIngressQosPolicy(const std::optional<std::string>& qosPolicyName);

  void updateQueueCounters();

  QueueConfig getMulticastQueueSettings() {
    return queueManager_->getCurrentQueueSettings().multicast;
  }

 private:
  // no copy or assignment
  BcmControlPlane(BcmControlPlane const&) = delete;
  BcmControlPlane& operator=(BcmControlPlane const&) = delete;

  void writeReasonToQueueEntry(int index, cfg::PacketRxReasonToQueue entry);
  std::optional<cfg::PacketRxReasonToQueue> readReasonToQueueEntry(
      int index) const;
  void deleteReasonToQueueEntry(int index);

  BcmSwitch* hw_{nullptr};
  // Broadcom global port number
  const bcm_gport_t gport_;
  std::unique_ptr<BcmCosQueueManager> queueManager_;
  bcm_cos_queue_t maxCPUQueue_;
  int maxCPUMappings_{0};
};

} // namespace facebook::fboss
