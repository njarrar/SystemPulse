// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/ash/pulse/pulse_tray_bridge.h"

#include <cmath>
#include <memory>
#include <string>
#include <utility>

#include "ash/public/cpp/pulse/pulse_tray_model.h"
#include "ash/webui/pulse_ui/grit/pulse_strings.h"
#include "ash/webui/system_apps/public/system_web_app_type.h"
#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ash/system_web_apps/system_web_app_manager.h"
#include "chrome/browser/ui/ash/system_web_apps/system_web_app_ui_utils.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "third_party/icu/source/common/unicode/locid.h"
#include "third_party/icu/source/i18n/unicode/numfmt.h"
#include "ui/base/l10n/l10n_util.h"

namespace ash::pulse {

namespace {

std::u16string Ltr(std::u16string text) {
  base::i18n::WrapStringWithLTRFormatting(&text);
  return text;
}

// Formats like Locale.num() in i18n.ts: the UI locale with the digits the
// Pulse catalog names (for example "latn" for Arabic), so the shelf and the
// window show the same figures.
std::u16string Num(double value, int decimals) {
  const std::string numbers = base::UTF16ToUTF8(
      l10n_util::GetStringUTF16(IDS_PULSE_META_NUMBERING_SYSTEM));
  const icu::Locale locale(
      base::StrCat({base::i18n::GetConfiguredLocale(), "@numbers=", numbers})
          .c_str());
  UErrorCode status = U_ZERO_ERROR;
  std::unique_ptr<icu::NumberFormat> format(
      icu::NumberFormat::createInstance(locale, status));
  if (U_FAILURE(status)) {
    return base::UTF8ToUTF16(base::NumberToString(value));
  }
  format->setMinimumFractionDigits(decimals);
  format->setMaximumFractionDigits(decimals);
  icu::UnicodeString out;
  format->format(value, out);
  return std::u16string(out.getBuffer(), static_cast<size_t>(out.length()));
}

}  // namespace

PulseTrayBridge::PulseTrayBridge(Profile* profile) : profile_(profile) {
  PulseTrayModel* model = PulseTrayModel::Get();
  model->SetToggleCallback(base::BindRepeating(&PulseTrayBridge::Toggle,
                                               base::Unretained(this)));
  model->SetVisible(true);
  // Start the wave from history so it is full on the first push.
  for (double v : PulseSampler::Get()->History(mojom::HistoryMetric::kCpu)) {
    cpu_.push_back(v);
    if (cpu_.size() > PulseTrayModel::kWaveBars - 1) {
      cpu_.pop_front();
    }
  }
  // The readout is always on screen, so the sampler runs at 1.5 s.
  observation_.Observe(PulseSampler::Get());
}

PulseTrayBridge::~PulseTrayBridge() {
  PulseTrayModel* model = PulseTrayModel::Get();
  model->SetVisible(false);
  model->SetToggleCallback(base::RepeatingClosure());
}

void PulseTrayBridge::OnSnapshot(const mojom::Snapshot& snapshot) {
  cpu_.push_back(snapshot.cpu->usage_percent);
  while (cpu_.size() > PulseTrayModel::kWaveBars) {
    cpu_.pop_front();
  }
  PulseTrayModel::Readings r;
  const size_t pad = PulseTrayModel::kWaveBars - cpu_.size();
  for (size_t i = 0; i < cpu_.size(); ++i) {
    r.wave[pad + i] = cpu_[i];
  }
  const int cpu = static_cast<int>(std::lround(snapshot.cpu->usage_percent));
  const double watts =
      snapshot.battery ? std::fabs(snapshot.battery->power_watts) : 0.0;
  r.cpu_text = Ltr(base::StrCat({Num(cpu, 0), u"%"}));
  r.watts_text = Ltr(base::StrCat({Num(watts, 1), u"W"}));
  r.alert = !snapshot.hog.is_null();
  const std::u16string sep = l10n_util::GetStringUTF16(IDS_PULSE_S_LIST_SEP);
  r.accessible_name = base::StrCat(
      {l10n_util::GetStringUTF16(IDS_PULSE_APP_NAME), sep,
       l10n_util::GetStringUTF16(IDS_PULSE_S_BAR_CPU), u" ", r.cpu_text, sep,
       Ltr(base::StrCat({Num(watts, 1), u" W"}))});
  PulseTrayModel::Get()->SetReadings(std::move(r));
}

void PulseTrayBridge::Toggle() {
  if (Browser* browser =
          FindSystemWebAppBrowser(profile_, SystemWebAppType::PULSE)) {
    if (browser->window()->IsActive()) {
      browser->window()->Close();
      return;
    }
    browser->window()->Activate();
    return;
  }
  LaunchSystemWebAppAsync(profile_, SystemWebAppType::PULSE);
}

}  // namespace ash::pulse
