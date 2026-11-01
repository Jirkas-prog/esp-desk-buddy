# ESP Desk Buddy

Stolní kamarád pro Waveshare ESP32-S3-Touch-AMOLED-1.75/1.75-B: animované oči, dotyk, hodiny, BLE přítomnost a Home Assistant Assist.

## Sestavení a nastavení

ESPHome 2026.10.0: pip install -r requirements.txt. Zkopírujte device.example.yaml na device.yaml a secrets.example.yaml na secrets.yaml; vyplňte vlastní údaje a náhodný API klíč. Potom esphome config device.yaml a esphome compile device.yaml. Hardwarový profil není pro variantu 1.75C. Hlas vyžaduje vlastní HA Assist pipeline; MQTT nenahrazuje hlasové API.

## Obsah repozitáře

Zdrojové kódy, konfigurace sestavení a dostupné testy. Soukromé přístupy, záznamy instalací, zálohy zařízení a dříve sestavené binární soubory sem nepatří. Příklady vyplňte vlastními údaji až místně.

Toto je zdrojový snímek uložený 1. 11. 2026. Stav konkrétních kontrol je uveden v `VALIDATION.md`; nahrání zdrojů neznamená nové ověření na fyzickém zařízení.

## Další projekty

[Přehled projektů Jirkas-prog](https://github.com/Jirkas-prog) · [Home Assistant doplňky](https://github.com/Jirkas-prog/home-assistant-addons)
