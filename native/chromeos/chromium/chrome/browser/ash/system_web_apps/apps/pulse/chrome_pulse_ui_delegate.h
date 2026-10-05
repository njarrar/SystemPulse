// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ASH_SYSTEM_WEB_APPS_APPS_PULSE_CHROME_PULSE_UI_DELEGATE_H_
#define CHROME_BROWSER_ASH_SYSTEM_WEB_APPS_APPS_PULSE_CHROME_PULSE_UI_DELEGATE_H_

#include "ash/webui/pulse_ui/pulse_ui_delegate.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"

class Profile;

namespace content {
class WebUI;
}

namespace ash::pulse {

// Launches Diagnostics and stops or restarts the Linux VM for chrome://pulse.
//
// Only Crostini can be ended. The ARCVM row stays read-only: stopping
// Android closes every Android app and is a policy-controlled action, and
// Chrome, the system UI and system daemons cannot be ended from here.
class ChromePulseUIDelegate : public PulseUIDelegate {
 public:
  explicit ChromePulseUIDelegate(content::WebUI* web_ui);
  ChromePulseUIDelegate(const ChromePulseUIDelegate&) = delete;
  ChromePulseUIDelegate& operator=(const ChromePulseUIDelegate&) = delete;
  ~ChromePulseUIDelegate() override;

  // PulseUIDelegate:
  std::string GetLanguage() override;
  void SetLanguage(const std::string& code) override;
  void OpenDiagnostics() override;
  bool CanEndGroup(mojom::ProcessGroupKind kind) override;
  void EndGroup(mojom::ProcessGroupKind kind,
                base::OnceCallback<void(bool)> done) override;
  void RestoreGroup(mojom::ProcessGroupKind kind,
                    base::OnceCallback<void(bool)> done) override;

 private:
  raw_ptr<Profile> profile_;
  base::WeakPtrFactory<ChromePulseUIDelegate> weak_factory_{this};
};

}  // namespace ash::pulse

#endif  // CHROME_BROWSER_ASH_SYSTEM_WEB_APPS_APPS_PULSE_CHROME_PULSE_UI_DELEGATE_H_
