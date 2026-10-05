// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_WEBUI_PULSE_UI_PULSE_PREFS_H_
#define ASH_WEBUI_PULSE_UI_PULSE_PREFS_H_

namespace ash::pulse::prefs {

// String. The Pulse app language: a locale code such as "ar", or "" to
// match the system language. Set from Settings > Language and from the
// header switcher.
inline constexpr char kPulseLanguage[] = "ash.pulse.language";

}  // namespace ash::pulse::prefs

#endif  // ASH_WEBUI_PULSE_UI_PULSE_PREFS_H_
