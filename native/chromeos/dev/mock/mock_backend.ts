// Simulated ash side for the dev harness. Values follow a MediaTek Kompanio
// 520 Chromebook (2x Cortex-A76 + 6x Cortex-A55, Mali-G57 MC2, 16 GB, eMMC,
// Wi-Fi 6, fanless) with the Linux VM busy compiling, which trips the hog
// alert the way the C++ sampler would after two minutes.
//
// Query string knobs (dev harness only): ?hog=0 turns the busy VM off,
// ?charging=1 plugs in the charger, ?seed=N changes the random walk.

import {HistoryMetric, MemoryPressure, NetworkKind, ProcessGroupKind, ThermalLevel} from './pulse_ui.mojom-webui.js';
import type {ProcessGroup, Snapshot} from './pulse_ui.mojom-webui.js';

const GB = 1024 ** 3;
const MB = 1024 ** 2;
const N = 121;  // Ten minutes at five seconds.

function rng(seed: number) {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6D2B79F5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

interface GroupSeed {
  kind: ProcessGroupKind;
  name: string;
  pid: number;
  procs: number;
  threads: number;
  cpu: number;
  memGb: number;
  gpu: number;
  canEnd: boolean;
}

// The same groups ash reports: ash itself, Chrome, the two VMs and the rest.
// A function, not a constant: this module and the bindings module import
// each other, so the enums are not ready while this module first runs.
function groupSeeds(): GroupSeed[] {
  return [
  {kind: ProcessGroupKind.kCrostini, name: 'Linux (Crostini)', pid: 2901, procs: 22, threads: 96, cpu: 51.6, memGb: 1.42, gpu: 2, canEnd: true},
  {kind: ProcessGroupKind.kChrome, name: 'Chrome', pid: 1402, procs: 14, threads: 131, cpu: 6.1, memGb: 1.88, gpu: 11, canEnd: false},
  {kind: ProcessGroupKind.kArcvm, name: 'Android (ARCVM)', pid: 3318, procs: 31, threads: 118, cpu: 3.0, memGb: 1.12, gpu: 6, canEnd: false},
  {kind: ProcessGroupKind.kAsh, name: 'System UI', pid: 611, procs: 3, threads: 29, cpu: 1.9, memGb: 0.44, gpu: 14, canEnd: false},
  {kind: ProcessGroupKind.kSystem, name: 'System', pid: 1, procs: 64, threads: 141, cpu: 0.8, memGb: 0.31, gpu: 0, canEnd: false},
  ];
}

export class MockPulseBackend {
  private readonly r: () => number;
  private readonly hogOn: boolean;
  private readonly charging: boolean;
  private timer = 0;
  private ended = new Set<ProcessGroupKind>();
  private live = {cpu: 0, ram: 58, watt: 0, temp: 0, gpu: 14, read: 42, write: 8.6, down: 1.8, up: 140};
  private readonly hist = new Map<HistoryMetric, number[]>();
  private readonly ghist = new Map<ProcessGroupKind, number[]>();
  private jit = new Map<ProcessGroupKind, number>();
  private hogSeconds = 160;
  private readonly seeds = groupSeeds();

  constructor(private readonly push: (s: Snapshot) => void) {
    const q = new URLSearchParams(globalThis.location ? location.search : '');
    this.r = rng(Number(q.get('seed') || 11));
    this.hogOn = q.get('hog') !== '0';
    this.charging = q.get('charging') === '1';
    this.live.cpu = this.hogOn ? 66 : 16;
    this.live.watt = this.hogOn ? 14.8 : 9.4;
    this.live.temp = this.hogOn ? 52 : 33;
    this.seedHistory();
  }

  private walk(base: number, vol: number, lo: number, hi: number): number[] {
    let v = base;
    const a: number[] = [];
    for (let i = 0; i < N; i++) {
      v += (this.r() - 0.5) * vol + (base - v) * 0.12;
      a.push(Math.max(lo, Math.min(hi, v)));
    }
    return a;
  }

  private bump(a: number[], at: number, h: number, w: number): number[] {
    return a.map((v, i) => v + h * Math.exp(-Math.pow((i - at) / w, 2)));
  }

  // The Linux VM started its build 2.7 minutes ago.
  private ramp(a: number[], add: number): number[] {
    return a.map(
        (v, i) => i > N - 34 ? v + add * Math.min(1, (i - (N - 34)) / 4) : v);
  }

  private seedHistory() {
    const hog = this.hogOn;
    const cpu = this.bump(this.walk(16, 6, 4, 40), 50, 22, 3);
    this.hist.set(HistoryMetric.kCpu, hog ? this.ramp(cpu, 48) : cpu);
    this.hist.set(HistoryMetric.kMemory, this.bump(this.walk(57, 1.2, 52, 62), 80, 2.5, 6));
    const p = this.walk(9.4, 1.6, 4, 16);
    this.hist.set(HistoryMetric.kPower, hog ? this.ramp(p, 5.4) : p);
    const t = this.walk(33, 0.7, 29, 38);
    this.hist.set(HistoryMetric.kThermal, hog ? this.ramp(t, 19) : t);
    this.hist.set(HistoryMetric.kGpu, this.bump(this.walk(13, 7, 2, 40), 66, 38, 4));
    this.hist.set(HistoryMetric.kStorageRead, this.bump(this.walk(28, 30, 0, 120), 62, 90, 2.5).map(x => x * MB));
    this.hist.set(HistoryMetric.kStorageWrite, this.bump(this.walk(7, 7, 0, 40), 88, 30, 3).map(x => x * MB));
    this.hist.set(HistoryMetric.kNetworkDown, this.bump(this.walk(1.6, 1.2, 0.05, 5), 70, 3, 4).map(x => x * MB));
    this.hist.set(HistoryMetric.kNetworkUp, this.walk(140, 90, 20, 480).map(x => x * 1024));
    for (const g of this.seeds) {
      let s = this.walk(g.kind === ProcessGroupKind.kCrostini && hog ? 3 : g.cpu, Math.max(0.6, g.cpu * 0.3), 0, 100);
      if (g.kind === ProcessGroupKind.kCrostini && hog) {
        s = this.ramp(s, g.cpu);
      }
      this.ghist.set(g.kind, s);
    }
  }

  start() {
    this.push(this.snapshot());
    if (new URLSearchParams(globalThis.location ? location.search : '').get('live') === '0') {
      return;
    }
    this.setLive(true);
  }

  stop() {
    clearInterval(this.timer);
    this.timer = 0;
  }

  // PageHandler.SetLiveUpdates: the 1.5 s push, as PulseSampler runs it.
  setLive(enabled: boolean) {
    this.stop();
    if (enabled) {
      this.timer = setInterval(() => {
        this.tick();
        this.push(this.snapshot());
      }, 1500) as unknown as number;
    }
  }

  private hogActive(): boolean {
    return this.hogOn && !this.ended.has(ProcessGroupKind.kCrostini);
  }

  private tick() {
    const L = this.live;
    const hog = this.hogActive();
    const j = (v: number, b: number, vol: number, lo: number, hi: number) =>
        Math.max(lo, Math.min(hi, v + (this.r() - 0.5) * vol + (b - v) * 0.3));
    L.cpu = j(L.cpu, hog ? 66 : 16, 5, 3, 95);
    L.ram = j(L.ram, 58, 0.6, 52, 64);
    L.watt = j(L.watt, hog ? 14.8 : 9.4, 1.0, 4, 24);
    L.temp = j(L.temp, hog ? 52 : 33, 0.6, 28, 70);
    L.down = j(L.down, 1.8, 0.8, 0.1, 6);
    L.up = j(L.up, 140, 50, 20, 600);
    L.read = j(L.read, 42, 18, 0, 160);
    L.write = j(L.write, 8.6, 5, 0, 60);
    L.gpu = j(L.gpu, 14, 7, 2, 60);
    for (const g of this.seeds) {
      const prev = this.jit.get(g.kind) || 0;
      this.jit.set(g.kind, Math.max(-0.14, Math.min(0.14, prev * 0.6 + (this.r() - 0.5) * 0.14)));
    }
    if (hog) {
      this.hogSeconds += 1.5;
    }
  }

  private groups(): ProcessGroup[] {
    return this.seeds.filter(g => !this.ended.has(g.kind)).map(g => {
      const j = this.jit.get(g.kind) || 0;
      const isHog = g.kind === ProcessGroupKind.kCrostini;
      const cpu = isHog && !this.hogOn ? 2.8 * (1 + j) :
          isHog ? Math.max(51, g.cpu * (1 + j * 0.4)) :
                  g.cpu * (1 + j);
      return {
        kind: g.kind,
        name: g.name,
        representativePid: g.pid,
        processCount: g.procs,
        threadCount: g.threads,
        cpuPercent: Math.max(0.1, cpu),
        memoryBytes: BigInt(Math.round(g.memGb * GB)),
        gpuPercent: Math.max(0, Math.round(g.gpu * (1 + j))),
        canEnd: g.canEnd,
      };
    });
  }

  snapshot(): Snapshot {
    const L = this.live;
    const total = 16 * GB;
    const used = L.ram / 100 * total;
    const zramResident = 1.2 * GB;
    const system = 2.0 * GB;
    const groups = this.groups();
    const crostini = this.ended.has(ProcessGroupKind.kCrostini) ? 0 : 1.42 * GB;
    const arcvm = this.ended.has(ProcessGroupKind.kArcvm) ? 0 : 1.12 * GB;
    const hogGroup = this.hogActive() ?
        groups.find(g => g.kind === ProcessGroupKind.kCrostini) :
        undefined;
    const u = Math.round(L.cpu * 0.66);
    const level = L.temp < 48 ? ThermalLevel.kCool :
        L.temp < 70           ? ThermalLevel.kWarm :
        L.temp < 90           ? ThermalLevel.kHot :
                                ThermalLevel.kThrottled;
    const gpuC = 36 + (L.gpu - 14) * 0.08;
    return {
      cpu: {
        usagePercent: L.cpu,
        userPercent: u,
        systemPercent: Math.round(L.cpu) - u,
        heterogeneous: true,
        perfClusterPercent: Math.min(100, L.cpu * 1.22),
        efficiencyClusterPercent: L.cpu * 0.58,
        physicalCores: 8,
        logicalCores: 8,
        modelName: 'MediaTek Kompanio 520',
        loadAverage1m: L.cpu / 11.2,
      },
      memory: {
        totalBytes: BigInt(total),
        availableBytes: BigInt(Math.round(total - used)),
        freeBytes: BigInt(Math.round(total - used)),
        appBytes: BigInt(Math.round(Math.max(0, used - system - zramResident))),
        systemBytes: BigInt(Math.round(system)),
        zramResidentBytes: BigInt(Math.round(zramResident)),
        zramOriginalBytes: BigInt(Math.round(3.7 * GB)),
        zramCompressedBytes: BigInt(Math.round(1.19 * GB)),
        zramRatio: 3.7 / 1.19,
        crostiniBytes: BigInt(Math.round(crostini)),
        arcvmBytes: BigInt(Math.round(arcvm)),
        pressure: this.pressure(),
      },
      battery: this.q('battery') === '0' ? null : {
        chargePercent: 84,
        powerWatts: this.charging ? 38 : -L.watt,
        charging: this.charging,
        minutesRemaining: this.charging ? 52 : this.hogActive() ? 160 : 255,
        healthPercent: 95,
        cycleCount: 118,
        fullWh: 47.5,
        designWh: 50.0,
        adapterWatts: this.charging ? 45 : 0,
      },
      thermal: {
        level,
        cpuCelsius: L.temp,
        gpuCelsius: gpuC,
        storageCelsius: 29,
        batteryCelsius: 28,
        throttlePercent: 0,
        sensors: [
          {name: 'cpu_little', celsius: L.temp - 1.2},
          {name: 'cpu_big0', celsius: L.temp},
          {name: 'gpu', celsius: gpuC},
          {name: 'battery', celsius: 28},
        ],
      },
      fans: {speedsRpm: []},
      storage: {
        deviceType: 'eMMC',
        totalBytes: BigInt(Math.round(488 * GB)),
        freeBytes: BigInt(Math.round(248 * GB)),
        readBytesPerSecond: L.read * MB,
        writeBytesPerSecond: L.write * MB,
        filesystem: 'ext4',
      },
      network: {
        kind: NetworkKind.kWiFi,
        name: 'Wi-Fi',
        online: true,
        signalPercent: 78,
        interfaceName: 'wlan0',
        rxBytesPerSecond: L.down * MB,
        txBytesPerSecond: L.up * 1024,
        rxBytesTotal: BigInt(Math.round(1.4 * GB)),
        txBytesTotal: BigInt(212 * MB),
      },
      gpu: {
        name: 'Mali-G57 MC2',
        usagePercent: L.gpu,
        clockMhz: 950,
        sharedMemoryBytes: BigInt(Math.round(0.6 * GB)),
      },
      groups,
      hog: hogGroup ? {
        kind: hogGroup.kind,
        name: hogGroup.name,
        cpuPercent: hogGroup.cpuPercent,
        secondsOverThreshold: Math.round(this.hogSeconds),
        canEnd: hogGroup.canEnd,
      } :
                      null,
    };
  }

  history(metric: HistoryMetric): number[] {
    const a = [...(this.hist.get(metric) || [])];
    const L = this.live;
    const now: Record<number, number> = {
      [HistoryMetric.kCpu]: L.cpu,
      [HistoryMetric.kMemory]: L.ram,
      [HistoryMetric.kPower]: L.watt,
      [HistoryMetric.kThermal]: L.temp,
      [HistoryMetric.kGpu]: L.gpu,
      [HistoryMetric.kStorageRead]: L.read * MB,
      [HistoryMetric.kStorageWrite]: L.write * MB,
      [HistoryMetric.kNetworkDown]: L.down * MB,
      [HistoryMetric.kNetworkUp]: L.up * 1024,
    };
    if (a.length) {
      a[a.length - 1] = now[metric]!;
    }
    return a;
  }

  groupHistory(kind: ProcessGroupKind): number[] {
    const a = [...(this.ghist.get(kind) || [])];
    const g = this.groups().find(x => x.kind === kind);
    if (a.length && g) {
      a[a.length - 1] = g.cpuPercent;
    }
    return a;
  }

  endGroup(kind: ProcessGroupKind): boolean {
    const g = this.seeds.find(x => x.kind === kind);
    if (!g || !g.canEnd || this.failEnd()) {
      return false;
    }
    this.ended.add(kind);
    this.live.cpu = this.hogActive() ? 66 : 16;
    this.live.watt = this.hogActive() ? 14.8 : 9.4;
    this.live.temp = this.hogActive() ? 52 : 33;
    return true;
  }

  // Crostini restarts the way CrostiniManager::RestartCrostini would.
  restoreGroup(kind: ProcessGroupKind): boolean {
    if (!this.ended.delete(kind)) {
      return false;
    }
    this.live.cpu = this.hogActive() ? 66 : 16;
    this.live.watt = this.hogActive() ? 14.8 : 9.4;
    this.live.temp = this.hogActive() ? 52 : 33;
    return true;
  }

  // The ash.pulse.language profile pref. The harness keeps it in
  // localStorage so it lasts across reloads.
  getLanguage(): string {
    try {
      return localStorage.getItem('pulse.pref.language') || '';
    } catch {
      return '';
    }
  }

  setLanguage(code: string) {
    try {
      localStorage.setItem('pulse.pref.language', code);
    } catch {
      // Storage off: the choice lasts for this window only.
    }
  }

  private q(key: string): string|null {
    return new URLSearchParams(globalThis.location ? location.search : '').get(key);
  }

  // ?pressure=high|critical sets the base::MemoryPressureMonitor level.
  private pressure(): MemoryPressure {
    const p = this.q('pressure');
    return p === 'high' ? MemoryPressure.kModerate :
        p === 'critical' ? MemoryPressure.kCritical : MemoryPressure.kNormal;
  }

  // ?fail=1 makes EndGroup fail, the way StopVm can.
  private failEnd(): boolean {
    return new URLSearchParams(globalThis.location ? location.search : '').get('fail') === '1';
  }

  openDiagnostics() {
    console.info('[mock] OpenDiagnostics: ash would launch chrome://diagnostics');
    document.dispatchEvent(new CustomEvent('pulse-mock-diagnostics'));
  }
}
