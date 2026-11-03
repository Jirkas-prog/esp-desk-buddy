const { test, expect } = require('@playwright/test');
const fs = require('node:fs');
const path = require('node:path');
const header = fs.readFileSync(path.join(__dirname, '../components/desk_buddy/setup_page.h'), 'utf8');
const html = header.split('R"HTML(')[1].split(')HTML"')[0];

async function open(page, onPost = route => route.fulfill({ status: 200, body: 'Saved. The device is restarting.' })) {
  await page.route('http://desk-buddy.test/setup', route => route.request().method() === 'POST'
    ? onPost(route) : route.fulfill({ contentType: 'text/html', body: html }));
  await page.goto('http://desk-buddy.test/setup');
}

test('browser locale, complete dictionaries and persistent override', async ({ browser }) => {
  const context = await browser.newContext({ locale: 'cs-CZ' });
  const page = await context.newPage();
  await open(page);
  await expect(page.locator('html')).toHaveAttribute('lang', 'cs');
  await expect(page.getByRole('button')).toHaveText('Uložit a restartovat');
  expect(await page.evaluate(() => Object.keys(messages.cs).sort())).toEqual(await page.evaluate(() => Object.keys(messages.en).sort()));
  await page.selectOption('#language', 'en');
  await page.reload();
  await expect(page.locator('html')).toHaveAttribute('lang', 'en');
  await expect(page).toHaveTitle('Network · Desk Buddy');
  await expect(page.getByRole('button')).toHaveText('Save and restart');
  await context.close();
});

test('switching language keeps drafts and never submits settings', async ({ page }) => {
  let posts = 0;
  await open(page, route => { posts++; return route.fulfill({ body: 'Saved. The device is restarting.' }); });
  const values = { wifi_ssid: 'Password', wifi_password: 'test-only', mqtt_host: 'mqtt.example.com', mqtt_user: 'Save and restart', mqtt_password: 'test-only-2', mqtt_port: '2883' };
  for (const [name, value] of Object.entries(values)) await page.locator(`[name="${name}"]`).fill(value);
  for (const language of ['cs', 'en', 'cs']) await page.selectOption('#language', language);
  for (const [name, value] of Object.entries(values)) await expect(page.locator(`[name="${name}"]`)).toHaveValue(value);
  expect(posts).toBe(0);
  await expect(page.getByRole('button')).toBeEnabled();
});

test('in-flight save stays disabled; result follows current language', async ({ page }) => {
  let request, pending, count = 0;
  await open(page, route => { request = route.request(); pending = route; count++; });
  await page.locator('[name="mqtt_host"]').fill('mqtt.example.com');
  await page.getByRole('button').click();
  await expect.poll(() => count).toBe(1);
  expect(request.headers()['x-buddy-setup']).toBe('1');
  const body = new URLSearchParams(request.postData());
  expect(body.get('mqtt_host')).toBe('mqtt.example.com');
  expect(body.get('ui_language')).toBe('en');
  await page.selectOption('#language', 'cs');
  await expect(page.getByRole('button')).toBeDisabled();
  expect(count).toBe(1);
  await pending.fulfill({ status: 200, body: 'Saved. The device is restarting.' });
  await expect(page.getByRole('status')).toHaveText('Uloženo. Zařízení se restartuje.');
  await expect(page.getByRole('button')).toBeEnabled();
});

test('validation and storage errors remain localized after switching', async ({ page }) => {
  let number = 0;
  await open(page, route => route.fulfill(++number === 1
    ? { status: 400, body: 'Neplatny host, port, SSID nebo heslo.' }
    : { status: 500, body: 'NVS save failed' }));
  await page.selectOption('#language', 'cs');
  await page.getByRole('button').click();
  await expect(page.getByRole('status')).toHaveText('Neplatný host, port, SSID nebo heslo.');
  await page.selectOption('#language', 'en');
  await expect(page.getByRole('status')).toHaveText('Invalid host, port, SSID or password.');
  await page.getByRole('button').click();
  await expect(page.getByRole('status')).toHaveText('Could not save settings in NVS.');
  await page.selectOption('#language', 'cs');
  await expect(page.getByRole('status')).toHaveText('Nastavení se nepodařilo uložit do NVS.');
});

test('network failure and blocked storage do not break language controls', async ({ page }) => {
  await page.addInitScript(() => {
    for (const method of ['getItem', 'setItem']) Object.defineProperty(Storage.prototype, method, { value() { throw new DOMException('Blocked', 'SecurityError'); } });
  });
  await open(page, route => route.abort());
  await page.selectOption('#language', 'cs');
  await page.getByRole('button').click();
  await expect(page.getByRole('status')).toHaveText('Spojení se přerušilo. Po restartu otevři adresu zařízení.');
  await expect(page.getByRole('button')).toBeEnabled();
  await page.selectOption('#language', 'en');
  await expect(page.getByRole('status')).toHaveText('The connection was interrupted. After the restart, open the device address.');
});

test('invalid preference falls back and both languages fit a narrow screen', async ({ page }) => {
  await page.setViewportSize({ width: 360, height: 800 });
  await page.addInitScript(() => localStorage.setItem('desk-buddy.setup.language', 'unsupported'));
  await open(page);
  await expect(page.locator('html')).toHaveAttribute('lang', 'en');
  for (const language of ['cs', 'en']) {
    await page.selectOption('#language', language);
    expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
    await expect(page.getByLabel('Language / Jazyk')).toBeVisible();
  }
});
