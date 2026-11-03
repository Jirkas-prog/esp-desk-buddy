#include "../components/desk_buddy/i18n.h"
#include <cassert>
#include <cstdio>
#include <iostream>
#include <set>
using namespace esphome::desk_buddy;
int main() {
  std::set<std::string> keys;
  for (const auto &entry : TRANSLATIONS) {
    assert(keys.insert(entry.source).second);
    assert(*entry.cs && *entry.en);
  }
  const std::string status="Chyba hlasu: test_code_42";
  assert(status_text(false,status)=="Voice error: test_code_42");
  assert(status_text(true,status)=="Chyba hlasu: test_code_42");
  assert(status=="Chyba hlasu: test_code_42");
  assert(status_text(false,"Pripraven")=="Ready");
  assert(status_text(true,"Pripraven")=="Připraven");
  assert(status_text(false,"unknown_diagnostic_code")=="unknown_diagnostic_code");
  char text[80];
  snprintf(text,sizeof(text),ui_text(false,"Baterie %.0f%% / %.2f V"),75.0,3.85);
  assert(std::string(text)=="Battery 75% / 3.85 V");
  snprintf(text,sizeof(text),ui_text(true,"Baterie %.0f%% / %.2f V"),75.0,3.85);
  assert(std::string(text)=="Baterie 75% / 3.85 V");
  assert(std::string(ui_text(false,"cekam na cas"))=="waiting for time");
  assert(std::string(ui_text(true,"cekam na cas"))=="čekám na čas");
  assert(std::string(ui_text(false,"NVS save failed"))=="NVS save failed");
  assert(std::string(ui_text(true,"NVS save failed"))=="Ulozeni NVS selhalo");
  std::cout<<"PASS: EN/CZ status changes, preserved error codes, battery formatting and catalog uniqueness\n";
}
