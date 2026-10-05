// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/webui/pulse_ui/telemetry/pulse_sampler.h"

#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <string_view>

#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/memory/memory_pressure_monitor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/system/sys_info.h"
#include "base/task/thread_pool.h"
#include "chromeos/ash/services/cros_healthd/public/cpp/service_connection.h"
#include "chromeos/dbus/power/power_manager_client.h"
#include "chromeos/dbus/power_manager/power_supply_properties.pb.h"
#include "chromeos/ash/services/cros_healthd/public/mojom/cros_healthd.mojom.h"
#include "chromeos/services/network_health/public/mojom/network_health_types.mojom.h"
#include "content/public/browser/gpu_data_manager.h"
#include "gpu/config/gpu_info.h"

namespace ash::pulse {

namespace {

namespace healthd = ::ash::cros_healthd::mojom;
namespace network_health = ::chromeos::network_health::mojom;

constexpr char kStatefulMount[] = "/mnt/stateful_partition";
constexpr double kKib = 1024.0;

std::string ReadSmallFile(const base::FilePath& path) {
  std::string text;
  if (!base::ReadFileToStringWithMaxSize(path, &text, 1 << 20)) {
    text.clear();
  }
  return text;
}

bool IsPidDir(const base::FilePath& path) {
  const std::string name = path.BaseName().value();
  return !name.empty() &&
         std::all_of(name.begin(), name.end(), base::IsAsciiDigit<char>);
}

// The GPU's devfreq node: Mali and other SoC GPUs register one whose name
// mentions the GPU.
void ReadGpuDevfreq(ProcfsReadings* out) {
  base::FileEnumerator e(base::FilePath("/sys/class/devfreq"),
                         /*recursive=*/false,
                         base::FileEnumerator::DIRECTORIES |
                             base::FileEnumerator::SHOW_SYM_LINKS);
  for (base::FilePath dir = e.Next(); !dir.empty(); dir = e.Next()) {
    const std::string name = base::ToLowerASCII(dir.BaseName().value());
    const std::string compat = base::ToLowerASCII(
        ReadSmallFile(dir.Append("device/of_node/compatible")));
    if (name.find("gpu") == std::string::npos &&
        compat.find("mali") == std::string::npos &&
        compat.find("gpu") == std::string::npos) {
      continue;
    }
    uint64_t hz = 0;
    if (base::StringToUint64(
            base::TrimWhitespaceASCII(ReadSmallFile(dir.Append("cur_freq")),
                                      base::TRIM_ALL),
            &hz)) {
      out->gpu_clock_mhz = static_cast<uint32_t>(hz / 1000000);
    }
    out->gpu_load_percent = ParseDevfreqLoad(ReadSmallFile(dir.Append("load")));
    return;
  }
}

void ReadProcesses(ProcfsReadings* out) {
  const uint64_t page_size = static_cast<uint64_t>(sysconf(_SC_PAGESIZE));
  base::FileEnumerator e(base::FilePath("/proc"), /*recursive=*/false,
                         base::FileEnumerator::DIRECTORIES);
  for (base::FilePath dir = e.Next(); !dir.empty(); dir = e.Next()) {
    if (!IsPidDir(dir)) {
      continue;
    }
    std::optional<PidStat> stat = ParsePidStat(ReadSmallFile(dir.Append("stat")));
    if (!stat || stat->state == 'Z') {
      continue;  // Exited between listing and reading, or a zombie.
    }
    ProcSample p;
    p.pid = stat->pid;
    p.ppid = stat->ppid;
    p.comm = stat->comm;
    p.argv = SplitCmdline(ReadSmallFile(dir.Append("cmdline")));
    p.cpu_ticks = stat->utime_ticks + stat->stime_ticks;
    p.start_time_ticks = stat->start_time_ticks;
    p.threads = stat->num_threads;
    p.rss_bytes = stat->rss_pages * page_size;
    out->processes.push_back(std::move(p));
  }
}

double Percent(uint64_t part, uint64_t whole) {
  return whole ? static_cast<double>(part) / static_cast<double>(whole) * 100.0
               : 0.0;
}

mojom::ProcessGroupKind ToMojom(GroupKind kind) {
  return static_cast<mojom::ProcessGroupKind>(static_cast<int>(kind));
}

// English names. The page shows its own localized names by kind.
const char* GroupName(GroupKind kind) {
  switch (kind) {
    case GroupKind::kAsh:
      return "System UI";
    case GroupKind::kChrome:
      return "Chrome";
    case GroupKind::kCrostini:
      return "Linux (Crostini)";
    case GroupKind::kArcvm:
      return "Android (ARCVM)";
    case GroupKind::kSystem:
      return "System";
  }
  return "";
}

mojom::ThermalLevel LevelFor(double celsius, bool throttled) {
  if (throttled) {
    return mojom::ThermalLevel::kThrottled;
  }
  if (celsius < 48) {
    return mojom::ThermalLevel::kCool;
  }
  if (celsius < 70) {
    return mojom::ThermalLevel::kWarm;
  }
  return celsius < 90 ? mojom::ThermalLevel::kHot
                      : mojom::ThermalLevel::kThrottled;
}

bool SensorMatches(const std::string& name,
                   std::initializer_list<std::string_view> needles) {
  const std::string lower = base::ToLowerASCII(name);
  for (std::string_view n : needles) {
    if (lower.find(n) != std::string::npos) {
      return true;
    }
  }
  return false;
}

}  // namespace

ProcfsReadings::ProcfsReadings() = default;
ProcfsReadings::ProcfsReadings(ProcfsReadings&&) = default;
ProcfsReadings& ProcfsReadings::operator=(ProcfsReadings&&) = default;
ProcfsReadings::~ProcfsReadings() = default;

ProcfsReadings ReadProcfs() {
  ProcfsReadings r;
  r.meminfo = ParseMemInfo(ReadSmallFile(base::FilePath("/proc/meminfo")));
  r.zram = ParseZramMmStat(
      ReadSmallFile(base::FilePath("/sys/block/zram0/mm_stat")));
  r.load_average_1m =
      ParseLoadAvg1(ReadSmallFile(base::FilePath("/proc/loadavg")));
  r.net = ParseNetDev(ReadSmallFile(base::FilePath("/proc/net/dev")));
  r.stateful_filesystem = FilesystemForMount(
      ReadSmallFile(base::FilePath("/proc/mounts")), kStatefulMount);
  const base::FilePath stateful(kStatefulMount);
  r.stateful_total_bytes = base::SysInfo::AmountOfTotalDiskSpace(stateful);
  r.stateful_free_bytes = base::SysInfo::AmountOfFreeDiskSpace(stateful);
  ReadGpuDevfreq(&r);
  ReadProcesses(&r);
  r.ticks_per_second = static_cast<uint64_t>(sysconf(_SC_CLK_TCK));
  r.cpu_count = static_cast<uint32_t>(base::SysInfo::NumberOfProcessors());
  return r;
}

// static
PulseSampler* PulseSampler::Get() {
  static base::NoDestructor<PulseSampler> instance;
  return instance.get();
}

PulseSampler::PulseSampler() {
  Sample();
}

PulseSampler::~PulseSampler() = default;

void PulseSampler::AddObserver(Observer* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const bool was_idle = observers_.empty();
  observers_.AddObserver(observer);
  if (latest_) {
    observer->OnSnapshot(*latest_);
  }
  if (was_idle) {
    // Switch to the fast cadence now rather than at the next slow tick.
    timer_.Stop();
    Sample();
  }
}

void PulseSampler::RemoveObserver(Observer* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.RemoveObserver(observer);
}

void PulseSampler::ScheduleNext() {
  timer_.Start(FROM_HERE,
               observers_.empty() ? kHistoryInterval : kFastInterval,
               base::BindOnce(&PulseSampler::Sample, base::Unretained(this)));
}

void PulseSampler::Sample() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const uint64_t round = ++round_;
  pending_procfs_.reset();
  pending_fast_.reset();
  fast_done_ = false;

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
      base::BindOnce(&ReadProcfs),
      base::BindOnce(&PulseSampler::OnProcfs, weak_factory_.GetWeakPtr(),
                     round));

  auto* probe = cros_healthd::ServiceConnection::GetInstance()->GetProbeService();
  probe->ProbeTelemetryInfo(
      {healthd::ProbeCategoryEnum::kCpu, healthd::ProbeCategoryEnum::kThermal,
       healthd::ProbeCategoryEnum::kFan, healthd::ProbeCategoryEnum::kBattery},
      base::BindOnce(&PulseSampler::OnFastTelemetry,
                     weak_factory_.GetWeakPtr(), round));

  const base::TimeTicks now = base::TimeTicks::Now();
  if (last_slow_probe_.is_null() || now - last_slow_probe_ >= kHistoryInterval) {
    last_slow_probe_ = now;
    probe->ProbeTelemetryInfo(
        {healthd::ProbeCategoryEnum::kNetwork,
         healthd::ProbeCategoryEnum::kNonRemovableBlockDevices},
        base::BindOnce(&PulseSampler::OnSlowTelemetry,
                       weak_factory_.GetWeakPtr()));
  }
}

