// robot_eyes.cpp - "Questie" charakter na obrazovce ui_Eyes.
//
// Verze v8: Přepracovaný systém STAV / EFEKT.
//
//   STAV = trvalý tvar+barva očí (drží 20-60s nebo do další swipe).
//          Manuální swipe NAHORU/DOLŮ posouvá listem tvarů.
//          Dvojklik ≤3s po změně stavu/barvy cykluje barvy.
//          Dvojklik >3s dělá "magoření" (dizzy/glitch).
//   EFEKT = krátká akce (0.5-6s), po které se oči plynule vrátí
//          do aktuálního "base stavu" (nikoliv univerzálního defaultu).
//
//   SPÁNEK má flag no_brain -> žádné idle efekty se v něm nespouští.
//
//   Barevná paleta je pole COLORS[] – snadno rozšířitelné.

#include "robot_eyes.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "usb_msc_manager.h"

static const char *TAG = "eyes";

// ============================================================
// LADICÍ KONSTANTY - runtime, načítají se z /data/system/dash.txt.
// V dash.txt sekce:
//   eyes_blank_effect_pct    (0..100, default 15)
//   eyes_blank_state_pct     (0..100, default 8)
//   eyes_effect_pause_min_ms (>=0,    default 3000)
//   eyes_effect_pause_max_ms (>=min,  default 8000)
// ============================================================
// Fwd decl z quest_loader.h (loader je C API).
extern "C" int  dashboard_load_eye_tunable(const char *key, int def_val);

// Runtime defaults (přepisují se z dash.txt při init).
static int g_blank_effect_percent   = 15;
static int g_blank_state_percent    = 8;
static int g_effect_pause_min_ms    = 3000;
static int g_effect_pause_max_ms    = 8000;

static void robot_eyes_reload_tunables(void) {
    g_blank_effect_percent = dashboard_load_eye_tunable("eyes_blank_effect_pct",    15);
    g_blank_state_percent  = dashboard_load_eye_tunable("eyes_blank_state_pct",      8);
    g_effect_pause_min_ms  = dashboard_load_eye_tunable("eyes_effect_pause_min_ms", 3000);
    g_effect_pause_max_ms  = dashboard_load_eye_tunable("eyes_effect_pause_max_ms", 8000);
    if (g_blank_effect_percent < 0)   g_blank_effect_percent = 0;
    if (g_blank_effect_percent > 100) g_blank_effect_percent = 100;
    if (g_blank_state_percent  < 0)   g_blank_state_percent  = 0;
    if (g_blank_state_percent  > 100) g_blank_state_percent  = 100;
    if (g_effect_pause_min_ms  < 0)   g_effect_pause_min_ms  = 0;
    if (g_effect_pause_max_ms  < g_effect_pause_min_ms) g_effect_pause_max_ms = g_effect_pause_min_ms;
}

// Zpětně kompatibilní aliasy - vsechen zbytek kódu používá tato jména.
#define BLANK_EFFECT_PERCENT   g_blank_effect_percent
#define BLANK_STATE_PERCENT    g_blank_state_percent
#define EFFECT_PAUSE_MIN_MS    g_effect_pause_min_ms
#define EFFECT_PAUSE_MAX_MS    g_effect_pause_max_ms


// ============================================================
// BAREVNÁ PALETA - lze snadno rozšířit přidáním další položky.
// Runtime editovatelná z /data/system/dash.txt (řádky eye_color.N=name,0xHEX).
// ============================================================
#define COLOR_MAX 16
typedef struct {
    char     name[24];
    uint32_t hex;
} eye_color_t;

static eye_color_t COLORS[COLOR_MAX] = {
    { "turquoise", 0x00FFEA },
    { "orange",    0xFF9200 },
    { "red",       0xFF2040 },
    { "pink",      0xFF3EA5 },
    // <-- výchozí paleta; další barvy se přidávají přes web editor.
};
static int COLOR_COUNT = 4;
static const int COLOR_DEFAULT_COUNT = 4;
static const eye_color_t COLOR_DEFAULTS[COLOR_MAX] = {
    { "turquoise", 0x00FFEA },
    { "orange",    0xFF9200 },
    { "red",       0xFF2040 },
    { "pink",      0xFF3EA5 },
};


// ============================================================
// TVARY OČÍ (stavy - shape presety)
//   Pořadí definuje listování swipem NAHORU/DOLŮ.
//   no_brain=true zakazuje idle efekty (spánek).
// ============================================================
typedef enum {
    SHAPE_OVAL = 0,         // Oválky – NOVÝ VÝCHOZÍ stav (110x190, r=55)
    SHAPE_OVAL_LOW,         // Oválky nižší (110x160, r=55)
    SHAPE_ROUND,            // Kulaté
    SHAPE_ROUNDED_SQ,       // Čtverec se zaoblenými rohy
    SHAPE_RECT,             // Obdélníkové (vyšší než širší, výchozí)
    SHAPE_SQUARE,           // Čtvercové (ostré rohy)
    SHAPE_SLIT_WIDE,        // Širší štěrbina
    SHAPE_SLIT_NARROW,      // Užší štěrbina
    SHAPE_SLEEP,            // Spánek (nejužší štěrbina, no_brain)
    SHAPE_BLANK,            // "Prázdný" stav – neaplikuje se, používá se jen jako placeholder
    SHAPE_COUNT
} eye_shape_t;

typedef struct {
    eye_shape_t id;
    char        name[24];
    int32_t     w;         // šířka oka
    int32_t     h;         // výška oka
    int32_t     radius;    // poloměr zaoblení
    bool        no_brain;  // true = blokuje idle efekty (spánek)
} shape_def_t;

// SHAPE_COUNT je fixní (svázaný s enumem); tabulka je runtime editovatelná
// jen v hodnotách w/h/radius/no_brain/name.
#define SHAPE_MAX SHAPE_COUNT
static shape_def_t SHAPES[SHAPE_MAX] = {
    // { id,               name,             w,   h,   radius, no_brain }
    { SHAPE_OVAL,          "oval",          110, 190, 55,     false },
    { SHAPE_OVAL_LOW,      "oval_low",      110, 160, 55,     false },
    { SHAPE_ROUND,         "round",         110, 110, 55,     false },
    { SHAPE_ROUNDED_SQ,    "rounded_sq",    120, 120, 32,     false },
    { SHAPE_RECT,          "rect",          110, 190, 14,     false },
    { SHAPE_SQUARE,        "square",        130, 130, 0,      false },
    { SHAPE_SLIT_WIDE,     "slit_wide",     140, 50,  22,     false },
    { SHAPE_SLIT_NARROW,   "slit_narrow",   140, 22,  10,     false },
    { SHAPE_SLEEP,         "sleep",         130, 8,   4,      true  },
    { SHAPE_BLANK,         "blank",           0, 0,   0,      false },
};
static const shape_def_t SHAPE_DEFAULTS[SHAPE_MAX] = {
    { SHAPE_OVAL,          "oval",          110, 190, 55,     false },
    { SHAPE_OVAL_LOW,      "oval_low",      110, 160, 55,     false },
    { SHAPE_ROUND,         "round",         110, 110, 55,     false },
    { SHAPE_ROUNDED_SQ,    "rounded_sq",    120, 120, 32,     false },
    { SHAPE_RECT,          "rect",          110, 190, 14,     false },
    { SHAPE_SQUARE,        "square",        130, 130, 0,      false },
    { SHAPE_SLIT_WIDE,     "slit_wide",     140, 50,  22,     false },
    { SHAPE_SLIT_NARROW,   "slit_narrow",   140, 22,  10,     false },
    { SHAPE_SLEEP,         "sleep",         130, 8,   4,      true  },
    { SHAPE_BLANK,         "blank",           0, 0,   0,      false },
};


// ============================================================
// TABULKA STAVŮ - používá idle "mozek" k občasným samovolným
// přepnutím tvaru/barvy. Uživatel může kdykoli přebít swipem/dvojklikem.
//
// Formát řádku:
//   { prob, min_ms, max_ms, shape, color_idx, extra }, // popis
//
//   prob    0..100  relativní váha (0 = nikdy)
//   min_ms  minimální doba trvání stavu (ms)
//   max_ms  maximální doba trvání stavu (ms)
//   shape   viz SHAPES[] výše
//   color   index do COLORS[] (0=turq, 1=orange, 2=red, 3=pink, ...)
//   extra   rezerva pro budoucí flagy
//
//   Trojice/N-tice řádků = jeden tvar ve všech barvách, pak prázdný řádek.
// ============================================================
typedef struct {
    int         prob;
    uint32_t    min_ms;
    uint32_t    max_ms;
    eye_shape_t shape;
    int         color;
    uint32_t    extra;
} state_entry_t;

#define STATE_MAX 128
static state_entry_t STATES[STATE_MAX] = {

    { 80, 25000, 60000, SHAPE_OVAL,         0, 0 }, // Oválky – turquoise (default)
    { 50, 25000, 60000, SHAPE_OVAL,         1, 0 }, // Oválky – orange
    { 25, 25000, 60000, SHAPE_OVAL,         2, 0 }, // Oválky – red
    { 40, 25000, 60000, SHAPE_OVAL,         3, 0 }, // Oválky – pink

    { 45, 20000, 60000, SHAPE_OVAL_LOW,     0, 0 }, // Oválky nižší – turquoise
    { 30, 20000, 60000, SHAPE_OVAL_LOW,     1, 0 }, // Oválky nižší – orange
    { 15, 20000, 60000, SHAPE_OVAL_LOW,     2, 0 }, // Oválky nižší – red
    { 25, 20000, 60000, SHAPE_OVAL_LOW,     3, 0 }, // Oválky nižší – pink

    { 60, 20000, 60000, SHAPE_ROUND,        0, 0 }, // Kulaté  – turquoise
    { 40, 20000, 60000, SHAPE_ROUND,        1, 0 }, // Kulaté  – orange
    { 20, 20000, 60000, SHAPE_ROUND,        2, 0 }, // Kulaté  – red
    { 40, 20000, 60000, SHAPE_ROUND,        3, 0 }, // Kulaté  – pink

    { 40, 20000, 60000, SHAPE_ROUNDED_SQ,   0, 0 }, // ZaoblČtverec – turquoise
    { 30, 20000, 60000, SHAPE_ROUNDED_SQ,   1, 0 }, // ZaoblČtverec – orange
    { 15, 20000, 60000, SHAPE_ROUNDED_SQ,   2, 0 }, // ZaoblČtverec – red
    { 30, 20000, 60000, SHAPE_ROUNDED_SQ,   3, 0 }, // ZaoblČtverec – pink

    { 60, 20000, 60000, SHAPE_RECT,         0, 0 }, // Obdélník – turquoise
    { 40, 20000, 60000, SHAPE_RECT,         1, 0 }, // Obdélník – orange
    { 20, 20000, 60000, SHAPE_RECT,         2, 0 }, // Obdélník – red
    { 30, 20000, 60000, SHAPE_RECT,         3, 0 }, // Obdélník – pink

    { 25, 20000, 60000, SHAPE_SQUARE,       0, 0 }, // Čtverec – turquoise
    { 25, 20000, 60000, SHAPE_SQUARE,       1, 0 }, // Čtverec – orange
    { 10, 20000, 60000, SHAPE_SQUARE,       2, 0 }, // Čtverec – red
    { 20, 20000, 60000, SHAPE_SQUARE,       3, 0 }, // Čtverec – pink

    { 20, 15000, 45000, SHAPE_SLIT_WIDE,    0, 0 }, // Štěrbina široká – turq
    { 20, 15000, 45000, SHAPE_SLIT_WIDE,    1, 0 }, // Štěrbina široká – orange
    { 10, 15000, 45000, SHAPE_SLIT_WIDE,    2, 0 }, // Štěrbina široká – red
    { 15, 15000, 45000, SHAPE_SLIT_WIDE,    3, 0 }, // Štěrbina široká – pink

    { 15, 15000, 45000, SHAPE_SLIT_NARROW,  0, 0 }, // Štěrbina úzká – turq
    { 15, 15000, 45000, SHAPE_SLIT_NARROW,  1, 0 }, // Štěrbina úzká – orange
    {  8, 15000, 45000, SHAPE_SLIT_NARROW,  2, 0 }, // Štěrbina úzká – red
    { 12, 15000, 45000, SHAPE_SLIT_NARROW,  3, 0 }, // Štěrbina úzká – pink

    {  5, 30000, 60000, SHAPE_SLEEP,        0, 0 }, // Spánek – turquoise (no_brain)
    {  5, 30000, 60000, SHAPE_SLEEP,        1, 0 }, // Spánek – orange   (no_brain)
    {  3, 30000, 60000, SHAPE_SLEEP,        2, 0 }, // Spánek – red      (no_brain)
    {  4, 30000, 60000, SHAPE_SLEEP,        3, 0 }, // Spánek – pink     (no_brain)
};
static int STATE_COUNT = 36;
static const int STATE_DEFAULT_COUNT = 36;
static const state_entry_t STATE_DEFAULTS[STATE_MAX] = {
    { 80, 25000, 60000, SHAPE_OVAL,         0, 0 },
    { 50, 25000, 60000, SHAPE_OVAL,         1, 0 },
    { 25, 25000, 60000, SHAPE_OVAL,         2, 0 },
    { 40, 25000, 60000, SHAPE_OVAL,         3, 0 },
    { 45, 20000, 60000, SHAPE_OVAL_LOW,     0, 0 },
    { 30, 20000, 60000, SHAPE_OVAL_LOW,     1, 0 },
    { 15, 20000, 60000, SHAPE_OVAL_LOW,     2, 0 },
    { 25, 20000, 60000, SHAPE_OVAL_LOW,     3, 0 },
    { 60, 20000, 60000, SHAPE_ROUND,        0, 0 },
    { 40, 20000, 60000, SHAPE_ROUND,        1, 0 },
    { 20, 20000, 60000, SHAPE_ROUND,        2, 0 },
    { 40, 20000, 60000, SHAPE_ROUND,        3, 0 },
    { 40, 20000, 60000, SHAPE_ROUNDED_SQ,   0, 0 },
    { 30, 20000, 60000, SHAPE_ROUNDED_SQ,   1, 0 },
    { 15, 20000, 60000, SHAPE_ROUNDED_SQ,   2, 0 },
    { 30, 20000, 60000, SHAPE_ROUNDED_SQ,   3, 0 },
    { 60, 20000, 60000, SHAPE_RECT,         0, 0 },
    { 40, 20000, 60000, SHAPE_RECT,         1, 0 },
    { 20, 20000, 60000, SHAPE_RECT,         2, 0 },
    { 30, 20000, 60000, SHAPE_RECT,         3, 0 },
    { 25, 20000, 60000, SHAPE_SQUARE,       0, 0 },
    { 25, 20000, 60000, SHAPE_SQUARE,       1, 0 },
    { 10, 20000, 60000, SHAPE_SQUARE,       2, 0 },
    { 20, 20000, 60000, SHAPE_SQUARE,       3, 0 },
    { 20, 15000, 45000, SHAPE_SLIT_WIDE,    0, 0 },
    { 20, 15000, 45000, SHAPE_SLIT_WIDE,    1, 0 },
    { 10, 15000, 45000, SHAPE_SLIT_WIDE,    2, 0 },
    { 15, 15000, 45000, SHAPE_SLIT_WIDE,    3, 0 },
    { 15, 15000, 45000, SHAPE_SLIT_NARROW,  0, 0 },
    { 15, 15000, 45000, SHAPE_SLIT_NARROW,  1, 0 },
    {  8, 15000, 45000, SHAPE_SLIT_NARROW,  2, 0 },
    { 12, 15000, 45000, SHAPE_SLIT_NARROW,  3, 0 },
    {  5, 30000, 60000, SHAPE_SLEEP,        0, 0 },
    {  5, 30000, 60000, SHAPE_SLEEP,        1, 0 },
    {  3, 30000, 60000, SHAPE_SLEEP,        2, 0 },
    {  4, 30000, 60000, SHAPE_SLEEP,        3, 0 },
};


// ============================================================
// TABULKA EFEKTŮ - krátké akce; po skončení návrat do base stavu.
//
// Formát řádku:
//   { prob, min_ms, max_ms, fn }, // popis
// ============================================================
typedef struct {
    int      prob;
    uint32_t min_ms;
    uint32_t max_ms;
    void   (*fn)(uint32_t);
    const char *desc;
} effect_entry_t;

// Fwd decl (definice jsou níže v souboru).
static void happy_shape(uint32_t);
static void sad_shape(uint32_t);
static void wink_shape(uint32_t);
static void surprise_shape(uint32_t);
static void sleepy_shape(uint32_t);
static void suspicious_shape(uint32_t);
static void love_shape(uint32_t);
static void scan_shape(uint32_t);
static void slow_glitch_shape(uint32_t);
static void narrow_shape(uint32_t);
static void triangle_shape(uint32_t);
static void circle_shape(uint32_t);
static void rolling_circles_shape(uint32_t);
static void dizzy_shape(uint32_t);
static void glance_lr_shape(uint32_t);
static void glance_left_shape(uint32_t);
static void glance_right_shape(uint32_t);
static void angry_shape(uint32_t);
static void glitch_shape(uint32_t);
static void blink_eyes(void);

// Wrappery pro krátké efekty. blink_effect_wrap dela vzor
// "mrk - pauza - mrk mrk - pauza - mrk" (nikoli sbijecka), eyebrow_effect_wrap
// zdvihne obociko a spusti ho zpet. Povinnou "tail pauzu" po efektu
// pridava run_random_effect().
static void eyebrow_effect_wrap(uint32_t hold_ms);
static void blink_effect_wrap(uint32_t hold_ms);
// BLANK efekt – nic neudělá, jen "spálí" hold_ms (klid mezi efekty).
static void blank_effect(uint32_t /*hold_ms*/) { /* nic */ }

