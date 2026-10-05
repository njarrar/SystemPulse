// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/webui/pulse_ui/pulse_page_handler.h"

#include <algorithm>
#include <string>
#include <utility>

#include "ash/webui/pulse_ui/pulse_ui_delegate.h"
#include "base/functional/bind.h"
#include "base/strings/string_util.h"

namespace ash::pulse {

PulsePageHandler::PulsePageHandler(
    mojo::PendingRemote<mojom::Page> page,
    mojo::PendingReceiver<mojom::PageHandler> receiver,
    PulseUIDelegate* delegate)
    : page_(std::move(page)),
      receiver_(this, std::move(receiver)),
      delegate_(delegate) {
  // Moves the sampler to the 1.5 second cadence and sends the latest
  // snapshot right away.
  observation_.Observe(PulseSampler::Get());
}

PulsePageHandler::~PulsePageHandler() = default;

mojom::SnapshotPtr PulsePageHandler::Decorate(const mojom::Snapshot& snapshot) {
  mojom::SnapshotPtr copy = snapshot.Clone();
  for (auto& group : copy->groups) {
    group->can_end = delegate_ && delegate_->CanEndGroup(group->kind);
  }
  if (copy->hog) {
    copy->hog->can_end = delegate_ && delegate_->CanEndGroup(copy->hog->kind);
  }
  return copy;
}

void PulsePageHandler::GetSnapshot(GetSnapshotCallback callback) {
  if (const mojom::Snapshot* latest = PulseSampler::Get()->latest()) {
    std::move(callback).Run(Decorate(*latest));
    return;
  }
  // Only reached before the first sample, while the page is observing.
  waiting_.push_back(std::move(callback));
}

void PulsePageHandler::GetHistory(mojom::HistoryMetric metric,
                                  GetHistoryCallback callback) {
  std::move(callback).Run(PulseSampler::Get()->History(metric));
}

void PulsePageHandler::GetGroupHistory(mojom::ProcessGroupKind kind,
                                       GetGroupHistoryCallback callback) {
  std::move(callback).Run(PulseSampler::Get()->GroupHistory(kind));
}

void PulsePageHandler::EndGroup(mojom::ProcessGroupKind kind,
                                EndGroupCallback callback) {
  if (!delegate_ || !delegate_->CanEndGroup(kind)) {
    std::move(callback).Run(false);
    return;
  }
  delegate_->EndGroup(kind, std::move(callback));
}

void PulsePageHandler::RestoreGroup(mojom::ProcessGroupKind kind,
                                    RestoreGroupCallback callback) {
  if (!delegate_) {
    std::move(callback).Run(false);
    return;
  }
  delegate_->RestoreGroup(kind, std::move(callback));
}

void PulsePageHandler::SetLiveUpdates(bool enabled) {
  // Leaving the observer list lets the sampler drop to its 5 second history
  // cadence when no other page is live.
  if (enabled && !observation_.IsObserving()) {
    observation_.Observe(PulseSampler::Get());
  } else if (!enabled && observation_.IsObserving()) {
    observation_.Reset();
  }
}

void PulsePageHandler::GetLanguage(GetLanguageCallback callback) {
  std::move(callback).Run(delegate_ ? delegate_->GetLanguage() : std::string());
}

void PulsePageHandler::SetLanguage(const std::string& code) {
  // The page sends a code from its own catalog. Keep the pref short and
  // plain so a bad renderer cannot store junk.
  if (!delegate_ || code.size() > 16 ||
      !std::all_of(code.begin(), code.end(), [](char c) {
        return base::IsAsciiAlphaNumeric(c) || c == '-';
      })) {
    return;
  }
  delegate_->SetLanguage(code);
}

void PulsePageHandler::OpenDiagnostics() {
  if (delegate_) {
    delegate_->OpenDiagnostics();
  }
}

void PulsePageHandler::OnSnapshot(const mojom::Snapshot& snapshot) {
  for (auto& callback : std::exchange(waiting_, {})) {
    std::move(callback).Run(Decorate(snapshot));
  }
  page_->OnSnapshot(Decorate(snapshot));
}

}  // namespace ash::pulse
