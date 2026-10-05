// Dev harness entry. Same boot as pulse_main.ts, plus query knobs that open
// a view straight away so screenshots are reproducible.
import './pulse_app.js';
import { startShelf } from './shelf_tray.js';
async function main() {
    const q = new URLSearchParams(location.search);
    try {
        // ?reset=1 forgets saved choices; ?pref=ar seeds the language pref.
        if (q.get('reset')) {
            localStorage.clear();
        }
        if (q.get('pref') !== null) {
            localStorage.setItem('pulse.pref.language', q.get('pref'));
        }
        // The page keeps the theme the user picked under pulse.theme.
        if (q.get('theme')) {
            localStorage.setItem('pulse.theme', JSON.stringify(q.get('theme')));
        }
    }
    catch {
        // Ignore: a private window may block storage.
    }
    const file = q.get('catalog') ? `pulse_catalog_${q.get('catalog')}.json` :
        'pulse_catalog.json';
    const catalog = await (await fetch(file)).json();
    const app = document.querySelector('pulse-app');
    await app.start(catalog, document.documentElement.lang);
    await new Promise(r => requestAnimationFrame(() => r(null)));
    await startShelf(catalog, document.getElementById('window'));
    const root = app.shadowRoot;
    const tick = () => new Promise(r => setTimeout(r, 100));
    // ?sim=hog,charging turns on the Settings demo switches.
    const sims = (q.get('sim') || '').split(',').filter(Boolean);
    if (sims.length) {
        root.querySelector('[data-view="settings"]').click();
        await tick();
        for (const key of sims) {
            root.querySelector(`[aria-labelledby="sw-${key}"]`)?.click();
            await tick();
        }
        root.querySelector('.back').click();
        await tick();
    }
    // ?end=1 ends the first group that can be ended (the Linux VM) through
    // the page, so Restore ended apps has a count.
    if (q.get('end')) {
        root.querySelector('.end-btn')?.click();
        await tick();
        root.querySelector('.btn.danger')?.click();
        await new Promise(r => setTimeout(r, 3000)); // Let the toast clear.
    }
    // ?endfail=1 tries to end the Linux VM while ?fail=1 makes it fail.
    if (q.get('endfail')) {
        root.querySelector('.end-btn')?.click();
        await tick();
        root.querySelector('.btn.danger')?.click();
        await tick();
    }
    const view = q.get('view');
    if (view) {
        const card = app.shadowRoot.querySelector(`[data-view="${view}"]`);
        card?.click();
        await new Promise(r => setTimeout(r, 100));
    }
    if (q.get('menu')) {
        app.shadowRoot.querySelector('.lang-trigger')?.click();
        await new Promise(r => setTimeout(r, 100));
    }
    document.body.dataset['ready'] = '1';
}
main();
