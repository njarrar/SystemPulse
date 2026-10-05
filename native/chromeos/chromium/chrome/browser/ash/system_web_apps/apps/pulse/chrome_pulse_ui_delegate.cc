// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/system_web_apps/apps/pulse/chrome_pulse_ui_delegate.h"

#include <utility>

#include "ash/webui/pulse_ui/mojom/pulse_ui.mojom.h"
#include "ash/webui/pulse_ui/pulse_prefs.h"
#include "ash/webui/system_apps/public/system_web_app_type.h"
#include "base/functional/bind.h"
#include "chrome/browser/ash/crostini/crostini_manager.h"
#include "chrome/browser/ash/crostini/crostini_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/ash/system_web_apps/system_web_app_ui_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_ui.h"

namespace ash::pulse {

ChromePulseUIDelegate::ChromePulseUIDelegate(content::WebUI* web_ui)
    : profile_(Profile::FromWebUI(web_ui)) {}

ChromePulseUIDelegate::~ChromePulseUIDelegate() = default;

std::string ChromePulseUIDelegate::GetLanguage() {
  return profile_->GetPrefs()->GetString(prefs::kPulseLanguage);
}

void ChromePulseUIDelegate::SetLanguage(const std::string& code) {
  profile_->GetPrefs()->SetString(prefs::kPulseLanguage, code);
}

void ChromePulseUIDelegate::OpenDiagnostics() {
  LaunchSystemWebAppAsync(profile_, SystemWebAppType::DIAGNOSTICS);
}

bool ChromePulseUIDelegate::CanEndGroup(mojom::ProcessGroupKind kind) {
  if (kind != mojom::ProcessGroupKind::kCrostini) {
    return false;
  }
  auto* manager = crostini::CrostiniManager::GetForProfile(profile_);
  return manager && manager->IsVmRunning(crostini::kCrostiniDefaultVmName);
}

void ChromePulseUIDelegate::EndGroup(mojom::ProcessGroupKind kind,
                                     base::OnceCallback<void(bool)> done) {
  if (!CanEndGroup(kind)) {
    std::move(done).Run(false);
    return;
  }
  crostini::CrostiniManager::GetForProfile(profile_)->StopVm(
      crostini::kCrostiniDefaultVmName,
      base::BindOnce(
          [](base::OnceCallback<void(bool)> done,
             crostini::CrostiniResult result) {
            std::move(done).Run(result == crostini::CrostiniResult::SUCCESS);
          },
          std::move(done)));
}

void ChromePulseUIDelegate::RestoreGroup(mojom::ProcessGroupKind kind,
                                         base::OnceCallback<void(bool)> done) {
  auto* manager = crostini::CrostiniManager::GetForProfile(profile_);
  if (kind != mojom::ProcessGroupKind::kCrostini || !manager) {
    std::move(done).Run(false);
    return;
  }
  manager->RestartCrostini(
      crostini::DefaultContainerId(),
      base::BindOnce(
          [](base::OnceCallback<void(bool)> done,
             crostini::CrostiniResult result) {
            std::move(done).Run(result == crostini::CrostiniResult::SUCCESS);
          },
          std::move(done)));
}

}  // namespace ash::pulse
