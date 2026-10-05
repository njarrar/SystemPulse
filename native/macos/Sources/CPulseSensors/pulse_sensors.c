#include "pulse_sensors.h"

#ifdef __APPLE__

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <dlfcn.h>
#include <string.h>
#include <stdint.h>

// MARK: - IOHID event system (not in the public SDK headers; exported by IOKit)

typedef CFTypeRef PulseHIDClient;
typedef CFTypeRef PulseHIDService;
typedef CFTypeRef PulseHIDEvent;

// Declared under local names bound to the IOKit symbols, so they never clash
// with the SDK headers that declare some of them with stricter types.
extern PulseHIDClient pulse_IOHIDEventSystemClientCreate(CFAllocatorRef allocator) __asm__("_IOHIDEventSystemClientCreate");
extern int pulse_IOHIDEventSystemClientSetMatching(PulseHIDClient client, CFDictionaryRef match) __asm__("_IOHIDEventSystemClientSetMatching");
extern CFArrayRef pulse_IOHIDEventSystemClientCopyServices(PulseHIDClient client) __asm__("_IOHIDEventSystemClientCopyServices");
extern PulseHIDEvent pulse_IOHIDServiceClientCopyEvent(PulseHIDService service, int64_t type, int32_t options, int64_t timestamp) __asm__("_IOHIDServiceClientCopyEvent");
extern CFTypeRef pulse_IOHIDServiceClientCopyProperty(PulseHIDService service, CFStringRef key) __asm__("_IOHIDServiceClientCopyProperty");
extern double pulse_IOHIDEventGetFloatValue(PulseHIDEvent event, int32_t field) __asm__("_IOHIDEventGetFloatValue");

#define PULSE_HID_TEMPERATURE 15
#define PULSE_HID_FIELD_BASE(type) ((type) << 16)

static PulseHIDClient hidClient = NULL;

static int has_prefix(const char *s, const char *p) { return strncmp(s, p, strlen(p)) == 0; }

int pulse_hid_read(PulseHIDTemps *out) {
    out->cpu = out->cpuMax = out->gpu = out->gpuMax = out->storage = out->battery = -1;
    out->sensorCount = 0;
    if (hidClient == NULL) {
        hidClient = pulse_IOHIDEventSystemClientCreate(kCFAllocatorDefault);
        if (hidClient == NULL) return 0;
        int page = 0xff00, usage = 5;
        CFNumberRef pageNum = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &page);
        CFNumberRef usageNum = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &usage);
        const void *keys[] = { CFSTR("PrimaryUsagePage"), CFSTR("PrimaryUsage") };
        const void *vals[] = { pageNum, usageNum };
        CFDictionaryRef match = CFDictionaryCreate(kCFAllocatorDefault, keys, vals, 2,
                                                   &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        pulse_IOHIDEventSystemClientSetMatching(hidClient, match);
        CFRelease(match);
        CFRelease(pageNum);
        CFRelease(usageNum);
    }
    CFArrayRef services = pulse_IOHIDEventSystemClientCopyServices(hidClient);
    if (services == NULL) return 0;

    double accSum = 0, tdieSum = 0, gpuSum = 0, accMax = -1, tdieMax = -1, gpuMax = -1;
    int accN = 0, tdieN = 0, gpuN = 0;
    CFIndex count = CFArrayGetCount(services);
    for (CFIndex k = 0; k < count; k++) {
        PulseHIDService svc = (PulseHIDService)CFArrayGetValueAtIndex(services, k);
        CFTypeRef nameRef = pulse_IOHIDServiceClientCopyProperty(svc, CFSTR("Product"));
        if (nameRef == NULL) continue;
        char name[128] = {0};
        if (CFGetTypeID(nameRef) == CFStringGetTypeID()) {
            CFStringGetCString((CFStringRef)nameRef, name, sizeof name, kCFStringEncodingUTF8);
        }
        CFRelease(nameRef);
        PulseHIDEvent ev = pulse_IOHIDServiceClientCopyEvent(svc, PULSE_HID_TEMPERATURE, 0, 0);
        if (ev == NULL) continue;
        double t = pulse_IOHIDEventGetFloatValue(ev, PULSE_HID_FIELD_BASE(PULSE_HID_TEMPERATURE));
        CFRelease(ev);
        if (!(t > 0 && t < 150)) continue;
        out->sensorCount++;
        if (has_prefix(name, "pACC MTR Temp") || has_prefix(name, "eACC MTR Temp")) {
            accSum += t; accN++; if (t > accMax) accMax = t;
        } else if (has_prefix(name, "PMU tdie") || has_prefix(name, "PMU2 tdie")) {
            tdieSum += t; tdieN++; if (t > tdieMax) tdieMax = t;
        } else if (has_prefix(name, "GPU MTR Temp")) {
            gpuSum += t; gpuN++; if (t > gpuMax) gpuMax = t;
        } else if (has_prefix(name, "NAND")) {
            if (t > out->storage) out->storage = t;
        } else if (strstr(name, "gas gauge") != NULL || strstr(name, "battery") != NULL) {
            out->battery = t;
        }
    }
    CFRelease(services);
    if (accN > 0) { out->cpu = accSum / accN; out->cpuMax = accMax; }
    else if (tdieN > 0) { out->cpu = tdieSum / tdieN; out->cpuMax = tdieMax; }
    if (gpuN > 0) { out->gpu = gpuSum / gpuN; out->gpuMax = gpuMax; }
    return out->sensorCount;
}

