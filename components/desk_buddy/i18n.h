#pragma once
#include <cstring>
#include <string>

namespace esphome::desk_buddy {
// Only source-owned UI/status text calls this catalog. User settings never do.
struct Translation { const char *source; const char *cs; const char *en; };
inline constexpr Translation TRANSLATIONS[] = {
  {"CEKAM NA CAS", "ČEKÁM NA ČAS", "WAITING FOR TIME"},
  {"HLASOVY ASISTENT", "HLASOVÝ ASISTENT", "VOICE ASSISTANT"},
  {"MIC ZAP", "MIC ZAP", "MIC ON"},
  {"MIC VYP", "MIC VYP", "MIC OFF"},
  {"ZVUK ZAP", "ZVUK ZAP", "SOUND ON"},
  {"ZVUK VYP", "ZVUK VYP", "SOUND OFF"},
  {"STOLNI KAMARAD", "STOLNÍ KAMARÁD", "DESK BUDDY"},
  {"PODRZ 2 s PRO VYPNUTI", "PODRŽ 2 s PRO VYPNUTÍ", "HOLD 2 s TO POWER OFF"},
  {"Zapnuti: USB / PWR", "Zapnutí: USB / PWR", "Power on: USB / PWR"},
  {"1 / 3    HODINY", "1 / 3    HODINY", "1 / 3    CLOCK"},
  {"2 / 3    HLAS", "2 / 3    HLAS", "2 / 3    VOICE"},
  {"3 / 3    NAPAJENI", "3 / 3    NAPÁJENÍ", "3 / 3    POWER"},
  {"POSLOUCHAM", "POSLOUCHÁM", "LISTENING"},
  {"PREMYSLIM", "PŘEMÝŠLÍM", "THINKING"},
  {"ODPOVIDAM", "ODPOVÍDÁM", "RESPONDING"},
  {"MIKROFON VYPNUT", "MIKROFON VYPNUT", "MICROPHONE OFF"},
  {"Baterie %.0f%% / %.2f V", "Baterie %.0f%% / %.2f V", "Battery %.0f%% / %.2f V"},
  {"Napajeni USB", "Napájení USB", "USB power"},
  {"Napajeni baterie", "Napájení baterie", "Battery power"},
  {"Startuji", "Startuji", "Starting"},
  {"Cekam na Home Assistant", "Čekám na Home Assistant", "Waiting for Home Assistant"},
  {"Chyba AXP2101", "Chyba AXP2101", "AXP2101 error"},
  {"Chyba cteni baterie", "Chyba čtení baterie", "Battery read error"},
  {"Vypnuti selhalo", "Vypnutí selhalo", "Power off failed"},
  {"Chyba ulozeni BLE", "Chyba uložení BLE", "Could not save BLE settings"},
  {"Pripraven", "Připraven", "Ready"},
  {"Home Assistant odpojen", "Home Assistant odpojen", "Home Assistant disconnected"},
  {"Posloucham", "Poslouchám", "Listening"},
  {"Premyslim", "Přemýšlím", "Thinking"},
  {"Odpovidam", "Odpovídám", "Responding"},
  {"Mikrofon vypnut", "Mikrofon vypnut", "Microphone off"},
  {"Chyba hlasu: ", "Chyba hlasu: ", "Voice error: "},
  {"rucni", "ruční", "manual"},
  {"rano", "ráno", "morning"},
  {"den", "den", "day"},
  {"vecer", "večer", "evening"},
  {"noc", "noc", "night"},
  {"cekam na cas", "čekám na čas", "waiting for time"},
  {"Cas neni synchronizovan", "Čas není synchronizován", "Time is not synchronized"},
  {"Dnes nenastava", "Dnes nenastává", "No event today"},
  {"POST required", "Je vyzadovan POST", "POST required"},
  {"Missing setup header", "Chybi hlavicka nastaveni", "Missing setup header"},
  {"NVS save failed", "Ulozeni NVS selhalo", "NVS save failed"},
  {"Neplatny host, port, SSID nebo heslo.", "Neplatny host, port, SSID nebo heslo.", "Invalid host, port, SSID or password."},
  {"Ulozeno. Zarizeni se restartuje.", "Ulozeno. Zarizeni se restartuje.", "Saved. The device is restarting."},
};
inline const char *ui_text(bool czech, const char *source) {
  for (const auto &entry : TRANSLATIONS)
    if (std::strcmp(source, entry.source) == 0) return czech ? entry.cs : entry.en;
  return source;
}
inline std::string status_text(bool czech, const std::string &status) {
  constexpr const char *prefix = "Chyba hlasu: ";
  if (status.compare(0, std::strlen(prefix), prefix) == 0)
    return std::string(ui_text(czech, prefix)) + status.substr(std::strlen(prefix));
  return ui_text(czech, status.c_str());
}
} // namespace esphome::desk_buddy
