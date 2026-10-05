// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_WEBUI_PULSE_UI_PULSE_PAGE_HANDLER_H_
#define ASH_WEBUI_PULSE_UI_PULSE_PAGE_HANDLER_H_

#include "ash/webui/pulse_ui/mojom/pulse_ui.mojom.h"
#include "ash/webui/pulse_ui/telemetry/pulse_sampler.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"

namespace ash::pulse {

class PulseUIDelegate;

// One per open chrome://pulse page. Relays PulseSampler snapshots to the
// page and carries out its requests.
class PulsePageHandler : public mojom::PageHandler,
                         public PulseSampler::Observer {
 public:
  PulsePageHandler(mojo::PendingRemote<mojom::Page> page,
                   mojo::PendingReceiver<mojom::PageHandler> receiver,
                   PulseUIDelegate* delegate);
  PulsePageHandler(const PulsePageHandler&) = delete;
  PulsePageHandler& operator=(const PulsePageHandler&) = delete;
  ~PulsePageHandler() override;

  // mojom::PageHandler:
  void GetSnapshot(GetSnapshotCallback callback) override;
  void GetHistory(mojom::HistoryMetric metric,
                  GetHistoryCallback callback) override;
  void GetGroupHistory(mojom::ProcessGroupKind kind,
                       GetGroupHistoryCallback callback) override;
  void EndGroup(mojom::ProcessGroupKind kind,
                EndGroupCallback callback) override;
  void RestoreGroup(mojom::ProcessGroupKind kind,
                    RestoreGroupCallback callback) override;
  void SetLiveUpdates(bool enabled) override;
  void GetLanguage(GetLanguageCallback callback) override;
  void SetLanguage(const std::string& code) override;
  void OpenDiagnostics() override;

  // PulseSampler::Observer:
  void OnSnapshot(const mojom::Snapshot& snapshot) override;

 private:
  // A copy of `snapshot` with can_end filled in by the delegate.
  mojom::SnapshotPtr Decorate(const mojom::Snapshot& snapshot);

  mojo::Remote<mojom::Page> page_;
  mojo::Receiver<mojom::PageHandler> receiver_;
  const raw_ptr<PulseUIDelegate> delegate_;
  // Calls waiting for the first sample.
  std::vector<GetSnapshotCallback> waiting_;
  base::ScopedObservation<PulseSampler, PulseSampler::Observer> observation_{
      this};
  base::WeakPtrFactory<PulsePageHandler> weak_factory_{this};
};

}  // namespace ash::pulse

#endif  // ASH_WEBUI_PULSE_UI_PULSE_PAGE_HANDLER_H_