// Efekty jsou svázané s C funkcemi (fn/desc jsou fixní); runtime se editují
// jen váhy a časy (prob/min_ms/max_ms) v /data/system/dash.txt (eye_effect.N=).
static effect_entry_t EFFECTS[] = {
    // BLANK je speciální – jeho "prob" je absolutní procento (viz BLANK_EFFECT_PERCENT).
    // Ponecháváme prob=0, aby ho vážený výběr nechytil; logika je oddělená.
    {  0, 1000, 3000,  blank_effect,          "Blank / žádná změna" },
    { 80, 1800, 3000,  blink_effect_wrap,     "Mrknutí (mrk – pauza – mrk mrk – pauza – mrk)" },
    { 60, 1500, 3500,  happy_shape,           "Radost / úsměv" },
    { 55,  700, 1400,  wink_shape,            "Mrknutí jedním okem" },
    { 40, 1500, 3000,  surprise_shape,        "Překvapení" },
    { 30, 2000, 4000,  sleepy_shape,          "Ospalost" },
    { 35, 2500, 4500,  suspicious_shape,      "Podezřívavost" },
    { 25, 2000, 4000,  love_shape,            "Zamilované oči" },
    { 30, 2500, 5000,  scan_shape,            "Skenování" },
    { 20, 3000, 6000,  slow_glitch_shape,     "Pomalý glitch" },
    { 35, 1500, 3000,  narrow_shape,          "Zúžení / mhouření" },
    { 20, 3000, 5000,  triangle_shape,        "Trojúhelníky" },
    { 20, 3000, 5000,  circle_shape,          "Kruhy (dočasně)" },
    { 15, 3500, 6000,  rolling_circles_shape, "Kroužení po obrazovce" },
    { 20, 2000, 4000,  dizzy_shape,           "Motání / závrať" },
    { 70,  900, 1800,  glance_lr_shape,       "Rozhlédnutí L+R" },
    { 70,  700, 1400,  glance_left_shape,     "Pohled doleva" },
    { 70,  700, 1400,  glance_right_shape,    "Pohled doprava" },
    { 50, 1500, 3500,  eyebrow_effect_wrap,   "Zdvižení obočí" },
    {  8, 2000, 4000,  angry_shape,           "Vztek" },
    {  2,  600, 1500,  glitch_shape,          "Rychlý glitch" },
    { 10, 2500, 4500,  sad_shape,             "Smutek (guilt)" },
};
static const int EFFECT_COUNT = (int)(sizeof(EFFECTS) / sizeof(EFFECTS[0]));
// Defaultní prob/min/max/desc pro reset na tovární hodnoty a web UI.
typedef struct { int prob; uint32_t min_ms; uint32_t max_ms; } effect_tuning_default_t;
static const effect_tuning_default_t EFFECT_TUNING_DEFAULTS[] = {
    {  0, 1000, 3000 }, { 80, 1800, 3000 }, { 60, 1500, 3500 }, { 55,  700, 1400 },
    { 40, 1500, 3000 }, { 30, 2000, 4000 }, { 35, 2500, 4500 }, { 25, 2000, 4000 },
    { 30, 2500, 5000 }, { 20, 3000, 6000 }, { 35, 1500, 3000 }, { 20, 3000, 5000 },
    { 20, 3000, 5000 }, { 15, 3500, 6000 }, { 20, 2000, 4000 }, { 70,  900, 1800 },
    { 70,  700, 1400 }, { 70,  700, 1400 }, { 50, 1500, 3500 }, {  8, 2000, 4000 },
    {  2,  600, 1500 }, { 10, 2500, 4500 },
};


// Okno pro cyklování barev dvojklikem po změně stavu.
#define STATE_COLOR_CYCLE_WINDOW_MS 3000

// ============================================================
// KONFIGURACE (ostatní parametry animací a chování)
// ============================================================
typedef struct {
    // --- Barvy speciálních efektů (nezávislé na barvě očí) ---
    uint32_t color_love;
    uint32_t color_warn;
    uint32_t color_angry;
    uint32_t color_guilt_q;
    uint32_t color_bg_mask;

    // --- Geometrie a limity ---
    int32_t eye_width;
    int32_t eye_height;
    int32_t eye_radius_max;
    int32_t eye_spacing;
    int32_t eye_height_closed;
    int32_t eye_height_sad;
    int32_t eye_height_surprise;
    int32_t eye_width_surprise;
    int32_t eye_min_dim;
    int32_t border_width;
    int32_t outline_width;
    int32_t mask_smile_width_over;
    int32_t mask_smile_bottom_over;

    int32_t screen_default_dim;
    int32_t look_max_x;
    int32_t look_max_y;
    int32_t look_scale_percent;

    int32_t tri_canvas_w;
    int32_t tri_canvas_h;
    int32_t tri_inset;
    int32_t tri_corner_r;

    uint32_t night_start_hour;
    uint32_t night_end_hour;
    uint32_t idle_min_ms;
    uint32_t idle_max_ms;
    uint32_t look_idle_return_ms;
    uint32_t finger_interest_ms;

    uint32_t double_tap_min_ms;
    uint32_t double_tap_max_ms;
    int32_t  double_tap_max_px;
    int32_t  caress_min_reversals;
    int32_t  caress_min_swing_px;
    uint32_t caress_window_ms;
    uint32_t caress_min_ms;
    uint32_t swipe_max_ms;
    int32_t  swipe_min_px;
    int32_t  swipe_dom_num;
    int32_t  swipe_dom_den;
    uint32_t browse_hold_ms;
    uint32_t gesture_reaction_min_ms;
    uint32_t gesture_reaction_rand_ms;
    uint32_t caress_happy_hold_ms;

    uint32_t anim_state_transition_ms; // rychlost přechodu mezi stavy
    uint32_t anim_settle_ms;
    uint32_t anim_sleep_h_ms;
    uint32_t anim_sleep_pos_ms;
    uint32_t anim_blink_close_ms;
    uint32_t anim_blink_open_ms;
    uint32_t anim_dbl_blink_open_ms;
    uint32_t anim_look_ms;
    uint32_t anim_look_smooth_ms;
    uint32_t anim_eyes_close_ms;
    uint32_t anim_eyes_open_ms;

    int32_t  happy_h_offset;
    int32_t  happy_w_offset;
    uint32_t anim_happy_resize_ms;
    int32_t  happy_bounce_y_min;
    int32_t  happy_bounce_y_max;
    uint32_t anim_happy_bounce_ms;

    uint32_t anim_sad_resize_ms;
    int32_t  sad_radius;
    int32_t  sad_y_offset;

    uint32_t anim_wink_resize_ms;
    int32_t  wink_other_h_offset;
    int32_t  wink_other_y_offset;

    uint32_t anim_surprise_resize_ms;
    int32_t  surprise_bounce_y;
    uint32_t anim_surprise_bounce_ms;

    int32_t  sleepy_h;
    int32_t  sleepy_w_offset;
    int32_t  sleepy_radius;
    uint32_t anim_sleepy_resize_ms;
    int32_t  sleepy_bounce_y;
    uint32_t anim_sleepy_bounce_ms;

    int32_t  suspicious_h;
    int32_t  suspicious_w_offset;
    int32_t  suspicious_radius;
    uint32_t anim_suspicious_resize_ms;
    uint32_t anim_suspicious_scan_ms;

    int32_t  love_size;
    int32_t  love_radius;
    uint32_t anim_love_resize_ms;
    int32_t  love_beat_size;
    uint32_t anim_love_beat_ms;

    int32_t  dizzy_size;
    uint32_t anim_dizzy_resize_ms;
    int32_t  dizzy_bounce_x;
    int32_t  dizzy_bounce_y;
    uint32_t anim_dizzy_x_ms;
    uint32_t anim_dizzy_y_ms;

    int32_t  scan_h;
    int32_t  scan_w_offset;
    int32_t  scan_radius;
    uint32_t anim_scan_resize_ms;
    uint32_t anim_scan_move_ms;

    int32_t  glitch_x;
    uint32_t anim_glitch_x_ms;
    int32_t  glitch_y;
    uint32_t anim_glitch_y_ms;
    int32_t  glitch_h_offset;
    uint32_t anim_glitch_h_ms;
    uint32_t anim_glitch_color_ms;

    int32_t  slow_glitch_x;
    uint32_t anim_slow_glitch_x_ms;
    int32_t  slow_glitch_y;
    uint32_t anim_slow_glitch_y_ms;
    uint32_t anim_slow_glitch_color_ms;

    int32_t  narrow_h;
    int32_t  narrow_w_offset;
    int32_t  narrow_radius;
    uint32_t anim_narrow_resize_ms;
    int32_t  narrow_bounce_y;
    uint32_t anim_narrow_bounce_ms;

    int32_t  glance_offset_x;
    uint32_t anim_glance_hold_1_ms;
    uint32_t anim_glance_hold_2_ms;
    uint32_t anim_glance_single_ms;

    uint32_t anim_circle_resize_ms;
    uint32_t anim_rolling_intro_ms;
    uint32_t anim_triangle_fade_ms;

    int32_t  angry_h;
    int32_t  angry_w_offset;
    int32_t  angry_radius;
    int32_t  angry_x_offset;
    uint32_t anim_angry_resize_ms;
    int32_t  angry_shake_y;
    uint32_t anim_angry_shake_ms;

    int32_t  eyebrow_h_offset;
    uint32_t anim_eyebrow_raise_ms;
    uint32_t anim_eyebrow_lower_ms;

    // color_eye je aktuální barva base stavu (přepočítává se z s_current_color_idx).
    uint32_t color_eye;
} robot_eyes_config_t;

robot_eyes_config_t cfg = {
    .color_love = 0xFF3EA5, .color_warn = 0xFFC400,
    .color_angry = 0xFF2040, .color_guilt_q = 0xFF1030, .color_bg_mask = 0x000000,

    .eye_width = 110, .eye_height = 190, .eye_radius_max = 55, .eye_spacing = 85,
    .eye_height_closed = 8, .eye_height_sad = 90, .eye_height_surprise = 210,
    .eye_width_surprise = 140, .eye_min_dim = 1, .border_width = 4, .outline_width = 12,
    .mask_smile_width_over = 40, .mask_smile_bottom_over = 30,

    .screen_default_dim = 466, .look_max_x = 100, .look_max_y = 60, .look_scale_percent = 45,

    .tri_canvas_w = 90, .tri_canvas_h = 140, .tri_inset = 8, .tri_corner_r = 12,

    .night_start_hour = 23, .night_end_hour = 6, .idle_min_ms = 3000, .idle_max_ms = 15000,
    .look_idle_return_ms = 3000, .finger_interest_ms = 1000,

    .double_tap_min_ms = 40, .double_tap_max_ms = 350, .double_tap_max_px = 80,
    .caress_min_reversals = 3, .caress_min_swing_px = 35, .caress_window_ms = 2200,
    .caress_min_ms = 400, .swipe_max_ms = 500, .swipe_min_px = 55,
    .swipe_dom_num = 3, .swipe_dom_den = 2, .browse_hold_ms = 10000,
    .gesture_reaction_min_ms = 3500, .gesture_reaction_rand_ms = 2500, .caress_happy_hold_ms = 4500,

    .anim_state_transition_ms = 450,
    .anim_settle_ms = 500, .anim_sleep_h_ms = 900, .anim_sleep_pos_ms = 700,
    .anim_blink_close_ms = 90, .anim_blink_open_ms = 140, .anim_dbl_blink_open_ms = 130,
    .anim_look_ms = 380, .anim_look_smooth_ms = 700, .anim_eyes_close_ms = 1500,
    .anim_eyes_open_ms = 300,

    .happy_h_offset = 30, .happy_w_offset = 20, .anim_happy_resize_ms = 400,
    .happy_bounce_y_min = -14, .happy_bounce_y_max = 8, .anim_happy_bounce_ms = 550,
    .anim_sad_resize_ms = 500, .sad_radius = 25, .sad_y_offset = 10,
    .anim_wink_resize_ms = 250, .wink_other_h_offset = 20, .wink_other_y_offset = -6,
    .anim_surprise_resize_ms = 320, .surprise_bounce_y = 3, .anim_surprise_bounce_ms = 120,
    .sleepy_h = 50, .sleepy_w_offset = 10, .sleepy_radius = 25, .anim_sleepy_resize_ms = 700,
    .sleepy_bounce_y = 6, .anim_sleepy_bounce_ms = 800,
    .suspicious_h = 40, .suspicious_w_offset = 15, .suspicious_radius = 20,
    .anim_suspicious_resize_ms = 500, .anim_suspicious_scan_ms = 900,
    .love_size = 130, .love_radius = 65, .anim_love_resize_ms = 400,
    .love_beat_size = 145, .anim_love_beat_ms = 350,
    .dizzy_size = 130, .anim_dizzy_resize_ms = 300, .dizzy_bounce_x = 35, .dizzy_bounce_y = 25,
    .anim_dizzy_x_ms = 220, .anim_dizzy_y_ms = 260,
    .scan_h = 60, .scan_w_offset = 30, .scan_radius = 20, .anim_scan_resize_ms = 400,
    .anim_scan_move_ms = 1200,
    .glitch_x = 10, .anim_glitch_x_ms = 45, .glitch_y = 6, .anim_glitch_y_ms = 60,
    .glitch_h_offset = 40, .anim_glitch_h_ms = 120, .anim_glitch_color_ms = 700,
    .slow_glitch_x = 18, .anim_slow_glitch_x_ms = 260, .slow_glitch_y = 10,
    .anim_slow_glitch_y_ms = 320, .anim_slow_glitch_color_ms = 2000,
    .narrow_h = 32, .narrow_w_offset = 25, .narrow_radius = 16, .anim_narrow_resize_ms = 380,
    .narrow_bounce_y = 3, .anim_narrow_bounce_ms = 350,
    .glance_offset_x = 30, .anim_glance_hold_1_ms = 450, .anim_glance_hold_2_ms = 950,
    .anim_glance_single_ms = 700,
    .anim_circle_resize_ms = 400, .anim_rolling_intro_ms = 600, .anim_triangle_fade_ms = 280,
    .angry_h = 45, .angry_w_offset = 20, .angry_radius = 20, .angry_x_offset = 8,
    .anim_angry_resize_ms = 260, .angry_shake_y = 6, .anim_angry_shake_ms = 45,
    .eyebrow_h_offset = 30, .anim_eyebrow_raise_ms = 700, .anim_eyebrow_lower_ms = 900,

    .color_eye = 0x00FFEA, // přepíše se z COLORS[s_current_color_idx] v initu
};


// ---- State ----
static lv_obj_t *s_parent    = NULL;
static lv_obj_t *s_root      = NULL;
static lv_obj_t *left_eye    = NULL;
static lv_obj_t *right_eye   = NULL;
static lv_obj_t *s_guilt_q   = NULL;

static lv_obj_t *s_smile_mask_l = NULL;
static lv_obj_t *s_smile_mask_r = NULL;
static lv_obj_t *s_frown_mask_l = NULL;
static lv_obj_t *s_frown_mask_r = NULL;

static lv_obj_t *s_deco_l = NULL;
static lv_obj_t *s_deco_r = NULL;

static lv_obj_t *s_tri_l     = NULL;
static lv_obj_t *s_tri_r     = NULL;
static void     *s_tri_buf_l = NULL;
static void     *s_tri_buf_r = NULL;

static lv_timer_t *s_rolling_timer     = NULL;
static uint32_t    s_rolling_start_ms  = 0;
static uint32_t    s_rolling_duration_ms = 0;

static lv_timer_t *s_smile_track_timer = NULL;
static lv_timer_t *s_brain_timer       = NULL;
static lv_timer_t *s_sleep_retry_timer = NULL;

static int32_t s_off_x   = 0;
static int32_t s_off_y   = 0;
static int32_t s_eye_h_l = 0;
static int32_t s_eye_h_r = 0;
static int32_t s_eye_w_l = 0;
static int32_t s_eye_w_r = 0;
static bool    s_closed    = false;
static bool    s_guilt     = false;
static bool    s_sleeping  = false;
static uint32_t s_mood_happy_until_ms = 0;
static uint32_t s_reaction_until_ms   = 0;

// True, dokud běží krátký EFEKT. Po dokončení se voláním return_to_base_state()
// oči plynule vrátí do (s_current_shape_idx, s_current_color_idx).
static bool s_effect_active = false;

// Legacy flag - zachován kvůli starším efektům (circle_shape, triangle_shape),
// které jej používaly jako "tenhle tvar je stavový, nevracet". V novém modelu
// je návrat řízen s_effect_active + return_to_base_state().
static bool s_shape_is_state = true;

static uint32_t s_next_action_ms = 0;

static volatile bool     s_finger_down = false;
static volatile uint32_t s_finger_release_ms = 0;

static lv_coord_t s_last_pressing_x = 0;
static lv_coord_t s_last_pressing_y = 0;
static uint32_t   s_press_start_ms = 0;
static uint32_t   s_press_dist_acc = 0;
static bool       s_caress_triggered = false;
static lv_coord_t s_press_start_x  = 0;
static lv_coord_t s_press_start_y  = 0;
static int8_t     s_last_dx_sign   = 0;
static lv_coord_t s_swing_extreme_x = 0;
static uint8_t    s_caress_reversals = 0;
static uint32_t   s_caress_window_start_ms = 0;

static bool       s_effect_locked = false;

// Emotion browsing (swipe L/R prochází efekty).
static int        s_browse_index = -1;
static uint32_t   s_browse_last_ms = 0;

// Double-tap
static uint32_t   s_last_release_ms = 0;
static lv_coord_t s_last_release_x  = 0;
static lv_coord_t s_last_release_y  = 0;
static uint8_t    s_tap_count       = 0;
static bool s_double_tap_triggered = false;

static uint32_t s_last_look_ms = 0;
static bool     s_look_offcenter = false;

// === NOVÉ: base state (trvalý tvar + barva) ===
static int      s_current_shape_idx = SHAPE_OVAL; // výchozí = oválky (nový)
static int      s_current_color_idx = 0;          // 0 = turquoise
static uint32_t s_last_state_change_ms = 0;       // poslední swipe up/down nebo cyklus barvy
static uint32_t s_state_hold_until_ms  = 0;       // dokdy má brain daný stav držet (0 = navěky)

// Fwd
static void apply_base_state(int shape_idx, int color_idx, uint32_t ms, bool from_effect);
static void return_to_base_state(uint32_t ms);
static void schedule_next_idle_action(void);
static void schedule_next_state_pick(void);
static void brain_pick_random_state(void);
static bool current_shape_no_brain(void);

