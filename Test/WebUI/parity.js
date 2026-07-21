// Parity: does the browser show what the native TermScreen holds?
// Drives keys through the web UI, then compares the page's rendered rows
// against /api/v1/panes/{id}/screen — the owning CowTerm's own model.
const { chromium } = require('playwright');
const PORT = process.env.PORT || 3697;
const sleep = ms => new Promise(r => setTimeout(r, ms));

const strip = s => s
  .replace(/\x1b\][^\x07\x1b]*(\x07|\x1b\\)/g, '')   // OSC
  .replace(/\x1b\[[0-9;?]*[ -\/]*[@-~]/g, '')        // CSI
  .replace(/\x1b[()][0-9A-B]/g, '')
  .replace(/\x1b[=>]/g, '');

const visible = text => strip(text).split('\n').map(l => l.replace(/\s+$/, '')).filter(Boolean);

(async () => {
  const b = await chromium.launch(); const page = await b.newPage();
  await page.goto(`http://127.0.0.1:${PORT}/`, { waitUntil: 'networkidle' });
  await page.waitForSelector('.leaf .xterm-rows', { timeout: 15000 });
  await sleep(2000);
  await page.click('.xterm-screen');
  await sleep(400);

  const pane = await page.evaluate(() => document.querySelector('.leaf').dataset.pane);

  const seq = [
    ['type', 'echo PARITY-ONE'], ['key', 'Enter'],
    ['type', 'sleep 30'],        ['key', 'Enter'],
    ['key', 'Control+c'], ['key', 'Control+c'], ['key', 'Control+c'],
    ['type', 'echo PARITY-TWO'], ['key', 'Enter'],
  ];
  for (const [kind, arg] of seq) {
    if (kind === 'type') await page.keyboard.type(arg); else await page.keyboard.press(arg);
    await sleep(450);
  }
  await sleep(1800);

  const browserRows = await page.evaluate(() => {
    const leaf = [...document.querySelectorAll('.leaf')].find(l => l.querySelector('.xterm-rows'));
    return [...leaf.querySelectorAll('.xterm-rows > div')].map(r => r.textContent.replace(/\s+$/, ''));
  });

  const res = await fetch(`http://127.0.0.1:${PORT}/api/v1/panes/${pane}/screen`);
  const nativeRows = visible(await res.text());
  const webRows = browserRows.filter(Boolean);

  const tail = a => a.slice(-6).join('\n');
  console.log('--- NATIVE (TermScreen) ---\n' + tail(nativeRows));
  console.log('\n--- WEB (xterm.js DOM) ---\n' + tail(webRows));

  const norm = a => a.map(l => l.replace(/\s+/g, ' ').trim()).filter(Boolean);
  const n = norm(nativeRows), w = norm(webRows);
  const common = n.filter(l => w.includes(l)).length;
  console.log(`\nnative lines ${n.length}, web lines ${w.length}, shared ${common}`);
  console.log(`PARITY-ONE  native:${n.some(l=>l.includes('PARITY-ONE'))} web:${w.some(l=>l.includes('PARITY-ONE'))}`);
  console.log(`PARITY-TWO  native:${n.some(l=>l.includes('PARITY-TWO'))} web:${w.some(l=>l.includes('PARITY-TWO'))}`);
  console.log(`^C          native:${n.some(l=>l.includes('^C'))} web:${w.some(l=>l.includes('^C'))}`);
  await b.close();
})().catch(e => { console.error(e); process.exit(1); });