void PulseSampler::OnProcfs(uint64_t round, ProcfsReadings readings) {
  if (round != round_) {
    return;
  }
  pending_procfs_ = std::move(readings);
  MaybeFinishRound(round);
}

void PulseSampler::OnFastTelemetry(uint64_t round,
                                   healthd::TelemetryInfoPtr info) {
  if (round != round_) {
    return;
  }
  pending_fast_ = std::move(info);
  fast_done_ = true;
  MaybeFinishRound(round);
}

void PulseSampler::OnSlowTelemetry(healthd::TelemetryInfoPtr info) {
  slow_ = std::move(info);
}

void PulseSampler::MaybeFinishRound(uint64_t round) {
  if (round != round_ || !pending_procfs_ || !fast_done_) {
    return;
  }
  const base::TimeTicks now = base::TimeTicks::Now();
  const double elapsed =
      last_round_time_.is_null() ? 0.0 : (now - last_round_time_).InSecondsF();
  last_round_time_ = now;
  const ProcfsReadings& procfs = *pending_procfs_;

  if (!grouper_) {
    grouper_.emplace(procfs.ticks_per_second, procfs.cpu_count);
  }
  const std::vector<GroupTotals> groups =
      grouper_->Update(procfs.processes, elapsed);

  const healthd::CpuInfo* cpu = nullptr;
  const healthd::BatteryInfo* battery = nullptr;
  const healthd::ThermalInfo* thermal = nullptr;
  std::vector<uint32_t> fans;
  if (pending_fast_) {
    if (pending_fast_->cpu_result && pending_fast_->cpu_result->is_cpu_info()) {
      cpu = pending_fast_->cpu_result->get_cpu_info().get();
    }
    if (pending_fast_->battery_result &&
        pending_fast_->battery_result->is_battery_info()) {
      battery = pending_fast_->battery_result->get_battery_info().get();
    }
    if (pending_fast_->thermal_result &&
        pending_fast_->thermal_result->is_thermal_info()) {
      thermal = pending_fast_->thermal_result->get_thermal_info().get();
    }
    if (pending_fast_->fan_result && pending_fast_->fan_result->is_fan_info()) {
      for (const auto& fan : pending_fast_->fan_result->get_fan_info()) {
        fans.push_back(fan->speed_rpm);
      }
    }
  }

  auto snapshot = mojom::Snapshot::New();
  snapshot->cpu = BuildCpu(cpu, procfs);
  snapshot->memory = BuildMemory(procfs, groups);
  snapshot->battery = BuildBattery(battery);
  std::optional<double> battery_celsius;
  if (thermal) {
    for (const auto& s : thermal->thermal_sensors) {
      if (SensorMatches(s->name, {"battery", "charger"})) {
        battery_celsius = s->temperature_celsius;
      }
    }
  }
  snapshot->thermal = BuildThermal(cpu, thermal, battery_celsius);
  snapshot->fans = mojom::FanSnapshot::New(std::move(fans));
  snapshot->storage = BuildStorage(procfs, elapsed);
  snapshot->network = BuildNetwork(procfs, elapsed);
  snapshot->gpu = BuildGpu(procfs);
  snapshot->groups = BuildGroups(groups);
  snapshot->hog = UpdateHog(groups, now);

  if (last_history_.is_null() || now - last_history_ >= kHistoryInterval) {
    last_history_ = now;
    RecordHistory(*snapshot);
  }
  latest_ = std::move(snapshot);
  pending_procfs_.reset();
  pending_fast_.reset();
  for (Observer& observer : observers_) {
    observer.OnSnapshot(*latest_);
  }
  ScheduleNext();
}

