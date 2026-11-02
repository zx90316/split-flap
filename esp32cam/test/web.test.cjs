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

    const response=await context.request.get(url+'/api/state');assert.equal(response.status(),401);assert.equal(response.headers()['www-authenticate'],undefined);
    await page.goto(url);await page.locator('#loginPanel').waitFor({state:'visible'});
    await page.locator('#password').fill('wrong-password');await page.locator('#loginButton').click();
    await page.locator('#loginError').filter({hasText:'不正確'}).waitFor();
    await page.locator('#password').fill('MockPassword123');await page.locator('#loginButton').click();
    await page.locator('#connection').filter({hasText:'Mega 已連線'}).waitFor();
    assert.equal(await page.locator('#armAll,[data-op="ARM"],#customMap,#unitMap').count(),0);
    const second=await browser.newContext();const other=await second.newPage();await other.goto(url);
    await other.locator('#password').fill('MockPassword123');await other.locator('#loginButton').click();
    await other.locator('#controlPanel').waitFor({state:'visible'});
    for(let i=0;i<3;i++){assert.equal((await browser.newContext().then(async c=>{const r=await c.request.get(url+'/api/state');await c.close();return r})).status(),401);assert.equal((await context.request.get(url+'/api/state')).status(),200)}
    await page.reload();await page.locator('#controlPanel').waitFor({state:'visible'});
    await other.locator('#logout').click();await other.locator('#loginPanel').waitFor({state:'visible'});assert.equal((await context.request.get(url+'/api/state')).status(),200);await second.close();
    console.log('PASS inline login, wrong-password message, independent browser sessions, logout and reload persistence; no WWW-Authenticate');
    await control({connected:false});await page.locator('#connection').filter({hasText:'Mega 尚未連線'}).waitFor();
    await page.locator('[data-tab="settings"]').click();await page.locator('#ssid').fill('First-Setup-WiFi');
    assert.equal(await page.locator('#saveNetwork').isEnabled(),true);assert.equal(await page.locator('#saveLayout').isEnabled(),false);
    await page.locator('#saveNetwork').click();await toast('網路設定已保存');assert.equal((await api()).network.ssid,'First-Setup-WiFi');
    await control({connected:true});await page.locator('#connection').filter({hasText:'Mega 已連線'}).waitFor();
    await page.locator('#ssid').fill('Unsaved Draft');await pause(1200);assert.equal(await page.locator('#ssid').inputValue(),'Unsaved Draft');
    console.log('PASS Wi-Fi setup without Mega and form drafts survive background refresh');
    await page.locator('[data-tab="display"]').click();await page.locator('summary').filter({hasText:'手動記錄目前字元'}).click();
    await page.locator('#manualCurrent select').selectOption('26');await page.locator('#setCurrent').click();await toast('已記錄目前字元');
    await page.locator('#targetText').fill('A');await page.locator('#sendText').click();await toast('文字更新完成');
    assert.equal((await api()).units[0].index,1);assert.equal((await api()).units[0].armed,true);
    console.log('PASS direct text display automatically prepares the configured unit without ARM UI');
    await page.locator('[data-tab="settings"]').click();await page.locator('#calStart').click();await toast('歸零／微調完成');
    assert.equal((await api()).units[0].rememberedIndex,1);assert.equal((await api()).workflow.calibrating,true);
    await page.locator('[data-jog="10"]').click();await toast('歸零／微調完成');
    await page.waitForFunction(()=>document.getElementById('draftOffset').value==='10');
    await page.locator('#draftOffset').fill('273');await page.locator('#offsetPreview').click();
    await page.waitForFunction(()=>!waiting&&S.mega.calibration[0].offset===273);
    await page.locator('#calFinish').click();await toast('校準完成，偏移已永久保存');
    let s=await api();assert.equal(s.display.autoRecovery,true);assert.equal(s.mega.calibration[0].offset,273);assert.equal(s.units[0].rememberedIndex,1);
    await control({reboot:true,powerOn:true});await page.waitForFunction(()=>!waiting&&S.units[0].index===1);
    assert.equal(await page.locator('#loginPanel').isVisible(),false);
    await page.locator('#calStart').click();await page.waitForFunction(()=>!waiting&&S.workflow.calibrating);
    await page.locator('[data-jog="100"]').click();await page.waitForFunction(()=>!waiting&&S.mega.calibration[0].offset===373);
    await page.locator('#calCancel').click();await toast('已取消並恢復原參數');assert.equal((await api()).mega.calibration[0].offset,273);
    console.log('PASS full homing/offset wizard, numeric/jog preview, durable confirmation, cancellation, power recovery and unchanged display history');
    await page.locator('#digits').fill('5');await page.locator('#saveLayout').click();await toast('位數已保存');
    assert.deepEqual((await api()).mega.units,[0,1,2,3,4]);assert.equal(await page.locator('#benchUnit option').count(),5);assert.equal(await page.locator('#unitRows tr').count(),5);assert.equal(await page.locator('#calUnit').inputValue(),'0');assert.equal(await page.locator('#offsetUnit').inputValue(),'0');
    await page.locator('#calStart').click();await page.waitForFunction(()=>!waiting&&S.workflow.calibrating);await page.locator('#calFinish').click();await page.waitForFunction(()=>!waiting&&!S.workflow.calibrating);
    await page.locator('[data-tab="clock"]').click();await page.locator('#phoneTime').click();await toast('手機校時完成');
    await page.locator('#mode').selectOption('clock');await page.locator('#format').selectOption('HH:MM');
    assert.equal(await page.locator('#format option[value="HH:MM:SS"]').count(),0);
    await page.locator('#mode').selectOption('carousel');await page.locator('#addItem').click();
    const rows=page.locator('.playlist-row');assert.equal(await rows.count(),2);
    await rows.nth(1).locator('input:not([type="number"])').fill('HELLO');await rows.nth(1).locator('input[type="number"]').fill('20');
    await rows.nth(1).getByRole('button',{name:'上移',exact:true}).click();
    await page.locator('#limited').check();await page.locator('#windowStart').fill('22:00');await page.locator('#windowEnd').fill('08:00');
    await page.locator('#startMode').click();await toast('自動顯示已開始');s=await api();assert.equal(s.display.mode,'carousel');assert.equal(s.display.items[0].text,'HELLO');assert.equal(s.display.windowStart,1320);assert.equal(s.display.windowEnd,480);
    await page.reload();await page.locator('[data-tab="clock"]').click();assert.equal(await page.locator('#mode').inputValue(),'carousel');assert.equal(await page.locator('.playlist-row').count(),2);
    await page.locator('#pauseMode').click();await toast('已暫停');assert.equal((await api()).display.running,false);
    await page.locator('[data-tab="settings"]').click();await page.locator('#digits').fill('10');await page.locator('#saveLayout').click();await toast('位數已保存');
    await page.locator('[data-tab="clock"]').click();await page.locator('#mode').selectOption('clock');await page.locator('#format').selectOption('hh:mm:ssAP');
    assert.equal(await page.locator('#clockPreview .flap').count(),10);
    console.log('PASS 1/5/10 contiguous unit UI, format width filtering, seconds/12-hour previews, editable carousel order/dwell, daily window and reload');
    await page.locator('[data-tab="settings"]').click();const downloadPromise=page.waitForEvent('download');await page.locator('#export').click();const download=await downloadPromise;const saved=path.join(BUILD,'settings-v2.json');await download.saveAs(saved);const exported=JSON.parse(await fs.readFile(saved,'utf8'));assert.equal(exported.schema,2);assert.equal(exported.mega.digits,10);assert.equal(exported.mega.units,undefined);assert.equal(JSON.stringify(exported).includes('MockPassword123'),false);
    const bad=structuredClone(exported);bad.schema=1;bad.mega.units=[7,0,2];delete bad.mega.digits;
    await page.locator('#importFile').setInputFiles({name:'bad.json',mimeType:'application/json',buffer:Buffer.from(JSON.stringify(bad))});await page.locator('#import').click();await toast('設定驗證失敗');assert.equal((await api()).digits,10);
    await page.locator('#importFile').setInputFiles(saved);await page.locator('#import').click();await toast('設定已匯入');
    console.log('PASS schema2 export/import, secret omission and rejection of legacy sparse layouts');
    await page.locator('[data-tab="display"]').click();await page.locator('#targetText').fill('DRAFT');let rejected=0;
    await page.route('**/api/state',route=>{rejected++;return route.fulfill({status:401,contentType:'application/json',body:'{"error":"login_required"}'})});
    await page.evaluate(()=>refresh().catch(offline));await page.locator('#loginPanel').waitFor({state:'visible'});const count=rejected;await pause(2300);await page.evaluate(()=>document.dispatchEvent(new Event('visibilitychange')));await pause(200);assert.equal(rejected,count);
    await page.unroute('**/api/state');await page.locator('#password').fill('MockPassword123');await page.locator('#loginButton').click();await page.locator('#controlPanel').waitFor({state:'visible'});assert.equal(await page.locator('#targetText').inputValue(),'DRAFT');
    console.log('PASS expired session stops polling/visibility retries and inline re-login preserves unsent text');
    await page.locator('[data-tab="update"]').click();await page.locator('#prepareUpdate').click();await toast('已準備更新');
    const packageFile=path.join(ROOT,'esp32cam/dist/splitflap-esp32s.sffw'),pkg=await fs.readFile(packageFile);
    await page.locator('#firmwareFile').setInputFiles({name:'broken.sffw',mimeType:'application/octet-stream',buffer:pkg.subarray(0,500)});await page.locator('#uploadUpdate').click();await page.locator('#updateStatus').filter({hasText:'更新未完成'}).waitFor();assert.equal((await api()).otaReady,false);
    await page.locator('#prepareUpdate').click();await toast('已準備更新');await page.locator('#firmwareFile').setInputFiles(packageFile);await page.locator('#uploadUpdate').click();await page.locator('#updateStatus').filter({hasText:'更新完成'}).waitFor();
    console.log('PASS OTA STOP/ticket, truncated rejection, retry and successful mocked reboot');
    await page.evaluate(()=>{document.getElementById('toast').style.display='none'});await page.locator('[data-tab="settings"]').click();await page.screenshot({path:path.join(BUILD,'v130-mobile-settings.png'),fullPage:true});
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
    await page.setViewportSize({width:1100,height:900});await page.locator('[data-tab="clock"]').click();await page.screenshot({path:path.join(BUILD,'v130-desktop-clock.png'),fullPage:true});
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);assert.deepEqual(errors,[]);
    await fs.writeFile(path.join(BUILD,'browser-results.json'),JSON.stringify({passed:true,version:'1.3.0',groups:9,errors},null,2));
  } catch(error) {
    if(stderr)console.error('Mock server diagnostics:',stderr.slice(-6000));
    const current=browser?.contexts()[0]?.pages().find(p=>!p.isClosed());
    if(current){
      console.error('Browser state:',await current.evaluate(()=>({toast:document.getElementById('toast')?.textContent,error:document.getElementById('globalError')?.textContent,digits:document.getElementById('digits')?.value,waiting,state:S})).catch(()=>null));
      await current.screenshot({path:path.join(BUILD,'ui-failure.jpg'),type:'jpeg',quality:85,fullPage:true}).catch(()=>{});
    }
    throw error;
  } finally {
    if(browser)await browser.close();
    server.kill();
  }
}
main().catch(e=>{console.error(e);process.exitCode=1});
