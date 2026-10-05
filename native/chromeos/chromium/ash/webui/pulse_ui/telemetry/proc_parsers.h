// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_WEBUI_PULSE_UI_TELEMETRY_PROC_PARSERS_H_
#define ASH_WEBUI_PULSE_UI_TELEMETRY_PROC_PARSERS_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Pure parsers for the procfs and sysfs files Pulse reads. They take file
// contents and never touch the disk, so tests feed them fixtures.
namespace ash::pulse {

// /proc/meminfo, in KiB. Fields the kernel does not report stay 0.
struct MemInfo {
  uint64_t total_kib = 0;
  uint64_t free_kib = 0;
  uint64_t available_kib = 0;
  uint64_t buffers_kib = 0;
  uint64_t cached_kib = 0;
  uint64_t shmem_kib = 0;
  uint64_t anon_kib = 0;
  uint64_t slab_kib = 0;
  uint64_t kernel_stack_kib = 0;
  uint64_t page_tables_kib = 0;
  uint64_t swap_total_kib = 0;
  uint64_t swap_free_kib = 0;

  // Kernel-owned memory shown as "System" on the Memory card: slab, kernel
  // stacks, page tables and buffers.
  uint64_t SystemKib() const;
};

// Returns nullopt when MemTotal is missing.
std::optional<MemInfo> ParseMemInfo(std::string_view text);

// /sys/block/zram0/mm_stat. Sizes are bytes.
// Columns: orig_data_size compr_data_size mem_used_total mem_limit
// mem_used_max same_pages pages_compacted huge_pages [huge_pages_since].
struct ZramMmStat {
  uint64_t orig_data_size = 0;
  uint64_t compr_data_size = 0;
  uint64_t mem_used_total = 0;
  uint64_t mem_limit = 0;
  uint64_t mem_used_max = 0;
  uint64_t same_pages = 0;
  uint64_t pages_compacted = 0;
  uint64_t huge_pages = 0;

  // orig_data_size / compr_data_size, or 0 when nothing is stored.
  double CompressionRatio() const;
};

// Needs at least the first three columns.
std::optional<ZramMmStat> ParseZramMmStat(std::string_view text);

// /proc/<pid>/stat. `comm` may hold spaces and parentheses, so fields after
// it are read from the last ')'.
struct PidStat {
  int32_t pid = 0;
  std::string comm;
  char state = '?';
  int32_t ppid = 0;
  uint64_t utime_ticks = 0;
  uint64_t stime_ticks = 0;
  uint32_t num_threads = 0;
  uint64_t start_time_ticks = 0;
  uint64_t rss_pages = 0;
};

std::optional<PidStat> ParsePidStat(std::string_view text);

// /proc/<pid>/cmdline: NUL-separated arguments. A trailing NUL is fine.
std::vector<std::string> SplitCmdline(std::string_view raw);

// One row of /proc/net/dev.
struct NetDevCounters {
  std::string interface_name;
  uint64_t rx_bytes = 0;
  uint64_t tx_bytes = 0;
};

std::vector<NetDevCounters> ParseNetDev(std::string_view text);

// First field of /proc/loadavg.
std::optional<double> ParseLoadAvg1(std::string_view text);

// Filesystem type of `mount_point` in /proc/mounts ("ext4"), or "" when the
// mount point is not listed. The last matching line wins, like the kernel.
std::string FilesystemForMount(std::string_view proc_mounts,
                               std::string_view mount_point);

// /sys/class/devfreq/<dev>/load on kernels that expose it: "37@950000000Hz"
// gives 37.
std::optional<double> ParseDevfreqLoad(std::string_view text);

}  // namespace ash::pulse

#endif  // ASH_WEBUI_PULSE_UI_TELEMETRY_PROC_PARSERS_H_
