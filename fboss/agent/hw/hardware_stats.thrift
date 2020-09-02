namespace cpp2 facebook.fboss
namespace py neteng.fboss.hardware_stats


const i64 STAT_UNINITIALIZED = -1


struct HwPortStats {
  1: i64 inBytes_ = STAT_UNINITIALIZED;
  2: i64 inUnicastPkts_ = STAT_UNINITIALIZED;
  3: i64 inMulticastPkts_ = STAT_UNINITIALIZED;
  4: i64 inBroadcastPkts_ = STAT_UNINITIALIZED;
  5: i64 inDiscards_ = STAT_UNINITIALIZED;
  6: i64 inErrors_ = STAT_UNINITIALIZED;
  7: i64 inPause_ = STAT_UNINITIALIZED;
  8: i64 inIpv4HdrErrors_ = STAT_UNINITIALIZED;
  9: i64 inIpv6HdrErrors_ = STAT_UNINITIALIZED;
  10: i64 inDstNullDiscards_ = STAT_UNINITIALIZED
  11: i64 inDiscardsRaw_ = STAT_UNINITIALIZED

  12: i64 outBytes_ = STAT_UNINITIALIZED;
  13: i64 outUnicastPkts_ = STAT_UNINITIALIZED;
  14: i64 outMulticastPkts_ = STAT_UNINITIALIZED;
  15: i64 outBroadcastPkts_ = STAT_UNINITIALIZED;
  16: i64 outDiscards_ = STAT_UNINITIALIZED;
  17: i64 outErrors_ = STAT_UNINITIALIZED;
  18: i64 outPause_ = STAT_UNINITIALIZED;
  19: i64 outCongestionDiscardPkts_ = STAT_UNINITIALIZED;
  20: map<i16, i64> queueOutDiscardBytes_ = {}
  21: map<i16, i64> queueOutBytes_ = {}
  22: i64 outEcnCounter_ = STAT_UNINITIALIZED
  23: map<i16, i64> queueOutPackets_ = {}
  24: map<i16, i64> queueOutDiscardPackets_ = {}
  25: map<i16, i64> queueWatermarkBytes_ = {}
  26: i64 fecCorrectableErrors =
            STAT_UNINITIALIZED (cpp2.type = "std::uint64_t");
  27: i64 fecUncorrectableErrors =
            STAT_UNINITIALIZED (cpp2.type = "std::uint64_t");

  // seconds from epoch
  50: i64 timestamp_ = STAT_UNINITIALIZED;
}

struct HwTrunkStats {
  1: i64 capacity_ = STAT_UNINITIALIZED

  2: i64 inBytes_ = STAT_UNINITIALIZED
  3: i64 inUnicastPkts_ = STAT_UNINITIALIZED
  4: i64 inMulticastPkts_ = STAT_UNINITIALIZED
  5: i64 inBroadcastPkts_ = STAT_UNINITIALIZED
  6: i64 inDiscards_ = STAT_UNINITIALIZED
  7: i64 inErrors_ = STAT_UNINITIALIZED
  8: i64 inPause_ = STAT_UNINITIALIZED
  9: i64 inIpv4HdrErrors_ = STAT_UNINITIALIZED
  10: i64 inIpv6HdrErrors_ = STAT_UNINITIALIZED
  11: i64 inDstNullDiscards_ = STAT_UNINITIALIZED
  12: i64 inDiscardsRaw_ = STAT_UNINITIALIZED

  13: i64 outBytes_ = STAT_UNINITIALIZED
  14: i64 outUnicastPkts_ = STAT_UNINITIALIZED
  15: i64 outMulticastPkts_ = STAT_UNINITIALIZED
  16: i64 outBroadcastPkts_ = STAT_UNINITIALIZED
  17: i64 outDiscards_ = STAT_UNINITIALIZED
  18: i64 outErrors_ = STAT_UNINITIALIZED
  19: i64 outPause_ = STAT_UNINITIALIZED
  20: i64 outCongestionDiscardPkts_ = STAT_UNINITIALIZED
  21: i64 outEcnCounter_ = STAT_UNINITIALIZED
}
