// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/public/cpp/pulse/pulse_tray_model.h"

#include <utility>

#include "base/no_destructor.h"

namespace ash {

// static
PulseTrayModel* PulseTrayModel::Get() {
  static base::NoDestructor<PulseTrayModel> instance;
  return instance.get();
}

PulseTrayModel::PulseTrayModel() = default;
PulseTrayModel::~PulseTrayModel() = default;

void PulseTrayModel::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void PulseTrayModel::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void PulseTrayModel::SetReadings(Readings readings) {
  readings_ = std::move(readings);
  for (Observer& observer : observers_) {
    observer.OnPulseReadingsChanged();
  }
}

void PulseTrayModel::SetVisible(bool visible) {
  if (visible_ == visible) {
    return;
  }
  visible_ = visible;
  for (Observer& observer : observers_) {
    observer.OnPulseTrayVisibilityChanged(visible);
  }
}

void PulseTrayModel::SetToggleCallback(base::RepeatingClosure callback) {
  toggle_ = std::move(callback);
}

void PulseTrayModel::Toggle() {
  if (toggle_) {
    toggle_.Run();
  }
}

}  // namespace ash
