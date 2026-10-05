// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_WEBUI_PULSE_UI_PULSE_UI_H_
#define ASH_WEBUI_PULSE_UI_PULSE_UI_H_

#include <memory>

#include "ash/webui/pulse_ui/mojom/pulse_ui.mojom.h"
#include "ash/webui/system_apps/public/system_web_app_ui_config.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "ui/webui/mojo_web_ui_controller.h"

class PrefRegistrySimple;

namespace ash::pulse {

class PulsePageHandler;
class PulseUI;
class PulseUIDelegate;

// Hosts chrome://pulse. //chrome registers it with a delegate factory.
class PulseUIConfig : public SystemWebAppUIConfig<PulseUI> {
 public:
  using DelegateFactory =
      base::RepeatingCallback<std::unique_ptr<PulseUIDelegate>(
          content::WebUI*)>;

  explicit PulseUIConfig(DelegateFactory delegate_factory);
  ~PulseUIConfig() override;

  std::unique_ptr<content::WebUIController> CreateWebUIController(
      content::WebUI* web_ui,
      const GURL& url) override;

 private:
  DelegateFactory delegate_factory_;
};

class PulseUI : public ui::MojoWebUIController,
                public mojom::PageHandlerFactory {
 public:
  PulseUI(content::WebUI* web_ui, std::unique_ptr<PulseUIDelegate> delegate);
  PulseUI(const PulseUI&) = delete;
  PulseUI& operator=(const PulseUI&) = delete;
  ~PulseUI() override;

  // Registers ash.pulse.language.
  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  void BindInterface(
      mojo::PendingReceiver<mojom::PageHandlerFactory> receiver);

  // mojom::PageHandlerFactory:
  void CreatePageHandler(
      mojo::PendingRemote<mojom::Page> page,
      mojo::PendingReceiver<mojom::PageHandler> handler) override;

 private:
  std::unique_ptr<PulseUIDelegate> delegate_;
  std::unique_ptr<PulsePageHandler> page_handler_;
  mojo::Receiver<mojom::PageHandlerFactory> factory_receiver_{this};

  WEB_UI_CONTROLLER_TYPE_DECL();
};

}  // namespace ash::pulse

#endif  // ASH_WEBUI_PULSE_UI_PULSE_UI_H_
