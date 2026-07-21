// Drives the real web UI in a real browser: the only way to test the parts
// that live in the page (key handling, xterm rendering).
//
// Keys are pressed the way a keyboard produces them — Shift down, key, Shift
// up — because press('"') synthesizes only the quote keydown and so hides
// any bug in how the page handles the modifier's own keydown.
const { chromium } = require('playwright');

const PORT = process.env.PORT || 3697;
const sleep = ms => new Promise(r => setTimeout(r, ms));

async function screens(page) {
  return page.evaluate(() => {
    const out = [];
    for (const leaf of document.querySelectorAll('.leaf')) {
      const rows = leaf.querySelectorAll('.xterm-rows > div');
      out.push({
        pane: leaf.dataset.pane,
        focused: leaf.classList.contains('focused'),
        text: [...rows].map(r => r.textContent.replace(/\s+$/, '')).filter(Boolean).join('\n'),
      });
    }
    return out;
  });
}

const sent = page => page.evaluate(() => window.__sent.splice(0));

// Real Shift+<key>, as a keyboard emits it.
async function shifted(page, key) {
  await page.keyboard.down('Shift');
  await page.keyboard.press(key);
  await page.keyboard.up('Shift');
}

(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  page.on('pageerror', e => console.log('  [pageerror]', e.message));

  // Record every op the page sends, so we can tell "key never sent" apart
  // from "key sent but nothing rendered".
  await page.addInitScript(() => {
    window.__sent = [];
    const real = WebSocket.prototype.send;
    WebSocket.prototype.send = function (d) {
      try { window.__sent.push(JSON.parse(d)); } catch { window.__sent.push({ raw: String(d) }); }
      return real.call(this, d);
    };
  });

  await page.goto(`http://127.0.0.1:${PORT}/`, { waitUntil: 'networkidle' });
  await page.waitForSelector('.leaf .xterm-rows', { timeout: 15000 });
  await sleep(1500);
  await page.click('.leaf');
  await sleep(300);
  await sent(page);

  const results = [];
  const check = (name, ok, detail) => {
    results.push({ name, ok });
    console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? '  — ' + detail : ''}`);
  };

  // --- leader keys that need Shift ---------------------------------------
  for (const [label, key, cmd] of [
    ['Ctrl+A "  split-below', 'Quote', 'split-below'],
    ['Ctrl+A %  split-beside', 'Digit5', 'split-beside'],
    ['Ctrl+A H  resize-left', 'KeyH', 'resize-left'],
  ]) {
    const before = (await screens(page)).length;
    await page.keyboard.press('Control+a');
    await sleep(200);
    await shifted(page, key);
    await sleep(1200);
    const ops = await sent(page);
    const commanded = ops.some(o => o.op === 'command' && o.command === cmd);
    const leaked = ops.filter(o => o.op === 'input').map(o => o.data).join('');
    const after = (await screens(page)).length;
    check(label, commanded,
      commanded ? `panes ${before}->${after}`
                : `no '${cmd}' sent; leaked to shell: ${JSON.stringify(leaked)}`);
  }

  // --- focus keys must not also type themselves ---------------------------
  // A swallowed leader key still runs its default action unless the handler
  // prevents it, and by then focus has moved — so the character lands in the
  // pane we just moved to.
  for (const key of ['j', 'k', 'h', 'l', 'o']) {
    await page.keyboard.press('Control+a');
    await sleep(200);
    await sent(page);
    await page.keyboard.press(key);
    await sleep(700);
    const typed = (await sent(page)).filter(o => o.op === 'input').map(o => o.data).join('');
    check(`Ctrl+A ${key} moves focus without typing "${key}"`, typed === '',
      typed === '' ? 'nothing leaked' : `leaked ${JSON.stringify(typed)}`);
  }

  // --- an unshifted one, as a control -------------------------------------
  {
    const before = (await screens(page)).length;
    await page.keyboard.press('Control+a');
    await sleep(200);
    await page.keyboard.press('x');
    await sleep(1200);
    const ops = await sent(page);
    const ok = ops.some(o => o.op === 'command' && o.command === 'close-pane');
    check('Ctrl+A x  close-pane (unshifted control)', ok,
      `panes ${before}->${(await screens(page)).length}`);
  }

  // --- multiple Ctrl+C ----------------------------------------------------
  await page.click('.leaf');
  await sleep(400);
  await sent(page);
  const pane = (await screens(page)).find(p => p.focused)?.pane
            || (await screens(page))[0].pane;

  // Interrupt a running foreground command each time. At a bare prompt zsh
  // reprints and erases its own ^C, so counting echoes there measures the
  // shell's redraw, not whether the interrupt arrived and rendered.
  let interrupted = 0;
  for (let i = 0; i < 5; i++) {
    await page.keyboard.type('sleep 30');
    await page.keyboard.press('Enter');
    await sleep(500);
    await page.keyboard.press('Control+c');
    await sleep(700);
    const v = (await screens(page)).find(p => p.pane === pane);
    if (v && (v.text.match(/\^C/g) || []).length > interrupted) interrupted++;
  }

  const ops = await sent(page);
  const etx = ops.filter(o => o.op === 'input' && o.data === '\x03').length;
  check('5x Ctrl+C reach the shell', etx === 5, `sent ${etx}/5 as 0x03`);
  check('5x Ctrl+C each render a ^C', interrupted === 5, `${interrupted}/5 interrupts shown`);
  const view = (await screens(page)).find(p => p.pane === pane);
  if (view) console.log('--- pane tail ---\n' + view.text.split('\n').slice(-8).join('\n'));

  console.log(`\n${results.filter(r => r.ok).length}/${results.length} passed`);
  await browser.close();
  process.exit(results.every(r => r.ok) ? 0 : 1);
})().catch(e => { console.error('DRIVER ERROR', e); process.exit(2); });
