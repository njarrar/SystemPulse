// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/webui/pulse_ui/telemetry/process_grouper.h"

#include <algorithm>
#include <string_view>

#include "base/strings/string_util.h"

namespace ash::pulse {

namespace {

std::string_view BaseName(std::string_view path) {
  const size_t slash = path.rfind('/');
  return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

bool IsCrosvm(const ProcSample& p) {
  return p.comm == "crosvm" ||
         (!p.argv.empty() && BaseName(p.argv[0]) == "crosvm");
}

bool IsChrome(const ProcSample& p) {
  return !p.argv.empty() && BaseName(p.argv[0]) == "chrome";
}

bool AnyArgContains(const ProcSample& p, std::string_view needle) {
  for (const std::string& arg : p.argv) {
    if (base::ToLowerASCII(arg).find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

// Value of --type=..., or "" for the browser process.
std::string_view ChromeProcessType(const ProcSample& p) {
  constexpr std::string_view kType = "--type=";
  for (const std::string& arg : p.argv) {
    if (base::StartsWith(arg, kType)) {
      return std::string_view(arg).substr(kType.size());
    }
  }
  return {};
}

}  // namespace

GroupKind ClassifyProcess(const ProcSample& p,
                          const std::map<int32_t, GroupKind>& parent_kinds) {
  if (IsCrosvm(p)) {
    if (AnyArgContains(p, "arcvm")) {
      return GroupKind::kArcvm;
    }
    if (AnyArgContains(p, "termina")) {
      return GroupKind::kCrostini;
    }
    // A device jail whose arguments say nothing: follow the parent.
    auto parent = parent_kinds.find(p.ppid);
    if (parent != parent_kinds.end() &&
        (parent->second == GroupKind::kArcvm ||
         parent->second == GroupKind::kCrostini)) {
      return parent->second;
    }
    return GroupKind::kSystem;
  }
  if (IsChrome(p)) {
    const std::string_view type = ChromeProcessType(p);
    if (type.empty() || type == "gpu-process") {
      return GroupKind::kAsh;
    }
    return GroupKind::kChrome;
  }
  return GroupKind::kSystem;
}

ProcessGrouper::ProcessGrouper(uint64_t ticks_per_second, uint32_t cpu_count)
    : ticks_per_second_(ticks_per_second ? ticks_per_second : 100),
      cpu_count_(cpu_count ? cpu_count : 1) {}

ProcessGrouper::~ProcessGrouper() = default;

std::vector<GroupTotals> ProcessGrouper::Update(
    const std::vector<ProcSample>& processes,
    double elapsed_seconds) {
  // Parents first, so children can inherit a VM group from them.
  std::vector<const ProcSample*> order;
  order.reserve(processes.size());
  for (const ProcSample& p : processes) {
    order.push_back(&p);
  }
  std::sort(order.begin(), order.end(),
            [](const ProcSample* a, const ProcSample* b) {
              return a->start_time_ticks != b->start_time_ticks
                         ? a->start_time_ticks < b->start_time_ticks
                         : a->pid < b->pid;
            });

  std::map<int32_t, GroupKind> kinds;
  std::array<GroupTotals, kGroupKindCount> totals;
  std::array<uint64_t, kGroupKindCount> rep_start{};
  std::array<int32_t, kGroupKindCount> rep_score{};
  for (size_t i = 0; i < kGroupKindCount; ++i) {
    totals[i].kind = static_cast<GroupKind>(i);
  }
  std::map<ProcessKey, uint64_t> now_ticks;
  const double capacity = elapsed_seconds *
                          static_cast<double>(ticks_per_second_) *
                          static_cast<double>(cpu_count_);

  for (const ProcSample* p : order) {
    const GroupKind kind = ClassifyProcess(*p, kinds);
    const size_t k = static_cast<size_t>(kind);
    GroupTotals& t = totals[k];

    // The group's face: the browser process for ash, the root crosvm for a
    // VM, else the oldest process.
    bool root = false;
    if (kind == GroupKind::kAsh) {
      root = ChromeProcessType(*p).empty();
    } else if (kind == GroupKind::kCrostini || kind == GroupKind::kArcvm) {
      auto parent = kinds.find(p->ppid);
      root = parent == kinds.end() || parent->second != kind;
    }
    const int32_t score = root ? 1 : 0;
    if (t.process_count == 0 || score > rep_score[k] ||
        (score == rep_score[k] && p->start_time_ticks < rep_start[k])) {
      t.representative_pid = p->pid;
      rep_score[k] = score;
      rep_start[k] = p->start_time_ticks;
    }

    kinds[p->pid] = kind;
    t.process_count++;
    t.thread_count += p->threads;
    t.rss_bytes += p->rss_bytes;

    const ProcessKey key{p->pid, p->start_time_ticks};
    now_ticks[key] = p->cpu_ticks;
    auto last = last_ticks_.find(key);
    if (!first_ && capacity > 0) {
      // A process born since the last scan counts from zero.
      const uint64_t before = last == last_ticks_.end() ? 0 : last->second;
      const uint64_t delta = p->cpu_ticks >= before ? p->cpu_ticks - before : 0;
      t.cpu_percent += static_cast<double>(delta) / capacity * 100.0;
    }
  }
  last_ticks_ = std::move(now_ticks);
  first_ = false;

  std::vector<GroupTotals> out;
  for (GroupTotals& t : totals) {
    if (t.process_count) {
      t.cpu_percent = std::min(100.0, t.cpu_percent);
      out.push_back(t);
    }
  }
  return out;
}

}  // namespace ash::pulse