extern "C" void __attribute__((weak)) power_manager_eye_touch(lv_coord_t x, lv_coord_t y) {
    (void)x; (void)y;
}
// ============================================================
// Animation setters
// ============================================================
static inline int32_t clamp_dim(int32_t v) {
    return v < cfg.eye_min_dim ? cfg.eye_min_dim : v;
}
static void set_h_left_cb(void *obj, int32_t v)  { v = clamp_dim(v); lv_obj_set_height((lv_obj_t*)obj, v); s_eye_h_l = v; }
static void set_h_right_cb(void *obj, int32_t v) { v = clamp_dim(v); lv_obj_set_height((lv_obj_t*)obj, v); s_eye_h_r = v; }
static void set_w_left_cb(void *obj, int32_t v)  { v = clamp_dim(v); lv_obj_set_width((lv_obj_t*)obj, v);  s_eye_w_l = v; }
static void set_w_right_cb(void *obj, int32_t v) { v = clamp_dim(v); lv_obj_set_width((lv_obj_t*)obj, v);  s_eye_w_r = v; }
static void set_radius_cb(void *obj, int32_t v)  {
    if (v < 0) v = 0;
    lv_obj_set_style_radius((lv_obj_t*)obj, v, (uint32_t)LV_PART_MAIN | (uint32_t)LV_STATE_DEFAULT);
}
static void set_x_left_cb(void *obj, int32_t v) {
    lv_obj_set_x((lv_obj_t*)obj, -cfg.eye_spacing + v);
    if ((lv_obj_t*)obj == left_eye) s_off_x = v;
}
static void set_x_right_cb(void *obj, int32_t v) {
    lv_obj_set_x((lv_obj_t*)obj,  cfg.eye_spacing + v);
}
static void set_y_cb(void *obj, int32_t v) {
    lv_obj_set_y((lv_obj_t*)obj, v);
    if ((lv_obj_t*)obj == left_eye) s_off_y = v;
}
static void set_opa_cb(void *obj, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t*)obj, (lv_opa_t)v, 0);
}
static void set_bg_hue_cb(void *obj, int32_t v) {
    lv_color_t c;
    if      (v < 60)  c = lv_color_hex(cfg.color_warn);
    else if (v < 180) c = lv_color_hex(cfg.color_eye);
    else if (v < 300) c = lv_color_hex(cfg.color_love);
    else              c = lv_color_hex(cfg.color_eye);
    lv_obj_set_style_bg_color((lv_obj_t*)obj, c, 0);
    lv_obj_set_style_border_color((lv_obj_t*)obj, c, 0);
    lv_obj_set_style_outline_color((lv_obj_t*)obj, c, 0);
}


// ============================================================
// Anim paths
// ============================================================
static int32_t anim_path_overshoot(const lv_anim_t *a) {
    const int32_t ANIM_RES = 1024;
    int32_t t = lv_map(a->act_time, 0, a->duration, 0, ANIM_RES);
    if (t > ANIM_RES) t = ANIM_RES;
    float x = (float)t / (float)ANIM_RES;
    x -= 1.0f;
    const float s = 1.70158f;
    float y = x * x * ((s + 1.0f) * x + s) + 1.0f;
    return a->start_value + (int32_t)((a->end_value - a->start_value) * y);
}

static int32_t anim_path_bounce_out(const lv_anim_t *a) {
    const int32_t ANIM_RES = 1024;
    int32_t t = lv_map(a->act_time, 0, a->duration, 0, ANIM_RES);
    if (t > ANIM_RES) t = ANIM_RES;
    float x = (float)t / (float)ANIM_RES;
    float y;
    const float n1 = 7.5625f;
    const float d1 = 2.75f;
    if      (x < 1.0f / d1)  y = n1 * x * x;
    else if (x < 2.0f / d1) { x -= 1.5f / d1;    y = n1 * x * x + 0.75f; }
    else if (x < 2.5f / d1) { x -= 2.25f / d1;   y = n1 * x * x + 0.9375f; }
    else                    { x -= 2.625f / d1;  y = n1 * x * x + 0.984375f; }
    return a->start_value + (int32_t)((a->end_value - a->start_value) * y);
}

// ============================================================
// Helpers
// ============================================================
static void destroy_triangles(void);
static void stop_rolling(void);

static void kill_shape_anims(lv_obj_t *o) {
    if (!o) return;
    lv_anim_delete(o, set_h_left_cb);
    lv_anim_delete(o, set_h_right_cb);
    lv_anim_delete(o, set_w_left_cb);
    lv_anim_delete(o, set_w_right_cb);
    lv_anim_delete(o, set_radius_cb);
    lv_anim_delete(o, set_y_cb);
    lv_anim_delete(o, set_x_left_cb);
    lv_anim_delete(o, set_x_right_cb);
    lv_anim_delete(o, set_bg_hue_cb);
}

static void anim_int(lv_obj_t *o, int32_t from, int32_t to, uint32_t ms,
                     lv_anim_exec_xcb_t setter, lv_anim_path_cb_t path = lv_anim_path_ease_in_out) {
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, setter);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_time(&a, ms);
    lv_anim_set_path_cb(&a, path);
    lv_anim_start(&a);
}

static void anim_int_pb(lv_obj_t *o, int32_t from, int32_t to, uint32_t ms, uint32_t pb_ms,
                        lv_anim_exec_xcb_t setter,
                        lv_anim_path_cb_t path = lv_anim_path_ease_in_out) {
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, setter);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_time(&a, ms);
    lv_anim_set_playback_time(&a, pb_ms);
    lv_anim_set_path_cb(&a, path);
    lv_anim_start(&a);
}

static void anim_int_repeat(lv_obj_t *o, int32_t from, int32_t to, uint32_t ms, uint32_t pb_ms,
                            uint16_t repeat_cnt, lv_anim_exec_xcb_t setter,
                            lv_anim_path_cb_t path = lv_anim_path_ease_in_out) {
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, setter);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_time(&a, ms);
    lv_anim_set_playback_time(&a, pb_ms);
    lv_anim_set_repeat_count(&a, repeat_cnt);
    lv_anim_set_path_cb(&a, path);
    lv_anim_start(&a);
}

