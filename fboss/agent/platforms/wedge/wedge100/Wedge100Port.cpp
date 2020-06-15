/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/platforms/wedge/wedge100/Wedge100Port.h"

#include "fboss/agent/platforms/wedge/wedge100/Wedge100Platform.h"
#include "fboss/lib/phy/gen-cpp2/phy_types.h"

namespace {
using facebook::fboss::TransmitterTechnology;
using facebook::fboss::Wedge100Port;
using facebook::fboss::phy::TxSettings;

TxSettings
make_tx_settings(int16_t dc, int16_t pre, int16_t main, int16_t post) {
  TxSettings tx;
  tx.driveCurrent = dc;
  tx.pre = pre;
  tx.main = main;
  tx.post = post;
  return tx;
}

/* Tuning values for this platform. These are separated in to 7 groups
 * based on the board trace and signal integrity for different ports.
 */
std::vector<Wedge100Port::TxOverrides> txOverrideGroups = {
    {
        {{TransmitterTechnology::COPPER, 1.0},
         make_tx_settings(0xa, 0x4, 0x3c, 0x30)},
        {{TransmitterTechnology::COPPER, 1.5},
         make_tx_settings(0xa, 0x4, 0x3c, 0x30)},
        {{TransmitterTechnology::COPPER, 2.0},
         make_tx_settings(0xa, 0x4, 0x3c, 0x30)},
        {{TransmitterTechnology::COPPER, 2.5},
         make_tx_settings(0xc, 0x6, 0x3e, 0x32)},
        {{TransmitterTechnology::COPPER, 3.0},
         make_tx_settings(0xc, 0x6, 0x3e, 0x32)},
    },
    {
        {{TransmitterTechnology::COPPER, 1.0},
         make_tx_settings(0xa, 0x6, 0x40, 0x2a)},
        {{TransmitterTechnology::COPPER, 1.5},
         make_tx_settings(0xa, 0x7, 0x3e, 0x2b)},
        {{TransmitterTechnology::COPPER, 2.0},
         make_tx_settings(0xb, 0x8, 0x3c, 0x2c)},
        {{TransmitterTechnology::COPPER, 2.5},
         make_tx_settings(0xc, 0x7, 0x3d, 0x2c)},
        {{TransmitterTechnology::COPPER, 3.0},
         make_tx_settings(0xc, 0x6, 0x3c, 0x2e)},
    },
    {
        {{TransmitterTechnology::COPPER, 1.0},
         make_tx_settings(0x9, 0x8, 0x42, 0x26)},
        {{TransmitterTechnology::COPPER, 1.5},
         make_tx_settings(0x9, 0x9, 0x41, 0x26)},
        {{TransmitterTechnology::COPPER, 2.0},
         make_tx_settings(0x9, 0x9, 0x40, 0x27)},
        {{TransmitterTechnology::COPPER, 2.5},
         make_tx_settings(0x9, 0x9, 0x3f, 0x28)},
        {{TransmitterTechnology::COPPER, 3.0},
         make_tx_settings(0xa, 0x8, 0x40, 0x28)},
    },
    {
        {{TransmitterTechnology::COPPER, 1.0},
         make_tx_settings(0x8, 0x6, 0x46, 0x24)},
        {{TransmitterTechnology::COPPER, 1.5},
         make_tx_settings(0x9, 0x6, 0x46, 0x24)},
        {{TransmitterTechnology::COPPER, 2.0},
         make_tx_settings(0x9, 0x7, 0x45, 0x24)},
        {{TransmitterTechnology::COPPER, 2.5},
         make_tx_settings(0x9, 0x8, 0x43, 0x25)},
        {{TransmitterTechnology::COPPER, 3.0},
         make_tx_settings(0xa, 0x8, 0x43, 0x25)},
    },
    {
        {{TransmitterTechnology::COPPER, 1.0},
         make_tx_settings(0x8, 0x6, 0x4c, 0x1e)},
        {{TransmitterTechnology::COPPER, 1.5},
         make_tx_settings(0x9, 0x7, 0x4b, 0x1e)},
        {{TransmitterTechnology::COPPER, 2.0},
         make_tx_settings(0x9, 0x7, 0x4b, 0x1e)},
        {{TransmitterTechnology::COPPER, 2.5},
         make_tx_settings(0x9, 0x8, 0x49, 0x1f)},
        {{TransmitterTechnology::COPPER, 3.0},
         make_tx_settings(0xa, 0x8, 0x48, 0x20)},
    },
    {
        {{TransmitterTechnology::COPPER, 1.0},
         make_tx_settings(0x8, 0x6, 0x4e, 0x1c)},
        {{TransmitterTechnology::COPPER, 1.5},
         make_tx_settings(0x9, 0x6, 0x4d, 0x1d)},
        {{TransmitterTechnology::COPPER, 2.0},
         make_tx_settings(0xa, 0x7, 0x4b, 0x1e)},
        {{TransmitterTechnology::COPPER, 2.5},
         make_tx_settings(0xa, 0x8, 0x49, 0x1f)},
        {{TransmitterTechnology::COPPER, 3.0},
         make_tx_settings(0xa, 0x8, 0x48, 0x20)},
    },
    {
        {{TransmitterTechnology::COPPER, 1.0},
         make_tx_settings(0x8, 0x6, 0x50, 0x1a)},
        {{TransmitterTechnology::COPPER, 1.5},
         make_tx_settings(0x9, 0x6, 0x4e, 0x1c)},
        {{TransmitterTechnology::COPPER, 2.0},
         make_tx_settings(0x9, 0x6, 0x4e, 0x1c)},
        {{TransmitterTechnology::COPPER, 2.5},
         make_tx_settings(0x9, 0x7, 0x4b, 0x1e)},
        {{TransmitterTechnology::COPPER, 3.0},
         make_tx_settings(0x9, 0x8, 0x4a, 0x1e)},
    },
};

std::array<uint8_t, 28> traceGroupMapping = {{
    // Each front panel port maps to one trace group in the above
    // vector. The index is the TransceiverID, the value is the index
    // for which set of overrides to use from txOverrideGroups.
    1, 0, 1, 1, 2, 2, 3, 3, 4, 4, 4, 4, 5, 5, 6,
    6, 6, 5, 5, 5, 4, 4, 3, 3, 3, 3, 2, 2
    // TODO: what should 28-31 be?
}};

} // namespace