mojom::CpuSnapshotPtr PulseSampler::BuildCpu(const healthd::CpuInfo* cpu,
                                             const ProcfsReadings& procfs) {
  auto out = mojom::CpuSnapshot::New();
  out->logical_cores = procfs.cpu_count;
  out->physical_cores = procfs.cpu_count;
  out->load_average_1m = procfs.load_average_1m.value_or(0);
  if (!cpu) {
    return out;
  }
  // Busy share per logical CPU since the previous round, from the
  // user/system/idle times cros_healthd reads out of /proc/stat.
  uint64_t d_user = 0;
  uint64_t d_system = 0;
  uint64_t d_total = 0;
  std::map<uint32_t, std::pair<double, int>> by_clock;  // max kHz -> sum, n
  std::set<std::pair<uint32_t, uint32_t>> cores;
  uint32_t logical = 0;
  for (uint32_t p = 0; p < cpu->physical_cpus.size(); ++p) {
    const auto& physical = cpu->physical_cpus[p];
    if (p == 0 && physical->model_name) {
      out->model_name = *physical->model_name;
    }
    for (uint32_t l = 0; l < physical->logical_cpus.size(); ++l) {
      const auto& lc = physical->logical_cpus[l];
      ++logical;
      cores.insert({p, lc->core_id});
      const CpuTimes now{lc->user_time_user_hz, lc->system_time_user_hz,
                         lc->user_time_user_hz + lc->system_time_user_hz +
                             lc->idle_time_user_hz};
      auto last = last_cpu_times_.find({p, l});
      if (last != last_cpu_times_.end() && now.total > last->second.total) {
        const CpuTimes& was = last->second;
        const uint64_t du = now.user >= was.user ? now.user - was.user : 0;
        const uint64_t ds =
            now.system >= was.system ? now.system - was.system : 0;
        const uint64_t dt = now.total - was.total;
        d_user += du;
        d_system += ds;
        d_total += dt;
        auto& slot = by_clock[lc->max_clock_speed_khz];
        slot.first += Percent(du + ds, dt);
        slot.second += 1;
      }
      last_cpu_times_[{p, l}] = now;
    }
  }
  out->logical_cores = logical ? logical : procfs.cpu_count;
  out->physical_cores = cores.empty() ? out->logical_cores
                                      : static_cast<uint32_t>(cores.size());
  if (d_total) {
    out->user_percent = std::min(100.0, Percent(d_user, d_total));
    out->system_percent = std::min(100.0, Percent(d_system, d_total));
    out->usage_percent =
        std::min(100.0, out->user_percent + out->system_percent);
  }
  if (!by_clock.empty()) {
    const auto& fast = by_clock.rbegin()->second;
    const auto& slow = by_clock.begin()->second;
    out->heterogeneous = by_clock.size() > 1;
    out->perf_cluster_percent = fast.first / std::max(1, fast.second);
    out->efficiency_cluster_percent = slow.first / std::max(1, slow.second);
  }
  return out;
}

