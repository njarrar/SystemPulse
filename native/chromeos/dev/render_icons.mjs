// Renders the Pulse app icons (48, 128, 256 px) into the given folder.
//   node dev/render_icons.mjs chromium/ash/webui/pulse_ui/resources
import fs from 'node:fs';
import {createRequire} from 'node:module';
const require = createRequire(import.meta.url);
const {chromium} = require(process.env.PLAYWRIGHT || '/opt/node22/lib/node_modules/playwright');
const [out] = process.argv.slice(2);
// ChromeOS app icon: app_icon.svg in that folder (a copy of brand/pulse-icon.svg).
const svg = fs.readFileSync(`${out}/app_icon.svg`, 'utf8');
const b = await chromium.launch();
for (const size of [48, 128, 256]) {
  const p = await b.newPage({viewport: {width: size, height: size}});
  await p.setContent(`<html><body style="margin:0;background:transparent">${svg.replace('<svg ', `<svg width="${size}" height="${size}" `)}</body></html>`);
  await p.screenshot({path: `${out}/app_icon_${size}.png`, omitBackground: true});
  await p.close();
}
await b.close();
