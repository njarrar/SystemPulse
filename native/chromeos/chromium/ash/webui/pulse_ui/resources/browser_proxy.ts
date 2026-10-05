// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Binds the Pulse page to its ash-side handler through
 * PageHandlerFactory. Tests swap the instance with setInstance().
 */

import {PageCallbackRouter, PageHandlerFactory, PageHandlerRemote} from './pulse_ui.mojom-webui.js';
import type {PageHandlerInterface} from './pulse_ui.mojom-webui.js';

export interface PulseBrowserProxy {
  readonly handler: PageHandlerInterface;
  readonly callbackRouter: PageCallbackRouter;
}

export class PulseBrowserProxyImpl implements PulseBrowserProxy {
  readonly handler: PageHandlerRemote;
  readonly callbackRouter: PageCallbackRouter;

  private constructor() {
    this.callbackRouter = new PageCallbackRouter();
    this.handler = new PageHandlerRemote();
    PageHandlerFactory.getRemote().createPageHandler(
        this.callbackRouter.$.bindNewPipeAndPassRemote(),
        this.handler.$.bindNewPipeAndPassReceiver());
  }

  static getInstance(): PulseBrowserProxy {
    return instance || (instance = new PulseBrowserProxyImpl());
  }

  static setInstance(proxy: PulseBrowserProxy) {
    instance = proxy;
  }
}

let instance: PulseBrowserProxy|null = null;
