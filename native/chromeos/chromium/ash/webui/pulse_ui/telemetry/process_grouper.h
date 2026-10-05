// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_WEBUI_PULSE_UI_TELEMETRY_PROCESS_GROUPER_H_
#define ASH_WEBUI_PULSE_UI_TELEMETRY_PROCESS_GROUPER_H_

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ash::pulse {

// Mirrors ash.pulse.mojom.ProcessGroupKind. Kept separate so this file has
// no Mojo dependency and builds in plain unit tests.
enum class GroupKind : int {
  kAsh = 0,
  kChrome = 1,
  kCrostini = 2,
  kArcvm = 3,
  kSystem = 4,
};
inline constexpr size_t kGroupKindCount = 5;

// What Pulse needs from one /proc/<pid>.
struct ProcSample {
  int32_t pid = 0;
  int32_t ppid = 0;
  std::string comm;
  std::vector<std::string> argv;
  uint64_t cpu_ticks = 0;  // utime + stime
  uint64_t start_time_ticks = 0;
  uint32_t threads = 0;
  uint64_t rss_bytes = 0;
};

struct GroupTotals {
  GroupKind kind = GroupKind::kSystem;
  int32_t representative_pid = 0;
  uint32_t process_count = 0;
  uint32_t thread_count = 0;
  // Share of the whole machine: 100 means every core is busy.
  double cpu_percent = 0;
  // Sum of resident set sizes. Shared pages count once per process, so this
  // runs high for Chrome; for crosvm it is the guest memory the VM touched.
  uint64_t rss_bytes = 0;
};

// Classifies one process from its command line, or from its parent when the
// command line says nothing (crosvm device jails are forks of crosvm).
//   crosvm with "arcvm" in its arguments        -> kArcvm
//   crosvm with "termina" in its arguments      -> kCrostini
//   chrome without --type, or --type=gpu-process -> kAsh (the system UI)
//   chrome with any other --type                -> kChrome
//   everything else                              -> kSystem
GroupKind ClassifyProcess(const ProcSample& p,
                          const std::map<int32_t, GroupKind>& parent_kinds);

// Turns successive process lists into per-group CPU shares. Feed it one full
// /proc scan per sample.
class ProcessGrouper {
 public:
  // `ticks_per_second` is sysconf(_SC_CLK_TCK); `cpu_count` the logical
  // cores online.
  ProcessGrouper(uint64_t ticks_per_second, uint32_t cpu_count);
  ~ProcessGrouper();

  ProcessGrouper(const ProcessGrouper&) = delete;
  ProcessGrouper& operator=(const ProcessGrouper&) = delete;

  // `elapsed_seconds` is the wall time since the previous call. The first
  // call reports 0% CPU for every group, since it has nothing to compare.
  // Groups with no processes are left out.
  std::vector<GroupTotals> Update(const std::vector<ProcSample>& processes,
                                  double elapsed_seconds);

 private:
  using ProcessKey = std::pair<int32_t, uint64_t>;  // pid, start time

  const uint64_t ticks_per_second_;
  const uint32_t cpu_count_;
  std::map<ProcessKey, uint64_t> last_ticks_;
  bool first_ = true;
};

}  // namespace ash::pulse

#endif  // ASH_WEBUI_PULSE_UI_TELEMETRY_PROCESS_GROUPER_H_
