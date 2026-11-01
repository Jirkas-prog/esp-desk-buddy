#pragma once
#include "lvgl.h"
#include <stdio.h>

// LVGL 9.5 made the selector an enum; QuestieV3 used integer 0 with 9.3.
#ifdef __cplusplus
inline int32_t lv_obj_get_style_radius(const lv_obj_t *obj,int part) {
    return lv_obj_get_style_radius(obj,static_cast<lv_part_t>(part));
}
#endif


#ifdef __cplusplus
extern "C" {
#endif

// Vytvori a spusti animovane oci na zadane obrazovce. Idempotentni - druhy
// hovor uz nic nepostavi (vraci se okamzite). Barva a charakter jsou
// definovany v .cpp (neon zelena, "cool frajer" osobnost).
void init_robot_eyes(lv_obj_t *parent_screen);

// Pohledy (target offsety od stredu). Animace startuji z aktualni pozice.
void robot_eyes_look_at_point(lv_coord_t screen_x, lv_coord_t screen_y);
void robot_eyes_look_center(void);
void robot_eyes_look_left(void);
void robot_eyes_look_right(void);

// Zavreni / otevreni vicek.
void robot_eyes_close(void);
void robot_eyes_open(void);

// --- Novy charakter Questie ---

// Prepnout na obrazovku oci (idempotentne - postavi je, pokud jeste nejsou).
void robot_eyes_show(void);

// Uvolni Eyes obrazovku z pameti (musi byt aktualne NEaktivni).
void robot_eyes_destroy(void);

// Velka radostna animace (po splneni ukolu). Zustane na Eyes obrazovce,
// dokud uzivatel sam neodejde. Behem par vterin utlumi do idle stavu, ale
// v dobre nalade.
void robot_eyes_celebrate(void);

// Rezim "guilt trip" - vcerejsi den bez splneneho daily ukolu.
// V tomto stavu se Questie na pohlazeni tvari smutne a vpravo nahore
// se objevi male neon-cervene "Q" na ktere se stoci pohled.
void robot_eyes_set_guilt(bool guilt);
bool robot_eyes_is_guilt(void);

// Zjisti stav "guilt" primo ze souboru daily.txt (yesterday no DONE) a
// aktualizuje interni flag. Volej pri bootu a pri zmene dne. Bezpecne
// volatelne z libovolneho vlakna (posle work na LVGL frontu).
void robot_eyes_check_daily_guilt(void);

// Vola se pokazde, kdyz uzivatel dokoncil jakykoli quest. Pro daily to
// znamena, ze mu je odpusteno.
void robot_eyes_notify_quest_completed(int category /* 0=daily */);

// Runtime tabulky (COLORS/SHAPES/STATES/EFFECTS). Nacita se z dash.txt.
// Volat pri bootu (init to dela sam) a po ulozeni dash.txt z web editoru.
void robot_eyes_reload_tables_from_dash(void);
// Zapise sekci s tabulkami do jiz otevreneho FILE* (mode "w" nebo "a").
void robot_eyes_serialize_tables_fp(FILE *f);


#ifdef __cplusplus
}
#endif
