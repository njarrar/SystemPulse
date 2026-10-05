// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/webui/pulse_ui/telemetry/proc_parsers.h"

#include <string>

#include "testing/gtest/include/gtest/gtest.h"

namespace ash::pulse {
namespace {

// Trimmed /proc/meminfo from a 16 GB MT8186 Chromebook.
constexpr char kMemInfo[] =
    "MemTotal:       16165012 kB\n"
    "MemFree:         5983204 kB\n"
    "MemAvailable:    6789120 kB\n"
    "Buffers:          112640 kB\n"
    "Cached:          3512448 kB\n"
    "SwapCached:        84212 kB\n"
    "Active:          5120000 kB\n"
    "Shmem:            401232 kB\n"
    "AnonPages:       5823004 kB\n"
    "Slab:             612352 kB\n"
    "SReclaimable:     301056 kB\n"
    "SUnreclaim:       311296 kB\n"
    "KernelStack:       28672 kB\n"
    "PageTables:       118784 kB\n"
    "SwapTotal:      23697404 kB\n"
    "SwapFree:       19812352 kB\n"
    "HugePages_Total:       0\n";

TEST(PulseProcParsersTest, MemInfo) {
  std::optional<MemInfo> m = ParseMemInfo(kMemInfo);
  ASSERT_TRUE(m.has_value());
  EXPECT_EQ(16165012u, m->total_kib);
  EXPECT_EQ(6789120u, m->available_kib);
  EXPECT_EQ(5823004u, m->anon_kib);
  EXPECT_EQ(612352u, m->slab_kib);
  EXPECT_EQ(612352u + 28672u + 118784u + 112640u, m->SystemKib());
  EXPECT_EQ(23697404u, m->swap_total_kib);
}

TEST(PulseProcParsersTest, MemInfoWithoutSlabOrAvailable) {
  std::optional<MemInfo> m = ParseMemInfo(
      "MemTotal: 1000 kB\nMemFree: 100 kB\nBuffers: 10 kB\nCached: 200 kB\n"
      "SReclaimable: 30 kB\nSUnreclaim: 20 kB\n");
  ASSERT_TRUE(m.has_value());
  EXPECT_EQ(50u, m->slab_kib);
  EXPECT_EQ(310u, m->available_kib);
}

TEST(PulseProcParsersTest, MemInfoNeedsTotal) {
  EXPECT_FALSE(ParseMemInfo("MemFree: 100 kB\n").has_value());
  EXPECT_FALSE(ParseMemInfo("").has_value());
}

TEST(PulseProcParsersTest, ZramMmStat) {
  // 3.7 GiB stored in 1.19 GiB, 1.2 GiB resident.
  std::optional<ZramMmStat> z = ParseZramMmStat(
      "3972844748 1277752115 1288490188        0 1395864371   201733"
      "        0    10563     2112\n");
  ASSERT_TRUE(z.has_value());
  EXPECT_EQ(3972844748u, z->orig_data_size);
  EXPECT_EQ(1277752115u, z->compr_data_size);
  EXPECT_EQ(1288490188u, z->mem_used_total);
  EXPECT_EQ(10563u, z->huge_pages);
  EXPECT_NEAR(3.109, z->CompressionRatio(), 0.001);
}

TEST(PulseProcParsersTest, ZramMmStatShortAndEmpty) {
  std::optional<ZramMmStat> z = ParseZramMmStat("0 0 4096\n");
  ASSERT_TRUE(z.has_value());
  EXPECT_EQ(0.0, z->CompressionRatio());
  EXPECT_FALSE(ParseZramMmStat("12 34").has_value());
  EXPECT_FALSE(ParseZramMmStat("a b c d").has_value());
}

TEST(PulseProcParsersTest, PidStatWithAwkwardComm) {
  std::optional<PidStat> s = ParsePidStat(
      "2901 (crosvm (vcpu) 1) S 1 2901 2901 0 -1 4194560 81231 0 12 0 "
      "91234 20345 0 0 20 0 96 0 51234 4026531840 372736 18446744073709551615"
      " 1 1 0 0 0 0 0 4096 0 0 0 0 17 3 0 0 0 0 0\n");
  ASSERT_TRUE(s.has_value());
  EXPECT_EQ(2901, s->pid);
  EXPECT_EQ("crosvm (vcpu) 1", s->comm);
  EXPECT_EQ('S', s->state);
  EXPECT_EQ(1, s->ppid);
  EXPECT_EQ(91234u, s->utime_ticks);
  EXPECT_EQ(20345u, s->stime_ticks);
  EXPECT_EQ(96u, s->num_threads);
  EXPECT_EQ(51234u, s->start_time_ticks);
  EXPECT_EQ(372736u, s->rss_pages);
}

TEST(PulseProcParsersTest, PidStatRejectsGarbage) {
  EXPECT_FALSE(ParsePidStat("").has_value());
  EXPECT_FALSE(ParsePidStat("12 (x) S 1 2").has_value());
  EXPECT_FALSE(ParsePidStat("x (y) S").has_value());
}

TEST(PulseProcParsersTest, Cmdline) {
  const std::string raw("/usr/bin/crosvm\0run\0--syslog-tag\0ARCVM(32)\0", 42);
  std::vector<std::string> argv = SplitCmdline(raw);
  ASSERT_EQ(4u, argv.size());
  EXPECT_EQ("/usr/bin/crosvm", argv[0]);
  EXPECT_EQ("ARCVM(32)", argv[3]);
  EXPECT_TRUE(SplitCmdline("").empty());
}

TEST(PulseProcParsersTest, NetDev) {
  std::vector<NetDevCounters> rows = ParseNetDev(
      "Inter-|   Receive                                                |  Transmit\n"
      " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
      "    lo:  123456     100    0    0    0     0          0         0   123456     100    0    0    0     0       0          0\n"
      " wlan0: 1503238553 1200000 0 12 0 0 0 0 222298112 800000 0 0 0 0 0 0\n");
  ASSERT_EQ(2u, rows.size());
  EXPECT_EQ("wlan0", rows[1].interface_name);
  EXPECT_EQ(1503238553u, rows[1].rx_bytes);
  EXPECT_EQ(222298112u, rows[1].tx_bytes);
}

TEST(PulseProcParsersTest, LoadAvgMountsDevfreq) {
  EXPECT_DOUBLE_EQ(5.89, *ParseLoadAvg1("5.89 4.12 3.01 3/912 31337\n"));
  EXPECT_FALSE(ParseLoadAvg1("").has_value());
  EXPECT_EQ("ext4",
            FilesystemForMount("/dev/mmcblk0p1 /mnt/stateful_partition ext4 "
                               "rw,nosuid 0 0\ntmpfs /run tmpfs rw 0 0\n",
                               "/mnt/stateful_partition"));
  EXPECT_EQ("", FilesystemForMount("tmpfs /run tmpfs rw 0 0\n", "/home"));
  EXPECT_DOUBLE_EQ(37.0, *ParseDevfreqLoad("37@950000000Hz\n"));
  EXPECT_FALSE(ParseDevfreqLoad("garbage").has_value());
}

}  // namespace
}  // namespace ash::pulse
