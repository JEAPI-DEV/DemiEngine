const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const assert = require('node:assert/strict');

(async () => {
  const browser = await chromium.launch({
    headless: true,
    ...(process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE } : {}),
  });
  try {
    const page = await browser.newPage();
    const errors = [];
    page.on('pageerror', error => errors.push(error.message));
    const origin = process.env.STORE_URL || 'http://127.0.0.1:8086';
    const response = await page.request.get(`${origin}/v1/catalog`);
    assert.equal(response.status(), 200);
    const { packages } = await response.json();
    let readmes = 0;
    for (const item of packages) {
      const url = `${origin}/packages/${encodeURIComponent(item.manifest.name)}`;
      assert.equal((await page.goto(url)).status(), 200);
      for (const [dependency, constraint] of Object.entries(item.manifest.dependencies)) {
        const link = page.locator('.tab-content p a').filter({ hasText: dependency });
        assert.equal(await link.count(), 1, `${item.manifest.name}: missing dependency ${dependency}`);
        assert.ok((await link.locator('..').innerText()).includes(constraint));
      }
      if (await page.locator('.package-readme').count()) {
        readmes++;
        assert.ok((await page.locator('.package-readme').innerText()).trim().length);
        assert.equal(await page.locator('.package-readme script, .package-readme iframe').count(), 0);
      } else {
        assert.ok((await page.locator('.tab-content').innerText()).includes(item.description));
      }
      for (const width of [1440, 768, 390]) {
        await page.setViewportSize({ width, height: 900 });
        await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
        assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false, `${item.manifest.name} overflows at ${width}`);
      }
      await page.goto(`${url}?tab=content`);
      assert.equal(await page.locator('.package-readme').count(), 0);
      assert.ok(await page.locator('.content-viewer').count());
    }
    assert.ok(readmes > 0, 'Expected published packages with bundled READMEs');
    assert.deepEqual(errors, []);
    console.log(`Checked ${packages.length} package pages; ${readmes} README previews; desktop/tablet/mobile layouts passed.`);
    if (process.env.SCREENSHOT_DIR) {
      await page.goto(`${origin}/packages/demi.gameplay.health`);
      for (const width of [1440, 390]) {
        await page.setViewportSize({ width, height: 1000 });
        await page.locator('.package-readme').scrollIntoViewIfNeeded();
        await page.screenshot({ path: `${process.env.SCREENSHOT_DIR}/readme-${width}.png`, fullPage: true });
      }
    }
  } finally {
    await browser.close();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
