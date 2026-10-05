// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_PUBLIC_CPP_PULSE_PULSE_TRAY_MODEL_H_
#define ASH_PUBLIC_CPP_PULSE_PULSE_TRAY_MODEL_H_

#include <array>
#include <string>

#include "ash/public/cpp/ash_public_export.h"
#include "base/functional/callback.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"

namespace ash {

// Tier 1 of Pulse: the readout in the shelf status area. //chrome fills it
// from PulseSampler; ash/system/pulse/PulseTray draws it. Kept in
// ash/public so //ash does not depend on //ash/webui.
class ASH_PUBLIC_EXPORT PulseTrayModel {
 public:
  // Six bars, oldest first, each 0 to 100 (CPU percent).
  static constexpr size_t kWaveBars = 6;

  struct Readings {
    std::array<double, kWaveBars> wave{};
    // Already formatted for the UI locale, for example "23%" and "9.4W".
    std::u16string cpu_text;
    std::u16string watts_text;
    // Spoken by screen readers, for example "Pulse, CPU 23%, 9.4 W".
    std::u16string accessible_name;
    // True while one process group holds the CPU (the hog alert).
    bool alert = false;
  };

  class Observer : public base::CheckedObserver {
   public:
    virtual void OnPulseReadingsChanged() = 0;
    virtual void OnPulseTrayVisibilityChanged(bool visible) = 0;
  };

  static PulseTrayModel* Get();

  PulseTrayModel(const PulseTrayModel&) = delete;
  PulseTrayModel& operator=(const PulseTrayModel&) = delete;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  const Readings& readings() const { return readings_; }
  void SetReadings(Readings readings);

  bool visible() const { return visible_; }
  void SetVisible(bool visible);

  // Opens or closes the Pulse window; set by //chrome.
  void SetToggleCallback(base::RepeatingClosure callback);
  void Toggle();

 private:
  friend class base::NoDestructor<PulseTrayModel>;
  PulseTrayModel();
  ~PulseTrayModel();

  Readings readings_;
  bool visible_ = false;
  base::RepeatingClosure toggle_;
  base::ObserverList<Observer> observers_;
};

}  // namespace ash

#endif  // ASH_PUBLIC_CPP_PULSE_PULSE_TRAY_MODEL_H_
