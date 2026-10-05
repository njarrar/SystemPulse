// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SYSTEM_PULSE_PULSE_TRAY_H_
#define ASH_SYSTEM_PULSE_PULSE_TRAY_H_

#include "ash/ash_export.h"
#include "ash/public/cpp/pulse/pulse_tray_model.h"
#include "ash/system/tray/tray_background_view.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "ui/base/metadata/metadata_header_macros.h"

namespace views {
class Label;
}

namespace ash {

class Shelf;

// Tier 1 of Pulse in the shelf status area: a pulsing dot, a six-bar CPU
// wave, CPU percent and battery watts. A click opens or closes the Pulse
// window, which anchors to the bottom-end corner above the shelf.
class ASH_EXPORT PulseTray : public TrayBackgroundView,
                             public PulseTrayModel::Observer {
  METADATA_HEADER(PulseTray, TrayBackgroundView)

 public:
  explicit PulseTray(Shelf* shelf);
  PulseTray(const PulseTray&) = delete;
  PulseTray& operator=(const PulseTray&) = delete;
  ~PulseTray() override;

  // TrayBackgroundView:
  void ClickedOutsideBubble(const ui::LocatedEvent& event) override;
  void UpdateTrayItemColor(bool is_active) override;
  std::u16string GetAccessibleNameForTray() override;
  void HandleLocaleChange() override;
  void HideBubbleWithView(const TrayBubbleView* bubble_view) override;
  void HideBubble(const TrayBubbleView* bubble_view) override;

  // PulseTrayModel::Observer:
  void OnPulseReadingsChanged() override;
  void OnPulseTrayVisibilityChanged(bool visible) override;

 private:
  class WaveView;

  void OnPressed(const ui::Event& event);
  void Refresh();

  raw_ptr<WaveView> wave_ = nullptr;
  raw_ptr<views::Label> cpu_label_ = nullptr;
  raw_ptr<views::Label> watts_label_ = nullptr;
  base::ScopedObservation<PulseTrayModel, PulseTrayModel::Observer>
      observation_{this};
};

}  // namespace ash

#endif  // ASH_SYSTEM_PULSE_PULSE_TRAY_H_
