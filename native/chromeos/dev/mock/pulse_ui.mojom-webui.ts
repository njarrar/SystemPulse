// Stand-in for the module that the Mojo bindings generator writes from
// //ash/webui/pulse_ui/mojom/pulse_ui.mojom (pulse_ui.mojom-webui.ts). It
// exports the same names and shapes (camelCase fields, uint64 as bigint,
// nullable fields as `T|null`, responses wrapped in objects), but the remote
// talks to MockPulseBackend instead of a message pipe. The dev harness copies
// it next to the page sources, so the page compiles unchanged.

import {MockPulseBackend} from './mock_backend.js';

export enum ThermalLevel {
  MIN_VALUE = 0,
  MAX_VALUE = 3,
  kCool = 0,
  kWarm = 1,
  kHot = 2,
  kThrottled = 3,
}

export enum MemoryPressure {
  MIN_VALUE = 0,
  MAX_VALUE = 2,
  kNormal = 0,
  kModerate = 1,
  kCritical = 2,
}

export enum ProcessGroupKind {
  MIN_VALUE = 0,
  MAX_VALUE = 4,
  kAsh = 0,
  kChrome = 1,
  kCrostini = 2,
  kArcvm = 3,
  kSystem = 4,
}

export enum NetworkKind {
  MIN_VALUE = 0,
  MAX_VALUE = 3,
  kWiFi = 0,
  kEthernet = 1,
  kCellular = 2,
  kOther = 3,
}

export enum HistoryMetric {
  MIN_VALUE = 0,
  MAX_VALUE = 8,
  kCpu = 0,
  kMemory = 1,
  kPower = 2,
  kThermal = 3,
  kGpu = 4,
  kStorageRead = 5,
  kStorageWrite = 6,
  kNetworkDown = 7,
  kNetworkUp = 8,
}

export interface CpuSnapshot {
  usagePercent: number;
  userPercent: number;
  systemPercent: number;
  heterogeneous: boolean;
  perfClusterPercent: number;
  efficiencyClusterPercent: number;
  physicalCores: number;
  logicalCores: number;
  modelName: string;
  loadAverage1m: number;
}

export interface MemorySnapshot {
  totalBytes: bigint;
  availableBytes: bigint;
  freeBytes: bigint;
  appBytes: bigint;
  systemBytes: bigint;
  zramResidentBytes: bigint;
  zramOriginalBytes: bigint;
  zramCompressedBytes: bigint;
  zramRatio: number;
  crostiniBytes: bigint;
  arcvmBytes: bigint;
  pressure: MemoryPressure;
}

export interface BatterySnapshot {
  chargePercent: number;
  powerWatts: number;
  charging: boolean;
  minutesRemaining: number;
  healthPercent: number;
  cycleCount: number;
  fullWh: number;
  designWh: number;
  adapterWatts: number;
}

export interface ThermalSensor {
  name: string;
  celsius: number;
}

export interface ThermalSnapshot {
  level: ThermalLevel;
  cpuCelsius: number;
  gpuCelsius: number|null;
  storageCelsius: number|null;
  batteryCelsius: number|null;
  throttlePercent: number;
  sensors: ThermalSensor[];
}

export interface FanSnapshot {
  speedsRpm: number[];
}

export interface StorageSnapshot {
  deviceType: string;
  totalBytes: bigint;
  freeBytes: bigint;
  readBytesPerSecond: number;
  writeBytesPerSecond: number;
  filesystem: string;
}

export interface NetworkSnapshot {
  kind: NetworkKind;
  name: string;
  online: boolean;
  signalPercent: number|null;
  interfaceName: string;
  rxBytesPerSecond: number;
  txBytesPerSecond: number;
  rxBytesTotal: bigint;
  txBytesTotal: bigint;
}

export interface GpuSnapshot {
  name: string;
  usagePercent: number|null;
  clockMhz: number|null;
  sharedMemoryBytes: bigint|null;
}

export interface ProcessGroup {
  kind: ProcessGroupKind;
  name: string;
  representativePid: number;
  processCount: number;
  threadCount: number;
  cpuPercent: number;
  memoryBytes: bigint;
  gpuPercent: number|null;
  canEnd: boolean;
}

