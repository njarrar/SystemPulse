// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ASH_PULSE_PULSE_TRAY_BRIDGE_H_
#define CHROME_BROWSER_UI_ASH_PULSE_PULSE_TRAY_BRIDGE_H_

#include "ash/webui/pulse_ui/telemetry/pulse_sampler.h"
#include "base/containers/circular_deque.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"

class Profile;

namespace ash::pulse {

// Feeds the shelf readout (Tier 1) from PulseSampler and opens the Pulse
// window when the readout is clicked. Lives as long as the user session.
class PulseTrayBridge : public PulseSampler::Observer {
 public:
  explicit PulseTrayBridge(Profile* profile);
  PulseTrayBridge(const PulseTrayBridge&) = delete;
  PulseTrayBridge& operator=(const PulseTrayBridge&) = delete;
  ~PulseTrayBridge() override;

  // PulseSampler::Observer:
  void OnSnapshot(const mojom::Snapshot& snapshot) override;

 private:
  void Toggle();

  raw_ptr<Profile> profile_;
  base::circular_deque<double> cpu_;
  base::ScopedObservation<PulseSampler, PulseSampler::Observer> observation_{
      this};
};

}  // namespace ash::pulse

#endif  // CHROME_BROWSER_UI_ASH_PULSE_PULSE_TRAY_BRIDGE_H_