// MARK: - SMC

#define KERNEL_INDEX_SMC 2
#define SMC_CMD_READ_BYTES 5
#define SMC_CMD_READ_INDEX 8
#define SMC_CMD_READ_KEYINFO 9

typedef struct { char major; char minor; char build; char reserved[1]; UInt16 release; } SMCKeyData_vers_t;
typedef struct { UInt16 version; UInt16 length; UInt32 cpuPLimit; UInt32 gpuPLimit; UInt32 memPLimit; } SMCKeyData_pLimitData_t;
typedef struct { UInt32 dataSize; UInt32 dataType; char dataAttributes; } SMCKeyData_keyInfo_t;
typedef unsigned char SMCBytes_t[32];
typedef struct {
    UInt32 key;
    SMCKeyData_vers_t vers;
    SMCKeyData_pLimitData_t pLimitData;
    SMCKeyData_keyInfo_t keyInfo;
    char result;
    char status;
    char data8;
    UInt32 data32;
    SMCBytes_t bytes;
} SMCKeyData_t;

static io_connect_t smcConn = 0;

static UInt32 fourcc(const char *s) {
    return ((UInt32)(unsigned char)s[0] << 24) | ((UInt32)(unsigned char)s[1] << 16) |
           ((UInt32)(unsigned char)s[2] << 8) | (UInt32)(unsigned char)s[3];
}

static void fourcc_str(UInt32 v, char *out) {
    out[0] = (char)(v >> 24); out[1] = (char)(v >> 16); out[2] = (char)(v >> 8); out[3] = (char)v; out[4] = 0;
}

static kern_return_t smc_call(SMCKeyData_t *in, SMCKeyData_t *out) {
    size_t outSize = sizeof(SMCKeyData_t);
    return IOConnectCallStructMethod(smcConn, KERNEL_INDEX_SMC, in, sizeof(SMCKeyData_t), out, &outSize);
}

int pulse_smc_open(void) {
    if (smcConn) return 0;
    io_service_t svc = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("AppleSMC"));
    if (!svc) return -1;
    kern_return_t kr = IOServiceOpen(svc, mach_task_self(), 0, &smcConn);
    IOObjectRelease(svc);
    if (kr != KERN_SUCCESS) { smcConn = 0; return -1; }
    return 0;
}

void pulse_smc_close(void) {
    if (smcConn) { IOServiceClose(smcConn); smcConn = 0; }
}

// A small cache of key info, so each read costs one call instead of two.
#define INFO_CACHE 256
static struct { UInt32 key; SMCKeyData_keyInfo_t info; } infoCache[INFO_CACHE];
static int infoCount = 0;

static int key_info(UInt32 key, SMCKeyData_keyInfo_t *info) {
    for (int k = 0; k < infoCount; k++) if (infoCache[k].key == key) { *info = infoCache[k].info; return 0; }
    SMCKeyData_t in, out;
    memset(&in, 0, sizeof in); memset(&out, 0, sizeof out);
    in.key = key;
    in.data8 = SMC_CMD_READ_KEYINFO;
    if (smc_call(&in, &out) != KERN_SUCCESS || out.result != 0) return -1;
    *info = out.keyInfo;
    if (infoCount < INFO_CACHE) { infoCache[infoCount].key = key; infoCache[infoCount].info = out.keyInfo; infoCount++; }
    return 0;
}

