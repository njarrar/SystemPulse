// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/pulse/pulse_tray.h"

#include <algorithm>

#include "ash/shelf/shelf.h"
#include "ash/style/ash_color_id.h"
#include "ash/style/typography.h"
#include "ash/system/tray/tray_constants.h"
#include "ash/system/tray/tray_container.h"
#include "base/functional/bind.h"
#include "cc/paint/paint_flags.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/chromeos/styles/cros_tokens_color_mappings.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/controls/label.h"
#include "ui/views/view.h"

namespace ash {

namespace {

// From the Part 1 tokens: --cpu and --warn, light and dark.
constexpr SkColor kCpuLight = SkColorSetRGB(0x10, 0xB9, 0x81);
constexpr SkColor kCpuDark = SkColorSetRGB(0x34, 0xD3, 0x99);
constexpr SkColor kWarnLight = SkColorSetRGB(0xF9, 0x73, 0x16);
constexpr SkColor kWarnDark = SkColorSetRGB(0xFB, 0x92, 0x3C);

constexpr int kDot = 6;
constexpr int kBarWidth = 2;
constexpr int kBarGap = 2;
constexpr int kWaveHeight = 14;

}  // namespace

// The dot and the six bars. Bar height: 3 + v / 70 * 11 px, clamped to
// 3..14, as in the prototype.
class PulseTray::WaveView : public views::View {
  METADATA_HEADER(WaveView, views::View)

 public:
  WaveView() {
    SetPreferredSize(gfx::Size(
        kDot + 6 +
            PulseTrayModel::kWaveBars * (kBarWidth + kBarGap) - kBarGap,
        kWaveHeight));
  }

  void SetWave(const std::array<double, PulseTrayModel::kWaveBars>& wave,
               bool alert) {
    wave_ = wave;
    alert_ = alert;
    SchedulePaint();
  }

  void OnPaint(gfx::Canvas* canvas) override {
    const bool dark = GetColorProvider() &&
                      color_utils::IsDark(GetColorProvider()->GetColor(
                          cros_tokens::kCrosSysSystemBase));
    const SkColor color = alert_ ? (dark ? kWarnDark : kWarnLight)
                                 : (dark ? kCpuDark : kCpuLight);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(color);
    const int mid = height() / 2;
    canvas->DrawCircle(gfx::PointF(kDot / 2.0f, mid), kDot / 2.0f, flags);

    int x = kDot + 6;
    for (double v : wave_) {
      const float h = std::clamp(3.0 + v / 70.0 * 11.0, 3.0, 14.0);
      gfx::RectF bar(GetMirroredXWithWidthInView(x, kBarWidth),
                     mid - h / 2.0f, kBarWidth, h);
      canvas->DrawRoundRect(bar, 1, flags);
      x += kBarWidth + kBarGap;
    }
  }

 private:
  std::array<double, PulseTrayModel::kWaveBars> wave_{};
  bool alert_ = false;
};

BEGIN_METADATA(PulseTray, WaveView)
END_METADATA

PulseTray::PulseTray(Shelf* shelf)
    : TrayBackgroundView(shelf, TrayBackgroundViewCatalogName::kPulse) {
  SetCallback(base::BindRepeating(&PulseTray::OnPressed,
                                  base::Unretained(this)));
  tray_container()->SetMargin(kTrayContainerYPadding, 10);

  wave_ = tray_container()->AddChildView(std::make_unique<WaveView>());

  auto make_label = [this]() {
    auto label = std::make_unique<views::Label>();
    label->SetAutoColorReadabilityEnabled(false);
    label->SetEnabledColorId(cros_tokens::kCrosSysOnSurface);
    TypographyProvider::Get()->StyleLabel(TypographyToken::kCrosButton2,
                                          *label);
    // PulseTrayBridge wraps the text in LTR marks, so signs and units keep
    // their order in RTL locales.
    label->SetFontList(label->font_list().DeriveWithWeight(
        gfx::Font::Weight::SEMIBOLD));
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    label->SetElideBehavior(gfx::NO_ELIDE);
    return tray_container()->AddChildView(std::move(label));
  };
  cpu_label_ = make_label();
  watts_label_ = make_label();
  watts_label_->SetEnabledColorId(cros_tokens::kCrosSysOnSurfaceVariant);

  observation_.Observe(PulseTrayModel::Get());
  SetVisiblePreferred(PulseTrayModel::Get()->visible());
  Refresh();
}

PulseTray::~PulseTray() = default;

void PulseTray::ClickedOutsideBubble(const ui::LocatedEvent& event) {}

void PulseTray::UpdateTrayItemColor(bool is_active) {
  wave_->SchedulePaint();
}

std::u16string PulseTray::GetAccessibleNameForTray() {
  return PulseTrayModel::Get()->readings().accessible_name;
}

void PulseTray::HandleLocaleChange() {
  Refresh();
}

void PulseTray::HideBubbleWithView(const TrayBubbleView* bubble_view) {}

void PulseTray::HideBubble(const TrayBubbleView* bubble_view) {}

void PulseTray::OnPulseReadingsChanged() {
  Refresh();
}

void PulseTray::OnPulseTrayVisibilityChanged(bool visible) {
  SetVisiblePreferred(visible);
}

void PulseTray::OnPressed(const ui::Event& event) {
  PulseTrayModel::Get()->Toggle();
}

void PulseTray::Refresh() {
  const PulseTrayModel::Readings& r = PulseTrayModel::Get()->readings();
  wave_->SetWave(r.wave, r.alert);
  cpu_label_->SetText(r.cpu_text);
  watts_label_->SetText(r.watts_text);
  cpu_label_->SetEnabledColorId(r.alert ? cros_tokens::kCrosSysWarning
                                        : cros_tokens::kCrosSysOnSurface);
  UpdateAccessibleName();
}

BEGIN_METADATA(PulseTray)
END_METADATA

}  // namespace ash