namespace facebook::fboss {

Wedge100Port::Wedge100Port(
    PortID id,
    Wedge100Platform* platform,
    std::optional<FrontPanelResources> frontPanel)
    : WedgePort(id, platform, frontPanel) {}

Wedge100Port::TxOverrides Wedge100Port::getTxOverrides() const {
  if (supportsTransceiver()) {
    auto id = static_cast<uint16_t>(*getTransceiverID());
    if (id < traceGroupMapping.size()) {
      auto traceGroup = traceGroupMapping[id];
      return txOverrideGroups[traceGroup];
    }
  }
  return TxOverrides();
}

std::vector<phy::PinConfig> Wedge100Port::getIphyPinConfigs(
    cfg::PortProfileID profileID) const {
  if (!supportsTransceiver()) {
    return {};
  }
  folly::EventBase evb;
  if (auto cable = getCableInfo(&evb).getVia(&evb)) {
    if (auto cableLength = cable->length_ref()) {
      // TODO(pgardideh): this is temporary until we fully remove any dependence
      // on transmitter tech and only rely on the profile ID
      if (*cable->transmitterTech_ref() == TransmitterTechnology::COPPER) {
        auto cableMeters = std::max(1.0, std::min(3.0, *cableLength));
        return getPlatform()->getPlatformMapping()->getPortIphyPinConfigs(
            getPortID(), profileID, cableMeters);
      }
    }
  }
  return {};
}

bool Wedge100Port::isTop() {
  if (auto tcvrID = getTransceiverID(); tcvrID) {
    return !((*tcvrID) & 0x1);
  }
  return false;
}

} // namespace facebook::fboss
