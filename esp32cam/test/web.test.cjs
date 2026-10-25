// SPDX-License-Identifier: GPL-3.0-only
// Isolated Chrome + HTTP fixture. Never connects to a COM port or user's browser profile.
const {chromium} = require(process.env.SF_PLAYWRIGHT_MODULE || 'playwright');
const {spawn} = require('node:child_process');
const fs = require('node:fs/promises');
const path = require('node:path');
const assert = require('node:assert/strict');
const ROOT = path.resolve(__dirname, '../..');
const BUILD = path.join(__dirname, '.build');
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));

async function main() {
  await fs.mkdir(BUILD, {recursive:true});
  const server = spawn(process.env.SF_PYTHON || 'python', ['esp32cam/test/mock_web.py', '--port', '0'],
    {cwd:ROOT, windowsHide:true, env:{...process.env, PYTHONUTF8:'1', PYTHONUNBUFFERED:'1'}, stdio:['ignore','pipe','pipe']});
  let browser;
  let stderr=''; server.stderr.on('data', b=>stderr+=b);
  try {
    const url = await new Promise((resolve,reject)=>{
      const timeout=setTimeout(()=>reject(Error('Mock server startup timeout: '+stderr)),10000);
      server.on('error',reject);server.stdout.on('data',b=>{const match=String(b).match(/http:\/\/127\.0\.0\.1:\d+/);if(match){clearTimeout(timeout);resolve(match[0])}});
    });
    browser = await chromium.launch(process.env.SF_WEB_CHROME ? {executablePath:process.env.SF_WEB_CHROME,headless:true} : {channel:'chrome',headless:true});
    const context=await browser.newContext({viewport:{width:390,height:844},deviceScaleFactor:1,acceptDownloads:true});
    let page=await context.newPage(); page.setDefaultTimeout(12000);
    const errors=[]; page.on('pageerror',e=>errors.push(e.message));
    context.on('requestfailed',request=>console.error('Request failed:',request.method(),request.url(),request.failure()?.errorText));
    const toast=async text=>page.locator('#toast').filter({hasText:text}).waitFor({state:'visible'});
    const api=async()=>{const response=await context.request.get(url+'/api/state');assert.equal(response.status(),200);return response.json()};
    const control=async body=>{const s=await api();const response=await context.request.post(url+'/mock/control',{data:body,headers:{'X-CSRF-Token':s.csrf}});assert.equal(response.status(),200)};
    const response=await context.request.get(url+'/api/state');assert.equal(response.status(),401);
    await page.goto(url);await page.getByRole('textbox',{name:'帳號',exact:true}).fill('admin');
    await page.getByRole('textbox',{name:'密碼',exact:true}).fill('MockPassword123');
    await Promise.all([page.waitForURL(url+'/'),page.getByRole('button',{name:'登入',exact:true}).click()]);
    await page.locator('#connection').filter({hasText:'Mega 已連線'}).waitFor();
    assert.equal(await page.locator('#startMode').isEnabled(),false);
    await page.locator('#targetText').fill('AB');assert.equal(await page.locator('#sendText').isEnabled(),false);
    await page.getByRole('combobox',{name:'U0 目前字元',exact:true}).selectOption('26');
    await page.locator('#setCurrent').click();await toast('已記錄目前字元');
    await page.locator('#armAll').click();await toast('全部已啟用');
    await page.locator('#targetText').fill('Ä');await page.locator('#sendText').click();await toast('文字更新完成');
    assert.equal(await page.locator('#currentPreview').textContent(),'Ä');
    console.log('PASS browser login, manual reference, ARM, glyph, invalid width and uninitialized clock');

    async function layout(count, map=null) {
      await page.getByRole('button',{name:'裝置設定',exact:true}).click();
      await page.locator('#digits').fill(String(count));await page.locator('#customMap').setChecked(!!map);
      if(map)await page.locator('#unitMap').fill(map);
      await page.locator('#saveLayout').click();await toast('安裝配置已保存');
      await page.getByRole('button',{name:'顯示',exact:true}).click();
      await page.locator('#displayStatus').filter({hasText:count+' 位'}).waitFor();
    }
    async function referenceAndArm() {
      for(const select of await page.locator('#manualCurrent select').all())await select.selectOption('0');
      await page.locator('#setCurrent').click();await toast('已記錄目前字元');
      await page.locator('#armAll').click();await toast('全部已啟用');
    }
    await layout(3,'7,0,2');assert.match(await page.locator('#displayStatus').innerText(),/U7 → U0 → U2/);
    assert.equal(await page.locator('#format option').count(),0);
    await referenceAndArm();await page.locator('#targetText').fill('äA');await page.locator('#sendText').click();await toast('文字更新完成');
    assert.equal(await page.locator('#currentPreview').textContent(),'ÄA ');
    await page.locator('#targetText').fill('ß');assert.equal(await page.locator('#sendText').isEnabled(),false);
    await page.locator('#targetText').fill('中文');assert.equal(await page.locator('#sendText').isEnabled(),false);
    console.log('PASS browser sparse U7/U0/U2, UTF-8, padding, unsupported glyphs and 1–3 digit clock gating');

    await layout(5);await referenceAndArm();await page.locator('#targetText').fill('HELLO');
    await page.locator('#sendText').click();await toast('文字更新完成');
    await page.evaluate(()=>scrollTo(0,0));
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
    await page.locator('#toast').waitFor({state:'hidden'});
    await page.screenshot({path:path.join(ROOT,'esp32cam/ui-preview.jpg'),type:'jpeg',quality:85});
    await page.screenshot({path:path.join(BUILD,'ui-mobile.jpg'),type:'jpeg',quality:85,fullPage:true});
    await page.locator('#phoneTime').click();await toast('手機校時完成');
    await page.locator('#format').selectOption('HH:MM');await page.locator('#startMode').click();await toast('自動顯示已啟動');
    assert.equal((await api()).display.running,true);
    await page.close();await pause(200);assert.equal((await api()).display.running,true);
    page=await context.newPage();page.setDefaultTimeout(12000);page.on('pageerror',e=>errors.push(e.message));await page.goto(url);
    await page.locator('#modeStatus').filter({hasText:'運作中'}).waitFor();await page.locator('#pauseMode').click();await toast('自動顯示已暫停');
    await page.locator('#mode').selectOption('date');assert.equal(await page.locator('#format option').count(),2);
    console.log('PASS browser 5-digit clock, phone time, page close/reopen, pause and date-format limits');

    await page.getByRole('button',{name:'裝置設定',exact:true}).click();
    await page.locator('#ssid').fill('SplitFlap-Lab');await page.locator('#wifiPassword').fill('TestNetwork123');
    await page.getByRole('button',{name:'保存網路設定',exact:true}).click();await toast('網路設定已保存');
    const downloadPromise=page.waitForEvent('download');await page.locator('#export').click();const download=await downloadPromise;
    const backup=path.join(BUILD,'settings.json');await download.saveAs(backup);await toast('已匯出設定');
    const contents=await fs.readFile(backup,'utf8');assert(!contents.includes('Password'));assert(!contents.includes('csrf'));
    const invalid=JSON.parse(contents);invalid.mega.units=[0,0];const invalidPath=path.join(BUILD,'invalid-settings.json');
    await fs.writeFile(invalidPath,JSON.stringify(invalid));await page.locator('#importFile').setInputFiles(invalidPath);
    await page.locator('#import').click();await toast('整份設定驗證失敗');assert.deepEqual((await api()).mega.units,[0,1,2,3,4]);
    await page.locator('#importFile').setInputFiles(backup);await page.locator('#import').click();await toast('設定已匯入');
    await control({connected:false});await page.locator('#connection').filter({hasText:'尚未連線'}).waitFor();assert.equal(await page.locator('#stop').isEnabled(),false);
    await control({connected:true});await page.locator('#connection').filter({hasText:'已連線'}).waitFor();
    await control({reboot:true});await toast('設備已重新啟動');assert.equal((await api()).units.some(u=>u.armed),false);
    console.log('PASS browser Wi-Fi form, export/import, invalid atomic config, link reconnect and reboot without ARM');

    await page.getByRole('button',{name:'韌體更新',exact:true}).click();
    const good=path.join(ROOT,'esp32cam/dist/splitflap-esp32cam.sffw');const bytes=await fs.readFile(good);
    const corrupt=Buffer.from(bytes);corrupt[100]^=1;const corruptPath=path.join(BUILD,'corrupt.sffw');await fs.writeFile(corruptPath,corrupt);
    const partialPath=path.join(BUILD,'partial.sffw');await fs.writeFile(partialPath,bytes.subarray(0,bytes.length-17));
    async function prepare(file) {
      await page.locator('#prepareUpdate').click();await toast('Mega 已確認停止');
      await page.locator('#firmwareFile').setInputFiles(file);
      await page.waitForFunction(()=>!document.getElementById('uploadUpdate').disabled);
    }
    const boot=(await api()).boot;
    await prepare(partialPath);await page.locator('#uploadUpdate').click();await toast('映像長度或協定版本不符');
    assert.match(await page.locator('#updateStatus').textContent(),/重新按/);
    assert.equal(await page.locator('#uploadUpdate').isEnabled(),false);
    assert.equal((await api()).boot,boot);
    await prepare(corruptPath);await page.locator('#uploadUpdate').click();await toast('SHA-256 不符');
    assert.equal((await api()).boot,boot);
    await page.screenshot({path:path.join(BUILD,'ui-ota-rejected.jpg'),type:'jpeg',quality:85});
    await prepare(good);await page.locator('#uploadUpdate').click();await toast('設備已重新啟動');
    assert.equal((await api()).units.some(u=>u.armed),false);
    await page.getByRole('button',{name:'顯示',exact:true}).click();
    await page.setViewportSize({width:1100,height:900});await page.evaluate(()=>scrollTo(0,0));
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
    await page.screenshot({path:path.join(BUILD,'ui-desktop.jpg'),type:'jpeg',quality:85});
    assert.deepEqual(errors,[]);
    console.log('PASS browser OTA prepare/STOP, truncated/wrong-SHA rejection, successful upload/reboot; mobile/desktop layout; no JS errors');
    await fs.writeFile(path.join(BUILD,'browser-results.json'),JSON.stringify({passed:true,viewport:[390,844],groups:5,errors},null,2));
  } catch(error) {
    if(stderr)console.error('Mock server diagnostics:',stderr.slice(-6000));
    const current=browser?.contexts()[0]?.pages().find(p=>!p.isClosed());
    if(current){
      console.error('Browser state:',await current.evaluate(()=>({toast:document.getElementById('toast')?.textContent,error:document.getElementById('globalError')?.textContent,digits:document.getElementById('digits')?.value,custom:document.getElementById('customMap')?.checked,waiting,state:S})).catch(()=>null));
      await current.screenshot({path:path.join(BUILD,'ui-failure.jpg'),type:'jpeg',quality:85,fullPage:true}).catch(()=>{});
    }
    throw error;
  } finally {
    if(browser)await browser.close();
    server.kill();
  }
}
main().catch(e=>{console.error(e);process.exitCode=1});
