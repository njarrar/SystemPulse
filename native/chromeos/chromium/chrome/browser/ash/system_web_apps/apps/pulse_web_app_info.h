// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ASH_SYSTEM_WEB_APPS_APPS_PULSE_WEB_APP_INFO_H_
#define CHROME_BROWSER_ASH_SYSTEM_WEB_APPS_APPS_PULSE_WEB_APP_INFO_H_

#include <memory>

#include "ash/webui/system_apps/public/system_web_app_delegate.h"
#include "ui/gfx/geometry/rect.h"

class Browser;
class Profile;

namespace web_app {
struct WebAppInstallInfo;
}

// The Pulse system monitor (chrome://pulse).
class PulseSystemAppDelegate : public ash::SystemWebAppDelegate {
 public:
  explicit PulseSystemAppDelegate(Profile* profile);

  // ash::SystemWebAppDelegate:
  std::unique_ptr<web_app::WebAppInstallInfo> GetWebAppInfo() const override;
  bool ShouldAllowResize() const override;
  bool ShouldAllowMaximize() const override;
  bool ShouldShowInLauncher() const override;
  bool ShouldShowInSearchAndShelf() const override;
  bool ShouldReuseExistingWindow() const override;
  gfx::Rect GetDefaultBounds(Browser* browser) const override;
};

// Exposed for tests: 420 x 860, at the bottom-end corner of the work area
// next to the shelf readout.
gfx::Rect GetDefaultBoundsForPulse(Browser* browser);

#endif  // CHROME_BROWSER_ASH_SYSTEM_WEB_APPS_APPS_PULSE_WEB_APP_INFO_H_
