// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_WEBUI_PULSE_UI_PULSE_UI_DELEGATE_H_
#define ASH_WEBUI_PULSE_UI_PULSE_UI_DELEGATE_H_

#include "ash/webui/pulse_ui/mojom/pulse_ui.mojom-forward.h"
#include <string>

#include "base/functional/callback_forward.h"

namespace ash::pulse {

// What chrome://pulse needs from //chrome. ash/webui cannot depend on
// //chrome/browser, so ChromePulseUIDelegate implements this there.
class PulseUIDelegate {
 public:
  virtual ~PulseUIDelegate() = default;

  // The ash.pulse.language profile pref: a locale code, or "" to match the
  // system language.
  virtual std::string GetLanguage() = 0;
  virtual void SetLanguage(const std::string& code) = 0;

  // Opens the Diagnostics system web app.
  virtual void OpenDiagnostics() = 0;

  // True when EndGroup(kind) can work right now, such as when the Crostini
  // VM is running.
  virtual bool CanEndGroup(mojom::ProcessGroupKind kind) = 0;

  // Stops the group. Runs `done` with false when that failed.
  virtual void EndGroup(mojom::ProcessGroupKind kind,
                        base::OnceCallback<void(bool)> done) = 0;

  // Starts a group that EndGroup stopped. Runs `done` with false when the
  // group cannot be started.
  virtual void RestoreGroup(mojom::ProcessGroupKind kind,
                            base::OnceCallback<void(bool)> done) = 0;
};

}  // namespace ash::pulse

#endif  // ASH_WEBUI_PULSE_UI_PULSE_UI_DELEGATE_H_
