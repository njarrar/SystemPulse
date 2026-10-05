// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/webui/pulse_ui/telemetry/process_grouper.h"

#include <map>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace ash::pulse {
namespace {

ProcSample Proc(int32_t pid,
                int32_t ppid,
                std::vector<std::string> argv,
                uint64_t ticks,
                uint64_t start,
                uint64_t rss_mib = 10,
                uint32_t threads = 4) {
  ProcSample p;
  p.pid = pid;
  p.ppid = ppid;
  p.argv = std::move(argv);
  p.comm = p.argv.empty() ? "kworker" : p.argv[0].substr(p.argv[0].rfind('/') + 1);
  p.cpu_ticks = ticks;
  p.start_time_ticks = start;
  p.rss_bytes = rss_mib << 20;
  p.threads = threads;
  return p;
}

const char kChrome[] = "/opt/google/chrome/chrome";
const char kCrosvm[] = "/usr/bin/crosvm";

std::vector<ProcSample> Machine(uint64_t t) {
  return {
      Proc(1, 0, {"/sbin/init"}, 10 * t, 1),
      Proc(611, 1, {kChrome, "--login-manager"}, 20 * t, 50, 400, 29),
      Proc(640, 611, {kChrome, "--type=gpu-process"}, 15 * t, 60, 200),
      Proc(700, 611, {kChrome, "--type=zygote"}, 0, 61),
      Proc(1402, 700, {kChrome, "--type=renderer"}, 30 * t, 70, 300),
      Proc(1410, 700, {kChrome, "--type=renderer"}, 31 * t, 71, 300),
      Proc(2901, 1, {kCrosvm, "run", "/run/imageloader/cros-termina/vm_kernel"},
           500 * t, 100, 1400, 40),
      Proc(2950, 2901, {kCrosvm, "device", "block"}, 16 * t, 101, 20),
      Proc(3318, 1, {kCrosvm, "run", "--syslog-tag", "ARCVM(32)"}, 25 * t, 90,
           1100, 60),
      Proc(3320, 3318, {kCrosvm, "device", "gpu"}, 5 * t, 91, 30),
      Proc(4000, 1, {kCrosvm, "run", "borealis"}, 1 * t, 120),
  };
}

TEST(PulseProcessGrouperTest, Classify) {
  std::map<int32_t, GroupKind> parents;
  EXPECT_EQ(GroupKind::kAsh,
            ClassifyProcess(Proc(1, 0, {kChrome, "--login-manager"}, 0, 0), parents));
  EXPECT_EQ(GroupKind::kAsh,
            ClassifyProcess(Proc(2, 1, {kChrome, "--type=gpu-process"}, 0, 0), parents));
  EXPECT_EQ(GroupKind::kChrome,
            ClassifyProcess(Proc(3, 1, {kChrome, "--type=renderer"}, 0, 0), parents));
  EXPECT_EQ(GroupKind::kArcvm,
            ClassifyProcess(Proc(4, 1, {kCrosvm, "--syslog-tag", "ARCVM(32)"}, 0, 0), parents));
  EXPECT_EQ(GroupKind::kCrostini,
            ClassifyProcess(Proc(5, 1, {kCrosvm, "/run/vm/termina.img"}, 0, 0), parents));
  EXPECT_EQ(GroupKind::kSystem,
            ClassifyProcess(Proc(6, 1, {"/usr/bin/powerd"}, 0, 0), parents));
  parents[7] = GroupKind::kArcvm;
  EXPECT_EQ(GroupKind::kArcvm,
            ClassifyProcess(Proc(8, 7, {kCrosvm, "device"}, 0, 0), parents));
}

TEST(PulseProcessGrouperTest, SharesAndTotals) {
  // 100 ticks per second, 8 cores, 10 s between scans: 8000 ticks = 100%.
  ProcessGrouper grouper(100, 8);
  std::vector<GroupTotals> first = grouper.Update(Machine(1), 10);
  for (const GroupTotals& g : first) {
    EXPECT_EQ(0.0, g.cpu_percent);
  }
  std::vector<GroupTotals> groups = grouper.Update(Machine(9), 10);
  std::map<GroupKind, GroupTotals> by;
  for (const GroupTotals& g : groups) {
    by[g.kind] = g;
  }
  ASSERT_EQ(5u, by.size());
  // Crostini: (500 + 16) * 8 ticks of 8000.
  EXPECT_NEAR(51.6, by[GroupKind::kCrostini].cpu_percent, 1e-9);
  EXPECT_EQ(2u, by[GroupKind::kCrostini].process_count);
  EXPECT_EQ(2901, by[GroupKind::kCrostini].representative_pid);
  EXPECT_EQ(1420ull << 20, by[GroupKind::kCrostini].rss_bytes);
  EXPECT_NEAR(3.0, by[GroupKind::kArcvm].cpu_percent, 1e-9);
  EXPECT_EQ(3318, by[GroupKind::kArcvm].representative_pid);
  EXPECT_EQ(611, by[GroupKind::kAsh].representative_pid);
  EXPECT_EQ(2u, by[GroupKind::kAsh].process_count);
  EXPECT_EQ(3u, by[GroupKind::kChrome].process_count);
  EXPECT_NEAR(6.1, by[GroupKind::kChrome].cpu_percent, 1e-9);
  // init and the borealis VM.
  EXPECT_EQ(2u, by[GroupKind::kSystem].process_count);
}

TEST(PulseProcessGrouperTest, NewProcessAndPidReuse) {
  ProcessGrouper grouper(100, 1);
  grouper.Update({Proc(10, 1, {"/bin/a"}, 100, 5)}, 1);
  // pid 10 exited and came back with a new start time: count from zero.
  std::vector<GroupTotals> g =
      grouper.Update({Proc(10, 1, {"/bin/a"}, 50, 7)}, 1);
  ASSERT_EQ(1u, g.size());
  EXPECT_NEAR(50.0, g[0].cpu_percent, 1e-9);
  // Counters never go backwards into a negative share, and cap at 100%.
  g = grouper.Update({Proc(10, 1, {"/bin/a"}, 10, 7)}, 1);
  EXPECT_EQ(0.0, g[0].cpu_percent);
  g = grouper.Update({Proc(10, 1, {"/bin/a"}, 1000, 7)}, 1);
  EXPECT_EQ(100.0, g[0].cpu_percent);
}

}  // namespace
}  // namespace ash::pulse
