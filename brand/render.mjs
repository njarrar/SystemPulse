// Renders the Pulse icon from pulse-icon.svg into every place that ships it.
//   node brand/render.mjs
// Writes:
//   brand/pulse-icon-512.png, brand/social-preview.png (1280 x 640)
//   native/chromeos/.../resources/app_icon.svg and app_icon_{48,128,256}.png
//   native/win11/src/Pulse.App/Assets/Pulse.ico (16 to 256 px)
//   native/macos/Assets/AppIcon.icns (16 to 1024 px)
// Windows XP and 98 draw their .ico at build time (tools/make_icon.py,
// tools/gen_icon.py) with the same shapes.
import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
const require = createRequire(import.meta.url);
const {chromium} = require(process.env.PLAYWRIGHT || '/opt/node22/lib/node_modules/playwright');

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.join(HERE, '..');
const svg = fs.readFileSync(path.join(HERE, 'pulse-icon.svg'), 'utf8');
const CHROMEOS = path.join(ROOT, 'native/chromeos/chromium/ash/webui/pulse_ui/resources');

const b = await chromium.launch();

async function png(size) {
  const p = await b.newPage({viewport: {width: size, height: size}});
  await p.setContent(`<html><body style="margin:0;background:transparent">${svg.replace('<svg ', `<svg width="${size}" height="${size}" `)}</body></html>`);
  const buf = await p.screenshot({omitBackground: true});
  await p.close();
  return buf;
}

// ICO with PNG entries (Vista and later read these).
function ico(images) {
  const head = Buffer.alloc(6 + 16 * images.length);
  head.writeUInt16LE(0, 0); head.writeUInt16LE(1, 2); head.writeUInt16LE(images.length, 4);
  let off = head.length;
  images.forEach(([size, data], i) => {
    const e = 6 + 16 * i;
    head.writeUInt8(size >= 256 ? 0 : size, e); head.writeUInt8(size >= 256 ? 0 : size, e + 1);
    head.writeUInt16LE(1, e + 4); head.writeUInt16LE(32, e + 6);
    head.writeUInt32LE(data.length, e + 8); head.writeUInt32LE(off, e + 12);
    off += data.length;
  });
  return Buffer.concat([head, ...images.map(([, d]) => d)]);
}

// ICNS with PNG entries.
function icns(entries) {
  const parts = entries.map(([type, data]) => {
    const h = Buffer.alloc(8); h.write(type, 0, 'ascii'); h.writeUInt32BE(data.length + 8, 4);
    return Buffer.concat([h, data]);
  });
  const body = Buffer.concat(parts);
  const h = Buffer.alloc(8); h.write('icns', 0, 'ascii'); h.writeUInt32BE(body.length + 8, 4);
  return Buffer.concat([h, body]);
}

const sizes = [16, 24, 32, 48, 64, 128, 256, 512, 1024];
const P = {};
for (const s of sizes) P[s] = await png(s);

fs.writeFileSync(path.join(HERE, 'pulse-icon-512.png'), P[512]);
fs.writeFileSync(path.join(CHROMEOS, 'app_icon.svg'), svg);
for (const s of [48, 128, 256]) fs.writeFileSync(path.join(CHROMEOS, `app_icon_${s}.png`), P[s]);
fs.writeFileSync(path.join(ROOT, 'native/win11/src/Pulse.App/Assets/Pulse.ico'),
  ico([16, 24, 32, 48, 64, 256].map((s) => [s, P[s]])));
fs.writeFileSync(path.join(ROOT, 'native/macos/Assets/AppIcon.icns'),
  icns([['icp4', P[16]], ['icp5', P[32]], ['icp6', P[64]], ['ic07', P[128]], ['ic08', P[256]],
        ['ic09', P[512]], ['ic10', P[1024]], ['ic11', P[32]], ['ic12', P[64]], ['ic13', P[256]], ['ic14', P[512]]]));

// GitHub social preview.
const page = await b.newPage({viewport: {width: 1280, height: 640}});
await page.setContent(`<html><body style="margin:0;width:1280px;height:640px;background:#0E1E19;color:#fff;
  font-family:'DejaVu Sans',sans-serif;display:flex;align-items:center;gap:72px;padding:0 112px;box-sizing:border-box">
  ${svg.replace('<svg ', '<svg width="340" height="340" ')}
  <div>
    <div style="font-size:120px;font-weight:700;letter-spacing:-3px;line-height:1">Pulse</div>
    <div style="font-size:38px;margin-top:22px;color:#D1FAE5">Your system's vitals, in a flash.</div>
    <div style="font-size:34px;margin-top:10px;color:#A7C4BA;text-align:left" dir="rtl">صحة جهازك بنظرة واحدة.</div>
    <div style="font-size:21px;margin-top:40px;color:#7FA397;white-space:nowrap">macOS · Windows 11 · Linux · ChromeOS · Windows XP · Windows 98</div>
  </div></body></html>`);
fs.writeFileSync(path.join(HERE, 'social-preview.png'), await page.screenshot());
await b.close();
