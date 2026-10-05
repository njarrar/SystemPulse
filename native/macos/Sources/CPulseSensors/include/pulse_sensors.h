// SMC and IOHID temperature access for Pulse. macOS only; the functions are
// stubs that return "not available" elsewhere.
#ifndef PULSE_SENSORS_H
#define PULSE_SENSORS_H

#include <sys/types.h>
#ifdef __APPLE__
#include <libproc.h>
#include <sys/proc_info.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/// Temperatures from the IOHID event system (Apple silicon). Values are in
/// degrees Celsius; a field is negative when no sensor of that kind exists.
typedef struct {
    double cpu;        // mean of pACC/eACC MTR sensors, else PMU tdie
    double cpuMax;
    double gpu;        // mean of GPU MTR sensors
    double gpuMax;
    double storage;    // NAND
    double battery;    // gas gauge
    int sensorCount;
} PulseHIDTemps;

/// Reads every HID temperature sensor once. Returns the number of sensors read.
int pulse_hid_read(PulseHIDTemps *out);

/// Opens a connection to AppleSMC. Returns 0 on success.
int pulse_smc_open(void);
void pulse_smc_close(void);

/// Reads a four-character SMC key as a number. Returns 0 on success.
/// Handles sp78, sp87, sp96, spa5, fp88, fpe2, flt, ui8, ui16, ui32, si8, si16.
int pulse_smc_read(const char *key, double *value);

/// Number of SMC keys ("#KEY"), or -1.
int pulse_smc_key_count(void);

/// The key at an index, written to out (5 bytes, NUL-terminated). Returns 0 on success.
int pulse_smc_key_at(int index, char *out);

/// The pid macOS holds responsible for a process (an XPC helper's host app),
/// or -1 when the system call is missing.
pid_t pulse_responsible_pid(pid_t pid);

#ifdef __cplusplus
}
#endif

#endif