mojom::MemorySnapshotPtr PulseSampler::BuildMemory(
    const ProcfsReadings& procfs,
    const std::vector<GroupTotals>& groups) const {
  auto out = mojom::MemorySnapshot::New();
  if (procfs.meminfo) {
    const MemInfo& m = *procfs.meminfo;
    const uint64_t total = m.total_kib * 1024;
    const uint64_t available = m.available_kib * 1024;
    const uint64_t used = total > available ? total - available : 0;
    const uint64_t system = m.SystemKib() * 1024;
    const uint64_t zram = procfs.zram ? procfs.zram->mem_used_total : 0;
    out->total_bytes = total;
    out->available_bytes = available;
    out->free_bytes = available;
    out->system_bytes = std::min(system, used);
    out->zram_resident_bytes = zram;
    // App is what is left of "used" once kernel memory and ZRAM's own
    // footprint are taken out, so the four bar segments add up to total.
    out->app_bytes = used > system + zram ? used - system - zram : 0;
  }
  if (procfs.zram) {
    out->zram_original_bytes = procfs.zram->orig_data_size;
    out->zram_compressed_bytes = procfs.zram->compr_data_size;
    out->zram_ratio = procfs.zram->CompressionRatio();
  }
  for (const GroupTotals& g : groups) {
    if (g.kind == GroupKind::kCrostini) {
      out->crostini_bytes = g.rss_bytes;
    } else if (g.kind == GroupKind::kArcvm) {
      out->arcvm_bytes = g.rss_bytes;
    }
  }
  out->pressure = mojom::MemoryPressure::kNormal;
  if (auto* monitor = base::MemoryPressureMonitor::Get()) {
    switch (monitor->GetCurrentPressureLevel()) {
      case base::MEMORY_PRESSURE_LEVEL_MODERATE:
        out->pressure = mojom::MemoryPressure::kModerate;
        break;
      case base::MEMORY_PRESSURE_LEVEL_CRITICAL:
        out->pressure = mojom::MemoryPressure::kCritical;
        break;
      default:
        break;
    }
  }
  return out;
}

