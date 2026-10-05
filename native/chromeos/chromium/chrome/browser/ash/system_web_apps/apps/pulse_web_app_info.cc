// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/system_web_apps/apps/pulse_web_app_info.h"

#include <algorithm>
#include <memory>

#include "ash/webui/grit/ash_pulse_ui_resources.h"
#include "ash/webui/pulse_ui/grit/pulse_strings.h"
#include "ash/webui/pulse_ui/url_constants.h"
#include "ash/webui/system_apps/public/system_web_app_type.h"
#include "base/i18n/rtl.h"
#include "chrome/browser/ash/system_web_apps/apps/system_web_app_install_utils.h"
#include "chrome/browser/web_applications/mojom/user_display_mode.mojom.h"
#include "chrome/browser/web_applications/web_app_install_info.h"
#include "third_party/blink/public/mojom/manifest/display_mode.mojom.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/display/screen.h"

namespace {

constexpr int kWidth = 420;
constexpr int kHeight = 860;
constexpr int kMargin = 8;

}  // namespace

std::unique_ptr<web_app::WebAppInstallInfo>
PulseSystemAppDelegate::GetWebAppInfo() const {
  GURL start_url(ash::kChromeUIPulseUrl);
  auto info =
      web_app::CreateSystemWebAppInstallInfoWithStartUrlAsIdentity(start_url);
  info->scope = start_url;
  info->title = l10n_util::GetStringUTF16(IDS_PULSE_APP_NAME);
  web_app::CreateIconInfoForSystemWebApp(
      info->start_url(),
      {
          {"app_icon_48.png", 48, IDR_ASH_PULSE_UI_APP_ICON_48_PNG},
          {"app_icon_128.png", 128, IDR_ASH_PULSE_UI_APP_ICON_128_PNG},
          {"app_icon_256.png", 256, IDR_ASH_PULSE_UI_APP_ICON_256_PNG},
      },
      *info);
  // Part 1 surface tokens: --fly-solid light and dark.
  info->theme_color = 0xFFF5F8F6;
  info->dark_mode_theme_color = 0xFF0F1A16;
  info->background_color = info->theme_color;
  info->dark_mode_background_color = info->dark_mode_theme_color;
  info->display_mode = blink::mojom::DisplayMode::kStandalone;
  info->user_display_mode = web_app::mojom::UserDisplayMode::kStandalone;
  return info;
}

PulseSystemAppDelegate::PulseSystemAppDelegate(Profile* profile)
    : ash::SystemWebAppDelegate(ash::SystemWebAppType::PULSE,
                                "Pulse",
                                GURL(ash::kChromeUIPulseUrl),
                                profile) {}

bool PulseSystemAppDelegate::ShouldAllowResize() const {
  return false;
}

bool PulseSystemAppDelegate::ShouldAllowMaximize() const {
  return false;
}

bool PulseSystemAppDelegate::ShouldShowInLauncher() const {
  return true;
}

bool PulseSystemAppDelegate::ShouldShowInSearchAndShelf() const {
  return true;
}

bool PulseSystemAppDelegate::ShouldReuseExistingWindow() const {
  return true;
}

gfx::Rect PulseSystemAppDelegate::GetDefaultBounds(Browser* browser) const {
  return GetDefaultBoundsForPulse(browser);
}

gfx::Rect GetDefaultBoundsForPulse(Browser* browser) {
  const gfx::Rect work_area =
      display::Screen::GetScreen()->GetDisplayForNewWindows().work_area();
  const int width = std::min(kWidth, work_area.width() - 2 * kMargin);
  const int height = std::min(kHeight, work_area.height() - 2 * kMargin);
  // Bottom-end: the right corner in LTR, the left corner in RTL, above the
  // shelf readout.
  const int x = base::i18n::IsRTL()
                    ? work_area.x() + kMargin
                    : work_area.right() - kMargin - width;
  return gfx::Rect(x, work_area.bottom() - kMargin - height, width, height);
}
