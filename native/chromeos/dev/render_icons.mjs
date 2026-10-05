// Renders the Pulse app icons (48, 128, 256 px) into the given folder.
//   node dev/render_icons.mjs chromium/ash/webui/pulse_ui/resources
import {createRequire} from 'node:module';
const require = createRequire(import.meta.url);
const {chromium} = require(process.env.PLAYWRIGHT || '/opt/node22/lib/node_modules/playwright');
const [out] = process.argv.slice(2);
// ChromeOS app icon: a round tile with the Pulse dual-ring emblem.
const svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256">
<defs><linearGradient id="bg" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#FFFFFF"/><stop offset="1" stop-color="#E8F5EF"/></linearGradient></defs>
<circle cx="128" cy="128" r="120" fill="url(#bg)"/>
<circle cx="128" cy="128" r="119" fill="none" stroke="#0E1E19" stroke-opacity="0.08" stroke-width="2"/>
<circle cx="104" cy="128" r="50" fill="none" stroke="#10B981" stroke-width="22"/>
<circle cx="152" cy="128" r="50" fill="none" stroke="#0EA5E9" stroke-width="22" stroke-opacity="0.9"/>
</svg>`;
const b = await chromium.launch();
for (const size of [48, 128, 256]) {
  const p = await b.newPage({viewport: {width: size, height: size}});
  await p.setContent(`<html><body style="margin:0;background:transparent">${svg.replace('<svg ', `<svg width="${size}" height="${size}" `)}</body></html>`);
  await p.screenshot({path: `${out}/app_icon_${size}.png`, omitBackground: true});
  await p.close();
}
await b.close();