mojom::BatterySnapshotPtr PulseSampler::BuildBattery(
    const healthd::BatteryInfo* b) const {
  if (!b || b->charge_full <= 0) {
    return nullptr;  // Chromebox, or no battery data.
  }
  auto out = mojom::BatterySnapshot::New();
  const bool charging = base::EqualsCaseInsensitiveASCII(b->status, "Charging");
  const double amps = std::fabs(b->current_now);
  out->charging = charging;
  out->charge_percent = std::clamp(b->charge_now / b->charge_full * 100.0, 0.0,
                                   100.0);
  out->power_watts = (charging ? 1 : -1) * b->voltage_now * amps;
  out->health_percent =
      b->charge_full_design > 0
          ? std::min(100.0, b->charge_full / b->charge_full_design * 100.0)
          : 100.0;
  out->cycle_count = static_cast<uint32_t>(std::max<int64_t>(0, b->cycle_count));
  out->full_wh = b->charge_full * b->voltage_min_design;
  out->design_wh = b->charge_full_design * b->voltage_min_design;
  out->adapter_watts = 0;
  if (auto* pm = chromeos::PowerManagerClient::Get()) {
    const std::optional<power_manager::PowerSupplyProperties>& status =
        pm->GetLastStatus();
    if (status && status->has_external_power_source_id()) {
      for (const auto& source : status->available_external_power_source()) {
        if (source.id() == status->external_power_source_id()) {
          out->adapter_watts = source.max_power();
        }
      }
    }
  }
  out->minutes_remaining = -1;
  if (amps > 0.01) {
    const double hours = charging ? (b->charge_full - b->charge_now) / amps
                                  : b->charge_now / amps;
    out->minutes_remaining = static_cast<int32_t>(std::lround(hours * 60));
  }
  return out;
}

