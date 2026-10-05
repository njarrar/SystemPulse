// Renders the dev harness in Chromium and saves PNGs of the app window.
//   node dev/shoot.mjs <www dir> <out dir>
// Fails if the page logs an error or an uncaught exception.
import {createRequire} from 'node:module';
import {createServer} from 'node:http';
import {readFile} from 'node:fs/promises';
import {extname, join, normalize} from 'node:path';

const require = createRequire(import.meta.url);
const {chromium} = require(process.env.PLAYWRIGHT || '/opt/node22/lib/node_modules/playwright');
const [www, out] = process.argv.slice(2);
const TYPES = {'.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css',
               '.json': 'application/json', '.woff2': 'font/woff2'};

const server = createServer(async (req, res) => {
  const path = normalize(decodeURIComponent(new URL(req.url, 'http://x').pathname));
  try {
    const body = await readFile(join(www, path === '/' ? 'index.html' : path));
    res.writeHead(200, {'Content-Type': TYPES[extname(path)] || 'application/octet-stream'});
    res.end(body);
  } catch {
    res.writeHead(404);
    res.end();
  }
});
await new Promise(r => server.listen(0, '127.0.0.1', r));
const base = `http://127.0.0.1:${server.address().port}/index.html`;

const SHOTS = [
  {name: 'pulse_chromeos_en', q: 'lang=en&live=0'},
  {name: 'pulse_chromeos_ar', q: 'lang=ar&live=0'},
  {name: 'pulse_chromeos_en_detail_memory', q: 'lang=en&live=0&view=mem', hover: 0.62},
  {name: 'pulse_chromeos_ar_detail_memory', q: 'lang=ar&live=0&view=mem', hover: 0.62},
  {name: 'pulse_chromeos_en_dark', q: 'lang=en&live=0&theme=dark', hoverRow: 0},
  {name: 'pulse_chromeos_ar_detail_network', q: 'lang=ar&live=0&view=net'},
  {name: 'pulse_chromeos_en-XA_menu', q: 'lang=en-XA&catalog=xa&live=0&menu=1'},
  {name: 'pulse_chromeos_en-XA_detail_cpu', q: 'lang=en-XA&catalog=xa&live=0&view=cpu'},
  {name: 'pulse_chromeos_en_settings', q: 'lang=en&live=0&hog=0&sim=hog&end=1&view=settings'},
  {name: 'pulse_chromeos_ar_settings', q: 'lang=ar&live=0&hog=0&sim=hog&end=1&view=settings'},
  // System language is English, the saved pref is Arabic: the app opens in
  // Arabic and Settings > Language shows the pick.
  {name: 'pulse_chromeos_ar_settings_language', q: 'lang=en&pref=ar&live=0&view=settings', focus: '.lang-select'},
  {name: 'pulse_chromeos_en_settings_language', q: 'lang=ar&pref=en&live=0&view=settings', focus: '.lang-select'},
  {name: 'pulse_chromeos_en_chromebox', q: 'lang=en&live=0&hog=0&battery=0&pressure=high'},
  {name: 'pulse_chromeos_en_end_failed', q: 'lang=en&live=0&fail=1&endfail=1'},
  {name: 'pulse_chromeos_en_sim_hog_charging', q: 'lang=en&live=0&hog=0&sim=hog,charging'},
  {name: 'pulse_chromeos_en_shelf', q: 'lang=en&live=0', full: true},
  {name: 'pulse_chromeos_ar_shelf', q: 'lang=ar&live=0', full: true},
];

const browser = await chromium.launch();
const errors = [];
for (const s of SHOTS.filter(x => !process.env.ONLY || x.name.includes(process.env.ONLY))) {
  const page = await browser.newPage({viewport: {width: 520, height: s.full ? 1040 : 1000}, deviceScaleFactor: 2});
  page.on('console', m => { if (m.type() === 'error') errors.push(`${s.name}: ${m.text()}`); });
  page.on('pageerror', e => errors.push(`${s.name}: ${e.message}`));
  await page.goto(`${base}?${s.q}`);
  await page.waitForSelector('body[data-ready="1"]');
  await page.evaluate(() => document.fonts.ready);
  await page.waitForTimeout(600);
  if (s.hover != null) {
    const box = await page.locator('pulse-app .chart').boundingBox();
    await page.mouse.move(box.x + box.width * s.hover, box.y + box.height / 2);
    await page.waitForTimeout(200);
  }
  if (s.focus) {
    await page.keyboard.press('Tab');
    await page.locator('pulse-app ' + s.focus).focus();
    await page.waitForTimeout(150);
  }
  if (s.hoverRow != null) {
    await page.locator('pulse-app .row').nth(s.hoverRow).hover();
    await page.waitForTimeout(250);
  }
  if (s.full) {
    await page.screenshot({path: join(out, s.name + '.png')});
  } else {
    await page.locator('#window').screenshot({path: join(out, s.name + '.png')});
  }
  console.log('wrote', join(out, s.name + '.png'));
  await page.close();
}
await browser.close();
server.close();
if (errors.length) {
  console.error(errors.join('\n'));
  process.exit(1);
}
