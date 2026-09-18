const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const assert = require('node:assert/strict');

(async () => {
  const origin = process.env.STORE_URL || 'http://127.0.0.1:8086';
  const browser = await chromium.launch({headless:true,
    ...(process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE ? {executablePath:process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE} : {})});
  try {
  const page = await browser.newPage({viewport:{width:1440,height:1000}});
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.goto(origin);
  assert.match(await page.locator('h1').textContent(),/Your game/);
  assert.equal(await page.getByRole('link',{name:'Get the engine',exact:false}).getAttribute('href'),
    'https://github.com/JEAPI-DEV/DemiEngine/tree/main');
  assert.equal(await page.getByRole('link',{name:'Example instructions',exact:false}).count(),1);
  assert.equal(await page.locator('.home-preview img').getAttribute('src'),'/images/destruction-lab.png');
  assert.match(await page.locator('.engine-status').textContent(),/What comes next/);
  assert.ok(await page.locator('a[href$="/docs/lua-modules.md"]').count());
  for (const width of [1440, 768, 390]) {
    await page.setViewportSize({width,height:1000});
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth > innerWidth),false);
    await page.locator('.asset-spotlight').scrollIntoViewIfNeeded();
    await page.waitForFunction(()=>Array.from(document.querySelectorAll('.home-preview img,.asset-spotlight img')).every(img=>img.complete && img.naturalWidth>0));
    if (process.env.SCREENSHOT_DIR) {
      await page.evaluate(()=>window.scrollTo({top:0,behavior:'instant'}));
      await page.screenshot({path:`${process.env.SCREENSHOT_DIR}/home-${width}.png`,fullPage:true});
    }
  }
  await page.setViewportSize({width:1440,height:1000});
  await page.goto(origin+'/packages/');
  const catalog = await (await page.request.get(origin+'/v1/catalog')).json();
  assert.equal(await page.locator('.card').count(),Math.min(12,catalog.packages.length));
  await page.getByRole('searchbox').fill('health');
  await page.getByRole('button',{name:'Search',exact:false}).click();
  assert.equal(await page.locator('.card').count(),1);
  await page.locator('.card').click();
  assert.match(await page.locator('h1').textContent(),/Health/);
  assert.equal((await page.locator('#install-command').textContent()).trim(),'demi package add demi.gameplay.health@1.0.0');
  assert.equal(await page.locator('.command-block .copy-icon').count(),1);
  await page.context().grantPermissions(['clipboard-read','clipboard-write']);
  await page.getByRole('button',{name:'Copy install command',exact:true}).click();
  await page.waitForFunction(()=>document.getElementById('copy-status').textContent === 'Copied.');
  assert.equal(await page.evaluate(()=>navigator.clipboard.readText()),'demi package add demi.gameplay.health@1.0.0');
  await page.getByRole('link',{name:'Package Content',exact:true}).click();
  assert.ok(await page.locator('.content-viewer .file-name').count()>0);
  assert.equal(await page.locator('.content-viewer a').count(),0);
  assert.equal(await page.getByRole('link',{name:'Package Content',exact:true}).getAttribute('aria-current'),'page');
  await page.getByRole('link',{name:'Releases',exact:true}).click();
  assert.ok(await page.locator('.release-row').count()>0);
  await page.getByRole('link',{name:'Publisher info',exact:true}).click();
  assert.equal(await page.locator('.tab-content h2').textContent(),'DemiEngine');
  await page.locator('.install-panel .license-link').click();
  assert.equal(await page.locator('h1').textContent(),'BSD-3-Clause');
  assert.match(await page.locator('.license-text').textContent(),/Redistribution and use/);
  await page.goto(origin+'/packages/demi.gameplay.health');
  await page.locator('.tag-links a').filter({hasText:'combat'}).click();
  assert.ok(await page.locator('.card').count()>1);
  assert.match(page.url(),/tag=combat/);
  await page.goto(origin+'/packages/demi.gameplay.health');
  const download = await Promise.all([
    page.waitForEvent('download'),
    page.getByRole('link',{name:'Download package',exact:false}).click()
  ]);
  assert.match(download[0].suggestedFilename(),/demi.gameplay.health-1.0.0.demipkg/);
  await page.goto(origin+'/packages/?q=notarealpackage');
  assert.equal(await page.locator('.empty').count(),1);
  await page.goto(origin+'/packages/?category=UI');
  assert.equal(await page.locator('.card').count(),1);
  await page.setViewportSize({width:390,height:844});
  await page.goto(origin+'/packages/');
  assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth > innerWidth),false);
  await page.goto(origin+'/packages/demi.gameplay.third_person');
  assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth > innerWidth),false);
  await page.getByRole('link',{name:'Package Content',exact:true}).click();
  assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth > innerWidth),false);
  assert.deepEqual(errors,[]);
  if (catalog.packages.some(p=>p.manifest.name==='kenney.textures.prototype')) {
    await page.goto(origin+'/packages/kenney.textures.prototype');
    assert.equal(await page.locator('.thumbnails button').count(),2);
    assert.doesNotMatch(await page.locator('#gallery-image').getAttribute('src'),/demi-asset/);
    await page.waitForFunction(()=>document.getElementById('gallery-image').naturalWidth>0);
    const firstImage = await page.locator('#gallery-image').getAttribute('src');
    await page.locator('.thumbnails button').nth(1).click();
    assert.notEqual(await page.locator('#gallery-image').getAttribute('src'),firstImage);
    assert.match(await page.locator('.install-panel .license-link').textContent(),/^CC0-1\.0/);
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth > innerWidth),false);
    if (process.env.SCREENSHOT_DIR) await page.screenshot({path:`${process.env.SCREENSHOT_DIR}/kenney-mobile.png`,fullPage:true});
  }
  console.log('Browser homepage, catalog, tags, copy icon, sections, licenses, download and mobile checks passed.');
  } finally {
    await browser.close();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