mojom::ThermalSnapshotPtr PulseSampler::BuildThermal(
    const healthd::CpuInfo* cpu,
    const healthd::ThermalInfo* thermal,
    const std::optional<double>& battery_celsius) const {
  auto out = mojom::ThermalSnapshot::New();
  double cpu_c = 0;
  if (cpu) {
    for (const auto& ch : cpu->temperature_channels) {
      cpu_c = std::max(cpu_c, static_cast<double>(ch->temperature_celsius));
    }
  }
  if (thermal) {
    for (const auto& s : thermal->thermal_sensors) {
      out->sensors.push_back(
          mojom::ThermalSensor::New(s->name, s->temperature_celsius));
      if (SensorMatches(s->name, {"gpu", "mali"})) {
        out->gpu_celsius = std::max(out->gpu_celsius.value_or(0),
                                    s->temperature_celsius);
      } else if (SensorMatches(s->name, {"nvme", "ssd", "emmc", "ufs"})) {
        out->storage_celsius = s->temperature_celsius;
      } else if (!cpu_c && SensorMatches(s->name, {"cpu", "soc", "pkg"})) {
        cpu_c = std::max(cpu_c, s->temperature_celsius);
      }
    }
  }
  out->battery_celsius = battery_celsius;
  out->cpu_celsius = cpu_c;

  // Throttled: the cores are capped well below their rated clock while warm.
  double rated = 0;
  double capped = 0;
  if (cpu) {
    for (const auto& physical : cpu->physical_cpus) {
      for (const auto& lc : physical->logical_cpus) {
        rated += lc->max_clock_speed_khz;
        capped += lc->scaling_max_frequency_khz;
      }
    }
  }
  const double cap = rated > 0 ? std::max(0.0, 1.0 - capped / rated) : 0.0;
  const bool throttled = cpu_c >= 70 && cap >= 0.10;
  out->throttle_percent = throttled ? cap * 100.0 : 0.0;
  out->level = LevelFor(cpu_c, throttled);
  return out;
}

mojom::StorageSnapshotPtr PulseSampler::BuildStorage(
    const ProcfsReadings& procfs,
    double elapsed_seconds) {
  if (procfs.stateful_total_bytes <= 0) {
    return nullptr;
  }
  auto out = mojom::StorageSnapshot::New();
  out->total_bytes = static_cast<uint64_t>(procfs.stateful_total_bytes);
  out->free_bytes =
      static_cast<uint64_t>(std::max<int64_t>(0, procfs.stateful_free_bytes));
  out->filesystem = procfs.stateful_filesystem;
  if (slow_ && slow_->block_device_result &&
      slow_->block_device_result->is_block_device_info()) {
    const auto& devices = slow_->block_device_result->get_block_device_info();
    const healthd::NonRemovableBlockDeviceInfo* boot = nullptr;
    for (const auto& d : devices) {
      if (!boot || d->purpose == healthd::StorageDevicePurpose::kBootDevice) {
        boot = d.get();
      }
    }
    if (boot) {
      out->device_type = boot->type;
      const std::pair<uint64_t, uint64_t> now{boot->bytes_read_since_last_boot,
                                              boot->bytes_written_since_last_boot};
      // cros_healthd refreshes these every 5 s; rate over that window.
      if (last_block_bytes_ && elapsed_seconds > 0 &&
          now != *last_block_bytes_) {
        const double window = kHistoryInterval.InSecondsF();
        out->read_bytes_per_second =
            (now.first - std::min(now.first, last_block_bytes_->first)) / window;
        out->write_bytes_per_second =
            (now.second - std::min(now.second, last_block_bytes_->second)) /
            window;
        last_block_bytes_ = now;
      } else if (!last_block_bytes_) {
        last_block_bytes_ = now;
      } else if (latest_ && latest_->storage) {
        out->read_bytes_per_second = latest_->storage->read_bytes_per_second;
        out->write_bytes_per_second = latest_->storage->write_bytes_per_second;
      }
    }
  }
  return out;
}

