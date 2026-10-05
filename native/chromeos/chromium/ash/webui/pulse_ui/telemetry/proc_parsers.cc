// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/webui/pulse_ui/telemetry/proc_parsers.h"

#include <string_view>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"

namespace ash::pulse {

namespace {

std::vector<std::string_view> Fields(std::string_view line) {
  return base::SplitStringPiece(line, base::kWhitespaceASCII,
                                base::TRIM_WHITESPACE,
                                base::SPLIT_WANT_NONEMPTY);
}

std::vector<std::string_view> Lines(std::string_view text) {
  return base::SplitStringPiece(text, "\n", base::TRIM_WHITESPACE,
                                base::SPLIT_WANT_NONEMPTY);
}

bool ToU64(std::string_view s, uint64_t* out) {
  return base::StringToUint64(s, out);
}

}  // namespace

uint64_t MemInfo::SystemKib() const {
  return slab_kib + kernel_stack_kib + page_tables_kib + buffers_kib;
}

std::optional<MemInfo> ParseMemInfo(std::string_view text) {
  MemInfo info;
  bool has_total = false;
  uint64_t sreclaimable = 0;
  uint64_t sunreclaim = 0;
  for (std::string_view line : Lines(text)) {
    const size_t colon = line.find(':');
    if (colon == std::string_view::npos) {
      continue;
    }
    const std::string_view key = line.substr(0, colon);
    const std::vector<std::string_view> rest = Fields(line.substr(colon + 1));
    uint64_t value = 0;
    if (rest.empty() || !ToU64(rest[0], &value)) {
      continue;
    }
    if (key == "MemTotal") {
      info.total_kib = value;
      has_total = true;
    } else if (key == "MemFree") {
      info.free_kib = value;
    } else if (key == "MemAvailable") {
      info.available_kib = value;
    } else if (key == "Buffers") {
      info.buffers_kib = value;
    } else if (key == "Cached") {
      info.cached_kib = value;
    } else if (key == "Shmem") {
      info.shmem_kib = value;
    } else if (key == "AnonPages") {
      info.anon_kib = value;
    } else if (key == "Slab") {
      info.slab_kib = value;
    } else if (key == "SReclaimable") {
      sreclaimable = value;
    } else if (key == "SUnreclaim") {
      sunreclaim = value;
    } else if (key == "KernelStack") {
      info.kernel_stack_kib = value;
    } else if (key == "PageTables") {
      info.page_tables_kib = value;
    } else if (key == "SwapTotal") {
      info.swap_total_kib = value;
    } else if (key == "SwapFree") {
      info.swap_free_kib = value;
    }
  }
  if (!has_total) {
    return std::nullopt;
  }
  if (!info.slab_kib) {
    info.slab_kib = sreclaimable + sunreclaim;
  }
  // Kernels before 3.14 lack MemAvailable; estimate it the old way.
  if (!info.available_kib) {
    info.available_kib = info.free_kib + info.buffers_kib + info.cached_kib;
  }
  return info;
}

double ZramMmStat::CompressionRatio() const {
  return compr_data_size ? static_cast<double>(orig_data_size) /
                               static_cast<double>(compr_data_size)
                         : 0.0;
}

std::optional<ZramMmStat> ParseZramMmStat(std::string_view text) {
  const std::vector<std::string_view> f = Fields(text);
  if (f.size() < 3) {
    return std::nullopt;
  }
  uint64_t v[8] = {};
  for (size_t i = 0; i < f.size() && i < 8; ++i) {
    if (!ToU64(f[i], &v[i])) {
      return std::nullopt;
    }
  }
  ZramMmStat stat;
  stat.orig_data_size = v[0];
  stat.compr_data_size = v[1];
  stat.mem_used_total = v[2];
  stat.mem_limit = v[3];
  stat.mem_used_max = v[4];
  stat.same_pages = v[5];
  stat.pages_compacted = v[6];
  stat.huge_pages = v[7];
  return stat;
}

std::optional<PidStat> ParsePidStat(std::string_view text) {
  const size_t open = text.find('(');
  const size_t close = text.rfind(')');
  if (open == std::string_view::npos || close == std::string_view::npos ||
      close < open) {
    return std::nullopt;
  }
  PidStat stat;
  uint64_t pid = 0;
  if (!ToU64(base::TrimWhitespaceASCII(text.substr(0, open), base::TRIM_ALL),
             &pid)) {
    return std::nullopt;
  }
  stat.pid = static_cast<int32_t>(pid);
  stat.comm = std::string(text.substr(open + 1, close - open - 1));
  // Fields after comm, numbered from 3 as in proc(5): state is 3, ppid 4,
  // utime 14, stime 15, num_threads 20, starttime 22, rss 24.
  const std::vector<std::string_view> f = Fields(text.substr(close + 1));
  if (f.size() < 22) {
    return std::nullopt;
  }
  auto at = [&f](size_t proc_field) { return f[proc_field - 3]; };
  stat.state = at(3).empty() ? '?' : at(3)[0];
  uint64_t ppid = 0;
  uint64_t threads = 0;
  if (!ToU64(at(4), &ppid) || !ToU64(at(14), &stat.utime_ticks) ||
      !ToU64(at(15), &stat.stime_ticks) || !ToU64(at(20), &threads) ||
      !ToU64(at(22), &stat.start_time_ticks)) {
    return std::nullopt;
  }
  stat.ppid = static_cast<int32_t>(ppid);
  stat.num_threads = static_cast<uint32_t>(threads);
  // rss is signed in the kernel; a negative value means 0 here.
  if (!ToU64(at(24), &stat.rss_pages)) {
    stat.rss_pages = 0;
  }
  return stat;
}

std::vector<std::string> SplitCmdline(std::string_view raw) {
  std::vector<std::string> out;
  for (std::string_view part :
       base::SplitStringPiece(raw, std::string_view("\0", 1),
                              base::KEEP_WHITESPACE,
                              base::SPLIT_WANT_NONEMPTY)) {
    out.emplace_back(part);
  }
  return out;
}

std::vector<NetDevCounters> ParseNetDev(std::string_view text) {
  // Two header lines, then "  iface: rx_bytes rx_packets ... tx_bytes ...",
  // with tx_bytes the ninth number.
  std::vector<NetDevCounters> out;
  for (std::string_view line : Lines(text)) {
    const size_t colon = line.find(':');
    if (colon == std::string_view::npos || line.find('|') != line.npos) {
      continue;
    }
    const std::vector<std::string_view> f = Fields(line.substr(colon + 1));
    NetDevCounters c;
    c.interface_name = std::string(
        base::TrimWhitespaceASCII(line.substr(0, colon), base::TRIM_ALL));
    if (f.size() < 9 || !ToU64(f[0], &c.rx_bytes) ||
        !ToU64(f[8], &c.tx_bytes)) {
      continue;
    }
    out.push_back(std::move(c));
  }
  return out;
}

std::optional<double> ParseLoadAvg1(std::string_view text) {
  const std::vector<std::string_view> f = Fields(text);
  double value = 0;
  if (f.empty() || !base::StringToDouble(f[0], &value)) {
    return std::nullopt;
  }
  return value;
}

std::string FilesystemForMount(std::string_view proc_mounts,
                               std::string_view mount_point) {
  std::string type;
  for (std::string_view line : Lines(proc_mounts)) {
    const std::vector<std::string_view> f = Fields(line);
    if (f.size() >= 3 && f[1] == mount_point) {
      type = std::string(f[2]);
    }
  }
  return type;
}

std::optional<double> ParseDevfreqLoad(std::string_view text) {
  const std::string_view trimmed =
      base::TrimWhitespaceASCII(text, base::TRIM_ALL);
  const size_t at = trimmed.find('@');
  uint64_t load = 0;
  if (!ToU64(trimmed.substr(0, at), &load) || load > 100) {
    return std::nullopt;
  }
  return static_cast<double>(load);
}

}  // namespace ash::pulse
