// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_WEBUI_PULSE_UI_TELEMETRY_PULSE_SAMPLER_H_
#define ASH_WEBUI_PULSE_UI_TELEMETRY_PULSE_SAMPLER_H_

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ash/webui/pulse_ui/mojom/pulse_ui.mojom.h"
#include "ash/webui/pulse_ui/telemetry/proc_parsers.h"
#include "ash/webui/pulse_ui/telemetry/process_grouper.h"
#include "base/containers/circular_deque.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chromeos/ash/services/cros_healthd/public/mojom/cros_healthd_probe.mojom.h"

namespace ash::pulse {

// Raw readings from procfs and sysfs, gathered on a blocking thread.
struct ProcfsReadings {
  ProcfsReadings();
  ProcfsReadings(ProcfsReadings&&);
  ProcfsReadings& operator=(ProcfsReadings&&);
  ~ProcfsReadings();

  std::optional<MemInfo> meminfo;
  std::optional<ZramMmStat> zram;
  std::optional<double> load_average_1m;
  std::vector<NetDevCounters> net;
  std::string stateful_filesystem;
  int64_t stateful_total_bytes = -1;
  int64_t stateful_free_bytes = -1;
  std::optional<double> gpu_load_percent;
  std::optional<uint32_t> gpu_clock_mhz;
  std::vector<ProcSample> processes;
  uint64_t ticks_per_second = 100;
  uint32_t cpu_count = 1;
};

// Reads every file above. Blocking; runs on the thread pool.
ProcfsReadings ReadProcfs();

// Samples system vitals for chrome://pulse. One instance per ash process,
// created by the first page and kept for the session so the ten-minute
// charts have data the next time the app opens.
//
// Cadence: every 5 seconds while no page is open (history only), every
// 1.5 seconds while one is. Each sample reads procfs once and asks
// cros_healthd for CPU, thermal, fan and battery data; network and storage
// data from cros_healthd refresh every 5 seconds.
class PulseSampler {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnSnapshot(const mojom::Snapshot& snapshot) = 0;
  };

  static constexpr base::TimeDelta kFastInterval = base::Milliseconds(1500);
  static constexpr base::TimeDelta kHistoryInterval = base::Seconds(5);
  static constexpr size_t kHistorySize = 121;  // Ten minutes.
  static constexpr base::TimeDelta kHogDuration = base::Minutes(2);
  static constexpr double kHogThresholdPercent = 50.0;

  static PulseSampler* Get();

  PulseSampler(const PulseSampler&) = delete;
  PulseSampler& operator=(const PulseSampler&) = delete;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Null until the first sample completes.
  const mojom::Snapshot* latest() const { return latest_.get(); }

  std::vector<double> History(mojom::HistoryMetric metric) const;
  std::vector<double> GroupHistory(mojom::ProcessGroupKind kind) const;

 private:
  friend class base::NoDestructor<PulseSampler>;

  struct CpuTimes {
    uint64_t user = 0;
    uint64_t system = 0;
    uint64_t total = 0;
  };

  PulseSampler();
  ~PulseSampler();

  void ScheduleNext();
  void Sample();
  void OnProcfs(uint64_t round, ProcfsReadings readings);
  void OnFastTelemetry(uint64_t round,
                       cros_healthd::mojom::TelemetryInfoPtr info);
  void OnSlowTelemetry(cros_healthd::mojom::TelemetryInfoPtr info);
  void MaybeFinishRound(uint64_t round);

  mojom::CpuSnapshotPtr BuildCpu(const cros_healthd::mojom::CpuInfo* cpu,
                                 const ProcfsReadings& procfs);
  mojom::MemorySnapshotPtr BuildMemory(
      const ProcfsReadings& procfs,
      const std::vector<GroupTotals>& groups) const;
  mojom::BatterySnapshotPtr BuildBattery(
      const cros_healthd::mojom::BatteryInfo* battery) const;
  mojom::ThermalSnapshotPtr BuildThermal(
      const cros_healthd::mojom::CpuInfo* cpu,
      const cros_healthd::mojom::ThermalInfo* thermal,
      const std::optional<double>& battery_celsius) const;
  mojom::StorageSnapshotPtr BuildStorage(const ProcfsReadings& procfs,
                                         double elapsed_seconds);
  mojom::NetworkSnapshotPtr BuildNetwork(const ProcfsReadings& procfs,
                                         double elapsed_seconds);
  mojom::GpuSnapshotPtr BuildGpu(const ProcfsReadings& procfs) const;
  std::vector<mojom::ProcessGroupPtr> BuildGroups(
      const std::vector<GroupTotals>& groups) const;
  mojom::HogAlertPtr UpdateHog(const std::vector<GroupTotals>& groups,
                               base::TimeTicks now);
  void RecordHistory(const mojom::Snapshot& snapshot);

  SEQUENCE_CHECKER(sequence_checker_);

  base::ObserverList<Observer> observers_;
  base::OneShotTimer timer_;

  // One round = one procfs read plus one fast cros_healthd probe.
  uint64_t round_ = 0;
  std::optional<ProcfsReadings> pending_procfs_;
  cros_healthd::mojom::TelemetryInfoPtr pending_fast_;
  bool fast_done_ = false;
  base::TimeTicks last_round_time_;
  base::TimeTicks last_slow_probe_;
  base::TimeTicks last_history_;

  // Latest network and storage data from cros_healthd.
  cros_healthd::mojom::TelemetryInfoPtr slow_;

  std::optional<ProcessGrouper> grouper_;
  std::map<std::pair<uint32_t, uint32_t>, CpuTimes> last_cpu_times_;
  std::optional<std::pair<uint64_t, uint64_t>> last_block_bytes_;
  std::map<std::string, std::pair<uint64_t, uint64_t>> last_net_bytes_;
  std::array<std::optional<base::TimeTicks>, kGroupKindCount> over_since_;

  mojom::SnapshotPtr latest_;
  std::map<mojom::HistoryMetric, base::circular_deque<double>> history_;
  std::array<base::circular_deque<double>, kGroupKindCount> group_history_;

  base::WeakPtrFactory<PulseSampler> weak_factory_{this};
};

}  // namespace ash::pulse

#endif  // ASH_WEBUI_PULSE_UI_TELEMETRY_PULSE_SAMPLER_H_
