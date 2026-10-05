// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Entry point for chrome://pulse. Loads the locale catalog
 * (generated from locales/*.json at build time) and starts <pulse-app> in
 * the system UI language from <html lang>.
 */

import './pulse_app.js';

import type {Catalog} from './i18n.js';

async function main() {
  const response = await fetch('pulse_catalog.json');
  const catalog = await response.json() as Catalog;
  const app = document.querySelector('pulse-app');
  if (!app) {
    throw new Error('Missing <pulse-app>');
  }
  await app.start(catalog, document.documentElement.lang || navigator.language);
}

main();