export interface HogAlert {
  kind: ProcessGroupKind;
  name: string;
  cpuPercent: number;
  secondsOverThreshold: number;
  canEnd: boolean;
}

export interface Snapshot {
  cpu: CpuSnapshot;
  memory: MemorySnapshot;
  battery: BatterySnapshot|null;
  thermal: ThermalSnapshot;
  fans: FanSnapshot;
  storage: StorageSnapshot|null;
  network: NetworkSnapshot|null;
  gpu: GpuSnapshot|null;
  groups: ProcessGroup[];
  hog: HogAlert|null;
}

// --- Interfaces -----------------------------------------------------------

export interface PageHandlerInterface {
  getSnapshot(): Promise<{snapshot: Snapshot}>;
  getHistory(metric: HistoryMetric): Promise<{samples: number[]}>;
  getGroupHistory(kind: ProcessGroupKind): Promise<{samples: number[]}>;
  endGroup(kind: ProcessGroupKind): Promise<{ok: boolean}>;
  restoreGroup(kind: ProcessGroupKind): Promise<{ok: boolean}>;
  setLiveUpdates(enabled: boolean): void;
  getLanguage(): Promise<{code: string}>;
  setLanguage(code: string): void;
  openDiagnostics(): void;
}

export interface PageInterface {
  onSnapshot(snapshot: Snapshot): void;
}

// Pipe endpoints. In the real bindings these wrap Mojo handles.
export class PageRemote {
  constructor(readonly router: PageCallbackRouter) {}
}
export class PageHandlerPendingReceiver {
  constructor(readonly remote: PageHandlerRemote) {}
}

class ListenerList<T extends unknown[]> {
  private next_ = 0;
  private readonly fns_ = new Map<number, (...args: T) => void>();
  addListener(fn: (...args: T) => void): number {
    this.fns_.set(++this.next_, fn);
    return this.next_;
  }
  removeListener(id: number): boolean {
    return this.fns_.delete(id);
  }
  dispatch(...args: T) {
    for (const fn of this.fns_.values()) {
      fn(...args);
    }
  }
}

export class PageCallbackRouter {
  readonly onSnapshot = new ListenerList<[Snapshot]>();
  readonly $ = {
    bindNewPipeAndPassRemote: (): PageRemote => new PageRemote(this),
  };
  removeListener(id: number): boolean {
    return this.onSnapshot.removeListener(id);
  }
}

export class PageHandlerRemote implements PageHandlerInterface {
  backend: MockPulseBackend|null = null;
  readonly $ = {
    bindNewPipeAndPassReceiver: (): PageHandlerPendingReceiver =>
        new PageHandlerPendingReceiver(this),
    close: () => {
      this.backend?.stop();
    },
  };
  private backend_(): MockPulseBackend {
    if (!this.backend) {
      throw new Error('PageHandlerRemote is not bound');
    }
    return this.backend;
  }
  getSnapshot() {
    return Promise.resolve({snapshot: this.backend_().snapshot()});
  }
  getHistory(metric: HistoryMetric) {
    return Promise.resolve({samples: this.backend_().history(metric)});
  }
  getGroupHistory(kind: ProcessGroupKind) {
    return Promise.resolve({samples: this.backend_().groupHistory(kind)});
  }
  endGroup(kind: ProcessGroupKind) {
    return Promise.resolve({ok: this.backend_().endGroup(kind)});
  }
  restoreGroup(kind: ProcessGroupKind) {
    return Promise.resolve({ok: this.backend_().restoreGroup(kind)});
  }
  setLiveUpdates(enabled: boolean) {
    this.backend_().setLive(enabled);
  }
  getLanguage() {
    return Promise.resolve({code: this.backend_().getLanguage()});
  }
  setLanguage(code: string) {
    this.backend_().setLanguage(code);
  }
  openDiagnostics() {
    this.backend_().openDiagnostics();
  }
}

export class PageHandlerFactoryRemote {
  createPageHandler(page: PageRemote, handler: PageHandlerPendingReceiver) {
    const backend = new MockPulseBackend(
        (s: Snapshot) => page.router.onSnapshot.dispatch(s));
    handler.remote.backend = backend;
    backend.start();
  }
}

export class PageHandlerFactory {
  static getRemote(): PageHandlerFactoryRemote {
    return new PageHandlerFactoryRemote();
  }
}