static void set_eye_border_visible(bool on) {
    if (!left_eye) return;
    lv_coord_t bw = on ? cfg.border_width  : 0;
    lv_coord_t ow = on ? cfg.outline_width : 0;
    lv_opa_t   bo = on ? LV_OPA_60 : LV_OPA_TRANSP;
    lv_opa_t   oo = on ? LV_OPA_30 : LV_OPA_TRANSP;
    lv_obj_set_style_border_width(left_eye,  bw, 0);
    lv_obj_set_style_border_width(right_eye, bw, 0);
    lv_obj_set_style_outline_width(left_eye,  ow, 0);
    lv_obj_set_style_outline_width(right_eye, ow, 0);
    lv_obj_set_style_border_opa(left_eye,  bo, 0);
    lv_obj_set_style_border_opa(right_eye, bo, 0);
    lv_obj_set_style_outline_opa(left_eye,  oo, 0);
    lv_obj_set_style_outline_opa(right_eye, oo, 0);
}
// ============================================================
// Cirkadiansky rytmus
// ============================================================
static bool is_night_time(void) {
    time_t now = time(NULL);
    if (now < 1700000000) return false;
    struct tm tm; localtime_r(&now, &tm);
    if (tm.tm_year < 120) return false;
    uint32_t h = tm.tm_hour;
    if (cfg.night_start_hour <= cfg.night_end_hour) {
        return h >= cfg.night_start_hour && h < cfg.night_end_hour;
    }
    return (h >= cfg.night_start_hour) || (h < cfg.night_end_hour);
}
// ============================================================
// Masky
// ============================================================
static void ensure_masks(void) {
    if (!s_root) return;
    auto make_mask = [](lv_obj_t *par) {
        lv_obj_t *m = lv_obj_create(par);
        lv_obj_remove_flag(m, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(m, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(m, lv_color_hex(cfg.color_bg_mask), 0);
        lv_obj_set_style_border_width(m, 0, 0);
        lv_obj_set_style_outline_width(m, 0, 0);
        lv_obj_set_style_radius(m, 0, 0);
        lv_obj_add_flag(m, LV_OBJ_FLAG_HIDDEN);
        return m;
    };
    if (!s_smile_mask_l) s_smile_mask_l = make_mask(s_root);
    if (!s_smile_mask_r) s_smile_mask_r = make_mask(s_root);
    if (!s_frown_mask_l) s_frown_mask_l = make_mask(s_root);
    if (!s_frown_mask_r) s_frown_mask_r = make_mask(s_root);
}

static void position_masks(void) {
    if (!left_eye || !right_eye) return;
    if (!s_smile_mask_l) return;
    lv_coord_t lx = lv_obj_get_x(left_eye);
    lv_coord_t ly = lv_obj_get_y(left_eye);
    lv_coord_t rx = lv_obj_get_x(right_eye);
    lv_coord_t ry = lv_obj_get_y(right_eye);
    lv_coord_t w  = s_eye_w_l;
    lv_coord_t h  = s_eye_h_l;

    int32_t mw = w + cfg.mask_smile_width_over;
    int32_t mh = (int32_t)(h * 0.85f) + cfg.mask_smile_bottom_over;
    int32_t bottom_overshoot = cfg.mask_smile_bottom_over;
    
    lv_obj_set_size(s_smile_mask_l, mw, mh);
    lv_obj_set_pos (s_smile_mask_l, lx - 20, ly + (h - mh) + bottom_overshoot);
    lv_obj_set_style_radius(s_smile_mask_l, mw / 2, 0);
    lv_obj_set_size(s_smile_mask_r, mw, mh);
    lv_obj_set_pos (s_smile_mask_r, rx - 20, ry + (h - mh) + bottom_overshoot);
    lv_obj_set_style_radius(s_smile_mask_r, mw / 2, 0);
    
    int32_t fh = (int32_t)(h * 0.72f);
    int32_t fw = w + 16;
    lv_obj_set_size(s_frown_mask_l, fw, fh);
    lv_obj_set_pos (s_frown_mask_l, lx - 8, ly - 2);
    lv_obj_set_style_radius(s_frown_mask_l, fw / 2, 0);
    lv_obj_set_size(s_frown_mask_r, fw, fh);
    lv_obj_set_pos (s_frown_mask_r, rx - 8, ry - 2);
    lv_obj_set_style_radius(s_frown_mask_r, fw / 2, 0);
}

static void stop_smile_tracker(void) {
    if (s_smile_track_timer) {
        lv_timer_delete(s_smile_track_timer);
        s_smile_track_timer = NULL;
    }
}

static void hide_all_masks(void) {
    stop_smile_tracker();
    if (s_smile_mask_l) lv_obj_add_flag(s_smile_mask_l, LV_OBJ_FLAG_HIDDEN);
    if (s_smile_mask_r) lv_obj_add_flag(s_smile_mask_r, LV_OBJ_FLAG_HIDDEN);
    if (s_frown_mask_l) lv_obj_add_flag(s_frown_mask_l, LV_OBJ_FLAG_HIDDEN);
    if (s_frown_mask_r) lv_obj_add_flag(s_frown_mask_r, LV_OBJ_FLAG_HIDDEN);
}

static void smile_track_cb(lv_timer_t *t) { (void)t; position_masks(); }

static void hide_deco(void) {
    if (s_deco_l) lv_obj_add_flag(s_deco_l, LV_OBJ_FLAG_HIDDEN);
    if (s_deco_r) lv_obj_add_flag(s_deco_r, LV_OBJ_FLAG_HIDDEN);
}

static void set_eye_color(uint32_t hex) {
    if (!left_eye) return;
    lv_color_t c = lv_color_hex(hex);
    lv_obj_set_style_bg_color(left_eye,  c, 0);
    lv_obj_set_style_bg_color(right_eye, c, 0);
    lv_obj_set_style_border_color(left_eye,  c, 0);
    lv_obj_set_style_border_color(right_eye, c, 0);
    lv_obj_set_style_outline_color(left_eye,  c, 0);
    lv_obj_set_style_outline_color(right_eye, c, 0);
}
// ============================================================
// Kompletni pre-effect uklid (spousti kazdy shape hned na zacatku)
// ============================================================
static void prepare_new_shape(void) {
    if (!left_eye) return;
    stop_rolling();
    destroy_triangles();
    hide_all_masks();
    hide_deco();
    if (left_eye)  lv_obj_remove_flag(left_eye,  LV_OBJ_FLAG_HIDDEN);
    if (right_eye) lv_obj_remove_flag(right_eye, LV_OBJ_FLAG_HIDDEN);
    kill_shape_anims(left_eye);
    kill_shape_anims(right_eye);
    s_shape_is_state = false;
}
// ============================================================
// Tvarove zmeny
// ============================================================
static void reset_shape(uint32_t ms) {
    if (!left_eye) return;
    prepare_new_shape();
    set_eye_color(cfg.color_eye);
    set_eye_border_visible(true);
    anim_int(left_eye,  s_eye_h_l, cfg.eye_height, ms, set_h_left_cb,  anim_path_overshoot);
    anim_int(right_eye, s_eye_h_r, cfg.eye_height, ms, set_h_right_cb, anim_path_overshoot);
    anim_int(left_eye,  s_eye_w_l, cfg.eye_width,  ms, set_w_left_cb,  anim_path_overshoot);
    anim_int(right_eye, s_eye_w_r, cfg.eye_width,  ms, set_w_right_cb, anim_path_overshoot);
    anim_int(left_eye,  lv_obj_get_style_radius(left_eye,  0), cfg.eye_radius_max, ms, set_radius_cb);
    anim_int(right_eye, lv_obj_get_style_radius(right_eye, 0), cfg.eye_radius_max, ms, set_radius_cb);
}

// ============================================================
// Deep sleep
// ============================================================
static void start_sleeping(void) {
    if (!left_eye) return;
    s_sleeping = true;
    s_closed   = true;
    prepare_new_shape();
    set_eye_color(cfg.color_eye);
    ESP_LOGI(TAG, "[JTAGDBG] eyes SLEEP (night)");
    anim_int(left_eye,  s_eye_h_l, cfg.eye_height_closed, cfg.anim_sleep_h_ms, set_h_left_cb);
    anim_int(right_eye, s_eye_h_r, cfg.eye_height_closed, cfg.anim_sleep_h_ms, set_h_right_cb);
    anim_int(left_eye,  s_off_x, 0, cfg.anim_sleep_pos_ms, set_x_left_cb);
    anim_int(right_eye, s_off_x, 0, cfg.anim_sleep_pos_ms, set_x_right_cb);
    anim_int(left_eye,  s_off_y, 0, cfg.anim_sleep_pos_ms, set_y_cb);
    anim_int(right_eye, s_off_y, 0, cfg.anim_sleep_pos_ms, set_y_cb);
}

static void sleep_retry_cb(lv_timer_t *t) {
    (void)t;
    s_sleep_retry_timer = NULL;
    if (s_effect_locked) return;
    if (!s_root || !left_eye) return;
    if (is_night_time()) start_sleeping();
}

static void schedule_sleep_retry(uint32_t ms) {
    if (s_sleep_retry_timer) {
        lv_timer_delete(s_sleep_retry_timer);
        s_sleep_retry_timer = NULL;
    }
    s_sleep_retry_timer = lv_timer_create(sleep_retry_cb, ms, NULL);
    lv_timer_set_repeat_count(s_sleep_retry_timer, 1);
}
// ============================================================
// Timer callbacks
// ============================================================
static void settle_cb(lv_timer_t *t) {
    if (s_effect_locked) return; // Desk buddy: voice owns the eyes.
    (void)t;
    if (!left_eye) return;
    if (s_shape_is_state) return;
    reset_shape(cfg.anim_settle_ms);
}


// ============================================================
// Blink
// ============================================================
static void blink_eyes(void) {
    if (!left_eye || s_closed) return;
    anim_int_pb(left_eye,  s_eye_h_l, cfg.eye_height_closed, cfg.anim_blink_close_ms, cfg.anim_blink_open_ms, set_h_left_cb,  anim_path_overshoot);
    anim_int_pb(right_eye, s_eye_h_r, cfg.eye_height_closed, cfg.anim_blink_close_ms, cfg.anim_blink_open_ms, set_h_right_cb, anim_path_overshoot);
}

// Naplánuje jedno mrknutí (obe oci) v čase `delay_ms` od teď.
static void schedule_blink(uint32_t delay_ms) {
    lv_timer_t *t = lv_timer_create([](lv_timer_t *tt){
        if (s_effect_locked) return; // Desk buddy: voice owns the eyes.
        (void)tt;
        if (left_eye && !s_closed) blink_eyes();
    }, delay_ms == 0 ? 1 : delay_ms, NULL);
    lv_timer_set_repeat_count(t, 1);
}

// Wrapper – realistický vzor mrkání "mrk – pauza – mrk mrk – pauza – mrk".
// Blikání není sbíječka: mezi skupinami mrknutí je vždy delší pauza.
// Vejde se do hold_ms; pokud času málo, zredukuje sekvenci.
static void blink_effect_wrap(uint32_t hold_ms) {
    if (!left_eye || s_closed) return;
    // Jedno mrknutí = close + open = anim_blink_close_ms + anim_blink_open_ms.
    uint32_t one_blink = cfg.anim_blink_close_ms + cfg.anim_blink_open_ms;
    uint32_t gap_short = 120;                    // rychlé "mrk mrk"
    uint32_t gap_long  = 500;                    // dýchací pauza mezi skupinami
    uint32_t t = 0;
    // 1) mrk
    blink_eyes();
    t += one_blink + gap_long;
    if (t + one_blink <= hold_ms) {
        // 2) mrk mrk
        schedule_blink(t);
        t += one_blink + gap_short;
        if (t + one_blink <= hold_ms) {
            schedule_blink(t);
            t += one_blink + gap_long;
            if (t + one_blink <= hold_ms) {
                // 3) mrk (zakonceni)
                schedule_blink(t);
                t += one_blink;
            }
        }
    }
    s_reaction_until_ms = lv_tick_get() + hold_ms;
}

// Wrapper – jedno zdvižení obočí (raise+lower), pak klid do konce hold_ms.
static void eyebrow_effect_wrap(uint32_t hold_ms) {
    if (!left_eye || s_closed) return;
    bool left_up = (rand() & 1) != 0;
    int32_t big   = cfg.eye_height + cfg.eyebrow_h_offset;
    int32_t small = cfg.eye_height - cfg.eyebrow_h_offset;
    anim_int_pb(left_up ? left_eye  : right_eye, cfg.eye_height, big,
                cfg.anim_eyebrow_raise_ms, cfg.anim_eyebrow_lower_ms,
                left_up ? set_h_left_cb : set_h_right_cb);
    anim_int_pb(left_up ? right_eye : left_eye,  cfg.eye_height, small,
                cfg.anim_eyebrow_raise_ms, cfg.anim_eyebrow_lower_ms,
                left_up ? set_h_right_cb : set_h_left_cb);
    s_reaction_until_ms = lv_tick_get() + hold_ms;
}

// ============================================================
// Look
// ============================================================
static void look_to(int32_t nx, int32_t ny) {
    if (!left_eye) return;
    if (nx >  cfg.look_max_x) nx =  cfg.look_max_x;
    if (nx < -cfg.look_max_x) nx = -cfg.look_max_x;
    if (ny >  cfg.look_max_y) ny =  cfg.look_max_y;
    if (ny < -cfg.look_max_y) ny = -cfg.look_max_y;
    lv_anim_delete(left_eye,  set_x_left_cb);
    lv_anim_delete(right_eye, set_x_right_cb);
    lv_anim_delete(left_eye,  set_y_cb);
    lv_anim_delete(right_eye, set_y_cb);
    anim_int(left_eye,  s_off_x, nx, cfg.anim_look_ms, set_x_left_cb,  anim_path_overshoot);
    anim_int(right_eye, s_off_x, nx, cfg.anim_look_ms, set_x_right_cb, anim_path_overshoot);
    anim_int(left_eye,  s_off_y, ny, cfg.anim_look_ms, set_y_cb,       anim_path_overshoot);
    anim_int(right_eye, s_off_y, ny, cfg.anim_look_ms, set_y_cb,       anim_path_overshoot);
    s_last_look_ms   = lv_tick_get();
    s_look_offcenter = (nx != 0) || (ny != 0);
}

static void look_to_smooth(int32_t nx, int32_t ny, uint32_t ms) {
    if (!left_eye) return;
    if (nx >  cfg.look_max_x) nx =  cfg.look_max_x;
    if (nx < -cfg.look_max_x) nx = -cfg.look_max_x;
    if (ny >  cfg.look_max_y) ny =  cfg.look_max_y;
    if (ny < -cfg.look_max_y) ny = -cfg.look_max_y;
    lv_anim_delete(left_eye,  set_x_left_cb);
    lv_anim_delete(right_eye, set_x_right_cb);
    lv_anim_delete(left_eye,  set_y_cb);
    lv_anim_delete(right_eye, set_y_cb);
    anim_int(left_eye,  s_off_x, nx, ms, set_x_left_cb,  lv_anim_path_ease_out);
    anim_int(right_eye, s_off_x, nx, ms, set_x_right_cb, lv_anim_path_ease_out);
    anim_int(left_eye,  s_off_y, ny, ms, set_y_cb,       lv_anim_path_ease_out);
    anim_int(right_eye, s_off_y, ny, ms, set_y_cb,       lv_anim_path_ease_out);
    s_last_look_ms   = lv_tick_get();
    s_look_offcenter = (nx != 0) || (ny != 0);
}

static void look_to_instant(int32_t nx, int32_t ny) {
    if (!left_eye) return;
    if (nx >  cfg.look_max_x) nx =  cfg.look_max_x;
    if (nx < -cfg.look_max_x) nx = -cfg.look_max_x;
    if (ny >  cfg.look_max_y) ny =  cfg.look_max_y;
    if (ny < -cfg.look_max_y) ny = -cfg.look_max_y;
    lv_anim_delete(left_eye,  set_x_left_cb);
    lv_anim_delete(right_eye, set_x_right_cb);
    lv_anim_delete(left_eye,  set_y_cb);
    lv_anim_delete(right_eye, set_y_cb);
    set_x_left_cb(left_eye,   nx);
    set_x_right_cb(right_eye, nx);
    set_y_cb(left_eye,        ny);
    set_y_cb(right_eye,       ny);
    s_last_look_ms   = lv_tick_get();
    s_look_offcenter = (nx != 0) || (ny != 0);
}

static void look_random(void) {
    int32_t nx = (rand() % (2*cfg.look_max_x)) - cfg.look_max_x;
    int32_t ny = (rand() % (2*cfg.look_max_y)) - cfg.look_max_y;
    look_to(nx, ny);
}

// ============================================================
// HAPPY / SAD
// ============================================================
static void happy_shape(uint32_t hold_ms) {
    if (!left_eye) return;
    prepare_new_shape();
    ensure_masks();
    set_eye_color(cfg.color_eye);
    set_eye_border_visible(false);
    int32_t oval_h = cfg.eye_height + cfg.happy_h_offset;
    int32_t oval_w = cfg.eye_width + cfg.happy_w_offset;
    anim_int(left_eye,  s_eye_h_l, oval_h, cfg.anim_happy_resize_ms, set_h_left_cb,  anim_path_overshoot);
    anim_int(right_eye, s_eye_h_r, oval_h, cfg.anim_happy_resize_ms, set_h_right_cb, anim_path_overshoot);
    anim_int(left_eye,  s_eye_w_l, oval_w, cfg.anim_happy_resize_ms, set_w_left_cb,  anim_path_overshoot);
    anim_int(right_eye, s_eye_w_r, oval_w, cfg.anim_happy_resize_ms, set_w_right_cb, anim_path_overshoot);
    anim_int(left_eye,  lv_obj_get_style_radius(left_eye,  0), oval_w / 2, cfg.anim_happy_resize_ms, set_radius_cb);
    anim_int(right_eye, lv_obj_get_style_radius(right_eye, 0), oval_w / 2, cfg.anim_happy_resize_ms, set_radius_cb);
    position_masks();
    lv_obj_remove_flag(s_smile_mask_l, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_smile_mask_r, LV_OBJ_FLAG_HIDDEN);
    anim_int_repeat(left_eye,  cfg.happy_bounce_y_min, cfg.happy_bounce_y_max, cfg.anim_happy_bounce_ms, cfg.anim_happy_bounce_ms, 4, set_y_cb);
    anim_int_repeat(right_eye, cfg.happy_bounce_y_min, cfg.happy_bounce_y_max, cfg.anim_happy_bounce_ms, cfg.anim_happy_bounce_ms, 4, set_y_cb);
    s_smile_track_timer = lv_timer_create(smile_track_cb, 30, NULL);
    int32_t ticks = (int32_t)(hold_ms / 30) + 20;
    lv_timer_set_repeat_count(s_smile_track_timer, ticks);
    s_reaction_until_ms = lv_tick_get() + hold_ms;
}

static void sad_shape(uint32_t hold_ms) {
    if (!left_eye) return;
    prepare_new_shape();
    ensure_masks();
    anim_int(left_eye,  s_eye_h_l, cfg.eye_height_sad, cfg.anim_sad_resize_ms, set_h_left_cb);
    anim_int(right_eye, s_eye_h_r, cfg.eye_height_sad, cfg.anim_sad_resize_ms, set_h_right_cb);
    anim_int(left_eye,  s_eye_w_l, cfg.eye_width,      cfg.anim_sad_resize_ms, set_w_left_cb);
    anim_int(right_eye, s_eye_w_r, cfg.eye_width,      cfg.anim_sad_resize_ms, set_w_right_cb);
    anim_int(left_eye,  lv_obj_get_style_radius(left_eye,  0), cfg.sad_radius, cfg.anim_sad_resize_ms, set_radius_cb);
    anim_int(right_eye, lv_obj_get_style_radius(right_eye, 0), cfg.sad_radius, cfg.anim_sad_resize_ms, set_radius_cb);
    anim_int(left_eye,  s_off_y, cfg.sad_y_offset, cfg.anim_sad_resize_ms, set_y_cb);
    anim_int(right_eye, s_off_y, cfg.sad_y_offset, cfg.anim_sad_resize_ms, set_y_cb);
    position_masks();
    lv_obj_remove_flag(s_frown_mask_l, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_frown_mask_r, LV_OBJ_FLAG_HIDDEN);
    look_to(cfg.look_max_x, -cfg.look_max_y);
    lv_timer_t *pt = lv_timer_create([](lv_timer_t *t){
        if (s_effect_locked) return; // Desk buddy: voice owns the eyes.
        (void)t; if (left_eye) position_masks(); }, 520, NULL);
    lv_timer_set_repeat_count(pt, 1);
    s_reaction_until_ms = lv_tick_get() + hold_ms;
}

// ============================================================
// Effects
// ============================================================
static void wink_shape(uint32_t hold_ms) {
    if (!left_eye || s_closed) return;
    prepare_new_shape();
    bool wink_left = (rand() & 1) != 0;
    lv_obj_t *w = wink_left ? left_eye : right_eye;
    lv_obj_t *o = wink_left ? right_eye : left_eye;
    lv_anim_exec_xcb_t sw = wink_left ? set_h_left_cb : set_h_right_cb;
    lv_anim_exec_xcb_t so = wink_left ? set_h_right_cb : set_h_left_cb;
    // Opakuj přes celé hold_ms, ať trvání odpovídá konfiguraci v EFFECTS[].
    uint32_t one = cfg.anim_wink_resize_ms * 2 + 150;
    uint16_t reps = (uint16_t)(hold_ms / one);
    if (reps < 1) reps = 1;
    anim_int_repeat(w, cfg.eye_height, cfg.eye_height_closed,
                    cfg.anim_wink_resize_ms, cfg.anim_wink_resize_ms, reps,
                    sw, anim_path_overshoot);
    anim_int_repeat(o, cfg.eye_height, cfg.eye_height + cfg.wink_other_h_offset,
                    cfg.anim_wink_resize_ms, cfg.anim_wink_resize_ms, reps,
                    so, anim_path_overshoot);
    anim_int_repeat(o, 0, cfg.wink_other_y_offset,
                    cfg.anim_wink_resize_ms, cfg.anim_wink_resize_ms, reps,
                    set_y_cb);
    s_reaction_until_ms = lv_tick_get() + hold_ms;
}

static void surprise_shape(uint32_t hold_ms) {
    if (!left_eye) return;
    prepare_new_shape();
    anim_int(left_eye,  s_eye_h_l, cfg.eye_height_surprise, cfg.anim_surprise_resize_ms, set_h_left_cb,  anim_path_overshoot);
    anim_int(right_eye, s_eye_h_r, cfg.eye_height_surprise, cfg.anim_surprise_resize_ms, set_h_right_cb, anim_path_overshoot);
    anim_int(left_eye,  s_eye_w_l, cfg.eye_width_surprise,  cfg.anim_surprise_resize_ms, set_w_left_cb,  anim_path_overshoot);
    anim_int(right_eye, s_eye_w_r, cfg.eye_width_surprise,  cfg.anim_surprise_resize_ms, set_w_right_cb, anim_path_overshoot);
    anim_int(left_eye,  lv_obj_get_style_radius(left_eye,  0), cfg.eye_width_surprise/2, cfg.anim_surprise_resize_ms, set_radius_cb);
    anim_int(right_eye, lv_obj_get_style_radius(right_eye, 0), cfg.eye_width_surprise/2, cfg.anim_surprise_resize_ms, set_radius_cb);
    anim_int_repeat(left_eye,  -cfg.surprise_bounce_y, cfg.surprise_bounce_y, cfg.anim_surprise_bounce_ms, cfg.anim_surprise_bounce_ms, 8, set_y_cb);
    anim_int_repeat(right_eye, -cfg.surprise_bounce_y, cfg.surprise_bounce_y, cfg.anim_surprise_bounce_ms, cfg.anim_surprise_bounce_ms, 8, set_y_cb);
    s_reaction_until_ms = lv_tick_get() + hold_ms;
}

static void sleepy_shape(uint32_t hold_ms) {
    if (!left_eye) return;
    prepare_new_shape();
    anim_int(left_eye,  s_eye_h_l, cfg.sleepy_h, cfg.anim_sleepy_resize_ms, set_h_left_cb);
    anim_int(right_eye, s_eye_h_r, cfg.sleepy_h, cfg.anim_sleepy_resize_ms, set_h_right_cb);
    anim_int(left_eye,  s_eye_w_l, cfg.eye_width + cfg.sleepy_w_offset, cfg.anim_sleepy_resize_ms, set_w_left_cb);
    anim_int(right_eye, s_eye_w_r, cfg.eye_width + cfg.sleepy_w_offset, cfg.anim_sleepy_resize_ms, set_w_right_cb);
    anim_int(left_eye,  lv_obj_get_style_radius(left_eye,  0), cfg.sleepy_radius, cfg.anim_sleepy_resize_ms, set_radius_cb);
    anim_int(right_eye, lv_obj_get_style_radius(right_eye, 0), cfg.sleepy_radius, cfg.anim_sleepy_resize_ms, set_radius_cb);
    uint16_t cycles = (uint16_t)((hold_ms / (cfg.anim_sleepy_bounce_ms * 2)) * 2);
    if (cycles < 2) cycles = 2;
    anim_int_repeat(left_eye,  -cfg.sleepy_bounce_y, cfg.sleepy_bounce_y, cfg.anim_sleepy_bounce_ms, cfg.anim_sleepy_bounce_ms, cycles, set_y_cb);
    anim_int_repeat(right_eye, -cfg.sleepy_bounce_y, cfg.sleepy_bounce_y, cfg.anim_sleepy_bounce_ms, cfg.anim_sleepy_bounce_ms, cycles, set_y_cb);
    s_reaction_until_ms = lv_tick_get() + hold_ms;
}

static void suspicious_shape(uint32_t hold_ms) {
    if (!left_eye) return;
    prepare_new_shape();
    set_eye_color(cfg.color_warn);
    anim_int(left_eye,  s_eye_h_l, cfg.suspicious_h, cfg.anim_suspicious_resize_ms, set_h_left_cb);
    anim_int(right_eye, s_eye_h_r, cfg.suspicious_h, cfg.anim_suspicious_resize_ms, set_h_right_cb);
    anim_int(left_eye,  s_eye_w_l, cfg.eye_width + cfg.suspicious_w_offset, cfg.anim_suspicious_resize_ms, set_w_left_cb);
    anim_int(right_eye, s_eye_w_r, cfg.eye_width + cfg.suspicious_w_offset, cfg.anim_suspicious_resize_ms, set_w_right_cb);
    anim_int(left_eye,  lv_obj_get_style_radius(left_eye,  0), cfg.suspicious_radius, cfg.anim_suspicious_resize_ms, set_radius_cb);
    anim_int(right_eye, lv_obj_get_style_radius(right_eye, 0), cfg.suspicious_radius, cfg.anim_suspicious_resize_ms, set_radius_cb);
    lv_timer_t *tt = lv_timer_create([](lv_timer_t *t){
        if (s_effect_locked) return; // Desk buddy: voice owns the eyes.
        (void)t;
        if (!left_eye) return;
        static int step = 0;
        int32_t x = (step % 2 == 0) ? -cfg.look_max_x : cfg.look_max_x;
        look_to(x, 0);
        step++;
    }, cfg.anim_suspicious_scan_ms, NULL);
    lv_timer_set_repeat_count(tt, 4);
    s_reaction_until_ms = lv_tick_get() + hold_ms;
}

static void love_shape(uint32_t hold_ms) {
    if (!left_eye) return;
    prepare_new_shape();
    set_eye_color(cfg.color_love);
    anim_int(left_eye,  s_eye_h_l, cfg.love_size, cfg.anim_love_resize_ms, set_h_left_cb,  anim_path_overshoot);
    anim_int(right_eye, s_eye_h_r, cfg.love_size, cfg.anim_love_resize_ms, set_h_right_cb, anim_path_overshoot);
    anim_int(left_eye,  s_eye_w_l, cfg.love_size, cfg.anim_love_resize_ms, set_w_left_cb,  anim_path_overshoot);
    anim_int(right_eye, s_eye_w_r, cfg.love_size, cfg.anim_love_resize_ms, set_w_right_cb, anim_path_overshoot);
    anim_int(left_eye,  lv_obj_get_style_radius(left_eye,  0), cfg.love_radius, cfg.anim_love_resize_ms, set_radius_cb);
    anim_int(right_eye, lv_obj_get_style_radius(right_eye, 0), cfg.love_radius, cfg.anim_love_resize_ms, set_radius_cb);
    uint16_t cycles = (uint16_t)(hold_ms / (cfg.anim_love_beat_ms * 2));
    if (cycles < 3) cycles = 3;
    anim_int_repeat(left_eye,  cfg.love_size, cfg.love_beat_size, cfg.anim_love_beat_ms, cfg.anim_love_beat_ms, cycles, set_w_left_cb);
    anim_int_repeat(right_eye, cfg.love_size, cfg.love_beat_size, cfg.anim_love_beat_ms, cfg.anim_love_beat_ms, cycles, set_w_right_cb);
    anim_int_repeat(left_eye,  cfg.love_size, cfg.love_beat_size, cfg.anim_love_beat_ms, cfg.anim_love_beat_ms, cycles, set_h_left_cb);
    anim_int_repeat(right_eye, cfg.love_size, cfg.love_beat_size, cfg.anim_love_beat_ms, cfg.anim_love_beat_ms, cycles, set_h_right_cb);
    s_reaction_until_ms = lv_tick_get() + hold_ms;
}

static void dizzy_shape(uint32_t hold_ms) {
    if (!left_eye) return;
    prepare_new_shape();
    anim_int(left_eye,  s_eye_h_l, cfg.dizzy_size, cfg.anim_dizzy_resize_ms, set_h_left_cb);
    anim_int(right_eye, s_eye_h_r, cfg.dizzy_size, cfg.anim_dizzy_resize_ms, set_h_right_cb);
    anim_int(left_eye,  s_eye_w_l, cfg.dizzy_size, cfg.anim_dizzy_resize_ms, set_w_left_cb);
    anim_int(right_eye, s_eye_w_r, cfg.dizzy_size, cfg.anim_dizzy_resize_ms, set_w_right_cb);
    uint16_t cx = (uint16_t)(hold_ms / cfg.anim_dizzy_x_ms);
    uint16_t cy = (uint16_t)(hold_ms / cfg.anim_dizzy_y_ms);
    if (cx < 6) cx = 6;
    if (cy < 6) cy = 6;
    anim_int_repeat(left_eye,  -cfg.dizzy_bounce_x,  cfg.dizzy_bounce_x, cfg.anim_dizzy_x_ms, cfg.anim_dizzy_x_ms, cx, set_x_left_cb,  lv_anim_path_ease_in_out);
    anim_int_repeat(right_eye,  cfg.dizzy_bounce_x, -cfg.dizzy_bounce_x, cfg.anim_dizzy_y_ms, cfg.anim_dizzy_y_ms, cx, set_x_right_cb, lv_anim_path_ease_in_out);
    anim_int_repeat(left_eye,  -cfg.dizzy_bounce_y,  cfg.dizzy_bounce_y, cfg.anim_dizzy_y_ms, cfg.anim_dizzy_y_ms, cy, set_y_cb,       lv_anim_path_ease_in_out);
    anim_int_repeat(right_eye,  cfg.dizzy_bounce_y, -cfg.dizzy_bounce_y, cfg.anim_dizzy_x_ms, cfg.anim_dizzy_x_ms, cy, set_y_cb,       lv_anim_path_ease_in_out);
    s_reaction_until_ms = lv_tick_get() + hold_ms;
}

static void scan_shape(uint32_t hold_ms) {
    if (!left_eye) return;
    prepare_new_shape();
    set_eye_color(cfg.color_warn);
    anim_int(left_eye,  s_eye_h_l, cfg.scan_h, cfg.anim_scan_resize_ms, set_h_left_cb);
    anim_int(right_eye, s_eye_h_r, cfg.scan_h, cfg.anim_scan_resize_ms, set_h_right_cb);
    anim_int(left_eye,  s_eye_w_l, cfg.eye_width + cfg.scan_w_offset, cfg.anim_scan_resize_ms, set_w_left_cb);
    anim_int(right_eye, s_eye_w_r, cfg.eye_width + cfg.scan_w_offset, cfg.anim_scan_resize_ms, set_w_right_cb);
    anim_int(left_eye,  lv_obj_get_style_radius(left_eye,  0), cfg.scan_radius, cfg.anim_scan_resize_ms, set_radius_cb);
    anim_int(right_eye, lv_obj_get_style_radius(right_eye, 0), cfg.scan_radius, cfg.anim_scan_resize_ms, set_radius_cb);
    uint16_t cycles = (uint16_t)(hold_ms / cfg.anim_scan_move_ms);
    if (cycles < 2) cycles = 2;
    anim_int_repeat(left_eye,  -cfg.look_max_x, cfg.look_max_x, cfg.anim_scan_move_ms, cfg.anim_scan_move_ms, cycles, set_x_left_cb, lv_anim_path_linear);
    anim_int_repeat(right_eye, -cfg.look_max_x, cfg.look_max_x, cfg.anim_scan_move_ms, cfg.anim_scan_move_ms, cycles, set_x_right_cb, lv_anim_path_linear);
    s_reaction_until_ms = lv_tick_get() + hold_ms;
}

// Rychle tekani + rychle prolikavani barev
static void glitch_shape(uint32_t hold_ms) {
    if (!left_eye) return;
    prepare_new_shape();
    uint16_t cycles = (uint16_t)(hold_ms / cfg.anim_glitch_x_ms);
    if (cycles < 10) cycles = 10;
    anim_int_repeat(left_eye,  -cfg.glitch_x, cfg.glitch_x, cfg.anim_glitch_x_ms, cfg.anim_glitch_x_ms, cycles, set_x_left_cb, lv_anim_path_linear);
    anim_int_repeat(right_eye,  cfg.glitch_x, -cfg.glitch_x, cfg.anim_glitch_x_ms, cfg.anim_glitch_x_ms, cycles, set_x_right_cb, lv_anim_path_linear);
    anim_int_repeat(left_eye,   -cfg.glitch_y,  cfg.glitch_y, cfg.anim_glitch_y_ms, cfg.anim_glitch_y_ms, cycles/2, set_y_cb, lv_anim_path_linear);
    anim_int_repeat(right_eye,   cfg.glitch_y, -cfg.glitch_y, cfg.anim_glitch_y_ms, cfg.anim_glitch_y_ms, cycles/2, set_y_cb, lv_anim_path_linear);
    anim_int_repeat(left_eye,  cfg.eye_height, cfg.eye_height-cfg.glitch_h_offset, cfg.anim_glitch_h_ms, cfg.anim_glitch_h_ms, cycles/3, set_h_left_cb, lv_anim_path_step);
    anim_int_repeat(right_eye, cfg.eye_height, cfg.eye_height-cfg.glitch_h_offset, cfg.anim_glitch_h_ms, cfg.anim_glitch_h_ms, cycles/3, set_h_right_cb, lv_anim_path_step);
    anim_int_repeat(left_eye,  0, 359, cfg.anim_glitch_color_ms, 0, (uint16_t)(hold_ms/cfg.anim_glitch_color_ms), set_bg_hue_cb, lv_anim_path_linear);
    anim_int_repeat(right_eye, 0, 359, cfg.anim_glitch_color_ms, 0, (uint16_t)(hold_ms/cfg.anim_glitch_color_ms), set_bg_hue_cb, lv_anim_path_linear);
    s_reaction_until_ms = lv_tick_get() + hold_ms;
}

// Pomala kopie glitchu - tekani i barvy pomaleji
static void slow_glitch_shape(uint32_t hold_ms) {
    if (!left_eye) return;
    prepare_new_shape();
    uint16_t cycles = (uint16_t)(hold_ms / cfg.anim_slow_glitch_x_ms);
    if (cycles < 4) cycles = 4;
    anim_int_repeat(left_eye,  -cfg.slow_glitch_x, cfg.slow_glitch_x, cfg.anim_slow_glitch_x_ms, cfg.anim_slow_glitch_x_ms, cycles, set_x_left_cb, lv_anim_path_ease_in_out);
    anim_int_repeat(right_eye,  cfg.slow_glitch_x, -cfg.slow_glitch_x, cfg.anim_slow_glitch_x_ms, cfg.anim_slow_glitch_x_ms, cycles, set_x_right_cb, lv_anim_path_ease_in_out);
    anim_int_repeat(left_eye,  -cfg.slow_glitch_y, cfg.slow_glitch_y, cfg.anim_slow_glitch_y_ms, cfg.anim_slow_glitch_y_ms, cycles/2, set_y_cb, lv_anim_path_ease_in_out);
    anim_int_repeat(right_eye,  cfg.slow_glitch_y, -cfg.slow_glitch_y, cfg.anim_slow_glitch_y_ms, cfg.anim_slow_glitch_y_ms, cycles/2, set_y_cb, lv_anim_path_ease_in_out);
    // Barvy - pomaly cyklus
    uint16_t hue_cycles = (uint16_t)(hold_ms / cfg.anim_slow_glitch_color_ms);
    if (hue_cycles < 1) hue_cycles = 1;
    anim_int_repeat(left_eye,  0, 359, cfg.anim_slow_glitch_color_ms, 0, hue_cycles, set_bg_hue_cb, lv_anim_path_linear);
    anim_int_repeat(right_eye, 0, 359, cfg.anim_slow_glitch_color_ms, 0, hue_cycles, set_bg_hue_cb, lv_anim_path_linear);
    s_reaction_until_ms = lv_tick_get() + hold_ms;
}

// ============================================================
// NARROW - "zuzeni oci" pri swipe shora dolu
// ============================================================
static void narrow_shape(uint32_t hold_ms) {
    if (!left_eye) return;
    prepare_new_shape();
    set_eye_color(cfg.color_eye);
    anim_int(left_eye,  s_eye_h_l, cfg.narrow_h, cfg.anim_narrow_resize_ms, set_h_left_cb,  anim_path_overshoot);
    anim_int(right_eye, s_eye_h_r, cfg.narrow_h, cfg.anim_narrow_resize_ms, set_h_right_cb, anim_path_overshoot);
    anim_int(left_eye,  s_eye_w_l, cfg.eye_width + cfg.narrow_w_offset, cfg.anim_narrow_resize_ms, set_w_left_cb,  anim_path_overshoot);
    anim_int(right_eye, s_eye_w_r, cfg.eye_width + cfg.narrow_w_offset, cfg.anim_narrow_resize_ms, set_w_right_cb, anim_path_overshoot);
    anim_int(left_eye,  lv_obj_get_style_radius(left_eye,  0), cfg.narrow_radius, cfg.anim_narrow_resize_ms, set_radius_cb);
    anim_int(right_eye, lv_obj_get_style_radius(right_eye, 0), cfg.narrow_radius, cfg.anim_narrow_resize_ms, set_radius_cb);
    uint16_t cycles = (uint16_t)(hold_ms / (cfg.anim_narrow_bounce_ms * 2));
    if (cycles < 2) cycles = 2;
    anim_int_repeat(left_eye,  -cfg.narrow_bounce_y, cfg.narrow_bounce_y, cfg.anim_narrow_bounce_ms, cfg.anim_narrow_bounce_ms, cycles, set_y_cb);
    anim_int_repeat(right_eye, -cfg.narrow_bounce_y, cfg.narrow_bounce_y, cfg.anim_narrow_bounce_ms, cfg.anim_narrow_bounce_ms, cycles, set_y_cb);
    s_reaction_until_ms = lv_tick_get() + hold_ms;
}

// ============================================================
// GLANCE - male rozhlidnuti (L+R nebo jednosmerne)
// ============================================================
static void glance_lr_shape(uint32_t hold_ms) {
    if (!left_eye || s_closed) return;
    prepare_new_shape();
    look_to(-cfg.glance_offset_x, 0);
    lv_timer_t *t1 = lv_timer_create([](lv_timer_t *t){
        if (s_effect_locked) return; // Desk buddy: voice owns the eyes.
        (void)t;
        if (!left_eye) return;
        look_to(cfg.glance_offset_x, 0);
    }, cfg.anim_glance_hold_1_ms, NULL);
    lv_timer_set_repeat_count(t1, 1);
    lv_timer_t *t2 = lv_timer_create([](lv_timer_t *t){
        if (s_effect_locked) return; // Desk buddy: voice owns the eyes.
        (void)t;
        if (!left_eye) return;
        look_to(0, 0);
    }, cfg.anim_glance_hold_2_ms, NULL);
    lv_timer_set_repeat_count(t2, 1);
    s_reaction_until_ms = lv_tick_get() + (hold_ms > 1200 ? hold_ms : 1200);
}

static void glance_right_shape(uint32_t hold_ms) {
    if (!left_eye || s_closed) return;
    prepare_new_shape();
    look_to(cfg.glance_offset_x, 0);
    lv_timer_t *t = lv_timer_create([](lv_timer_t *tt){
        if (s_effect_locked) return; // Desk buddy: voice owns the eyes.
        (void)tt; if (!left_eye) return; look_to(0, 0);
    }, cfg.anim_glance_single_ms, NULL);
    lv_timer_set_repeat_count(t, 1);
    s_reaction_until_ms = lv_tick_get() + (hold_ms > 900 ? hold_ms : 900);
}

static void glance_left_shape(uint32_t hold_ms) {
    if (!left_eye || s_closed) return;
    prepare_new_shape();
    look_to(-cfg.glance_offset_x, 0);
    lv_timer_t *t = lv_timer_create([](lv_timer_t *tt){
        if (s_effect_locked) return; // Desk buddy: voice owns the eyes.
        (void)tt; if (!left_eye) return; look_to(0, 0);
    }, cfg.anim_glance_single_ms, NULL);
    lv_timer_set_repeat_count(t, 1);
    s_reaction_until_ms = lv_tick_get() + (hold_ms > 900 ? hold_ms : 900);
}

// ============================================================
// CIRCLE - oci se stanou kruhy (STAV - zustava do dalsi zmeny)
// ============================================================
static void circle_shape(uint32_t hold_ms) {
    if (!left_eye) return;
    prepare_new_shape();
    set_eye_color(cfg.color_eye);
    anim_int(left_eye,  s_eye_h_l, cfg.eye_width, cfg.anim_circle_resize_ms, set_h_left_cb,  anim_path_overshoot);
    anim_int(right_eye, s_eye_h_r, cfg.eye_width, cfg.anim_circle_resize_ms, set_h_right_cb, anim_path_overshoot);
    anim_int(left_eye,  s_eye_w_l, cfg.eye_width, cfg.anim_circle_resize_ms, set_w_left_cb,  anim_path_overshoot);
    anim_int(right_eye, s_eye_w_r, cfg.eye_width, cfg.anim_circle_resize_ms, set_w_right_cb, anim_path_overshoot);
    anim_int(left_eye,  lv_obj_get_style_radius(left_eye,  0), cfg.eye_width/2, cfg.anim_circle_resize_ms, set_radius_cb);
    anim_int(right_eye, lv_obj_get_style_radius(right_eye, 0), cfg.eye_width/2, cfg.anim_circle_resize_ms, set_radius_cb);
    s_shape_is_state = true;
    s_reaction_until_ms = lv_tick_get() + hold_ms;
}

// ============================================================
// ROLLING CIRCLES
// ============================================================
static void rolling_restore_layout(void) {
    if (left_eye) {
        lv_obj_set_align(left_eye,  LV_ALIGN_CENTER);
        lv_obj_set_pos(left_eye,  -cfg.eye_spacing, 0);
        s_off_x = 0; s_off_y = 0;
    }
    if (right_eye) {
        lv_obj_set_align(right_eye, LV_ALIGN_CENTER);
        lv_obj_set_pos(right_eye,  cfg.eye_spacing, 0);
    }
}

static void stop_rolling(void) {
    if (s_rolling_timer) {
        lv_timer_delete(s_rolling_timer);
        s_rolling_timer = NULL;
        rolling_restore_layout();
    }
}

static void rolling_tick_cb(lv_timer_t *t) {
    (void)t;
    if (!left_eye || !right_eye || !s_root) { stop_rolling(); return; }

    lv_coord_t W = lv_obj_get_width(s_root);  if (W <= 0) W = cfg.screen_default_dim;
    lv_coord_t H = lv_obj_get_height(s_root); if (H <= 0) H = cfg.screen_default_dim;

    lv_coord_t cx = W / 2;
    lv_coord_t cy = H / 2;
    float R_full = (float)((W < H ? W : H) / 2) - (float)(cfg.eye_width / 2) - 8.0f;
    if (R_full < 40.0f) R_full = 40.0f;

    uint32_t now = lv_tick_get();
    uint32_t elapsed = now - s_rolling_start_ms;
    if (elapsed >= s_rolling_duration_ms) {
        stop_rolling();
        reset_shape(cfg.anim_settle_ms);
        return;
    }

    float progress = (float)elapsed / (float)s_rolling_duration_ms;
    float start_a = 3.14159265f / 2.0f;
    float total_a = -2.0f * (2.0f * 3.14159265f);
    float a = start_a + total_a * progress;

    float R = R_full;
    if (elapsed < cfg.anim_rolling_intro_ms) {
        float k = (float)elapsed / (float)cfg.anim_rolling_intro_ms;
        float inv = 1.0f - k;
        float e = 1.0f - inv * inv * inv;
        R = R_full * e;
    }

    float a_r = a;
    float a_l = a + 3.14159265f;

    lv_coord_t rx = (lv_coord_t)(cx + R * cosf(a_r) - cfg.eye_width / 2);
    lv_coord_t ry = (lv_coord_t)(cy + R * sinf(a_r) - cfg.eye_width / 2);
    lv_coord_t lx = (lv_coord_t)(cx + R * cosf(a_l) - cfg.eye_width / 2);
    lv_coord_t ly = (lv_coord_t)(cy + R * sinf(a_l) - cfg.eye_width / 2);

    lv_obj_set_align(left_eye,  LV_ALIGN_TOP_LEFT);
    lv_obj_set_align(right_eye, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(left_eye,  lx, ly);
    lv_obj_set_pos(right_eye, rx, ry);
}

static void rolling_circles_shape(uint32_t hold_ms) {
    if (!left_eye || !s_root) return;
    prepare_new_shape();
    set_eye_color(cfg.color_eye);

    set_w_left_cb(left_eye,   cfg.eye_width);
    set_w_right_cb(right_eye, cfg.eye_width);
    set_h_left_cb(left_eye,   cfg.eye_width);
    set_h_right_cb(right_eye, cfg.eye_width);
    lv_obj_set_style_radius(left_eye,  cfg.eye_width/2, 0);
    lv_obj_set_style_radius(right_eye, cfg.eye_width/2, 0);

    if (hold_ms < 3000) hold_ms = 3000;
    s_rolling_start_ms    = lv_tick_get();
    s_rolling_duration_ms = hold_ms;

    stop_rolling();
    s_rolling_timer = lv_timer_create(rolling_tick_cb, 20, NULL);

    s_reaction_until_ms = lv_tick_get() + hold_ms + 800;
}

// ============================================================
// TRIANGLE - "pravouhle trojuhelniky" (STAV)
// ============================================================
static void destroy_triangles(void) {
    if (s_tri_l) { lv_obj_delete(s_tri_l); s_tri_l = NULL; }
    if (s_tri_r) { lv_obj_delete(s_tri_r); s_tri_r = NULL; }
    // Buffers come from heap_caps_malloc (PSRAM or internal heap).
    if (s_tri_buf_l) { free(s_tri_buf_l); s_tri_buf_l = NULL; }
    if (s_tri_buf_r) { free(s_tri_buf_r); s_tri_buf_r = NULL; }
    if (left_eye)  lv_obj_remove_flag(left_eye,  LV_OBJ_FLAG_HIDDEN);
    if (right_eye) lv_obj_remove_flag(right_eye, LV_OBJ_FLAG_HIDDEN);
}

static void draw_triangle_on_canvas(lv_obj_t *canvas, bool right_side, uint32_t color_hex) {
    if (!canvas) return;
    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_TRANSP);
    const float w = cfg.tri_canvas_w - 1 - 2 * cfg.tri_inset;
    const float h = cfg.tri_canvas_h - 1 - 2 * cfg.tri_inset;
    if (w <= 0 || h <= 0) return;
    const float diagonal = sqrtf(w*w + h*h);
    // Inset all three edges, then stroke the smaller triangle with round ends.
    // This gives true circular corners, with LVGL's anti-aliasing on every edge.
    const float r = fmaxf(0, fminf(cfg.tri_corner_r, (w+h-diagonal)*0.5f-1));
    const float x = cfg.tri_canvas_w - 1 - cfg.tri_inset - r;
    const float y = cfg.tri_canvas_h - 1 - cfg.tri_inset - r;
    lv_point_precise_t p[3] = {
        {int32_t(lroundf(x)), int32_t(lroundf(cfg.tri_inset + r*(diagonal+h)/w))},
        {int32_t(lroundf(cfg.tri_inset + r*(diagonal+w)/h)), int32_t(lroundf(y))},
        {int32_t(lroundf(x)), int32_t(lroundf(y))}
    };
    if (right_side) for (auto &point : p) point.x = cfg.tri_canvas_w - 1 - point.x;
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    lv_draw_triangle_dsc_t fill;
    lv_draw_triangle_dsc_init(&fill);
    fill.color = lv_color_hex(color_hex);
    for (int i=0; i<3; ++i) fill.p[i] = p[i];
    lv_draw_triangle(&layer, &fill);
    if (r > 0) {
        lv_draw_line_dsc_t edge;
        lv_draw_line_dsc_init(&edge);
        edge.color = fill.color;
        edge.width = int32_t(lroundf(2*r));
        edge.round_start = edge.round_end = true;
        for (int i=0; i<3; ++i) {
            edge.p1 = p[i];edge.p2 = p[(i+1)%3];
            lv_draw_line(&layer, &edge);
        }
    }
    lv_canvas_finish_layer(canvas, &layer);
}

static lv_obj_t *make_triangle_canvas(bool right_side, void **out_buf) {
    if (!s_root) return NULL;
    size_t buf_sz = lv_draw_buf_width_to_stride(cfg.tri_canvas_w, LV_COLOR_FORMAT_ARGB8888) * cfg.tri_canvas_h;
    // Prefer PSRAM; both allocation paths are compatible with free().
    void *buf = heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = heap_caps_malloc(buf_sz, MALLOC_CAP_8BIT);
    if (!buf) {
        ESP_LOGW(TAG, "triangle canvas alloc failed (%u B, free int=%u, spi=%u)",
                 (unsigned)buf_sz,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        return NULL;
    }
    ESP_LOGI(TAG, "triangle canvas alloc OK (%u B) side=%s", (unsigned)buf_sz, right_side?"R":"L");
    *out_buf = buf;
    lv_obj_t *cv = lv_canvas_create(s_root);
    lv_canvas_set_buffer(cv, buf, cfg.tri_canvas_w, cfg.tri_canvas_h, LV_COLOR_FORMAT_ARGB8888);
    lv_obj_remove_flag(cv, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(cv, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_align(cv, LV_ALIGN_CENTER);
    int32_t xc = right_side ? cfg.eye_spacing : -cfg.eye_spacing;
    lv_obj_set_pos(cv, xc, 0); // CENTER alignment already subtracts half the canvas size.
    draw_triangle_on_canvas(cv, right_side, cfg.color_eye);
    return cv;
}

static void triangle_shape(uint32_t hold_ms) {
    if (!left_eye || !s_root) return;
    ESP_LOGI(TAG, "triangle_shape start hold=%ums (canvas %dx%d)",
             (unsigned)hold_ms, (int)cfg.tri_canvas_w, (int)cfg.tri_canvas_h);
    prepare_new_shape();
    s_tri_l = make_triangle_canvas(false, &s_tri_buf_l);
    s_tri_r = make_triangle_canvas(true,  &s_tri_buf_r);
    
    if (s_tri_l && s_tri_r) {
        lv_obj_add_flag(left_eye,  LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(right_eye, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(s_tri_l, LV_OPA_TRANSP, 0);
        anim_int(s_tri_l, LV_OPA_TRANSP, LV_OPA_COVER, cfg.anim_triangle_fade_ms, set_opa_cb); // Opacity must stay within 0..255.
        lv_obj_set_style_opa(s_tri_r, LV_OPA_TRANSP, 0);
        anim_int(s_tri_r, LV_OPA_TRANSP, LV_OPA_COVER, cfg.anim_triangle_fade_ms, set_opa_cb); // Opacity must stay within 0..255.
        s_shape_is_state = true;
        s_reaction_until_ms = lv_tick_get() + hold_ms;
    } else {
        ESP_LOGW(TAG, "triangle_shape: canvas fail -> fallback narrow_shape");
        destroy_triangles();
        narrow_shape(hold_ms);
    }
}

// ============================================================
// ANGRY
// ============================================================
static void angry_shape(uint32_t hold_ms) {
    if (!left_eye) return;
    prepare_new_shape();
    set_eye_color(cfg.color_angry);
    anim_int(left_eye,  s_eye_h_l, cfg.angry_h, cfg.anim_angry_resize_ms, set_h_left_cb,  anim_path_bounce_out);
    anim_int(right_eye, s_eye_h_r, cfg.angry_h, cfg.anim_angry_resize_ms, set_h_right_cb, anim_path_bounce_out);
    anim_int(left_eye,  s_eye_w_l, cfg.eye_width + cfg.angry_w_offset, cfg.anim_angry_resize_ms, set_w_left_cb,  anim_path_overshoot);
    anim_int(right_eye, s_eye_w_r, cfg.eye_width + cfg.angry_w_offset, cfg.anim_angry_resize_ms, set_w_right_cb, anim_path_overshoot);
    anim_int(left_eye,  lv_obj_get_style_radius(left_eye,  0), cfg.angry_radius, cfg.anim_angry_resize_ms, set_radius_cb);
    anim_int(right_eye, lv_obj_get_style_radius(right_eye, 0), cfg.angry_radius, cfg.anim_angry_resize_ms, set_radius_cb);
    anim_int(left_eye,  s_off_x,  cfg.angry_x_offset, cfg.anim_angry_resize_ms, set_x_left_cb,  anim_path_overshoot);
    anim_int(right_eye, s_off_x, -cfg.angry_x_offset, cfg.anim_angry_resize_ms, set_x_right_cb, anim_path_overshoot);
    uint16_t shake = (uint16_t)(hold_ms / cfg.anim_angry_shake_ms);
    if (shake < 8)  shake = 8;
    if (shake > 40) shake = 40;
    anim_int_repeat(left_eye,  -cfg.angry_shake_y, cfg.angry_shake_y, cfg.anim_angry_shake_ms, cfg.anim_angry_shake_ms, shake, set_y_cb, lv_anim_path_linear);
    anim_int_repeat(right_eye,  cfg.angry_shake_y, -cfg.angry_shake_y, cfg.anim_angry_shake_ms, cfg.anim_angry_shake_ms, shake, set_y_cb, lv_anim_path_linear);
    s_reaction_until_ms = lv_tick_get() + hold_ms;
}
// ============================================================
// Verejne API
// ============================================================
extern "C" void robot_eyes_look_center(void) { look_to(0, 0); }
extern "C" void robot_eyes_look_left(void)   { look_to(-cfg.look_max_x, 0); }
extern "C" void robot_eyes_look_right(void)  { look_to( cfg.look_max_x, 0); }

static void point_to_offset(lv_coord_t sx, lv_coord_t sy, int32_t *nx, int32_t *ny) {
    lv_coord_t w = s_root ? lv_obj_get_width(s_root)  : cfg.screen_default_dim;
    lv_coord_t h = s_root ? lv_obj_get_height(s_root) : cfg.screen_default_dim;
    if (w <= 0) w = cfg.screen_default_dim;
    if (h <= 0) h = cfg.screen_default_dim;
    *nx = (int32_t)((sx - w/2) * cfg.look_scale_percent / 100);
    *ny = (int32_t)((sy - h/2) * cfg.look_scale_percent / 100);
}

extern "C" void robot_eyes_look_at_point(lv_coord_t x, lv_coord_t y) {
    int32_t nx, ny;
    point_to_offset(x, y, &nx, &ny);
    look_to(nx, ny);
}

extern "C" void robot_eyes_close(void) {
    if (!left_eye || s_closed) return;
    s_closed = true;
    ESP_LOGI(TAG, "[JTAGDBG] eyes CLOSE");
    anim_int(left_eye,  s_eye_h_l, cfg.eye_height_closed, cfg.anim_eyes_close_ms, set_h_left_cb);
    anim_int(right_eye, s_eye_h_r, cfg.eye_height_closed, cfg.anim_eyes_close_ms, set_h_right_cb);
}

extern "C" void robot_eyes_open(void) {
    if (!left_eye || !s_closed) return;
    s_closed   = false;
    s_sleeping = false;
    ESP_LOGI(TAG, "[JTAGDBG] eyes OPEN");
    anim_int(left_eye,  s_eye_h_l, cfg.eye_height, cfg.anim_eyes_open_ms, set_h_left_cb,  anim_path_overshoot);
    anim_int(right_eye, s_eye_h_r, cfg.eye_height, cfg.anim_eyes_open_ms, set_h_right_cb, anim_path_overshoot);
}
// ============================================================
// Guilt UI
// ============================================================
static void ensure_guilt_q(void) {
    if (!s_root) return;
    if (s_guilt_q) return;
    s_guilt_q = lv_label_create(s_root);
    lv_label_set_text(s_guilt_q, "Q");
    lv_obj_set_style_text_color(s_guilt_q, lv_color_hex(cfg.color_guilt_q), 0);
    lv_obj_set_style_text_font(s_guilt_q, LV_FONT_DEFAULT, 0);
    lv_obj_align(s_guilt_q, LV_ALIGN_TOP_RIGHT, -90, 90);
    lv_obj_add_flag(s_guilt_q, LV_OBJ_FLAG_HIDDEN);
}

extern "C" void robot_eyes_set_guilt(bool g) {
    s_guilt = g;
    if (!s_root) return;
    ensure_guilt_q();
    if (!s_guilt_q) return;
    if (g) lv_obj_remove_flag(s_guilt_q, LV_OBJ_FLAG_HIDDEN);
    else   lv_obj_add_flag   (s_guilt_q, LV_OBJ_FLAG_HIDDEN);
    ESP_LOGI(TAG, "guilt=%d", (int)g);
}
extern "C" bool robot_eyes_is_guilt(void) { return s_guilt; }

// ============================================================
// Daily guilt check
// ============================================================
static int ymd_of(time_t t) {
    struct tm tm; localtime_r(&t, &tm);
    return (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;
}

static void guilt_check_apply_cb(void *ud) {
    (void)ud;
    if (!s_root) return;
    time_t now = time(NULL);
    if (now < 1700000000) { robot_eyes_set_guilt(false); return; }
    { struct tm tm; localtime_r(&now, &tm);
      if (tm.tm_year < 120) { robot_eyes_set_guilt(false); return; } }

    int today_ymd = ymd_of(now);
    int yesterday_ymd = ymd_of(now - 24 * 3600);
    int d_done_today = 0;
    int d_done_yesterday = 0;

    usb_msc_storage_lock();
    FILE *f = fopen(USB_MSC_SYSTEM_DIR "/stats.txt", "r");
    if (f) {
        char line[128];
        while (fgets(line, sizeof(line), f)) {
            if (line[0] == '#') continue;
            int y=0, m=0, d=0, d_done=0;
            if (sscanf(line, "%d-%d-%d | %d", &y, &m, &d, &d_done) >= 4) {
                int this_ymd = y * 10000 + m * 100 + d;
                if (this_ymd == today_ymd)          d_done_today     = d_done;
                else if (this_ymd == yesterday_ymd) d_done_yesterday = d_done;
            }
        }
        fclose(f);
    }
    usb_msc_storage_unlock();

    if (!s_root) return;
    bool is_guilty = (d_done_yesterday == 0 && d_done_today == 0);
    robot_eyes_set_guilt(is_guilty);
    ESP_LOGI(TAG, "Guilt check stats.txt: today=%d (done:%d), yesterday=%d (done:%d) -> guilt=%d",
             today_ymd, d_done_today, yesterday_ymd, d_done_yesterday, (int)is_guilty);
}

extern "C" void robot_eyes_check_daily_guilt(void) {
    lv_async_call(guilt_check_apply_cb, NULL);
}

extern "C" void robot_eyes_notify_quest_completed(int category) {
    if (category == 0) robot_eyes_set_guilt(false);
}

// ============================================================
// Celebrate a show
// ============================================================
//
// Cerna obrazovka je od odstraneni ui_Eyes v LVGL Studiu spravovana lokalne.
// Pouziva se jako podklad pro robot_eyes (a je pripravena k opakovanemu
// pouziti kdykoli je potreba jen "cerne platno").
//
// black_screen_get()     - vytvori (idempotentne) a vrati handle.
// black_screen_destroy() - uvolni z RAM (musi byt aktualne NEaktivni).
extern "C" lv_obj_t *black_screen_get(void);
extern "C" void      black_screen_destroy(void);

static lv_obj_t *s_black_screen = NULL;

extern "C" lv_obj_t *black_screen_get(void) {
    if (s_black_screen) return s_black_screen;
    s_black_screen = lv_obj_create(NULL);
    lv_obj_remove_flag(s_black_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_black_screen, lv_color_hex(0x000000),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_black_screen, 255,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    return s_black_screen;
}

extern "C" void black_screen_destroy(void) {
    if (!s_black_screen) return;
    if (lv_screen_active() == s_black_screen) return;
    lv_obj_del(s_black_screen);
    s_black_screen = NULL;
}

extern "C" void robot_eyes_show(void) {
    lv_obj_t *scr = black_screen_get();
    if (!scr) return;
    if (!left_eye) init_robot_eyes(scr);
    lv_screen_load(scr);
}

extern "C" void robot_eyes_destroy(void) {
    if (!s_black_screen) return;
    if (lv_screen_active() == s_black_screen) return;
    // Ocni objekty patri pod s_black_screen -> smazani screenu je smaze taky.
    // Resetujeme forward-deklarovane pointery aby init_robot_eyes() postavil
    // vse znovu pri nasledujicim robot_eyes_show().
    left_eye = NULL;
    right_eye = NULL;
    s_root = NULL;
    black_screen_destroy();
}

extern "C" void robot_eyes_celebrate(void) {
    robot_eyes_show();
    if (!left_eye) return;
    if (s_closed) robot_eyes_open();
    robot_eyes_set_guilt(false);
    happy_shape(cfg.caress_happy_hold_ms);
    s_mood_happy_until_ms = lv_tick_get() + 15000;
    lv_timer_t *t = lv_timer_create(settle_cb, cfg.caress_happy_hold_ms, NULL);
    lv_timer_set_repeat_count(t, 1);
}


// ============================================================
// Návrat po EFEKTU do aktuálního base stavu
// ============================================================
static bool current_shape_no_brain(void) {
    if (s_current_shape_idx < 0 || s_current_shape_idx >= (int)SHAPE_COUNT) return false;
    return SHAPES[s_current_shape_idx].no_brain;
}

static void apply_base_state(int shape_idx, int color_idx, uint32_t ms, bool from_effect) {
    if (s_effect_locked) return; // Desk buddy: voice owns the eyes.
    if (!left_eye) return;
    if (shape_idx < 0 || shape_idx >= (int)SHAPE_COUNT) shape_idx = SHAPE_OVAL;
    if (color_idx < 0 || color_idx >= COLOR_COUNT) color_idx = 0;
    const shape_def_t *sh = &SHAPES[shape_idx];
    uint32_t hex = COLORS[color_idx].hex;

    // BLANK: nic neaplikuj (necháme aktuální tvar/barvu netknuté).
    if (shape_idx == (int)SHAPE_BLANK) {
        ESP_LOGI(TAG, "[eyes] base -> BLANK (skip)");
        s_effect_active = false;
        s_shape_is_state = true;
        s_reaction_until_ms = 0;
        return;
    }

    // Úklid vizuálních vrstev z předchozího efektu.
    prepare_new_shape();
    cfg.color_eye = hex;
    set_eye_color(hex);
    set_eye_border_visible(true);

    anim_int(left_eye,  s_eye_h_l, sh->h, ms, set_h_left_cb,  anim_path_overshoot);
    anim_int(right_eye, s_eye_h_r, sh->h, ms, set_h_right_cb, anim_path_overshoot);
    anim_int(left_eye,  s_eye_w_l, sh->w, ms, set_w_left_cb,  anim_path_overshoot);
    anim_int(right_eye, s_eye_w_r, sh->w, ms, set_w_right_cb, anim_path_overshoot);
    anim_int(left_eye,  lv_obj_get_style_radius(left_eye,  0), sh->radius, ms, set_radius_cb);
    anim_int(right_eye, lv_obj_get_style_radius(right_eye, 0), sh->radius, ms, set_radius_cb);
    anim_int(left_eye,  s_off_y, 0, ms, set_y_cb);
    anim_int(right_eye, s_off_y, 0, ms, set_y_cb);
    anim_int(left_eye,  s_off_x, 0, ms, set_x_left_cb);
    anim_int(right_eye, s_off_x, 0, ms, set_x_right_cb);

    s_current_shape_idx = shape_idx;
    s_current_color_idx = color_idx;
    if (!from_effect) {
        s_last_state_change_ms = lv_tick_get();
    }
    s_effect_active   = false;
    s_shape_is_state  = true;
    s_reaction_until_ms = 0;

    // Desk buddy: count the quiet interval after the return animation.
    schedule_next_idle_action();
    s_next_action_ms += ms;

    // SLEEP: pomalé "dýchání" - oči se pohybují nahoru/dolů jako když spí.
    // Rozjedeme repeat anim po dokončení tvarové tranzice.
    if (shape_idx == (int)SHAPE_SLEEP) {
        lv_timer_t *bt = lv_timer_create([](lv_timer_t *tt){
        if (s_effect_locked) return; // Desk buddy: voice owns the eyes.
            (void)tt;
            if (!left_eye) return;
            if (s_current_shape_idx != (int)SHAPE_SLEEP) return;
            // Amplituda 8 px, perioda ~2.4 s (nádech + výdech).
            anim_int_repeat(left_eye,  -8, 8, 1200, 1200, LV_ANIM_REPEAT_INFINITE, set_y_cb, lv_anim_path_ease_in_out);
            anim_int_repeat(right_eye, -8, 8, 1200, 1200, LV_ANIM_REPEAT_INFINITE, set_y_cb, lv_anim_path_ease_in_out);
        }, ms + 50, NULL);
        lv_timer_set_repeat_count(bt, 1);
    }

    ESP_LOGI(TAG, "[eyes] base -> shape=%s color=%s", sh->name, COLORS[color_idx].name);
}

static void return_to_base_state(uint32_t ms) {
    apply_base_state(s_current_shape_idx, s_current_color_idx, ms, true);
}


// ============================================================
// Idle "mozek" - vybírá efekty a občas přehazuje base state.
// ============================================================
static void schedule_next_idle_action(void) {
    // Pauza mezi efekty je řízena EFFECT_PAUSE_MIN_MS / EFFECT_PAUSE_MAX_MS (viz nahoře).
    uint32_t lo = EFFECT_PAUSE_MIN_MS, hi = EFFECT_PAUSE_MAX_MS;
    if (hi < lo) hi = lo;
    uint32_t pause = lo + (uint32_t)(rand() % (hi - lo + 1u));
    s_next_action_ms = lv_tick_get() + pause;
}

static void schedule_next_state_pick(void) {
    // Náhodné trvání aktuálního stavu 20-60s (řídí se prvním nalezeným řádkem v STATES).
    uint32_t min_ms = 20000, max_ms = 60000;
    for (int i = 0; i < STATE_COUNT; i++) {
        if ((int)STATES[i].shape == s_current_shape_idx && STATES[i].color == s_current_color_idx) {
            min_ms = STATES[i].min_ms;
            max_ms = STATES[i].max_ms;
            break;
        }
    }
    if (max_ms < min_ms) max_ms = min_ms;
    uint32_t d = min_ms + (uint32_t)(rand() % (max_ms - min_ms + 1u));
    s_state_hold_until_ms = lv_tick_get() + d;
}

static void brain_pick_random_state(void) {
    // Absolutní šance, že brain tentokrát base state neměnní.
    if (BLANK_STATE_PERCENT > 0) {
        int roll = rand() % 100;
        if (roll < BLANK_STATE_PERCENT) {
            ESP_LOGI(TAG, "[eyes] state -> BLANK (keep current)");
            schedule_next_state_pick();
            return;
        }
    }
    int total = 0;
    for (int i = 0; i < STATE_COUNT; i++) if (STATES[i].prob > 0) total += STATES[i].prob;
    if (total <= 0) return;
    int r = rand() % total;
    int acc = 0;
    for (int i = 0; i < STATE_COUNT; i++) {
        if (STATES[i].prob <= 0) continue;
        acc += STATES[i].prob;
        if (r < acc) {
            apply_base_state((int)STATES[i].shape, STATES[i].color, cfg.anim_state_transition_ms, false);
            s_last_state_change_ms = 0; // brain-driven change nezavádí okno pro color cycle
            break;
        }
    }
    schedule_next_state_pick();
}

static int s_previous_random_effect = -1;
static void run_random_effect(void) {
    // 1) ABSOLUTNÍ šance na BLANK (nezávislá na váhách ostatních efektů).
    if (BLANK_EFFECT_PERCENT > 0) {
        int roll = rand() % 100;
        if (roll < BLANK_EFFECT_PERCENT) {
            ESP_LOGI(TAG, "[eyes] effect: BLANK (skip)");
            return;
        }
    }

    // 2) Vážený výběr efektu (prob==0 se přeskočí, včetně BLANK řádku).
    int total = 0;
    for (int i = 0; i < EFFECT_COUNT; i++) if (EFFECTS[i].prob > 0 && i != s_previous_random_effect) total += EFFECTS[i].prob;
    if (total <= 0) return;
    int r = rand() % total;
    int acc = 0;
    const effect_entry_t *ch = NULL;
    for (int i = 0; i < EFFECT_COUNT; i++) {
        if (EFFECTS[i].prob <= 0 || i == s_previous_random_effect) continue;
        acc += EFFECTS[i].prob;
        if (r < acc) { ch = &EFFECTS[i]; s_previous_random_effect = i; break; }
    }
    if (!ch) return;

    // 3) Vypočti hold z konfigurace (min_ms..max_ms) – TOTO je autoritativní délka efektu.
    uint32_t min_ms = ch->min_ms, max_ms = ch->max_ms;
    if (max_ms < min_ms) max_ms = min_ms;
    uint32_t hold = min_ms + (uint32_t)(rand() % (max_ms - min_ms + 1u));

    // Povinná "tail pauza" po každém efektu – aby oči nikdy nešpírtaly efekt
    // za efektem bez oddechu. 600..1500 ms randomizace.
    uint32_t tail_pause = 600 + (uint32_t)(rand() % 901u);
    uint32_t total_window = hold + tail_pause;

    s_effect_active = true;
    // 4) Rezervuj celé okno (efekt + tail pauza), aby brain ani gesta nepřerušily.
    s_reaction_until_ms = lv_tick_get() + total_window;
    ch->fn(hold);
    ESP_LOGI(TAG, "[eyes] effect: %s (%ums + tail %ums)", ch->desc, (unsigned)hold, (unsigned)tail_pause);

    // 5) Návrat do BASE stavu po dokončení efektu (dojezd do středu / defaultu),
    //    následně dobehne tail pauza než se pusti dalsi efekt.
    lv_timer_t *st = lv_timer_create([](lv_timer_t *t){
        if (s_effect_locked) return; // Desk buddy: voice owns the eyes.
        (void)t;
        if (!left_eye) return;
        if (s_finger_down) return;
        return_to_base_state(cfg.anim_settle_ms);
    }, hold + 300, NULL);
    lv_timer_set_repeat_count(st, 1);
}

static void eye_behavior_timer_cb(lv_timer_t *t) {
    if (s_effect_locked) return; // Desk buddy: voice owns the eyes.
    (void)t;
    if (!s_parent || !s_root || lv_screen_active() != s_parent) return;

    if (is_night_time()) {
        if (s_current_shape_idx != SHAPE_SLEEP) {
            apply_base_state(SHAPE_SLEEP, s_current_color_idx, cfg.anim_state_transition_ms, false);
        }
        return;
    }

    if (s_closed) return;
    if (s_finger_down) return;
    if ((lv_tick_get() - s_finger_release_ms) < cfg.finger_interest_ms) return;

    if (s_look_offcenter && !s_effect_active &&
        (lv_tick_get() - s_last_look_ms) >= cfg.look_idle_return_ms) {
        look_to_smooth(0, 0, cfg.anim_look_smooth_ms);
        s_look_offcenter = false;
    }

    // Desk buddy: neither idle effects nor state changes interrupt a rest.
    if (s_effect_active || int32_t(lv_tick_get() - s_next_action_ms) < 0) return;

    // Automatický přechod na jiný base state - brain-pick timer běží i v
    // no_brain stavech (spánek), aby se po nastaveném čase přepnul jinam.
    if (s_state_hold_until_ms != 0 &&
        lv_tick_get() >= s_state_hold_until_ms &&
        !s_effect_active) {
        brain_pick_random_state();
        return;
    }

    if (s_effect_locked) return;
    if (lv_tick_get() < s_reaction_until_ms) return;
    if (lv_tick_get() < s_next_action_ms) return;

    // SPÁNEK: no_brain -> žádné idle efekty
    if (current_shape_no_brain()) return;

    run_random_effect();
    schedule_next_idle_action();
}


// ============================================================
// Trigger reakce (dvojklik >3s, hlazení)
// ============================================================
static void trigger_double_tap_reaction(lv_coord_t px, lv_coord_t py) {
    s_tap_count = 0;
    s_last_release_ms = 0;
    s_double_tap_triggered = true;
    s_reaction_until_ms = 0;
    if (s_closed) robot_eyes_open();
    uint32_t hold = cfg.gesture_reaction_min_ms + (uint32_t)(rand() % cfg.gesture_reaction_rand_ms);
    s_effect_active = true;
    if (rand() & 1) dizzy_shape(hold);
    else            glitch_shape(hold);
    lv_timer_t *st = lv_timer_create([](lv_timer_t *t){
        if (s_effect_locked) return; // Desk buddy: voice owns the eyes.
        (void)t; if (left_eye) return_to_base_state(cfg.anim_settle_ms);
    }, hold + 200, NULL);
    lv_timer_set_repeat_count(st, 1);
    power_manager_eye_touch(px, py);
}

static void trigger_caress_reaction(void) {
    s_caress_triggered = true;
    s_double_tap_triggered = true;
    s_tap_count = 0;
    s_reaction_until_ms = 0;
    if (s_closed) robot_eyes_open();
    uint32_t hold = cfg.caress_happy_hold_ms;
    s_effect_active = true;
    happy_shape(hold);
    s_mood_happy_until_ms = lv_tick_get() + 10000;
    lv_timer_t *st = lv_timer_create([](lv_timer_t *t){
        if (s_effect_locked) return; // Desk buddy: voice owns the eyes.
        (void)t; if (left_eye) return_to_base_state(cfg.anim_settle_ms);
    }, hold + 200, NULL);
    lv_timer_set_repeat_count(st, 1);
}

// Swipe L/R = browse EFEKTŮ (dočasná ukázka, potom návrat do base).
static void browse_show_current(void) {
    if (s_browse_index < 0) s_browse_index = 0;
    if (s_browse_index >= EFFECT_COUNT) s_browse_index = 0;
    while (EFFECTS[s_browse_index].prob == 0) {
        s_browse_index = (s_browse_index + 1) % EFFECT_COUNT;
    }
    s_reaction_until_ms = 0;
    if (s_closed) robot_eyes_open();
    s_effect_active = true;
    EFFECTS[s_browse_index].fn(cfg.browse_hold_ms);
    s_browse_last_ms = lv_tick_get();
    lv_timer_t *st = lv_timer_create([](lv_timer_t *t){
        if (s_effect_locked) return; // Desk buddy: voice owns the eyes.
        (void)t; if (left_eye) return_to_base_state(cfg.anim_settle_ms);
    }, cfg.browse_hold_ms + 200, NULL);
    lv_timer_set_repeat_count(st, 1);
    ESP_LOGI(TAG, "[eyes] browse effect #%d: %s", s_browse_index, EFFECTS[s_browse_index].desc);
}
static void browse_next_effect(void) { s_browse_index = (s_browse_index + 1) % EFFECT_COUNT; browse_show_current(); }
static void browse_prev_effect(void) { s_browse_index = (s_browse_index - 1 + EFFECT_COUNT) % EFFECT_COUNT; browse_show_current(); }

// Swipe UP/DOWN = cyklus STAVŮ (tvarů).
static void state_shape_next(void) {
    int n = s_current_shape_idx + 1;
    if (n >= (int)SHAPE_COUNT) n = 0;
    apply_base_state(n, s_current_color_idx, cfg.anim_state_transition_ms, false);
    s_last_state_change_ms = lv_tick_get();
    schedule_next_state_pick();
}
static void state_shape_prev(void) {
    int n = s_current_shape_idx - 1;
    if (n < 0) n = (int)SHAPE_COUNT - 1;
    apply_base_state(n, s_current_color_idx, cfg.anim_state_transition_ms, false);
    s_last_state_change_ms = lv_tick_get();
    schedule_next_state_pick();
}

// Dvojklik ≤3s od poslední změny stavu/barvy = cyklus barev.
static void state_color_next(void) {
    int n = (s_current_color_idx + 1) % COLOR_COUNT;
    apply_base_state(s_current_shape_idx, n, cfg.anim_state_transition_ms, false);
    s_last_state_change_ms = lv_tick_get();
    schedule_next_state_pick();
}

static void do_browse_swipe(lv_dir_t d);

static void root_press_cb(lv_event_t *e) {
    if (s_effect_locked) return; // Desk buddy: voice owns the eyes.
    (void)e;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p; lv_indev_get_point(indev, &p);
    s_finger_down = true;
    s_double_tap_triggered = false;
    s_caress_triggered = false;
    s_last_pressing_x = p.x;
    s_last_pressing_y = p.y;
    s_press_start_x = p.x;
    s_press_start_y = p.y;
    s_press_start_ms = lv_tick_get();
    s_press_dist_acc = 0;
    s_last_dx_sign = 0;
    s_swing_extreme_x = p.x;
    s_caress_reversals = 0;
    s_caress_window_start_ms = lv_tick_get();

    hide_all_masks();
    hide_deco();
    s_reaction_until_ms = 0;
    if (s_closed) robot_eyes_open();

    if (s_rolling_timer) {
        stop_rolling();
        return_to_base_state(300);
    }

    // Ve spánku (SHAPE_SLEEP) na dotyk zablikaji nastvane "pulnocni" oci -
    // a po ~1.4 s se zase vrati zpet do spanku (breathing).
    if (current_shape_no_brain() && !s_effect_active) {
        int saved_shape = s_current_shape_idx;
        int saved_color = s_current_color_idx;
        angry_shape(1200);
        s_reaction_until_ms = lv_tick_get() + 1400;
        lv_timer_t *rt = lv_timer_create([](lv_timer_t *tt){
        if (s_effect_locked) return; // Desk buddy: voice owns the eyes.
            (void)tt;
            if (!left_eye) return;
            // Zpet do spanku (breathing se automaticky nahodi v apply_base_state).
            apply_base_state((int)SHAPE_SLEEP, s_current_color_idx, cfg.anim_state_transition_ms, false);
        }, 1400, NULL);
        lv_timer_set_repeat_count(rt, 1);
        (void)saved_shape; (void)saved_color;
        power_manager_eye_touch(p.x, p.y);
        return;
    }

    // Ve spánku dotyk pouze probudí pohled - nevykřičí, čeká na swipe (probouzení swipem).
    int32_t nx, ny;
    point_to_offset(p.x, p.y, &nx, &ny);
    look_to_smooth(nx, ny, 150);
    power_manager_eye_touch(p.x, p.y);
}

static void root_pressing_cb(lv_event_t *e) {
    if (s_effect_locked) return; // Desk buddy: voice owns the eyes.
    (void)e;
    if (!s_finger_down) return;

    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p; lv_indev_get_point(indev, &p);

    int32_t dx = (int32_t)p.x - (int32_t)s_last_pressing_x;
    s_last_pressing_x = p.x;
    s_last_pressing_y = p.y;

    uint32_t now_ms = lv_tick_get();
    if ((now_ms - s_caress_window_start_ms) > cfg.caress_window_ms) {
        s_caress_window_start_ms = now_ms;
        s_caress_reversals = 0;
        s_last_dx_sign = 0;
        s_swing_extreme_x = p.x;
    }
    if (dx > 2 || dx < -2) {
        int8_t sgn = (dx > 0) ? +1 : -1;
        if (s_last_dx_sign != 0 && sgn != s_last_dx_sign) {
            int32_t swing = (int32_t)s_last_pressing_x - (int32_t)s_swing_extreme_x;
            if (swing < 0) swing = -swing;
            if (swing >= cfg.caress_min_swing_px) {
                s_caress_reversals++;
                s_swing_extreme_x = s_last_pressing_x;
            }
        }
        s_last_dx_sign = sgn;
    }
    if (!s_caress_triggered &&
        s_caress_reversals >= cfg.caress_min_reversals &&
        (now_ms - s_press_start_ms) >= cfg.caress_min_ms) {
        trigger_caress_reaction();
    }

    hide_all_masks();
    int32_t nx, ny;
    point_to_offset(p.x, p.y, &nx, &ny);
    if (!s_rolling_timer) look_to_smooth(nx, ny, 150);
    power_manager_eye_touch(p.x, p.y);
}

static void root_release_cb(lv_event_t *e) {
    if (s_effect_locked) return; // Desk buddy: voice owns the eyes.
    s_finger_down = false;
    s_finger_release_ms = lv_tick_get();

    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_RELEASED) {
        s_tap_count = 0;
        s_last_release_ms = 0;
        return;
    }
    if (s_double_tap_triggered || s_caress_triggered) return;

    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p; lv_indev_get_point(indev, &p);

    uint32_t now = lv_tick_get();
    uint32_t press_dur = now - s_press_start_ms;
    int32_t  mvx = (int32_t)p.x - (int32_t)s_press_start_x;
    int32_t  mvy = (int32_t)p.y - (int32_t)s_press_start_y;
    int32_t  amvx = mvx < 0 ? -mvx : mvx;
    int32_t  amvy = mvy < 0 ? -mvy : mvy;

    if (press_dur <= cfg.swipe_max_ms) {
        bool horiz = (amvx >= cfg.swipe_min_px) &&
                     (amvx * cfg.swipe_dom_den >= amvy * cfg.swipe_dom_num);
        bool vert  = (amvy >= cfg.swipe_min_px) &&
                     (amvy * cfg.swipe_dom_den >= amvx * cfg.swipe_dom_num);
        if (horiz) {
            lv_dir_t d = (mvx > 0) ? LV_DIR_RIGHT : LV_DIR_LEFT;
            do_browse_swipe(d);
            s_tap_count = 0;
            s_last_release_ms = 0;
            return;
        } else if (vert) {
            // NAHORU = další stav (shape list), DOLŮ = předchozí stav.
            if (mvy < 0) state_shape_next();
            else         state_shape_prev();
            s_tap_count = 0;
            s_last_release_ms = 0;
            return;
        }
    }

    // Detekce dvojkliku
    uint32_t dt = now - s_last_release_ms;
    int32_t dx = (int32_t)p.x - (int32_t)s_last_release_x;
    int32_t dy = (int32_t)p.y - (int32_t)s_last_release_y;
    bool close_enough = (s_last_release_ms != 0) &&
                        (dt >= cfg.double_tap_min_ms) && (dt <= cfg.double_tap_max_ms) &&
                        ((dx*dx + dy*dy) <= (cfg.double_tap_max_px * cfg.double_tap_max_px));
    if (close_enough) {
        s_tap_count++;
        if (s_tap_count >= 2) {
            // Rozhodujeme: cyklus barvy (≤3s od poslední změny stavu/barvy) vs. wacky reakce.
            bool in_color_cycle_window = (s_last_state_change_ms != 0) &&
                ((now - s_last_state_change_ms) <= STATE_COLOR_CYCLE_WINDOW_MS);
            if (in_color_cycle_window) {
                s_tap_count = 0;
                s_last_release_ms = 0;
                s_double_tap_triggered = true;
                state_color_next();
                return;
            }
            trigger_double_tap_reaction(p.x, p.y);
            return;
        }
    } else {
        s_tap_count = 1;
    }
    s_last_release_ms = now;
    s_last_release_x  = p.x;
    s_last_release_y  = p.y;
}

static void do_browse_swipe(lv_dir_t d) {
    if (s_guilt) {
        uint32_t hold = cfg.gesture_reaction_min_ms + (uint32_t)(rand() % cfg.gesture_reaction_rand_ms);
        s_effect_active = true;
        sad_shape(hold);
        lv_timer_t *t = lv_timer_create([](lv_timer_t *tt){
        if (s_effect_locked) return; // Desk buddy: voice owns the eyes.
            (void)tt; if (left_eye) return_to_base_state(cfg.anim_settle_ms);
        }, hold + 200, NULL);
        lv_timer_set_repeat_count(t, 1);
        return;
    }
    if (d == LV_DIR_LEFT)       browse_prev_effect();
    else if (d == LV_DIR_RIGHT) browse_next_effect();
}

static void root_gesture_cb(lv_event_t *e) {
    (void)e;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_dir_t d = lv_indev_get_gesture_dir(indev);

    s_double_tap_triggered = true;
    s_tap_count = 0;
    s_last_release_ms = 0;

    if (s_guilt) {
        uint32_t hold = cfg.gesture_reaction_min_ms + (uint32_t)(rand() % cfg.gesture_reaction_rand_ms);
        s_effect_active = true;
        sad_shape(hold);
        lv_timer_t *t = lv_timer_create([](lv_timer_t *tt){
        if (s_effect_locked) return; // Desk buddy: voice owns the eyes.
            (void)tt; if (left_eye) return_to_base_state(cfg.anim_settle_ms);
        }, hold + 200, NULL);
        lv_timer_set_repeat_count(t, 1);
        return;
    }

    if (d == LV_DIR_LEFT)   { browse_prev_effect(); return; }
    if (d == LV_DIR_RIGHT)  { browse_next_effect(); return; }
    if (d == LV_DIR_TOP)    { state_shape_next();   return; }
    if (d == LV_DIR_BOTTOM) { state_shape_prev();   return; }
}

// ============================================================
// TEARDOWN
// ============================================================
static void root_delete_cb(lv_event_t *e) {
    (void)e;
    ESP_LOGI(TAG, "[JTAGDBG] eyes root DELETE -> teardown");

    if (s_brain_timer)       { lv_timer_delete(s_brain_timer);       s_brain_timer = NULL; }
    if (s_smile_track_timer) { lv_timer_delete(s_smile_track_timer); s_smile_track_timer = NULL; }
    if (s_sleep_retry_timer) { lv_timer_delete(s_sleep_retry_timer); s_sleep_retry_timer = NULL; }
    if (s_rolling_timer)     { lv_timer_delete(s_rolling_timer);     s_rolling_timer = NULL; }

    if (left_eye)  { kill_shape_anims(left_eye);  lv_anim_delete(left_eye, NULL); }
    if (right_eye) { kill_shape_anims(right_eye); lv_anim_delete(right_eye, NULL); }

    left_eye = right_eye = NULL;
    s_tri_l = s_tri_r = NULL;
    if (s_tri_buf_l) { free(s_tri_buf_l); s_tri_buf_l = NULL; }
    if (s_tri_buf_r) { free(s_tri_buf_r); s_tri_buf_r = NULL; }
    s_smile_mask_l = s_smile_mask_r = NULL;
    s_frown_mask_l = s_frown_mask_r = NULL;
    s_deco_l = s_deco_r = NULL;
    s_guilt_q = NULL;
    s_root   = NULL;
    s_parent = NULL;

    s_closed = false;
    s_sleeping = false;
    s_finger_down = false;
    s_reaction_until_ms   = 0;
    s_mood_happy_until_ms = 0;
    s_last_release_ms = 0;
    s_tap_count = 0;
    s_double_tap_triggered = false;
    s_caress_triggered = false;
    s_effect_active = false;
    s_shape_is_state = true;
    s_next_action_ms = 0;
    s_press_dist_acc = 0;
    s_last_look_ms = 0;
    s_look_offcenter = false;
    s_effect_locked = false;
    s_browse_index = -1;
    s_browse_last_ms = 0;
    s_caress_reversals = 0;
    s_last_dx_sign = 0;
    s_last_state_change_ms = 0;
    s_state_hold_until_ms  = 0;
}

// ============================================================
// Runtime editace tabulek (COLORS/SHAPES/STATES/EFFECTS) z dash.txt
// ============================================================
// Bezpečná kopie stringu bez ukončení '\0' když se nevejde.
static void safe_strncpy(char *dst, const char *src, size_t cap) {
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = 0; return; }
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

static void reset_eye_tables_to_defaults(void) {
    // COLORS
    memcpy(COLORS, COLOR_DEFAULTS, sizeof(COLORS));
    COLOR_COUNT = COLOR_DEFAULT_COUNT;
    // SHAPES (jen tuning; enum je fixní)
    memcpy(SHAPES, SHAPE_DEFAULTS, sizeof(SHAPES));
    // STATES
    memcpy(STATES, STATE_DEFAULTS, sizeof(STATES));
    STATE_COUNT = STATE_DEFAULT_COUNT;
    // EFFECTS tuning (prob/min/max)
    for (int i = 0; i < EFFECT_COUNT && i < (int)(sizeof(EFFECT_TUNING_DEFAULTS)/sizeof(EFFECT_TUNING_DEFAULTS[0])); i++) {
        EFFECTS[i].prob   = EFFECT_TUNING_DEFAULTS[i].prob;
        EFFECTS[i].min_ms = EFFECT_TUNING_DEFAULTS[i].min_ms;
        EFFECTS[i].max_ms = EFFECT_TUNING_DEFAULTS[i].max_ms;
    }
}

// Parse jednoho řádku 'eye_*.N=...' do runtime tabulek.
static void parse_eye_table_line(const char *line) {
    if (!line) return;
    // COLORS: eye_color.N=name,0xHEX  (nebo dec)
    int n = -1;
    char rest[96]; rest[0] = 0;
    if (sscanf(line, "eye_color.%d=%95[^\r\n]", &n, rest) == 2) {
        if (n < 0 || n >= COLOR_MAX) return;
        char name[24]; unsigned long hex = 0;
        char *comma = strchr(rest, ',');
        if (!comma) return;
        *comma = 0;
        safe_strncpy(name, rest, sizeof(name));
        const char *hp = comma + 1;
        while (*hp == ' ') hp++;
        if (hp[0] == '0' && (hp[1] == 'x' || hp[1] == 'X')) hex = strtoul(hp + 2, NULL, 16);
        else hex = strtoul(hp, NULL, 0);
        safe_strncpy(COLORS[n].name, name, sizeof(COLORS[n].name));
        COLORS[n].hex = (uint32_t)(hex & 0xFFFFFF);
        if (n + 1 > COLOR_COUNT) COLOR_COUNT = n + 1;
        return;
    }
    if (sscanf(line, "eye_color.count=%d", &n) == 1) {
        if (n < 0) n = 0;
        if (n > COLOR_MAX) n = COLOR_MAX;
        COLOR_COUNT = n;
        return;
    }
    // SHAPES: eye_shape.N=name,w,h,radius,nobrain
    {
        int idx = -1;
        char shname[24]; int w=0,h=0,r=0,nb=0;
        if (sscanf(line, "eye_shape.%d=%23[^,],%d,%d,%d,%d", &idx, shname, &w, &h, &r, &nb) == 6) {
            if (idx < 0 || idx >= SHAPE_MAX) return;
            safe_strncpy(SHAPES[idx].name, shname, sizeof(SHAPES[idx].name));
            SHAPES[idx].w = w; SHAPES[idx].h = h; SHAPES[idx].radius = r;
            SHAPES[idx].no_brain = (nb != 0);
            return;
        }
    }
    // STATES: eye_state.N=prob,min_ms,max_ms,shape,color
    {
        int idx=-1,prob=0,shp=0,col=0;
        unsigned int mn=0,mx=0;
        if (sscanf(line, "eye_state.%d=%d,%u,%u,%d,%d", &idx, &prob, &mn, &mx, &shp, &col) == 6) {
            if (idx < 0 || idx >= STATE_MAX) return;
            if (prob < 0) prob = 0;
            if (prob > 100) prob = 100;
            if (shp < 0 || shp >= SHAPE_COUNT) shp = 0;
            if (col < 0) col = 0;
            STATES[idx].prob   = prob;
            STATES[idx].min_ms = mn;
            STATES[idx].max_ms = (mx < mn) ? mn : mx;
            STATES[idx].shape  = (eye_shape_t)shp;
            STATES[idx].color  = col;
            STATES[idx].extra  = 0;
            if (idx + 1 > STATE_COUNT) STATE_COUNT = idx + 1;
            return;
        }
    }
    if (sscanf(line, "eye_state.count=%d", &n) == 1) {
        if (n < 0) n = 0;
        if (n > STATE_MAX) n = STATE_MAX;
        STATE_COUNT = n;
        return;
    }
    // EFFECTS: eye_effect.N=prob,min_ms,max_ms
    {
        int idx=-1,prob=0;
        unsigned int mn=0,mx=0;
        if (sscanf(line, "eye_effect.%d=%d,%u,%u", &idx, &prob, &mn, &mx) == 4) {
            if (idx < 0 || idx >= EFFECT_COUNT) return;
            if (prob < 0) prob = 0;
            if (prob > 100) prob = 100;
            EFFECTS[idx].prob   = prob;
            EFFECTS[idx].min_ms = mn;
            EFFECTS[idx].max_ms = (mx < mn) ? mn : mx;
            return;
        }
    }
}

extern "C" void robot_eyes_reload_tables_from_dash(void) {
    reset_eye_tables_to_defaults();
    if (!usb_msc_ensure_vfs_mounted()) {
        ESP_LOGW(TAG, "eye tables reload: VFS neni namontovane, drzim defaults");
        return;
    }
    FILE *f = fopen(USB_MSC_MOUNT_POINT "/system/dash.txt", "r");
    if (!f) return;
    char line[160];
    // Nejdřív hledáme count řádky (mohou přepsat), pak jednotlivé zápisy.
    // Jednoduše to jedeme dvouprůchodově: nejprve nastav counts,
    // pak (rewind) načti jednotlivé indexované řádky.
    while (fgets(line, sizeof(line), f)) parse_eye_table_line(line);
    fclose(f);
    ESP_LOGI(TAG, "[eyes] tables loaded: colors=%d states=%d effects=%d",
             COLOR_COUNT, STATE_COUNT, EFFECT_COUNT);
}

// Serializace tabulek do otevřeného FILE* (voláno z dashboard_write v quest_loader.cpp).
extern "C" void robot_eyes_serialize_tables_fp(FILE *f) {
    if (!f) return;
    fprintf(f,
        "# --- Robot eyes: editovatelne tabulky (upravovat pres web editor) ---\n"
        "# eye_color.N=name,0xHEX    N=0..%d\n"
        "# eye_shape.N=name,w,h,radius,no_brain    N=0..%d (fixni pocet)\n"
        "# eye_state.N=prob,min_ms,max_ms,shape_idx,color_idx    N=0..%d\n"
        "# eye_effect.N=prob,min_ms,max_ms    N=0..%d (fn+desc jsou fixni v C)\n",
        COLOR_MAX - 1, SHAPE_MAX - 1, STATE_MAX - 1, EFFECT_COUNT - 1);
    fprintf(f, "eye_color.count=%d\n", COLOR_COUNT);
    for (int i = 0; i < COLOR_COUNT; i++) {
        fprintf(f, "eye_color.%d=%s,0x%06X\n", i, COLORS[i].name, (unsigned)(COLORS[i].hex & 0xFFFFFF));
    }
    for (int i = 0; i < SHAPE_MAX; i++) {
        fprintf(f, "eye_shape.%d=%s,%ld,%ld,%ld,%d\n", i,
                SHAPES[i].name, (long)SHAPES[i].w, (long)SHAPES[i].h,
                (long)SHAPES[i].radius, SHAPES[i].no_brain ? 1 : 0);
    }
    fprintf(f, "eye_state.count=%d\n", STATE_COUNT);
    for (int i = 0; i < STATE_COUNT; i++) {
        fprintf(f, "eye_state.%d=%d,%u,%u,%d,%d\n", i,
                STATES[i].prob, (unsigned)STATES[i].min_ms, (unsigned)STATES[i].max_ms,
                (int)STATES[i].shape, STATES[i].color);
    }
    for (int i = 0; i < EFFECT_COUNT; i++) {
        fprintf(f, "eye_effect.%d=%d,%u,%u\n", i,
                EFFECTS[i].prob, (unsigned)EFFECTS[i].min_ms, (unsigned)EFFECTS[i].max_ms);
    }
}


// ============================================================
// Init
// ============================================================
extern "C" void init_robot_eyes(lv_obj_t *parent) {
    if (s_root) {
        ESP_LOGI(TAG, "[JTAGDBG] init_robot_eyes: already built, skip");
        return;
    }
    ESP_LOGI(TAG, "[JTAGDBG] init_robot_eyes(parent=%p)", parent);
    s_parent = parent;

    lv_obj_set_style_bg_color(parent, lv_color_hex(cfg.color_bg_mask), 0);

    s_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(s_root, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_add_event_cb(s_root, root_press_cb,    LV_EVENT_PRESSED,     NULL);
    lv_obj_add_event_cb(s_root, root_pressing_cb, LV_EVENT_PRESSING,    NULL);
    lv_obj_add_event_cb(s_root, root_release_cb,  LV_EVENT_RELEASED,    NULL);
    lv_obj_add_event_cb(s_root, root_release_cb,  LV_EVENT_PRESS_LOST,  NULL);
    lv_obj_add_event_cb(s_root, root_gesture_cb,  LV_EVENT_GESTURE,     NULL);
    lv_obj_add_event_cb(s_root, root_delete_cb,   LV_EVENT_DELETE,      NULL);

    // Načti runtime tabulky (COLORS/SHAPES/STATES/EFFECTS) z dash.txt.
    robot_eyes_reload_tables_from_dash();
    // Načti ladicí konstanty z dash.txt (procenta a časy pauz efektů).
    robot_eyes_reload_tunables();


    // Výchozí barva = COLORS[0].
    s_current_color_idx = 0;
    cfg.color_eye = COLORS[s_current_color_idx].hex;

    auto make_eye = [](lv_obj_t *par, int x_pos) {
        lv_obj_t *eye = lv_obj_create(par);
        lv_obj_remove_flag(eye, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(eye, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_border_width(eye, 0, 0);
        lv_obj_set_style_radius(eye, cfg.eye_radius_max, 0);
        lv_obj_set_style_bg_color(eye, lv_color_hex(cfg.color_eye), 0);
        lv_obj_set_style_border_color(eye, lv_color_hex(cfg.color_eye), 0);
        lv_obj_set_style_border_width(eye, cfg.border_width, 0);
        lv_obj_set_style_border_opa(eye, LV_OPA_60, 0);
        lv_obj_set_style_outline_color(eye, lv_color_hex(cfg.color_eye), 0);
        lv_obj_set_style_outline_width(eye, cfg.outline_width, 0);
        lv_obj_set_style_outline_opa(eye, LV_OPA_30, 0);
        lv_obj_set_size(eye, cfg.eye_width, cfg.eye_height);
        lv_obj_set_align(eye, LV_ALIGN_CENTER);
        lv_obj_set_pos(eye, x_pos, 0);
        return eye;
    };

    left_eye  = make_eye(s_root, -cfg.eye_spacing);
    right_eye = make_eye(s_root,  cfg.eye_spacing);

    ensure_masks();
    hide_all_masks();

    ensure_guilt_q();
    if (s_guilt_q) {
        if (s_guilt) lv_obj_remove_flag(s_guilt_q, LV_OBJ_FLAG_HIDDEN);
        else         lv_obj_add_flag   (s_guilt_q, LV_OBJ_FLAG_HIDDEN);
    }

    s_off_x = 0; s_off_y = 0;
    s_eye_h_l = cfg.eye_height; s_eye_h_r = cfg.eye_height;
    s_eye_w_l = cfg.eye_width;  s_eye_w_r = cfg.eye_width;
    s_closed = false; s_sleeping = false;
    s_finger_down = false; s_finger_release_ms = 0;
    s_reaction_until_ms = 0; s_mood_happy_until_ms = 0;
    s_last_release_ms = 0; s_tap_count = 0;
    s_double_tap_triggered = false;
    s_caress_triggered = false;
    s_effect_active = false;
    s_shape_is_state = true;
    s_press_dist_acc = 0;
    s_effect_locked = false;
    s_browse_index = -1;
    s_caress_reversals = 0;
    s_last_dx_sign = 0;
    s_current_shape_idx = SHAPE_OVAL;
    s_last_state_change_ms = 0;
    schedule_next_idle_action();

    // Aplikuj výchozí base stav.
    apply_base_state(s_current_shape_idx, s_current_color_idx, cfg.anim_state_transition_ms, false);
    schedule_next_state_pick();

    if (s_brain_timer) { lv_timer_delete(s_brain_timer); s_brain_timer = NULL; }
    s_brain_timer = lv_timer_create(eye_behavior_timer_cb, 250, NULL);

    if (is_night_time()) {
        apply_base_state(SHAPE_SLEEP, s_current_color_idx, cfg.anim_state_transition_ms, false);
    }

    ESP_LOGI(TAG, "init_robot_eyes OK (Questie v8 - STATE/EFFECT model)");
}

// Desk buddy platform integration.
#define QUESTIE_INCLUDE_BRIDGE
#include "questie_bridge_impl.h"