int pulse_smc_read(const char *key, double *value) {
    if (!smcConn && pulse_smc_open() != 0) return -1;
    if (strlen(key) != 4) return -1;
    UInt32 k = fourcc(key);
    SMCKeyData_keyInfo_t info;
    if (key_info(k, &info) != 0) return -1;
    SMCKeyData_t in, out;
    memset(&in, 0, sizeof in); memset(&out, 0, sizeof out);
    in.key = k;
    in.keyInfo.dataSize = info.dataSize;
    in.data8 = SMC_CMD_READ_BYTES;
    if (smc_call(&in, &out) != KERN_SUCCESS || out.result != 0) return -1;

    char type[5];
    fourcc_str(info.dataType, type);
    const unsigned char *b = out.bytes;
    UInt32 size = info.dataSize;
    if (strcmp(type, "flt ") == 0 && size == 4) {
        float f; memcpy(&f, b, 4); *value = f;                    // little-endian on Apple silicon
    } else if (strcmp(type, "sp78") == 0 && size == 2) {
        *value = (double)(int16_t)((b[0] << 8) | b[1]) / 256.0;
    } else if (strcmp(type, "sp87") == 0 && size == 2) {
        *value = (double)(int16_t)((b[0] << 8) | b[1]) / 128.0;
    } else if (strcmp(type, "sp96") == 0 && size == 2) {
        *value = (double)(int16_t)((b[0] << 8) | b[1]) / 64.0;
    } else if (strcmp(type, "spa5") == 0 && size == 2) {
        *value = (double)(int16_t)((b[0] << 8) | b[1]) / 32.0;
    } else if (strcmp(type, "fp88") == 0 && size == 2) {
        *value = (double)((b[0] << 8) | b[1]) / 256.0;
    } else if (strcmp(type, "fpe2") == 0 && size == 2) {
        *value = (double)((b[0] << 8) | b[1]) / 4.0;
    } else if (strcmp(type, "ui8 ") == 0 && size >= 1) {
        *value = b[0];
    } else if (strcmp(type, "si8 ") == 0 && size >= 1) {
        *value = (int8_t)b[0];
    } else if (strcmp(type, "ui16") == 0 && size == 2) {
        *value = (double)((b[0] << 8) | b[1]);
    } else if (strcmp(type, "si16") == 0 && size == 2) {
        *value = (double)(int16_t)((b[0] << 8) | b[1]);
    } else if (strcmp(type, "ui32") == 0 && size == 4) {
        *value = (double)(((UInt32)b[0] << 24) | ((UInt32)b[1] << 16) | ((UInt32)b[2] << 8) | b[3]);
    } else {
        return -1;
    }
    return 0;
}

int pulse_smc_key_count(void) {
    double v;
    if (pulse_smc_read("#KEY", &v) != 0) return -1;
    return (int)v;
}

int pulse_smc_key_at(int index, char *out) {
    if (!smcConn && pulse_smc_open() != 0) return -1;
    SMCKeyData_t in, res;
    memset(&in, 0, sizeof in); memset(&res, 0, sizeof res);
    in.data8 = SMC_CMD_READ_INDEX;
    in.data32 = (UInt32)index;
    if (smc_call(&in, &res) != KERN_SUCCESS || res.result != 0) return -1;
    fourcc_str(res.key, out);
    return 0;
}

// MARK: - Responsible pid

typedef pid_t (*pulse_resp_fn)(pid_t);

pid_t pulse_responsible_pid(pid_t pid) {
    static pulse_resp_fn fn = NULL;
    static int looked = 0;
    if (!looked) {
        fn = (pulse_resp_fn)dlsym(RTLD_DEFAULT, "responsibility_get_pid_responsible_for_pid");
        looked = 1;
    }
    return fn ? fn(pid) : -1;
}

#else

int pulse_hid_read(PulseHIDTemps *out) { (void)out; return 0; }
int pulse_smc_open(void) { return -1; }
void pulse_smc_close(void) {}
int pulse_smc_read(const char *key, double *value) { (void)key; (void)value; return -1; }
int pulse_smc_key_count(void) { return -1; }
int pulse_smc_key_at(int index, char *out) { (void)index; (void)out; return -1; }
pid_t pulse_responsible_pid(pid_t pid) { (void)pid; return -1; }

#endif
