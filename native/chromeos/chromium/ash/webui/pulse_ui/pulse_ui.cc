// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/webui/pulse_ui/pulse_ui.h"

#include <utility>

#include "ash/webui/common/trusted_types_util.h"
#include "ash/webui/grit/ash_pulse_ui_resources.h"
#include "ash/webui/grit/ash_pulse_ui_resources_map.h"
#include "ash/webui/pulse_ui/grit/pulse_strings.h"
#include "ash/webui/pulse_ui/pulse_page_handler.h"
#include "ash/webui/pulse_ui/pulse_prefs.h"
#include "ash/webui/pulse_ui/pulse_ui_delegate.h"
#include "ash/webui/pulse_ui/url_constants.h"
#include "ash/webui/system_apps/public/system_web_app_type.h"
#include "components/prefs/pref_registry_simple.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "ui/webui/webui_util.h"

namespace ash::pulse {

PulseUIConfig::PulseUIConfig(DelegateFactory delegate_factory)
    : SystemWebAppUIConfig(kChromeUIPulseHost, SystemWebAppType::PULSE),
      delegate_factory_(std::move(delegate_factory)) {}

PulseUIConfig::~PulseUIConfig() = default;

std::unique_ptr<content::WebUIController>
PulseUIConfig::CreateWebUIController(content::WebUI* web_ui, const GURL& url) {
  return std::make_unique<PulseUI>(web_ui, delegate_factory_.Run(web_ui));
}

PulseUI::PulseUI(content::WebUI* web_ui,
                 std::unique_ptr<PulseUIDelegate> delegate)
    : ui::MojoWebUIController(web_ui), delegate_(std::move(delegate)) {
  content::WebUIDataSource* source =
      content::WebUIDataSource::CreateAndAdd(
          web_ui->GetWebContents()->GetBrowserContext(), kChromeUIPulseHost);

  // The page reads its copy from pulse_catalog.json, built from
  // locales/*.json. Only the title, language and direction come from the
  // pak, so the window title matches before the script runs.
  source->AddLocalizedString("pulseTitle", IDS_PULSE_APP_NAME);
  source->UseStringsJs();
  webui::SetupWebUIDataSource(source, kAshPulseUiResources,
                              IDR_ASH_PULSE_UI_INDEX_HTML);
  // SetupWebUIDataSource already sets textdirection and language through
  // webui::SetLoadTimeDataDefaults.

  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ScriptSrc,
      "script-src chrome://resources 'self';");
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::StyleSrc,
      "style-src chrome://resources 'self' 'unsafe-inline';");
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ImgSrc, "img-src 'self' data:;");
  // The UI builds every node with createElement; it never assigns HTML.
  ash::EnableTrustedTypesCSP(source);
}

PulseUI::~PulseUI() = default;

// static
void PulseUI::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterStringPref(prefs::kPulseLanguage, std::string());
}

void PulseUI::BindInterface(
    mojo::PendingReceiver<mojom::PageHandlerFactory> receiver) {
  factory_receiver_.reset();
  factory_receiver_.Bind(std::move(receiver));
}

void PulseUI::CreatePageHandler(
    mojo::PendingRemote<mojom::Page> page,
    mojo::PendingReceiver<mojom::PageHandler> handler) {
  page_handler_ = std::make_unique<PulsePageHandler>(
      std::move(page), std::move(handler), delegate_.get());
}

WEB_UI_CONTROLLER_TYPE_IMPL(PulseUI)

}  // namespace ash::pulse