mojom::NetworkSnapshotPtr PulseSampler::BuildNetwork(
    const ProcfsReadings& procfs,
    double elapsed_seconds) {
  // The busiest physical interface carries the rates.
  const NetDevCounters* best = nullptr;
  for (const NetDevCounters& c : procfs.net) {
    const std::string& n = c.interface_name;
    const bool physical = base::StartsWith(n, "wlan") ||
                          base::StartsWith(n, "mlan") ||
                          base::StartsWith(n, "eth") ||
                          base::StartsWith(n, "wwan") ||
                          base::StartsWith(n, "usb");
    if (physical && (!best || c.rx_bytes > best->rx_bytes)) {
      best = &c;
    }
  }
  auto out = mojom::NetworkSnapshot::New();
  out->kind = mojom::NetworkKind::kOther;
  if (best) {
    out->interface_name = best->interface_name;
    out->rx_bytes_total = best->rx_bytes;
    out->tx_bytes_total = best->tx_bytes;
    auto last = last_net_bytes_.find(best->interface_name);
    if (last != last_net_bytes_.end() && elapsed_seconds > 0) {
      out->rx_bytes_per_second =
          (best->rx_bytes - std::min(best->rx_bytes, last->second.first)) /
          elapsed_seconds;
      out->tx_bytes_per_second =
          (best->tx_bytes - std::min(best->tx_bytes, last->second.second)) /
          elapsed_seconds;
    }
    last_net_bytes_[best->interface_name] = {best->rx_bytes, best->tx_bytes};
  }
  if (slow_ && slow_->network_result &&
      slow_->network_result->is_network_health()) {
    const auto& networks = slow_->network_result->get_network_health()->networks;
    const network_health::Network* active = nullptr;
    for (const auto& n : networks) {
      if (n->state == network_health::NetworkState::kOnline ||
          n->state == network_health::NetworkState::kConnected ||
          n->state == network_health::NetworkState::kPortal) {
        active = n.get();
        break;
      }
    }
    if (active) {
      out->online = active->state == network_health::NetworkState::kOnline;
      switch (active->type) {
        case network_health::NetworkType::kWiFi:
          out->kind = mojom::NetworkKind::kWiFi;
          out->name = "Wi-Fi";
          break;
        case network_health::NetworkType::kEthernet:
          out->kind = mojom::NetworkKind::kEthernet;
          out->name = "Ethernet";
          break;
        case network_health::NetworkType::kCellular:
          out->kind = mojom::NetworkKind::kCellular;
          out->name = "Mobile data";
          break;
        default:
          break;
      }
      if (active->signal_strength) {
        out->signal_percent = active->signal_strength->value;
      }
    }
  }
  if (!best && out->name.empty()) {
    return nullptr;
  }
  return out;
}

mojom::GpuSnapshotPtr PulseSampler::BuildGpu(
    const ProcfsReadings& procfs) const {
  auto out = mojom::GpuSnapshot::New();
  const gpu::GPUInfo info = content::GpuDataManager::GetInstance()->GetGPUInfo();
  out->name = info.gl_renderer.empty() ? info.active_gpu().device_string
                                       : info.gl_renderer;
  out->usage_percent = procfs.gpu_load_percent;
  out->clock_mhz = procfs.gpu_clock_mhz;
  return out;
}

std::vector<mojom::ProcessGroupPtr> PulseSampler::BuildGroups(
    const std::vector<GroupTotals>& groups) const {
  std::vector<mojom::ProcessGroupPtr> out;
  for (const GroupTotals& g : groups) {
    auto p = mojom::ProcessGroup::New();
    p->kind = ToMojom(g.kind);
    p->name = GroupName(g.kind);
    p->representative_pid = g.representative_pid;
    p->process_count = g.process_count;
    p->thread_count = g.thread_count;
    p->cpu_percent = g.cpu_percent;
    p->memory_bytes = g.rss_bytes;
    p->can_end = false;  // PulsePageHandler asks its delegate.
    out.push_back(std::move(p));
  }
  return out;
}

