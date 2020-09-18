// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/platforms/common/utils/GalaxyLedUtils.h"

#include <folly/Range.h>

namespace facebook::fboss {

folly::ByteRange GalaxyLedUtils::defaultLed0Code() {
  /* Auto-generated using the Broadcom ledasm tool */
  // clang-format off
static const std::vector<uint8_t> kGalaxyLed0Code {
    0x02, 0x20, 0x28, 0x67, 0x22, 0x12, 0x40, 0x80, 0xD1, 0x74, 0x02, 0x02,
    0x00, 0x28, 0x67, 0x22, 0x12, 0x10, 0x80, 0xD1, 0x74, 0x0D, 0x12, 0xE0,
    0x85, 0x05, 0xD2, 0x0A, 0x71, 0x20, 0x52, 0x00, 0x3A, 0x90, 0x67, 0x81,
    0x75, 0xB5, 0x77, 0x28, 0x67, 0x88, 0x75, 0x2E, 0x77, 0x40, 0x67, 0x8F,
    0x75, 0x34, 0x77, 0x4D, 0x67, 0x96, 0x75, 0x3A, 0x77, 0x5A, 0x67, 0x9D,
    0x75, 0x74, 0x77, 0x67, 0x28, 0x67, 0xAB, 0x75, 0xC9, 0x16, 0xE0, 0xDA,
    0x09, 0x74, 0xB5, 0x77, 0xC9, 0x28, 0x67, 0xAB, 0x75, 0xBF, 0x16, 0xE0,
    0xDA, 0x09, 0x74, 0xB5, 0x77, 0xBF, 0x28, 0x67, 0xAB, 0x75, 0xD3, 0x16,
    0xE0, 0xDA, 0x09, 0x74, 0xB5, 0x77, 0xD3, 0x28, 0x67, 0xAB, 0x75, 0xDD,
    0x16, 0xE0, 0xDA, 0x09, 0x74, 0xB5, 0x77, 0xDD, 0x28, 0x67, 0xAB, 0x75,
    0xE7, 0x16, 0xE0, 0xDA, 0x09, 0x74, 0xB5, 0x77, 0xE7, 0x12, 0xA0, 0xF8,
    0x15, 0x1A, 0x00, 0x57, 0x12, 0xA0, 0xF8, 0x15, 0x1A, 0x01, 0x57, 0x12,
    0xA0, 0xF8, 0x15, 0x1A, 0x02, 0x57, 0x12, 0xA0, 0xF8, 0x15, 0x1A, 0x03,
    0x57, 0x12, 0xA0, 0xF8, 0x15, 0x1A, 0x04, 0x57, 0x12, 0xA0, 0xF8, 0x15,
    0x1A, 0x05, 0x57, 0x28, 0x32, 0x00, 0x32, 0x01, 0xB7, 0x97, 0x77, 0xB4,
    0x57, 0x32, 0x0F, 0x87, 0x32, 0x0F, 0x87, 0x32, 0x0F, 0x87, 0x57, 0x32,
    0x0F, 0x87, 0x32, 0x0E, 0x87, 0x32, 0x0F, 0x87, 0x57, 0x32, 0x0E, 0x87,
    0x32, 0x0F, 0x87, 0x32, 0x0F, 0x87, 0x57, 0x32, 0x0F, 0x87, 0x32, 0x0F,
    0x87, 0x32, 0x0E, 0x87, 0x57, 0x32, 0x0E, 0x87, 0x32, 0x0E, 0x87, 0x32,
    0x0F, 0x87, 0x57, 0x32, 0x0E, 0x87, 0x32, 0x0F, 0x87, 0x32, 0x0E, 0x87,
    0x57, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
};
  // clang-format on
  return folly::ByteRange(kGalaxyLed0Code.data(), kGalaxyLed0Code.size());
}

folly::ByteRange GalaxyLedUtils::defaultLed1Code() {
  /* Auto-generated using the Broadcom ledasm tool */
  // clang-format off
static const std::vector<uint8_t> kGalaxyLed1Code {
    0x02, 0x30, 0x28, 0x67, 0x17, 0x12, 0x40, 0x80, 0xD1, 0x74, 0x02, 0x12,
    0xE0, 0x85, 0x05, 0xD2, 0x0A, 0x71, 0x15, 0x52, 0x00, 0x3A, 0x30, 0x67,
    0x76, 0x75, 0xAA, 0x77, 0x1D, 0x67, 0x7D, 0x75, 0x23, 0x77, 0x35, 0x67,
    0x84, 0x75, 0x29, 0x77, 0x42, 0x67, 0x8B, 0x75, 0x2F, 0x77, 0x4F, 0x67,
    0x92, 0x75, 0x69, 0x77, 0x5C, 0x28, 0x67, 0xA0, 0x75, 0xBE, 0x16, 0xE0,
    0xDA, 0x09, 0x74, 0xAA, 0x77, 0xBE, 0x28, 0x67, 0xA0, 0x75, 0xB4, 0x16,
    0xE0, 0xDA, 0x09, 0x74, 0xAA, 0x77, 0xB4, 0x28, 0x67, 0xA0, 0x75, 0xC8,
    0x16, 0xE0, 0xDA, 0x09, 0x74, 0xAA, 0x77, 0xC8, 0x28, 0x67, 0xA0, 0x75,
    0xD2, 0x16, 0xE0, 0xDA, 0x09, 0x74, 0xAA, 0x77, 0xD2, 0x28, 0x67, 0xA0,
    0x75, 0xDC, 0x16, 0xE0, 0xDA, 0x09, 0x74, 0xAA, 0x77, 0xDC, 0x12, 0xA0,
    0xF8, 0x15, 0x1A, 0x00, 0x57, 0x12, 0xA0, 0xF8, 0x15, 0x1A, 0x01, 0x57,
    0x12, 0xA0, 0xF8, 0x15, 0x1A, 0x02, 0x57, 0x12, 0xA0, 0xF8, 0x15, 0x1A,
    0x03, 0x57, 0x12, 0xA0, 0xF8, 0x15, 0x1A, 0x04, 0x57, 0x12, 0xA0, 0xF8,
    0x15, 0x1A, 0x05, 0x57, 0x28, 0x32, 0x00, 0x32, 0x01, 0xB7, 0x97, 0x77,
    0xA9, 0x57, 0x32, 0x0F, 0x87, 0x32, 0x0F, 0x87, 0x32, 0x0F, 0x87, 0x57,
    0x32, 0x0F, 0x87, 0x32, 0x0E, 0x87, 0x32, 0x0F, 0x87, 0x57, 0x32, 0x0E,
    0x87, 0x32, 0x0F, 0x87, 0x32, 0x0F, 0x87, 0x57, 0x32, 0x0F, 0x87, 0x32,
    0x0F, 0x87, 0x32, 0x0E, 0x87, 0x57, 0x32, 0x0E, 0x87, 0x32, 0x0E, 0x87,
    0x32, 0x0F, 0x87, 0x57, 0x32, 0x0E, 0x87, 0x32, 0x0F, 0x87, 0x32, 0x0E,
    0x87, 0x57, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
};
  // clang-format on
  return folly::ByteRange(kGalaxyLed1Code.data(), kGalaxyLed1Code.size());
}

int GalaxyLedUtils::getPortIndex(PortID physicalPort) {
  int index = 0;
  if (static_cast<int>(physicalPort) > 32 &&
      static_cast<int>(physicalPort) < 97) {
    // Ports 33 - 96, with port 33 is at offset 1.
    index = physicalPort - 32;
  } else {
    // Ports 1 - 32 and 97 - 128 with port 97 is at offset 33
    index = (physicalPort > 32 ? physicalPort - 64 : physicalPort);
  }
  return index;
}

size_t GalaxyLedUtils::getPortOffset(int index) {
  return 0xa0 /* LS_LED_DATA_OFFSET_A0 */ + index - 1;
}

void GalaxyLedUtils::getDesiredLEDState(
    uint32_t* state,
    bool up,
    bool /*adminUp*/) {
  // Status is at bit 0
  if (up) {
    (*state) |= 0x1;
  } else {
    (*state) &= ~(0x1);
  }
  // Set bit 1, which causes Celestica to set LED to Blue if port is up
  (*state) |= (0x1 << 1);
  (*state) &= ~0x80;
}

std::optional<uint32_t> GalaxyLedUtils::getLEDProcessorNumber(PortID port) {
  /* Port 1-32 and 97-128 are managed by LED Proccessor 0
   * Port 33-96 are managed by LED Processor 1
   *  Port 129 & 131 are managed by LED Processor 2. We don't
   *  care about these
   */
  if (port == 129 || port == 131) {
    return std::nullopt;
  }
  if (port > 32 && port < 97) {
    return 1;
  }
  return 0;
}
} // namespace facebook::fboss
