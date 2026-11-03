#pragma once
namespace esphome::desk_buddy {
static const char SETUP_PAGE[]=R"HTML(<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Network · Desk Buddy</title>
<style>body{font:16px system-ui;background:#0c151e;color:#e3edf4;max-width:600px;margin:40px auto;padding:20px}h1{color:#28f4da}fieldset{border:1px solid #354753;border-radius:12px;margin:20px 0;padding:20px}label{display:block;margin:14px 0}input,button,select{font:inherit;padding:12px;border:1px solid #566670;border-radius:8px;background:#182630;color:inherit;box-sizing:border-box;width:100%}button{background:#28f4da;color:#12232a;cursor:pointer}button:disabled{opacity:.6;cursor:wait}a{color:#28f4da}p{line-height:1.6}</style>
<label><span data-ui="language">Language / Jazyk</span><select id="language"><option value="en">EN — English</option><option value="cs">CZ — Čeština</option></select></label>
<h1 data-ui="heading">Desk Buddy</h1><p><a href="/" data-ui="back">← All settings and current status</a></p>
<p data-ui="intro">Fill in only the network you want to change. An empty Wi-Fi name or MQTT host leaves that section unchanged. Stored passwords are never displayed. Enter the password again for any changed section; an empty password means no password.</p>
<form id="settings"><fieldset><legend>Wi-Fi</legend><label>SSID<input name="wifi_ssid" maxlength="32" autocomplete="off"></label><label><span data-ui="password">Password</span><input name="wifi_password" type="password" maxlength="63" autocomplete="new-password"></label></fieldset>
<fieldset><legend>MQTT</legend><label>Broker<input name="mqtt_host" maxlength="96" placeholder="mqtt.local"></label><label>Port<input name="mqtt_port" type="number" min="1" max="65535" value="1883"></label><label><span data-ui="user">Username</span><input name="mqtt_user" maxlength="64" autocomplete="off"></label><label><span data-ui="password">Password</span><input name="mqtt_password" type="password" maxlength="96" autocomplete="new-password"></label></fieldset>
<button><span data-ui="save">Save and restart</span></button></form><p id="result" role="status"></p>
<script>
const messages={
  "en": {
    "title": "Network · Desk Buddy",
    "heading": "Desk Buddy",
    "back": "← All settings and current status",
    "intro": "Fill in only the network you want to change. An empty Wi-Fi name or MQTT host leaves that section unchanged. Stored passwords are never displayed. Enter the password again for any changed section; an empty password means no password.",
    "password": "Password",
    "user": "Username",
    "save": "Save and restart",
    "language": "Language / Jazyk",
    "connectionLost": "The connection was interrupted. After the restart, open the device address.",
    "saved": "Saved. The device is restarting.",
    "invalid": "Invalid host, port, SSID or password.",
    "nvs": "Could not save settings in NVS.",
    "header": "Missing setup header.",
    "method": "POST required."
  },
  "cs": {
    "title": "Síť · Stolní kamarád",
    "heading": "Stolní kamarád",
    "back": "← Všechna nastavení a aktuální stav",
    "intro": "Vyplň jen síť, kterou chceš změnit. Prázdné jméno Wi-Fi nebo MQTT host ponechá danou sekci beze změny. Hesla se ze zařízení nezobrazují. U měněné sekce zadej heslo znovu; prázdné heslo znamená síť bez hesla.",
    "password": "Heslo",
    "user": "Uživatel",
    "save": "Uložit a restartovat",
    "language": "Language / Jazyk",
    "connectionLost": "Spojení se přerušilo. Po restartu otevři adresu zařízení.",
    "saved": "Uloženo. Zařízení se restartuje.",
    "invalid": "Neplatný host, port, SSID nebo heslo.",
    "nvs": "Nastavení se nepodařilo uložit do NVS.",
    "header": "Chybí hlavička nastavení.",
    "method": "Je vyžadován POST."
  }
};
const preference='desk-buddy.setup.language';
let language=(navigator.language||'en').toLowerCase().startsWith('cs')?'cs':'en';
try {const saved=localStorage.getItem(preference);if(saved==='en'||saved==='cs') language=saved;} catch {}
let resultKey='',rawResult='';
const result=document.querySelector('#result');
function renderLanguage() {
  document.documentElement.lang=language;
  document.title=messages[language].title;
  document.querySelector('#language').value=language;
  document.querySelectorAll('[data-ui]').forEach(node=>{node.textContent=messages[language][node.dataset.ui];});
  result.textContent=resultKey?messages[language][resultKey]:rawResult;
}
document.querySelector('#language').onchange=e=>{
  language=e.target.value;
  try {localStorage.setItem(preference,language);} catch {}
  renderLanguage();
};
const responses={
  'Saved. The device is restarting.':'saved','Ulozeno. Zarizeni se restartuje.':'saved',
  'Invalid host, port, SSID or password.':'invalid','Neplatny host, port, SSID nebo heslo.':'invalid',
  'NVS save failed':'nvs','Ulozeni NVS selhalo':'nvs',
  'Missing setup header':'header','Chybi hlavicka nastaveni':'header',
  'POST required':'method','Je vyzadovan POST':'method'
};
document.querySelector('form').onsubmit=async e=>{
  e.preventDefault();const button=e.target.querySelector('button');button.disabled=true;
  try {
    const body=new URLSearchParams(new FormData(e.target));body.set('ui_language',language);
    const response=await fetch('/setup',{method:'POST',headers:{'X-Buddy-Setup':'1','Content-Type':'application/x-www-form-urlencoded'},body});
    rawResult=await response.text();resultKey=Object.hasOwn(responses,rawResult)?responses[rawResult]:'';
  } catch {resultKey='connectionLost';rawResult='';}
  finally {button.disabled=false;renderLanguage();}
};
renderLanguage();
</script></html>)HTML";
}
