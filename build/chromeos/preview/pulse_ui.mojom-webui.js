// Stand-in for the module that the Mojo bindings generator writes from
// //ash/webui/pulse_ui/mojom/pulse_ui.mojom (pulse_ui.mojom-webui.ts). It
// exports the same names and shapes (camelCase fields, uint64 as bigint,
// nullable fields as `T|null`, responses wrapped in objects), but the remote
// talks to MockPulseBackend instead of a message pipe. The dev harness copies
// it next to the page sources, so the page compiles unchanged.
import { MockPulseBackend } from './mock_backend.js';
export var ThermalLevel;
(function (ThermalLevel) {
    ThermalLevel[ThermalLevel["MIN_VALUE"] = 0] = "MIN_VALUE";
    ThermalLevel[ThermalLevel["MAX_VALUE"] = 3] = "MAX_VALUE";
    ThermalLevel[ThermalLevel["kCool"] = 0] = "kCool";
    ThermalLevel[ThermalLevel["kWarm"] = 1] = "kWarm";
    ThermalLevel[ThermalLevel["kHot"] = 2] = "kHot";
    ThermalLevel[ThermalLevel["kThrottled"] = 3] = "kThrottled";
})(ThermalLevel || (ThermalLevel = {}));
export var MemoryPressure;
(function (MemoryPressure) {
    MemoryPressure[MemoryPressure["MIN_VALUE"] = 0] = "MIN_VALUE";
    MemoryPressure[MemoryPressure["MAX_VALUE"] = 2] = "MAX_VALUE";
    MemoryPressure[MemoryPressure["kNormal"] = 0] = "kNormal";
    MemoryPressure[MemoryPressure["kModerate"] = 1] = "kModerate";
    MemoryPressure[MemoryPressure["kCritical"] = 2] = "kCritical";
})(MemoryPressure || (MemoryPressure = {}));
export var ProcessGroupKind;
(function (ProcessGroupKind) {
    ProcessGroupKind[ProcessGroupKind["MIN_VALUE"] = 0] = "MIN_VALUE";
    ProcessGroupKind[ProcessGroupKind["MAX_VALUE"] = 4] = "MAX_VALUE";
    ProcessGroupKind[ProcessGroupKind["kAsh"] = 0] = "kAsh";
    ProcessGroupKind[ProcessGroupKind["kChrome"] = 1] = "kChrome";
    ProcessGroupKind[ProcessGroupKind["kCrostini"] = 2] = "kCrostini";
    ProcessGroupKind[ProcessGroupKind["kArcvm"] = 3] = "kArcvm";
    ProcessGroupKind[ProcessGroupKind["kSystem"] = 4] = "kSystem";
})(ProcessGroupKind || (ProcessGroupKind = {}));
export var NetworkKind;
(function (NetworkKind) {
    NetworkKind[NetworkKind["MIN_VALUE"] = 0] = "MIN_VALUE";
    NetworkKind[NetworkKind["MAX_VALUE"] = 3] = "MAX_VALUE";
    NetworkKind[NetworkKind["kWiFi"] = 0] = "kWiFi";
    NetworkKind[NetworkKind["kEthernet"] = 1] = "kEthernet";
    NetworkKind[NetworkKind["kCellular"] = 2] = "kCellular";
    NetworkKind[NetworkKind["kOther"] = 3] = "kOther";
})(NetworkKind || (NetworkKind = {}));
export var HistoryMetric;
(function (HistoryMetric) {
    HistoryMetric[HistoryMetric["MIN_VALUE"] = 0] = "MIN_VALUE";
    HistoryMetric[HistoryMetric["MAX_VALUE"] = 8] = "MAX_VALUE";
    HistoryMetric[HistoryMetric["kCpu"] = 0] = "kCpu";
    HistoryMetric[HistoryMetric["kMemory"] = 1] = "kMemory";
    HistoryMetric[HistoryMetric["kPower"] = 2] = "kPower";
    HistoryMetric[HistoryMetric["kThermal"] = 3] = "kThermal";
    HistoryMetric[HistoryMetric["kGpu"] = 4] = "kGpu";
    HistoryMetric[HistoryMetric["kStorageRead"] = 5] = "kStorageRead";
    HistoryMetric[HistoryMetric["kStorageWrite"] = 6] = "kStorageWrite";
    HistoryMetric[HistoryMetric["kNetworkDown"] = 7] = "kNetworkDown";
    HistoryMetric[HistoryMetric["kNetworkUp"] = 8] = "kNetworkUp";
})(HistoryMetric || (HistoryMetric = {}));
// Pipe endpoints. In the real bindings these wrap Mojo handles.
export class PageRemote {
    router;
    constructor(router) {
        this.router = router;
    }
}
export class PageHandlerPendingReceiver {
    remote;
    constructor(remote) {
        this.remote = remote;
    }
}
class ListenerList {
    next_ = 0;
    fns_ = new Map();
    addListener(fn) {
        this.fns_.set(++this.next_, fn);
        return this.next_;
    }
    removeListener(id) {
        return this.fns_.delete(id);
    }
    dispatch(...args) {
        for (const fn of this.fns_.values()) {
            fn(...args);
        }
    }
}
export class PageCallbackRouter {
    onSnapshot = new ListenerList();
    $ = {
        bindNewPipeAndPassRemote: () => new PageRemote(this),
    };
    removeListener(id) {
        return this.onSnapshot.removeListener(id);
    }
}
export class PageHandlerRemote {
    backend = null;
    $ = {
        bindNewPipeAndPassReceiver: () => new PageHandlerPendingReceiver(this),
        close: () => {
            this.backend?.stop();
        },
    };
    backend_() {
        if (!this.backend) {
            throw new Error('PageHandlerRemote is not bound');
        }
        return this.backend;
    }
    getSnapshot() {
        return Promise.resolve({ snapshot: this.backend_().snapshot() });
    }
    getHistory(metric) {
        return Promise.resolve({ samples: this.backend_().history(metric) });
    }
    getGroupHistory(kind) {
        return Promise.resolve({ samples: this.backend_().groupHistory(kind) });
    }
    endGroup(kind) {
        return Promise.resolve({ ok: this.backend_().endGroup(kind) });
    }
    restoreGroup(kind) {
        return Promise.resolve({ ok: this.backend_().restoreGroup(kind) });
    }
    setLiveUpdates(enabled) {
        this.backend_().setLive(enabled);
    }
    getLanguage() {
        return Promise.resolve({ code: this.backend_().getLanguage() });
    }
    setLanguage(code) {
        this.backend_().setLanguage(code);
    }
    openDiagnostics() {
        this.backend_().openDiagnostics();
    }
}
export class PageHandlerFactoryRemote {
    createPageHandler(page, handler) {
        const backend = new MockPulseBackend((s) => page.router.onSnapshot.dispatch(s));
        handler.remote.backend = backend;
        backend.start();
    }
}
export class PageHandlerFactory {
    static getRemote() {
        return new PageHandlerFactoryRemote();
    }
}