mojom::HogAlertPtr PulseSampler::UpdateHog(
    const std::vector<GroupTotals>& groups,
    base::TimeTicks now) {
  std::array<bool, kGroupKindCount> over{};
  for (const GroupTotals& g : groups) {
    over[static_cast<size_t>(g.kind)] = g.cpu_percent > kHogThresholdPercent;
  }
  const GroupTotals* hog = nullptr;
  base::TimeDelta longest;
  for (size_t i = 0; i < kGroupKindCount; ++i) {
    if (!over[i]) {
      over_since_[i].reset();
      continue;
    }
    if (!over_since_[i]) {
      over_since_[i] = now;
    }
    const base::TimeDelta held = now - *over_since_[i];
    if (held >= kHogDuration && held > longest) {
      for (const GroupTotals& g : groups) {
        if (static_cast<size_t>(g.kind) == i) {
          hog = &g;
          longest = held;
        }
      }
    }
  }
  if (!hog) {
    return nullptr;
  }
  return mojom::HogAlert::New(ToMojom(hog->kind), GroupName(hog->kind),
                              hog->cpu_percent,
                              static_cast<uint32_t>(longest.InSeconds()),
                              /*can_end=*/false);
}

void PulseSampler::RecordHistory(const mojom::Snapshot& s) {
  auto push = [this](mojom::HistoryMetric m, double v) {
    base::circular_deque<double>& d = history_[m];
    d.push_back(v);
    while (d.size() > kHistorySize) {
      d.pop_front();
    }
  };
  const double total = static_cast<double>(s.memory->total_bytes);
  push(mojom::HistoryMetric::kCpu, s.cpu->usage_percent);
  push(mojom::HistoryMetric::kMemory,
       total > 0 ? (total - static_cast<double>(s.memory->available_bytes)) /
                       total * 100.0
                 : 0.0);
  push(mojom::HistoryMetric::kPower,
       s.battery ? std::fabs(s.battery->power_watts) : 0.0);
  push(mojom::HistoryMetric::kThermal, s.thermal->cpu_celsius);
  push(mojom::HistoryMetric::kGpu,
       s.gpu && s.gpu->usage_percent ? *s.gpu->usage_percent : 0.0);
  push(mojom::HistoryMetric::kStorageRead,
       s.storage ? s.storage->read_bytes_per_second : 0.0);
  push(mojom::HistoryMetric::kStorageWrite,
       s.storage ? s.storage->write_bytes_per_second : 0.0);
  push(mojom::HistoryMetric::kNetworkDown,
       s.network ? s.network->rx_bytes_per_second : 0.0);
  push(mojom::HistoryMetric::kNetworkUp,
       s.network ? s.network->tx_bytes_per_second : 0.0);
  std::array<double, kGroupKindCount> share{};
  for (const auto& g : s.groups) {
    share[static_cast<size_t>(g->kind)] = g->cpu_percent;
  }
  for (size_t i = 0; i < kGroupKindCount; ++i) {
    group_history_[i].push_back(share[i]);
    while (group_history_[i].size() > kHistorySize) {
      group_history_[i].pop_front();
    }
  }
}

std::vector<double> PulseSampler::History(mojom::HistoryMetric metric) const {
  auto it = history_.find(metric);
  if (it == history_.end()) {
    return {};
  }
  return std::vector<double>(it->second.begin(), it->second.end());
}

std::vector<double> PulseSampler::GroupHistory(
    mojom::ProcessGroupKind kind) const {
  const auto& d = group_history_[static_cast<size_t>(kind)];
  return std::vector<double>(d.begin(), d.end());
}

}  // namespace ash::pulse
