// ===================================================================
//  BRAWL MICROSTARS v4 — Shooter 2D type Brawl Stars pour NumWorks
//  App externe Epsilon (.nwa via nwlink / EADK)
//
//  CONTROLES (V5.3) :
//    Fleches : bouger | OK ou var : attaque vers la direction du perso
//    RETOUR : ulti (super) | HOME : quitter le match (sans ecran de resultat)
//    HOME : quitter le match | + * / - : achats dans le menu BRAWLERS
//
//  V5 : menu style Brawl Stars, front montants (plus de double-appui),
//       star drop a ouvrir manuellement, quetes persistantes, IA anti-blocage,
//       but Brawl Ball seulement entre les poteaux, choix du brawler avant match.
//  V5.16 : modes en TUILES, BOSS RUSH COOP (3 boss a patterns), KO mort subite
//       eliminatoria, cubes en SURVIE, eventails Poco/Emz/Frank perceurs,
//       Poco bot ne gaspille plus son soin, crits x1.4 jaunes + soins verts,
//       screen shake directionnel fluide, mini explosion de style a la mort,
//       arc de visee pointille pour lanceurs, stats PAR brawler (save 776 o,
//       sauvee 1 fois par match), trophée d'or du plus joue, entrapinement+
//       (cibles qui ripent + DPS), compteur FPS (option), saison discrete,
//       arene circulaire + couloirs en H, boost foireux de debut SUPPRIMES.
// ===================================================================

#include <ion.h>
extern "C" {
#include <eadk.h> // eadk.h n'a pas de garde extern "C" : sans ca, le C++ decore
                  // les noms des fonctions EADK et la calculatrice ne les trouve plus
}
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "epsilon_files.h" // sauvegarde dans le systeme de fichiers d'Epsilon (methode NumPlay)
#include <cstdio> // V5.26 : snprintf (stats de fin)

// L'identite NWA (eadk_app_name / eadk_api_level) est dans src/app_id.c :
// un petit fichier C pur compile sans LTO, car le LTO C++ perd
// l'attribut de section .rodata.eadk_* exige par le chargeur d'Epsilon.

#define SW 320
#define SH 240
#define RNG(r) ((r) * 0.75f) // V5.13 : portees raccourcies de 25% (tout le monde tirait trop loin)
#define TILE 14
#define MAPW 32
#define MAPH 24

static uint16_t seed = 12345;
static int rnd(int n) { seed = (uint16_t)(seed * 25173 + 13849); return (seed >> 4) % (n > 0 ? n : 1); }

// ---- V5.10 : tables trigonometriques precalculees (perfs en match charge) ----
static float gTrigC[256], gTrigS[256];
static void initTrig() { for (int i = 0; i < 256; i++) { double a = i * 0.0245436926; gTrigC[i] = (float)cos(a); gTrigS[i] = (float)sin(a); } }
static float fcosf(float a) { return gTrigC[(int)(a * 40.743692f) & 255]; }
static float fsinf(float a) { return gTrigS[(int)(a * 40.743692f) & 255]; }

// Rendu DIRECT a l'ecran via EADK, sans frame buffer (economise ~150 Ko de RAM,
// indispensable sur N0110/N0115 ou la RAM des apps externes est limitee).
// Chaque primitive pousse ses lignes au fur et a mesure, comme NumPlay.
static void px(int x, int y, KDColor c) {
  if (x >= 0 && x < SW && y >= 0 && y < SH) Ion::Display::pushRect(KDRect(x, y, 1, 1), &c);
}
static void fRect(int x, int y, int w, int h, KDColor c) {
  if (w < 0) { x += w; w = -w; } if (h < 0) { y += h; h = -h; }
  if (x < 0) { w += x; x = 0; } if (y < 0) { h += y; y = 0; }
  if (x + w > SW) w = SW - x;
  if (y + h > SH) h = SH - y;
  if (w <= 0 || h <= 0) return;
  Ion::Display::pushRectUniform(KDRect(x, y, w, h), c);
}
// ligne de travail : les primitives a motif remplissent rowBuf puis poussent les runs
static KDColor rowBuf[SW];
static bool rowSet[SW];
static void rowClear(int x0, int x1) {
  if (x0 < 0) x0 = 0;
  if (x1 >= SW) x1 = SW - 1;
  for (int x = x0; x <= x1; x++) rowSet[x] = false;
}
static inline void rowPut(int x, KDColor c) { if (x >= 0 && x < SW) { rowBuf[x] = c; rowSet[x] = true; } }
static void rowFlush(int y, int x0, int x1) {
  if (y < 0 || y >= SH) return;
  int x = x0 < 0 ? 0 : x0, xe = x1 >= SW ? SW - 1 : x1;
  while (x <= xe) {
    if (!rowSet[x]) { x++; continue; }
    int s = x;
    while (x <= xe && rowSet[x]) x++;
    Ion::Display::pushRect(KDRect(s, y, x - s, 1), rowBuf + s);
  }
}
static void fillSpan(int y, int x0, int x1, KDColor c) {
  if (y < 0 || y >= SH || x0 > x1) return;
  if (x0 < 0) x0 = 0;
  if (x1 >= SW) x1 = SW - 1;
  for (int x = x0; x <= x1; x++) rowBuf[x] = c;
  Ion::Display::pushRect(KDRect(x0, y, x1 - x0 + 1, 1), rowBuf + x0);
}
static void rRect(int x, int y, int w, int h, int r, KDColor c) {
  for (int j = 0; j < h; j++) {
    int ry = y + j;
    rowClear(x, x + w - 1);
    for (int i = 0; i < w; i++) {
      int dx = (i < r) ? (r - i) : ((i >= w - r) ? (i - (w - r - 1)) : 0);
      int dy = (j < r) ? (r - j) : ((j >= h - r) ? (j - (h - r - 1)) : 0);
      if (dx * dx + dy * dy <= r * r) rowPut(x + i, c);
    }
    rowFlush(ry, x, x + w - 1);
  }
}
static void fCirc(int cx, int cy, int r, KDColor c) {
  for (int j = -r; j <= r; j++) {
    int w = (int)sqrtf((float)(r * r - j * j));
    fillSpan(cy + j, cx - w, cx + w, c);
  }
}
static void oCirc(int cx, int cy, int r, KDColor fill, KDColor outline) {
  for (int j = -r - 1; j <= r + 1; j++) {
    int w2 = (int)sqrtf((float)((r + 1) * (r + 1) - j * j));
    rowClear(cx - w2, cx + w2);
    for (int i = -w2; i <= w2; i++) {
      int d2 = i * i + j * j;
      rowPut(cx + i, (d2 <= r * r) ? fill : outline);
    }
    rowFlush(cy + j, cx - w2, cx + w2);
  }
}
static void shCircRGB(int cx, int cy, int r, uint32_t topRGB, uint32_t botRGB, KDColor outline) {
  KDColor top = KDColor::RGB24(topRGB), bot = KDColor::RGB24(botRGB);
  for (int j = -r - 1; j <= r + 1; j++) {
    int w2 = (int)sqrtf((float)((r + 1) * (r + 1) - j * j));
    float t = (r > 0) ? (float)(j + r) / (2.0f * r) : 0.0f;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    KDColor rowCol = KDColor::RGB(
      (uint8_t)(top.red() + (bot.red() - top.red()) * t),
      (uint8_t)(top.green() + (bot.green() - top.green()) * t),
      (uint8_t)(top.blue() + (bot.blue() - top.blue()) * t));
    rowClear(cx - w2, cx + w2);
    for (int i = -w2; i <= w2; i++) {
      int d2 = i * i + j * j;
      rowPut(cx + i, (d2 <= r * r) ? rowCol : outline);
    }
    rowFlush(cy + j, cx - w2, cx + w2);
  }
}
static void fRing(int cx, int cy, int r, KDColor c) {
  // V5.12 perf : outline par lignes (2 points/ligne via rowBuf) => ~2r+2 pushes au lieu de 120 px, cercle plus propre
  for (int dy = -r; dy <= r; dy++) {
    int y = cy + dy; if (y < 0 || y >= SH) continue;
    int s = (int)(0.5f + sqrtf((float)(r * r - dy * dy)));
    rowClear(cx - r - 1, cx + r + 1);
    rowPut(cx - s, c); rowPut(cx + s, c);
    rowFlush(y, cx - r - 1, cx + r + 1);
  }
}
static void arc(int cx, int cy, int r, float a0, float a1, int thick, KDColor c) {
  // V5.12 perf : pas de 3 degres + epaisseur reduite de 1 => ~4x moins de pixels par anneau, visuel identique
  for (int a = (int)(a0 * 180 / 3.14159f); a < (int)(a1 * 180 / 3.14159f); a += 3)
    for (int t = r - thick + 1; t <= r; t++)
      px(cx + (int)(t * fcosf(a * 0.01745f)), cy + (int)(t * fsinf(a * 0.01745f)), c);
}
static void shadow(int cx, int cy, int rx, int ry) {
  for (int j = -ry; j <= ry; j++) {
    rowClear(cx - rx, cx + rx);
    for (int i = -rx; i <= rx; i++)
      if ((i * i * 100) / (rx * rx + 1) + (j * j * 100) / (ry * ry + 1) <= 100)
        rowPut(cx + i, KDColor::RGB24(0x0A1608));
    rowFlush(cy + j, cx - rx, cx + rx);
  }
}
static void blit() { /* rendu direct : rien a pousser */ }

// ------------------------- Palette -----------------------------------
#define C_BG     KDColor::RGB24(0x1B2A41)
#define C_PANEL  KDColor::RGB24(0x3B2A63)
#define C_PANEL2 KDColor::RGB24(0x4A3785)
#define C_BTN    KDColor::RGB24(0xFFC928)
#define C_BTND   KDColor::RGB24(0xD9A400)
#define C_WALL   KDColor::RGB24(0xB08050)
#define C_WALLD  KDColor::RGB24(0x7A5432)
#define C_WALLL  KDColor::RGB24(0xD0A070)
#define C_BUSH   KDColor::RGB24(0x3D7A20)
#define C_BUSH2  KDColor::RGB24(0x4F9A2E)
#define C_BLUE   KDColor::RGB24(0x2E9BFF)
#define C_BLUED  KDColor::RGB24(0x1E6FBF)
#define C_RED    KDColor::RGB24(0xFF3B3B)
#define C_REDD   KDColor::RGB24(0xBF2222)
#define C_WHT    KDColor::RGB24(0xFFFFFF)
#define C_BLK    KDColor::RGB24(0x101010)
#define C_YEL    KDColor::RGB24(0xFFC928)
#define C_ORG    KDColor::RGB24(0xFF8327)
#define C_PUR    KDColor::RGB24(0xB44BFF)
#define C_MAG    KDColor::RGB24(0xFF2EA6)
#define C_CYN    KDColor::RGB24(0x35D4E8)
#define C_GRE    KDColor::RGB24(0x8A8A8A)
#define C_GEM    KDColor::RGB24(0x35D4E8)
#define C_GEMD   KDColor::RGB24(0x1B8FA8)
#define C_BALL   KDColor::RGB24(0xF2F2F2)
#define C_POISON KDColor::RGB24(0x7B2FA0)
#define C_POIS2  KDColor::RGB24(0x5A1F78)
#define C_GREEN  KDColor::RGB24(0x3DDC55)
#define C_GOLD   KDColor::RGB24(0xFFD700)
#define C_DGREY  KDColor::RGB24(0x2A2438)

// Fond violet du menu, pousse par bandes rapides (apres la palette : utilise C_PANEL)
static void bgPanel() {
  for (int y = 0; y < SH; y++) {
    int x = 0;
    while (x < SW) {
      int band = (x + y * 2) / 40;
      int xe = (band + 1) * 40 - y * 2 - 1;
      if (xe >= SW) xe = SW - 1;
      fillSpan(y, x, xe, (band % 2) ? C_PANEL : C_PANEL2);
      x = xe + 1;
    }
  }
}

#define RAR_N 6
static const uint32_t RAR_COL[RAR_N]  = {0xB9C4C9,0x36D4E8,0x38E05C,0xB44BFF,0xFF2EA6,0xFFC928};
static const char*  RAR_NAME[RAR_N]   = {"DEPART","RARE","SUPER RARE","EPIC","MYTHIQ","LEGENDE"};

// ------------------------- Themes de carte (1 par mode) --------------
// Chaque mode a son propre decor : couleurs + type de murs + vegetation
struct MapTheme {
  const char *themeName;
  uint32_t gr1, gr2;     // damier sol
  uint32_t grDecor;      // brins d'herbe / decor clair
  uint32_t wall, wallD, wallL;
  uint32_t bush1, bush2;
  uint8_t wallStyle;     // 0 brique, 1 pierre taille, 2 sacs de sable, 3 crates, 4 glace
  uint8_t decoStyle;     // 0 herbe, 1 fleurs, 2 cranes, 3 lignes stade, 4 neige
};
static const MapTheme THEMES[11] = {
  // GEM : mine cristalline
  {"MINE DE GEMMES", 0x4E8A5A,0x447A4E, 0x66B534, 0x8A7A5A,0x5C4E3A,0xB0A070, 0x3D7A20,0x4F9A2E, 1, 1},
  // BALL : stade foot
  {"STADE", 0x4F9026,0x458522, 0x66B534, 0xC9973B,0x8A6622,0xE0B050, 0x3D7A20,0x4F9A2E, 3, 3},
  // SOLO : desert canion
  {"CANION", 0xC9A05A,0xBD9350, 0xE0B870, 0xB07040,0x7A4A28,0xD09060, 0x7A8A3A,0x8A9A48, 2, 2},
  // DUO : foret sombre
  {"FORET SOMBRE", 0x3A6A3E,0x32603A, 0x4F8A4E, 0x6A5A3A,0x4A3E28,0x8A7A50, 0x2A5A18,0x3A6E22, 0, 0},
  // HEIST : banque / ville
  {"BANQUE", 0x707880,0x666E76, 0x8A929A, 0x9A8A6A,0x6A5A44,0xBAA880, 0x3D7A20,0x4F9A2E, 1, 2},
  // BOUNTY : marais
  {"MARAIS", 0x4E7A5E,0x447056, 0x669A76, 0x6A7A5A,0x4A5640,0x8A9A70, 0x2A5A3A,0x3A6E4A, 2, 0},
  // KO : volcan
  {"VOLCAN", 0x6A4A3A,0x5E4234, 0x8A6A50, 0x4A3A3A,0x2E2424,0x6A5A5A, 0x5A3A20,0x6E4A2A, 0, 2},
  // ZONE : toundra glacee
  {"TOUNDRA", 0x9ABAD0,0x8EAE7A,0xB4CEE2, 0x7A9AC0,0x4E6A8A,0xA0C0E0, 0x5A7A4E,0x6E8E60, 4, 4},
  // SIEGE : usine a robots (metal froid)
  {"USINE A ROBOTS", 0x3E4450,0x353B46, 0x5A6470, 0x6E8A9A,0x4A5A6A,0x9AA8B8, 0x3D7A20,0x4F9A2E, 3, 2},
  // DUEL : arene de combat
  {"ARENE", 0x8A6A4A,0x7E6042, 0xA8845C, 0x9A8A6A,0x6A5A44,0xBAA880, 0x3D7A20,0x4F9A2E, 1, 2},
  // SURVIE : forteresse assiegee
  {"FORTERESSE", 0x4A3E56,0x40344C, 0x5E4E6A, 0x8A7A9A,0x5A4E6A,0xB0A0C0, 0x2A5A18,0x3A6E22, 0, 2},
};

// ------------------------- Definitions brawlers ----------------------
enum AtkType { ATK_SHOT, ATK_SPREAD, ATK_LOB };
enum SuperType { SUP_BURST, SUP_DASH, SUP_HEAL, SUP_BARRAGE, SUP_MEGASHOT, SUP_WAVE, SUP_SUMMON,
                 SUP_STEALTH, SUP_SHIELD, SUP_PULL, SUP_PUSH, SUP_WHIRL, SUP_MINES, SUP_UPGRADE,
                 SUP_MECH, SUP_LEAP, SUP_HASTE, SUP_WALL };
enum GadType { GAD_HEAL, GAD_SPEED, GAD_AMMO, GAD_BLAST };
enum StarType { STAR_DMG, STAR_HP, STAR_SPEED, STAR_REGEN };

struct BrawlerDef {
  const char *name;
  int baseHP, baseDmg, range, reloadMs;
  float projSpeed;
  int projCount, spreadDeg;
  AtkType atk;
  SuperType sup;
  GadType gad;
  StarType star;
  int rar;
  int unlockTroph;
  uint32_t colTop, colBot;
  uint8_t projShape;
};

static const BrawlerDef BRAWLERS[55] = {
  {"Shelly",   5600, 1280, 170, 1500, 340, 3, 24, ATK_SPREAD, SUP_BURST,    GAD_SPEED, STAR_DMG,    0,     0, 0x8A4BE0, 0x5C2BA0, 0},
  {"Nita",     5400, 1320, 160, 1400, 330, 1,  0, ATK_SHOT,   SUP_SUMMON,   GAD_HEAL,  STAR_HP,    0,     0, 0xC98A3B, 0x8A5C22, 1},
  {"Colt",     4000,  940, 240, 1600, 420, 6,  8, ATK_SHOT,   SUP_MEGASHOT, GAD_AMMO,  STAR_DMG,   0,     0, 0x3B9AC9, 0x22628A, 1},
  {"Bull",     6600, 1750, 100, 1900, 300, 5, 30, ATK_SPREAD, SUP_DASH,     GAD_BLAST, STAR_HP,    1,    60, 0xC9973B, 0x8A6622, 0},
  {"Brock",    3800, 1620, 280, 2000, 360, 1,  0, ATK_SHOT,   SUP_MEGASHOT, GAD_SPEED, STAR_DMG,   1,   120, 0xC93B3B, 0x8A2222, 2},
  {"El Primo", 6800, 1400,  90, 1300, 330, 4,  0, ATK_SHOT,   SUP_DASH,     GAD_HEAL,  STAR_HP,    1,   180, 0x3BA05C, 0x22663A, 0},
  {"Barley",   3600, 1160, 240, 1700, 260, 1,  0, ATK_LOB,    SUP_BARRAGE,  GAD_AMMO,  STAR_REGEN, 1,   240, 0x7BC93B, 0x4F8A22, 3},
  {"Poco",     5600, 1100, 200, 1400, 300, 5, 34, ATK_SPREAD, SUP_HEAL,     GAD_HEAL,  STAR_REGEN, 1,   300, 0xB03BE0, 0x7822A0, 1},
  {"Rosa",     6400, 1250, 120, 1500, 340, 3, 35, ATK_SPREAD, SUP_SHIELD,   GAD_SPEED, STAR_HP,    2,   400, 0x7AC96B, 0x4F8A46, 0},
  {"Jessie",   4600, 1180, 210, 1700, 300, 1,  0, ATK_SHOT,   SUP_SUMMON,   GAD_AMMO,  STAR_REGEN, 2,   500, 0xA03BA0, 0x701F70, 2},
  {"Dynamike", 4000, 1330, 230, 1800, 280, 2, 12, ATK_LOB,    SUP_BARRAGE,  GAD_BLAST, STAR_DMG,   2,   620, 0xE0B03B, 0xA07822, 3},
  {"Tick",     3000, 1120, 260, 1500, 260, 1,  0, ATK_LOB,    SUP_SUMMON,   GAD_BLAST, STAR_REGEN, 2,   750, 0x8AC93B, 0x5C8A22, 3},
  {"8-Bit",    5800, 1350, 260, 2000, 330, 1,  0, ATK_SHOT,   SUP_SUMMON,   GAD_AMMO,  STAR_DMG,   2,   880, 0xC9A03B, 0x8A6E22, 1},
  {"Rico",     4200, 1010, 260, 1500, 400, 5,  6, ATK_SHOT,   SUP_MEGASHOT, GAD_SPEED, STAR_DMG,   2,  1000, 0x3BC9B0, 0x228A78, 1},
  {"Darryl",   6300, 1460, 130, 1400, 380, 2, 14, ATK_SPREAD, SUP_DASH,     GAD_BLAST, STAR_HP,    2,  1150, 0xC96B3B, 0x8A4822, 0},
  {"Penny",    4600, 1290, 230, 1700, 340, 1,  0, ATK_SHOT,   SUP_SUMMON,   GAD_HEAL,  STAR_REGEN, 2,  1300, 0xE07B3B, 0xA05222, 0},
  {"Carl",     5500, 1250, 190, 1200, 320, 1,  0, ATK_SHOT,   SUP_DASH,     GAD_SPEED, STAR_HP,    3,  1500, 0x7B9BC9, 0x50688A, 3},
  {"Jacky",    6000, 1300, 110, 1500, 320, 4, 22, ATK_SPREAD, SUP_WHIRL,    GAD_BLAST, STAR_HP,    3,  1700, 0xE0B03B, 0xA07822, 0},
  {"Bo",       5000, 1200, 250, 1800, 310, 3,  9, ATK_SHOT,   SUP_MINES,    GAD_SPEED, STAR_REGEN, 3,  1900, 0x3B7AC9, 0x224F8A, 2},
  {"Emz",      4800, 1180, 200, 1600, 280, 4, 26, ATK_SPREAD, SUP_BURST,    GAD_HEAL,  STAR_REGEN, 3,  2100, 0xE03B8A, 0xA02258, 1},
  {"Stu",      3800, 1080, 220, 1300, 420, 1,  0, ATK_SHOT,   SUP_DASH,     GAD_SPEED, STAR_DMG,   3,  2350, 0xC93BA0, 0x8A2270, 2},
  {"Pam",      6200, 1150, 220, 1800, 300, 4, 10, ATK_SPREAD, SUP_SUMMON,   GAD_HEAL,  STAR_REGEN, 3,  2600, 0xB09B3B, 0x7A6A22, 2},
  {"Frank",    7400, 1800, 140, 2400, 260, 3, 30, ATK_SPREAD, SUP_WAVE,     GAD_AMMO,  STAR_HP,    3,  2900, 0x5C7A9B, 0x3B5068, 3},
  {"Bibi",     5900, 1350, 120, 1300, 360, 3, 15, ATK_SPREAD, SUP_WAVE,     GAD_SPEED, STAR_HP,    3,  3200, 0xE05C3B, 0xA03E22, 0},
  {"Bea",      3600,  960, 270, 1500, 380, 1,  0, ATK_SHOT,   SUP_BARRAGE,  GAD_AMMO,  STAR_DMG,   3,  3500, 0xE0C93B, 0xA08A22, 2},
  {"Nani",     3400, 1420, 260, 1900, 340, 3, 16, ATK_SHOT,   SUP_SUMMON,   GAD_SPEED, STAR_DMG,   4,  3900, 0x9B7BE0, 0x6850A0, 2},
  {"Edgar",    4200, 1050,  80, 1100, 380, 2, 10, ATK_SPREAD, SUP_DASH,     GAD_HEAL,  STAR_REGEN, 4,  4300, 0x3B3B50, 0x22223A, 0},
  {"Griff",    4900, 1080, 230, 1500, 360, 3,  8, ATK_SHOT,   SUP_BARRAGE,  GAD_AMMO,  STAR_DMG,   4,  4700, 0xC9C93B, 0x8A8A22, 1},
  {"Gale",     5300, 1240, 200, 1600, 320, 3, 14, ATK_SPREAD, SUP_PUSH,     GAD_SPEED, STAR_HP,    4,  5100, 0x7BC9E0, 0x508A9B, 2},
  {"Colette",  4800, 1300, 210, 1500, 330, 2,  6, ATK_SHOT,   SUP_DASH, GAD_AMMO,  STAR_DMG,   4,  5500, 0xE05C9B, 0xA03E68, 1},
  {"Belle",    3900, 1150, 260, 1700, 350, 1,  0, ATK_SHOT,   SUP_BARRAGE,  GAD_SPEED, STAR_DMG,   4,  6000, 0x7B3BE0, 0x5022A0, 2},
  {"Buzz",     6100, 1380, 110, 1300, 370, 3, 18, ATK_SPREAD, SUP_DASH,     GAD_BLAST, STAR_HP,    4,  6500, 0x3BE0C9, 0x229B8A, 0},
  {"Ash",      6500, 1290, 180, 1700, 300, 4, 12, ATK_SHOT,   SUP_SUMMON,   GAD_AMMO,  STAR_HP,    4,  7000, 0x9BA05C, 0x686A3A, 3},
  {"Lola",     4400, 1220, 240, 1600, 340, 1,  0, ATK_SHOT,   SUP_SUMMON,   GAD_AMMO,  STAR_DMG,   4,  7500, 0xE03B5C, 0xA0223E, 1},
  {"Fang",     5700, 1420, 140, 1400, 350, 2, 12, ATK_SPREAD, SUP_DASH,     GAD_SPEED, STAR_HP,    4,  8000, 0xC95C3B, 0x8A3E22, 0},
  {"Maisie",   4700, 1260, 240, 1700, 330, 1,  0, ATK_SHOT,   SUP_BURST,    GAD_BLAST, STAR_DMG,   5,  8600, 0x3BE0A0, 0x229B68, 2},
  {"Squeak",   3500, 1200, 250, 1800, 280, 1,  0, ATK_LOB,    SUP_BARRAGE,  GAD_BLAST, STAR_REGEN, 5,  9200, 0xC93BE0, 0x8A22A0, 3},
  {"Hank",     6900, 1500, 220, 2000, 290, 1,  0, ATK_SHOT,   SUP_BARRAGE,  GAD_AMMO,  STAR_HP,    5,  9800, 0x3B6BE0, 0x2247A0, 2},
  {"Pearl",    5200, 1230, 210, 1500, 320, 2,  8, ATK_SPREAD, SUP_HEAL,     GAD_HEAL,  STAR_REGEN, 5, 10500, 0xE09B3B, 0xA06822, 1},
  {"Spike",    3200, 1120, 250, 1900, 300, 3, 20, ATK_LOB,    SUP_BURST,    GAD_SPEED, STAR_DMG,   5, 11200, 0x7BE05C, 0x4FA03E, 3},
  {"Crow",     3200, 1080, 260, 1500, 400, 3,  6, ATK_SHOT,   SUP_BARRAGE,  GAD_SPEED, STAR_DMG,   5, 12000, 0x5C3BE0, 0x3E22A0, 2},
  {"Leon",     4200, 1160, 220, 1300, 390, 4,  7, ATK_SPREAD, SUP_STEALTH,  GAD_SPEED, STAR_DMG,   5, 12800, 0x3BE07B, 0x22A050, 1},
  {"Sandy",    4600, 1180, 240, 1600, 320, 3,  9, ATK_LOB,    SUP_STEALTH,  GAD_AMMO,  STAR_REGEN, 5, 13700, 0x7B7BE0, 0x5050A0, 2},
  {"Amber",    4800, 1090, 200, 1200, 340, 2,  4, ATK_SHOT,   SUP_BARRAGE,  GAD_AMMO,  STAR_DMG,   5, 14600, 0xE0C95C, 0xA08A3E, 1},
  {"Gene",     4500, 1230, 250, 1700, 310, 1,  0, ATK_SHOT,   SUP_PULL,     GAD_HEAL,  STAR_REGEN, 5, 15500, 0x9B3BE0, 0x6822A0, 2},
  {"Mortis",   5000, 1260, 150, 1300, 380, 1,  0, ATK_SHOT,   SUP_WAVE,     GAD_SPEED, STAR_HP,    5, 16500, 0x3B3B5C, 0x22223E, 3},
  {"Byron",    3900, 1050, 270, 1800, 340, 1,  0, ATK_SHOT,   SUP_HEAL,     GAD_HEAL,  STAR_REGEN, 5, 17500, 0xC9C9C9, 0x8A8A8A, 2},
  {"Meg",      4100, 1150, 230, 1500, 330, 2,  9, ATK_SHOT,   SUP_MECH,     GAD_AMMO,  STAR_DMG,   5, 18600, 0xE03BC9, 0xA0228A, 1},
  {"Chester",  4600, 1210, 210, 1400, 350, 3, 11, ATK_SPREAD, SUP_BARRAGE,  GAD_BLAST, STAR_DMG,   5, 19800, 0x5CC9E0, 0x3E8A9B, 0},
  {"Surge",    4500, 1250, 200, 1400, 360, 3, 13, ATK_SPREAD, SUP_UPGRADE,  GAD_SPEED, STAR_DMG,   5, 21000, 0x3BE0E0, 0x229B9B, 2},
  {"Piper",    3200, 1780, 340, 2000, 430, 1,  0, ATK_SHOT,   SUP_LEAP,     GAD_AMMO,  STAR_DMG,   3, 22000, 0xE05C9B, 0xA03E68, 2},
  {"Max",      4200, 1000, 200, 1100, 400, 4,  6, ATK_SPREAD, SUP_HASTE,    GAD_SPEED, STAR_SPEED, 4, 23000, 0xE03B7B, 0xA02250, 2},
  {"Tara",     4400, 1150, 240, 1700, 330, 3,  5, ATK_SHOT,   SUP_PULL,     GAD_HEAL,  STAR_DMG,   5, 24000, 0x8A2BE0, 0x5C1DA0, 2},
  {"Sprout",   3600, 1100, 250, 1800, 290, 1,  0, ATK_LOB,    SUP_WALL,     GAD_BLAST, STAR_REGEN, 5, 25000, 0x7BE05C, 0x4FA03E, 3},
  {"Mr. P",    4600, 1100, 240, 1700, 300, 1,  0, ATK_LOB,    SUP_SUMMON,   GAD_AMMO,  STAR_REGEN, 4, 26000, 0x3B7AC9, 0x224F8A, 3},
};

// ------------- Traits visuels uniques par brawler --------------------
// Derives deterministes de l'index : chaque brawler a sa coiffure,
// son chapeau/accessoire et sa couleur d'accent. 50 looks differents.
static const char *STAR_NAMES[55] = {
  "COQUILLE DURE","AMITIE FEROCE","TIR VELOCE","PEAUX DURES","ROQUETTES LONGUES",
  "MUAY PRIMO","SIROP EPAIS","GUERISON DAIRY","GANTS FERTILES","SURCHARGE",
  "DYNAMITE GRANDE","CARAPACE","SAUT LONG","ROULE-BALLON","SAC MAGIQUE",
  "POT DE POUDRE","CHALEUR FROIDE","SEISME","TROISIEME OEIL","CŒUR TENDRE",
  "VITESSE ECLAIR","SOIN MATERNEL","FORCE HERCULE","RACQUETTE","RAYON GEL",
  "PEUGEOT RAPIDE","GRIFFES LESTES","REMISE RONDE","COFFRE OUVERT","GIVRE EPINEUX",
  "ECONOMIE","GRAND ECLAIR","BOUCLIER MAITRE","POUBELLE FUMANTE","LILAS DORE",
  "SAVATE FORT","BRASIER","PUSTULE","GONFLE","DOUCE PERLE",
  "SABLE SLOW","VENIN","FANTOME","TROUBLE","HUILE",
  "LAMPE","CAFE SERRE","BOUSSOLE","RONFLEMENT","ALTERNANCE",
  "MUNITION FUMANTE","DEMARRE RAPIDE","DESTIN TRACE","PHOTOSYNTHESE","POURBOIRE GENEREUX"
};
static const char *BRAWLER_BIOS[55] = {
  "FUSIL A POMPE DEPANNEUSE","AMIE DES OURS","DEUX PISTOLETS STAR","BRUTE DE COWBOY","FUSIL ROQUETTES",
  "LUTTEUR MASQUE","BARMAN ROBOT","MARIACHI DOUX","BOTANISTE BOXEUSE","INGENIEURE SCRAPPY",
  "DEMINEUR FOU","TETE A CLAQUES","ARCADE AMBULANTE","RICOCHET MAITRE","TONNEAU ROULANT",
  "PIRATE AU SABLE","MINEUR FOU","OUVRIERE DE CHANTIER","ARCHER SIOUX","INFLUENCEUSE TOXIQUE",
  "PILOTE DE COURSE","MAMAN SOIGNEUSE","MONSTRE FRANKENSTEIN","BATTEUSE DE BASEBALL","ABEILLE REINE",
  "GRAND-MERE A DISTANCE","CAPEUR DES TOITS","PATRON AVARE","MAITRE DU VENT","FACTRICE IMPIOYE",
  "VOLUSE MASQUEE","SAUVEUR DE PLAGE","CENDRILLON COLERE","SPECTRE ECRIVAIN","DIVA DE SCENE",
  "MAITRE DU KUNG-FU","FOCUS BRULANT","ALIEN ELASTIQUE","GENERAL POISSON","PATISSIERE FEROCE",
  "CACTUS LEGENDAIRE","CORBEAU ASSASSIN","NINJA INVISIBLE","SPECTRE SOMNOLANT","ALCHIMISTE FEERIE",
  "GENIE DE LAMPE","VAMPIR MOUSTACHE","ROBOT PILOTE","CLOWN IMPREVISIBLE","SURGE ELECTRIQUE",
  "SNIPER DE FETE","COACH SPORTIVE","CARTOMANCIENT","JARDINIER VOLANT","PORTEUR TROP POLI"
};
// Noms et descriptions des SUPERS (inspires de Brawl Stars)
static const char *SUPER_NAMES[55] = {
  "SUPER COQUILLE","ATTENTION OURS","BALLE DE DESTRUCTION","CHARGE TAURAU","PLUIE DE ROQUETTES",
  "METEOR PRIMO","ULTIME COCKTAIL","GUERISON ENCOREE","CARAPACE VEGETALE","SCRAPPY FURIEUX",
  "GRANDE DYNAMITE","MINE REJETEE","TOURELLE BOOSTEUSE","PLUIE RICOCHET","DOUBLE BARIL",
  "MORTIER","FURIE CARROUEL","SEISME TOTAL","TRIPLE MINE","SPRAY MASSIF",
  "TURBO BURNOUT","STATION DE SOIN","MASSUE SEISME","GOMME FURIEUSE","ESSAIM GEANT",
  "PEEP AUTO","SAUT OMBREUX","PLUIE DE PIECES","BLIZZARD POLAIRE","PERCUTE TOUT",
  "MARQUE POSEE","TORPILLE SAISIE","RATS EN COLERE","EGO SURSCENE","SAVATE VOLANTE",
  "CHOC ARDENT","BLOB SPLITTEUR","BALLON MASTODONTE","FOURNAISE","ZONE EPINEUSE",
  "CRAQUE-CORBEAU","FUMEE FANTOME","SABLE TEMPETE","HUILE BRULANTE","MAIN MAGIQUE",
  "BAIN DE SANG","SERUM VITAL","MEGA ROBOTS","CLOCHE FURIEUSE","SURGE SUPREME",
  "ESCAPE FUMANT","PARTANCE ECLAIR","TROU NOIR","MUR NATUREL","PORTAGE RAPIDE"
};
// Pouvoir star n2 pour chaque brawler
static const char *STAR2_NAMES[55] = {
  "BANDE PLAISANTE","BONNE ETOILE","DEUXIEME CHARGE","PEAUX TRES DURES","ROQUETTE DOUBLE",
  "PRIMO PRIME","BOISSON LONGUE","CHANSON DOUCE","ROSA ROBUSTE","PANNEAU SOLIDE",
  "SAUT DYNAMITE","TETE MAGNETIQUE","ARCADE PUISSANTE","RIC-ORIGINE","ROULE DOUBLE",
  "PIERRE POKER","FRAPPE PROFONDE","OUVRIERE CHERE","OEIL D AIGLE","CŒUR FORT",
  "VROUM MAXI","MERE TOUTE PUISSANTE","ETONNEMENT SUPREME","COURSES RAPIDES","DARD PERCENT",
  "PEUGEOT TURBO","TIR GRELE","PORTAIL MULTIPLE","CYCLONE RAPIDE","GLACE PROFONDE",
  "REMISE DOUBLE","SAUVETAGE","POUBELLE TRIEE","ASHER ANTIQUE","LILAS MAJEUR",
  "SAVATE LONGUE","BRISE-OS","PUSTULES DOUBLE","POISSON ADULTE","PERLE MAJEURE",
  "CHAMPS PROTEGE","CORBEAU CHER","PAS RAPIDE","SOMBRE PERLE","HUILE MAJEURE",
  "LAMPE MAGIQUE","SOIN FINAL","MEGA PILOTAGE","CLOCHE DOUBLE","SURGE MAJEUR",
  "PORTEE ROSEE","CARDIO PINK","MAIN TELLURIQUE","PELLE SOLLIDE","PANIER LARGE"
};
// ----------------- V5.10 : menus, skins, duel, carriere, perfs -----------------
// Selection de brawlers en GRILLE de cartes facon Brawl Stars (5x4, 3 pages) ;
// mode DUEL 3 BRAWLERS (on choisit ses 3 persos, le suivant entre a la mort) ;
// BOUTIQUE de skins (AZUR/OR/OMBRE, teintes du corps, achetables en pieces) ;
// ecran CARRIERE (parties, victoires, taux, kills, brawler favori persistants) ;
// trophees bonus MVP/boucherie ; EMOTES (touches + et -, choisies au menu) ;
// rejouer instantane (+ sur l'ecran de resultat) ; cos/sin en tables precalculees.
// ----------------- V5.9 : grande mise a jour gameplay -----------------
// Stats de fin de match (top degats/kills), IA adaptative (gAIDyn) et personnalites
// IA (agressif/defensif/fourbe), evenement aleatoire par match (PV/vitesse/supers/
// munitions, rotation par compteur de matchs : aucune horloge, aucun internet),
// mode SIEGE DU BOSS (3 heros contre un MEGA ROBOT geant), power cubes en
// Survivant (boites destructibles, +10% PV et butin qui tombe a la mort),
// teleporteurs par paires (modes 3v3) et tonneaux explosifs destructibles.
// ----------------- V5.8 : vague 6 de sprites (Piper, Max, Tara, Sprout, Mr. P) + IA -----------------
// Meg se transforme en MEGA ROBOT pendant 8 s apres son super (mechT, flag gMechDraw).
// IA : Meg-mecha agressive et ne fuit plus, Piper bondit pour s'echapper, Max boost pour chasser.
// ----------------- V5.7b : sprites manuels redessines (10 premiers brawlers) -----------------
// Refonte complete apres preview : vraies silhouettes humanoïdes (tete separee,
// torse, jambes, arme tenue PRES du corps) inspirees des graphismes reels.
// Tout est proportionnel a r (float), coordonnees arrondies aux fonctions de dessin.
static void heroEyes(int x, int y, int r, int dx, bool angry) {
  int sp = (r >= 6) ? (r / 3 + 1) : 1;
  int pup = (dx > 0) ? 1 : 0;
  if (r >= 8) {
    fCirc(x - sp, y, 2, C_WHT); fCirc(x + sp, y, 2, C_WHT);
    px(x - sp + pup, y, C_BLK); px(x + sp + pup, y, C_BLK);
  } else { px(x - 1, y, C_WHT); px(x + 1, y, C_WHT); }
  if (angry && r >= 6) { fRect(x - sp - 1, y - 3, 3, 1, C_BLK); fRect(x + sp - 1, y - 3, 3, 1, C_BLK); }
}
// corps humanoide commun : jambes animees + torse. r = rayon total du sprite.
// ---- V5.10 : skins = teintes alternatives du corps (achetees en boutique) ----
static uint8_t gSkinTint = 0; // 0 normal, 1 AZUR, 2 OR, 3 OMBRE (defini par drawBrawlerSprite)
static uint32_t skinCol(uint32_t c) {
  if (!gSkinTint) return c;
  uint32_t r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
  if (gSkinTint == 1) return (b << 16) | (g << 8) | r;                  // AZUR : rouge et bleu echangees
  if (gSkinTint == 2) { r = (r + 170) / 2; g = (g + 140) / 2; b /= 2; }  // OR : teinte doree
  else { r /= 2; g = (g * 7) / 10; b = (b * 6) / 10 + 25; }             // OMBRE : sombre bleute
  return (r << 16) | (g << 8) | b;
}
static void heroBody(int cx, int cy, int r, bool flash, uint32_t topRGB, uint32_t botRGB, uint32_t legsRGB, int walk) {
  topRGB = skinCol(topRGB); botRGB = skinCol(botRGB); legsRGB = skinCol(legsRGB); // skin equipe
  int step = (walk > 0 && r >= 6) ? ((walk / 40) % 2) : 0;
  uint32_t tg = flash ? 0xFFFFFF : topRGB, bg = flash ? 0xAAAAAA : botRGB;
  // jambes (petit decalage vertical alterne = marche)
  fRect(cx - (int)(r * 0.42f), cy + (int)(r * 0.45f), (int)(r * 0.34f), (int)(r * 0.5f), KDColor::RGB24(legsRGB));
  fRect(cx + (int)(r * 0.08f), cy + (int)(r * 0.45f) - step, (int)(r * 0.34f), (int)(r * 0.5f) + step, KDColor::RGB24(legsRGB));
  // torse (deux cercles + pont = silhouette de buste)
  fCirc(cx, cy + (int)(r * 0.18f), (int)(r * 0.45f), KDColor::RGB24(tg));
  fCirc(cx, cy + (int)(r * 0.48f), (int)(r * 0.38f), KDColor::RGB24(bg));
  fRect(cx - (int)(r * 0.45f), cy + (int)(r * 0.2f), (int)(r * 0.9f), (int)(r * 0.3f), KDColor::RGB24(bg));
}
static bool gMechDraw = false; // Meg transformee en mega robot pendant son super
static void drawHeroSprite(int cx, int cy, int r, int bIdx, int faceAng, bool flash, int walk, int attackT) {
  float fa = faceAng * 0.01745f;
  int fx = (int)(fcosf(fa) * r * 0.3f), fy = (int)(fsinf(fa) * r * 0.3f); // regard vers la visee
  int ex = (fx > 2) ? 2 : 1;                                              // decalage des yeux selon la visee
  int hy = cy - (int)(r * 0.55f), hr = (int)(r * 0.48f);                  // centre/rayon de la tete
  switch (bIdx) {
    case 0: { // SHELLY : chevelure noire a frange rose, veste rose, fusil a pompe
      heroBody(cx, cy, r, flash, 0xE0559B, 0xC2347F, 0x3A2030, walk);
      fCirc(cx, hy, hr + 1, flash ? C_WHT : KDColor::RGB24(0x26262F));    // masse de cheveux
      fCirc(cx + 1, hy + 1, hr - (hr >= 6 ? hr * 0.35f : 2), flash ? C_WHT : KDColor::RGB24(0xB07850)); // visage
      fRect(cx - hr, hy - (int)(hr * 0.55f), hr * 2, 2, flash ? C_WHT : KDColor::RGB24(0xE880A8)); // frange rose haute
      heroEyes(cx + ex, hy + fy / 2, hr, fx, false);
      if (r >= 6) fRect(cx - 2, hy + (int)(hr * 0.55f), 5, 1, C_BLK);     // sourire
      fRect(cx + (int)(r * 0.35f), cy + (int)(r * 0.1f), (int)(r * 0.75f), (int)(r * 0.14f) + 1, flash ? C_GRE : KDColor::RGB24(0x6A4A2A)); // crosse bois
      fRect(cx + (int)(r * 0.95f), cy + (int)(r * 0.07f), (int)(r * 0.35f), (int)(r * 0.2f) + 1, C_GRE); // canon acier
      break;
    }
    case 1: { // NITA : capuche d'ours a oreilles, manteau orange
      heroBody(cx, cy, r, flash, 0xE8A33D, 0xC98833, 0x4A3010, walk);
      KDColor hood = KDColor::RGB24(0x8A5838), hood2 = KDColor::RGB24(flash ? 0xFFFFFF : 0xB07850);
      int er = hr / 2 + 1;                                                 // oreilles d'ours
      fCirc(cx - (int)(r * 0.62f), hy - (int)(r * 0.42f), er, hood); fCirc(cx + (int)(r * 0.62f), hy - (int)(r * 0.42f), er, hood);
      fCirc(cx, hy, hr + 2, hood);                                         // capuche
      fCirc(cx, hy + 1, hr - 2, hood2);                                    // visage
      heroEyes(cx + ex, hy + fy / 2, hr, fx, true);
      if (r >= 6) fRect(cx - 2, hy + (int)(hr * 0.5f), 5, 2, C_BLK);       // grande bouche
      break;
    }
    case 2: { // COLT : casquette rouge, cheveux blonds, veste bleue, 2 revolvers
      heroBody(cx, cy, r, flash, 0x4A90D9, 0x3568B0, 0x2A3A5A, walk);
      KDColor hair = KDColor::RGB24(0xE8C93D);
      fCirc(cx, hy, hr, flash ? C_WHT : KDColor::RGB24(0xB07850));         // visage
      fRect(cx - hr, hy - 2, 2, (int)(hr * 0.7f), hair); fRect(cx + hr - 2, hy - 2, 2, (int)(hr * 0.7f), hair); // meches blondes
      fCirc(cx, hy - (int)(hr * 0.45f), (int)(hr * 0.72f), flash ? C_WHT : KDColor::RGB24(0xD93B3B)); // casquette
      fRect(cx, hy - (int)(hr * 0.35f), hr + 4, 2, flash ? C_GRE : KDColor::RGB24(0xB02A2A)); // visiere
      heroEyes(cx + ex, hy + 1 + fy / 2, hr, fx, false);
      if (r >= 6) fRect(cx - 2, hy + (int)(hr * 0.5f), 5, 1, C_BLK);
      fRect(cx - (int)(r * 0.62f), cy + (int)(r * 0.25f), (int)(r * 0.4f), (int)(r * 0.12f) + 1, C_GRE); // revolver gauche
      fRect(cx + (int)(r * 0.25f), cy + (int)(r * 0.25f), (int)(r * 0.4f), (int)(r * 0.12f) + 1, C_GRE);   // revolver droit
      break;
    }
    case 3: { // BULL : cornes, barbe, marcelle, gros fusil a double canon
      heroBody(cx, cy, r, flash, 0x8A5838, 0x6A4328, 0x3A2515, walk);
      KDColor horn = KDColor::RGB24(0xE8E0D0);
      fCirc(cx, hy, hr + 1, flash ? C_WHT : KDColor::RGB24(0xB07850));     // tete
      fRect(cx - hr - 3, hy - (int)(hr * 0.3f), 3, 2, horn); fRect(cx + hr, hy - (int)(hr * 0.3f), 3, 2, horn); // cornes
      fRect(cx - (int)(hr * 0.7f), hy + (int)(hr * 0.35f), (int)(hr * 1.4f), (int)(hr * 0.5f), KDColor::RGB24(0x3A2515)); // barbe
      heroEyes(cx + ex, hy + fy / 2, hr, fx, true);
      if (r >= 6) fRect(cx - 3, hy + (int)(hr * 0.5f), 7, 1, C_WHT);       // dents
      fRect(cx - (int)(r * 0.1f), cy + (int)(r * 0.15f), (int)(r * 0.9f), (int)(r * 0.13f) + 1, KDColor::RGB24(0x6A4A2A)); // canon 1
      fRect(cx - (int)(r * 0.1f), cy + (int)(r * 0.3f), (int)(r * 0.9f), (int)(r * 0.13f) + 1, KDColor::RGB24(0x6A4A2A)); // canon 2
      fRect(cx + (int)(r * 0.7f), cy + (int)(r * 0.12f), (int)(r * 0.3f), (int)(r * 0.35f), C_GRE); // pompe acier
      break;
    }
    case 4: { // BROCK : afro rouge, veste verte, lance-roquettes sur l'epaule
      heroBody(cx, cy, r, flash, 0x2A7A4A, 0x1F5C38, 0x143D24, walk);
      KDColor afro = KDColor::RGB24(0xE83B3B);
      fCirc(cx, hy, hr, flash ? C_WHT : KDColor::RGB24(0x7A4A28));         // visage
      fCirc(cx, hy - (int)(hr * 0.3f), hr + 1, afro);                      // afro
      fCirc(cx - (int)(hr * 0.6f), hy - (int)(hr * 0.1f), (int)(hr * 0.55f), afro);
      fCirc(cx + (int)(hr * 0.6f), hy - (int)(hr * 0.1f), (int)(hr * 0.55f), afro);
      heroEyes(cx + ex, hy + 2 + fy / 2, hr, fx, false);
      if (r >= 6) fRect(cx - 2, hy + (int)(hr * 0.5f), 5, 2, C_WHT);       // grand sourire
      fRect(cx - (int)(r * 0.9f), cy - (int)(r * 0.15f), (int)(r * 1.1f), (int)(r * 0.3f), KDColor::RGB24(0x3A5A3A)); // lance-roquettes
      fCirc(cx + (int)(r * 0.25f), cy, (int)(r * 0.18f) + 1, flash ? C_WHT : KDColor::RGB24(0xE8447A)); // ogive
      break;
    }
    case 5: { // EL PRIMO : masque de luchador vert, torse muscule, poings leves
      uint32_t skin = flash ? 0xFFFFFF : 0xC98A5C;
      int step = (walk > 0 && r >= 6) ? ((walk / 40) % 2) : 0;
      fRect(cx - (int)(r * 0.42f), cy + (int)(r * 0.45f), (int)(r * 0.34f), (int)(r * 0.5f), KDColor::RGB24(0x3B6EA5)); // jambes bleues
      fRect(cx + (int)(r * 0.08f), cy + (int)(r * 0.45f) - step, (int)(r * 0.34f), (int)(r * 0.5f) + step, KDColor::RGB24(0x3B6EA5));
      fCirc(cx, cy + (int)(r * 0.2f), (int)(r * 0.5f), KDColor::RGB24(skin));   // torse muscule
      fCirc(cx, cy + (int)(r * 0.5f), (int)(r * 0.4f), KDColor::RGB24(skin));
      fRect(cx - (int)(r * 0.5f), cy + (int)(r * 0.25f), r, (int)(r * 0.25f), KDColor::RGB24(skin));
      fRect(cx - (int)(r * 0.4f), cy, (int)(r * 0.15f), (int)(r * 0.5f), KDColor::RGB24(0x2A6A46)); // bretelles
      fRect(cx + (int)(r * 0.25f), cy, (int)(r * 0.15f), (int)(r * 0.5f), KDColor::RGB24(0x2A6A46));
      fCirc(cx - (int)(r * 0.55f), cy - (int)(r * 0.05f), (int)(r * 0.2f), flash ? C_WHT : KDColor::RGB24(0x3BA05C)); // poings verts
      fCirc(cx + (int)(r * 0.55f), cy - (int)(r * 0.05f), (int)(r * 0.2f), flash ? C_WHT : KDColor::RGB24(0x3BA05C));
      fCirc(cx, hy, hr + 1, flash ? C_WHT : KDColor::RGB24(0x3BA05C));      // masque vert
      fCirc(cx + 1, hy + 1, hr - (hr >= 6 ? (int)(hr * 0.3f) : 2), KDColor::RGB24(0x2A6A46)); // ouverture du masque
      fRect(cx - (int)(hr * 0.8f), hy - (int)(hr * 0.1f), (int)(hr * 1.7f), 2, C_GOLD); // bande doree
      heroEyes(cx + ex + 1, hy + 1 + fy / 2, hr, fx, true);
      if (r >= 6) fRect(cx - 2, hy + (int)(hr * 0.5f), 6, 2, C_BLK);       // bouche serieuse
      break;
    }
    case 6: { // BARLEY : robot barman fin, yeux verts, noeud papillon, bouteille
      uint32_t steel = flash ? 0xFFFFFF : 0x9AB8C9, steelD = flash ? 0xAAAAAA : 0x6E8A9A;
      fRect(cx - (int)(r * 0.22f), cy + (int)(r * 0.05f), (int)(r * 0.44f), (int)(r * 0.95f), KDColor::RGB24(steel)); // corps fin
      fRect(cx - (int)(r * 0.22f), cy + (int)(r * 0.35f), (int)(r * 0.44f), (int)(r * 0.3f), flash ? C_WHT : KDColor::RGB24(0x2A3A4A)); // gilet
      fRect(cx - (int)(r * 0.12f), cy + (int)(r * 0.08f), (int)(r * 0.24f), (int)(r * 0.12f) + 1, flash ? C_WHT : KDColor::RGB24(0xC93030)); // noeud papillon
      fRect(cx - (int)(r * 0.28f), cy - (int)(r * 0.95f), (int)(r * 0.56f), 2, KDColor::RGB24(steelD)); // epaules
      fCirc(cx, hy - 1, (int)(hr * 0.85f), KDColor::RGB24(steel));         // petite tete metallique
      fCirc(cx - (int)(hr * 0.35f), hy - 1 + fy / 2, (hr >= 6 ? (int)(hr * 0.22f) : 2) + 1, KDColor::RGB24(0x3BE84A)); // yeux verts
      fCirc(cx + (int)(hr * 0.35f), hy - 1 + fy / 2, (hr >= 6 ? (int)(hr * 0.22f) : 2) + 1, KDColor::RGB24(0x3BE84A));
      if (r >= 6) fRect(cx - (int)(hr * 0.3f), hy + (int)(hr * 0.35f), (int)(hr * 0.6f), 2, C_BLK); // bouche LED
      fRect(cx + (int)(r * 0.35f), cy + (int)(r * 0.15f), (int)(r * 0.2f), (int)(r * 0.45f), flash ? C_WHT : KDColor::RGB24(0x3B8A4A)); // bouteille verte
      fCirc(cx + (int)(r * 0.45f), cy + (int)(r * 0.1f), (int)(r * 0.12f) + 1, C_GRE); // goulot
      break;
    }
    case 7: { // POCO : crane, sombrero, poncho rose, guitare contre le torse
      fCirc(cx, cy + (int)(r * 0.35f), (int)(r * 0.55f), flash ? C_WHT : KDColor::RGB24(0xC94B6E)); // poncho
      fRect(cx - (int)(r * 0.55f), cy + (int)(r * 0.3f), (int)(r * 1.1f), (int)(r * 0.5f), flash ? C_WHT : KDColor::RGB24(0xC94B6E));
      fRect(cx - (int)(r * 0.55f), cy + (int)(r * 0.5f), (int)(r * 1.1f), 2, C_YEL);  // frange doree
      KDColor bone = KDColor::RGB24(flash ? 0xFFFFFF : 0xF2EAD8);
      fCirc(cx, hy, hr, bone);                                             // crane
      fCirc(cx - (int)(hr * 0.38f), hy + fy / 2, (hr >= 6 ? (int)(hr * 0.28f) : 2) + 1, C_BLK); // orbites
      fCirc(cx + (int)(hr * 0.38f), hy + fy / 2, (hr >= 6 ? (int)(hr * 0.28f) : 2) + 1, C_BLK);
      if (r >= 6) { px(cx - (int)(hr * 0.38f) + 1 + ex, hy + fy / 2, C_WHT); px(cx + (int)(hr * 0.38f) + 1 + ex, hy + fy / 2, C_WHT); }
      if (r >= 6) fRect(cx - 2, hy + (int)(hr * 0.55f), 5, 2, C_BLK);      // bouche dente
      fRect(cx - (int)(r * 0.9f), hy - (int)(hr * 0.35f), (int)(r * 1.8f), 3, flash ? C_WHT : KDColor::RGB24(0xC9703B)); // bord du sombrero
      fCirc(cx, hy - (int)(hr * 0.5f), (int)(hr * 0.75f), flash ? C_WHT : KDColor::RGB24(0xC9703B)); // calotte
      fRect(cx - (int)(r * 0.9f), hy - (int)(hr * 0.35f), (int)(r * 1.8f), 1, C_GOLD); // bande doree
      fCirc(cx + (int)(r * 0.3f), cy + (int)(r * 0.3f), (int)(r * 0.3f), KDColor::RGB24(0x8A5A2A)); // caisse de guitare
      fRect(cx + (int)(r * 0.27f), cy - (int)(r * 0.1f), (int)(r * 0.08f) + 1, (int)(r * 0.4f), KDColor::RGB24(0x6A4A2A)); // manche
      break;
    }
    case 8: { // ROSA : afro violet, gants de boxe rouges aux bras tendus
      heroBody(cx, cy, r, flash, 0x7A4AA0, 0x6A3A8A, 0x3A2050, walk);
      KDColor arm = KDColor::RGB24(flash ? 0xFFFFFF : 0x7A4AA0);
      fRect(cx - (int)(r * 0.62f), cy + (int)(r * 0.05f), (int)(r * 0.4f), (int)(r * 0.14f) + 1, arm); // bras tendus
      fRect(cx + (int)(r * 0.22f), cy + (int)(r * 0.05f), (int)(r * 0.4f), (int)(r * 0.14f) + 1, arm);
      fCirc(cx - (int)(r * 0.6f), cy + (int)(r * 0.1f), (int)(r * 0.22f) + 1, flash ? C_WHT : KDColor::RGB24(0xD93B3B)); // gants rouges
      fCirc(cx + (int)(r * 0.6f), cy + (int)(r * 0.1f), (int)(r * 0.22f) + 1, flash ? C_WHT : KDColor::RGB24(0xD93B3B));
      fCirc(cx, hy, hr, flash ? C_WHT : KDColor::RGB24(0x9A6844));          // visage
      KDColor afro = KDColor::RGB24(0x7A3BB0);
      fCirc(cx, hy - (int)(hr * 0.25f), hr + 1, afro);                      // afro violet
      fCirc(cx - (int)(hr * 0.7f), hy - (int)(hr * 0.05f), (int)(hr * 0.6f), afro);
      fCirc(cx + (int)(hr * 0.7f), hy - (int)(hr * 0.05f), (int)(hr * 0.6f), afro);
      heroEyes(cx + ex, hy + 2 + fy / 2, hr, fx, true);
      if (r >= 6) fRect(cx - 2, hy + (int)(hr * 0.5f), 5, 1, C_BLK);
      break;
    }
    case 9: { // JESSIE : casquette rouge, couette, chemise jaune, fusil bricole
      heroBody(cx, cy, r, flash, 0xE8C93D, 0xC9A528, 0x6A5A20, walk);
      fCirc(cx, hy, hr, flash ? C_WHT : KDColor::RGB24(0xB07850));          // visage
      fRect(cx + hr - 2, hy - 2, 3, hr, KDColor::RGB24(0x8A5838));          // couette derriere
      fCirc(cx, hy - (int)(hr * 0.45f), (int)(hr * 0.72f), flash ? C_WHT : KDColor::RGB24(0xD93B3B)); // casquette
      fRect(cx, hy - (int)(hr * 0.35f), hr + 4, 2, flash ? C_GRE : KDColor::RGB24(0xB02A2A)); // visiere
      heroEyes(cx + ex, hy + 1 + fy / 2, hr, fx, false);
      if (r >= 6) fRect(cx - 2, hy + (int)(hr * 0.5f), 5, 1, C_BLK);
      fRect(cx + (int)(r * 0.3f), cy + (int)(r * 0.15f), (int)(r * 0.7f), (int)(r * 0.13f) + 1, C_GRE); // canon
      fRect(cx + (int)(r * 0.3f), cy + (int)(r * 0.02f), (int)(r * 0.25f), (int)(r * 0.12f) + 1, KDColor::RGB24(0x8A5A2A)); // crosse bricolee
      break;
    }
    case 10: { // DYNAMIKE : prospecteur, barbe blanche, casque de mineur, baton de dynamite
      heroBody(cx, cy, r, flash, 0x8A6A4A, 0x6A4A30, 0x3A2A1A, walk);
      fCirc(cx, hy, hr, flash ? C_WHT : KDColor::RGB24(0xC9A070));          // visage
      fRect(cx - (int)(hr * 0.75f), hy + (int)(hr * 0.25f), (int)(hr * 1.5f), (int)(hr * 0.75f), flash ? C_WHT : KDColor::RGB24(0xE8E0D0)); // grande barbe blanche
      heroEyes(cx + ex, hy + fy / 2, hr, fx, false);
      fCirc(cx, hy - (int)(hr * 0.5f), (int)(hr * 0.7f), flash ? C_WHT : KDColor::RGB24(0x4A5A6A));  // casque de mineur
      fRect(cx - hr - 1, hy - (int)(hr * 0.3f), hr * 2 + 2, 2, KDColor::RGB24(0x3A4A5A)); // bord du casque
      fCirc(cx + (int)(hr * 0.55f), hy - (int)(hr * 0.45f), 2, C_YEL);      // lampe frontale
      fRect(cx + (int)(r * 0.45f), cy + (int)(r * 0.15f), (int)(r * 0.12f) + 1, (int)(r * 0.4f), flash ? C_WHT : KDColor::RGB24(0xD93B3B)); // baton de dynamite
      px(cx + (int)(r * 0.5f), cy + (int)(r * 0.1f), C_GRE); px(cx + (int)(r * 0.52f), cy + (int)(r * 0.06f), C_YEL); // meche + etincelle
      break;
    }
    case 11: { // TICK : petite tete robot ronde, yeux enormes, cle remontoir
      fRect(cx - (int)(r * 0.25f), cy, (int)(r * 0.5f) + 1, (int)(r * 0.9f), flash ? C_WHT : KDColor::RGB24(0x5A6A7A));       // petit corps
      fRect(cx - (int)(r * 0.32f), cy + (int)(r * 0.85f), (int)(r * 0.18f) + 1, (int)(r * 0.15f) + 1, KDColor::RGB24(0x3A4A5A)); // pieds
      fRect(cx + (int)(r * 0.14f), cy + (int)(r * 0.85f), (int)(r * 0.18f) + 1, (int)(r * 0.15f) + 1, KDColor::RGB24(0x3A4A5A));
      fRect(cx - 1, hy - hr - 3, 3, 5, C_GRE);                             // tige de la cle
      fRect(cx - 4, hy - hr - 5, 9, 3, C_GRE);                             // cle remontoir
      fCirc(cx, hy + 1, hr, flash ? C_WHT : KDColor::RGB24(0xB0C4D8));      // tete ronde acier
      fCirc(cx - (int)(hr * 0.4f), hy + 1 + fy / 2, (hr >= 6 ? (int)(hr * 0.28f) : 3) + 1, C_WHT); // yeux enormes
      fCirc(cx + (int)(hr * 0.4f), hy + 1 + fy / 2, (hr >= 6 ? (int)(hr * 0.28f) : 3) + 1, C_WHT);
      if (r >= 6) { px(cx - (int)(hr * 0.4f) + 1, hy + 1 + fy / 2, C_BLK); px(cx + (int)(hr * 0.4f) + 1, hy + 1 + fy / 2, C_BLK); }
      if (r >= 6) fRect(cx - 2, hy + (int)(hr * 0.6f), 5, 2, C_BLK);        // sourire
      fRect(cx - (int)(r * 0.4f), cy - (int)(r * 0.1f), (int)(r * 0.8f) + 1, 2, KDColor::RGB24(0x8A9AAC)); // epaules
      break;
    }
    case 12: { // 8-BIT : grosse tete carree arcade, yeux pixel jaunes, antenne
      heroBody(cx, cy, r, flash, 0x6A6A7A, 0x4A4A5A, 0x2A2A3A, walk);
      fRect(cx - hr, hy - (int)(hr * 0.9f), hr * 2, (int)(hr * 1.8f) + 1, flash ? C_WHT : KDColor::RGB24(0x8A8A9A)); // tete carree grise
      fRect(cx - hr, hy - (int)(hr * 0.9f), hr * 2, 2, C_GRE);
      fRect(cx - (int)(hr * 0.55f), hy - (int)(hr * 0.2f) + fy / 2, 3, 3, C_YEL); // yeux pixel jaunes
      fRect(cx + (int)(hr * 0.2f), hy - (int)(hr * 0.2f) + fy / 2, 3, 3, C_YEL);
      if (r >= 6) fRect(cx - (int)(hr * 0.45f), hy + (int)(hr * 0.45f), (int)(hr * 0.9f) + 1, 2, C_BLK); // bouche = fente
      fRect(cx - 1, hy - (int)(hr * 1.9f), 3, hr, KDColor::RGB24(0x4A4A5A)); // antenne
      fCirc(cx + 1, hy - (int)(hr * 1.9f) - 1, 2, flash ? C_WHT : KDColor::RGB24(0xE8447A)); // boule rouge
      fRect(cx + (int)(r * 0.3f), cy + (int)(r * 0.1f), (int)(r * 0.75f) + 1, (int)(r * 0.16f) + 1, flash ? C_WHT : KDColor::RGB24(0x4A4A5A)); // gros laser
      fRect(cx + (int)(r * 0.95f), cy + (int)(r * 0.05f), (int)(r * 0.25f) + 1, (int)(r * 0.26f) + 1, C_YEL); // muzzle jaune
      break;
    }
    case 13: { // RICO : robot violet, tete allongee, banane de dents, long canon
      heroBody(cx, cy, r, flash, 0x9A7AB8, 0x7A5A98, 0x4A3A5A, walk);
      KDColor ric = KDColor::RGB24(flash ? 0xFFFFFF : 0x9A7AB8);
      fRect(cx - (int)(hr * 0.6f), hy - (int)(hr * 0.8f), (int)(hr * 1.2f) + 1, (int)(hr * 2.1f) + 1, ric);   // tete allongee violette
      fRect(cx - (int)(hr * 0.6f), hy - (int)(hr * 0.8f), (int)(hr * 1.2f) + 1, 2, flash ? C_GRE : KDColor::RGB24(0xB89AD8));
      fRect(cx - (int)(hr * 0.4f), hy - (int)(hr * 0.2f) + fy / 2, 3, 3, C_WHT); // yeux carres
      fRect(cx + (int)(hr * 0.1f), hy - (int)(hr * 0.2f) + fy / 2, 3, 3, C_WHT);
      if (r >= 6) fRect(cx - (int)(hr * 0.45f), hy + (int)(hr * 0.5f), (int)(hr * 0.9f) + 1, 3, C_WHT); // banane de dents
      fRect(cx + (int)(r * 0.25f), cy + (int)(r * 0.15f), (int)(r * 0.95f) + 1, (int)(r * 0.12f) + 1, C_GRE); // long canon
      fRect(cx + (int)(r * 1.1f), cy + (int)(r * 0.08f), (int)(r * 0.2f) + 1, (int)(r * 0.26f) + 1, KDColor::RGB24(0x7A5A98)); // embout evase
      break;
    }
    case 14: { // DARRYL : robot DANS un tonneau de bois
      fRect(cx - (int)(r * 0.5f), cy - (int)(r * 0.05f), r, (int)(r * 0.95f) + 1, flash ? C_WHT : KDColor::RGB24(0x8A5A2A)); // tonneau bois
      fRect(cx - (int)(r * 0.5f), cy - (int)(r * 0.05f), r, 2, KDColor::RGB24(0x6A4A2A));
      fRect(cx - (int)(r * 0.5f), cy + (int)(r * 0.3f), r, 2, C_GRE);       // cerclage metal
      fRect(cx - (int)(r * 0.5f), cy + (int)(r * 0.75f), r, 2, C_GRE);
      fRect(cx - (int)(r * 0.16f), cy - (int)(r * 0.05f), 2, (int)(r * 0.95f), KDColor::RGB24(0x6A4A2A)); // planches
      fRect(cx + (int)(r * 0.16f), cy - (int)(r * 0.05f), 2, (int)(r * 0.95f), KDColor::RGB24(0x6A4A2A));
      fRect(cx - (int)(r * 0.34f), cy + (int)(r * 0.9f), (int)(r * 0.3f) + 1, (int)(r * 0.15f) + 1, KDColor::RGB24(0x3A4A5A)); // petits pieds
      fRect(cx + (int)(r * 0.06f), cy + (int)(r * 0.9f), (int)(r * 0.3f) + 1, (int)(r * 0.15f) + 1, KDColor::RGB24(0x3A4A5A));
      fCirc(cx - (int)(r * 0.62f), cy + (int)(r * 0.1f), (int)(r * 0.16f), flash ? C_WHT : KDColor::RGB24(0x9AB8C9)); // bras dehors
      fCirc(cx + (int)(r * 0.62f), cy + (int)(r * 0.1f), (int)(r * 0.16f), flash ? C_WHT : KDColor::RGB24(0x9AB8C9));
      fCirc(cx, hy, (int)(hr * 0.8f), flash ? C_WHT : KDColor::RGB24(0xB0C4D8)); // tete acier
      fCirc(cx - (int)(hr * 0.3f), hy + fy / 2, (hr >= 6 ? (int)(hr * 0.22f) : 2) + 1, C_WHT); // yeux lunette
      fCirc(cx + (int)(hr * 0.3f), hy + fy / 2, (hr >= 6 ? (int)(hr * 0.22f) : 2) + 1, C_WHT);
      if (r >= 6) { px(cx - (int)(hr * 0.3f) + 1, hy + fy / 2, C_BLK); px(cx + (int)(hr * 0.3f) + 1, hy + fy / 2, C_BLK); }
      if (r >= 6) fRect(cx - 2, hy + (int)(hr * 0.5f), 5, 2, C_BLK);
      break;
    }
    case 15: { // PENNY : pirate, chapeau a badge or, mousquet a sac de pieces
      heroBody(cx, cy, r, flash, 0x3A7A9A, 0x2A5A7A, 0x2A3A4A, walk);
      fCirc(cx, hy, hr, flash ? C_WHT : KDColor::RGB24(0xC98850));          // visage
      heroEyes(cx + ex, hy + fy / 2, hr, fx, false);
      if (r >= 6) fRect(cx - 2, hy + (int)(hr * 0.5f), 5, 1, C_BLK);
      if (r >= 6) { px(cx - (int)(hr * 0.55f), hy + (int)(hr * 0.2f), KDColor::RGB24(0xE880A8)); px(cx + (int)(hr * 0.6f), hy + (int)(hr * 0.2f), KDColor::RGB24(0xE880A8)); } // joues roses
      fRect(cx - hr - 2, hy - (int)(hr * 0.35f), hr * 2 + 4, 3, flash ? C_WHT : KDColor::RGB24(0x2A3A5A)); // large chapeau
      fCirc(cx, hy - (int)(hr * 0.55f), (int)(hr * 0.65f), flash ? C_WHT : KDColor::RGB24(0x2A3A5A));
      fCirc(cx + (int)(hr * 0.3f), hy - (int)(hr * 0.5f), 2, C_GOLD);       // badge or
      fRect(cx + (int)(r * 0.3f), cy + (int)(r * 0.15f), (int)(r * 0.8f) + 1, (int)(r * 0.12f) + 1, KDColor::RGB24(0x6A4A2A)); // mousquet
      fCirc(cx + (int)(r * 1.05f), cy + (int)(r * 0.15f), (int)(r * 0.16f), flash ? C_WHT : C_GOLD); // sac de pieces
      break;
    }
    case 16: { // CARL : casque bleu, crane ras, grand sourire, pioche sur l'epaule
      heroBody(cx, cy, r, flash, 0x3A8A8A, 0x2A6A6A, 0x3A4A5A, walk);
      fCirc(cx, hy, hr, flash ? C_WHT : KDColor::RGB24(0xC98850));           // crane rase
      heroEyes(cx + ex, hy - 1 + fy / 2, hr, fx, false);
      if (r >= 6) { fRect(cx - 3, hy + (int)(hr * 0.35f), 7, 3, C_WHT); fRect(cx - 2, hy + (int)(hr * 0.35f), 5, 1, C_BLK); } // grand sourire
      fCirc(cx, hy - (int)(hr * 0.5f), (int)(hr * 0.72f), flash ? C_WHT : KDColor::RGB24(0x3568B0)); // casque bleu
      fRect(cx - hr, hy - (int)(hr * 0.4f), hr * 2, 3, KDColor::RGB24(0x2A4A8A)); // bande
      fRect(cx - (int)(r * 0.15f), cy - (int)(r * 0.25f), (int)(r * 0.14f) + 1, (int)(r * 0.85f), KDColor::RGB24(0x6A4A2A)); // manche pioche
      fRect(cx - (int)(r * 0.35f), cy - (int)(r * 0.35f), (int)(r * 0.55f) + 1, (int)(r * 0.14f) + 1, C_GRE); // tete de pioche
      break;
    }
    case 17: { // JACKY : casque de chantier jaune, combinee bleue, marteau-piqueur
      heroBody(cx, cy, r, flash, 0x3568B0, 0x2A4A8A, 0x2A3A5A, walk);
      fCirc(cx, hy, hr, flash ? C_WHT : KDColor::RGB24(0xC98850));
      heroEyes(cx + ex, hy + fy / 2, hr, fx, true);                         // regard determine
      if (r >= 6) fRect(cx - 2, hy + (int)(hr * 0.5f), 5, 1, C_BLK);
      fCirc(cx, hy - (int)(hr * 0.45f), (int)(hr * 0.75f), flash ? C_WHT : C_YEL); // casque jaune
      fRect(cx - hr - 2, hy - (int)(hr * 0.25f), hr * 2 + 4, 3, flash ? C_GRE : C_YEL); // bord du casque
      fRect(cx + (int)(r * 0.4f), cy - (int)(r * 0.1f), (int)(r * 0.2f) + 1, (int)(r * 0.7f), C_GRE); // marteau-piqueur
      fRect(cx + (int)(r * 0.33f), cy + (int)(r * 0.55f), (int)(r * 0.34f) + 1, (int)(r * 0.14f) + 1, C_GRE);
      px(cx + (int)(r * 0.5f), cy - (int)(r * 0.15f), C_RED);
      break;
    }
    case 18: { // BO : capuche verte, bandeau sur les yeux, arc et fleches
      heroBody(cx, cy, r, flash, 0x3A8A4A, 0x2A6A38, 0x2A4A2A, walk);
      fCirc(cx, hy, hr + 1, flash ? C_WHT : KDColor::RGB24(0x2A6A3A));      // capuche
      fCirc(cx, hy + 1, hr - 2, flash ? C_WHT : KDColor::RGB24(0x9A6844));  // visage
      fRect(cx - (int)(hr * 0.45f), hy + 1 + fy / 2, (int)(hr * 0.9f) + 1, 2, KDColor::RGB24(0x7A5A3A)); // bandeau sur les yeux
      if (r >= 6) { px(cx - (int)(hr * 0.3f) + ex, hy + 1 + fy / 2, C_BLK); px(cx + (int)(hr * 0.35f) + ex, hy + 1 + fy / 2, C_BLK); }
      if (r >= 6) fRect(cx - 2, hy + (int)(hr * 0.55f), 5, 1, C_BLK);
      fRect(cx + (int)(r * 0.5f), cy - (int)(r * 0.2f), (int)(r * 0.1f) + 1, (int)(r * 0.7f), KDColor::RGB24(0x6A4A2A)); // fleches (carquois)
      fRect(cx + (int)(r * 0.42f), cy - (int)(r * 0.35f), (int)(r * 0.26f) + 1, (int)(r * 0.12f) + 1, KDColor::RGB24(0xE8E0D0)); // empennes
      fCirc(cx + (int)(r * 0.4f), cy + (int)(r * 0.15f), (int)(r * 0.3f), KDColor::RGB24(0x6A4A2A)); // arc en main
      break;
    }
    case 19: { // EMZ : longue criniere rose, robe rose, telephone a la main
      heroBody(cx, cy, r, flash, 0xE85A8A, 0xC94B6E, 0x3A2050, walk);
      KDColor pink = KDColor::RGB24(flash ? 0xFFFFFF : 0xD85AA8);
      fRect(cx - hr - 2, hy - 2, 3, (int)(r * 1.2f), pink);                  // meches longues
      fRect(cx + hr - 1, hy - 2, 3, (int)(r * 1.2f), pink);
      fCirc(cx, hy - (int)(hr * 0.25f), hr + 1, pink);                      // criniere
      fCirc(cx - (int)(hr * 0.7f), hy - (int)(hr * 0.05f), (int)(hr * 0.6f), pink);
      fCirc(cx + (int)(hr * 0.7f), hy - (int)(hr * 0.05f), (int)(hr * 0.6f), pink);
      fCirc(cx, hy, hr, flash ? C_WHT : KDColor::RGB24(0x9A6844));          // visage par-dessus
      heroEyes(cx + ex, hy + fy / 2, hr, fx, false);
      if (r >= 6) fRect(cx - 2, hy + (int)(hr * 0.5f), 5, 1, C_BLK);
      fRect(cx + (int)(r * 0.35f), cy + (int)(r * 0.1f), (int)(r * 0.22f) + 1, (int)(r * 0.35f), KDColor::RGB24(0x2A2A3A)); // telephone
      fRect(cx + (int)(r * 0.37f), cy + (int)(r * 0.12f), (int)(r * 0.18f) + 1, (int)(r * 0.28f), KDColor::RGB24(0x3BE84A)); // ecran
      break;
    }
    case 20: { // STU : robot de course, casque violet a visiere, yeux jaunes lumineux
      heroBody(cx, cy, r, flash, 0x3A5A8A, 0x2A4A6A, 0x1A2A4A, walk);
      fCirc(cx, hy, hr, flash ? C_WHT : KDColor::RGB24(0x9AB8C9));          // visage robot
      fRect(cx - (int)(hr * 0.7f), hy - (int)(hr * 0.2f) + fy / 2, (int)(hr * 1.5f), (int)(hr * 0.55f) + 1, KDColor::RGB24(0x2A2A4A)); // visiere sombre
      fRect(cx - (int)(hr * 0.45f), hy + fy / 2, 3, 3, C_YEL);               // yeux lumineux dans la visiere
      fRect(cx + (int)(hr * 0.15f), hy + fy / 2, 3, 3, C_YEL);
      if (r >= 6) fRect(cx - (int)(hr * 0.5f), hy + (int)(hr * 0.45f), hr, 3, C_GRE); // bouche grille
      fCirc(cx, hy - (int)(hr * 0.65f), (int)(hr * 0.7f), flash ? C_WHT : KDColor::RGB24(0x7A3BB0)); // casque violet
      fRect(cx - (int)(hr * 0.8f), hy - (int)(hr * 0.9f), 4, 4, KDColor::RGB24(0x7A3BB0)); // flamme arriere
      fRect(cx + (int)(hr * 0.4f), hy - (int)(hr * 0.95f), 4, 4, KDColor::RGB24(0x7A3BB0));
      fRect(cx + (int)(r * 0.35f), cy + (int)(r * 0.1f), (int)(r * 0.7f) + 1, (int)(r * 0.12f) + 1, C_GRE); // arme laser
      fCirc(cx + r, cy + (int)(r * 0.16f), (int)(r * 0.14f) + 1, flash ? C_WHT : KDColor::RGB24(0x7A3BB0)); // muzzle violet
      break;
    }
    case 21: { // PAM : maman mecano, frange orange haute + bun, robe turquoise, cle a molette
      heroBody(cx, cy, r, flash, 0x3AA89A, 0x2A8A7A, 0x2A4A4A, walk);
      KDColor hair = KDColor::RGB24(flash ? 0xFFFFFF : 0xE87A2A);
      fRect(cx - (int)(hr * 0.9f), hy - (int)(hr * 0.1f), 3, (int)(hr * 1.3f), hair); // meches cotes
      fRect(cx + (int)(hr * 0.7f), hy - (int)(hr * 0.1f), 3, (int)(hr * 1.3f), hair);
      fCirc(cx, hy - (int)(hr * 0.55f), (int)(hr * 0.7f), hair);            // frange haute
      fCirc(cx, hy - (int)(hr * 1.15f), (int)(hr * 0.35f), hair);           // bun au-dessus
      fCirc(cx, hy, hr, flash ? C_WHT : KDColor::RGB24(0xB07850));          // visage
      heroEyes(cx + ex, hy + fy / 2, hr, fx, false);
      if (r >= 6) fRect(cx - 2, hy + (int)(hr * 0.5f), 5, 1, C_BLK);
      fRect(cx + (int)(r * 0.4f), cy + (int)(r * 0.05f), (int)(r * 0.14f) + 1, (int)(r * 0.6f), C_GRE); // manche de la cle
      fRect(cx + (int)(r * 0.32f), cy - (int)(r * 0.05f), (int)(r * 0.3f) + 1, (int)(r * 0.14f) + 1, C_GRE); // tete de cle
      break;
    }
    case 22: { // FRANK : monstre zombie geant, capuche, marteau enorme
      int big = r + 2;                                                     // il est plus grand
      fRect(cx - (int)(r * 0.45f), cy + (int)(r * 0.5f), (int)(r * 0.38f) + 1, (int)(r * 0.55f), KDColor::RGB24(0x2A4A6A)); // grosses jambes
      fRect(cx + (int)(r * 0.07f), cy + (int)(r * 0.5f), (int)(r * 0.38f) + 1, (int)(r * 0.55f), KDColor::RGB24(0x2A4A6A));
      fCirc(cx, cy + (int)(r * 0.15f), (int)(big * 0.5f), flash ? C_WHT : KDColor::RGB24(0x3568B0)); // gros torse (hoodie)
      fRect(cx - (int)(big * 0.5f), cy + (int)(r * 0.25f), big, (int)(r * 0.45f), KDColor::RGB24(0x2A4A8A));
      fCirc(cx - (int)(big * 0.62f), cy + (int)(r * 0.05f), (int)(r * 0.24f), flash ? C_WHT : KDColor::RGB24(0xC9D86A)); // mains jaunes zombie
      fCirc(cx + (int)(big * 0.62f), cy + (int)(r * 0.05f), (int)(r * 0.24f), flash ? C_WHT : KDColor::RGB24(0xC9D86A));
      fCirc(cx, hy, hr + 2, flash ? C_WHT : KDColor::RGB24(0x3568B0));      // capuche bleue
      fCirc(cx, hy + 1, hr - 2, flash ? C_WHT : KDColor::RGB24(0xC9D86A)); // visage jaune-vert zombie
      fRect(cx - (int)(hr * 0.55f), hy - 2 + fy / 2, 4, 4, C_BLK);         // yeux ronds noirs
      fRect(cx + (int)(hr * 0.2f), hy - 2 + fy / 2, 4, 4, C_BLK);
      if (r >= 6) fRect(cx - (int)(hr * 0.4f), hy + (int)(hr * 0.45f), (int)(hr * 0.8f) + 1, 3, C_BLK); // bouche cousee
      px(cx - (int)(hr * 0.3f), hy + (int)(hr * 0.45f), KDColor::RGB24(0x3568B0)); px(cx - (int)(hr * 0.1f), hy + (int)(hr * 0.5f), KDColor::RGB24(0x3568B0)); // points de suture
      fRect(cx + (int)(r * 0.5f), cy - (int)(r * 0.25f), (int)(r * 0.12f) + 1, (int)(r * 0.85f), KDColor::RGB24(0x6A4A2A)); // manche du marteau
      fRect(cx + (int)(r * 0.28f), cy - (int)(r * 0.45f), (int)(r * 0.6f), (int)(r * 0.35f), C_GRE); // tete du marteau
      break;
    }
    case 23: { // BIBI : couettes roses, bulle de chewing-gum, batte de baseball
      heroBody(cx, cy, r, flash, 0xC93050, 0xA82040, 0x2A2A4A, walk);
      KDColor pink = KDColor::RGB24(flash ? 0xFFFFFF : 0xE87AB8);
      fCirc(cx - (int)(hr * 0.85f), hy - (int)(hr * 0.3f), (int)(hr * 0.5f), pink); // couettes roses
      fCirc(cx + (int)(hr * 0.85f), hy - (int)(hr * 0.3f), (int)(hr * 0.5f), pink);
      fRect(cx - (int)(hr * 0.95f), hy - (int)(hr * 0.4f), 3, (int)(hr * 1.2f), pink);
      fRect(cx + (int)(hr * 0.92f), hy - (int)(hr * 0.4f), 3, (int)(hr * 1.2f), pink);
      fCirc(cx + (int)(r * 0.75f), hy - (int)(hr * 0.3f), (int)(r * 0.18f), flash ? C_WHT : KDColor::RGB24(0xE87AB8)); // bulle de chewing-gum
      fCirc(cx, hy, hr, flash ? C_WHT : KDColor::RGB24(0x9A6844));
      heroEyes(cx + ex, hy + fy / 2, hr, fx, false);
      if (r >= 6) fRect(cx - 2, hy + (int)(hr * 0.5f), 5, 1, C_BLK);
      fRect(cx + (int)(r * 0.4f), cy, (int)(r * 0.14f) + 1, (int)(r * 0.7f), KDColor::RGB24(0xC9A05C)); // manche batte
      fRect(cx + (int)(r * 0.52f), cy - (int)(r * 0.15f), (int)(r * 0.3f), (int)(r * 0.35f), flash ? C_WHT : KDColor::RGB24(0xC9A05C)); // batte
      break;
    }
    case 24: { // BEA : rayures d'abeille, bonnet jaune, antennes, petite abeille
      heroBody(cx, cy, r, flash, 0xFFC93D, 0xE8B020, 0x2A2A3A, walk);
      fRect(cx - (int)(r * 0.45f), cy + (int)(r * 0.25f), (int)(r * 0.9f) + 1, 2, C_BLK); // rayures noires
      fRect(cx - (int)(r * 0.4f), cy + (int)(r * 0.45f), (int)(r * 0.8f) + 1, 2, C_BLK);
      fCirc(cx, hy, hr, flash ? C_WHT : KDColor::RGB24(0x9A6844));
      heroEyes(cx + ex, hy + fy / 2, hr, fx, false);
      if (r >= 6) fRect(cx - 2, hy + (int)(hr * 0.5f), 5, 1, C_BLK);
      fRect(cx - hr, hy - (int)(hr * 0.4f), hr * 2, 3, flash ? C_GRE : C_YEL); // bonnet jaune
      fRect(cx - (int)(hr * 0.7f), hy - hr, 2, (int)(hr * 0.7f), C_GRE);      // antennes
      fRect(cx + (int)(hr * 0.6f), hy - hr, 2, (int)(hr * 0.7f), C_GRE);
      fCirc(cx - (int)(r * 0.75f), cy - (int)(r * 0.55f), (int)(r * 0.16f), flash ? C_WHT : KDColor::RGB24(0xFFC93D)); // petite abeille
      fRect(cx - (int)(r * 0.78f), cy - (int)(r * 0.5f), (int)(r * 0.2f), 2, C_BLK);
      fRect(cx - (int)(r * 0.75f), cy - (int)(r * 0.62f), (int)(r * 0.1f) + 1, 2, KDColor::RGB24(0xAAAAAA)); // ailes
      break;
    }
    case 25: { // NANI : robot grand-mere, oeil cyclope a iris bleu, robe violette
      heroBody(cx, cy, r, flash, 0x7A5A9A, 0x5A3A7A, 0x3A2A4A, walk);
      fRect(cx - (int)(r * 0.5f), cy + (int)(r * 0.35f), r, (int)(r * 0.3f) + 1, KDColor::RGB24(0x9A7AB8)); // col de robe
      fCirc(cx, hy, hr, flash ? C_WHT : KDColor::RGB24(0xB0C4D8));           // tete acier ronde
      fCirc(cx + 1, hy + fy / 2, (hr >= 6 ? (int)(hr * 0.35f) : 3), C_WHT);  // grand oeil unique
      fCirc(cx + 2, hy + fy / 2, 2, flash ? C_GRE : KDColor::RGB24(0x3BA0D9)); // iris bleu
      px(cx + 2, hy + fy / 2, C_BLK);
      if (r >= 6) fRect(cx - (int)(hr * 0.6f), hy + (int)(hr * 0.5f), (int)(hr * 1.2f), 2, C_GRE); // bouche
      fRect(cx - 1, hy - hr - 3, 3, 4, C_GRE);                              // antenne
      fCirc(cx, hy - hr - 4, 2, flash ? C_WHT : KDColor::RGB24(0xC94B6E));   // boule rose
      fRect(cx + (int)(r * 0.45f), cy + (int)(r * 0.1f), (int)(r * 0.5f) + 1, (int)(r * 0.1f) + 1, KDColor::RGB24(0x9A7AB8)); // petard laser
      fCirc(cx + (int)(r * 0.9f), cy + (int)(r * 0.15f), (int)(r * 0.12f) + 1, C_YEL);
      break;
    }
    case 26: { // EDGAR : capuche sombre, echarpe sur le bas du visage, frange sur le front
      heroBody(cx, cy, r, flash, 0x2A3A6A, 0x1A2A4A, 0x1A1A2A, walk);
      fCirc(cx, hy, hr + 2, flash ? C_WHT : KDColor::RGB24(0x22305A));       // capuche sombre
      fRect(cx - (int)(hr * 0.8f), hy - (int)(hr * 0.3f), (int)(hr * 1.7f), (int)(hr * 0.75f) + 1, KDColor::RGB24(0x3A4A7A)); // visage couvert
      fRect(cx - (int)(hr * 0.45f), hy - (int)(hr * 0.15f) + fy / 2, 3, 3, C_WHT); // seuls yeux visibles
      fRect(cx + (int)(hr * 0.15f), hy - (int)(hr * 0.15f) + fy / 2, 3, 3, C_WHT);
      fRect(cx - (int)(hr * 0.95f), hy - (int)(hr * 0.5f), 4, (int)(hr * 1.1f), KDColor::RGB24(0x1A1A2A)); // frange
      fRect(cx + (int)(hr * 0.55f), hy - (int)(hr * 0.5f), 4, (int)(hr * 1.1f), KDColor::RGB24(0x1A1A2A));
      fRect(cx - hr, hy + (int)(hr * 0.3f), hr * 2, 2, KDColor::RGB24(0x22305A)); // bas d'echarpe
      fRect(cx + (int)(r * 0.4f), cy + (int)(r * 0.2f), (int)(r * 0.12f) + 1, (int)(r * 0.35f), C_GRE); // dague
      fRect(cx + (int)(r * 0.3f), cy + (int)(r * 0.5f), (int)(r * 0.3f) + 1, 2, C_GRE);
      break;
    }
    case 27: { // GRIFF : tete de pigeon grise, gros sourcils, cravate, pieces d'or
      heroBody(cx, cy, r, flash, 0x8A5838, 0x6A4228, 0x2A2A3A, walk);
      fCirc(cx, hy, hr + 1, flash ? C_WHT : KDColor::RGB24(0x9AA8B8));       // tete grise de pigeon
      fRect(cx - (int)(hr * 0.3f), hy - (int)(hr * 0.4f), (int)(hr * 0.9f), 4, KDColor::RGB24(0x6A7888)); // sourcils
      fCirc(cx - (int)(hr * 0.35f), hy + fy / 2, 3, C_WHT); px(cx - (int)(hr * 0.35f) + 1, hy + fy / 2, C_BLK); // oeil gauche
      fCirc(cx + (int)(hr * 0.35f), hy + fy / 2, 3, C_WHT); px(cx + (int)(hr * 0.35f) + 1, hy + fy / 2, C_BLK);
      fRect(cx + (int)(hr * 0.5f), hy + (int)(hr * 0.25f), 6, 4, flash ? C_WHT : KDColor::RGB24(0xE8A03D)); // bec orange
      fRect(cx - 2, cy + (int)(r * 0.05f), 5, (int)(r * 0.35f), KDColor::RGB24(0xC93030)); // cravate rouge
      fCirc(cx - (int)(r * 0.55f), cy + (int)(r * 0.3f), 3, flash ? C_WHT : C_GOLD); // pieces d'or
      fCirc(cx - (int)(r * 0.4f), cy + (int)(r * 0.45f), 3, flash ? C_WHT : C_GOLD);
      fCirc(cx + (int)(r * 0.65f), cy + (int)(r * 0.1f), 3, flash ? C_WHT : C_GOLD);
      break;
    }
    case 28: { // GALE : chevelure orange, col de fourrure, souffleuse a neige
      heroBody(cx, cy, r, flash, 0x3A8A9A, 0x2A6A78, 0x2A4A4A, walk);
      fRect(cx - (int)(r * 0.5f), cy - (int)(r * 0.05f), r, (int)(r * 0.25f), flash ? C_WHT : KDColor::RGB24(0xE8E0D0)); // col de fourrure
      KDColor hair = KDColor::RGB24(flash ? 0xFFFFFF : 0xE8552A);
      fCirc(cx, hy - (int)(hr * 0.35f), hr + 1, hair);                       // chevelure orange (AVANT le visage)
      fCirc(cx - (int)(hr * 0.7f), hy - (int)(hr * 0.1f), (int)(hr * 0.6f), hair);
      fCirc(cx + (int)(hr * 0.7f), hy - (int)(hr * 0.1f), (int)(hr * 0.6f), hair);
      fCirc(cx, hy, hr, flash ? C_WHT : KDColor::RGB24(0x9A6844));           // visage
      heroEyes(cx + ex, hy + fy / 2, hr, fx, false);
      if (r >= 6) fRect(cx - 2, hy + (int)(hr * 0.5f), 5, 1, C_BLK);
      fRect(cx + (int)(r * 0.35f), cy + (int)(r * 0.1f), (int)(r * 0.8f) + 1, (int)(r * 0.14f) + 1, C_GRE); // souffleuse
      fCirc(cx + (int)(r * 1.1f), cy + (int)(r * 0.17f), (int)(r * 0.14f) + 1, flash ? C_GRE : C_WHT); // cone de soufflage
      break;
    }
    case 29: { // COLETTE : fan-girl, grosses lunettes rondes, carnet orange
      heroBody(cx, cy, r, flash, 0x8A4A9A, 0x6A3A7A, 0x2A2A3A, walk);
      KDColor hair = KDColor::RGB24(flash ? 0xFFFFFF : 0x5A3A7A);
      fRect(cx - (int)(hr * 0.9f), hy - (int)(hr * 0.3f), 3, (int)(hr * 1.4f), hair); // meches cotes
      fRect(cx + (int)(hr * 0.9f), hy - (int)(hr * 0.3f), 3, (int)(hr * 1.4f), hair);
      fCirc(cx, hy - (int)(hr * 0.55f), (int)(hr * 0.75f), hair);            // chevelure (AVANT le visage)
      fCirc(cx, hy, hr, flash ? C_WHT : KDColor::RGB24(0xD8A878));           // visage pale
      fCirc(cx - (int)(hr * 0.38f), hy + fy / 2, (hr >= 6 ? (int)(hr * 0.3f) : 3), C_WHT); // lunettes rondes
      fCirc(cx + (int)(hr * 0.38f), hy + fy / 2, (hr >= 6 ? (int)(hr * 0.3f) : 3), C_WHT);
      px(cx - (int)(hr * 0.38f) + 1, hy + fy / 2, C_BLK); px(cx + (int)(hr * 0.38f) + 1, hy + fy / 2, C_BLK);
      fRect(cx - (int)(hr * 0.1f), hy - 1 + fy / 2, (int)(hr * 0.2f) + 1, 2, C_BLK); // pont des lunettes
      if (r >= 6) fRect(cx - 2, hy + (int)(hr * 0.55f), 5, 1, C_RED);        // sourire enthousiaste
      fRect(cx + (int)(r * 0.4f), cy + (int)(r * 0.15f), (int)(r * 0.3f), (int)(r * 0.4f), flash ? C_WHT : KDColor::RGB24(0xE8552A)); // carnet orange
      fRect(cx + (int)(r * 0.42f), cy + (int)(r * 0.17f), (int)(r * 0.26f), (int)(r * 0.36f), KDColor::RGB24(0xFFF5E8)); // pages
      break;
    }
    case 30: { // BELLE : tireuse d'elite, long manteau gris-bleu, meche violette, grappin
      heroBody(cx, cy, r, flash, 0x4A5A78, 0x3A4A66, 0x2A2A3A, walk);
      fRect(cx - (int)(r * 0.5f), cy + (int)(r * 0.05f), r, (int)(r * 0.8f), flash ? C_WHT : KDColor::RGB24(0x3A4A66)); // long manteau
      fRect(cx - (int)(r * 0.16f), cy - (int)(r * 0.05f), (int)(r * 0.32f), (int)(r * 0.35f), flash ? C_WHT : KDColor::RGB24(0xF2EAD8)); // col clair
      if (r >= 10) { px(cx, cy + (int)(r * 0.2f), C_WHT); px(cx, cy + (int)(r * 0.35f), C_WHT); px(cx, cy + (int)(r * 0.5f), C_WHT); } // boutons
      fRect(cx + (int)(r * 0.35f), cy + (int)(r * 0.15f), (int)(r * 0.25f), (int)(r * 0.14f) + 1, flash ? C_WHT : KDColor::RGB24(0x4A5A78)); // bras
      fRect(cx + (int)(r * 0.55f), cy + (int)(r * 0.05f), (int)(r * 0.4f), 2, C_GRE);  // corde tendue du grappin
      fCirc(cx + (int)(r * 0.95f), cy + (int)(r * 0.02f), (int)(r * 0.13f) + 1, C_GOLD); // grappin or
      fCirc(cx, hy, hr, flash ? C_WHT : KDColor::RGB24(0xF2C898));                      // visage clair
      fRect(cx - hr, hy - (int)(hr * 0.55f), hr * 2, (int)(hr * 0.75f), flash ? C_WHT : KDColor::RGB24(0x2A2A3A)); // cheveux haut
      fRect(cx - hr, hy - (int)(hr * 0.5f), (int)(hr * 0.42f), (int)(hr * 1.5f), flash ? C_WHT : KDColor::RGB24(0x2A2A3A)); // cote gauche
      fRect(cx + (int)(hr * 0.58f), hy - (int)(hr * 0.5f), (int)(hr * 0.42f), (int)(hr * 1.5f), flash ? C_WHT : KDColor::RGB24(0x2A2A3A)); // cote droit
      fRect(cx - (int)(hr * 0.6f), hy - (int)(hr * 0.6f), (int)(hr * 0.5f), 2, flash ? C_WHT : KDColor::RGB24(0x8A3AA8)); // meche violette
      heroEyes(cx + ex, hy + 1, hr, fx, false);
      if (r >= 6) fRect(cx - 1, hy + (int)(hr * 0.45f), 4, 1, C_RED);                    // levres
      fRect(cx - (int)(hr * 0.9f), hy - (int)(hr * 0.35f), (int)(hr * 1.8f), 2, C_GRE);  // lunettes de visee sur le front
      break;
    }
    case 31: { // BUZZ : robot de securite, casquette jaune, visiere a yeux verts, matraque
      heroBody(cx, cy, r, flash, 0x3A5A8A, 0x2A4668, 0x1A2A3E, walk);
      fRect(cx + (int)(r * 0.45f), cy + (int)(r * 0.35f), (int)(r * 0.5f), (int)(r * 0.13f) + 1, flash ? C_WHT : KDColor::RGB24(0xE8C93D)); // bras jaune
      fRect(cx - (int)(r * 0.75f), cy + (int)(r * 0.3f), (int)(r * 0.3f), (int)(r * 0.13f) + 1, C_GRE); // bras metal gauche
      fCirc(cx, hy, hr + 1, flash ? C_WHT : KDColor::RGB24(0xB8C4D8));                   // tete metal
      fRect(cx - hr, hy - 1, hr * 2 + 1, (int)(hr * 0.9f) + 1, flash ? C_WHT : KDColor::RGB24(0x1A2A3E)); // visiere noire
      fCirc(cx - (int)(hr * 0.4f), hy + 1, (int)(hr * 0.24f) + 1, KDColor::RGB24(0x3BE84A)); // yeux verts
      fCirc(cx + (int)(hr * 0.4f), hy + 1, (int)(hr * 0.24f) + 1, KDColor::RGB24(0x3BE84A));
      fCirc(cx, hy - (int)(hr * 0.65f), (int)(hr * 0.7f), flash ? C_WHT : KDColor::RGB24(0xE8C93D)); // casquette jaune
      fRect(cx - (int)(hr * 0.8f), hy - (int)(hr * 0.45f), (int)(hr * 1.7f), 3, flash ? C_WHT : KDColor::RGB24(0xE8C93D)); // visiere casquette
      fRect(cx + (int)(r * 0.6f), cy + (int)(r * 0.05f), (int)(r * 0.14f) + 1, (int)(r * 0.5f), C_GRE); // manche matraque
      fRect(cx + (int)(r * 0.45f), cy - (int)(r * 0.05f), (int)(r * 0.44f), (int)(r * 0.16f) + 1, C_GRE); // matraque
      px(cx + (int)(r * 0.98f), cy - (int)(r * 0.25f), C_YEL); px(cx + (int)(r * 1.05f), cy - (int)(r * 0.15f), C_YEL); px(cx + (int)(r * 0.92f), cy - (int)(r * 0.02f), C_YEL); // etincelles
      break;
    }
    case 32: { // ASH : balayeur raleur, casquette verte, balai, rat sur l'epaule
      heroBody(cx, cy, r, flash, 0x3A8A8A, 0x2A6A6A, 0x2A3A4A, walk);
      fRect(cx + (int)(r * 0.3f), cy + (int)(r * 0.25f), (int)(r * 0.6f), (int)(r * 0.1f) + 1, KDColor::RGB24(0x8A6A3A)); // manche balai
      fRect(cx + (int)(r * 0.72f), cy + (int)(r * 0.32f), (int)(r * 0.42f), (int)(r * 0.22f) + 1, flash ? C_WHT : KDColor::RGB24(0xC9A05C)); // brosse
      fCirc(cx + (int)(r * 0.42f), cy + (int)(r * 0.3f), (int)(r * 0.13f) + 1, flash ? C_WHT : KDColor::RGB24(0xD8A878)); // main
      fCirc(cx, hy, hr, flash ? C_WHT : KDColor::RGB24(0xD8A878));                       // tete renfrognee
      fCirc(cx, hy - (int)(hr * 0.5f), (int)(hr * 0.75f), flash ? C_WHT : KDColor::RGB24(0x2A5A2A)); // casquette verte
      fRect(cx, hy - (int)(hr * 0.35f), hr + 4, 3, flash ? C_WHT : KDColor::RGB24(0x2A5A2A)); // visiere
      heroEyes(cx + ex, hy + 2, hr, fx, true);
      if (r >= 6) fRect(cx - 2, hy + (int)(hr * 0.55f), 5, 2, C_BLK);                    // bouche mauvaise humeur
      fCirc(cx - (int)(r * 0.5f), cy - (int)(r * 0.3f), (int)(r * 0.16f) + 1, flash ? C_WHT : KDColor::RGB24(0x9A9AA8)); // rat corps
      fCirc(cx - (int)(r * 0.38f), cy - (int)(r * 0.38f), (int)(r * 0.1f) + 1, flash ? C_WHT : KDColor::RGB24(0x9A9AA8)); // tete rat
      px(cx - (int)(r * 0.36f), cy - (int)(r * 0.4f), C_RED);                            // oeil rouge du rat
      px(cx - (int)(r * 0.64f), cy - (int)(r * 0.24f), KDColor::RGB24(0x9A9AA8));        // queue
      break;
    }
    case 33: { // LOLA : star de cinema, boucles roses, robe rouge, plume
      fRect(cx - (int)(r * 0.55f), cy + (int)(r * 0.05f), (int)(r * 1.1f), (int)(r * 0.7f), flash ? C_WHT : KDColor::RGB24(0xC93050)); // robe
      fCirc(cx, cy + (int)(r * 0.75f), (int)(r * 0.5f), flash ? C_WHT : KDColor::RGB24(0xC93050)); // bas arrondi
      fRect(cx - (int)(r * 0.3f), cy + (int)(r * 0.08f), (int)(r * 0.6f), (int)(r * 0.14f), flash ? C_WHT : KDColor::RGB24(0xF2EAD8)); // col blanc
      fCirc(cx, hy, hr, flash ? C_WHT : KDColor::RGB24(0xF2C898));                       // visage
      KDColor pink = KDColor::RGB24(flash ? 0xFFFFFF : 0xE8447A);
      fCirc(cx - (int)(hr * 0.95f), hy - (int)(hr * 0.1f), (int)(hr * 0.55f), pink);     // boucles cote gauche
      fCirc(cx + (int)(hr * 0.95f), hy - (int)(hr * 0.1f), (int)(hr * 0.55f), pink);     // boucles cote droit
      fCirc(cx - (int)(hr * 0.5f), hy - (int)(hr * 0.7f), (int)(hr * 0.55f), pink);      // boucle haut gauche
      fCirc(cx + (int)(hr * 0.5f), hy - (int)(hr * 0.7f), (int)(hr * 0.55f), pink);      // boucle haut droit
      fCirc(cx, hy - (int)(hr * 0.85f), (int)(hr * 0.6f), pink);                         // boucle sommet
      heroEyes(cx + ex, hy + 1, hr, fx, false);
      if (r >= 6) fRect(cx - 2, hy + (int)(hr * 0.5f), 5, 2, C_RED);                     // rouge a levres
      fRect(cx + (int)(hr * 0.9f), hy - (int)(hr * 1.5f), 2, 6, C_YEL); px(cx + (int)(hr * 0.9f), hy - (int)(hr * 1.6f), C_YEL); // plume
      if (r >= 6) { px(cx - (int)(hr * 0.3f), hy + (int)(hr * 1.2f), C_WHT); px(cx, hy + (int)(hr * 1.25f), C_WHT); px(cx + (int)(hr * 0.3f), hy + (int)(hr * 1.2f), C_WHT); } // collier de perles
      break;
    }
    case 34: { // FANG : karateka, bandeau noir, gi blanc, jambe qui frappe
      heroBody(cx, cy, r, flash, 0xF2EAD8, 0xE8E0D0, 0xE8E0D0, walk);
      fRect(cx - (int)(r * 0.22f), cy + (int)(r * 0.15f), (int)(r * 0.44f) + 1, (int)(r * 0.16f) + 1, KDColor::RGB24(0xCC2222)); // ceinture
      fRect(cx - (int)(r * 0.05f), cy + (int)(r * 0.1f), (int)(r * 0.1f) + 1, (int)(r * 0.25f), KDColor::RGB24(0xCC2222)); // noeud
      fRect(cx + (int)(r * 0.1f), cy + (int)(r * 0.5f), (int)(r * 0.55f), (int)(r * 0.15f) + 1, flash ? C_WHT : KDColor::RGB24(0xE8E0D0)); // jambe qui frappe
      fRect(cx + (int)(r * 0.6f), cy + (int)(r * 0.45f), (int)(r * 0.2f) + 1, (int)(r * 0.25f) + 1, flash ? C_WHT : KDColor::RGB24(0xF2EAD8)); // pied
      fCirc(cx, hy, hr, flash ? C_WHT : KDColor::RGB24(0xF2C898));
      fRect(cx - (int)(hr * 0.9f), hy - (int)(hr * 0.5f), (int)(hr * 1.85f), 3, C_BLK); // bandeau front
      fRect(cx - (int)(hr * 1.25f), hy - (int)(hr * 0.45f), 4, (int)(hr * 0.8f), C_BLK); // pans flottants
      heroEyes(cx + ex, hy + 2, hr, fx, true);
      if (r >= 6) fRect(cx - 2, hy + (int)(hr * 0.55f), 5, 2, C_BLK);
      break;
    }
    case 35: { // MAISIE : tenniswoman, couette rousse, bandeau bleu, raquette a tamis
      heroBody(cx, cy, r, flash, 0xF2EAD8, 0xE8E0D0, 0x3568B0, walk);
      fCirc(cx + (int)(r * 0.62f), cy - (int)(r * 0.1f), (int)(r * 0.24f) + 1, flash ? C_WHT : KDColor::RGB24(0x9A9AA8)); // tamis
      if (r >= 10) { px(cx + (int)(r * 0.55f), cy - (int)(r * 0.15f), C_BLK); px(cx + (int)(r * 0.62f), cy - (int)(r * 0.15f), C_BLK); px(cx + (int)(r * 0.69f), cy - (int)(r * 0.15f), C_BLK);
        px(cx + (int)(r * 0.55f), cy - (int)(r * 0.08f), C_BLK); px(cx + (int)(r * 0.62f), cy - (int)(r * 0.08f), C_BLK); px(cx + (int)(r * 0.69f), cy - (int)(r * 0.08f), C_BLK); } // cordes
      fRect(cx + (int)(r * 0.58f), cy + (int)(r * 0.12f), (int)(r * 0.08f) + 1, (int)(r * 0.3f), C_GRE); // manche raquette
      fRect(cx + (int)(r * 0.3f), cy + (int)(r * 0.2f), (int)(r * 0.3f), (int)(r * 0.12f) + 1, flash ? C_WHT : KDColor::RGB24(0xF2EAD8)); // bras
      fCirc(cx, hy, hr, flash ? C_WHT : KDColor::RGB24(0xF2C898));
      KDColor red = KDColor::RGB24(flash ? 0xFFFFFF : 0xC9552A);
      fCirc(cx, hy - (int)(hr * 0.4f), (int)(hr * 0.75f), red);                          // cheveux
      fRect(cx + (int)(hr * 0.75f), hy - (int)(hr * 0.3f), (int)(hr * 0.5f), (int)(hr * 1.2f), red); // couette qui descend
      fCirc(cx + (int)(hr * 0.95f), hy + (int)(hr * 0.7f), (int)(hr * 0.4f), red);       // bout de couette
      fRect(cx - hr, hy - (int)(hr * 0.55f), hr * 2, 2, flash ? C_WHT : KDColor::RGB24(0x3B6EA5)); // bandeau front
      heroEyes(cx + ex, hy + 1, hr, fx, true);
      if (r >= 6) fRect(cx - 2, hy + (int)(hr * 0.55f), 5, 1, C_BLK);
      break;
    }
    case 36: { // SQUEAK : alien rose, grandes oreilles, balle orange
      fCirc(cx, cy + (int)(r * 0.3f), (int)(r * 0.42f), flash ? C_WHT : KDColor::RGB24(0xE87AB8)); // corps rond
      fRect(cx - (int)(r * 0.25f), cy + (int)(r * 0.62f), (int)(r * 0.2f) + 1, (int)(r * 0.18f) + 1, KDColor::RGB24(0xC94B6E)); // pieds
      fRect(cx + (int)(r * 0.05f), cy + (int)(r * 0.62f), (int)(r * 0.2f) + 1, (int)(r * 0.18f) + 1, KDColor::RGB24(0xC94B6E));
      fCirc(cx - (int)(hr * 0.95f), hy, (int)(hr * 0.55f), flash ? C_WHT : KDColor::RGB24(0xE87AB8)); // grande oreille gauche
      fCirc(cx + (int)(hr * 0.95f), hy, (int)(hr * 0.55f), flash ? C_WHT : KDColor::RGB24(0xE87AB8)); // grande oreille droite
      fCirc(cx, hy, hr, flash ? C_WHT : KDColor::RGB24(0xF2A0C8));                       // tete rose clair
      fCirc(cx - (int)(hr * 0.95f), hy, (int)(hr * 0.25f) + 1, flash ? C_WHT : KDColor::RGB24(0xE8447A)); // oreille interne
      fCirc(cx + (int)(hr * 0.95f), hy, (int)(hr * 0.25f) + 1, flash ? C_WHT : KDColor::RGB24(0xE8447A));
      heroEyes(cx + ex, hy + 1, hr, fx, false);
      if (r >= 6) fRect(cx - 2, hy + (int)(hr * 0.5f), 5, 2, C_BLK);
      fRect(cx + (int)(r * 0.3f), cy + (int)(r * 0.25f), (int)(r * 0.25f), (int)(r * 0.1f) + 1, flash ? C_WHT : KDColor::RGB24(0xE87AB8)); // bras
      fCirc(cx + (int)(r * 0.62f), cy + (int)(r * 0.18f), (int)(r * 0.17f) + 1, flash ? C_WHT : KDColor::RGB24(0xE8772A)); // balle orange
      break;
    }
    case 37: { // HANK : maitre-nageur, bouee rose a 2 patchs blancs, sifflet rouge
      heroBody(cx, cy, r, flash, 0x3A8AC9, 0x2A6AA8, 0x2A3A5A, walk);
      fCirc(cx, cy + (int)(r * 0.32f), (int)(r * 0.6f), flash ? C_WHT : KDColor::RGB24(0xF2A0C8)); // bouee rose claire
      fCirc(cx, cy + (int)(r * 0.32f), (int)(r * 0.44f), KDColor::RGB24(flash ? 0xAAAAAA : 0x2A6AA8)); // trou (torse)
      fRect(cx - (int)(r * 0.09f), cy - (int)(r * 0.28f), (int)(r * 0.18f) + 1, (int)(r * 0.13f) + 1, C_WHT); // patch haut
      fRect(cx - (int)(r * 0.09f), cy + (int)(r * 0.78f), (int)(r * 0.18f) + 1, (int)(r * 0.13f) + 1, C_WHT); // patch bas
      fRect(cx - (int)(r * 0.78f), cy - (int)(r * 0.1f), (int)(r * 0.26f), (int)(r * 0.34f), flash ? C_WHT : KDColor::RGB24(0xF2C898)); // bras gauche
      fRect(cx + (int)(r * 0.52f), cy - (int)(r * 0.1f), (int)(r * 0.26f), (int)(r * 0.34f), flash ? C_WHT : KDColor::RGB24(0xF2C898)); // bras droit
      fCirc(cx - (int)(r * 0.68f), cy - (int)(r * 0.16f), (int)(r * 0.15f) + 1, flash ? C_WHT : KDColor::RGB24(0xF2C898)); // poing gauche
      fCirc(cx + (int)(r * 0.68f), cy - (int)(r * 0.16f), (int)(r * 0.15f) + 1, flash ? C_WHT : KDColor::RGB24(0xF2C898)); // poing droit
      fCirc(cx, hy, hr + 1, flash ? C_WHT : KDColor::RGB24(0xF2C898));  // grosse tete sympathique
      heroEyes(cx + ex, hy + 1, hr, fx, false);
      if (r >= 6) fRect(cx - 2, hy + (int)(hr * 0.5f), 5, 2, C_BLK);
      if (r >= 10) { px(cx - (int)(hr * 0.4f), hy + 2, C_GRE); px(cx + (int)(hr * 0.4f), hy + 2, C_GRE); } // sourcils plats
      fRect(cx + (int)(r * 0.12f), hy + (int)(hr * 0.95f), (int)(r * 0.22f), 2, C_GRE); // cordon du sifflet
      fCirc(cx + (int)(r * 0.36f), hy + hr, (int)(r * 0.12f) + 1, flash ? C_WHT : KDColor::RGB24(0xE8444E)); // sifflet rouge
      fRect(cx + (int)(r * 0.42f), hy + hr - 1, (int)(r * 0.16f) + 1, 3, flash ? C_WHT : KDColor::RGB24(0xE8444E));
      break;
    }
    case 38: { // PEARL : boulangere, toque blanche compacte, tablier, poele
      heroBody(cx, cy, r, flash, 0xE8772A, 0xC9552A, 0x3A2A2A, walk);
      fRect(cx - (int)(r * 0.35f), cy + (int)(r * 0.15f), (int)(r * 0.7f) + 1, (int)(r * 0.5f), flash ? C_WHT : KDColor::RGB24(0xF2EAD8)); // tablier
      fRect(cx + (int)(r * 0.35f), cy + (int)(r * 0.2f), (int)(r * 0.3f), (int)(r * 0.12f) + 1, flash ? C_WHT : KDColor::RGB24(0xE8772A)); // bras
      fRect(cx + (int)(r * 0.6f), cy + (int)(r * 0.22f), (int)(r * 0.3f), (int)(r * 0.08f) + 1, C_GRE); // manche poele
      fCirc(cx + (int)(r * 0.95f), cy + (int)(r * 0.25f), (int)(r * 0.24f) + 1, flash ? C_WHT : KDColor::RGB24(0x2A2A3A)); // poele noire
      fCirc(cx, hy, hr, flash ? C_WHT : KDColor::RGB24(0xF2C898));
      KDColor white = KDColor::RGB24(flash ? 0xAAAAAA : 0xF2EAD8);
      fCirc(cx, hy - (int)(hr * 0.8f), (int)(hr * 0.55f), white);                        // toque (petite)
      fRect(cx - (int)(hr * 0.55f), hy - (int)(hr * 0.95f), (int)(hr * 1.1f), (int)(hr * 0.5f), white);
      fRect(cx - (int)(hr * 0.55f), hy - (int)(hr * 0.5f), (int)(hr * 1.1f), 2, white);  // bande de la toque
      heroEyes(cx + ex, hy + 1, hr, fx, false);
      if (r >= 6) fRect(cx - 2, hy + (int)(hr * 0.5f), 5, 1, C_BLK);
      fRect(cx - (int)(hr * 0.8f), hy + (int)(hr * 0.8f), (int)(hr * 1.6f), 3, flash ? C_WHT : KDColor::RGB24(0xE8772A)); // foulard sous le menton
      break;
    }
    case 39: { // SPIKE : cactus kawaii, grosse tete contour sombre, epines jaunes, fleur
      fRect(cx - (int)(r * 0.18f), cy + (int)(r * 0.25f), (int)(r * 0.36f) + 1, (int)(r * 0.55f), flash ? C_WHT : KDColor::RGB24(0x3AA05C)); // corps
      fRect(cx - (int)(r * 0.26f), cy + (int)(r * 0.75f), (int)(r * 0.2f) + 1, (int)(r * 0.16f) + 1, KDColor::RGB24(0x2A6A3A)); // pieds
      fRect(cx + (int)(r * 0.06f), cy + (int)(r * 0.75f), (int)(r * 0.2f) + 1, (int)(r * 0.16f) + 1, KDColor::RGB24(0x2A6A3A));
      fRect(cx - (int)(r * 0.42f), cy + (int)(r * 0.1f), (int)(r * 0.16f) + 1, (int)(r * 0.3f), flash ? C_WHT : KDColor::RGB24(0x3AA05C)); // bras gauche
      fRect(cx + (int)(r * 0.26f), cy + (int)(r * 0.1f), (int)(r * 0.16f) + 1, (int)(r * 0.3f), flash ? C_WHT : KDColor::RGB24(0x3AA05C)); // bras droit
      fCirc(cx, hy, hr + 1, KDColor::RGB24(0x2A6A3A));               // contour sombre
      fCirc(cx, hy, hr - 1, flash ? C_WHT : KDColor::RGB24(0x3AA05C)); // grosse tete
      for (int a = 0; a < 8; a++) {                                    // epines-diamants jaunes
        float an = a * 0.7854f + 0.3927f;
        int sx = cx + (int)(fcosf(an) * (hr + 1)), sy = hy + (int)(fsinf(an) * (hr + 1));
        fRect(sx - 1, sy - 1, 3, 3, flash ? C_WHT : KDColor::RGB24(0xFFE05A));
      }
      if (r >= 6) { // yeux kawaii avec reflets
        fCirc(cx - (int)(hr * 0.35f), hy + 1, 3, C_WHT); fCirc(cx + (int)(hr * 0.35f), hy + 1, 3, C_WHT);
        px(cx - (int)(hr * 0.35f) + 1, hy + 1, C_BLK); px(cx + (int)(hr * 0.35f) + 1, hy + 1, C_BLK);
      } else { px(cx - 2, hy, C_WHT); px(cx + 2, hy, C_WHT); }
      fRect(cx - 2, hy + (int)(hr * 0.4f), 5, 2, C_BLK);               // grand sourire
      if (r >= 10) { px(cx - 3, hy + (int)(hr * 0.4f) + 2, C_WHT); px(cx + 3, hy + (int)(hr * 0.4f) + 2, C_WHT); } // crocs
      for (int a = 0; a < 4; a++) {                                     // fleur : 4 petals roses
        float an = a * 1.5708f + 0.7854f;
        fCirc(cx + (int)(fcosf(an) * r * 0.13f), hy - hr - 3 + (int)(fsinf(an) * r * 0.13f), (int)(r * 0.13f) + 1, flash ? C_WHT : KDColor::RGB24(0xE8447A));
      }
      fCirc(cx, hy - hr - 3, (int)(r * 0.1f) + 1, C_YEL);              // coeur jaune
      break;
    }
    case 40: { // CROW : corbeau ninja, capuche sombre, bec jaune, couteaux
      heroBody(cx, cy, r, flash, 0x3E22A0, 0x5C3BE0, 0x1A1A2A, walk);
      fCirc(cx, hy, hr + 2, flash ? C_WHT : KDColor::RGB24(0x3E22A0)); // capuche
      fCirc(cx, hy + 1, hr - 1, flash ? C_WHT : KDColor::RGB24(0xF2C898)); // visage
      fRect(cx - hr, hy - (int)(hr * 0.45f), hr * 2, (int)(hr * 0.9f), flash ? C_WHT : KDColor::RGB24(0x3E22A0)); // capuche au-dessus des yeux
      fRect(cx - (int)(hr * 0.5f), hy - (int)(hr * 0.8f), hr, 2, flash ? C_WHT : KDColor::RGB24(0x5050A0)); // meche
      heroEyes(cx + ex, hy + 2, hr, fx, false);
      fRect(cx - 1, hy + (int)(hr * 0.35f), 3, (int)(hr * 0.4f), flash ? C_WHT : KDColor::RGB24(0xE8C93D)); // bec
      fRect(cx - (int)(r * 0.75f), cy + (int)(r * 0.1f), (int)(r * 0.1f) + 1, (int)(r * 0.35f), C_GRE); // couteau gauche
      px(cx - (int)(r * 0.72f), cy + (int)(r * 0.06f), C_WHT);
      fRect(cx + (int)(r * 0.68f), cy + (int)(r * 0.1f), (int)(r * 0.1f) + 1, (int)(r * 0.35f), C_GRE); // couteau droit
      px(cx + (int)(r * 0.71f), cy + (int)(r * 0.06f), C_WHT);
      break;
    }
    case 41: { // LEON : cameleon, capuche verte, echarpe rose, shuriken
      heroBody(cx, cy, r, flash, 0x3BE07B, 0x22A050, 0x1E7A38, walk);
      fRect(cx - (int)(r * 0.45f), cy + (int)(r * 0.05f), (int)(r * 0.9f), (int)(r * 0.16f), flash ? C_WHT : KDColor::RGB24(0xE8447A)); // echarpe
      fRect(cx - (int)(r * 0.5f), cy + (int)(r * 0.1f), (int)(r * 0.2f), (int)(r * 0.3f), flash ? C_WHT : KDColor::RGB24(0xE8447A)); // pan
      fCirc(cx, hy, hr + 1, flash ? C_WHT : KDColor::RGB24(0x22A050));  // capuche
      fCirc(cx, hy + 1, hr - 1, flash ? C_WHT : KDColor::RGB24(0xF2C898)); // visage
      fRect(cx - hr, hy - (int)(hr * 0.4f), hr * 2, (int)(hr * 0.8f), flash ? C_WHT : KDColor::RGB24(0x22A050)); // cheveux
      fRect(cx - (int)(hr * 0.9f), hy - (int)(hr * 0.6f), (int)(hr * 0.4f), 3, flash ? C_WHT : KDColor::RGB24(0x3BE07B)); // pointes
      fRect(cx + (int)(hr * 0.5f), hy - (int)(hr * 0.7f), (int)(hr * 0.5f), 3, flash ? C_WHT : KDColor::RGB24(0x3BE07B));
      heroEyes(cx + ex, hy + 2, hr, fx, true);
      if (r >= 6) fRect(cx - 2, hy + (int)(hr * 0.5f), 5, 2, C_BLK);   // sourire malin
      int sx = cx + (int)(r * 0.62f), sy = cy + (int)(r * 0.2f);       // shuriken 4 branches
      fRect(sx - 2, sy - (int)(r * 0.16f), 4, (int)(r * 0.32f), C_GRE);
      fRect(sx - (int)(r * 0.16f), sy - 2, (int)(r * 0.32f), 4, C_GRE);
      fCirc(sx, sy, (int)(r * 0.06f) + 1, C_WHT);
      fRect(cx + (int)(r * 0.3f), cy + (int)(r * 0.15f), (int)(r * 0.3f), (int)(r * 0.12f) + 1, flash ? C_WHT : KDColor::RGB24(0xF2C898)); // bras
      break;
    }
    case 42: { // SANDY : endormi, grande robe violette, capuche, yeux mi-clos
      fRect(cx - (int)(r * 0.5f), cy + (int)(r * 0.05f), r, (int)(r * 0.75f), flash ? C_WHT : KDColor::RGB24(0x7B7BE0)); // robe
      fRect(cx - (int)(r * 0.55f), cy + (int)(r * 0.35f), (int)(r * 1.1f), (int)(r * 0.1f) + 1, flash ? C_WHT : KDColor::RGB24(0x5050A0)); // bord
      fCirc(cx, hy, hr + 2, flash ? C_WHT : KDColor::RGB24(0x5050A0)); // capuche
      fCirc(cx, hy + 1, hr - 1, flash ? C_WHT : KDColor::RGB24(0xF2C898)); // visage
      fRect(cx - hr + 1, hy - (int)(hr * 0.35f), hr * 2 - 2, (int)(hr * 0.7f), flash ? C_WHT : KDColor::RGB24(0x5050A0)); // frange
      if (r >= 6) { // yeux mi-clos endormis
        fRect(cx - (int)(hr * 0.5f), hy + 1, 4, 2, C_BLK); fRect(cx + (int)(hr * 0.5f) - 3, hy + 1, 4, 2, C_BLK);
        px(cx - (int)(hr * 0.45f), hy + 2, C_WHT); px(cx + (int)(hr * 0.45f), hy + 2, C_WHT);
      } else { px(cx - 2, hy + 1, C_BLK); px(cx + 2, hy + 1, C_BLK); }
      fRect(cx - 1, hy + (int)(hr * 0.45f), 3, 1, C_BLK);             // bouche endormie
      px(cx + hr, hy - hr, C_YEL); px(cx + (int)(hr * 0.5f), hy - hr - 4, C_YEL); px(cx - (int)(hr * 0.8f), hy - hr - 3, C_YEL); // etoiles
      break;
    }
    case 43: { // AMBER : danseuse du feu, cheveux dorees, torche enflammee
      heroBody(cx, cy, r, flash, 0xE8C93D, 0xC9552A, 0x3A2A2A, walk);
      fRect(cx + (int)(r * 0.42f), cy + (int)(r * 0.05f), (int)(r * 0.12f) + 1, (int)(r * 0.4f), KDColor::RGB24(0x8A6A3A)); // manche torche
      fCirc(cx + (int)(r * 0.48f), cy - (int)(r * 0.15f), (int)(r * 0.2f) + 1, flash ? C_WHT : KDColor::RGB24(0xE8772A)); // flamme
      fCirc(cx + (int)(r * 0.48f), cy - (int)(r * 0.22f), (int)(r * 0.12f) + 1, flash ? C_WHT : KDColor::RGB24(0xFFE05A)); // coeur
      fRect(cx + (int)(r * 0.25f), cy + (int)(r * 0.15f), (int)(r * 0.25f), (int)(r * 0.12f) + 1, flash ? C_WHT : KDColor::RGB24(0xF2C898)); // bras
      fCirc(cx, hy, hr, flash ? C_WHT : KDColor::RGB24(0xF2C898));
      fCirc(cx, hy - (int)(hr * 0.35f), (int)(hr * 0.8f), flash ? C_WHT : KDColor::RGB24(0xF2A03C)); // chevelure doree
      fRect(cx - hr - 1, hy - (int)(hr * 0.3f), 3, (int)(hr * 1.4f), flash ? C_WHT : KDColor::RGB24(0xF2A03C)); // meche gauche
      fRect(cx + hr - 2, hy - (int)(hr * 0.3f), 3, (int)(hr * 1.4f), flash ? C_WHT : KDColor::RGB24(0xF2A03C)); // meche droite
      heroEyes(cx + ex, hy + 1, hr, fx, false);
      if (r >= 6) fRect(cx - 2, hy + (int)(hr * 0.5f), 5, 1, C_RED);
      if (r >= 10) { px(cx - (int)(r * 0.3f), cy + (int)(r * 0.35f), KDColor::RGB24(0xE8772A)); px(cx - (int)(r * 0.1f), cy + (int)(r * 0.4f), KDColor::RGB24(0xE8772A)); px(cx + (int)(r * 0.1f), cy + (int)(r * 0.35f), KDColor::RGB24(0xE8772A)); px(cx + (int)(r * 0.3f), cy + (int)(r * 0.4f), KDColor::RGB24(0xE8772A)); } // liseré de feu
      break;
    }
    case 44: { // GENE : genie violet, barbe grise, turban dore, main magique
      fRect(cx - (int)(r * 0.45f), cy + (int)(r * 0.05f), (int)(r * 0.9f), (int)(r * 0.75f), flash ? C_WHT : KDColor::RGB24(0x7A4AA0)); // robe
      fRect(cx - (int)(r * 0.2f), cy + (int)(r * 0.1f), (int)(r * 0.4f), (int)(r * 0.4f), flash ? C_WHT : KDColor::RGB24(0xE8E0D0)); // plastron
      fCirc(cx, hy, hr, flash ? C_WHT : KDColor::RGB24(0xF2C898));
      fRect(cx - (int)(hr * 0.8f), hy + (int)(hr * 0.2f), (int)(hr * 1.6f) + 1, (int)(hr * 0.7f), flash ? C_WHT : KDColor::RGB24(0xD8D8E8)); // barbe
      fCirc(cx, hy - (int)(hr * 0.6f), (int)(hr * 0.75f), flash ? C_WHT : KDColor::RGB24(0xE8B84A)); // turban
      fRect(cx - (int)(hr * 0.8f), hy - (int)(hr * 0.7f), (int)(hr * 1.6f) + 1, 3, flash ? C_WHT : KDColor::RGB24(0xE8B84A));
      px(cx, hy - (int)(hr * 1.1f), C_YEL);                           // bijou
      if (r >= 6) { fCirc(cx - (int)(hr * 0.35f), hy + 1, 2, C_WHT); fCirc(cx + (int)(hr * 0.35f), hy + 1, 2, C_WHT); px(cx - (int)(hr * 0.35f) + 1, hy + 1, C_BLK); px(cx + (int)(hr * 0.35f) + 1, hy + 1, C_BLK); }
      fRect(cx - 2, hy + (int)(hr * 0.3f), 5, 2, C_BLK);               // sourire dans la barbe
      fRect(cx + (int)(r * 0.3f), cy + (int)(r * 0.2f), (int)(r * 0.3f), (int)(r * 0.12f) + 1, flash ? C_WHT : KDColor::RGB24(0x7A4AA0)); // bras
      fCirc(cx + (int)(r * 0.62f), cy + (int)(r * 0.15f), (int)(r * 0.14f) + 1, flash ? C_WHT : KDColor::RGB24(0x6AE87A)); // main magique verte
      break;
    }
    case 45: { // MORTIS : vampire, costume blanc, haut-de-forme, pelle, yeux rouges
      heroBody(cx, cy, r, flash, 0xF2EAD8, 0xE8E0D0, 0x1A1A2A, walk);
      fRect(cx - (int)(r * 0.15f), cy + (int)(r * 0.1f), (int)(r * 0.3f), (int)(r * 0.3f), flash ? C_WHT : KDColor::RGB24(0x111111)); // noeud papillon
      fRect(cx - (int)(r * 0.3f), cy + (int)(r * 0.12f), (int)(r * 0.6f), 2, C_BLK); // col
      fRect(cx + (int)(r * 0.5f), cy - (int)(r * 0.1f), (int)(r * 0.1f) + 1, (int)(r * 0.5f), KDColor::RGB24(0x8A6A3A)); // manche pelle
      fRect(cx + (int)(r * 0.36f), cy + (int)(r * 0.4f), (int)(r * 0.4f), (int)(r * 0.16f) + 1, C_GRE); // fer de la pelle
      fRect(cx + (int)(r * 0.3f), cy - (int)(r * 0.15f), (int)(r * 0.25f), (int)(r * 0.1f) + 1, flash ? C_WHT : KDColor::RGB24(0xF2EAD8)); // bras
      fCirc(cx, hy, hr, flash ? C_WHT : KDColor::RGB24(0xE8E8F2));     // visage pale
      fRect(cx - (int)(hr * 0.7f), hy - (int)(hr * 0.55f), (int)(hr * 1.4f) + 1, (int)(hr * 0.9f), C_BLK); // calotte
      fRect(cx - hr, hy - (int)(hr * 0.2f), hr * 2 + 1, 3, C_BLK);    // bord du chapeau
      if (r >= 6) { fCirc(cx - (int)(hr * 0.35f), hy + 1, 2, flash ? C_WHT : KDColor::RGB24(0xE83B3B)); fCirc(cx + (int)(hr * 0.35f), hy + 1, 2, flash ? C_WHT : KDColor::RGB24(0xE83B3B)); }
      else { px(cx - 2, hy + 1, C_RED); px(cx + 2, hy + 1, C_RED); }
      fRect(cx - 1, hy + (int)(hr * 0.45f), 3, 2, C_BLK);             // bouche
      if (r >= 6) px(cx - 1, hy + (int)(hr * 0.45f) + 2, C_WHT);      // croc
      break;
    }
    case 46: { // BYRON : majordome, queue-de-pie noir, monocle, canne doree
      heroBody(cx, cy, r, flash, 0x2E2E48, 0x22223A, 0x1A1A2A, walk);
      fRect(cx - (int)(r * 0.15f), cy + (int)(r * 0.1f), (int)(r * 0.3f), (int)(r * 0.2f) + 1, C_WHT); // plastron
      fRect(cx - (int)(r * 0.35f), cy + (int)(r * 0.3f), (int)(r * 0.2f), (int)(r * 0.3f), flash ? C_WHT : KDColor::RGB24(0x2E2E48)); // queues
      fRect(cx + (int)(r * 0.15f), cy + (int)(r * 0.3f), (int)(r * 0.2f), (int)(r * 0.3f), flash ? C_WHT : KDColor::RGB24(0x2E2E48));
      fRect(cx + (int)(r * 0.55f), cy - (int)(r * 0.15f), (int)(r * 0.06f) + 1, (int)(r * 0.6f), C_GRE); // canne
      fCirc(cx + (int)(r * 0.58f), cy - (int)(r * 0.18f), (int)(r * 0.1f) + 1, C_GOLD); // pommeau
      fRect(cx + (int)(r * 0.3f), cy + (int)(r * 0.15f), (int)(r * 0.28f), (int)(r * 0.1f) + 1, flash ? C_WHT : KDColor::RGB24(0x2E2E48)); // bras
      fCirc(cx, hy, hr, flash ? C_WHT : KDColor::RGB24(0xE8E8F2));     // visage pale
      fRect(cx - (int)(hr * 0.8f), hy - (int)(hr * 0.5f), (int)(hr * 1.6f) + 1, (int)(hr * 0.6f), flash ? C_WHT : KDColor::RGB24(0x9A9AA8)); // cheveux gris
      if (r >= 6) { fCirc(cx - (int)(hr * 0.35f), hy + 1, 2, C_WHT); fCirc(cx + (int)(hr * 0.35f), hy + 1, 2, C_WHT); px(cx - (int)(hr * 0.35f) + 1, hy + 1, C_BLK); px(cx + (int)(hr * 0.35f) + 1, hy + 1, C_BLK); }
      else { px(cx - 2, hy + 1, C_WHT); px(cx + 2, hy + 1, C_WHT); }
      fCirc(cx + (int)(hr * 0.35f), hy + 1, 3, C_GRE);                // monocle
      fRect(cx - (int)(hr * 0.5f), hy + (int)(hr * 0.5f), hr, 2, C_GRE); // moustache
      break;
    }
    case 47: { // MEG : petite mecano, cheveux roux, lunettes de soudure, jambes robot
      if (gMechDraw) { // MEGA ROBOT : grosse armure magenta, tete robot, gatling
        fRect(cx - (int)(r * 0.4f), cy + (int)(r * 0.5f), (int)(r * 0.3f), (int)(r * 0.2f) + 1, KDColor::RGB24(0x6E8A9A)); // jambes
        fRect(cx + (int)(r * 0.1f), cy + (int)(r * 0.5f), (int)(r * 0.3f), (int)(r * 0.2f) + 1, KDColor::RGB24(0x6E8A9A));
        fRect(cx - (int)(r * 0.42f), cy + (int)(r * 0.7f), (int)(r * 0.34f), (int)(r * 0.12f) + 1, C_GRE); // pieds
        fRect(cx + (int)(r * 0.08f), cy + (int)(r * 0.7f), (int)(r * 0.34f), (int)(r * 0.12f) + 1, C_GRE);
        fRect(cx - (int)(r * 0.52f), cy, (int)(r * 1.04f), (int)(r * 0.55f) + 1, flash ? C_WHT : KDColor::RGB24(0xE03BC9)); // gros torse
        fRect(cx - (int)(r * 0.52f), cy + (int)(r * 0.3f), (int)(r * 1.04f), (int)(r * 0.14f) + 1, flash ? C_GRE : KDColor::RGB24(0xA0228A)); // bande
        fCirc(cx - (int)(r * 0.55f), cy - (int)(r * 0.05f), (int)(r * 0.22f) + 1, C_GRE); // epaulettes
        fCirc(cx + (int)(r * 0.55f), cy - (int)(r * 0.05f), (int)(r * 0.22f) + 1, C_GRE);
        fCirc(cx - (int)(r * 0.62f), cy + (int)(r * 0.12f), (int)(r * 0.19f) + 1, flash ? C_WHT : KDColor::RGB24(0x9A9AA8)); // poings
        fCirc(cx + (int)(r * 0.62f), cy + (int)(r * 0.12f), (int)(r * 0.19f) + 1, flash ? C_WHT : KDColor::RGB24(0x9A9AA8));
        fCirc(cx, cy - (int)(r * 0.3f), (int)(r * 0.32f) + 1, C_GRE); // tete robot
        fCirc(cx, cy - (int)(r * 0.3f), (int)(r * 0.24f) + 1, flash ? C_GRE : KDColor::RGB24(0xE8E8F2)); // dome
        fRect(cx - (int)(r * 0.22f), cy - (int)(r * 0.32f), (int)(r * 0.44f) + 1, 3, C_BLK); // visiere
        px(cx - (int)(r * 0.1f), cy - (int)(r * 0.31f), C_YEL); px(cx + (int)(r * 0.08f), cy - (int)(r * 0.31f), C_YEL); // yeux
        fRect(cx - 1, cy - (int)(r * 0.72f), 3, (int)(r * 0.14f) + 1, C_GRE); // antenne
        fCirc(cx, cy - (int)(r * 0.76f), (int)(r * 0.07f) + 1, flash ? C_WHT : KDColor::RGB24(0xE8444E)); // feu rouge
        fRect(cx + (int)(r * 0.72f), cy + (int)(r * 0.18f), (int)(r * 0.08f) + 1, (int)(r * 0.34f) + 1, C_GRE); // gatling
        fRect(cx + (int)(r * 0.66f), cy + (int)(r * 0.12f), (int)(r * 0.18f), (int)(r * 0.26f) + 1, flash ? C_WHT : KDColor::RGB24(0x9A9AA8)); // chargeur
        px(cx + (int)(r * 0.82f), cy + (int)(r * 0.2f), C_YEL); // bouche du canon
        fRect(cx - (int)(r * 0.14f), cy + (int)(r * 0.1f), (int)(r * 0.28f) + 1, (int)(r * 0.16f) + 1, flash ? C_WHT : KDColor::RGB24(0xA0228A)); // plaque
        fRect(cx - (int)(r * 0.06f), cy + (int)(r * 0.13f), (int)(r * 0.12f) + 1, 3, C_YEL); // coeur lumineux
        break;
      }
      fRect(cx - (int)(r * 0.35f), cy + (int)(r * 0.45f), (int)(r * 0.28f), (int)(r * 0.5f), C_GRE); // jambe robot gauche
      fRect(cx + (int)(r * 0.07f), cy + (int)(r * 0.45f), (int)(r * 0.28f), (int)(r * 0.5f), C_GRE);  // jambe robot droite
      fRect(cx - (int)(r * 0.35f), cy + (int)(r * 0.62f), (int)(r * 0.28f), (int)(r * 0.06f) + 1, flash ? C_WHT : KDColor::RGB24(0x6E8A9A)); // articulations
      fRect(cx + (int)(r * 0.07f), cy + (int)(r * 0.62f), (int)(r * 0.28f), (int)(r * 0.06f) + 1, flash ? C_WHT : KDColor::RGB24(0x6E8A9A));
      fCirc(cx, cy + (int)(r * 0.18f), (int)(r * 0.45f), flash ? C_WHT : KDColor::RGB24(0xE8552A)); // torse orange
      fRect(cx - (int)(r * 0.45f), cy + (int)(r * 0.2f), (int)(r * 0.9f), (int)(r * 0.25f), flash ? C_WHT : KDColor::RGB24(0xC9552A));
      fCirc(cx, hy, hr, flash ? C_WHT : KDColor::RGB24(0xF2C898));
      fCirc(cx, hy - (int)(hr * 0.35f), (int)(hr * 0.8f), flash ? C_WHT : KDColor::RGB24(0xE83B3B)); // cheveux roux
      fRect(cx - hr, hy - (int)(hr * 0.3f), (int)(hr * 0.45f), (int)(hr * 1.3f), flash ? C_WHT : KDColor::RGB24(0xE83B3B));
      fRect(cx + (int)(hr * 0.55f), hy - (int)(hr * 0.3f), (int)(hr * 0.45f), (int)(hr * 1.3f), flash ? C_WHT : KDColor::RGB24(0xE83B3B));
      fRect(cx - (int)(hr * 0.7f), hy - (int)(hr * 0.5f), (int)(hr * 1.4f), 3, C_GRE); // lunettes sur le front
      fCirc(cx - (int)(hr * 0.4f), hy - (int)(hr * 0.45f), 2, C_YEL); fCirc(cx + (int)(hr * 0.4f), hy - (int)(hr * 0.45f), 2, C_YEL);
      heroEyes(cx + ex, hy + 2, hr, fx, false);
      if (r >= 6) fRect(cx - 2, hy + (int)(hr * 0.5f), 5, 2, C_BLK);
      fRect(cx + (int)(r * 0.45f), cy + (int)(r * 0.15f), (int)(r * 0.1f) + 1, (int)(r * 0.35f), C_GRE); // cle a molette
      fRect(cx + (int)(r * 0.38f), cy + (int)(r * 0.5f), (int)(r * 0.25f), (int)(r * 0.1f) + 1, C_GRE);
      fRect(cx + (int)(r * 0.25f), cy + (int)(r * 0.15f), (int)(r * 0.25f), (int)(r * 0.1f) + 1, flash ? C_WHT : KDColor::RGB24(0xE8552A)); // bras
      break;
    }
    case 48: { // CHESTER : bouffon, chapeau a cloches bicolore, grand sourire, balles
      heroBody(cx, cy, r, flash, 0x7B7BE0, 0x5050A0, 0x1A1A2A, walk);
      fCirc(cx - (int)(hr * 0.45f), hy - (int)(hr * 0.8f), (int)(hr * 0.55f), flash ? C_WHT : KDColor::RGB24(0x3BE84A)); // corne verte
      fCirc(cx + (int)(hr * 0.45f), hy - (int)(hr * 0.8f), (int)(hr * 0.55f), flash ? C_WHT : KDColor::RGB24(0xE8447A)); // corne rose
      fRect(cx - (int)(hr * 0.9f), hy - (int)(hr * 0.55f), (int)(hr * 1.8f) + 1, 3, C_GOLD); // bande doree
      fCirc(cx - (int)(hr * 0.85f), hy - (int)(hr * 0.35f), (int)(r * 0.08f) + 1, C_GOLD);  // cloches
      fCirc(cx + (int)(hr * 0.85f), hy - (int)(hr * 0.35f), (int)(r * 0.08f) + 1, C_GOLD);
      fCirc(cx, hy, hr, flash ? C_WHT : KDColor::RGB24(0xF2C898));
      heroEyes(cx + ex, hy + 1, hr, fx, false);
      fRect(cx - 3, hy + (int)(hr * 0.45f), 7, 2, C_BLK);              // grand sourire
      if (r >= 10) { px(cx - 2, hy + (int)(hr * 0.45f) + 2, C_WHT); px(cx + 2, hy + (int)(hr * 0.45f) + 2, C_WHT); } // dents
      fCirc(cx - (int)(r * 0.55f), cy - (int)(r * 0.35f), (int)(r * 0.1f) + 1, flash ? C_WHT : KDColor::RGB24(0xE8447A)); // balles de jonglage
      fCirc(cx - (int)(r * 0.75f), cy - (int)(r * 0.1f), (int)(r * 0.1f) + 1, flash ? C_WHT : KDColor::RGB24(0x3BE84A));
      break;
    }
    case 49: { // SURGE : robot surfeur, visiere cyan, hoverboard, boisson energetique
      fRect(cx - (int)(r * 0.55f), cy + (int)(r * 0.95f), (int)(r * 1.1f), (int)(r * 0.12f) + 1, flash ? C_WHT : KDColor::RGB24(0x3BC9E8)); // hoverboard
      fRect(cx - (int)(r * 0.45f), cy + (int)(r * 1.05f), (int)(r * 0.9f), 2, C_WHT); // reflux
      heroBody(cx, cy, r, flash, 0x50C8E8, 0x2AA8C9, 0x1A2A3E, walk);
      fCirc(cx, hy, hr + 1, flash ? C_WHT : KDColor::RGB24(0xB8C4D8)); // tete metal
      fRect(cx - hr, hy - 1, hr * 2 + 1, (int)(hr * 0.9f) + 1, flash ? C_WHT : KDColor::RGB24(0x1A2A3E)); // visiere
      fCirc(cx - (int)(hr * 0.4f), hy + 1, (int)(hr * 0.22f) + 1, flash ? C_WHT : KDColor::RGB24(0x50C8E8)); // yeux cyan
      fCirc(cx + (int)(hr * 0.4f), hy + 1, (int)(hr * 0.22f) + 1, flash ? C_WHT : KDColor::RGB24(0x50C8E8));
      fRect(cx - (int)(hr * 0.8f), hy - (int)(hr * 0.6f), (int)(hr * 1.6f), 3, flash ? C_WHT : KDColor::RGB24(0x50C8E8)); // bande cyan
      fRect(cx + (int)(r * 0.35f), cy + (int)(r * 0.15f), (int)(r * 0.28f), (int)(r * 0.12f) + 1, flash ? C_WHT : KDColor::RGB24(0x50C8E8)); // bras + boisson
      fRect(cx + (int)(r * 0.45f), cy + (int)(r * 0.02f), (int)(r * 0.12f) + 1, (int)(r * 0.14f) + 1, C_WHT);
      break;
    }
    case 50: { // PIPER : grand chapeau rose, robe, fusil de precision
      fRect(cx - (int)(r * 0.3f), cy + (int)(r * 0.45f), (int)(r * 0.26f), (int)(r * 0.45f) + 1, flash ? C_WHT : KDColor::RGB24(0xE87AB8)); // robe
      fRect(cx - (int)(r * 0.3f), cy, (int)(r * 0.6f), (int)(r * 0.5f) + 1, flash ? C_WHT : KDColor::RGB24(0xE87AB8));
      fRect(cx - (int)(r * 0.15f), cy - 1, (int)(r * 0.3f), (int)(r * 0.35f) + 1, C_WHT);                 // plastron blanc
      fCirc(cx, hy, hr + 1, flash ? C_WHT : KDColor::RGB24(0xF2C99A));                                   // visage
      fRect(cx - hr, hy - (int)(hr * 0.2f), hr * 2 + 1, (int)(hr * 0.9f) + 1, flash ? C_WHT : KDColor::RGB24(0xF2A03C)); // cheveux
      fRect(cx - (int)(r * 1.05f), hy - (int)(hr * 0.35f), (int)(r * 2.1f) + 1, 3, flash ? C_WHT : KDColor::RGB24(0xE855A8)); // bordure
      fRect(cx - (int)(r * 0.45f), hy - (int)(hr * 1.4f), (int)(r * 0.9f), (int)(hr * 1.2f) + 1, flash ? C_WHT : KDColor::RGB24(0xE855A8)); // calotte
      heroEyes(cx + ex, hy + 1, hr, fx, false);
      if (r >= 6) fRect(cx - 2, hy + (int)(hr * 0.5f), 4, 1, flash ? C_WHT : KDColor::RGB24(0xE8447A)); // levres
      fRect(cx + (int)(r * 0.3f), cy + (int)(r * 0.12f), (int)(r * 0.55f), 3, flash ? C_WHT : KDColor::RGB24(0x8A6A3A)); // fusil bois
      fRect(cx + (int)(r * 0.85f), cy + (int)(r * 0.11f), (int)(r * 0.3f), 2, C_GRE);                    // canon acier
      if (r >= 6) fRect(cx + (int)(r * 0.5f), cy + (int)(r * 0.05f), 3, 2, C_GRE);                       // lunette
      break;
    }
    case 51: { // MAX : runner cyan, bandeau rouge, lunettes remontees, gourde
      heroBody(cx, cy, r, flash, 0x3BC9E8, 0x22628A, 0x1A2A3E, walk);
      fRect(cx - (int)(r * 0.3f), cy + (int)(r * 0.18f), (int)(r * 0.6f), 2, flash ? C_WHT : KDColor::RGB24(0xF2D84A)); // bande jaune short
      fCirc(cx, hy, hr + 1, flash ? C_WHT : KDColor::RGB24(0x1A1A22));   // cheveux noirs
      fCirc(cx + 1, hy + 1, hr - (hr >= 6 ? hr * 0.35f : 2), flash ? C_WHT : KDColor::RGB24(0x8A5A38)); // visage
      fRect(cx - hr, hy - (int)(hr * 0.55f), hr * 2, 2, C_RED);           // bandeau rouge sur le front
      fRect(cx - hr, hy - 1, hr * 2 + 1, (int)(hr * 0.6f), flash ? C_WHT : KDColor::RGB24(0x9A9AA8)); // lunettes remontees
      fCirc(cx - (int)(hr * 0.4f), hy - 1, (int)(hr * 0.2f) + 1, flash ? C_WHT : KDColor::RGB24(0xF2D84A)); // verres jaunes
      fCirc(cx + (int)(hr * 0.4f), hy - 1, (int)(hr * 0.2f) + 1, flash ? C_WHT : KDColor::RGB24(0xF2D84A));
      fRect(cx - (int)(r * 0.55f), cy + (int)(r * 0.05f), (int)(r * 0.2f), (int)(r * 0.35f) + 1, C_WHT); // gourde
      fRect(cx - (int)(r * 0.52f), cy + (int)(r * 0.02f), (int)(r * 0.14f), 2, C_GRE);                   // goulot
      break;
    }
    case 52: { // TARA : robe violette, voile noir, troisieme oeil dore, cartes
      fRect(cx - (int)(r * 0.4f), cy - (int)(r * 0.1f), (int)(r * 0.8f), (int)(r * 0.9f) + 1, flash ? C_WHT : KDColor::RGB24(0x7A4AA0)); // robe
      fRect(cx - (int)(r * 0.4f), cy + (int)(r * 0.25f), (int)(r * 0.8f), 3, flash ? C_WHT : KDColor::RGB24(0xE8B84A)); // ceinture doree
      fCirc(cx, hy, hr + 2, flash ? C_WHT : KDColor::RGB24(0x111118));   // voile noir
      fCirc(cx + 1, hy + 1, hr - (hr >= 6 ? hr * 0.3f : 2), flash ? C_WHT : KDColor::RGB24(0xC98860)); // visage
      heroEyes(cx + ex, hy + 1, hr, fx, true);
      if (r >= 6) fCirc(cx, hy - (int)(hr * 0.35f), 2, flash ? C_WHT : C_YEL); // troisieme oeil dore
      if (r >= 6) fRect(cx - 2, hy + (int)(hr * 0.55f), 4, 1, flash ? C_WHT : KDColor::RGB24(0xE8447A)); // levres
      fRect(cx - (int)(r * 0.5f), cy + (int)(r * 0.15f), 3, (int)(r * 0.35f), C_WHT); // 3 cartes en eventail
      fRect(cx - 1, cy + (int)(r * 0.1f), 3, (int)(r * 0.4f), C_WHT);
      fRect(cx + (int)(r * 0.42f), cy + (int)(r * 0.15f), 3, (int)(r * 0.35f), C_WHT);
      break;
    }
    case 53: { // SPROUT : robot jardinier vert, fourche a roue, plante au sommet
      fRect(cx - (int)(r * 0.45f), cy - (int)(r * 0.1f), (int)(r * 0.9f), (int)(r * 0.9f) + 1, flash ? C_WHT : KDColor::RGB24(0x3AA05C)); // corps vert
      fRect(cx - (int)(r * 0.18f), cy + (int)(r * 0.8f), (int)(r * 0.36f), (int)(r * 0.12f) + 1, C_GRE); // fourche
      fCirc(cx, cy + (int)(r * 0.85f), (int)(r * 0.16f) + 1, flash ? C_WHT : KDColor::RGB24(0x9A9AA8)); // roue
      fCirc(cx, hy - (int)(hr * 0.4f), hr + 1, flash ? C_WHT : KDColor::RGB24(0x3AA05C)); // tete
      fRect(cx - (int)(hr * 0.5f), hy - (int)(hr * 1.0f), 2, (int)(hr * 0.4f) + 1, flash ? C_WHT : KDColor::RGB24(0x6E8A9A)); // anse gauche
      fRect(cx + (int)(hr * 0.48f), hy - (int)(hr * 1.0f), 2, (int)(hr * 0.4f) + 1, flash ? C_WHT : KDColor::RGB24(0x6E8A9A)); // anse droite
      fRect(cx - hr, hy - 1, hr * 2 + 1, (int)(hr * 0.8f) + 1, C_WHT);    // visiere blanche
      heroEyes(cx + ex, hy + 1, hr, fx, false);
      if (r >= 6) fRect(cx - 2, hy + (int)(hr * 0.5f), 4, 2, flash ? C_WHT : KDColor::RGB24(0xE8444E)); // bouche LED
      fCirc(cx, hy - (int)(hr * 1.6f), (int)(r * 0.12f) + 1, flash ? C_WHT : KDColor::RGB24(0x3BE84A)); // plante au sommet
      fRect(cx + (int)(r * 0.4f), cy + (int)(r * 0.15f), (int)(r * 0.15f), (int)(r * 0.25f) + 1, flash ? C_WHT : KDColor::RGB24(0x3AA05C)); // petit bras
      break;
    }
    case 54: { // MR. P : concierge, chapeau melon, grosse moustache, valise
      heroBody(cx, cy, r, flash, 0x3B6EA5, 0x22628A, 0x1A2A3E, walk);
      fRect(cx - (int)(r * 0.3f), cy + (int)(r * 0.2f), (int)(r * 0.6f), 2, flash ? C_WHT : KDColor::RGB24(0xF2D84A)); // bande jaune
      px(cx - (int)(r * 0.12f), cy + (int)(r * 0.28f), C_GOLD); px(cx + (int)(r * 0.12f), cy + (int)(r * 0.28f), C_GOLD); // boutons
      fCirc(cx, hy, hr + 1, flash ? C_WHT : KDColor::RGB24(0xD9A06A));   // visage
      fRect(cx - (int)(hr * 0.55f), hy - (int)(hr * 1.0f), (int)(hr * 1.1f), (int)(hr * 0.8f) + 1, C_BLK); // chapeau melon
      fRect(cx - (int)(hr * 0.9f), hy - (int)(hr * 0.45f), (int)(hr * 1.8f) + 1, 2, C_BLK); // bord
      heroEyes(cx + ex, hy, hr, fx, false);
      fRect(cx - (int)(hr * 0.55f), hy + (int)(hr * 0.45f), (int)(hr * 1.1f), 2, flash ? C_WHT : KDColor::RGB24(0x111111)); // grosse moustache
      fRect(cx - (int)(r * 0.55f), cy + (int)(r * 0.25f), (int)(r * 0.35f), (int)(r * 0.3f) + 1, flash ? C_WHT : KDColor::RGB24(0x8A6A3A)); // valise
      fRect(cx - (int)(r * 0.45f), cy + (int)(r * 0.2f), (int)(r * 0.15f), 2, C_GOLD);                   // poignee doree
      break;
    }
  }
  // flash de bouche pendant l'attaque
  if (attackT > 0 && r >= 4) {
    int mx = (int)(fcosf(fa) * (r + 6)), my = (int)(fsinf(fa) * (r + 6));
    fCirc(cx + mx, cy + my, attackT > 120 ? 3 : 2, attackT > 120 ? C_WHT : C_YEL);
    fCirc(cx + mx, cy + my, 1, C_ORG);
  }
}

// dessine le sprite complet d'un brawler (corps + traits uniques)
// V5.12 : structs de compte deplaces ici (acc utilise par le skin equipe ci-dessous)
struct BrawlerAccount { int level, trophies; bool gadget, starPower, hyper; uint8_t gadChoice, starChoice; };
struct Account { BrawlerAccount b[55]; int coins;
  int matches, wins, losses, killsTotal; // carrieres (V5.10)
  uint8_t skin[55];     // skin selectionne par brawler : 0 normal, 1 AZUR, 2 OR, 3 OMBRE
  uint8_t skinOwned[7]; // bitmask 55 skins achetes
  uint8_t emoteP, emoteM; // emote assignee aux touches + et - (0 = aucune)
  char name[9];  // pseudo du joueur (V5.11)
  int bestWave;  // record du mode SURVIE (V5.11)
  uint8_t pMatches[55], pWins[55]; uint16_t pKills[55], pDmg[55]; // V5.16 : stats PAR brawler (dmg en centaines)
} acc;
// walk : phase d'animation de marche (0 = immobile)
static void drawBrawlerSprite(int cx, int cy, int r, int bIdx, int faceAng, bool flash, int walk = 0, int attackT = 0) {
  // les 55 brawlers ont un sprite dessine a la main (V5.8 : vague 6 + mega robot de Meg)
  if (bIdx >= 0 && bIdx < 55) {
    if (attackT > 0) { // recul du corps pendant l'attaque, comme les autres
      int kick = (attackT > 120) ? 2 : 1;
      cx -= (int)(fcosf(faceAng * 0.01745f) * kick);
      cy -= (int)(fsinf(faceAng * 0.01745f) * kick);
    }
    gSkinTint = (acc.skin[bIdx] > 0 && (acc.skinOwned[bIdx >> 3] & (1 << (bIdx & 7)))) ? acc.skin[bIdx] : 0; // skin equipe
    drawHeroSprite(cx, cy, r, bIdx, faceAng, flash, walk, attackT);
    gSkinTint = 0;
    return;
  }
  // V5.14 : l'ancien rendu generique (placeholder + brawlerLook/Look) a ete SUPPRIME :
  // les 55 sprites dessines a la main couvrent tout, ce code ne servait plus a rien.
}

// ------------------------- Compte / progression ----------------------
// V5.12 : struct BrawlerAccount/Account et la globale acc definis plus haut (avant drawBrawlerSprite)
static int unlockedBrawlers = 0;

// pouvoir star equipe selon le choix du joueur (V5.13b : gadgets et hypercharge supprimes)
static const char* curStarName(int i) {
  return acc.b[i].starChoice == 1 ? STAR2_NAMES[i] : STAR_NAMES[i];
}

// ---- Sauvegarde persistante (systeme de fichiers interne d'Epsilon) ----
// Methode NumPlay : ecriture directe des records via epsilon_files.h.
// Format : 55*6 (niveau, trophees, flags, choix gadget/star) + pieces + quetes + compteurs = 361 octets.
struct Quest { int type; int goal; int prog; int reward; };
static Quest quests[3];
static int killCounter = 0, gemCounter = 0, superCounter = 0, winStreak = 0;
static int auWins = 0, auKills = 0, auBestS = 0; // V5.18 : stats Among Us Micro (victoires, kills, meilleur temps)
static int vsBestT = 0, vsKillsT = 0, vsBestLvl = 1; // V5.32 : stats Vampire Mini (record temps, kills totaux, meilleur niveau)
static int tdBestW = 0, tdWavesT = 0, tdKillsT = 0; // V5.32 : stats Tower Defense (record vague, vagues totales, kills totaux)
static int otWins = 0, otLosses = 0, otDraws = 0; // V5.32 : stats Othello (victoires, defaites, matchs nuls)
static int rgBestF = 0, rgKillsT = 0, rgFloorsT = 0; // V5.32 : stats Donjon (record etage, kills totaux, etages totaux)
#define SAVE_NAME "minibs.sav"
#define SAVE_BACKUP "minibs.bak" // V5.14 : double record anti-corruption
static int gFps60 = 0; // V5.14 : option "60 FPS" (0 = 30 fps par defaut, plus fluide a 1 mais plus gourmand)
static int gFpsShow = 0; // V5.16 : afficher le compteur FPS en match
static void saveAcc() {
  uint8_t buf[806]; // V5.32 : 806 octets reels (+12 stats Othello + Donjon)
  int p = 0;
  for (int i = 0; i < 55; i++) {
    buf[p++] = (uint8_t)acc.b[i].level;
    buf[p++] = (uint8_t)(acc.b[i].trophies >> 8);
    buf[p++] = (uint8_t)(acc.b[i].trophies & 0xFF);
    buf[p++] = (uint8_t)((acc.b[i].gadget ? 1 : 0) | (acc.b[i].starPower ? 2 : 0) | (acc.b[i].hyper ? 4 : 0));
    buf[p++] = acc.b[i].gadChoice;
    buf[p++] = acc.b[i].starChoice;
  }
  buf[p++] = (uint8_t)(acc.coins >> 8);
  buf[p++] = (uint8_t)(acc.coins & 0xFF);
  for (int i = 0; i < 3; i++) {
    buf[p++] = (uint8_t)quests[i].type;
    buf[p++] = (uint8_t)(quests[i].goal >> 8);
    buf[p++] = (uint8_t)(quests[i].goal & 0xFF);
    buf[p++] = (uint8_t)(quests[i].prog >> 8);
    buf[p++] = (uint8_t)(quests[i].prog & 0xFF);
    buf[p++] = (uint8_t)(quests[i].reward >> 8);
    buf[p++] = (uint8_t)(quests[i].reward & 0xFF);
  }
  buf[p++] = (uint8_t)(killCounter >> 8); buf[p++] = (uint8_t)(killCounter & 0xFF);
  buf[p++] = (uint8_t)(gemCounter >> 8);  buf[p++] = (uint8_t)(gemCounter & 0xFF);
  buf[p++] = (uint8_t)(superCounter >> 8);buf[p++] = (uint8_t)(superCounter & 0xFF);
  buf[p++] = (uint8_t)(winStreak >> 8);   buf[p++] = (uint8_t)(winStreak & 0xFF);
  // ---- V5.10 : carriere, skins, emotes ----
  buf[p++] = (uint8_t)(acc.matches >> 8);  buf[p++] = (uint8_t)(acc.matches & 0xFF);
  buf[p++] = (uint8_t)(acc.wins >> 8);     buf[p++] = (uint8_t)(acc.wins & 0xFF);
  buf[p++] = (uint8_t)(acc.losses >> 8);   buf[p++] = (uint8_t)(acc.losses & 0xFF);
  buf[p++] = (uint8_t)(acc.killsTotal >> 8); buf[p++] = (uint8_t)(acc.killsTotal & 0xFF);
  for (int i = 0; i < 55; i++) buf[p++] = acc.skin[i];
  for (int i = 0; i < 7; i++)  buf[p++] = acc.skinOwned[i];
  buf[p++] = acc.emoteP; buf[p++] = acc.emoteM;
  for (int i = 0; i < 9; i++) buf[p++] = (uint8_t)acc.name[i]; // V5.11 : pseudo
  buf[p++] = (uint8_t)(acc.bestWave >> 8); buf[p++] = (uint8_t)(acc.bestWave & 0xFF); // record survie
  buf[p++] = gFps60 ? 1 : 0; // V5.14 : option 60 FPS (445 octets)
  for (int i = 0; i < 55; i++) { // V5.16 : stats par brawler (330 octets)
    buf[p++] = acc.pMatches[i]; buf[p++] = acc.pWins[i];
    buf[p++] = (uint8_t)(acc.pKills[i] >> 8); buf[p++] = (uint8_t)(acc.pKills[i] & 0xFF);
    buf[p++] = (uint8_t)(acc.pDmg[i] >> 8);   buf[p++] = (uint8_t)(acc.pDmg[i] & 0xFF);
  }
  buf[p++] = gFpsShow ? 1 : 0; // V5.16 : compteur FPS
  // V5.18 : stats Among Us Micro (782 octets au total)
  buf[p++] = (uint8_t)(auWins >> 8);  buf[p++] = (uint8_t)(auWins & 0xFF);
  buf[p++] = (uint8_t)(auKills >> 8); buf[p++] = (uint8_t)(auKills & 0xFF);
  buf[p++] = (uint8_t)(auBestS >> 8); buf[p++] = (uint8_t)(auBestS & 0xFF);
  buf[p++] = (uint8_t)(vsBestT >> 8); buf[p++] = (uint8_t)(vsBestT & 0xFF); // V5.32 : stats Vampire Mini
  buf[p++] = (uint8_t)(vsKillsT >> 8); buf[p++] = (uint8_t)(vsKillsT & 0xFF);
  buf[p++] = (uint8_t)(vsBestLvl >> 8); buf[p++] = (uint8_t)(vsBestLvl & 0xFF); // V5.32 : stats Tower Defense
  buf[p++] = (uint8_t)(tdBestW >> 8);  buf[p++] = (uint8_t)(tdBestW & 0xFF);
  buf[p++] = (uint8_t)(tdWavesT >> 8); buf[p++] = (uint8_t)(tdWavesT & 0xFF);
  buf[p++] = (uint8_t)(tdKillsT >> 8); buf[p++] = (uint8_t)(tdKillsT & 0xFF); // V5.32 : stats Othello
  buf[p++] = (uint8_t)(otWins >> 8);   buf[p++] = (uint8_t)(otWins & 0xFF);
  buf[p++] = (uint8_t)(otLosses >> 8); buf[p++] = (uint8_t)(otLosses & 0xFF);
  buf[p++] = (uint8_t)(otDraws >> 8);  buf[p++] = (uint8_t)(otDraws & 0xFF);
  buf[p++] = (uint8_t)(rgBestF >> 8);  buf[p++] = (uint8_t)(rgBestF & 0xFF); // V5.32 : stats Donjon
  buf[p++] = (uint8_t)(rgKillsT >> 8); buf[p++] = (uint8_t)(rgKillsT & 0xFF);
  buf[p++] = (uint8_t)(rgFloorsT >> 8); buf[p++] = (uint8_t)(rgFloorsT & 0xFF);
  ef_write(SAVE_NAME, buf, (uint32_t)p);
  ef_write(SAVE_BACKUP, buf, (uint32_t)p); // V5.14 : sauvegarde de secours anti-corruption
  // V5.18 : methode NumPlay — on copie TOUTE la sauvegarde dans un SCRIPT PYTHON
  // (minibs_saves.py). Installer un .nwa efface les fichiers de l'app mais PAS les
  // scripts Python : la progression revient toute seule apres une reinstal/update.
  // V5.31 : UNE LIGNE PAR JEU dans le python : a = Brawl (776 octets), b = Among Us (6 octets).
  // Les futurs jeux prendront c, d, e... 24 lignes dispo. AUCUN nom de jeu dans le fichier.
  static char py[1800];
  int q = 0;
  const char *pre = "# Ne pas modifier\n";
  while (*pre) { py[q++] = *pre++; }
  const char *la = "a = \"";
  while (*la) { py[q++] = *la++; }
  for (int i = 0; i < p && i < 776; i++) {
    py[q++] = "0123456789ABCDEF"[(buf[i] >> 4) & 15];
    py[q++] = "0123456789ABCDEF"[buf[i] & 15];
  }
  py[q++] = '"'; py[q++] = '\n';
  const char *lb = "b = \"";
  while (*lb) { py[q++] = *lb++; }
  for (int i = 776; i < p && i < 782; i++) {
    py[q++] = "0123456789ABCDEF"[(buf[i] >> 4) & 15];
    py[q++] = "0123456789ABCDEF"[buf[i] & 15];
  }
  py[q++] = '"'; py[q++] = '\n';
  const char *lc = "c = \""; // V5.32 : ligne c = Vampire Mini (6 octets)
  while (*lc) { py[q++] = *lc++; }
  for (int i = 782; i < p && i < 788; i++) {
    py[q++] = "0123456789ABCDEF"[(buf[i] >> 4) & 15];
    py[q++] = "0123456789ABCDEF"[buf[i] & 15];
  }
  py[q++] = '"'; py[q++] = '\n';
  const char *ld = "d = \""; // V5.32 : ligne d = Tower Defense (6 octets)
  while (*ld) { py[q++] = *ld++; }
  for (int i = 788; i < p && i < 794; i++) {
    py[q++] = "0123456789ABCDEF"[(buf[i] >> 4) & 15];
    py[q++] = "0123456789ABCDEF"[buf[i] & 15];
  }
  py[q++] = '"'; py[q++] = '\n';
  const char *le = "e = \""; // V5.32 : ligne e = Othello (6 octets)
  while (*le) { py[q++] = *le++; }
  for (int i = 794; i < p && i < 800; i++) {
    py[q++] = "0123456789ABCDEF"[(buf[i] >> 4) & 15];
    py[q++] = "0123456789ABCDEF"[buf[i] & 15];
  }
  py[q++] = '"'; py[q++] = '\n';
  const char *lf = "f = \""; // V5.32 : ligne f = Donjon (6 octets)
  while (*lf) { py[q++] = *lf++; }
  for (int i = 800; i < p; i++) {
    py[q++] = "0123456789ABCDEF"[(buf[i] >> 4) & 15];
    py[q++] = "0123456789ABCDEF"[buf[i] & 15];
  }
  py[q++] = '"'; py[q++] = '\n'; py[q] = 0;
  ef_write("minibs_saves.py", (const uint8_t *)py, (uint32_t)q);
}
static int pyHex(const uint8_t *f, uint32_t flen, char var, uint8_t *out, int maxB) { // V5.31 : decode la ligne 'var = "HEX"'
  for (uint32_t i = 0; i + 5 < flen; i++) {
    if (f[i] == (uint8_t)var && f[i + 1] == ' ' && f[i + 2] == '=' && f[i + 3] == ' ' && f[i + 4] == '"') {
      int n = 0;
      uint32_t j = i + 5;
      while (j + 1 < flen && f[j] != '"' && n < maxB) {
        int h1 = (f[j] >= '0' && f[j] <= '9') ? f[j] - '0' : (f[j] >= 'A' && f[j] <= 'F') ? f[j] - 'A' + 10 : -1;
        int h2 = (f[j + 1] >= '0' && f[j + 1] <= '9') ? f[j + 1] - '0' : (f[j + 1] >= 'A' && f[j + 1] <= 'F') ? f[j + 1] - 'A' + 10 : -1;
        if (h1 < 0 || h2 < 0) return n;
        out[n++] = (uint8_t)((h1 << 4) | h2);
        j += 2;
      }
      return n;
    }
  }
  return 0;
}
static bool loadAcc() {
  uint32_t len = 0;
  const uint8_t *buf = ef_read(SAVE_NAME, &len);
  // V5.14 : double record anti-corruption — si le record principal est perdu/corrompu, on tente la copie de secours
  bool bad = !buf || (len != 231 && len != 331 && len != 361 && len != 433 && len != 444 && len != 445 && len != 776 && len != 782 && len != 788 && len != 794 && len != 800 && len != 806);
  if (bad) {
    buf = ef_read(SAVE_BACKUP, &len);
    bad = !buf || (len != 231 && len != 331 && len != 361 && len != 433 && len != 444 && len != 445 && len != 776 && len != 782 && len != 788 && len != 794 && len != 800 && len != 806);
  }
  // V5.18 : DERNIER recours (methode NumPlay) — la copie PYTHON de la sauvegarde.
  // minibs_saves.py survit a la reinstal de l'app : on decode son hex et on relit tout.
  if (bad) {
    static uint8_t pyBuf[806];
    uint32_t plen = 0;
    const uint8_t *py = ef_read("minibs_saves.py", &plen);
    if (py && plen > 10) {
      int nA = pyHex(py, plen, 'a', pyBuf, 776);       // V5.31 : ligne a = Brawl
      int nB = pyHex(py, plen, 'b', pyBuf + 776, 6);   // V5.31 : ligne b = Among Us
      int nC = pyHex(py, plen, 'c', pyBuf + 782, 6);   // V5.32 : ligne c = Vampire Mini
      int nD = pyHex(py, plen, 'd', pyBuf + 788, 6);   // V5.32 : ligne d = Tower Defense
      int nE = pyHex(py, plen, 'e', pyBuf + 794, 6);  // V5.32 : ligne e = Othello
      int nF = pyHex(py, plen, 'f', pyBuf + 800, 6);  // V5.32 : ligne f = Donjon
      if (nA >= 776) { // nouveau format multi-lignes : une ligne par jeu
        buf = pyBuf;
        if (nB < 6) len = 776;
        else if (nC < 6) len = 782;
        else if (nD < 6) len = 788;
        else if (nE < 6) len = 794;
        else if (nF < 6) len = 800;
        else len = 806;
        bad = false;
      } else { // ancien format historique (s = "...") : compatibilite
        int st = 0, d = 0;
        for (uint32_t i = 0; i < plen && d < 806 * 2; i++) {
          uint8_t c = py[i];
          if (st == 0) { if (c == '"') st = 1; continue; } // cherche le debut de la chaine
          if (c == '"') break;                            // fin de la chaine hex
          int hv = (c >= '0' && c <= '9') ? c - '0' : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
          if (hv < 0) break;                              // caractere inattendu : stop
          if (d % 2 == 0) pyBuf[d / 2] = (uint8_t)(hv << 4); else pyBuf[d / 2] |= (uint8_t)hv;
          d++;
        }
        if (d >= 2) {
          buf = pyBuf; len = (uint32_t)(d / 2);
          bad = (len != 231 && len != 331 && len != 361 && len != 433 && len != 444 && len != 445 && len != 776 && len != 782 && len != 788 && len != 794 && len != 800 && len != 806);
        }
      }
    }
  }
  if (bad) return false; // format inattendu : partie vierge
  // V5.13 FIX : tailles REELLES ecrites par saveAcc au fil des versions.
  // 445 = format actuel | 444 = V5.13 | 433 = V5.10/V5.11 (emotes, sans pseudo/record) | 361/331/231 = anciens.
  int nb = (len == 331 || len == 231) ? 50 : 55; // nombre de brawlers presents dans la sauvegarde
  int p = 0;
  for (int i = 0; i < 55; i++) {
    if (i >= nb) { // brawlers ajoutes apres coup : compte vierge
      acc.b[i].level = 0; acc.b[i].trophies = 0;
      acc.b[i].gadget = acc.b[i].starPower = acc.b[i].hyper = false;
      acc.b[i].gadChoice = acc.b[i].starChoice = 0;
      continue;
    }
    acc.b[i].level = buf[p++];
    int t = (buf[p] << 8) | buf[p + 1]; p += 2;
    acc.b[i].trophies = t;
    uint8_t flags = buf[p++];
    acc.b[i].gadget = (flags & 1) != 0;
    acc.b[i].starPower = (flags & 2) != 0;
    acc.b[i].hyper = (flags & 4) != 0;
    if (len != 231) {
      acc.b[i].gadChoice = buf[p++] & 1;
      acc.b[i].starChoice = buf[p++] & 1;
    } else { acc.b[i].gadChoice = 0; acc.b[i].starChoice = 0; }
    if (acc.b[i].level > 11) acc.b[i].level = 11;
    if (acc.b[i].trophies < 0 || acc.b[i].trophies > 9999) acc.b[i].trophies = 0;
  }
  acc.coins = (buf[p] << 8) | buf[p + 1];
  if (acc.coins < 0) acc.coins = 0;
  p += 2;
  for (int i = 0; i < 3; i++) {
    quests[i].type = buf[p++];
    quests[i].goal = (buf[p] << 8) | buf[p + 1]; p += 2;
    quests[i].prog = (buf[p] << 8) | buf[p + 1]; p += 2;
    quests[i].reward = (buf[p] << 8) | buf[p + 1]; p += 2;
    if (quests[i].type > 4) { quests[i].type = 0; quests[i].goal = 1; quests[i].prog = 0; quests[i].reward = 80; }
  }
  killCounter = (buf[p] << 8) | buf[p + 1]; p += 2;
  gemCounter  = (buf[p] << 8) | buf[p + 1]; p += 2;
  superCounter = (buf[p] << 8) | buf[p + 1]; p += 2;
  winStreak   = (buf[p] << 8) | buf[p + 1];
  if (winStreak < 0) winStreak = 0;
  // ---- V5.10 : carriere, skins, emotes (absents des vieilles sauvegardes) ----
  if (len == 433 || len == 444 || len == 445 || len == 776 || len == 782 || len == 788 || len == 794 || len == 800 || len == 806) { // V5.18 FIX : 776/782 sinon la carriere etait sautee et le reste DECALE !
    p += 2;
    acc.matches = (buf[p] << 8) | buf[p + 1]; p += 2;
    acc.wins = (buf[p] << 8) | buf[p + 1]; p += 2;
    acc.losses = (buf[p] << 8) | buf[p + 1]; p += 2;
    acc.killsTotal = (buf[p] << 8) | buf[p + 1]; p += 2;
    for (int i = 0; i < 55; i++) { acc.skin[i] = buf[p++] & 3; }
    for (int i = 0; i < 7; i++) acc.skinOwned[i] = buf[p++];
    acc.emoteP = buf[p++] % 7; acc.emoteM = buf[p++] % 7;
  } else {
    acc.matches = acc.wins = acc.losses = acc.killsTotal = 0;
    for (int i = 0; i < 55; i++) acc.skin[i] = 0;
    for (int i = 0; i < 7; i++) acc.skinOwned[i] = 0;
    acc.emoteP = acc.emoteM = 0;
  }
  // ---- V5.11 : pseudo + record survie (444 = format actuel) ----
  if (len == 444 || len == 445 || len == 776 || len == 782 || len == 788 || len == 794 || len == 800 || len == 806) { // V5.17 FIX : 776/782 sinon le pseudo etait PERDU au chargement !
    for (int i = 0; i < 9; i++) acc.name[i] = (char)buf[p++];
    acc.name[8] = 0;
    acc.bestWave = (buf[p] << 8) | buf[p + 1];
    if (acc.bestWave < 0 || acc.bestWave > 999) acc.bestWave = 0;
    p += 2;
  } else { acc.name[0] = 0; acc.bestWave = 0; }
  if (len == 445 || len == 776 || len == 782 || len == 788 || len == 794 || len == 800 || len == 806) gFps60 = (buf[p++] != 0); // V5.14 : option 60 FPS persistee
  // ---- V5.16 : stats par brawler (formats 776 et 782) ----
  if (len == 776 || len == 782 || len == 788 || len == 794 || len == 800 || len == 806) {
    for (int i = 0; i < 55; i++) {
      acc.pMatches[i] = buf[p++]; acc.pWins[i] = buf[p++];
      acc.pKills[i] = (buf[p] << 8) | buf[p + 1]; p += 2;
      acc.pDmg[i] = (buf[p] << 8) | buf[p + 1]; p += 2;
    }
    gFpsShow = (buf[p++] != 0);
    if (len >= 782) { // V5.18 : stats Among Us Micro en fin de sauvegarde
      auWins = (buf[p] << 8) | buf[p + 1]; p += 2;
      auKills = (buf[p] << 8) | buf[p + 1]; p += 2;
      auBestS = (buf[p] << 8) | buf[p + 1]; p += 2;
      if (auWins < 0) auWins = 0;
      if (auKills < 0) auKills = 0;
      if (auBestS < 0 || auBestS > 9999) auBestS = 0;
    }
    if (len >= 788) { // V5.32 : stats Vampire Mini en fin de sauvegarde
      vsBestT = (buf[p] << 8) | buf[p + 1]; p += 2;
      vsKillsT = (buf[p] << 8) | buf[p + 1]; p += 2;
      vsBestLvl = (buf[p] << 8) | buf[p + 1]; p += 2;
      if (vsBestT < 0 || vsBestT > 99999) vsBestT = 0;
      if (vsKillsT < 0) vsKillsT = 0;
      if (vsBestLvl < 1 || vsBestLvl > 999) vsBestLvl = 1;
    }
    if (len >= 794) { // V5.32 : stats Tower Defense en toute fin
      tdBestW = (buf[p] << 8) | buf[p + 1]; p += 2;
      tdWavesT = (buf[p] << 8) | buf[p + 1]; p += 2;
      tdKillsT = (buf[p] << 8) | buf[p + 1]; p += 2;
      if (tdBestW < 0 || tdBestW > 9999) tdBestW = 0;
      if (tdWavesT < 0) tdWavesT = 0;
      if (tdKillsT < 0) tdKillsT = 0;
    }
    if (len == 800 || len == 806) { // V5.32 : stats Othello
      otWins = (buf[p] << 8) | buf[p + 1]; p += 2;
      otLosses = (buf[p] << 8) | buf[p + 1]; p += 2;
      otDraws = (buf[p] << 8) | buf[p + 1]; p += 2;
      if (otWins < 0) otWins = 0;
      if (otLosses < 0) otLosses = 0;
      if (otDraws < 0) otDraws = 0;
    }
    if (len == 806) { // V5.32 : stats Donjon Roguelike
      rgBestF = (buf[p] << 8) | buf[p + 1]; p += 2;
      rgKillsT = (buf[p] << 8) | buf[p + 1]; p += 2;
      rgFloorsT = (buf[p] << 8) | buf[p + 1]; p += 2;
      if (rgBestF < 0 || rgBestF > 9999) rgBestF = 0;
      if (rgKillsT < 0) rgKillsT = 0;
      if (rgFloorsT < 0) rgFloorsT = 0;
    }
  }
  for (int i = 0; i < 9; i++) if (acc.name[i] != 0 && (acc.name[i] < ' ' || acc.name[i] > 'z')) acc.name[i] = 0; // sanity
  return true;
}

static int upgradeCost(int lvl) { return 20 + lvl * 30; }
static float lvlMul(int lvl) { return 1.0f + (lvl - 1) * 0.05f; }

static int totalTrophies() {
  int t = 0;
  for (int i = 0; i < 55; i++) t += acc.b[i].trophies;
  return t;
}
static bool isUnlocked(int i) { return acc.b[i].level > 0; }

static void refreshUnlocks(int *newlyUnlocked) {
  *newlyUnlocked = -1;
  int tt = totalTrophies();
  for (int i = 0; i < 55; i++)
    if (acc.b[i].level == 0 && tt >= BRAWLERS[i].unlockTroph) {
      acc.b[i].level = 1; acc.b[i].trophies = 0;
      acc.b[i].gadget = false; acc.b[i].starPower = false; acc.b[i].hyper = false;
      *newlyUnlocked = i;
    }
  unlockedBrawlers = 0;
  for (int i = 0; i < 55; i++) if (isUnlocked(i)) unlockedBrawlers++;
}

static void initQuests(); // prototype : definie plus bas, appelee par initAcc
static void initAcc() {
  memset(&acc, 0, sizeof(acc));
  acc.coins = 100;
  if (loadAcc()) {
    // compte restaure : on recalcule juste les debloquages derives
    int newUnlock = -1;
    refreshUnlocks(&newUnlock);
    saveAcc();
    return;
  }
  // partie vierge : quetes initiales puis sauvegarde complete
  initQuests();
  int newUnlock = -1;
  refreshUnlocks(&newUnlock);
  saveAcc();
}

// ------------------------- Modes --------------------------------------
enum GameMode { MODE_GEM = 0, MODE_BALL, MODE_SOLO, MODE_DUO, MODE_HEIST, MODE_BOUNTY, MODE_KO, MODE_ZONE, MODE_SIEGE, MODE_DUEL, MODE_SURVIE, MODE_BOSSRUSH, MODE_COUNT };
static const char *MODE_NAMES[MODE_COUNT] = {
  "RAMASSE-GEMMES 3V3", "BRAWL BALL 3V3", "SURVIVANT 6J", "SURVIVANT DUO 4X2",
  "BRAQUAGE 3V3", "PRIME 3V3", "A-MORT 3V3", "ZONE CHAUDE 3V3", "SIEGE DU BOSS 3V1", "DUEL 3 BRAWLERS", "SURVIE : VAGUES", "BOSS RUSH COOP"
};
static const char *MODE_DESC[MODE_COUNT] = {
  "10 GEMMES + 15S", "2 BUTS POUR GAGNER", "DERNIER EN VIE", "DERNIERE EQUIPE",
  "DETROUISE LE COFFRE", "20 ETOILES DE KILLS", "ELIMINE L'EQUIPE !", "CAPTURE LES ZONES", "TUEZ LE MEGA ROBOT !", "3 VIES, DUO A DUO", "COMBIEN DE VAGUES ?", "3 BOSS A LA CHAINE !"
};

static uint8_t tileMap[44][32]; // taille MAX : la vraie taille (mapW, mapH) depend du mode
static int mapW = MAPW, mapH = MAPH;

// ------------------------- Entites ----------------------------------
#define MAX_ENT 14 // place pour les pets invoques (ours, tourelles, stations)
#define MAX_PROJ 64
#define MAX_POP 16
#define MAX_SAFE 4
#define MAX_ZONE 2 // V5.13 : 2 zones comme dans Brawl Stars (au lieu de 3)

struct Ent {
  int brawlerIdx, accIdx, team;
  float x, y, hp, maxHP;
  float ammo, superCharge;
  bool isHuman, alive;
  int respawnT;
  float regenT;
  float speedBuffT, hyperT, dashT, aimAng, faceAng;
  int dashDx, dashDy;
  float boostT, boostCd; // Carl : super = boost 5 s qui blesse au passage
  bool dashRet; // Colette : le dash fait un aller-retour
  int gems, goals, stars, kills;
  bool beaCharge; // V5.16 : Bea — la balle suivante est chargee si la precedente a touche
  int aiRvIdx; float aiRvT; // V5.17 : VENGEANCE — cible a traquer + timer
  uint8_t aiTilt; // V5.17 : TILT — morts en chaine sans kill → bot patient
  bool hasBall;
  int aiThinkT, aiWx, aiWy;
  float aiSkill, stuckT;
  uint8_t aiRole; // 0 = attaquant/buteur, 1 = defenseur, 2 = tueur
  uint8_t aiOnObj; // l'IA est-elle sur son objectif de mode
  int burstLeft, burstTickT; // rafale type Colt : balles qui partent l'une apres l'autre
  float burstAng, burstMul;
  int walkT, lastHitT, attackT;
  bool drawDone;
  bool isPet; int petOwner; uint8_t petKind; float petLife; // creature invoquee (0=ours 1=tourelle 2=station de soin)
  float stealthT, shieldT; // invisibilite (Leon/Sandy) et bouclier (Rosa)
  float slowT; // ralenti (drones de Bea, spray d Emz, balles de Piper)
  float poisonT; // V5.23 : poison de Byron — degats continus
  int poisonBy;  // V5.25 : qui a empoisonne (pour crediter le kill)
  float stunT; // V5.19 : etourdi par le taser de Buzz — ni bouger, ni tirer
  float blindT; // V5.13b : aveugle (super de Jacky) : ne voit plus rien, erre sans tirer
  float atkCharge; // charge de Hank : maintenir l'attaque pour gonfler le ballon
  float mechT; // Meg : temps restant en mega robot (super)
  float dmgDone;  // degats totaux infliges (stats de fin de match)
  int cubes;      // power cubes ramasses (Survivant : +10% PV et degats chacun)
  uint8_t isBoss, aiPers; // boss du Siege / personnalite IA (0 agressif, 1 defensif, 2 fourbe)
  float tpCd;     // recharge du teleporteur (anti aller-retour infini)
  int upStage; // paliers de Surge
};
static Ent ents[MAX_ENT]; static int entCount = 0;
static int humanIdx = -1;

struct Proj {
  bool active, lob, fromSuper, pierce;
  float x, y, vx, vy, dmg, travelled, range;
  int team, owner, radius, shape, spin;
  uint8_t hitIds[8]; int nHits; // pour les projectiles perforants : touche chaque cible une fois
  float mineT; // >0 : mine posee au sol (attaque de Tick), explose au contact ou a la fin du delai
  bool boomer, ret; // pic de Carl : boomerang qui revient vers son proprietaire
  float steer; // V5.17 : Nani — les balles s'ecartent puis REJOIGNENT la ligne de visee (rad/s)
  int bounce; // rebonds restants sur les murs (Rico)
  int bIdx; // V5.13 : brawler qui a tire (graphisme specifique du projectile)
};
static Proj projs[MAX_PROJ];

// zones au sol persistantes (cocktails de Barley) : degats toutes les 0.66 s pendant 2 s
struct FireZone { bool active; float x, y, life, tickT, dmg, r; int team, owner; };
static FireZone fireZones[8];
static void spawnFireZone(float x, float y, float dmg, int team, int owner, float life = 2.0f, float rad = 40.0f) {
  for (int i = 0; i < 8; i++)
    if (!fireZones[i].active) {
      fireZones[i].active = true;
      fireZones[i].x = x; fireZones[i].y = y; fireZones[i].dmg = dmg;
      fireZones[i].team = team; fireZones[i].owner = owner;
      fireZones[i].life = life; fireZones[i].tickT = 0.0f; fireZones[i].r = rad;
      return;
    }
}
static bool gPierceProj = false; // le prochain projectile perce tout (SUP_WAVE)
static float gLobRangeMul = -1.0f; // <0 = portee max ; sinon le lob atterrit sur sa cible
static bool gForceLob = false;     // force le prochain tir en lob (mines de Bo, grenades de Piper)
static float gFireOffX = 0, gFireOffY = 0; // V5.13 : decalage de spawn (rafales Colt/Rico : balles les unes derriere les autres)

struct Pop { bool active; float x, y; int life, val; uint8_t kind; }; // kind 0 blanc, 1 CRIT jaune, 2 SOIN vert
static Pop pops[MAX_POP];

struct Gem { bool active; float x, y; };
static Gem gems[10];

struct Safe { float x, y; float hp, maxHP; int team; };
static Safe safes[MAX_SAFE];

struct HotZone { float x, y, prog; int owner; }; // V5.13 : owner = equipe qui a touche la zone en premier (progression verrouillee)
static HotZone zones[MAX_ZONE];

static float ballX, ballY, ballVX, ballVY;
static int ballCd = 0; // V5.15 : delai de remise en jeu apres un but (anti double-but)
static int ballShooter = -1, ballShotT = 0; // V5.17 : le tireur ne peut pas rattraper sa passe aussitot
static int mode, gemCountdown, teamGems[5], teamStars[5];
static int soloShrink, poisonWaveT, matchTimeMs, respawnMs = 3000;
static int shakeT = 0, shakeMag = 0, animT = 0;
static float shakeDX = 0, shakeDY = 0; // V5.16 : direction du screen shake (recul opposé aux degats)
static bool gSuddenDone = false; // V5.16 : KO elimination deja declenchee
static int gKOBannerT = 0;      // V5.16 : banniere "ELIMINATOIRE !"
static int gBossN = 0, gBossPattern = 0, gBossT = 0; // V5.16 : BOSS RUSH : boss n°, pattern, timer
static float gFrameMs = 33.0f;  // V5.16 : EMA du temps de frame pour le compteur FPS
// V5.15 : BUT = ballon au centre + TOUT LE MONDE respawne a son camp, PV au max (comme BS)
static void ballResetAfterGoal() {
  ballX = mapW * TILE / 2; ballY = mapH * TILE / 2; ballVX = ballVY = 0;
  ballCd = 1500; // 1.5 s avant que le ballon redevienne jouable
  int row = 0;
  for (int i = 0; i < entCount; i++) {
    Ent &e = ents[i];
    if (e.isPet) { continue; }
    e.hasBall = false;
    e.hp = e.maxHP; e.regenT = 0; // remise a plat des deux equipes
    e.x = (e.team == 0) ? 40 : mapW * TILE - 40; // retour au camp
    e.y = mapH * TILE / 2 + ((row % 3) - 1) * 30;
    row++;
  }
  // V5.16 : plus de shake ici — evite la dependance a l'ordre de declaration des globals
}
static int matchResult = -2;
static int startCountdown = 0; // 3-2-1-GO au debut du match

// ---- V5.9 : evenements par match, siege du boss, power cubes, teleporteurs, tonneaux ----
// Aucune horloge, aucun internet : la rotation se fait par compteur de matchs.
#define MAX_BOX 6    // boites de power cubes (Survivant)
#define MAX_CUBE 20  // cubes au sol
#define MAX_TP 4     // teleporteurs (2 paires)
#define MAX_BARREL 4 // tonneaux explosifs (modes 3v3)
struct LootBox { bool active; float x, y, hp; };
struct Cube    { bool active; float x, y; };
struct Portal  { bool active; float x, y; int link; };
struct Barrel  { bool active; float x, y; };
static LootBox boxes[MAX_BOX];
static Cube cubes[MAX_CUBE];
static Portal portals[MAX_TP];
static Barrel barrels[MAX_BARREL];
static int matchMod = 0; // V5.16 : les evenements de match ont ete SUPPRIMES (boosts "foireux")
static float gAIDyn = 1.0f; // IA adaptative : ajustee apres chaque match selon le resultat
// ---- V5.10 : duel 3 brawlers (relais a la mort), emotes, skins, rejouer ----
static int duelQ[2][3] = {{0,0,0},{0,0,0}};   // les 3 brawlers de chaque equipe
static int duelAcc[2][3] = {{-1,-1,-1},{-1,-1,-1}}; // index de compte (-1 = IA)
static int duelNext[2] = {1, 1};             // prochain brawler a faire entrer
static int gEmoteT = 0, gEmoteKind = 0;      // emote affichee au-dessus du joueur
static bool gReplay = false;                 // rejouer instantanement le meme mode
// ---- V5.11 : survie par vagues ----
static int waveN = 0, waveBreakT = 0, gWaveBannerT = 0; // vague en cours, delai entre vagues, bandeau "VAGUE N"

// ---- Particules (impacts, poussieres, etincelles) ----
#define MAX_PART 40
struct Part { bool active; float x, y, vx, vy; int life; uint8_t kind; };
static Part parts[MAX_PART];
static void spawnParts(float x, float y, int kind, int n) {
  for (int i = 0; i < MAX_PART && n > 0; i++)
    if (!parts[i].active) {
      parts[i].active = true; parts[i].x = x; parts[i].y = y;
      parts[i].vx = (rnd(100) - 50) * 0.6f; parts[i].vy = (rnd(100) - 60) * 0.6f;
      parts[i].life = 300 + rnd(200); parts[i].kind = kind; n--;
    }
}
// ---- Killfeed (dernieres eliminations) ----
#define KILLFEED 3
struct Kill { int alive; int victim, killer; int life; };
static Kill killfeed[KILLFEED];
// (Quest, quests et les compteurs sont declares plus haut, avec saveAcc/loadAcc)
static void addKill(int victim, int killer) {
  if (killer == humanIdx) killCounter++;
  for (int i = KILLFEED - 1; i > 0; i--) killfeed[i] = killfeed[i - 1];
  killfeed[0].alive = 1; killfeed[0].victim = victim; killfeed[0].killer = killer;
  killfeed[0].life = 4000;
}

static void addPop(float x, float y, int val, int kind = 0) {
  for (int i = 0; i < MAX_POP; i++)
    if (!pops[i].active) {
      pops[i].active = true; pops[i].x = x; pops[i].y = y;
      pops[i].life = 900; pops[i].val = val; pops[i].kind = (uint8_t)kind;
      return;
    }
}

// ------------------------- Utilitaires monde ------------------------
static bool solidAt(float x, float y) {
  int tx = (int)(x / TILE), ty = (int)(y / TILE);
  if (tx < 0 || ty < 0 || tx >= mapW || ty >= mapH) return true;
  return tileMap[tx][ty] == 1;
}
static bool inBush(float x, float y) {
  int tx = (int)(x / TILE), ty = (int)(y / TILE);
  if (tx < 0 || ty < 0 || tx >= mapW || ty >= mapH) return false;
  return tileMap[tx][ty] == 2;
}
// destruction de murs par les supers (Shelly, Bull, El Primo, Colt... comme dans BS)
static void breakWalls(float cx, float cy, float r) {
  int t0x = (int)fmaxf(0, (cx - r) / TILE), t1x = (int)fminf(mapW - 1, (cx + r) / TILE);
  int t0y = (int)fmaxf(0, (cy - r) / TILE), t1y = (int)fminf(mapH - 1, (cy + r) / TILE);
  for (int tx = t0x; tx <= t1x; tx++)
    for (int ty = t0y; ty <= t1y; ty++) {
      float wx = tx * TILE + TILE / 2.0f, wy = ty * TILE + TILE / 2.0f;
      float dx = wx - cx, dy = wy - cy;
      if (tileMap[tx][ty] == 1 && dx * dx + dy * dy < r * r) {
        tileMap[tx][ty] = 0;
        spawnParts(wx, wy, 1, 6);
      }
    }
}
static void moveEnt(Ent &e, float dx, float dy) {
  // V5.13b : si l'entite est COINCEE DANS UN MUR (spawn rate, teleporteur...), on la laisse en sortir librement
  if (!solidAt(e.x, e.y)) {
    if (solidAt(e.x + dx, e.y)) dx = 0;
    if (solidAt(e.x, e.y + dy)) dy = 0;
  }
  e.x += dx; e.y += dy;
  e.x = fmaxf(8, fminf(mapW * TILE - 8, e.x));
  e.y = fmaxf(8, fminf(mapH * TILE - 8, e.y));
  if (dx || dy) { e.faceAng = (int)(atan2f(dy, dx) * 57.2957795f) & 255; e.walkT += (int)(fabsf(dx) + fabsf(dy)); }
}

// ------------------------- Generation carte --------------------------
// Rotation des variantes de carte : l'EADK n'expose pas la date, on
// utilise un compteur de matchs (aucun internet, aucune horloge requise).
// Chaque mode possede 3 variantes de disposition, elles tournent a chaque partie.
static int matchCounter = 0;
static int mapDayIndex() {
  return matchCounter % 6; // V5.11 : 6 variantes par mode (rotation par compteur de matchs)
}
// ---- V5.14 : saisons par compteur de matchs (3 parties par saison, aucune horloge) ----
static int seasonIdx() { return (matchCounter / 3) % 3; } // 0 = normal, 1 = AUTOMNE, 2 = NEIGE
static const char *SEASON_NAMES[3] = {"", "AUTOMNE", "NEIGE"}; // V5.16 : plus court, banniere discrete
static uint32_t seasonTint(uint32_t c) { // teinte les couleurs du terrain selon la saison
  int s = seasonIdx(); if (!s) return c;
  int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
  if (s == 1) { // automne : plus chaud, plus orange
    r = (r + 70 > 255) ? 255 : r + 70; if (g > 30) g -= 30; if (b > 40) b -= 40;
  } else {      // neige : tout est plus clair et bleute
    r = (r + 200) / 2; g = (g + 205) / 2; b = (b + 230) / 2;
  }
  return (r << 16) | (g << 8) | b;
}
static void genMap() {
  // on nettoie TOUT le tableau max : au-dela de la zone de jeu, tout est mur
  for (int x = 0; x < 44; x++)
    for (int y = 0; y < 32; y++)
      tileMap[x][y] = 1;
  int variant = mapDayIndex();
  uint16_t savedSeed = seed;
  seed = (uint16_t)(12345 + variant * 7919 + mode * 104729); // carte fixe du jour
  for (int x = 0; x < mapW; x++)
    for (int y = 0; y < mapH; y++) {
      tileMap[x][y] = 0;
      if (x == 0 || y == 0 || x == mapW - 1 || y == mapH - 1) tileMap[x][y] = 1;
    }
  // densite et motif selon la variante du jour
  int density = 85 + variant * 14;        // variante 0 : aeree, 5 : tres bourree
  for (int t = 0; t < density; t++) {
    int x = 1 + rnd(mapW / 2), y = 1 + rnd(mapH - 2);
    int v = rnd(10) < ((variant % 3 == 1) ? 5 : 4) ? 2 : 1;
    if (x < 5 || abs((int)(y - mapH / 2)) < 2) continue;
    if (variant % 3 == 2 && (x % 3 == 0)) continue;   // couloirs alignes
    tileMap[x][y] = v;
    tileMap[mapW - 1 - x][mapH - 1 - y] = v;
  }
  // structures symetriques propres a chaque variante
  if (variant >= 1) {
    for (int y = 4; y < mapH - 4; y++) {          // colonnes laterales
      tileMap[3][y] = 1; tileMap[mapW - 4][y] = 1;
    }
    for (int i = 0; i < 4; i++) {                 // blocs centraux
      tileMap[mapW / 2 - 3 + i][3] = 2;
      tileMap[mapW / 2 - 3 + i][mapH - 4] = 2;
    }
  }
  if (variant % 3 == 2) {                       // buissons protecteurs aux spawn (variantes 2 et 5)
    for (int i = 0; i < 3; i++) {
      tileMap[5][mapH / 2 - 3 + i * 3] = 2;
      tileMap[mapW - 6][mapH / 2 - 3 + i * 3] = 2;
    }
  }
  if (variant == 3) { // V5.11 : arene ouverte, 4 bastions aux quarts
    for (int i = 0; i < 2; i++)
      for (int j = 0; j < 2; j++) {
        int bx = mapW / 4 + i * (mapW / 2), by = mapH / 4 + j * (mapH / 2 - 2) - 1;
        for (int w = 0; w < 2; w++)
          for (int h = 0; h < 2; h++) tileMap[bx + w][by + h] = 1;
      }
  }
  if (variant == 4) { // V5.11 : la croix : couloir central borde de murs a breches
    for (int y = 2; y < mapH - 2; y++)
      if (y != mapH / 2 && y != mapH / 2 - 1 && y != mapH / 2 + 1 && y != 5 && y != mapH - 6) {
        tileMap[mapW / 2 - 3][y] = 1; tileMap[mapW / 2 + 3][y] = 1;
      }
    for (int x = 4; x < mapW - 4; x++)
      if (x != mapW / 2 && x != mapW / 2 - 1) { tileMap[x][3] = 1; tileMap[x][mapH - 4] = 1; }
  }
  if (variant == 5) { // V5.11 : la jungle : gros buissons au centre
    for (int t = 0; t < 40; t++) {
      int x = mapW / 2 - 6 + rnd(12), y = 2 + rnd(mapH - 4);
      int v = rnd(10) < 6 ? 2 : 1;
      if (abs((int)(y - mapH / 2)) < 2) continue;
      tileMap[x][y] = v;
    }
  }
  if (variant == 1) { // V5.16 : ARENE CIRCULAIRE — anneau de murs a 4 ouvertures autour du centre
    int ccx = mapW / 2, ccy = mapH / 2;
    for (int a = 0; a < 360; a += 3) {
      int seg = (a / 45) % 8; // 8 segments, 4 ouverts (1 sur 2)
      if (seg % 2 == 1) continue;
      int wx = ccx + (int)(6.5f * fcosf(a * 0.01745f));
      int wy = ccy + (int)(4.5f * fsinf(a * 0.01745f));
      if (wx > 1 && wy > 1 && wx < mapW - 2 && wy < mapH - 2 && abs(wy - ccy) > 1)
        tileMap[wx][wy] = 1;
    }
  }
  if (variant == 2) { // V5.16 : COULOIRS EN H — deux lignes de murs a breches + colonne de buissons
    for (int x = 3; x < mapW - 3; x++) {
      if (x != mapW / 2 && x != mapW / 2 - 1 && x % 7 != 0) { // breches regulieres
        tileMap[x][mapH / 4] = 1;
        tileMap[x][mapH - mapH / 4 - 1] = 1;
      }
    }
    for (int y = 2; y < mapH - 2; y++)
      if (y != mapH / 2 && y != mapH / 2 - 1) tileMap[mapW / 2][y] = 2; // colonne centrale de buissons (passable)
  }
  if (mode == MODE_BALL || mode == MODE_HEIST || mode == MODE_ZONE)
    for (int x = 1; x < mapW - 1; x++)
      for (int y = mapH / 2 - 1; y <= mapH / 2 + 1; y++) tileMap[x][y] = 0;
  if (mode == MODE_BALL) // bouches de but ouvertes dans le mur de bord : entre les poteaux uniquement
    for (int y = mapH / 2 - 2; y <= mapH / 2 + 1; y++) { tileMap[0][y] = 0; tileMap[mapW - 1][y] = 0; }
  seed = savedSeed; // restaure la graine du match pour l'aléa en jeu
}

// ------------------------- Spawn -------------------------------------
static float aiSkillFromTrophies() {
  float s = totalTrophies() / 1000.0f * gAIDyn; // IA adaptative : durcit apres les defaites, faiblit apres les victoires
  return fmaxf(0.0f, fminf(1.0f, s));
}

// V5.16 : munitions MAX selon le brawler — Carl (pic-boomerang) et Bea (charge) : 1 seule
static int ammoMax(int bIdx) { return (bIdx == 16 || bIdx == 24) ? 1 : 3; }
// V5.17 : niveau "metagame" du match = trophees du brawler joue (dose tous les reflexes d'IA)
static float matchTr() {
  if (humanIdx < 0 || ents[humanIdx].accIdx < 0) return 0.0f;
  return (float)acc.b[ents[humanIdx].accIdx].trophies;
}
static void addEnt(int bIdx, int accIdx, int team, float x, float y, bool human, float aiSkill) {
  Ent &e = ents[entCount++];
  const BrawlerDef &d = BRAWLERS[bIdx];
  int lvl = 1; bool star = false;
  if (accIdx >= 0) {
    lvl = acc.b[accIdx].level; star = acc.b[accIdx].starPower;
  } else {
    lvl = 1 + (int)(aiSkill * 9); star = aiSkill > 0.4f;
  }
  float mul = lvlMul(lvl);
  if (star && d.star == STAR_HP) mul *= 1.05f;
  e.brawlerIdx = bIdx; e.accIdx = accIdx; e.team = team;
  e.x = x; e.y = y;
  e.maxHP = d.baseHP * mul; e.hp = e.maxHP;
  e.ammo = ammoMax(bIdx); e.superCharge = 0;
  e.isHuman = human; e.alive = true; e.respawnT = 0;
  e.regenT = 0; e.speedBuffT = e.hyperT = e.dashT = 0; e.boostT = e.boostCd = 0; e.dashRet = false;
  e.aimAng = e.faceAng = (team == 0) ? 0 : 180;
  e.gems = 0; e.hasBall = false; e.goals = 0; e.stars = 0; e.kills = 0;
  e.aiThinkT = rnd(500); e.aiWx = x; e.aiWy = y;
  e.aiSkill = aiSkill; e.stuckT = 0; e.aiRole = 0; e.aiOnObj = 0;
  e.burstLeft = 0; e.burstTickT = 0; e.burstAng = 0; e.burstMul = 1;
  e.walkT = 0; e.lastHitT = 0; e.attackT = 0; e.drawDone = false;
  e.isPet = false; e.petOwner = -1; e.petKind = 0; e.petLife = 0; e.beaCharge = false;
  e.aiRvIdx = -1; e.aiRvT = 0; e.aiTilt = 0; // V5.17
  e.stealthT = 0; e.shieldT = 0; e.slowT = 0; e.stunT = 0; e.blindT = 0; e.poisonT = 0; e.poisonBy = -1; e.atkCharge = 0; e.upStage = 0; e.mechT = 0; e.dmgDone = 0; e.cubes = 0; e.isBoss = 0; e.aiPers = (uint8_t)rnd(3); e.tpCd = 0;
}

// invoque une creature devant son proprietaire (super de Nita, Jessie, Penny, 8-Bit, Pam)
static void spawnPet(Ent &owner, int kind) {
  // recycle le slot d'une creature morte, sinon ajoute (les slots morts ne se liberent jamais)
  int slot = -1;
  for (int i = 0; i < entCount; i++)
    if (ents[i].isPet && !ents[i].alive) { slot = i; break; }
  if (slot < 0) {
    if (entCount >= MAX_ENT) return;
    slot = entCount++;
  }
  // une seule creature vivante par proprietaire, sauf les rats d'Ash (plusieurs a la fois)
  for (int i = 0; i < entCount; i++)
    if (ents[i].isPet && ents[i].petOwner == (&owner - ents) && ents[i].alive && (kind != 3 || ents[i].petKind != 3)) { ents[i].hp = 0; ents[i].petLife = 0; }
  Ent &e = ents[slot];
  e.brawlerIdx = owner.brawlerIdx; e.accIdx = -1; e.team = owner.team;
  e.x = owner.x + fcosf(owner.aimAng) * 22; e.y = owner.y + fsinf(owner.aimAng) * 22;
  e.maxHP = (kind == 0) ? 4200 : (kind == 1 ? 3200 : (kind == 2 ? 2800 : (kind == 3 ? 900 : (kind == 4 ? 3000 : (kind == 6 ? 1600 : 2600)))));
  e.hp = e.maxHP; // V5.17 FIX : SANS cette ligne le pet spawnait a 0 PV et mourait aussitot (tourelle/tete invisibles !)
  e.ammo = 3; // V5.17 FIX : idem pour les munitions — la tourelle de Penny ne tirait JAMAIS (ammo non init)
  e.isHuman = false; e.alive = true; e.respawnT = 0;
  e.regenT = 0; e.speedBuffT = e.hyperT = e.dashT = 0; e.boostT = e.boostCd = 0; e.dashRet = false;
  e.aimAng = e.faceAng = owner.aimAng;
  e.gems = 0; e.hasBall = false; e.goals = 0; e.stars = 0; e.kills = 0;
  e.aiThinkT = 0; e.aiWx = e.x; e.aiWy = e.y;
  e.aiSkill = 0.8f; e.stuckT = 0; e.aiRole = 0; e.aiOnObj = 0;
  e.burstLeft = 0; e.burstTickT = 0; e.burstAng = 0; e.burstMul = 1;
  e.walkT = 0; e.lastHitT = 0; e.attackT = 0; e.drawDone = false;
  e.stealthT = 0; e.shieldT = 0; e.slowT = 0; e.stunT = 0; e.blindT = 0; e.poisonT = 0; e.poisonBy = -1; e.atkCharge = 0; e.upStage = 0; e.mechT = 0; e.dmgDone = 0; e.cubes = 0; e.isBoss = 0; e.aiPers = (uint8_t)rnd(3); e.tpCd = 0;
  e.isPet = true; e.petOwner = (&owner - ents); e.petKind = (uint8_t)kind;
  e.petLife = (kind == 3) ? 20.0f : ((kind == 4) ? 12.0f : ((kind == 6) ? 12.0f : 25.0f));
  spawnParts(e.x, e.y, 1, 6);
}

static void setupMatch(int selMode, int myBrawler) {
  mode = selMode; entCount = 0; humanIdx = -1; matchCounter++;
  for (int i = 0; i < MAX_PROJ; i++) projs[i].active = false;
  for (int i = 0; i < MAX_POP; i++) pops[i].active = false;
  for (int i = 0; i < MAX_PART; i++) parts[i].active = false;
  for (int i = 0; i < KILLFEED; i++) killfeed[i].alive = 0;
  for (int i = 0; i < 10; i++) gems[i].active = false;
  for (int i = 0; i < 8; i++) fireZones[i].active = false;
  for (int i = 0; i < MAX_SAFE; i++) { safes[i].hp = 0; safes[i].maxHP = 0; }
  for (int i = 0; i < MAX_ZONE; i++) { zones[i].prog = 0; zones[i].owner = -1; }
  // map plus grande en survivant duo (et solo) : 44x32 au lieu de 32x24
  mapW = (mode == MODE_SOLO || mode == MODE_DUO) ? 44 : MAPW;
  mapH = (mode == MODE_SOLO || mode == MODE_DUO) ? 32 : MAPH;
  genMap();
  for (int i = 0; i < 5; i++) { teamGems[i] = 0; teamStars[i] = 0; }
  gemCountdown = -1; matchTimeMs = 0; shakeT = 0; poisonWaveT = 0;
  gSuddenDone = false; gKOBannerT = 0; gBossN = 0; gBossPattern = 0; gBossT = 0; // V5.16
  startCountdown = 3000;
  float skill = aiSkillFromTrophies();

  int pool[55], pn = 0;
  for (int i = 0; i < 55; i++) if (isUnlocked(i)) pool[pn++] = i;
  auto pick = [&]() { return pool[rnd(pn)]; };

  if (mode == MODE_SOLO || mode == MODE_DUO) {
    if (mode == MODE_SOLO) {
      const int pos[6][2] = {{40,40},{mapW*TILE-40,40},{40,mapH*TILE-40},{mapW*TILE-40,mapH*TILE-40},{mapW*TILE/2,30},{mapW*TILE/2,mapH*TILE-30}};
      for (int i = 0; i < 6; i++) {
        int bIdx = (i == 0) ? myBrawler : pick();
        addEnt(bIdx, (i == 0) ? myBrawler : -1, i, pos[i][0], pos[i][1], i == 0, skill);
        if (i == 0) humanIdx = 0;
      }
    } else {
      const int pos[4][2] = {{40,40},{mapW*TILE-40,40},{40,mapH*TILE-40},{mapW*TILE-40,mapH*TILE-40}};
      addEnt(myBrawler, myBrawler, 0, pos[0][0], pos[0][1] - 25, true, skill); humanIdx = 0;
      addEnt(pick(), -1, 0, pos[0][0], pos[0][1] + 25, false, skill);
      for (int t = 1; t < 4; t++) {
        addEnt(pick(), -1, t, pos[t][0], pos[t][1] - 25, false, skill);
        addEnt(pick(), -1, t, pos[t][0], pos[t][1] + 25, false, skill);
      }
    }
    soloShrink = (mapW + mapH) * TILE / 2 + 200;
    poisonWaveT = 60000; // le poison n'arrive qu'apres 1 minute de partie
  } else {
    // V5.13 FIX : le poison du mode A-MORT (KO) n'etait jamais initialise (soloShrink=0 ->
    // cercle casse des la 1re seconde). Il arrive maintenant comme en Survivant : apres 1 min.
    if (mode == MODE_KO) { soloShrink = (mapW + mapH) * TILE / 2 + 200; poisonWaveT = 60000; }
    addEnt(myBrawler, myBrawler, 0, 40, mapH * TILE / 2, true, skill); humanIdx = 0;
    if (mode == MODE_SURVIE) { // SURVIE : seul contre des vagues de bots de plus en plus fortes
      waveN = 0; waveBreakT = 1600; gWaveBannerT = 0;
    } else if (mode == MODE_DUEL) { // DUEL : 1 contre 1, le brawler suivant entre a la mort du courant
      duelNext[0] = 1; duelNext[1] = 1;
      addEnt(duelQ[1][0], duelAcc[1][0], 1, mapW * TILE - 40, mapH * TILE / 2, false, skill);
    } else {
    int a1 = pick(); if (a1 == myBrawler) a1 = pick();
    int a2 = pick(); if (a2 == myBrawler || a2 == a1) a2 = pick();
    addEnt(a1, -1, 0, 40, mapH * TILE / 2 - 70, false, skill);
    addEnt(a2, -1, 0, 40, mapH * TILE / 2 + 70, false, skill);
    if (mode == MODE_SIEGE || mode == MODE_BOSSRUSH) { // V5.16 : BOSS RUSH = 3 boss a la chaine
      addEnt(47, -1, 1, mapW * TILE - 80, mapH * TILE / 2, false, 0.9f);
      Ent &bs = ents[entCount - 1];
      bs.isBoss = 1; bs.mechT = 1.0f;
      bs.maxHP = (mode == MODE_BOSSRUSH) ? (30000 + skill * 15000) : (42000 + skill * 28000);
      bs.hp = bs.maxHP; // scale avec tes trophees
      tileMap[(int)(bs.x / TILE)][(int)(bs.y / TILE)] = 0; // le boss n'apparait jamais dans un mur
    } else {
    int e1 = pick(), e2 = pick(), e3 = pick();
    addEnt(e1, -1, 1, mapW * TILE - 40, mapH * TILE / 2, false, skill);
    addEnt(e2, -1, 1, mapW * TILE - 40, mapH * TILE / 2 - 70, false, skill);
    addEnt(e3, -1, 1, mapW * TILE - 40, mapH * TILE / 2 + 70, false, skill);
    }
    }

    if (mode == MODE_GEM)
      for (int i = 0; i < 6; i++) {
        gems[i].active = true;
        gems[i].x = mapW * TILE / 2 + (rnd(3) - 1) * 30;
        gems[i].y = mapH * TILE / 2 + (rnd(3) - 1) * 30;
      }
    else if (mode == MODE_BALL) { ballX = mapW * TILE / 2; ballY = mapH * TILE / 2; ballVX = ballVY = 0; ballCd = 0; }
    else if (mode == MODE_HEIST) {
      safes[0].team = 0; safes[0].x = 30; safes[0].y = mapH * TILE / 2; safes[0].maxHP = 26000; safes[0].hp = safes[0].maxHP;
      safes[1].team = 1; safes[1].x = mapW * TILE - 30; safes[1].y = mapH * TILE / 2; safes[1].maxHP = 26000; safes[1].hp = safes[1].maxHP;
    }
    else if (mode == MODE_ZONE) { // V5.13 : 2 zones placees a gauche/droite du centre
      zones[0].x = mapW * TILE / 2 - 100; zones[0].y = mapH * TILE / 2;
      zones[1].x = mapW * TILE / 2 + 100; zones[1].y = mapH * TILE / 2;
      for (int z = 0; z < MAX_ZONE; z++) // on degage les murs/buissons autour des zones
        for (int dy = -2; dy <= 2; dy++)
          for (int dx = -2; dx <= 2; dx++) {
            int tx = (int)(zones[z].x / TILE) + dx, ty = (int)(zones[z].y / TILE) + dy;
            if (tx >= 0 && tx < mapW && ty >= 0 && ty < mapH) tileMap[tx][ty] = 0;
          }
    }
  }
  // V5.13b FIX : personne ne doit apparaitre DANS un mur (les bots s'y terraient et devenaient intouchables)
  for (int i = 0; i < entCount; i++)
    for (int dy = -1; dy <= 1; dy++)
      for (int dx = -1; dx <= 1; dx++) {
        int tx = (int)(ents[i].x / TILE) + dx, ty = (int)(ents[i].y / TILE) + dy;
        if (tx >= 0 && tx < mapW && ty >= 0 && ty < mapH) tileMap[tx][ty] = 0;
      }
  // ---- V5.9 : evenement du match + placement des objets (cubes, teleporteurs, tonneaux) ----
  matchMod = 0; // V5.16 : plus d'evenement de match (boosts supprimes)
  for (int i = 0; i < MAX_BOX; i++) boxes[i].active = false;
  for (int i = 0; i < MAX_CUBE; i++) cubes[i].active = false;
  for (int i = 0; i < MAX_TP; i++) portals[i].active = false;
  for (int i = 0; i < MAX_BARREL; i++) barrels[i].active = false;
  if (mode == MODE_SOLO || mode == MODE_DUO) { // boites de power cubes autour du centre
    for (int b = 0; b < 6; b++) {
      boxes[b].active = true; boxes[b].hp = 3000;
      boxes[b].x = mapW * TILE / 2 + ((b % 2) ? 1 : -1) * (60 + (b / 2) * 90);
      boxes[b].y = mapH * TILE / 2 + (((b / 2) % 2) ? 1 : -1) * (40 + (b / 2) * 70);
      tileMap[(int)(boxes[b].x / TILE)][(int)(boxes[b].y / TILE)] = 0;
    }
  } else { // teleporteurs et tonneaux sur les cartes 3v3
    if (mode == MODE_GEM || mode == MODE_BOUNTY || mode == MODE_ZONE || mode == MODE_KO) {
      float mW = mapW * TILE, mH = mapH * TILE;
      portals[0].active = portals[1].active = true; // diagonale
      portals[0].x = 3 * TILE + 8; portals[0].y = 3 * TILE + 8; portals[0].link = 1;
      portals[1].x = mW - 3 * TILE - 8; portals[1].y = mH - 3 * TILE - 8; portals[1].link = 0;
      portals[2].active = portals[3].active = true; // anti-diagonale
      portals[2].x = mW - 3 * TILE - 8; portals[2].y = 3 * TILE + 8; portals[2].link = 3;
      portals[3].x = 3 * TILE + 8; portals[3].y = mH - 3 * TILE - 8; portals[3].link = 2;
      for (int t = 0; t < 4; t++) tileMap[(int)(portals[t].x / TILE)][(int)(portals[t].y / TILE)] = 0;
    }
    for (int b = 0; b < 4; b++) { // tonneaux explosifs autour du centre
      barrels[b].active = true;
      barrels[b].x = mapW * TILE / 2 + ((b % 2) ? 1 : -1) * (90 + (b / 2) * 60);
      barrels[b].y = mapH * TILE / 2 + ((b < 2) ? 1 : -1) * 70;
      tileMap[(int)(barrels[b].x / TILE)][(int)(barrels[b].y / TILE)] = 0;
    }
  }
  // roles IA : dans chaque equipe, le 1er attaque l'objectif, le 2e defend, le 3e chasse
  {
    int memberCount[5] = {0, 0, 0, 0, 0};
    for (int i = 0; i < entCount; i++) {
      if (ents[i].isHuman) { ents[i].aiRole = 0; continue; }
      int r = memberCount[ents[i].team]++ % 3;
      ents[i].aiRole = (uint8_t)r;
    }
  }
}

// ------------------------- Projectiles ------------------------------
static void fireProj(Ent &e, float ang, bool isSuper, float dmgMul, float spdMul, float rangeMul) {
  const BrawlerDef &d = BRAWLERS[e.brawlerIdx];
  for (int i = 0; i < MAX_PROJ; i++) {
    if (projs[i].active) continue;
    Proj &p = projs[i];
    p.active = true; p.lob = (d.atk == ATK_LOB) || gForceLob; p.fromSuper = isSuper;
    p.x = e.x + gFireOffX; p.y = e.y + gFireOffY; // V5.13 : decalage de rafale
    float sp = d.projSpeed * spdMul;
    p.vx = fcosf(ang) * sp; p.vy = fsinf(ang) * sp;
    p.dmg = d.baseDmg * dmgMul;
    if (e.brawlerIdx == 24 && e.beaCharge && !isSuper) { p.dmg *= 3.0f; e.beaCharge = false; } // V5.16 : Bea CHARGEE (balle precedente a touche) : x3 !
    p.range = RNG(d.range) * rangeMul;
    if (p.lob && gLobRangeMul > 0) p.range = p.range * fminf(1.0f, gLobRangeMul); // atterrit sur la cible
    p.travelled = 0; p.team = e.team; p.owner = (&e - ents);
    p.radius = isSuper ? 4 : 2;
    p.boomer = (!isSuper && e.brawlerIdx == 16); p.ret = false; // Carl : pic-boomerang
    p.pierce = gPierceProj || p.boomer || (!isSuper && !e.isPet && (e.brawlerIdx == 7 || e.brawlerIdx == 19 || e.brawlerIdx == 22)); // V5.16 : eventail PERCEUR de Poco/Emz/Frank
    if (p.pierce) p.radius = p.boomer ? 7 : 3; // pic de Carl grand, balles perceuses petites
    if (!isSuper && !e.isPet && (e.brawlerIdx == 7 || e.brawlerIdx == 19 || e.brawlerIdx == 22)) p.radius = 7; // V5.16 : attaque de ZONE — l'eventail est LARGE
    p.nHits = 0;
    p.shape = d.projShape; p.spin = 0; p.mineT = 0;
    p.bIdx = e.brawlerIdx; // V5.13 : graphisme du projectile selon le brawler
    p.bounce = ((e.brawlerIdx == 13 || e.brawlerIdx == 23) && !e.isPet) ? (e.brawlerIdx == 13 ? 3 : 1) : 0; // Rico rebondit 3x, Bibi 1x
    p.steer = 0; // V5.17 : pas de convergence par defaut
    // V5.17 : NANI — ses 3 balles s'ecartent en eventail puis SE REJOIGNENT sur la ligne
    // de visee a mi-portee (comme le vrai Nani : ca resserre le garrot au bout)
    if (!isSuper && !e.isPet && e.brawlerIdx == 25) {
      float base = atan2f(p.vy, p.vx);
      float diff = base - e.aimAng;
      while (diff > 3.1416f) diff -= 6.2832f;
      while (diff < -3.1416f) diff += 6.2832f;
      float tEnd = p.range / fmaxf(1.0f, d.projSpeed * spdMul); // elles se rejoignent a LA FIN du trajet
      if (fabsf(diff) > 0.01f && tEnd > 0.05f) p.steer = -diff / tEnd;
    }
    if (!isSuper) spawnParts(e.x + fcosf(ang) * 10, e.y + fsinf(ang) * 10 - 4, 0, 2); // FLASH DE BOUCHE
    return;
  }
}

static void damageEnt(Ent &e, float dmg, Ent &src); // declaree ici car appelee par doSuper avant sa definition
// boites de cubes (Survivant) et tonneaux explosifs : endommages par les tirs
static void hitDestructibles(Proj &p) {
  for (int b = 0; b < MAX_BOX; b++) {
    if (!boxes[b].active) continue;
    float dx = boxes[b].x - p.x, dy = boxes[b].y - p.y;
    if (dx * dx + dy * dy < (p.radius + 8) * (p.radius + 8)) {
      boxes[b].hp -= p.dmg;
      spawnParts(p.x, p.y, 0, 3);
      if (boxes[b].hp <= 0) { // la boite eclate et lache 2 power cubes
        boxes[b].active = false;
        spawnParts(boxes[b].x, boxes[b].y, 0, 12);
        shakeT = 120; shakeMag = 2;
        for (int c = 0; c < 2; c++)
          for (int k = 0; k < MAX_CUBE; k++) if (!cubes[k].active) {
            cubes[k].active = true;
            cubes[k].x = boxes[b].x + (rnd(30) - 15); cubes[k].y = boxes[b].y + (rnd(30) - 15);
            break;
          }
      }
      if (!p.pierce) { p.active = false; return; }
    }
  }
  for (int b = 0; b < MAX_BARREL; b++) {
    if (!barrels[b].active) continue;
    float dx = barrels[b].x - p.x, dy = barrels[b].y - p.y;
    if (dx * dx + dy * dy < (p.radius + 8) * (p.radius + 8)) {
      barrels[b].active = false;
      spawnParts(barrels[b].x, barrels[b].y, 0, 16);
      shakeT = 200; shakeMag = 4;
      breakWalls(barrels[b].x, barrels[b].y, 26); // l'explosion defonce aussi les murs voisins
      for (int j = 0; j < entCount; j++) { // degats de zone pour TOUT le monde (alliés compris)
        Ent &t = ents[j];
        if (!t.alive) continue;
        float ddx = t.x - barrels[b].x, ddy = t.y - barrels[b].y;
        if (ddx * ddx + ddy * ddy < 55 * 55) {
          float bl = sqrtf(ddx * ddx + ddy * ddy);
          if (bl > 0.01f) { t.x += ddx / bl * 42; t.y += ddy / bl * 42; } // V5.16 : le tonneau PROPELLE au lieu de juste blesser
          damageEnt(t, 3000, ents[p.owner]);
        }
      }
      if (!p.pierce) { p.active = false; return; }
    }
  }
}

// V5.17 : reflexes d'IA — vengeance, tilt, chat (bulle) — tous doses par matchTr()
static int gChatEnt = -1, gChatT = 0, gChatIdx = 0; // V5.17 : bulle de chat des bots (rare)
static float gHankHoldT = 0; // V5.17 : temps de maintien de la bulle Hank a charge max (3 s)
static void doAttack(Ent &e, float ang) {
  const BrawlerDef &d = BRAWLERS[e.brawlerIdx];
  if (e.ammo < 1) return;
  e.aimAng = ang;
  float mul = lvlMul(e.accIdx >= 0 ? acc.b[e.accIdx].level : 1);
  if (e.hyperT > 0) mul *= 1.25f;
  if (e.accIdx >= 0 && acc.b[e.accIdx].starPower && d.star == STAR_DMG) mul *= 1.1f;
  int n = d.projCount + e.upStage, spread = d.spreadDeg; // Surge : +1 balle par palier
  // Hank : la charge accumulee gonfle le ballon (plus de degats, plus de portee)
  float hm = 1.0f, hrm = 1.0f;
  if ((e.brawlerIdx == 37 || e.brawlerIdx == 32) && !e.isPet) { // Hank / Ash : attaque a charge
    float ch = fminf(1.2f, e.atkCharge);
    hm = 1.0f + ch * 0.9f; hrm = 1.0f + ch * 0.45f;
    e.atkCharge = 0;
  }
  // Mortis : chaque coup de pelle le propulse en avant
  if (e.brawlerIdx == 45 && !e.isPet) { e.dashT = 0.15f; e.dashDx = (int)(fcosf(ang) * 240); e.dashDy = (int)(fsinf(ang) * 240); }
  // V5.17 : Hank — il ne TIRE PAS de projectile : la bulle gonfle devant lui puis
  // ECLATE sur place — les ennemis dans le rayon prennent les degats (comme le vrai Hank)
  if (e.brawlerIdx == 37 && !e.isPet) {
    float ch = hm - 1.0f;                        // 0 a ~1.1 selon la charge
    float R = 48 + ch * 42;                      // rayon de l'explosion selon la charge
    float bx = e.x + fcosf(ang) * 14, by = e.y + fsinf(ang) * 14;
    for (int j = 0; j < entCount; j++) {
      Ent &t = ents[j];
      if (!t.alive || t.team == e.team) continue;
      float ddx = t.x - bx, ddy = t.y - by;
      if (ddx * ddx + ddy * ddy < R * R) damageEnt(t, d.baseDmg * mul * hm * 0.85f, e);
    }
    spawnParts(bx, by, 0, 18); shakeT = 150; shakeMag = 3;
    e.ammo -= 1; e.regenT = 0; e.attackT = 200; gLobRangeMul = -1.0f;
    return; // PAS de projectile : la bulle a deja eclate
  }
  // V5.17 : FANG — le coup de chaussure fait TRES mal au corps a corps + une chaussure
  // lente qui file tout droit et ne pique qu'un peu (comme le vrai Fang)
  if (e.brawlerIdx == 34 && !e.isPet) {
    float bx = e.x + fcosf(ang) * 16, by = e.y + fsinf(ang) * 16;
    bool touched = false;
    for (int j = 0; j < entCount; j++) {
      Ent &t = ents[j];
      if (!t.alive || t.team == e.team) continue;
      float ddx = t.x - bx, ddy = t.y - by;
      if (ddx * ddx + ddy * ddy < 32 * 32) { damageEnt(t, d.baseDmg * mul * 1.15f, e); touched = true; }
    }
    if (touched) spawnParts(bx, by, 0, 8);
    fireProj(e, ang, false, mul * 0.45f, 0.7f, 1.3f); // la chaussure volante, lente
    e.ammo -= 1; e.regenT = 0; e.attackT = 200; gLobRangeMul = -1.0f;
    return;
  }
  if (e.brawlerIdx == 28 && n > 1) { // V5.21 : GALE — ses balles partent COTE A COTE (paralleles)
    for (int i = 0; i < n; i++) {
      float off = (i - (n - 1) / 2.0f) * 11.0f; // decalage perpendiculaire a la visee
      gFireOffX = -fsinf(ang) * off; gFireOffY = fcosf(ang) * off;
      fireProj(e, ang, false, mul * hm, 1.0f, hrm);
    }
    gFireOffX = 0; gFireOffY = 0;
  } else if (n > 1 && d.atk == ATK_SHOT && e.brawlerIdx != 25) { // Nani EXCLUE : ses 3 balles partent en eventail d'un coup
    // tir en rafale (Colt, Rico, Bo...) : les balles partent l'une apres l'autre,
    // pas toutes d'un coup en eventail. La premiere part maintenant.
    fireProj(e, ang, false, mul * hm, 1.0f, hrm);
    e.burstLeft = n - 1; e.burstAng = ang; e.burstMul = mul * hm; e.burstTickT = 70;
  } else {
    for (int i = 0; i < n; i++) {
      float a = ang + ((n > 1) ? (spread * 0.01745f * (i - (n - 1) / 2.0f)) : 0);
      fireProj(e, a, false, mul * hm, 1.0f, hrm);
    }
  }
  e.ammo -= 1; e.regenT = 0; e.attackT = 200; gLobRangeMul = -1.0f;
  // Lola : son ego tire au meme endroit qu'elle
  if (e.brawlerIdx == 33 && !e.isPet) {
    for (int i = 0; i < entCount; i++) {
      Ent &g = ents[i];
      if (g.alive && g.isPet && g.petKind == 4 && g.petOwner == (&e - ents)) {
        fireProj(g, ang, false, mul * hm, 1.0f, 0.7f);
        g.attackT = 150; g.aimAng = g.faceAng = ang;
        break;
      }
    }
  }
}

static void doSuper(Ent &e, float ang) {
  const BrawlerDef &d = BRAWLERS[e.brawlerIdx];
  if (e.superCharge < 100) return;
  e.superCharge = 0; e.aimAng = ang; e.attackT = 300;
  if (e.isHuman) superCounter++;
  float mul = lvlMul(e.accIdx >= 0 ? acc.b[e.accIdx].level : 1);
  if (e.hyperT > 0) mul *= 1.25f;
  shakeT = 200; shakeMag = 3;
  switch (d.sup) {
    case SUP_BURST: {
      breakWalls(e.x, e.y, 70); // le super dezone : detruit les murs autour (Shelly, Jacky...)
      for (int i = 0; i < entCount; i++)
        if (ents[i].alive && ents[i].team != e.team) {
          float dx = ents[i].x - e.x, dy = ents[i].y - e.y;
          if (dx * dx + dy * dy < 80 * 80) {
            ents[i].hp -= d.baseDmg * 1.6f * mul; ents[i].regenT = 0; ents[i].lastHitT = 150;
            if (e.brawlerIdx == 17 && !ents[i].isPet) ents[i].blindT = 2.5f; // V5.13b : super de JACKY = AVEUGLE 2.5 s
          }
        }
      for (int a = 0; a < 16; a++) fireProj(e, a * 0.3927f, true, mul * 0.6f, 1.2f, 0.5f);
      break;
    }
    case SUP_DASH: { // charge qui defonce les murs (Bull, Darryl, El Primo...)
      if (e.brawlerIdx == 16) { // Carl : vitesse spectaculaire 5 s + degats en passant (casse PAS les murs)
        e.speedBuffT = 5.0f; e.boostT = 5.0f; e.boostCd = 0;
        spawnParts(e.x, e.y, 1, 10);
      } else if (e.brawlerIdx == 20) { // Stu : dash fulgurant, casse PAS les murs
        e.dashT = 0.25f; e.speedBuffT = 1.0f;
        e.dashDx = (int)(fcosf(ang) * 380); e.dashDy = (int)(fsinf(ang) * 380);
      } else if (e.brawlerIdx == 29) { // Colette : aller-retour, casse PAS les murs
        e.dashT = 0.4f; e.dashRet = true;
        e.dashDx = (int)(fcosf(ang) * 330); e.dashDy = (int)(fsinf(ang) * 330);
      } else if (e.brawlerIdx == 14) { // V5.17 : Darryl — long tonneau qui REBONDIT sur les murs (sans les casser)
        e.dashT = 0.8f; e.dashDx = (int)(fcosf(ang) * 340); e.dashDy = (int)(fsinf(ang) * 340);
      } else if (e.brawlerIdx == 31) { // V5.19 : Buzz — coup de taser : il fonce mais fait 0 degat, il ETOURDIT
        e.dashT = 0.4f;
        e.dashDx = (int)(fcosf(ang) * 360); e.dashDy = (int)(fsinf(ang) * 360);
      } else if (e.brawlerIdx == 34) { // Fang : le coup volant — il FONCE sur l'ennemi le plus
        // proche puis REBONDIT d'ennemi en ennemi proche, SANS casser ni traverser les murs
        bool hit[MAX_ENT] = {};
        for (int step = 0; step < 4; step++) {
          int best = -1; float bd = 190.0f;
          for (int j = 0; j < entCount; j++) {
            Ent &o = ents[j];
            if (!o.alive || o.isPet || o.team == e.team || hit[j]) continue;
            float dx = o.x - e.x, dy = o.y - e.y;
            float dd = sqrtf(dx * dx + dy * dy);
            if (dd < bd) { bd = dd; best = j; }
          }
          if (best < 0) break;
          // il avance par petits pas : un mur l'arrete NET (plus de teleportation a travers)
          float tx = ents[best].x, ty = ents[best].y;
          float ddx = tx - e.x, ddy = ty - e.y, dl = sqrtf(ddx * ddx + ddy * ddy);
          if (dl < 0.01f) { hit[best] = true; continue; }
          bool reached = false;
          for (int st = 0; st < 40 && !reached; st++) {
            float ox = e.x, oy = e.y;
            moveEnt(e, ddx / dl * 8.0f, ddy / dl * 8.0f);
            if (fabsf(e.x - ox) + fabsf(e.y - oy) < 0.5f) break; // bloque par un mur : fin du rebond
            float rdx = tx - e.x, rdy = ty - e.y;
            if (rdx * rdx + rdy * rdy < 22 * 22) reached = true;
          }
          if (!reached) break; // le mur a arrete la charge : le super s'arrete la
          hit[best] = true;
          damageEnt(ents[best], 1000.0f, e);
          spawnParts(e.x, e.y, 0, 6);
        }
      } else {
        e.dashT = 0.35f; e.dashDx = (int)(fcosf(ang) * 320); e.dashDy = (int)(fsinf(ang) * 320); breakWalls(e.x, e.y, 40);
      }
      break; }
    case SUP_HEAL: {
      if (e.brawlerIdx == 46) { // V5.23 : BYRON — lance une POTION : soigne les allies touches, brule les ennemis
        gForceLob = true;
        fireProj(e, ang, true, mul, 0.9f, 0.85f);
        gForceLob = false;
        break;
      }
      if (e.brawlerIdx == 38) { // Pearl : onde de choc - casse les murs proches et REPOUSSE (pas de soin)
        breakWalls(e.x, e.y, 70);
        for (int i = 0; i < entCount; i++) {
          Ent &o = ents[i];
          if (!o.alive || o.team == e.team) continue;
          float dx = o.x - e.x, dy = o.y - e.y;
          float dd = sqrtf(dx * dx + dy * dy);
          if (dd < 130 && dd > 0.01f) {
            o.x += dx / dd * 70; o.y += dy / dd * 70; // repousse
            damageEnt(o, 500.0f, e);
            spawnParts(o.x, o.y, 0, 4);
          }
        }
        shakeT = 150; shakeMag = 3;
        spawnParts(e.x, e.y, 2, 12);
        break;
      }
      for (int i = 0; i < entCount; i++)
        if (ents[i].alive && ents[i].team == e.team) {
          float before = ents[i].hp;
          ents[i].hp = fminf(ents[i].maxHP, ents[i].hp + ents[i].maxHP * 0.4f);
          if (ents[i].hp > before) addPop(ents[i].x, ents[i].y - 12, (int)(ents[i].hp - before), 2); // soin = vert
        }
      break;
    }
    case SUP_BARRAGE: {
      // Barley : 5 bouteilles en eventail (au lieu de 8 au hasard)
      if (e.brawlerIdx == 6) {
        for (int i = 0; i < 5; i++) fireProj(e, ang + (i - 2) * 0.13f, true, mul, 1.0f, 1.3f);
      } else if (e.brawlerIdx == 10) {
        // Dynamike : UNE giga-dynamite (degats x2.2, rayon d'explosion x2) - tres cher a charger
        fireProj(e, ang, true, mul * 2.2f, 1.0f, 1.25f);
        shakeT = 150; shakeMag = 3;
      } else if (e.brawlerIdx == 37) {
        // Hank : 8 projectiles en cercle ET regenere 30% de ses PV
        for (int i = 0; i < 8; i++) fireProj(e, ang + i * 0.7854f, true, mul * 0.75f, 1.0f, 1.15f);
        float before = e.hp;
        e.hp = fminf(e.maxHP, e.hp + e.maxHP * 0.3f);
        if (e.hp > before) addPop(e.x, e.y - 12, (int)(e.hp - before));
        spawnParts(e.x, e.y, 1, 8);
      } else if (e.brawlerIdx == 43) {
        // Amber : barrage de flammes qui laisse des flaques enflammees (fortes mais courtes)
        gForceLob = true;
        for (int i = 0; i < 10; i++) fireProj(e, ang + (i - 4.5f) * 0.05f, true, mul * 0.55f, 1.4f, 1.15f);
        gForceLob = false;
      } else {
        for (int i = 0; i < 8; i++) fireProj(e, ang + (rnd(30) - 15) * 0.01745f, true, mul, 1.0f, 1.3f);
      }
      break;
    }
    case SUP_MEGASHOT: { // ULTI Colt/Rico : RAFALE de balles les unes derriere les autres, perce et defonce les murs
      if (e.brawlerIdx == 2 || e.brawlerIdx == 13) {
        gPierceProj = true; // les balles traversent tout et cassent les murs au passage
        int n = (e.brawlerIdx == 2) ? 8 : 6; // Colt : longue rafale ; Rico : rafale rebondissante
        for (int i = 0; i < n; i++) {
          gFireOffX = -fcosf(ang) * i * 9; gFireOffY = -fsinf(ang) * i * 9; // chaque balle part DERRIERE la precedente
          fireProj(e, ang, true, mul, 1.3f, 1.7f);
        }
        gFireOffX = 0; gFireOffY = 0;
        gPierceProj = false;
      } else { // Brock & autres : eventail perceur
        for (int i = 0; i < 5; i++) fireProj(e, ang + (i - 2) * 0.06f, true, mul * 0.75f, 1.3f, 1.6f);
      }
      break;
    }
    case SUP_WAVE: { // onde de choc perforante qui traverse tout (Frank)
      gPierceProj = true;
      fireProj(e, ang, true, mul * 1.5f, 0.9f, 1.4f);
      gPierceProj = false;
      if (e.brawlerIdx == 45) { // Mortis : son super le SOIGNE (35% des PV)
        float before = e.hp;
        e.hp = fminf(e.maxHP, e.hp + e.maxHP * 0.35f);
        if (e.hp > before) addPop(e.x, e.y - 12, (int)(e.hp - before));
        spawnParts(e.x, e.y, 1, 6);
      }
      break;
    }
    case SUP_SUMMON: { // creature invoquee (Nita ours, Jessie/8-Bit/Penny tourelle, Pam station, Ash rats, Lola ego, Mr. P pingouins)
      if (e.brawlerIdx == 15) { // V5.19 : Penny — FINI la tourelle qui ne touche que les cibles immobiles :
        // son ulti devient une PLUIE DE PIECES facon Dynamike : 3 impacts de mortier en eventail,
        // zone d'attaque visee, degats seulement si l'ennemi reste dedans a l'impact
        gForceLob = true;
        fireProj(e, ang - 14 * 0.01745f, true, mul * 0.5f, 0.8f, 0.85f);
        fireProj(e, ang, true, mul * 0.7f, 0.8f, 1.0f);
        fireProj(e, ang + 14 * 0.01745f, true, mul * 0.5f, 0.8f, 0.85f);
        gForceLob = false;
        spawnParts(e.x, e.y, 1, 8);
        break;
      }
      int kind = 1;
      if (e.brawlerIdx == 1) kind = 0; // Nita : ours
      else if (e.brawlerIdx == 11) kind = 6; // Tick : sa tete-mine
      else if (e.brawlerIdx == 25) kind = 6; // Nani : Peep, bombe qui fonce sur l'ennemi
      else if (e.brawlerIdx == 12) kind = 7; // 8-Bit : tourelle BOOSTER (ne tire pas, booste les degats allies)
      else if (e.brawlerIdx == 21) kind = 2; // Pam : station de soin
      else if (e.brawlerIdx == 32) kind = 3; // Ash : rats
      else if (e.brawlerIdx == 33) kind = 4; // Lola : ego tireur
      else if (e.brawlerIdx == 54) kind = 5; // Mr. P : pingouin
      spawnPet(e, kind);
      if (kind == 3) { // Ash : remplace ses anciens rats par 8 rats faibles
        for (int i = 0; i < entCount; i++)
          if (ents[i].isPet && ents[i].petOwner == (&e - ents) && ents[i].petKind == 3 && ents[i].alive) { ents[i].hp = 0; ents[i].petLife = 0; }
        for (int r = 0; r < 8; r++) spawnPet(e, 3);
      }
      break;
    }
    case SUP_STEALTH: { // Leon : LUI SEUL devient invisible | Sandy : tempete de sable pour l'equipe proche
      if (e.brawlerIdx == 41) {
        e.stealthT = 8.0f; spawnParts(e.x, e.y, 1, 8);
      } else {
      for (int i = 0; i < entCount; i++) {
        Ent &a = ents[i];
        if (!a.alive || a.team != e.team || a.isPet) continue;
        float dx = a.x - e.x, dy = a.y - e.y;
        if (dx * dx + dy * dy < 260 * 260) { a.stealthT = 8.0f; spawnParts(a.x, a.y, 1, 5); }
      }
      }
      break;
    }
    case SUP_SHIELD: { e.shieldT = 4.0f; spawnParts(e.x, e.y, 1, 6); break; } // carapace de Rosa : -65% degats
    case SUP_PULL: { // main magique de Gene / trou noir de Tara : attire et blesse
      for (int i = 0; i < entCount; i++) {
        Ent &o = ents[i];
        if (!o.alive || o.team == e.team || o.isPet) continue;
        float dx = e.x - o.x, dy = e.y - o.y, dd = sqrtf(dx * dx + dy * dy);
        if (dd > 20 && dd < 240) {
          float pull = fminf(dd - 40, 130);
          moveEnt(o, dx / dd * pull, dy / dd * pull);
          damageEnt(o, d.baseDmg * mul, e);
        }
      }
      break;
    }
    case SUP_PUSH: { // blizzard de Gale : rafale de balles cote a cote (comme son attaque, mais 6) + poussee
      spawnParts(e.x, e.y, 1, 16);
      for (int i = 0; i < 6; i++) { // V5.22 : 6 balles PARALLELES, comme l'attaque
        float off = (i - 2.5f) * 13.0f;
        gFireOffX = -fsinf(ang) * off; gFireOffY = fcosf(ang) * off;
        fireProj(e, ang, true, mul * 0.6f, 0.85f, 0.7f);
      }
      gFireOffX = 0; gFireOffY = 0;
      for (int i = 0; i < entCount; i++) {
        Ent &o = ents[i];
        if (!o.alive || o.team == e.team || o.isPet) continue;
        float dx = o.x - e.x, dy = o.y - e.y, dd = sqrtf(dx * dx + dy * dy);
        if (dd > 1 && dd < 240) {
          moveEnt(o, dx / dd * 120, dy / dd * 120);
          damageEnt(o, d.baseDmg * 0.6f * mul, e);
        }
      }
      break;
    }
    case SUP_WHIRL: { // Jacky : attire vers elle puis frappe en zone
      for (int i = 0; i < entCount; i++) {
        Ent &o = ents[i];
        if (!o.alive || o.team == e.team || o.isPet) continue;
        float dx = e.x - o.x, dy = e.y - o.y, dd = sqrtf(dx * dx + dy * dy);
        if (dd > 1 && dd < 130) {
          float pull = fmaxf(0, dd - 60);
          moveEnt(o, dx / dd * pull, dy / dd * pull);
          damageEnt(o, d.baseDmg * 1.6f * mul, e);
        }
      }
      shakeT = 250; shakeMag = 4;
      break;
    }
    case SUP_MINES: { // Bo : pose 3 mines INVISIBLES qui n'explosent que si un ennemi marche dessus
      {
        float cx = e.x + fcosf(ang) * 110, cy = e.y + fsinf(ang) * 110;
        static const float mOff[3][2] = {{-18, 0}, {18, 0}, {0, 16}};
        for (int m = 0; m < 3; m++) {
          for (int k = 0; k < MAX_PROJ; k++) {
            if (projs[k].active) continue;
            Proj &mp = projs[k];
            mp.active = true; mp.lob = false; mp.steer = 0; mp.fromSuper = true; mp.pierce = false;
            mp.x = cx + mOff[m][0]; mp.y = cy + mOff[m][1];
            mp.vx = mp.vy = 0; mp.dmg = 1300.0f * mul; mp.travelled = 0; mp.range = 0;
            mp.team = e.team; mp.owner = (&e - ents); mp.radius = 3;
            mp.nHits = 0; mp.shape = 1; mp.spin = 0; mp.mineT = 15.0f;
            mp.boomer = false; mp.ret = false; mp.bounce = 0;
            mp.bIdx = e.brawlerIdx; // V5.13
            break;
          }
        }
        spawnParts(cx, cy, 0, 4);
      }
      break;
    }
    case SUP_UPGRADE: { // Surge : palier suivant (+1 balle par tir, plus rapide)
      if (e.upStage < 2) e.upStage++;
      e.speedBuffT = 4.0f; e.ammo = ammoMax(e.brawlerIdx);
      spawnParts(e.x, e.y, 1, 8);
      break;
    }
    case SUP_MECH: { // Meg : transformation en mecha (PV et vitesse augmentes)
      e.maxHP *= 1.35f; e.hp = e.maxHP; e.ammo = ammoMax(e.brawlerIdx); e.speedBuffT = 4.0f; e.mechT = 8.0f;
      spawnParts(e.x, e.y, 1, 10);
      break;
    }
    case SUP_LEAP: { // Piper : bond vers sa direction de deplacement (plus d'arriere) + grenades
      float ja = e.faceAng * 0.017453f; // faceAng suit la direction du deplacement
      e.dashT = 0.35f; e.dashDx = (int)(fcosf(ja) * 340); e.dashDy = (int)(fsinf(ja) * 340);
      gForceLob = true;
      for (int i = 0; i < 4; i++) fireProj(e, ang + (rnd(30) - 15) * 0.01745f, true, mul * 0.7f, 0.8f, 0.45f);
      gForceLob = false;
      break;
    }
    case SUP_HASTE: { // Max : coup de vitesse pour toute l'equipe proche
      for (int i = 0; i < entCount; i++) {
        Ent &a = ents[i];
        if (!a.alive || a.team != e.team || a.isPet) continue;
        float dx = a.x - e.x, dy = a.y - e.y;
        if (dx * dx + dy * dy < 280 * 280) { a.speedBuffT = 5.0f; spawnParts(a.x, a.y, 1, 4); }
      }
      break;
    }
    case SUP_WALL: { // Sprout : fait pousser un mur de buissons a la visee
      float wx = e.x + fcosf(ang) * fminf(RNG(d.range), 200), wy = e.y + fsinf(ang) * fminf(RNG(d.range), 200);
      int tx = (int)(wx / TILE), ty = (int)(wy / TILE);
      for (int cx2 = tx - 1; cx2 <= tx + 1; cx2++)
        for (int cy2 = ty - 2; cy2 <= ty + 2; cy2++)
          if (cx2 >= 1 && cy2 >= 1 && cx2 < mapW - 1 && cy2 < mapH - 1 && tileMap[cx2][cy2] == 0)
            tileMap[cx2][cy2] = 1;
      spawnParts(wx, wy, 2, 10);
      break;
    }
  }
}




// ------------------------- Clavier ------------------------------------
// Commandes (V5.3, nouveau schema) :
//   Fleches : bouger | OK ou var : attaque vers la direction du perso
//   RETOUR (back) : ulti (super) | HOME : quitter
//   HOME : quitter le match. Le maintien de OK/var tire en continu.
static bool keyOK, keyBack, prevOK, prevShift, prevMinus, prevBack;
static bool kUp, kDown, kLeft, kRight, kShift, kBacksp, kHome, kMinus, kVar, kExp;
static bool kPlus, kMul, kDiv, kPow;
static bool prevVar, prevExp, prevBacksp;

static void pollKeys() {
  Ion::Keyboard::State st = Ion::Keyboard::getState();
  using K = Ion::Keyboard::Key;
  kUp = st.keyDown(K::Up); kDown = st.keyDown(K::Down);
  kLeft = st.keyDown(K::Left); kRight = st.keyDown(K::Right);
  keyOK = st.keyDown(K::OK);
  kShift = st.keyDown(K::Shift); // inutilise (gadgets supprimes)
  kBacksp = st.keyDown(K::Backspace) || st.keyDown(K::X); // inutilise
  kHome = st.keyDown(K::Home);
  kVar = st.keyDown(K::Var);
  kExp = st.keyDown(K::Toolbox) || st.keyDown(K::EXE); // alias d'attaque
  kMinus = st.keyDown(K::Minus); keyBack = st.keyDown(K::Back); // back = ulti en match
  kPlus = st.keyDown(K::Plus); kMul = st.keyDown(K::Multiply);
  kDiv = st.keyDown(K::Division); kPow = st.keyDown(K::Power);
}
// anti double-appui : attend que les touches d'action soient relachees
static void waitRelease() {
  while (true) {
    pollKeys();
    if (!keyOK && !keyBack && !kShift && !kBacksp && !kVar && !kExp && !kMinus) return;
    Ion::Timing::msleep(30);
  }
}

// ------------------------- IA ---------------------------------------
static float nearestEnemyDist(Ent &e, Ent **out) {
  float best = 1e9; *out = nullptr;
  for (int i = 0; i < entCount; i++) {
    Ent &o = ents[i];
    if (!o.alive || o.team == e.team) continue;
    if (inBush(o.x, o.y) && !e.isHuman) continue;
    if (o.stealthT > 0) continue; // invisible : impossible a cibler
    float dx = o.x - e.x, dy = o.y - e.y, d2 = dx * dx + dy * dy;
    if (d2 < best) { best = d2; *out = &o; }
  }
  return sqrtf(best);
}

static void shootBall(Ent &e, float ang); // declaree ici : updateAI l'utilise avant sa definition

static void updateAI(Ent &e, float dt) {
  if (e.stunT > 0) return; // V5.19 : tase par Buzz — completement fige, il ne bouge plus
  // V5.13b : AVEUGLE (super de Jacky) : le bot ne voit plus rien, erre au hasard et ne tire plus
  if (e.blindT > 0) {
    if (e.aiThinkT <= 0) {
      e.aiWx = (int)e.x + rnd(160) - 80; e.aiWy = (int)e.y + rnd(160) - 80; e.aiThinkT = 600;
    }
    e.aimAng = e.faceAng = (float)(rnd(360));
    return;
  }
  const BrawlerDef &d = BRAWLERS[e.brawlerIdx];
  float sk = e.aiSkill;
  bool melee = RNG(d.range) < 130;
  Ent *tgt = nullptr;
  float ed = nearestEnemyDist(e, &tgt);
  float mtr = matchTr(); // V5.17 : trophees du brawler joue : dose TOUS les reflexes ci-dessous
  // V5.17 : FOCUS DU PLUS FAIBLE (des 500 tr, non mecanique : 65% seulement, jamais hors de portee)
  if (mtr >= 500 && tgt) {
    float bd = RNG(d.range) * 1.05f;
    for (int j = 0; j < entCount; j++) {
      Ent &o = ents[j];
      if (!o.alive || o.isPet || o.team == e.team || &o == &e) continue;
      if (o.hp < o.maxHP * 0.7f) {
        float ddx = o.x - e.x, ddy = o.y - e.y;
        if (ddx * ddx + ddy * ddy < bd * bd && rnd(100) < 65) {
          tgt = &o;
          ed = sqrtf(ddx * ddx + ddy * ddy);
          break;
        }
      }
    }
  }

  // ---------- 1) OBJECTIF : on avance TOUJOURS, pas besoin d'ennemi a portee ----------
  e.aiThinkT -= (int)(dt * 1000);
  if (e.aiThinkT <= 0) {
    e.aiThinkT = (int)(450 - 250 * sk) + rnd(200);
    float tx = e.x, ty = e.y;
    bool onObj = false;
    if (mode == MODE_GEM) {
      // la gemme la plus PRECHE, pas la premiere de la liste
      float bestD = 1e9;
      for (int i = 0; i < 10; i++)
        if (gems[i].active) {
          float ddx = gems[i].x - e.x, ddy = gems[i].y - e.y, dd = ddx * ddx + ddy * ddy;
          if (dd < bestD) { bestD = dd; tx = gems[i].x; ty = gems[i].y; }
        }
      // compte a rebours : on protege son butin en reculant dans son camp
      if (gemCountdown > 0 && bestD > 100 * 100) { tx = (e.team == 0) ? 60 : mapW * TILE - 60; ty = mapH * TILE / 2; }
      else if (bestD == 1e9) { tx = mapW * TILE / 2; ty = mapH * TILE / 2; }
      onObj = (bestD != 1e9);
    }
    else if (mode == MODE_BALL) {
      // roles Brawl Ball : 0 = buteur, 1 = defenseur, 2 = tueur
      float goalX = (e.team == 0) ? mapW * TILE - 12 : 12;
      float ownX  = (e.team == 0) ? 20 : mapW * TILE - 20;
      if (e.hasBall) { tx = goalX; ty = mapH * TILE / 2; } // porteur : fonce marquer, meuble rien
      else if (e.aiRole == 1) { // defenseur : veille devant son but
        tx = ownX + ((e.team == 0) ? 45 : -45);
        ty = mapH * TILE / 2 + ((e.walkT / 600) % 2 ? 50 : -50);
      }
      else if (e.aiRole == 2) { /* tueur : cible l'ennemi via tgt plus bas */ }
      else if (ballVX == 0 && ballVY == 0) { tx = ballX; ty = ballY; } // ballon libre : on va le prendre
      else { tx = (goalX + ballX) / 2; ty = ballY; } // soutien : accompagne le porteur
      onObj = true;
    }
    else if (mode == MODE_HEIST) {
      // on pique le coffre adverse, en s'arretant a portee de tir (on n'embrasse pas le coffre)
      for (int s = 0; s < MAX_SAFE; s++)
        if (safes[s].maxHP > 0 && safes[s].hp > 0 && safes[s].team != e.team) {
          float ddx = safes[s].x - e.x, ddy = safes[s].y - e.y, dd = sqrtf(ddx * ddx + ddy * ddy);
          float back = fmaxf(0, dd - RNG(d.range) * 0.8f);
          tx = safes[s].x - (dd > 1 ? ddx / dd * back : 0);
          ty = safes[s].y - (dd > 1 ? ddy / dd * back : 0);
        }
      onObj = true;
    }
    else if (mode == MODE_ZONE) {
      float bestD = 1e9; bool found = false;
      for (int z = 0; z < MAX_ZONE; z++) {
        bool mine = (e.team == 0) ? zones[z].prog >= 100 : zones[z].prog <= -100;
        if (mine) continue;
        float ddx = zones[z].x - e.x, ddy = zones[z].y - e.y, dd = ddx * ddx + ddy * ddy;
        if (dd < bestD) { bestD = dd; found = true; tx = zones[z].x; ty = zones[z].y; }
      }
      if (!found) {
        float bestP = 1e9;
        for (int z = 0; z < MAX_ZONE; z++)
          if (zones[z].prog < bestP) { bestP = zones[z].prog; tx = zones[z].x; ty = zones[z].y; }
      }
      onObj = true;
    }
    else { // RECOMPENSE, KO, SURVIVANT : chasse active vers le centre ou l'ennemi connu
      if (tgt) { tx = tgt->x; ty = tgt->y; }
      else { tx = mapW * TILE / 2; ty = mapH * TILE / 2; }
      onObj = (tgt != nullptr);
    }
    // anti-embouteillage : si un coequipier vise deja ce point, on se decale
    for (int i = 0; i < entCount; i++) {
      Ent &a = ents[i];
      if (&a == &e || !a.alive || a.isPet || a.team != e.team) continue;
      float ddx = a.x - tx, ddy = a.y - ty;
      if (ddx * ddx + ddy * ddy < 60 * 60) { tx += (rnd(80) - 40); ty += (rnd(80) - 40); break; }
    }
    e.aiWx = tx; e.aiWy = ty;
    e.aiOnObj = onObj;
  }

  // ---------- 2) SURVIE : on decroche quand on est trop amoché ----------
  float lowTh = 0.38f - 0.15f * sk; // personnalites IA : l'agressif tente, le defensif decroche tot
  if (e.aiPers == 0) lowTh -= 0.1f;
  else if (e.aiPers == 1) lowTh += 0.12f;
  bool low = e.hp < e.maxHP * lowTh;
  if (e.brawlerIdx == 47 && e.mechT > 0) low = false; // Meg en mecha : blindee, elle ne fuit jamais
  float wx = e.aiWx, wy = e.aiWy;
  if (low && tgt && ed < 240) {
    // repli vers son coin de depart en tirant en reculant si possible
    wx = (e.team == 0) ? 30 : mapW * TILE - 30;
    wy = mapH * TILE / 2 + (rnd(120) - 60);
  }

  // fuir la zone de poison : retour vers le centre
  if (mode == MODE_SOLO || mode == MODE_DUO || mode == MODE_KO) {
    int cx = mapW * TILE / 2, cy = mapH * TILE / 2;
    int ddx = e.x - cx, ddy = e.y - cy;
    if (ddx * ddx + ddy * ddy > (soloShrink - 40) * (soloShrink - 40)) { wx = cx; wy = cy; }
  }
  // fuir les flaques ennemies (Barley, Amber...) : un joueur ne reste pas dans le feu
  for (int fz = 0; fz < 8; fz++)
    if (fireZones[fz].active && fireZones[fz].team != e.team) {
      float fdx = e.x - fireZones[fz].x, fdy = e.y - fireZones[fz].y;
      if (fdx * fdx + fdy * fdy < 60 * 60) { wx = e.x + fdx * 2.0f; wy = e.y + fdy * 2.0f; break; }
    }

  // ---------- 3) COMBAT : chaque classe joue comme un vrai joueur de son perso ----------
  float angT = e.aimAng;
  // distance de confort selon la classe : un Tick ne FONCE pas sur une Shelly
  bool lobber = (d.atk == ATK_LOB);                            // Barley, Dynamike, Tick... : bombardent de loin
  bool sniper = (!lobber && d.atk == ATK_SHOT && d.projCount <= 1 && RNG(d.range) >= 250); // Piper, Brock, Bea...
  float keepDist = RNG(d.range) * 0.55f;                           // par defaut : mi-distance
  if (lobber) keepDist = RNG(d.range) * 0.85f;                     // les lanceurs restent en arriere et arrosent
  if (sniper) keepDist = RNG(d.range) * 0.92f;                      // les snipers tirent de TRES loin
  if (e.brawlerIdx == 50) keepDist = RNG(d.range) * 0.97f;         // Piper : la reine du combat a distance
  if (e.brawlerIdx == 47 && e.mechT > 0) keepDist = RNG(d.range) * 0.35f; // Meg en mecha : colle pour ecraser
  if (e.brawlerIdx == 34) keepDist = RNG(d.range) * 0.25f;         // V5.17 : Fang — son shoe fait bien plus mal de pres
  if (e.brawlerIdx == 37) keepDist = 55.0f;                        // V5.17 : Hank — sa bulle eclate pres de lui
  if (e.aiPers == 0) keepDist *= 0.75f;      // personnalite agressive
  else if (e.aiPers == 1) keepDist *= 1.25f; // personnalite defensive
  if (e.aiTilt >= 2) keepDist *= 1.25f;      // V5.17 : TILT — morts en chaine → patient, garde distance
  if (melee) keepDist = RNG(d.range) * 0.35f;                      // les melees doivent coller pour exister
  if (d.baseHP >= 6000 && !lobber) keepDist *= 0.85f;         // les tanks osent s'approcher
  if (d.baseHP <= 4200) keepDist = fmaxf(keepDist, 175.0f);   // les fragiles gardent AU MOINS 175 px (Tick, Shelly...)
  if (tgt) {
    angT = atan2f(tgt->y - e.y, tgt->x - e.x);
    // PREDICTION : un vrai joueur anticipe le deplacement de sa cible (les bons le font mieux)
    float lead = sk * 0.8f * ed / fmaxf(d.projSpeed, 100.0f);
    float tvx = fcosf(tgt->faceAng * 0.017453f) * 88.0f, tvy = fsinf(tgt->faceAng * 0.017453f) * 88.0f;
    angT = atan2f(tgt->y + tvy * lead - e.y, tgt->x + tvx * lead - e.x);
    float err = (1.0f - sk) * 12.0f;
    bool zoneAtk = (e.brawlerIdx == 7 || e.brawlerIdx == 17 || e.brawlerIdx == 19 ||
                    e.brawlerIdx == 22 || e.brawlerIdx == 23); // V5.16 : attaque de ZONE (Poco, Jacky, Emz, Frank, Bibi)
    if (ed < RNG(d.range) * 0.95f && (!zoneAtk || ed > RNG(d.range) * 0.55f) &&
        e.ammo >= 1 && e.attackT <= 0 && !(mode == MODE_BALL && e.hasBall)) {
      // zoneAtk : on ne tire QUE quand l'ennemi est au bout de la zone (l'eventail couvre tout le passage)
      if (e.brawlerIdx == 37 || e.brawlerIdx == 32) e.atkCharge = 0.4f + rnd(70) / 100.0f; // l'IA (Hank/Ash) charge un peu au hasard
      if (d.atk == ATK_LOB) gLobRangeMul = fminf(1.0f, ed / RNG(d.range));
      doAttack(e, angT + (rnd(100) / 100.0f - 0.5f) * err * 0.01745f);
      gLobRangeMul = -1.0f;
    }
    // placement par classe : trop pres pour NOTRE style -> on decroche en tirant
    if (!melee && !low && ed < keepDist * 0.8f && ed < RNG(d.range)) {
      float l = fmaxf(ed, 1);
      wx = e.x - (tgt->x - e.x) / l * 90; wy = e.y - (tgt->y - e.y) / l * 90;
    } else if (tgt && ed < RNG(d.range) * 0.9f && !low) {
      // a bonne distance : on canarde en strafant, comme un joueur qui juke
      float side = ((e.walkT / 800) % 2) ? 1.5708f : -1.5708f;
      wx = e.x + fcosf(angT + side) * 80; wy = e.y + fsinf(angT + side) * 80;
    }
    // sinon (cible trop loin) : on file a l'objectif, la rencontre viendra a nous
  }
  // IA porteuse du ballon : fonce au but, TIRE vers le but des qu'elle est a portee, et shoote les defenseurs
  if (mode == MODE_BALL && e.hasBall) {
    wx = (e.team == 0) ? mapW * TILE - 20 : 20; wy = mapH * TILE / 2;
    float goalX = (e.team == 0) ? mapW * TILE - 10 : 10;
    float gd = fabsf(e.x - goalX) + fabsf(e.y - mapH * TILE / 2) * 0.5f;
    if (gd < 300 && e.attackT <= 0 && rnd(100) < 60) {
      // assez proche du but : on degage le ballon vers les poteaux (comme un vrai joueur)
      shootBall(e, atan2f(mapH * TILE / 2 - e.y, goalX - e.x));
    } else if (tgt && ed < RNG(d.range) * 0.8f && e.ammo >= 1 && e.attackT <= 0) {
      doAttack(e, atan2f(tgt->y - e.y, tgt->x - e.x));
    }
  }

  // ---------- 4) SUPER : au bon moment et au bon endroit ----------
  if (e.superCharge >= 100 && !e.isBoss) { // le boss du Siege n'a pas de super : il EST le super
    bool done = false;
    switch (d.sup) {
      // supers de zone : on les pose sur l'objectif, ennemi ou pas
      case SUP_SUMMON: case SUP_WALL: case SUP_MINES:
        if (e.aiOnObj && (rnd(100) < 40 * sk)) { doSuper(e, angT); done = true; }
        break;
      // supers de fuite/survie : quand ca chauffe (Piper : le bond sert surtout a s'echapper)
      // V5.17 : FEINTE (des 1000 tr) — le bot garde son super de fuite au chaud et te le lache AU NEZ
      case SUP_STEALTH: case SUP_MECH: case SUP_LEAP:
        if (low && (mtr < 1000 || ed < 130)) { doSuper(e, angT); done = true; } break;
      case SUP_SHIELD:
        if (tgt && ed < RNG(d.range) * 1.2f) { doSuper(e, angT); done = true; } break;
      case SUP_UPGRADE: case SUP_HEAL:
        // V5.16 : Poco ne gaspille plus son soin — seulement si lui ou un allie est blessé
        {
          bool allyLow = (e.hp < e.maxHP * 0.65f);
          for (int j = 0; j < entCount && !allyLow; j++)
            if (ents[j].alive && !ents[j].isPet && ents[j].team == e.team && ents[j].hp < ents[j].maxHP * 0.65f) allyLow = true;
          if (allyLow && rnd(100) < 40) { doSuper(e, angT); done = true; }
        } break;
      case SUP_HASTE: // Max : boost de vitesse pour chasser ou s'echapper
        if (low || (tgt && ed < RNG(d.range) * 1.3f)) { doSuper(e, angT); done = true; } break;
      default: // supers d'attaque : seulement a portee d'une cible, et pas claques a la va-vite
        if (tgt && ed < RNG(d.range) && rnd(100) < 55 + 35 * sk) {
          if (d.atk == ATK_LOB) gLobRangeMul = fminf(1.0f, ed / RNG(d.range));
          doSuper(e, angT); gLobRangeMul = -1.0f; done = true;
        }
        break;
    }
    (void)done;
  }

  // V5.16 : IA plus REALISTE — a vide (rechargement), on decroche en rechargant
  if (e.ammo < 1 && tgt && ed < keepDist * 1.2f && !e.isBoss) {
    float bkx = e.x - tgt->x, bky = e.y - tgt->y, bl = sqrtf(bkx * bkx + bky * bky);
    if (bl > 0.01f) { wx = e.x + bkx / bl * 70; wy = e.y + bky / bl * 70; }
  }
  // V5.16 : IA plus REALISTE — on ne s'empile pas sur un allie : on s'ecarte un peu
  for (int j = 0; j < entCount; j++) {
    Ent &o = ents[j];
    if (&o == &e || !o.alive || o.isPet || o.team != e.team) continue;
    float adx = e.x - o.x, ady = e.y - o.y;
    if (adx * adx + ady * ady < 40 * 40) { wx += (adx > 0 ? 30 : -30); wy += (ady > 0 ? 20 : -20); break; }
  }

  // ---------- V5.17 : reflexes de "vrai joueur", TOUS DOSES par les trophees du match ----------
  {
    float gate = fminf(1.0f, mtr / 1600.0f); // 0 a 0 tr, 1 a 1600 tr
    // VENGEANCE (des 700 tr, dosee) : gros coup recu → on traque le coupable 8 s, jamais au suicide
    if (e.aiRvIdx >= 0 && e.aiRvIdx < entCount && e.aiRvT > 0 && ents[e.aiRvIdx].alive &&
        e.hp > e.maxHP * 0.45f && mtr >= 700) {
      wx = ents[e.aiRvIdx].x; wy = ents[e.aiRvIdx].y;
    }
    // KITE general (dose) : les tireurs decrochent pendant leur temps de rechargement
    if (!melee && tgt && ed < keepDist * 0.7f && e.attackT > 0 && rnd(100) < 35 + 45 * gate) {
      float bkx = e.x - tgt->x, bky = e.y - tgt->y, bl = sqrtf(bkx * bkx + bky * bky);
      if (bl > 0.01f) { wx = e.x + bkx / bl * 70; wy = e.y + bky / bl * 70; }
    }
    // COUVERTURE (des 1200 tr, progressive) : un tireur ne traverse pas a decouvert sous le feu
    if (!melee && tgt && ed < 260 && rnd(100) < (mtr - 1200) / 8.0f) { wx = e.x; wy = e.y; }
    // HEIST (des 600 tr) : le pillard recule quand un defenseur approche
    if (mode == MODE_HEIST && e.aiOnObj && tgt && ed < 150 && e.ammo < 2 && mtr >= 600 && rnd(100) < 50) {
      float bkx = e.x - tgt->x, bky = e.y - tgt->y, bl = sqrtf(bkx * bkx + bky * bky);
      if (bl > 0.01f) { wx = e.x + bkx / bl * 80; wy = e.y + bky / bl * 80; }
    }
    // GEMMES (des 700 tr) : ESCORTE du porte-cles — on se colle au plus charge
    if (mode == MODE_GEM && mtr >= 700 && e.gems < 2 && rnd(100) < 30) {
      Ent *carrier = nullptr; int bestG = 2;
      for (int j = 0; j < entCount; j++) {
        Ent &o = ents[j];
        if (!o.alive || o.isPet || o.team != e.team) continue;
        if (o.gems > bestG) { bestG = o.gems; carrier = &o; }
      }
      if (carrier && carrier != &e) { wx = carrier->x + 40; wy = carrier->y + 40; }
    }
    // EMBUSCADE en buisson (des 800 tr, progressive) : sans cible, on file par le buisson le plus proche
    if (!tgt && mtr >= 800 && rnd(100) < (mtr - 800) / 12.0f) {
      int mx = (int)(e.x / TILE), my = (int)(e.y / TILE);
      bool found = false;
      for (int r2 = 1; r2 <= 5 && !found; r2++)
        for (int dx2 = -r2; dx2 <= r2 && !found; dx2++)
          for (int dy2 = -r2; dy2 <= r2 && !found; dy2++) {
            int tx2 = mx + dx2, ty2 = my + dy2;
            if (tx2 < 1 || ty2 < 1 || tx2 >= mapW - 1 || ty2 >= mapH - 1) continue;
            if (tileMap[tx2][ty2] == 2) { wx = tx2 * TILE + TILE / 2; wy = ty2 * TILE + TILE / 2; found = true; }
          }
    }
  }

  // V5.16 : les attaquants PAR BALLES SEPAREES (Shelly, Bull, Primo, Rosa, Darryl, Pam,
  // Edgar, Fang, Mortis...) foncent au corps a corps : de pres, TOUTES leurs balles touchent
  if (tgt && ed < 150 && !e.isBoss &&
      (e.brawlerIdx == 0 || e.brawlerIdx == 3 || e.brawlerIdx == 5 || e.brawlerIdx == 8 ||
       e.brawlerIdx == 14 || e.brawlerIdx == 21 || e.brawlerIdx == 26 || e.brawlerIdx == 34 ||
       e.brawlerIdx == 37 || e.brawlerIdx == 45)) {
    wx = tgt->x; wy = tgt->y; // on colle l'adversaire
  }
  // V5.16 : les attaquants de ZONE (Poco, Jacky, Emz, Frank, Bibi) tapent au bout de leur
  // zone : si l'ennemi colle, ils RE reculent pour le reprendre en pleine onde
  if (tgt && ed < 70 && !e.isBoss &&
      (e.brawlerIdx == 7 || e.brawlerIdx == 17 || e.brawlerIdx == 19 || e.brawlerIdx == 22 ||
       e.brawlerIdx == 23)) {
    float bkx = e.x - tgt->x, bky = e.y - tgt->y, bl = sqrtf(bkx * bkx + bky * bky);
    if (bl > 0.01f) { wx = e.x + bkx / bl * 70; wy = e.y + bky / bl * 70; } // on reprend ses distances
  }

  // ---------- 5) DEPLACEMENT ----------
  float dx = wx - e.x, dy = wy - e.y;
  float len = sqrtf(dx * dx + dy * dy);
  if (len > 10) {
    float sp = 72 + 24 * sk;
    if (e.isBoss) sp *= 0.6f; // V5.16 : le mega robot est lent et massif
    if (e.speedBuffT > 0) sp *= 1.3f; // boost de vitesse (super de Carl, Max, Stu...)
    if (e.boostT > 0) sp *= 1.7f; // super de Carl
    if (e.slowT > 0) sp *= 0.6f;
    float ox = e.x, oy = e.y;
    moveEnt(e, dx / len * sp * dt, dy / len * sp * dt);
    e.walkT += (int)(dt * 1000);
    // anti-blocage : si on n'a presque pas bouge, on contourne par un cote
    float moved = fabsf(e.x - ox) + fabsf(e.y - oy);
    if (moved < sp * dt * 0.3f) {
      e.stuckT += dt;
      if (e.stuckT > 0.35f) {
        e.stuckT = 0;
        float nx = -dy / len, ny = dx / len;
        int side = rnd(2) ? 1 : -1;
        e.aiWx = (int)(e.x + nx * side * 80);
        e.aiWy = (int)(e.y + ny * side * 80);
        e.aiThinkT = 600;
      }
    } else e.stuckT = 0;
  } else e.stuckT = 0;
}

// ------------------------- Match --------------------------------------
// Tir du ballon porte : le "tir" du porteur envoie le ballon (Brawl Ball)
static void shootBall(Ent &e, float ang) {
  e.hasBall = false;
  ballX = e.x + fcosf(ang) * 12; ballY = e.y + fsinf(ang) * 12;
  ballVX = fcosf(ang) * 300; ballVY = fsinf(ang) * 300;
  ballShooter = (&e - ents); ballShotT = 800; // V5.17 FIX : sans ca, le tireur rattrapait le ballon dans la seconde
  e.attackT = 200; shakeT = 120; shakeMag = 2;
}
// But marque par le ballon libre : credite l'equipe attaquante et recentre
static void teamGoal(int team) {
  int rec = -1;
  for (int i = 0; i < entCount; i++)
    if (ents[i].team == team && !ents[i].isPet && (rec < 0 || i == humanIdx)) rec = i;
  if (rec >= 0) ents[rec].goals++;
  ballResetAfterGoal(); // V5.15 : remise au centre + respawn des joueurs (plus de but en double)
}
static void damageEnt(Ent &e, float dmg, Ent &src) {
  if (e.shieldT > 0) dmg *= 0.35f; // carapace de Rosa active
  // tourelle booster de 8-Bit : +30% degats pour les allies proches d'elle
  for (int i = 0; i < entCount; i++) {
    Ent &b = ents[i];
    if (!b.alive || !b.isPet || b.petKind != 7 || b.team != src.team) continue;
    float bx = b.x - src.x, by = b.y - src.y;
    if (bx * bx + by * by < 220 * 220) { dmg *= 1.3f; break; }
  }
  int popKind = 0;
  if (rnd(100) < 12) { dmg *= 1.4f; popKind = 1; } // V5.16 : CRITIQUE x1.4 (12%) en jaune gras
  e.hp -= dmg; e.regenT = 0; e.lastHitT = 150;
  // V5.13b : effet de statut — les balles de PIPER ralentissent la cible 1.5 s
  if (&src != &e && !src.isPet && src.brawlerIdx == 50) e.slowT = fmaxf(e.slowT, 1.5f);
  src.dmgDone += dmg; // stats de fin de match
  float chg = (src.hyperT > 0) ? 20.0f : 14.0f;
  if (!src.isPet && src.brawlerIdx == 10) chg = 7.0f; // Dynamike : la giga-dynamite est TRES cher (15 touches)
  if (!src.isPet && src.brawlerIdx == 20) chg = 100.0f; // Stu : UNE touche = super rechargee
  src.superCharge = fminf(100, src.superCharge + chg); // ~7 tirs reussis = ulti prete
  addPop(e.x + rnd(8) - 4, e.y - 12, (int)dmg, popKind);
  spawnParts(e.x, e.y, 0, 5); // etincelles d'impact plus visibles
  // V5.17 : VENGEANCE (des 700 tr) — un bot gros-blesse retient qui l'a frappe
  if (&e != &src && !e.isHuman && !e.isPet && e.team != src.team && dmg > e.maxHP * 0.35f && matchTr() >= 700) {
    e.aiRvIdx = (&src - ents); e.aiRvT = 8000;
  }
  // V5.17 : CHAT des bots (sans spam : 1 bulle max toute les 4,5 s, et que 15% des gros coups)
  if (!e.isHuman && !e.isPet && gChatT <= 0 && dmg > e.maxHP * 0.28f && rnd(100) < 15) {
    gChatEnt = (&e - ents); gChatT = 4500; gChatIdx = rnd(2);
  }
  // V5.16 : screen shake DIRECTIONNEL — la camera recule dans le sens du coup recu
  if (&e == &ents[humanIdx] && &src != &e) {
    float ddx = e.x - src.x, ddy = e.y - src.y, l = sqrtf(ddx * ddx + ddy * ddy);
    if (l > 0.01f) { shakeDX = ddx / l; shakeDY = ddy / l; }
  }
}

// IA des creatures invoquees : ours/rats/pingouins melee, tourelle/ego tireurs, station de soin
static void updatePet(Ent &e, float dt) {
  e.petLife -= dt;
  if (e.petLife <= 0) { e.hp = 0; return; } // expiration : part en particules via le bloc de mort
  if (e.petKind == 0 || e.petKind == 3 || e.petKind == 5) { // chasseurs au corps a corps (ours, rats, pingouin)
    float spd = (e.petKind == 3) ? 200.0f : ((e.petKind == 5) ? 170.0f : 150.0f);
    float dmg = (e.petKind == 3) ? 400.0f : ((e.petKind == 5) ? 700.0f : 950.0f);
    Ent *t = nullptr; nearestEnemyDist(e, &t);
    if (t) {
      float dx = t->x - e.x, dy = t->y - e.y, l = sqrtf(dx * dx + dy * dy);
      if (l > 26) { moveEnt(e, dx / l * spd * dt, dy / l * spd * dt); e.walkT += (int)(dt * 1000); }
      else if (e.attackT <= 0) {
        e.aimAng = e.faceAng = atan2f(dy, dx);
        damageEnt(*t, dmg, ents[e.petOwner]);
        e.attackT = 500;
      }
    }
  } else if (e.petKind == 4) { // ego de Lola : colle a sa maitresse et tire quand elle tire (doAttack)
    Ent &ow = ents[e.petOwner];
    if (ow.alive) {
      float dx = ow.x - e.x, dy = ow.y - e.y, l = sqrtf(dx * dx + dy * dy);
      if (l > 36) { moveEnt(e, dx / l * 200 * dt, dy / l * 200 * dt); e.walkT += (int)(dt * 1000); }
      e.aimAng = e.faceAng = ow.aimAng; // meme visee que Lola
    }
  } else if (e.petKind == 1) { // tourelle (Jessie, Penny) : immobile, tire toute seule
    Ent *t = nullptr; float ed = nearestEnemyDist(e, &t);
    if (t && ed < 260 && e.attackT <= 0 && e.ammo >= 1) {
      float ang = atan2f(t->y - e.y, t->x - e.x);
      e.aimAng = e.faceAng = ang;
      fireProj(e, ang, false, 1.0f, 1.0f, 0.7f);
      e.attackT = 600;
    }
  } else if (e.petKind == 6) { // tete-mine de Tick : court vers l'ennemi et explose dessus
    Ent *t = nullptr; float ed = nearestEnemyDist(e, &t);
    if (t) {
      float dx = t->x - e.x, dy = t->y - e.y;
      if (ed > 30) { moveEnt(e, dx / ed * 130 * dt, dy / ed * 130 * dt); e.walkT += (int)(dt * 1000); }
      if (ed < 34 && e.attackT <= 0) { // boum : gros degats en zone autour de la tete
        for (int i = 0; i < entCount; i++) {
          Ent &o = ents[i];
          if (!o.alive || o.team == e.team || o.isPet) continue;
          float ox2 = o.x - e.x, oy2 = o.y - e.y;
          if (ox2 * ox2 + oy2 * oy2 < 55 * 55) damageEnt(o, 2400, ents[e.petOwner]);
        }
        shakeT = 250; shakeMag = 4;
        spawnParts(e.x, e.y, 2, 12);
        e.hp = 0; e.petLife = 0; e.attackT = 600; // la tete est detruite par son explosion
      }
    }
  } else if (e.petKind == 7) { // tourelle booster de 8-Bit : immobile, ne tire pas
    // l'effet (+30% degats pour les allies proches) est applique dans damageEnt
  } else { // station de soin de Pam : regenere les allies proches
    for (int i = 0; i < entCount; i++) {
      Ent &a = ents[i];
      if (!a.alive || a.team != e.team || a.isPet) continue;
      float dx = a.x - e.x, dy = a.y - e.y;
      if (dx * dx + dy * dy < 90 * 90) {
        float before = a.hp;
        a.hp = fminf(a.maxHP, a.hp + a.maxHP * 0.10f * dt);
        if ((int)(a.hp / 200) != (int)(before / 200) && a.hp > before) addPop(a.x, a.y - 12, (int)(a.hp - before));
      }
    }
  }
}

static void updateMatch(float dt) {
  if (startCountdown > -800) {
    // phase 3-2-1-GO : le temps tourne uniquement pour l'anim
    startCountdown -= (int)(dt * 1000);
    animT += (int)(dt * 1000);
    return;
  }
  matchTimeMs += (int)(dt * 1000);
  animT += (int)(dt * 1000);
  if (shakeT > 0) shakeT -= (int)(dt * 1000);
  bool ffa = (mode == MODE_SOLO || mode == MODE_DUO);
  bool noRespawn = ffa || mode == MODE_KO || mode == MODE_SIEGE || mode == MODE_DUEL || mode == MODE_SURVIE; // V5.16 : BOSS RUSH = respawn 20 s
  if (mode == MODE_BOSSRUSH) respawnMs = 20000; else respawnMs = 3000; // V5.16 : long delai en boss rush

  for (int i = 0; i < MAX_POP; i++)
    if (pops[i].active) { pops[i].life -= (int)(dt * 1000); pops[i].y -= dt * 22; if (pops[i].life <= 0) pops[i].active = false; }
  for (int i = 0; i < MAX_PART; i++)
    if (parts[i].active) {
      parts[i].life -= (int)(dt * 1000);
      parts[i].x += parts[i].vx * dt; parts[i].y += parts[i].vy * dt;
      parts[i].vy += 60 * dt;
      if (parts[i].life <= 0) parts[i].active = false;
    }
  if (gKOBannerT > 0) gKOBannerT -= (int)(dt * 1000); // V5.16 : bannieres eliminatoria / boss rush
  for (int i = 0; i < KILLFEED; i++)
    if (killfeed[i].alive) { killfeed[i].life -= (int)(dt * 1000); if (killfeed[i].life <= 0) killfeed[i].alive = 0; }

  Ent &me = ents[humanIdx];
  pollKeys();
  if (kHome) { matchResult = 2; return; } // HOME quitte : pas d.ecran de resultat (matchResult=2 = abandon)
  // emotes (touches + et -) : bulle au-dessus du joueur pendant 1.6 s
  static bool sEmP = false, sEmM = false;
  if (me.alive) {
    if (kPlus && !sEmP && acc.emoteP > 0) { gEmoteT = 1600; gEmoteKind = acc.emoteP; }
    if (kMinus && !sEmM && acc.emoteM > 0) { gEmoteT = 1600; gEmoteKind = acc.emoteM; }
  }
  sEmP = kPlus; sEmM = kMinus;
  if (gEmoteT > 0) gEmoteT -= (int)(dt * 1000);
  if (me.alive) {
    float spd = 88.0f;
    if (me.speedBuffT > 0) spd *= 1.3f;
    if (me.boostT > 0) spd *= 1.7f; // super de Carl : vitesse spectaculaire
    if (me.hyperT > 0) spd *= 1.15f;
    if (me.slowT > 0) spd *= 0.6f;
    const BrawlerDef &d = BRAWLERS[me.brawlerIdx];
    if (me.accIdx >= 0 && acc.b[me.accIdx].starPower && d.star == STAR_SPEED) spd *= 1.1f;
    float dx = (kRight ? 1 : 0) - (kLeft ? 1 : 0);
    float dy = (kDown ? 1 : 0) - (kUp ? 1 : 0);
    if (me.stunT <= 0 && (dx || dy)) { float l = sqrtf(dx * dx + dy * dy); moveEnt(me, dx / l * spd * dt, dy / l * spd * dt); } // V5.19 : tase = immobile
    // --- Commandes V5.3 : OK/var = attaque vers la direction du perso,
    //     RETOUR = ulti, HOME = quitter (sans ecran de resultat) ---
    bool shotUlti = keyBack && !prevBack;
    bool shotAtk = (keyOK && !prevOK) || (kVar && !prevVar) || (kExp && !prevExp);
    // maintien de OK/var/menu = tir continu, meme en marchant (V5.13b : PAS quand on porte le ballon :
    // avec la balle, OK = degager le ballon, un appui = un tir, comme dans BS)
    if (me.attackT <= 0 && me.ammo >= 1 && (keyOK || kVar || kExp) && !(mode == MODE_BALL && me.hasBall)) shotAtk = true;
    // Hank : maintenir OK gonfle son ballon, relacher (ou charge max) pour tirer
    if (me.brawlerIdx == 37 || me.brawlerIdx == 32) { // Hank / Ash : maintenir pour charger
      if ((keyOK || kVar || kExp) && me.ammo >= 1) {
        shotAtk = false;
        if (me.atkCharge < dt) gHankHoldT = 0; // nouvelle charge : on repart de zero
        me.atkCharge += dt;
        if (me.atkCharge >= 1.2f) {
          me.atkCharge = 1.2f;
          // V5.17 : a fond, la bulle RESTE gonflee 3 s si on maintient — ensuite elle eclate
          gHankHoldT += dt;
          if (gHankHoldT >= 3.0f && me.attackT <= 0) shotAtk = true;
        }
      } else if (me.atkCharge > 0) {
        gHankHoldT = 0;
        if (me.attackT <= 0 && me.ammo >= 1) shotAtk = true; // relachement : on lance
        else me.atkCharge = 0;
      }
    }
    if (me.stunT > 0) { // V5.19 : tase par Buzz — ni bouger, ni tirer, ni ulti
      dx = 0; dy = 0; shotAtk = false; shotUlti = false;
    }
    // V5.15 : le tir automatique VISE L'ENNEMI LE PLUS PROCHE meme quand on se deplace —
    // les fleches servent a bouger, pas a viser (comme l'auto-visse de BS)
    Ent *tgt = nullptr; nearestEnemyDist(me, &tgt);
    float ang = (tgt) ? atan2f(tgt->y - me.y, tgt->x - me.x)
              : ((dx || dy) ? atan2f(dy, dx) : me.aimAng);
    if (shotUlti) { if (me.superCharge >= 100) doSuper(me, ang); }
    if (shotAtk) {
      if (mode == MODE_BALL && me.hasBall) {
        shootBall(me, ang); // on tire le ballon au lieu d'attaquer
      } else {
        // lanceur : le tir atterrit sur l'ennemi le plus proche si on est a l'arret
        if (d.atk == ATK_LOB && tgt) gLobRangeMul = fminf(1.0f, sqrtf((tgt->x-me.x)*(tgt->x-me.x)+(tgt->y-me.y)*(tgt->y-me.y)) / RNG(d.range));
        doAttack(me, ang);
        gLobRangeMul = -1.0f;
      }
    }
  }
  prevOK = keyOK; prevShift = kShift; prevMinus = kMinus;
  prevVar = kVar; prevExp = kExp; prevBacksp = kBacksp;

  for (int i = 0; i < entCount; i++) {
    Ent &e = ents[i];
    if (!e.alive) {
      e.respawnT -= (int)(dt * 1000);
      if (e.respawnT <= 0 && !noRespawn) {
        e.alive = true; e.hp = e.maxHP; e.ammo = ammoMax(e.brawlerIdx);
        e.x = (e.team == 0) ? 40 : mapW * TILE - 40;
        e.y = mapH * TILE / 2 + (rnd(3) - 1) * 60;
      }
      continue;
    }
    if (!e.isHuman && !e.isPet) updateAI(e, dt);
    else if (e.isPet) updatePet(e, dt);
    if (e.speedBuffT > 0) e.speedBuffT -= dt;
    if (e.stealthT > 0) e.stealthT -= dt;
    if (e.shieldT > 0) e.shieldT -= dt;
    if (e.mechT > 0) e.mechT -= dt; // Meg : fin du mega robot
    if (e.isBoss) e.mechT = 1.0f; // le boss du Siege reste en mega robot
    e.tpCd -= dt; // recharge du teleporteur
    if (e.slowT > 0) e.slowT -= dt;
    if (e.poisonT > 0) { // V5.25 : poison de Byron — il ronge... et peut ACHEVER
      e.poisonT -= dt;
      e.hp -= 320.0f * dt; e.regenT = 0; e.lastHitT = 150;
      if (e.hp <= 0 && e.alive && e.poisonBy >= 0 && e.poisonBy < entCount)
        damageEnt(e, 1.0f - e.hp, ents[e.poisonBy]); // mort geree proprement (credit au poisonneur)
    }
    if (e.stunT > 0) e.stunT -= dt; // V5.19 : taser de Buzz
    if (e.blindT > 0) e.blindT -= dt; // V5.13b : aveuglement (super de Jacky)
    if (e.hyperT > 0) e.hyperT -= dt;
    if (e.lastHitT > 0) e.lastHitT -= (int)(dt * 1000);
    if (e.attackT > 0) e.attackT -= (int)(dt * 1000);
    // rafale en cours (Colt, Rico...) : la balle suivante part apres un court delai
    if (e.burstLeft > 0) {
      e.burstTickT -= (int)(dt * 1000);
      if (e.burstTickT <= 0) {
        fireProj(e, e.burstAng, false, e.burstMul, 1.0f, e.burstMul > 1.9f ? 1.55f : 1.0f); // rafale chargee de Hank : portee etendue
        e.burstLeft--; e.burstTickT = 70;
      }
    }
    if (e.dashT > 0) {
      e.dashT -= dt;
      if (e.dashT <= 0 && e.dashRet) { // Colette : demi-tour, elle revient d'ou elle vient
        e.dashRet = false; e.dashT = 0.4f; e.dashDx = -e.dashDx; e.dashDy = -e.dashDy;
      }
      // seuls les dashs "classiques" (Bull, El Primo...) defoncent les murs —
      // V5.17 : Darryl (comme Stu/Colette/Piper) REBONDIT dessus sans les casser
      if (e.brawlerIdx != 14 && e.brawlerIdx != 20 && e.brawlerIdx != 29 && e.brawlerIdx != 31 && e.brawlerIdx != 50)
        breakWalls(e.x, e.y, 14); // la charge defonce les murs sur son passage
      float dashOX = e.x, dashOY = e.y;
      moveEnt(e, e.dashDx * dt, e.dashDy * dt);
      // V5.17 : Darryl — bloque par un mur ? il REBONDIT dans l'autre sens
      if (e.brawlerIdx == 14 && fabsf(e.x - dashOX) + fabsf(e.y - dashOY) < 0.5f) {
        e.dashDx = -e.dashDx; e.dashDy = -e.dashDy;
      }
      for (int j = 0; j < entCount; j++)
        if (ents[j].alive && ents[j].team != e.team) {
          float ddx = ents[j].x - e.x, ddy = ents[j].y - e.y;
          if (ddx * ddx + ddy * ddy < 20 * 20) {
            if (e.brawlerIdx == 31) ents[j].stunT = fmaxf(ents[j].stunT, 1.6f); // V5.19 : Buzz = TASER : 0 degat mais ETOURDI
            else damageEnt(ents[j], 2200, e);
          }
        }
    }
    // Carl booste : il blesse les ennemis qu'il traverse pendant 5 s
    if (e.boostT > 0) {
      e.boostT -= dt; e.boostCd -= dt;
      if (e.boostCd <= 0)
        for (int j = 0; j < entCount; j++) {
          Ent &o = ents[j];
          if (!o.alive || o.team == e.team) continue;
          float dx = o.x - e.x, dy = o.y - e.y;
          if (dx * dx + dy * dy < 26 * 26) { damageEnt(o, 900.0f, e); e.boostCd = 0.35f; break; }
        }
    }
    const BrawlerDef &d = BRAWLERS[e.brawlerIdx];
    e.ammo = fminf(ammoMax(e.brawlerIdx), e.ammo + dt * 1000.0f / d.reloadMs); // V5.16 : Carl/Bea = 1 munition
    e.regenT += dt;
    if (e.aiRvT > 0) e.aiRvT -= dt * 1000; // V5.17 : timer de vengeance
    if (gChatT > 0) gChatT -= (int)(dt * 1000); // V5.17 : bulle de chat des bots
    float regenDelay = 4.0f;
    if (e.accIdx >= 0 && acc.b[e.accIdx].starPower && d.star == STAR_REGEN) regenDelay = 2.0f;
    if (e.regenT > regenDelay) e.hp = fminf(e.maxHP, e.hp + e.maxHP * 0.12f * dt);
    if (e.hyperT > 0) e.superCharge = fminf(100, e.superCharge + 6 * dt);
    // power cubes au sol (Survivant + SURVIE V5.16) : +10% PV max et soigne au ramassage
    if (mode == MODE_SOLO || mode == MODE_DUO || mode == MODE_SURVIE)
      for (int c = 0; c < MAX_CUBE; c++) {
        if (!cubes[c].active) continue;
        float dx = cubes[c].x - e.x, dy = cubes[c].y - e.y;
        if (dx * dx + dy * dy < 14 * 14) {
          cubes[c].active = false;
          e.cubes++;
          e.maxHP *= 1.1f; e.hp = fminf(e.maxHP, e.hp + e.maxHP * 0.1f);
          spawnParts(e.x, e.y, 2, 4); addPop(e.x, e.y - 16, 10);
        }
      }
    // teleporteurs : un pas dessus et on se retrouve a l'autre bout (cooldown anti-boucle)
    if (e.tpCd <= 0)
      for (int t = 0; t < MAX_TP; t++) {
        Portal &tp = portals[t];
        if (!tp.active || !portals[tp.link].active) continue;
        float dx = tp.x - e.x, dy = tp.y - e.y;
        if (dx * dx + dy * dy < 12 * 12) {
          spawnParts(e.x, e.y, 2, 6);
          e.x = portals[tp.link].x; e.y = portals[tp.link].y;
          spawnParts(e.x, e.y, 2, 6);
          e.tpCd = 1.5f;
          break;
        }
      }
    if (e.hp <= 0) {
      e.alive = false;
      if (e.isPet) { // une creature morte ne respawn pas et ne rapporte rien
        e.respawnT = 99999999;
        spawnParts(e.x, e.y, 1, 8);
        continue;
      }
      e.respawnT = respawnMs;
      if (!e.isHuman) e.aiTilt++; // V5.17 : TILT — mort en chaine → le bot devient patient
      if (e.gems > 0)
        for (int g = 0; g < 10 && e.gems > 0; g++)
          if (!gems[g].active) { gems[g].active = true; gems[g].x = e.x + rnd(20) - 10; gems[g].y = e.y + rnd(20) - 10; e.gems--; }
      if (e.hasBall) { e.hasBall = false; ballX = e.x; ballY = e.y; }
      if (e.cubes > 0) // le butin du mort tombe au sol
        for (int c = 0; c < MAX_CUBE && e.cubes > 0; c++) if (!cubes[c].active) {
          cubes[c].active = true;
          cubes[c].x = e.x + (rnd(40) - 20); cubes[c].y = e.y + (rnd(40) - 20);
          e.cubes--;
        }
      if (mode == MODE_SURVIE && e.team != 0) // V5.16 : les bots de SURVIE droppent un power cube (+10% PV)
        for (int c = 0; c < MAX_CUBE; c++) if (!cubes[c].active) {
          cubes[c].active = true; cubes[c].x = e.x + (rnd(20) - 10); cubes[c].y = e.y + (rnd(20) - 10);
          break;
        }
      // DUEL : le brawler suivant de l'equipe entre immediatement en jeu
      if (mode == MODE_DUEL && !e.isPet && duelNext[e.team] < 3) {
        int nb = duelQ[e.team][duelNext[e.team]], na = duelAcc[e.team][duelNext[e.team]];
        duelNext[e.team]++;
        e.respawnT = 99999999;
        addEnt(nb, na, e.team, (e.team == 0) ? 40 : mapW * TILE - 40, mapH * TILE / 2, e.team == 0, aiSkillFromTrophies());
        if (e.team == 0) humanIdx = entCount - 1;
        spawnParts((e.team == 0) ? 40 : mapW * TILE - 40, mapH * TILE / 2, 2, 8);
      }
      shakeT = 200; shakeMag = 3;
      spawnParts(e.x, e.y, 1, 14); spawnParts(e.x, e.y, 0, 6); // V5.16 : mini explosion de STYLE (0 degat)
      for (int j = 0; j < entCount; j++)
        if (ents[j].alive && ents[j].team != e.team && fabsf(ents[j].x - e.x) < 140 && fabsf(ents[j].y - e.y) < 140) { addKill((&e - ents), j); ents[j].kills++; ents[j].aiTilt = 0; break; } // un kill remet les idees en place (V5.17)
      if (ffa && e.isHuman && mode == MODE_SOLO) matchResult = -1; // en solo : mort = fini. En duo : tant que le coequipier vit, on continue
      if (mode == MODE_BOUNTY) {
        int killer = 1;
        for (int j = 0; j < entCount; j++)
          if (ents[j].alive && ents[j].team != e.team && fabsf(ents[j].x - e.x) < 140 && fabsf(ents[j].y - e.y) < 140) { killer = ents[j].team; break; }
        int gained = 1 + (e.stars > 2 ? 2 : e.stars);
        teamStars[killer] += gained;
      }
    }
  }

  for (int i = 0; i < MAX_PROJ; i++) {
    Proj &p = projs[i];
    if (!p.active) continue;
    p.spin += 30;
    // mine posee (attaque de Tick) : attend 3 s ou explose au contact d'un ennemi
    if (p.mineT > 0) {
      p.mineT -= dt;
      bool boom = (p.mineT <= 0);
      if (!boom)
        for (int j = 0; j < entCount && !boom; j++) {
          Ent &t = ents[j];
          if (!t.alive || t.team == p.team) continue;
          float ddx = t.x - p.x, ddy = t.y - p.y;
          if (ddx * ddx + ddy * ddy < 36 * 36) boom = true;
        }
      if (boom) {
        spawnParts(p.x, p.y, 0, 10);
        shakeT = 100; shakeMag = 2;
        for (int j = 0; j < entCount; j++) {
          Ent &t = ents[j];
          if (!t.alive || t.team == p.team) continue;
          float ddx = t.x - p.x, ddy = t.y - p.y;
          if (ddx * ddx + ddy * ddy < 34 * 34) damageEnt(t, p.dmg, ents[p.owner]);
        }
        p.active = false;
      }
      continue;
    }
    // V5.17 : Nani — les balles partent en eventail (comme Shelly) et SE REJOIGNENT
    // a la FIN de leur trajet : le garrot se referme au dernier moment
    if (p.steer != 0) {
      if (p.travelled < p.range) {
        float a = atan2f(p.vy, p.vx) + p.steer * dt;
        float sp = sqrtf(p.vx * p.vx + p.vy * p.vy);
        p.vx = fcosf(a) * sp; p.vy = fsinf(a) * sp;
      } else p.steer = 0;
    }
    float nx = p.x + p.vx * dt, ny = p.y + p.vy * dt;
    p.travelled += sqrtf((nx - p.x) * (nx - p.x) + (ny - p.y) * (ny - p.y));
    p.x = nx; p.y = ny;
    if (!p.lob && p.travelled > p.range) {
      if (p.boomer && !p.ret) { // le pic de Carl fait demi-tour : il revient comme un boomerang
        p.ret = true; p.nHits = 0; p.travelled = 0;
      } else if (!p.fromSuper && ents[p.owner].brawlerIdx == 44 && !ents[p.owner].isPet) {
        // Gene : sa boule n'a touche personne -> elle eclate en eventail de 5 eclats
        float fa = atan2f(p.vy, p.vx);
        for (int s = 0; s < 5; s++) {
          for (int k = 0; k < MAX_PROJ; k++) {
            if (projs[k].active) continue;
            Proj &sp = projs[k];
            sp.active = true; sp.lob = false; sp.steer = 0; sp.fromSuper = false; sp.pierce = false;
            sp.x = p.x; sp.y = p.y;
            float a2 = fa + (s - 2) * 0.16f;
            sp.vx = fcosf(a2) * 260; sp.vy = fsinf(a2) * 260;
            sp.dmg = p.dmg * 0.35f; sp.range = 90; sp.travelled = 0;
            sp.team = p.team; sp.owner = p.owner; sp.radius = 2;
            sp.nHits = 0; sp.shape = 0; sp.spin = 0; sp.mineT = 0;
            sp.boomer = false; sp.ret = false; sp.bounce = 0;
            sp.bIdx = p.bIdx; // V5.13 : herite le graphisme du brawler tireur
            break;
          }
        }
        spawnParts(p.x, p.y, 0, 5);
        p.active = false; continue;
      } else if (!p.fromSuper && p.bIdx == 37) { // V5.17 : Hank — la bulle ECLATE en zone
        for (int j = 0; j < entCount; j++) {
          Ent &t = ents[j];
          if (!t.alive || t.team == p.team) continue;
          float ddx = t.x - p.x, ddy = t.y - p.y;
          if (ddx * ddx + ddy * ddy < 45 * 45) damageEnt(t, p.dmg * 0.75f, ents[p.owner]);
        }
        spawnParts(p.x, p.y, 0, 12); shakeT = 120; shakeMag = 3;
        p.active = false; continue;
      } else { p.active = false; continue; }
    }
    // V5.17 : Hank — sa bulle GROSSIT en avancant puis eclate en zone a la portee max
    if (p.bIdx == 37 && !p.fromSuper && p.mineT <= 0)
      p.radius = 4 + (int)(p.travelled / fmaxf(1.0f, (float)p.range) * 9.0f);
    // V5.17 : ESQUIVE (tres haut niveau : brawler a 1600 tr MINIMUM) — les bons bots    // s'ecartent d'un pas du projectile qui arrive sur eux, pas de fuite panique
    if (matchTr() >= 1600 && !p.lob && p.mineT <= 0) {
      for (int j = 0; j < entCount; j++) {
        Ent &e2 = ents[j];
        if (!e2.alive || e2.isHuman || e2.isPet || e2.dashT > 0 || e2.team == p.team) continue;
        float rx = e2.x - p.x, ry = e2.y - p.y;
        float dd = rx * rx + ry * ry;
        if (dd > 70 * 70 || dd < 1) continue;
        if (rx * p.vx + ry * p.vy <= 0) continue; // le tir s'eloigne : pas la peine
        if (rnd(100) < 55) {
          float nx2 = -p.vy, ny2 = p.vx, nl = sqrtf(nx2 * nx2 + ny2 * ny2);
          int side = (rnd(2)) ? 1 : -1;
          if (nl > 0.01f) moveEnt(e2, nx2 / nl * side * 42.0f * dt, ny2 / nl * side * 42.0f * dt);
        }
      }
    }
    if (p.ret) { // retour vers Carl : peut toucher d'autres cibles au passage
      float bdx = ents[p.owner].x - p.x, bdy = ents[p.owner].y - p.y;
      float bl = sqrtf(bdx * bdx + bdy * bdy);
      if (bl < 12 || !ents[p.owner].alive) { p.active = false; continue; }
      float bsp = sqrtf(p.vx * p.vx + p.vy * p.vy);
      p.vx = bdx / bl * bsp; p.vy = bdy / bl * bsp;
    }
    // anti-tunneling : milieu du pas teste aussi. Les supers perceurs cassent
    // les murs a l'impact (Colt, Brock, Rico...), les tirs normaux s'y arretent.
    if (!p.lob && !p.boomer) {
      // V5.13b FIX : on teste AUSSI les croisements d'axes — plus moyen de glisser
      // dans l'interstice de deux murs en diagonale (le coin bloque, comme dans BS)
      float ox = p.x - p.vx * dt, oy = p.y - p.vy * dt; // position d'origine du pas
      bool hitWall = solidAt(p.x, p.y) || solidAt(p.x - p.vx * dt * 0.5f, p.y - p.vy * dt * 0.5f)
                  || solidAt(p.x, oy) || solidAt(ox, p.y); // tuiles croisees horizontalement/verticalement
      if (hitWall) {
        if (p.bounce > 0) { // Rico : demi-tour sur le mur, la balle continue
          p.bounce--;
          p.range += 100; // le rebond prolonge la portee
          if (solidAt(p.x + p.vx * dt, p.y)) p.vx = -p.vx; // mur vertical : rebond horizontal
          else p.vy = -p.vy;
          p.x -= p.vx * dt * 2; p.y -= p.vy * dt * 2; // on se degage du mur
        }
        else if (p.fromSuper) breakWalls(p.x, p.y, 16);
        else { spawnParts(p.x, p.y, 0, 3); p.active = false; continue; } // etincelles quand un tir se crase au mur
      }
    }
    hitDestructibles(p); // boites de cubes et tonneaux explosifs
    if (!p.active) continue;
    if (mode == MODE_HEIST)
      for (int s = 0; s < MAX_SAFE; s++) {
        if (safes[s].hp <= 0 || safes[s].team == p.team) continue;
        float ddx = safes[s].x - p.x, ddy = safes[s].y - p.y;
        if (ddx * ddx + ddy * ddy < (p.radius + 12) * (p.radius + 12)) {
          safes[s].hp -= p.dmg;
          addPop(safes[s].x, safes[s].y - 14, (int)p.dmg);
          shakeT = 100; shakeMag = 2;
          p.active = false; break;
        }
      }
    if (!p.active) continue;
    // les lanceurs (ATK_LOB) : le projectile passe au-dessus des murs et des
    // ennemis, puis explose en ZONE a l'atterrissage (comme dans Brawl Stars)
    if (p.lob) {
      if (p.travelled >= p.range) {
        // Tick : sa tete atterrie eclate en 3 mines armees pendant 3 s
        if (ents[p.owner].brawlerIdx == 11 && !p.fromSuper && !ents[p.owner].isPet) {
          static const float mOff[3][2] = {{-12,-6},{12,-6},{0,10}};
          for (int m = 0; m < 3; m++) {
            for (int k = 0; k < MAX_PROJ; k++) {
              if (projs[k].active) continue;
              Proj &mp = projs[k];
              mp.active = true; mp.lob = false; mp.steer = 0; mp.fromSuper = false; mp.pierce = false;
              mp.x = p.x + mOff[m][0]; mp.y = p.y + mOff[m][1];
              mp.vx = mp.vy = 0; mp.dmg = p.dmg; mp.travelled = 0; mp.range = 0;
              mp.team = p.team; mp.owner = p.owner; mp.radius = 3;
              mp.nHits = 0; mp.shape = p.shape; mp.spin = 0; mp.mineT = 3.0f;
              mp.bIdx = p.bIdx; // V5.13
              break;
            }
          }
          spawnParts(p.x, p.y, 0, 6);
          p.active = false;
          continue;
        }
        // Barley : le cocktail explose ET laisse une PETITE flaque qui brule 2 s
        if (ents[p.owner].brawlerIdx == 6 && !ents[p.owner].isPet)
          spawnFireZone(p.x, p.y, p.dmg * 0.5f, p.team, p.owner, 2.0f, 28.0f);
        // Amber : flaque enflammee RENFORCEE mais qui disparait vite (1 s)
        else if (ents[p.owner].brawlerIdx == 43 && !ents[p.owner].isPet)
          spawnFireZone(p.x, p.y, p.dmg * 0.7f, p.team, p.owner, 1.0f, 40.0f);
        int R = p.fromSuper ? 40 : 30;
        spawnParts(p.x, p.y, 0, 10);
        shakeT = 100; shakeMag = 2;
        if (p.fromSuper && ents[p.owner].brawlerIdx == 10) { // giga-dynamite : zone ET plus large, ET qui defonce les murs
          R = 100; breakWalls(p.x, p.y, 80); shakeT = 200; shakeMag = 4; spawnParts(p.x, p.y, 0, 20);
        }
        for (int j = 0; j < entCount; j++) {
          Ent &t = ents[j];
          if (!t.alive || t.team == p.team) continue;
          float ddx = t.x - p.x, ddy = t.y - p.y;
          if (ddx * ddx + ddy * ddy < R * R) damageEnt(t, p.dmg, ents[p.owner]);
        }
        // V5.23 : POTION DE BYRON — les ALLIES touches par la zone sont SOIGNES
        if (p.fromSuper && p.bIdx == 46) {
          spawnParts(p.x, p.y, 1, 12); // eclaboussure de potion
          for (int j = 0; j < entCount; j++) {
            Ent &t = ents[j];
            if (!t.alive || t.team != p.team) continue;
            float ddx = t.x - p.x, ddy = t.y - p.y;
            if (ddx * ddx + ddy * ddy < R * R && t.hp < t.maxHP) {
              float h = fminf(t.maxHP - t.hp, p.dmg * 0.9f);
              t.hp += h;
              addPop(t.x, t.y - 14, (int)h);
              spawnParts(t.x, t.y, 1, 4);
            }
          }
        }
        // V5.23 : les explosions ne REPOUSSENT plus personne (tirs normaux, lanceurs,
        // super de Barley...) — seules les poussettes exprès (Gale, Pearl...) deplacent.
        if (mode == MODE_HEIST)
          for (int s = 0; s < MAX_SAFE; s++) {
            if (safes[s].hp <= 0 || safes[s].team == p.team) continue;
            float ddx = safes[s].x - p.x, ddy = safes[s].y - p.y;
            if (ddx * ddx + ddy * ddy < (R + 12) * (R + 12)) {
              safes[s].hp -= p.dmg;
              addPop(safes[s].x, safes[s].y - 14, (int)p.dmg);
            }
          }
        p.active = false;
      }
      continue;
    }
    for (int j = 0; j < entCount; j++) {
      Ent &t = ents[j];
      if (!t.alive || t.team == p.team) continue;
      float ddx = t.x - p.x, ddy = t.y - p.y;
      if (ddx * ddx + ddy * ddy < (p.radius + 7) * (p.radius + 7)) {
        float dm = p.dmg;
        // Piper : plus la cible est loin, plus ca fait mal (x0.5 a bout portant, x1.5 a portee max)
        if (ents[p.owner].brawlerIdx == 50)
          dm *= 0.5f + fminf(1.0f, p.travelled / fmaxf(1.0f, p.range));
        // drones de Bea / spray d'Emz (super) : la cible est ralentie
        if (p.fromSuper && (ents[p.owner].brawlerIdx == 24 || ents[p.owner].brawlerIdx == 19)) t.slowT = 2.5f;
        // V5.23 : BYRON — son attaque EMPOISONNE la cible pendant 3 s
        if (!p.fromSuper && p.bIdx == 46) { t.poisonT = 3.0f; t.poisonBy = p.owner; }
        // V5.16 : Bea — si sa balle touche, la suivante est CHARGEE (degats x3)
        if (ents[p.owner].brawlerIdx == 24 && !p.fromSuper) ents[p.owner].beaCharge = true;
        if (p.pierce) { // onde perforante : chaque cible touchee une seule fois
          bool already = false;
          for (int k = 0; k < p.nHits; k++) if (p.hitIds[k] == j) { already = true; break; }
          if (already) continue;
          if (p.nHits < 8) p.hitIds[p.nHits++] = j;
          damageEnt(t, dm, ents[p.owner]);
        } else {
          damageEnt(t, dm, ents[p.owner]);
          p.active = false; break;
        }
        // Edgar : chaque coup qui touche lui rend de la vie
        if (ents[p.owner].brawlerIdx == 26 && ents[p.owner].alive) {
          float before = ents[p.owner].hp;
          ents[p.owner].hp = fminf(ents[p.owner].maxHP, ents[p.owner].hp + dm * 0.35f);
          if ((int)ents[p.owner].hp != (int)before) addPop(ents[p.owner].x, ents[p.owner].y - 12, (int)(ents[p.owner].hp - before));
        }
      }
    }
  }

  // zones au sol (Barley) : degats toutes les 0.66 s tant qu'on reste dedans
  for (int i = 0; i < 8; i++) {
    FireZone &z = fireZones[i];
    if (!z.active) continue;
    z.life -= dt;
    if (z.life <= 0) { z.active = false; continue; }
    z.tickT -= dt;
    if (z.tickT <= 0) {
      z.tickT = 0.66f;
      for (int j = 0; j < entCount; j++) {
        Ent &t = ents[j];
        if (!t.alive || t.team == z.team || t.isPet) continue;
        float ddx = t.x - z.x, ddy = t.y - z.y;
        if (ddx * ddx + ddy * ddy < z.r * z.r) damageEnt(t, z.dmg, ents[z.owner]);
      }
      if (mode == MODE_HEIST)
        for (int s = 0; s < MAX_SAFE; s++) {
          if (safes[s].hp <= 0 || safes[s].team == z.team) continue;
          float ddx = safes[s].x - z.x, ddy = safes[s].y - z.y;
          if (ddx * ddx + ddy * ddy < 50 * 50) { safes[s].hp -= z.dmg; addPop(safes[s].x, safes[s].y - 14, (int)z.dmg); }
        }
    }
  }

  if (mode == MODE_GEM) {
    for (int t = 0; t < 5; t++) teamGems[t] = 0;
    for (int i = 0; i < entCount; i++) if (ents[i].alive) teamGems[ents[i].team] += ents[i].gems;
    for (int g = 0; g < 10; g++) {
      if (!gems[g].active) continue;
      for (int i = 0; i < entCount; i++)
        if (!ents[i].isPet && ents[i].alive && fabsf(ents[i].x - gems[g].x) < 12 && fabsf(ents[i].y - gems[g].y) < 12) {
          gems[g].active = false; ents[i].gems++;
          if (i == humanIdx) gemCounter++;
          break;
        }
      int actives = 0;
      for (int q = 0; q < 10; q++) actives += gems[q].active;
      if (actives == 0 && rnd(300) < 6) {
        gems[g].active = true;
        gems[g].x = mapW * TILE / 2 + (rnd(3) - 1) * 40;
        gems[g].y = mapH * TILE / 2 + (rnd(3) - 1) * 40;
      }
    }
    if (teamGems[0] >= 10 || teamGems[1] >= 10) {
      if (gemCountdown < 0) gemCountdown = 15000;
      gemCountdown -= (int)(dt * 1000);
      if (gemCountdown <= 0) {
        // fin du compte a rebours : l'equipe qui a 10+ gemmes gagne, sinon egalite
        if (teamGems[0] >= 10 && teamGems[1] >= 10) matchResult = (teamGems[0] >= teamGems[1]) ? 1 : -1;
        else if (teamGems[0] >= 10) matchResult = 1;
        else if (teamGems[1] >= 10) matchResult = -1;
        else matchResult = 0;
      }
    } else gemCountdown = -1;
  }

  if (mode == MODE_BALL) {
    if (ballCd > 0) ballCd -= (int)(dt * 1000); // V5.15 : delai anti double-but
    if (ballShotT > 0) ballShotT -= (int)(dt * 1000); // V5.17 : cooldown de reprise pour le tireur
    bool carried = false;
    for (int i = 0; i < entCount; i++) if (ents[i].alive && ents[i].hasBall) carried = true;
    if (!carried && ballCd <= 0) { // V5.15 : ramassage impossible pendant la remise en jeu
      for (int i = 0; i < entCount; i++) {
        Ent &e = ents[i];
        if (e.isPet || !e.alive || e.hasBall) continue;
        if (i == ballShooter && ballShotT > 0) continue; // V5.17 : pas de reprise immediate par le tireur
        if (fabsf(e.x - ballX) < 12 && fabsf(e.y - ballY) < 12) { e.hasBall = true; break; }
      }
      if (!carried) {
        ballX += ballVX * dt; ballY += ballVY * dt;
        ballVX *= 0.95f; ballVY *= 0.95f;
        if (solidAt(ballX, ballY)) { ballX -= ballVX * dt * 2; ballY -= ballVY * dt * 2; ballVX = ballVY = 0; }
        // ballon libre qui entre dans un but (entre les poteaux) : but pour l'equipe qui attaque ce cote
        int goalCY = mapH * TILE / 2;
        if (ballCd <= 0 && fabsf(ballY - goalCY) < 2.2f * TILE) { // V5.15 : aucun but pendant la remise en jeu
          if (ballX > mapW * TILE - 14) { teamGoal(0); }
          else if (ballX < 14) { teamGoal(1); }
        } else {
          if (ballX > mapW * TILE - 14) { ballX = mapW * TILE - 14; ballVX = 0; }
          if (ballX < 14) { ballX = 14; ballVX = 0; }
        }
      }
    } else {
      int goalCY = mapH * TILE / 2;
      for (int i = 0; i < entCount; i++)
        if (ents[i].alive && ents[i].hasBall) {
          ballX = ents[i].x; ballY = ents[i].y;
          bool inGoalMouth = fabsf(ballY - goalCY) < 2.2f * TILE; // dans les poteaux, pas au-dessus/dessous
          if ((ents[i].team == 0 && ballX > mapW * TILE - 26) ||
              (ents[i].team == 1 && ballX < 26)) {
            if (inGoalMouth && ballCd <= 0) { // V5.15 : remise complete (centre + camps), pas de double-but
              ents[i].goals++; ents[i].hasBall = false;
              ballResetAfterGoal();
            } else { // hors des poteaux : on reste dans le terrain
              ballX = ents[i].x = (ents[i].team == 0) ? mapW * TILE - 26 : 26;
            }
          }
        }
    }
    int g0 = 0, g1 = 0;
    for (int i = 0; i < entCount; i++) {
      if (ents[i].team == 0) g0 = fmaxf(g0, ents[i].goals); else g1 = fmaxf(g1, ents[i].goals);
    }
    if (g0 >= 2) matchResult = 1;
    if (g1 >= 2) matchResult = -1;
  }

  if (mode == MODE_HEIST) {
    for (int s = 0; s < MAX_SAFE; s++) {
      if (safes[s].maxHP > 0 && safes[s].hp <= 0)
        matchResult = (s == 1) ? 1 : -1;
    }
  }

  if (mode == MODE_BOUNTY) {
    if (teamStars[0] >= 20) matchResult = 1;
    if (teamStars[1] >= 20) matchResult = -1;
  }

  if (mode == MODE_ZONE) {
    int all0 = 1, all1 = 1; // il faut TOUTES les zones prises pour gagner
    for (int z = 0; z < MAX_ZONE; z++) {
      int myTeamHere = 0, enTeamHere = 0;
      for (int i = 0; i < entCount; i++) {
        if (!ents[i].alive || ents[i].isPet) continue; // les tourelles ne capturent pas
        if (fabsf(ents[i].x - zones[z].x) < 24 && fabsf(ents[i].y - zones[z].y) < 24) {
          if (ents[i].team == 0) myTeamHere++; else enTeamHere++;
        }
      }
      // V5.13 : capture VERROUILLEE — REMPLACEE en V5.19 : le verrou bloquait la
      // 2e zone des qu l'ennemi l'avait entamee. Une zone tenue par une seule equipe
      // REPART dans SON sens, meme si l'adversaire l'avait commencee. Zone contestee = gelee.
      if (myTeamHere > 0 && enTeamHere == 0) zones[z].prog = fminf(100, zones[z].prog + 9 * dt);
      if (enTeamHere > 0 && myTeamHere == 0) zones[z].prog = fmaxf(-100, zones[z].prog - 9 * dt);
      if (zones[z].prog < 100) all0 = 0;
      if (zones[z].prog > -100) all1 = 0;
    }
    if (all0) matchResult = 1;   // toutes les zones chaudes prises : victoire
    if (all1) matchResult = -1;
  }

  if (mode == MODE_KO || ffa) {
    poisonWaveT -= (int)(dt * 1000);
    if (poisonWaveT <= 0) {
      // vague lente : retrait progressif, bloque au minimum (= poison sur toute la map)
      poisonWaveT = 7000;
      soloShrink -= (mode == MODE_KO) ? 50 : 35;
      if (soloShrink < 80) soloShrink = 80; // toute la map empoisonnee : ca reste jusqu'au gagnant
    }
    int cx = mapW * TILE / 2, cy = mapH * TILE / 2;
    for (int i = 0; i < entCount; i++) {
      Ent &e = ents[i];
      if (!e.alive) continue;
      int ddx = e.x - cx, ddy = e.y - cy;
      if (ddx * ddx + ddy * ddy > soloShrink * soloShrink) { e.hp -= 700 * dt; e.regenT = 0; }
    }
    if (mode == MODE_KO) {
      int alive0 = 0, alive1 = 0;
      for (int i = 0; i < entCount; i++)
        if (ents[i].alive && !ents[i].isPet) { if (ents[i].team == 0) alive0++; else alive1++; }
      if (alive0 == 0) matchResult = -1;
      if (alive1 == 0) matchResult = 1;
      // V5.16 : A-MORT ELIMINATOIRE — apres 2 min, mort subite : tous teleportes face a face au centre
      if (alive0 > 0 && alive1 > 0 && matchTimeMs > 120000 && !gSuddenDone) {
        gSuddenDone = true;
        int cx2 = mapW * TILE / 2, cy2 = mapH * TILE / 2;
        breakWalls(cx2, cy2, 130);
        int offs[2] = {0, 0};
        for (int i = 0; i < entCount; i++) {
          Ent &e2 = ents[i];
          if (!e2.alive || e2.isPet) continue;
          e2.hp = e2.maxHP; e2.regenT = 0; e2.gems = 0;
          e2.x = cx2 + ((e2.team == 0) ? -70 : 70);
          e2.y = cy2 + (offs[e2.team & 1]++ - 1) * 40;
          tileMap[(int)(e2.x / TILE)][(int)(e2.y / TILE)] = 0;
          spawnParts(e2.x, e2.y, 2, 8);
        }
        gKOBannerT = 1500;
      }
    } else {
      // Survivant solo/duo : victoire si tous les ennemis sont morts ET qu'il
      // reste quelqu'un de ton equipe (toi OU ton coequipier), defaite sinon
      bool myTeam = false, otherTeam = false;
      for (int i = 0; i < entCount; i++)
        if (ents[i].alive && !ents[i].isPet) { if (ents[i].team == 0) myTeam = true; else otherTeam = true; }
      if (!myTeam) matchResult = -1;        // equipe du joueur eliminee
      else if (!otherTeam) matchResult = 1; // tous les ennemis morts : on gagne
    }
  }

  if (mode == MODE_SURVIE) { // SURVIE : vague suivante quand la map est vide, defaite si le joueur tombe
    bool playerAlive = false; int foes = 0;
    for (int i = 0; i < entCount; i++) {
      if (!ents[i].alive || ents[i].isPet) continue;
      if (ents[i].team == 0) playerAlive = true; else foes++;
    }
    if (!playerAlive) matchResult = -1;
    else if (foes == 0) {
      waveBreakT -= (int)(dt * 1000);
      if (waveBreakT <= 0) { // VAGUE SUIVANTE !
        waveN++;
        int n = 1 + waveN; if (n > 5) n = 5;
        float sk = fminf(1.0f, 0.3f + waveN * 0.07f); // l'IA apprend a bien jouer vague apres vague
        for (int en = 0; en < n; en++) {
          float ex = (en % 2) ? (mapW * TILE - 30) : 30;
          float ey = (en < 2) ? 30 : ((en < 4) ? mapH * TILE - 30 : mapH * TILE / 2);
          tileMap[(int)(ex / TILE)][(int)(ey / TILE)] = 0; // jamais dans un mur
          addEnt(rnd(55), -1, 1, ex, ey, false, sk);
          Ent &b = ents[entCount - 1];
          b.maxHP *= 1.0f + waveN * 0.18f; b.hp = b.maxHP; // les bots encaissent de plus en plus
        }
        spawnParts(mapW * TILE / 2, mapH * TILE / 2, 0, 10);
        gWaveBannerT = 1500;
        waveBreakT = 2500;
      }
    } else waveBreakT = 2500;
  }
  if (mode == MODE_SIEGE || mode == MODE_BOSSRUSH || mode == MODE_DUEL) { // Siege/BossRush : boss a terre / Duel : equipe epuisee
    bool bossAlive = false, t0 = false, t1 = false;
    for (int i = 0; i < entCount; i++) {
      if (ents[i].isPet) continue;
      if (ents[i].isBoss) { if (ents[i].alive) bossAlive = true; }
      else if (ents[i].team == 0) { if (ents[i].alive || ents[i].respawnT < 9000000) t0 = true; } // V5.16 : les morts en attente de respawn comptent
      else if (ents[i].alive) t1 = true;
    }
    if (mode == MODE_SIEGE) { if (!bossAlive) matchResult = 1; else if (!t0) matchResult = -1; }
    else if (mode == MODE_BOSSRUSH) {
      if (!bossAlive) {
        gBossN++;
        if (gBossN >= 3) matchResult = 1; // les 3 robots sont tombes : victoire !
        else { // V5.16 : boss suivant, plus costaud, avec un NOUVEAU pattern
          gBossPattern = gBossN; gBossT = 2000;
          addEnt(47, -1, 1, mapW * TILE - 80, mapH * TILE / 2, false, 0.9f);
          Ent &bs = ents[entCount - 1];
          bs.isBoss = 1; bs.mechT = 1.0f;
          bs.maxHP = 30000 + gBossN * 15000 + (int)(aiSkillFromTrophies() * 15000); bs.hp = bs.maxHP;
          if (gBossPattern == 2) bs.speedBuffT = 999.0f; // pattern 2 : le boss pieque le rythme
          tileMap[(int)(bs.x / TILE)][(int)(bs.y / TILE)] = 0;
          gKOBannerT = 1500; // banniere "BOSS n/3"
        }
      } else if (!t0) matchResult = -1;
    }
    else { if (!t1) matchResult = 1; else if (!t0) matchResult = -1; }
  }

  if (mode == MODE_BOSSRUSH) { // V5.16 : patterns du mega robot : normal / rafales radiales / invocations
    for (int i = 0; i < entCount; i++) {
      Ent &b = ents[i];
      if (!b.alive || !b.isBoss) continue;
      gBossT -= (int)(dt * 1000);
      if (gBossT > 0) continue;
      if (gBossPattern == 1) { // pattern 1 : rafale RADIALE de 16 tirs
        gBossT = 3500;
        for (int a = 0; a < 16; a++) fireProj(b, a * 0.3927f, false, 0.4f, 0.8f, 0.6f);
        spawnParts(b.x, b.y, 1, 8);
      } else if (gBossPattern == 2) { // pattern 2 : INVOQUE 2 pingouins
        gBossT = 9000;
        spawnPet(b, 5); spawnPet(b, 5);
      } else gBossT = 2000;
    }
  }
  if (matchTimeMs > 150000) {
    if (mode == MODE_GEM) matchResult = (teamGems[0] > teamGems[1]) ? 1 : (teamGems[0] < teamGems[1] ? -1 : 0);
    else if (mode == MODE_BOUNTY) matchResult = (teamStars[0] > teamStars[1]) ? 1 : (teamStars[0] < teamStars[1] ? -1 : 0);
    else if (mode == MODE_HEIST) matchResult = (safes[1].hp < safes[0].hp) ? 1 : ((safes[1].hp > safes[0].hp) ? -1 : 0);
    else if (mode == MODE_ZONE) {
      float m = 0, n = 0;
      for (int z = 0; z < MAX_ZONE; z++) { if (zones[z].prog > 0) m += zones[z].prog; else n -= zones[z].prog; }
      matchResult = (m > n) ? 1 : ((m < n) ? -1 : 0);
    }
    else if (!ffa && mode != MODE_KO && mode != MODE_SIEGE) matchResult = 0; // pas de limite de temps en solo/duo/KO/siege
  }
}

// ------------------------- Texte / polices ---------------------------
static const uint8_t FONT5[10][5] = {
  {0x0E,0x11,0x11,0x11,0x0E},{0x04,0x0C,0x04,0x04,0x0E},{0x0E,0x01,0x0E,0x10,0x0E},
  {0x0E,0x01,0x06,0x01,0x0E},{0x11,0x11,0x0E,0x01,0x01},{0x0E,0x10,0x0E,0x01,0x0E},
  {0x0E,0x10,0x0E,0x11,0x0E},{0x0E,0x01,0x02,0x04,0x04},{0x0E,0x11,0x0E,0x11,0x0E},
  {0x0E,0x11,0x0E,0x01,0x0E},
};
static const uint8_t FONTA[26][5] = {
  {0x0E,0x11,0x1F,0x11,0x11},{0x1E,0x11,0x1E,0x11,0x1E},{0x0F,0x10,0x10,0x10,0x0F},
  {0x1E,0x11,0x11,0x11,0x1E},{0x1F,0x10,0x1E,0x10,0x1F},{0x1F,0x10,0x1E,0x10,0x10},
  {0x0F,0x10,0x13,0x11,0x0F},{0x11,0x11,0x1F,0x11,0x11},{0x0E,0x04,0x04,0x04,0x0E},
  {0x07,0x02,0x02,0x02,0x1C},{0x11,0x12,0x1C,0x12,0x11},{0x10,0x10,0x10,0x10,0x1F},
  {0x11,0x1B,0x15,0x11,0x11},{0x11,0x19,0x15,0x13,0x11},{0x0E,0x11,0x11,0x11,0x0E},
  {0x1E,0x11,0x1E,0x10,0x10},{0x0E,0x11,0x15,0x12,0x0D},{0x1E,0x11,0x1E,0x12,0x11},
  {0x0F,0x10,0x0E,0x01,0x1E},{0x1F,0x04,0x04,0x04,0x04},{0x11,0x11,0x11,0x11,0x0E},
  {0x11,0x11,0x11,0x0A,0x04},{0x11,0x11,0x15,0x1B,0x11},{0x11,0x0A,0x04,0x0A,0x11},
  {0x11,0x11,0x0F,0x01,0x0E},{0x1F,0x01,0x0E,0x10,0x1F},
};
// Rendu d'une ligne de glyphe en runs (ombre + trait), ~10 pushes par caractere
// au lieu de 30-60 : c'est le plus gros gain de performances du jeu.
// Texte : chaque bit du glyphe 5x5 est dessine en bloc 2x2 => police 10px,
// beaucoup plus lisible sur l'ecran de la calculatrice.
static void drawGlyphRow(int x, int y, uint8_t row, int ncols, KDColor c) {
  if (!row) return;
  // V5.12 perf : fusionne les bits adjacents en un seul fRect (runs) au lieu d'un fRect par bit
  int col = 0;
  while (col < ncols) {
    if (row & (0x10 >> col)) {
      int s = col;
      while (col < ncols && (row & (0x10 >> col))) col++;
      fRect(x + s * 2, y, (col - s) * 2, 2, c);
    } else col++;
  }
}
static void drawDigit(int x, int y, int d, KDColor c) {
  for (int r = 0; r < 5; r++) drawGlyphRow(x, y + r * 2, FONT5[d][r], 5, c); // 5 colonnes : chiffre complet
}
static void drawNum(int x, int y, int v, KDColor c) {
  if (v < 0) { fRect(x, y + 4, 6, 2, c); drawNum(x + 10, y, -v, c); return; }
  char buf[8]; int n = 0;
  do { buf[n++] = '0' + v % 10; v /= 10; } while (v);
  for (int i = n - 1; i >= 0; i--) { drawDigit(x, y, buf[i] - '0', c); x += 12; }
}
static void drawText(int x, int y, const char *s, KDColor c) {
  while (*s) {
    char ch = *s++;
    if (ch >= 'a' && ch <= 'z') ch -= 32; // minuscules acceptees : dessinees en majuscules
    if (ch == ' ') { x += 10; continue; }
    if (ch == '-') { fRect(x, y + 4, 8, 2, c); x += 10; continue; }
    if (ch == '!') { fRect(x, y, 2, 10, c); x += 8; continue; }
    if (ch == ':') { fRect(x, y, 2, 2, c); fRect(x, y + 6, 2, 2, c); x += 8; continue; }
    if (ch == '.') { fRect(x, y + 8, 2, 2, c); x += 8; continue; }
    if (ch == '+') { fRect(x, y + 4, 8, 2, c); fRect(x + 3, y + 2, 2, 6, c); x += 12; continue; }
    if (ch == '?') { fRect(x, y, 2, 4, c); fRect(x + 4, y, 2, 2, c); fRect(x + 2, y + 4, 2, 2, c); fRect(x + 2, y + 8, 2, 2, c); x += 10; continue; }
    if (ch >= 'A' && ch <= 'Z') {
      for (int r = 0; r < 5; r++) drawGlyphRow(x, y + r * 2, FONTA[ch - 'A'][r], 5, c);
    } else if (ch >= '0' && ch <= '9') drawDigit(x, y, ch - '0', c);
    x += 12;
  }
}
static int textW(const char *s) { int w = 0; for (const char *p = s; *p; p++) w += (*p == ' ') ? 10 : 12; return w; }
static void drawTextC(int y, const char *s, KDColor c) { drawText((SW - textW(s)) / 2, y, s, c); }

// ---- V5.10 : icone d'emote (bulle au-dessus du joueur, touches + et -) ----
static const char *EMOTE_NAMES[7] = {"AUCUNE", "COEUR", "CRANE", "GG", "FUREUR", "AIDE", "ETOILE"};
static void drawEmoteIcon(int cx, int cy, int k) {
  if (k == 1) { // coeur
    fCirc(cx - 3, cy - 1, 3, C_RED); fCirc(cx + 3, cy - 1, 3, C_RED);
    fRect(cx - 6, cy, 12, 4, C_RED); fRect(cx - 4, cy + 4, 8, 2, C_RED); fRect(cx - 2, cy + 6, 4, 2, C_RED);
  } else if (k == 2) { // crane
    fCirc(cx, cy, 6, C_WHT); fRect(cx - 4, cy + 4, 8, 3, C_WHT);
    fRect(cx - 4, cy - 1, 2, 3, C_BLK); fRect(cx + 2, cy - 1, 2, 3, C_BLK); fRect(cx - 1, cy + 3, 2, 2, C_BLK);
  } else if (k == 3) drawText(cx - textW("GG") / 2, cy - 4, "GG", C_GREEN);
  else if (k == 4) drawText(cx - 2, cy - 5, "!", C_RED);
  else if (k == 5) drawText(cx - 2, cy - 5, "?", C_YEL);
  else { // etoile doree
    fRect(cx - 2, cy - 6, 4, 12, C_GOLD); fRect(cx - 6, cy - 2, 12, 4, C_GOLD);
    fRect(cx - 1, cy - 7, 2, 14, C_YEL); fRect(cx - 7, cy - 1, 14, 2, C_YEL);
  }
}

// ------------------------- Rendu du monde (themes) --------------------
static void drawWallThemed(int sx, int sy) {
  const MapTheme &T = THEMES[mode];
  KDColor w = KDColor::RGB24(T.wall), wd = KDColor::RGB24(T.wallD), wl = KDColor::RGB24(T.wallL);
  int lift = 3;
  fRect(sx, sy, TILE, TILE, wl);
  switch (T.wallStyle) {
    case 0: // brique
      fRect(sx, sy + lift, TILE, TILE - lift, wd);
      for (int row = 0; row < 2; row++)
        for (int c2 = 0; c2 < 2; c2++)
          fRect(sx + 1 + c2 * 6 + (row % 2) * 2, sy + lift + 1 + row * 4, 5, 3, w);
      break;
    case 1: // pierre taille
      fRect(sx, sy + lift, TILE, TILE - lift, wd);
      fRect(sx + 1, sy + lift + 1, TILE - 2, TILE - lift - 2, w);
      fRect(sx + 2, sy + lift + 2, TILE - 4, 2, wl);
      px(sx + 4, sy + lift + 7, wd); px(sx + 9, sy + lift + 9, wd);
      break;
    case 2: // sacs de sable
      for (int row = 0; row < 3; row++)
        for (int c2 = 0; c2 < 2; c2++) {
          int ox = sx + 1 + c2 * 6 + (row % 2) * 2;
          oCirc(ox + 2, sy + lift + 2 + row * 3, 2, w, wd);
        }
      break;
    case 3: // crates bois
      fRect(sx, sy + lift, TILE, TILE - lift, w);
      fRect(sx, sy + lift, TILE, 2, wl);
      fRect(sx, sy + lift + 3, 2, TILE - lift - 3, wl);
      fRect(sx + TILE - 3, sy + lift + 3, 2, TILE - lift - 3, wl);
      fRect(sx + 2, sy + lift + 5, TILE - 4, 2, wd);
      fRect(sx + 5, sy + lift, 2, TILE - lift, wd);
      break;
    default: // glace
      fRect(sx, sy + lift, TILE, TILE - lift, w);
      fRect(sx + 2, sy + lift + 2, 4, 3, wl);
      fRect(sx + 8, sy + lift + 6, 4, 2, wl);
      px(sx + 10, sy + lift + 2, wl);
      break;
  }
  fRect(sx, sy, TILE, 1, C_BLK); fRect(sx, sy + TILE - 1, TILE, 1, C_BLK);
  fRect(sx, sy, 1, TILE, C_BLK); fRect(sx + TILE - 1, sy, 1, TILE, C_BLK);
}
static void drawBushThemed(int sx, int sy) {
  const MapTheme &T = THEMES[mode];
  KDColor b1 = KDColor::RGB24(seasonTint(T.bush1)), b2 = KDColor::RGB24(seasonTint(T.bush2));
  fRect(sx, sy, TILE, TILE, b1);
  fCirc(sx + 4, sy + 4, 4, b2);
  fCirc(sx + 10, sy + 5, 4, b2);
  fCirc(sx + 7, sy + 10, 4, b2);
  px(sx + 5, sy + 3, KDColor::RGB24(seasonTint(T.grDecor))); px(sx + 11, sy + 4, KDColor::RGB24(seasonTint(T.grDecor)));
}
static void drawGroundDecor(int sx, int sy, int tx, int ty) {
  const MapTheme &T = THEMES[mode];
  KDColor dec = KDColor::RGB24(seasonTint(T.grDecor));
  switch (T.decoStyle) {
    case 0: px(sx + (tx * 5 + ty * 3) % 12, sy + (tx * 3 + ty * 7) % 12, dec); break;
    case 1: // fleurs
      if (((tx * 7 + ty * 13) & 7) == 0) {
        int bx = (tx * 5 + ty * 3) % 10, by = (tx * 3 + ty * 7) % 10;
        px(sx + bx, sy + by, dec); px(sx + bx - 1, sy + by, C_MAG); px(sx + bx + 1, sy + by, C_MAG);
        px(sx + bx, sy + by - 1, C_MAG); px(sx + bx, sy + by + 1, C_MAG);
      }
      break;
    case 2: // cranes / pierres
      if (((tx * 7 + ty * 13) & 15) == 0) {
        int bx = (tx * 3 + ty * 5) % 10, by = (tx * 5 + ty * 3) % 10;
        fRect(sx + bx, sy + by, 3, 2, KDColor::RGB24(0xE0D8C0));
        px(sx + bx, sy + by, C_BLK); px(sx + bx + 2, sy + by, C_BLK);
      }
      break;
    case 3: // lignes de stade
      if (ty % 4 == 2) fRect(sx, sy + 6, TILE, 1, dec);
      break;
    default: // neige / eclats de glace
      if (((tx * 7 + ty * 13) & 7) == 0) {
        int bx = (tx * 5 + ty * 3) % 12, by = (tx * 3 + ty * 7) % 12;
        fRect(sx + bx, sy + by, 2, 1, dec); px(sx + bx + 2, sy + by + 1, dec);
      }
      break;
  }
}
static void drawGem(int cx, int cy, int t) {
  int bob = (t / 400) % 2;
  shadow(cx, cy + 5, 4, 2);
  for (int j = -5; j <= 5; j++)
    for (int i = -4; i <= 4; i++) {
      int w = (j < 0) ? 4 + j : 4 - j;
      if (abs(i) <= w) px(cx + i, cy + j - bob, (i < -1 || j < -2) ? C_GEMD : C_GEM);
    }
  px(cx - 1, cy - 3, C_WHT); px(cx, cy - 4, C_WHT);
}
static void drawBall(int cx, int cy) {
  shadow(cx, cy + 6, 5, 2);
  oCirc(cx, cy, 5, C_BALL, C_BLK);
  fRect(cx - 5, cy - 1, 10, 2, C_BLK);
  px(cx - 2, cy - 3, C_WHT);
}
static void drawHPBar(int sx, int sy, float hp, float maxHP, KDColor col, KDColor colD) {
  int w = 18;
  fRect(sx - w / 2 - 1, sy - 1, w + 2, 6, C_BLK);
  fRect(sx - w / 2, sy, w, 4, KDColor::RGB24(0x3A3A3A));
  int fill = (int)(w * hp / maxHP);
  fRect(sx - w / 2, sy, fill, 4, col);
  fRect(sx - w / 2, sy + 3, fill, 1, colD);
  for (int s = 4; s < w; s += 4) fRect(sx - w / 2 + s, sy, 1, 4, C_BLK);
}
static void drawEnt(Ent &e, int sx, int sy) {
  int myTeam = ents[humanIdx].team;
  bool teamRed = (e.team != myTeam);
  KDColor tCol = teamRed ? C_RED : C_BLUE;
  KDColor tColD = teamRed ? C_REDD : C_BLUED;
  bool flash = e.lastHitT > 0 && (e.lastHitT / 50) % 2 == 0;
  int bob = ((e.walkT / 40) % 2) * (e.dashT > 0 ? 2 : 1);
  int by = sy - bob;

  if (e.isPet) { // rendu des creatures invoquees
    shadow(sx, sy + 6, 6, 3);
    if (e.petKind == 0) { // ours de Nita
      fCirc(sx - 5, by - 9, 3, tColD); fCirc(sx + 5, by - 9, 3, tColD);
      fCirc(sx, by - 4, 8, flash ? C_WHT : tCol); fCirc(sx, by - 2, 8, tColD);
      fCirc(sx - 3, by - 6, 1, C_BLK); fCirc(sx + 3, by - 6, 1, C_BLK);
      fRect(sx - 2, by - 2, 4, 2, C_BLK);
    } else if (e.petKind == 1) { // tourelle (Jessie, Penny, 8-Bit)
      fRect(sx - 7, by, 14, 5, tColD);
      fRect(sx - 5, by - 6, 10, 7, flash ? C_WHT : tCol);
      fRect(sx + (int)(fcosf(e.aimAng) * 5) - 1, by - 3 + (int)(fsinf(e.aimAng) * 4), 5, 2, C_BLK);
      fCirc(sx - 3, by - 4, 1, C_YEL); fCirc(sx + 3, by - 4, 1, C_YEL);
    } else if (e.petKind == 2) { // station de soin de Pam
      int r = 7 + (animT / 200) % 2;
      fCirc(sx, by - 2, r, flash ? C_WHT : C_GREEN);
      fRect(sx - 1, by - 6, 2, 8, C_WHT); fRect(sx - 4, by - 3, 8, 2, C_WHT);
      fRing(sx, by - 2, r + 2 + (animT / 300) % 3, C_GREEN);
    } else if (e.petKind == 3) { // rats d'Ash
      fCirc(sx, by - 3, 5, flash ? C_WHT : C_GRE);
      fRect(sx + 3, by - 1, 4, 1, C_GRE); // queue
      fCirc(sx + 2, by - 5, 1, C_RED); fCirc(sx - 1, by - 5, 1, C_RED);
    } else if (e.petKind == 4) { // ego de Lola : mini clone aux couleurs du proprio
      drawBrawlerSprite(sx, by, 5, e.brawlerIdx, (int)e.faceAng, flash, e.walkT, e.attackT);
      fRing(sx, by, 7, tCol);
    } else if (e.petKind == 6) { // tete-mine de Tick
      fCirc(sx, by - 4, 7, flash ? C_WHT : tCol);
      fRect(sx - 3, by - 7, 6, 2, C_WHT);
      fCirc(sx - 3, by - 5, 1, C_BLK); fCirc(sx + 3, by - 5, 1, C_BLK);
      fRing(sx, by - 4, 9 + (animT / 300) % 2, C_RED); // clignote : danger
    } else if (e.petKind == 7) { // tourelle booster de 8-Bit : ne tire pas, aura de boost
      fRect(sx - 7, by, 14, 5, tColD);
      fRect(sx - 5, by - 6, 10, 7, flash ? C_WHT : tCol);
      fCirc(sx - 3, by - 4, 1, C_YEL); fCirc(sx + 3, by - 4, 1, C_YEL);
      fRing(sx, by - 2, 12 + (animT / 300) % 3, C_YEL); // aura de degats boosts
    } else { // pingouins de Mr. P
      fCirc(sx, by - 3, 6, flash ? C_WHT : tCol);
      fRect(sx - 2, by - 2, 4, 4, C_WHT); // ventre
      fCirc(sx + 1, by - 6, 1, C_BLK); fCirc(sx - 2, by - 6, 1, C_BLK);
    }
    if (e.petLife < 5 && (e.petLife * 2) - (int)(e.petLife * 2) < 0.5f) return; // clignote avant de disparaitre
    drawHPBar(sx, by - 16, e.hp, e.maxHP, e.team == ents[humanIdx].team ? C_GREEN : tCol, tColD);
    return;
  }

  // invisible (super de Leon / Sandy) : juste un scintillement
  if (e.stealthT > 0) {
    shadow(sx, sy + 7, 7, 3);
    if ((animT / 160) % 2 == 0) { fCirc(sx, sy - 2, 2, tCol); fCirc(sx + 5, sy - 5, 1, tCol); }
    return;
  }

  int br = e.isBoss ? 12 : 9; // V5.13 : persos plus gros en match (les details du sprite dessine main apparaissent a partir de r>=6)
  shadow(sx, sy + br, br, 3);
  gMechDraw = (e.brawlerIdx == 47 && e.mechT > 0); // Meg : mega robot pendant son super
  drawBrawlerSprite(sx, by, br, e.brawlerIdx, (int)e.faceAng, flash, e.walkT, e.attackT);
  gMechDraw = false;
  if (!e.isBoss) arc(sx, by, 10, 3.34f, 6.08f, 2, tCol); // V5.13 : anneau agrandi avec le perso
  int gx = (int)(fcosf(e.aimAng) * 9), gy = (int)(fsinf(e.aimAng) * 9);
  fCirc(sx + gx, by + gy, 2, C_GRE);
  int am = ammoMax(e.brawlerIdx); // V5.16 : Carl/Bea = 1 seule munition
  for (int a = 0; a < am; a++) {
    // V5.13b : munitions façon BS — barres plus LARGES sous le joueur, petites sous les autres
    int bw = (am == 1) ? (e.isHuman ? 22 : 12) : (e.isHuman ? 8 : 5), bh = e.isHuman ? 4 : 2;
    int gap = (am == 1) ? 0 : (e.isHuman ? 10 : 6), bx0 = (am == 1) ? (e.isHuman ? -11 : -6) : (e.isHuman ? -14 : -8);
    int filled = (int)(fmaxf(0, fminf(1, e.ammo - a)) * bw);
    KDColor barC = (e.brawlerIdx == 24 && e.beaCharge) ? C_RED : C_ORG; // V5.16 : Bea chargee = barre rouge
    fRect(sx + bx0 + a * gap, by + 9, bw, bh, C_BLK);
    fRect(sx + bx0 + a * gap, by + 9, filled, bh, barC);
  }
  if (e.superCharge >= 100) {
    int r = 10 + (animT / 150) % 2;
    fRing(sx, by, r, C_PUR);
    fCirc(sx + (int)(fcosf(animT * 0.01f) * r), by + (int)(fsinf(animT * 0.01f) * r), 1, C_YEL);
  } else if (e.superCharge > 0) {
    arc(sx, by, 10, -1.5708f, -1.5708f + 6.2832f * e.superCharge / 100.0f, 2, C_PUR);
  }
  drawHPBar(sx, by - 15, e.hp, e.maxHP, e.isHuman ? C_GREEN : tCol, tColD);
  // points de vie affiches au-dessus de la barre (comme dans Brawl Stars)
  {
    int hv = (int)(e.hp + 0.5f); if (hv < 0) hv = 0;
    int digits = 1, tmp = hv;
    while (tmp >= 10) { tmp /= 10; digits++; }
    drawNum(sx - digits * 6, by - 29, hv, e.isHuman ? C_YEL : C_WHT);
  }
  // jauge de charge de Hank : le ballon gonfle tant qu'on maintient l'attaque
  if (e.isHuman && e.atkCharge > 0) {
    fRect(sx - 11, by - 42, 22, 5, C_BLK);
    fRect(sx - 10, by - 41, (int)(20 * fminf(1.2f, e.atkCharge) / 1.2f), 3, C_ORG);
  }
  if (e.gems > 0) {
    fCirc(sx + 12, by - 12, 6, C_GREEN);
    drawNum(sx + 9, by - 14, e.gems, C_WHT);
  }
  if (mode == MODE_BOUNTY && e.stars > 0) {
    fCirc(sx - 12, by - 12, 6, C_GOLD);
    drawNum(sx - 15, by - 14, e.stars, C_BLK);
  }
  if (e.hasBall) oCirc(sx, by - 22, 4, C_BALL, C_BLK);
}
static void drawProj(Proj &p, int sx, int sy, int myTeam) {
  KDColor c = p.fromSuper ? C_PUR : (p.team == myTeam ? C_YEL : C_RED);
  KDColor c2 = p.fromSuper ? C_MAG : C_ORG;
  if (p.mineT > 0) { // mine posee : celle de Bo est INVISIBLE, celle de Tick clignote
    if (p.bIdx == 18) return; // mines de Bo : rien a l'ecran
    int blink = (p.mineT < 1.0f) ? 120 : 300;
    if ((animT / blink) % 2 == 0) oCirc(sx, sy, 3, C_YEL, C_BLK);
    else oCirc(sx, sy, 3, C_ORG, C_BLK);
    return;
  }
  // EFFET D'ARC : les projectiles lances (Barley, Dynamike...) montent puis retombent
  if (p.lob && p.range > 1) {
    float t = fminf(1.0f, p.travelled / p.range);
    sy -= (int)(fsinf(t * 3.14159f) * 14); // l'ombre au sol reste, le projectile decolle
  }
  // TRAINEE : 3 pixels qui s'estompent derriere le projectile
  if (!p.lob) {
    for (int tr = 1; tr <= 3; tr++) {
      KDColor tc = (tr == 3) ? KDColor::RGB24(0x444444) : (tr == 2 ? c : c2);
      px(sx - (int)(p.vx * 0.007f * tr), sy - (int)(p.vy * 0.007f * tr), tc);
    }
  }
  // ---- V5.13 : chaque brawler a le graphisme de SON projectile ----
  // dx/dy : direction discrete (-2..2) pour orienter les sprites sans trigo
  int dx = p.vx > 150 ? 2 : (p.vx < -150 ? -2 : (p.vx > 40 ? 1 : (p.vx < -40 ? -1 : 0)));
  int dy = p.vy > 150 ? 2 : (p.vy < -150 ? -2 : (p.vy > 40 ? 1 : (p.vy < -40 ? -1 : 0)));
  int blink = (animT / 60) % 2; // clignotement (flammes, etincelles)
  switch (p.bIdx) {
    case 0: case 14: // Shelly, Darryl : plombs de fusil a pompe (3 billes oranges)
      fRect(sx - 1, sy, 2, 2, C_ORG); px(sx - 2, sy - 1, C_YEL); px(sx + 2, sy + 1, C_YEL);
      break;
    case 2: case 13: // Colt, Rico : BALLES (balle bande, pointe blanche)
      fRect(sx - 1, sy - 1, 3, 3, C_YEL);
      px(sx - dx, sy - dy, C_ORG); px(sx + dx, sy + dy, C_WHT);
      if (p.fromSuper) { // ULTI : balles derriere balles (train de balles)
        fRect(sx - dx * 4 - 1, sy - dy * 4 - 1, 2, 2, C_YEL);
        fRect(sx - dx * 8, sy - dy * 8, 2, 2, C_ORG);
      }
      break;
    case 1: // Nita : griffes de bear brun
      px(sx - 1, sy - 1, KDColor::RGB24(0x8A5C22)); px(sx + 1, sy, KDColor::RGB24(0xA07030)); px(sx, sy + 1, KDColor::RGB24(0x8A5C22));
      break;
    case 4: // Brock : FUSEE rouge avec flamme et fumee
      fCirc(sx, sy, 2, C_RED); px(sx + dx, sy + dy, C_ORG);
      px(sx - dx * 2, sy - dy * 2, blink ? C_YEL : C_ORG); px(sx - dx * 4, sy - dy * 4, C_GRE);
      break;
    case 6: // Barley : BOUTEILLE verte avec goulot
      fRect(sx, sy - 1, 2, 3, C_GREEN); px(sx, sy - 2, C_WHT); px(sx + 1, sy - 2, C_WHT);
      break;
    case 7: // Poco : ONDE DE SON magenta — attaque de ZONE, le projectile est une onde large (V5.16)
      { int wx2 = -dy, wy2 = dx;
        fCirc(sx, sy, 3, C_MAG);
        px(sx + wx2 * 4, sy + wy2 * 4, C_PUR); px(sx - wx2 * 4, sy - wy2 * 4, C_PUR);
        px(sx + wx2 * 6, sy + wy2 * 6, C_MAG); px(sx - wx2 * 6, sy - wy2 * 6, C_MAG);
        px(sx + wx2 * 8, sy + wy2 * 8, KDColor::RGB24(0x602070)); px(sx - wx2 * 8, sy - wy2 * 8, KDColor::RGB24(0x602070)); }
      break;
    case 9: // Jessie : boule d'energie cyan avec etincelle
      fCirc(sx, sy, 2, C_CYN); px(sx - 1, sy - 1, C_WHT);
      break;
    case 10: // Dynamike : BATON de dynamite rouge + meche
      fRect(sx, sy - 1, 2, 3, C_RED); px(sx - 1, sy - 2, blink ? C_YEL : C_ORG);
      break;
    case 11: // Tick : tete grise avec antenne
      fCirc(sx, sy, 2, C_GRE); px(sx, sy - 3, C_YEL); px(sx - 1, sy, C_BLK);
      break;
    case 12: // 8-Bit : pixel d'arcade jaune/bleu
      fRect(sx - 1, sy - 1, 3, 3, C_YEL); px(sx, sy, C_BLK); px(sx + 1, sy - 1, C_MAG);
      break;
    case 15: // Penny : piece d'or
      fRect(sx - 1, sy - 1, 2, 2, C_GOLD); px(sx, sy, C_ORG);
      break;
    case 16: // Carl : PIC de mine (manche + tete grise), boomerang
      fRect(sx - 1, sy, 3, 1, KDColor::RGB24(0x8A5C22));
      fCirc(sx + dx * 2, sy + dy * 2, 2, C_GRE); px(sx + dx * 2, sy + dy * 2, C_WHT);
      break;
    case 18: // Bo : FLECHE blanche a pointe
      px(sx + dx, sy + dy, C_WHT); px(sx, sy, C_WHT); px(sx - dx, sy - dy, C_ORG);
      break;
    case 19: // Emz : NUAGE DE SPRAY rose — zone large (V5.16)
      { int wx2 = -dy, wy2 = dx;
        fCirc(sx, sy, 3, KDColor::RGB24(0xFF7AB8));
        px(sx + wx2 * 5, sy + wy2 * 5, C_MAG); px(sx - wx2 * 5, sy - wy2 * 5, C_MAG);
        px(sx + wx2 * 7, sy + wy2 * 7, KDColor::RGB24(0xD05090)); px(sx - wx2 * 7, sy - wy2 * 7, KDColor::RGB24(0xD05090)); }
      break;
    case 20: // Stu : flamme de booster cyan
      fCirc(sx, sy, 2, C_CYN); px(sx - dx, sy - dy, blink ? C_WHT : C_CYN);
      break;
    case 21: // Pam : boule de soin orange avec croix
      fCirc(sx, sy, 2, C_ORG); px(sx, sy - 1, C_WHT); px(sx, sy + 1, C_WHT);
      break;
    case 22: // Frank : ONDE DE MARTEAU bleu-gris — attaque de ZONE large (V5.16)
      { int wx2 = -dy, wy2 = dx;
        fCirc(sx, sy, 3, KDColor::RGB24(0x8A9Bb0));
        px(sx + wx2 * 4, sy + wy2 * 4, KDColor::RGB24(0x6A829A)); px(sx - wx2 * 4, sy - wy2 * 4, KDColor::RGB24(0x6A829A));
        px(sx + wx2 * 7, sy + wy2 * 7, KDColor::RGB24(0x506880)); px(sx - wx2 * 7, sy - wy2 * 7, KDColor::RGB24(0x506880)); }
      break;
    case 24: // Bea : ABEILLE rayee noir/jaune
      fRect(sx - 1, sy, 2, 2, C_YEL); px(sx, sy, C_BLK); px(sx + 1, sy - 2, C_WHT);
      break;
    case 25: // Nani : mini-drone magenta
      fRect(sx - 1, sy - 1, 3, 3, C_MAG); px(sx, sy, C_WHT);
      break;
    case 27: // Griff : pieces lancees (or empile)
      fRect(sx - 1, sy - 1, 2, 2, C_GOLD); fRect(sx, sy + 1, 2, 1, C_ORG); px(sx + 2, sy - 2, C_GOLD);
      break;
    case 28: // Gale : boule de neige bleue + flocon
      fCirc(sx, sy, 2, C_BLUED); px(sx - 1, sy - 1, C_WHT); px(sx + 1, sy + 1, C_WHT);
      break;
    case 30: // Belle : marqueur magenta electrique
      fRect(sx, sy - 1, 2, 3, C_MAG); px(sx - dx, sy - dy, C_YEL);
      break;
    case 31: // Buzz : eclair de taser jaune
      px(sx, sy - 1, C_YEL); px(sx + 1, sy, C_YEL); px(sx, sy + 1, C_YEL); px(sx - 1, sy, blink ? C_WHT : C_ORG);
      break;
    case 36: // Squeak : boule verte avec yeux
      fCirc(sx, sy, 2, C_GREEN); px(sx - 1, sy, C_BLK); px(sx + 1, sy, C_BLK);
      break;
    case 37: // Hank : BULLE qui grossit en avancant puis eclate (V5.17)
      { int br = (p.radius > 3) ? p.radius : 3;
        fCirc(sx, sy, br, p.team == myTeam ? C_BLUED : KDColor::RGB24(0xA03040));
        fCirc(sx - br / 3, sy - br / 3, br > 4 ? br / 3 : 1, C_WHT); } // reflet
      break;
    case 38: // Pearl : perle blanche brillante
      fCirc(sx, sy, 2, C_WHT); px(sx - 1, sy - 1, KDColor::RGB24(0xFFE8C0));
      break;
    case 39: // Spike : AIGUILLE de cactus verte
      fRect(sx - 1, sy, 3, 1, C_GREEN); px(sx + dx, sy + dy, C_YEL);
      break;
    case 40: // Crow : DAGUE empoisonnee violette
      px(sx + dx, sy + dy, C_MAG); px(sx, sy, C_PUR); px(sx - dx, sy - dy, C_POISON);
      break;
    case 41: // Leon : SHURIKEN vert a 4 branches
      fCirc(sx, sy, 1, C_GREEN); px(sx + 2, sy, C_GREEN); px(sx - 2, sy, C_GREEN); px(sx, sy + 2, C_GREEN); px(sx, sy - 2, C_GREEN);
      break;
    case 43: // Amber : FLAMME orange/jaune clignotante
      fCirc(sx, sy, 2, C_ORG); px(sx, sy, blink ? C_YEL : C_WHT); px(sx - dx, sy - dy, blink ? C_YEL : C_ORG);
      break;
    case 44: // Gene : boule de fumee bleue avec remuage
      fCirc(sx, sy, 2, C_BLUE); px(sx - 1, sy + 1, C_MAG); px(sx + 1, sy - 1, C_CYN);
      break;
    case 46: // Byron : fiole de poison
      fRect(sx, sy - 1, 2, 2, C_GREEN); px(sx, sy - 2, C_WHT); px(sx - dx, sy - dy, C_POISON);
      break;
    case 47: // Meg : petit laser cyan
      fRect(sx - 1, sy, 3, 1, C_CYN); px(sx + dx, sy + dy, C_WHT);
      break;
    case 48: // Chester : clochette jaune
      fRect(sx - 1, sy - 1, 3, 2, C_YEL); px(sx, sy + 1, C_ORG); px(sx, sy - 2, C_GRE);
      break;
    case 49: // Surge : boule electrique bleue avec etincelles
      fCirc(sx, sy, 2, C_BLUE); px(sx + 2, sy - 1, blink ? C_WHT : C_CYN); px(sx - 1, sy + 2, blink ? C_CYN : C_WHT);
      break;
    case 50: // Piper : balle de sniper blanche + longue trainee rose
      fRect(sx - 1, sy - 1, 3, 2, C_WHT); px(sx + dx, sy + dy, C_MAG);
      px(sx - dx * 2, sy - dy * 2, C_MAG); px(sx - dx * 3, sy - dy * 3, KDColor::RGB24(0xFF9AC8));
      break;
    case 51: // Max : eclair cyan rapide
      px(sx, sy, C_CYN); px(sx - dx, sy - dy, C_WHT); px(sx - dx * 2, sy - dy * 2, C_BLUED);
      break;
    case 52: // Tara : CARTE de tarot violette avec liseré blanc
      fRect(sx - 1, sy - 1, 2, 3, C_PUR); px(sx - 1, sy - 1, C_WHT); px(sx, sy + 1, C_MAG);
      break;
    case 53: // Sprout : boule de graine verte avec feuille
      fCirc(sx, sy, 2, C_GREEN); px(sx, sy - 2, C_YEL); px(sx + 1, sy - 2, C_GREEN);
      break;
    case 54: // Mr. P : VALISE marron avec poignee
      fRect(sx - 1, sy - 1, 3, 2, KDColor::RGB24(0x8A5C22)); px(sx, sy - 2, C_GRE);
      break;
    case 3: case 5: case 17: case 23: case 26: case 34: case 45: // corps-a-corps (Bull, El Primo, Jacky, Bibi, Edgar, Fang, Mortis) : poing/arme beige
      fCirc(sx, sy, 2, KDColor::RGB24(0xE0A878)); px(sx + dx, sy + dy, C_ORG);
      break;
    default: { // autres brawlers : projectile aux couleurs du brawler qui l'a tire
      uint32_t bc = BRAWLERS[p.bIdx < 55 ? p.bIdx : 0].colTop;
      if (p.shape == 0) oCirc(sx, sy, p.radius + 1, KDColor::RGB24(bc), C_BLK);
      else if (p.shape == 1) { fRect(sx - 1, sy - 1, 2, 2, KDColor::RGB24(bc)); px(sx + dx, sy + dy, c2); }
      else if (p.shape == 2) { oCirc(sx, sy, p.radius + 1, KDColor::RGB24(bc), C_BLK); px(sx - dx, sy - dy, C_ORG); px(sx - dx * 2, sy - dy * 2, C_YEL); }
      else { fCirc(sx, sy, p.radius + 1, KDColor::RGB24(bc)); px(sx + (p.spin / 8) % 3 - 1, sy - (p.spin / 16) % 2, C_BLK); }
      break;
    }
  }
}

static void renderMatch() {
  const MapTheme &T = THEMES[mode];
  Ent &me = ents[humanIdx];
  int camX = (int)me.x - SW / 2, camY = (int)me.y - SH / 2;
  camX = fmaxf(0, fminf(mapW * TILE - SW, camX));
  camY = fmaxf(0, fminf(mapH * TILE - SH, camY));
  if (shakeT > 0) { // V5.16 : shake DIRECTIONNEL FLUIDE — decay lineaire, plus de tremblement aleatoire
    float k = shakeT / 400.0f;
    camX += (int)(shakeDX * shakeMag * k); camY += (int)(shakeDY * shakeMag * k);
  }

  for (int ty = camY / TILE; ty <= (camY + SH) / TILE && ty < mapH; ty++) {
    int sy = ty * TILE - camY;
    // perf : fond en 2 pushes par ligne de tuiles au lieu de 23
    fRect(0, sy, SW, TILE, KDColor::RGB24(seasonTint(T.gr1)));
    for (int tx = camX / TILE; tx <= (camX + SW) / TILE && tx < mapW; tx++)
      if ((tx + ty) & 1) {
        int sx = tx * TILE - camX;
        fRect(sx, sy, TILE, TILE, KDColor::RGB24(seasonTint(T.gr2)));
      }
    for (int tx = camX / TILE; tx <= (camX + SW) / TILE && tx < mapW; tx++) {
      int sx = tx * TILE - camX;
      // V5.12 perf : decor uniquement sur tuiles vides (plus de dessin gaspille sous murs/buissons)
      if (tileMap[tx][ty] == 1) drawWallThemed(sx, sy);
      else if (tileMap[tx][ty] == 2) drawBushThemed(sx, sy);
      else if (tileMap[tx][ty] == 0) drawGroundDecor(sx, sy, tx, ty);
    }
  }

  if (mode == MODE_BALL)
    for (int ty = mapH / 2 - 2; ty <= mapH / 2 + 2; ty++) {
      fRect(2 - camX, ty * TILE - camY + 2, TILE - 4, TILE - 4, C_BLUED);
      fRect(mapW * TILE - TILE - camX + 2, ty * TILE - camY + 2, TILE - 4, TILE - 4, C_REDD);
    }

  if (mode == MODE_HEIST)
    for (int s = 0; s < MAX_SAFE; s++) {
      if (safes[s].maxHP <= 0) continue;
      int sx = (int)safes[s].x - camX, sy = (int)safes[s].y - camY;
      shadow(sx, sy + 14, 14, 4);
      fRect(sx - 13, sy - 13, 26, 26, safes[s].team == 0 ? C_BLUED : C_REDD);
      fRect(sx - 11, sy - 11, 22, 22, safes[s].team == 0 ? C_BLUE : C_RED);
      fRect(sx - 4, sy - 3, 8, 6, C_YEL);
      fRect(sx - 12, sy - 18, 24, 5, C_BLK);
      fRect(sx - 11, sy - 17, (int)(22 * safes[s].hp / safes[s].maxHP), 3, C_GOLD);
    }

  // ---- V5.9 : teleporteurs, boites de cubes, cubes au sol, tonneaux ----
  for (int t = 0; t < MAX_TP; t++) { // anneaux animes (paires cyan / magenta)
    if (!portals[t].active) continue;
    int sx = (int)portals[t].x - camX, sy = (int)portals[t].y - camY;
    KDColor pc = (t < 2) ? C_CYN : C_MAG;
    fRing(sx, sy, 9 + (animT / 250 + t) % 3, pc);
    fRing(sx, sy, 13, C_BLK);
    fRect(sx - 2, sy - 2, 4, 4, pc);
  }
  for (int b = 0; b < MAX_BOX; b++) { // caisses en bois remplies de cubes
    if (!boxes[b].active) continue;
    int sx = (int)boxes[b].x - camX, sy = (int)boxes[b].y - camY;
    shadow(sx, sy + 8, 8, 2);
    fRect(sx - 8, sy - 8, 16, 16, KDColor::RGB24(0x8A6A3A));
    fRect(sx - 8, sy - 8, 16, 2, KDColor::RGB24(0xB08A50));
    fRect(sx - 8, sy - 2, 16, 2, KDColor::RGB24(0x5C4A2A));
    fRect(sx - 2, sy - 8, 2, 16, KDColor::RGB24(0x5C4A2A));
    if (boxes[b].hp < 3000) { fRect(sx - 6, sy - 6, 4, 3, C_BLK); fRect(sx + 2, sy + 2, 3, 4, C_BLK); }
  }
  for (int c = 0; c < MAX_CUBE; c++) { // power cubes violet scintillants
    if (!cubes[c].active) continue;
    int sx = (int)cubes[c].x - camX, sy = (int)cubes[c].y - camY;
    KDColor cc = ((animT / 300) % 2) ? KDColor::RGB24(0xB44BFF) : KDColor::RGB24(0x8A2BC9);
    fRect(sx - 3, sy - 3, 6, 6, cc);
    fRect(sx - 2, sy - 4, 4, 1, KDColor::RGB24(0xE0B0FF));
  }
  for (int b = 0; b < MAX_BARREL; b++) { // tonneaux rouges explosifs
    if (!barrels[b].active) continue;
    int sx = (int)barrels[b].x - camX, sy = (int)barrels[b].y - camY;
    shadow(sx, sy + 7, 7, 2);
    fRect(sx - 6, sy - 7, 12, 14, KDColor::RGB24(0xC23428));
    fRect(sx - 6, sy - 4, 12, 2, C_GOLD);
    fRect(sx - 6, sy + 2, 12, 2, C_GOLD);
    fRect(sx - 2, sy - 9, 4, 2, KDColor::RGB24(0x2E2424));
  }

  if (mode == MODE_ZONE)
    for (int z = 0; z < MAX_ZONE; z++) {
      int sx = (int)zones[z].x - camX, sy = (int)zones[z].y - camY;
      // V5.17 : comme dans le vrai Brawl Stars — l'anneau est scinde en 2 MOITIES :
      // moitie GAUCHE = progression de TON equipe, moitie DROITE = celle de l'ennemi
      fRing(sx, sy, 24, KDColor::RGB24(0x3A3A4A)); fRing(sx, sy, 25, C_BLK); // anneau de base
      float pr = fabsf(zones[z].prog);
      if (zones[z].prog >= 0) // ton equipe capture : l'aiguille balaie la moitie gauche
        arc(sx, sy, 25, 1.5708f, 1.5708f + 3.1416f * pr / 100.0f, 3, (pr > 66) ? C_GREEN : C_BLUE);
      else // l'ennemi capture : la sienne balaie la moitie droite
        arc(sx, sy, 25, -1.5708f, -1.5708f + 3.1416f * pr / 100.0f, 3, C_RED);
      fRing(sx, sy, 20, C_BTND); // interieur sombre
      if (pr > 3) { // pourcentage au centre, colore selon qui mene
        KDColor pc = (zones[z].prog >= 0) ? ((pr > 66) ? C_GREEN : C_BLUE) : C_RED;
        drawNum(sx - 10, sy - 4, (int)pr, pc);
        drawText(sx + 6, sy - 4, "%", pc);
      }
    }

  if (mode == MODE_SOLO || mode == MODE_DUO || mode == MODE_KO) {
    int cx = mapW * TILE / 2 - camX, cy = mapH * TILE / 2 - camY;
    for (int a = 0; a < 360; a += 6) {
      int ex = cx + (int)(soloShrink * fcosf(a * 0.01745f));
      int ey = cy + (int)(soloShrink * fsinf(a * 0.01745f));
      fRect(ex - 2, ey - 2, 5, 5, C_POISON);
      if ((a / 6 + animT / 200) % 4 == 0) fRect(ex - 3, ey - 3, 7, 7, C_POIS2);
    }
  }

  if (mode == MODE_GEM)
    for (int g = 0; g < 10; g++)
      if (gems[g].active) drawGem((int)gems[g].x - camX, (int)gems[g].y - camY, animT + g * 300);
  if (mode == MODE_BALL) drawBall((int)ballX - camX, (int)ballY - camY);

  for (int pass = 0; pass < entCount; pass++) {
    int best = -1, bestY = 1 << 30;
    for (int i = 0; i < entCount; i++) {
      Ent &e = ents[i];
      if (!e.alive || e.drawDone) continue;
      if ((int)e.y < bestY) { bestY = (int)e.y; best = i; }
    }
    if (best < 0) break;
    ents[best].drawDone = true;
    Ent &e = ents[best];
    int sx = (int)e.x - camX, sy2 = (int)e.y - camY;
    if (best != humanIdx && inBush(e.x, e.y)) {
      float dx = e.x - me.x, dy = e.y - me.y;
      if (dx * dx + dy * dy > 60 * 60) continue;
    }
    if (best == humanIdx && acc.name[0]) // pseudo au-dessus du joueur (V5.11)
      drawText(sx - textW(acc.name) / 2, sy2 - 26, acc.name, C_GREEN);
    drawEnt(e, sx, sy2);
  }
  for (int i = 0; i < entCount; i++) ents[i].drawDone = false;

  // zones au sol (flaques de Barley)
  for (int i = 0; i < 8; i++) {
    FireZone &z = fireZones[i];
    if (!z.active) continue;
    int zx = (int)z.x - camX, zy = (int)z.y - camY;
    if (zx < -50 || zx > SW + 50 || zy < -50 || zy > SH + 50) continue;
    KDColor fc = (z.team == ents[humanIdx].team) ? C_YEL : C_ORG;
    oCirc(zx, zy, (int)z.r, KDColor::RGB24(0x9A4A00), KDColor::RGB24(0x6A3200)); // flaque orange foncee (taille reelle)
    if ((animT / 200) % 2 == 0) fCirc(zx + rnd((int)z.r) - (int)z.r / 2, zy + rnd((int)z.r) - (int)z.r / 2, 2, fc); // flammes
  }

  for (int i = 0; i < MAX_PROJ; i++)
    if (projs[i].active) {
      if (projs[i].mineT > 0 && ents[projs[i].owner].brawlerIdx == 18) continue; // mine de Bo : invisible
      shadow((int)projs[i].x - camX, (int)projs[i].y - camY + 6, 2, 1);
      drawProj(projs[i], (int)projs[i].x - camX, (int)projs[i].y - camY, ents[humanIdx].team);
    }

  for (int i = 0; i < MAX_POP; i++)
    if (pops[i].active) {
      int sx = (int)pops[i].x - camX - 6, sy = (int)pops[i].y - camY;
      if (pops[i].kind == 1) { // CRIT : jaune, double trace = gras
        drawNum(sx - 1, sy, pops[i].val, C_YEL); drawNum(sx + 1, sy, pops[i].val, C_YEL);
      } else if (pops[i].kind == 2) drawNum(sx, sy, pops[i].val, C_GREEN); // SOIN : vert
      else drawNum(sx, sy, pops[i].val, C_WHT);
    }

  // particules
  for (int i = 0; i < MAX_PART; i++)
    if (parts[i].active) {
      int sx = (int)parts[i].x - camX, sy = (int)parts[i].y - camY;
      if (parts[i].kind == 0) { px(sx, sy, C_WHT); px(sx + 1, sy, C_YEL); }
      else { px(sx, sy, C_ORG); px(sx - 1, sy + 1, C_RED); px(sx + 1, sy - 1, C_YEL); }
    }

  // killfeed (coin haut droit, plus de HUD au-dessus depuis la V5.13)
  for (int i = 0; i < KILLFEED; i++)
    if (killfeed[i].alive) {
      int y = 4 + i * 14;
      fRect(SW - 118, y, 118, 12, C_BLK);
      drawBrawlerSprite(SW - 110, y + 6, 4, killfeed[i].killer, 90, false);
      if (killfeed[i].killer == ents[humanIdx].brawlerIdx && acc.name[0]) // ton pseudo pour tes kills (V5.11)
        drawText(SW - 102, y + 1, acc.name, C_GREEN);
      else drawText(SW - 102, y + 1, "A TUE", C_GRE);
      drawBrawlerSprite(SW - 32, y + 6, 4, killfeed[i].victim, 90, false);
    }

  // ---- Minimap (coin bas droit) ----
  {
    int mw = 64, mh = 48, mx = SW - mw - 2, my = SH - mh - 14;
    fRect(mx - 1, my - 1, mw + 2, mh + 2, C_BLK);
    fRect(mx, my, mw, mh, KDColor::RGB24(0x1E2A38));
    // murs en runs (1 push par ligne au lieu de ~200 pushes par pixel)
    for (int ty = 0; ty < mapH; ty += 2) {
      rowClear(mx, mx + mw - 1);
      for (int tx = 0; tx < mapW; tx += 2)
        if (tileMap[tx][ty] == 1) rowPut(mx + tx * mw / mapW, C_GRE);
      rowFlush(my + ty * mh / mapH, mx, mx + mw - 1);
    }
    if (mode == MODE_GEM)
      for (int g = 0; g < 10; g++)
        if (gems[g].active) px(mx + (int)(gems[g].x * mw / (mapW * TILE)), my + (int)(gems[g].y * mh / (mapH * TILE)), C_GEM);
    if (mode == MODE_BALL) px(mx + (int)(ballX * mw / (mapW * TILE)), my + (int)(ballY * mh / (mapH * TILE)), C_WHT);
    if (mode == MODE_HEIST)
      for (int s = 0; s < MAX_SAFE; s++)
        if (safes[s].maxHP > 0) px(mx + (int)(safes[s].x * mw / (mapW * TILE)), my + (int)(safes[s].y * mh / (mapH * TILE)), C_GOLD);
    // V5.11 : objets de la map visibles sur la minimap
    for (int b = 0; b < MAX_BARREL; b++)
      if (barrels[b].active) px(mx + (int)(barrels[b].x * mw / (mapW * TILE)), my + (int)(barrels[b].y * mh / (mapH * TILE)), C_ORG);
    for (int t = 0; t < MAX_TP; t++)
      if (portals[t].active) px(mx + (int)(portals[t].x * mw / (mapW * TILE)), my + (int)(portals[t].y * mh / (mapH * TILE)), (t < 2) ? C_CYN : C_MAG);
    for (int b = 0; b < MAX_BOX; b++)
      if (boxes[b].active) px(mx + (int)(boxes[b].x * mw / (mapW * TILE)), my + (int)(boxes[b].y * mh / (mapH * TILE)), KDColor::RGB24(0xB08A50));
    for (int c = 0; c < MAX_CUBE; c++)
      if (cubes[c].active) px(mx + (int)(cubes[c].x * mw / (mapW * TILE)), my + (int)(cubes[c].y * mh / (mapH * TILE)), C_PUR);
    for (int i = 0; i < entCount; i++)
      if (ents[i].alive) {
        KDColor c = (i == humanIdx) ? C_GREEN : (ents[i].team == me.team ? C_BLUE : C_RED);
        px(mx + (int)(ents[i].x * mw / (mapW * TILE)), my + (int)(ents[i].y * mh / (mapH * TILE)), c);
        if (i == humanIdx) { px(mx + (int)(ents[i].x * mw / (mapW * TILE)) + 1, my + (int)(ents[i].y * mh / (mapH * TILE)), c); }
      }
  }

  // V5.13 : plus de barre de HUD en haut (elle faisait mal aux yeux) :
  // on garde UNIQUEMENT le killfeed (haut droit) et les objectifs du mode (bas droite).
  // L'ulti prete = petit badge "S" jaune clignotant a cote du perso.
  if (me.superCharge >= 100 && (animT / 200) % 2 == 0) {
    int px2 = (int)me.x - camX + 12, py2 = (int)me.y - camY - 16;
    fRect(px2 - 1, py2 - 4, 8, 8, C_YEL);
    drawText(px2, py2 - 3, "S", C_BLK);
  }

  // ---- V5.11 : bandeau "VAGUE N" au lancement d'une vague (mode SURVIE) ----
  if (mode == MODE_SURVIE && gWaveBannerT > 0) {
    gWaveBannerT -= 33;
    fRect(SW / 2 - 60, 84, 120, 22, C_BLK);
    fRect(SW / 2 - 58, 86, 116, 18, C_PANEL);
    drawTextC(92, "VAGUE", C_YEL);
    drawNum(SW / 2 + 18, 92, waveN, C_WHT);
  }

  // ---- V5.16 : les 5 dernieres secondes du compte a rebours GEMMES : bord d'ecran PULSANT violet ----
  if (mode == MODE_GEM && gemCountdown > 0 && gemCountdown <= 5000 && (animT / 250) % 2 == 0) {
    fRect(0, 0, SW, 3, C_PUR); fRect(0, SH - 3, SW, 3, C_PUR);
    fRect(0, 0, 3, SH, C_PUR); fRect(SW - 3, 0, 3, SH, C_PUR);
  }

  // ---- V5.13b : AVEUGLE (super de Jacky) : ecran assombri en bandes pour le joueur ----
  if (me.blindT > 0)
    for (int yy = 0; yy < SH; yy += 3) fRect(0, yy, SW, 1, C_BLK);

  // ---- V5.14 : particules de SAISON en match (feuilles en automne, flocons en neige) ----
  if (seasonIdx()) {
    KDColor sc1 = (seasonIdx() == 2) ? C_WHT : C_ORG;
    KDColor sc2 = (seasonIdx() == 2) ? KDColor::RGB24(0xC8DCF0) : KDColor::RGB24(0xA05020);
    int fall = (seasonIdx() == 2) ? 14 : 9; // la neige tombe plus vite
    for (int i = 0; i < 12; i++) {
      int fx = (i * 71 + animT / (seasonIdx() == 2 ? 30 : 55) + i * i * 3) % SW;
      int fy = (i * 97 + animT / fall) % SH;
      px(fx, fy, sc1); px(fx + 1, fy + 1, sc2);
    }
  }

  // ---- V5.17 : bulle de CHAT des bots (rare, anti-spam integre au declencheur) ----
  if (gChatT > 0 && gChatEnt >= 0 && gChatEnt < entCount && ents[gChatEnt].alive) {
    static const char *chatMsgs[2] = {"AIE !", "NON !"};
    int ex = (int)ents[gChatEnt].x - camX, ey = (int)ents[gChatEnt].y - camY - 30;
    const char *cm = chatMsgs[gChatIdx];
    rRect(ex - textW(cm) / 2 - 5, ey - 12, textW(cm) + 10, 15, 4, C_WHT);
    fRect(ex - 2, ey + 3, 4, 4, C_WHT);
    drawTextC(ey - 9, cm, C_BLK);
  }

  // V5.17 : Hank — la bulle GONFLE devant lui pendant qu'il maintient l'attaque (comme le vrai Hank :
  // plus on maintient, plus l'explosion est mortelle), puis elle ECLATE en zone sur place
  if (me.alive && me.brawlerIdx == 37 && me.atkCharge > 0.1f) {
    int br = 5 + (int)(fminf(1.2f, me.atkCharge) * 22); // V5.17 : bulle bien plus grosse
    int hx = (int)me.x - camX + (int)(fcosf(me.aimAng) * 14);
    int hy = (int)me.y - camY + (int)(fsinf(me.aimAng) * 14);
    fCirc(hx, hy, br, C_BLUED);
    fCirc(hx - br / 3, hy - br / 3, br > 5 ? br / 3 : 1, C_WHT); // reflet
    fRing(hx, hy, br + 1, C_WHT); // contour de ballon
  }

  // V5.16 : ARC DE VISEE en pointilles pour les lanceurs (ATK_LOB) pendant l'attaque
  if (me.alive && BRAWLERS[me.brawlerIdx].atk == ATK_LOB && keyOK) {
    float rng = RNG(BRAWLERS[me.brawlerIdx].range);
    for (int i = 1; i <= 8; i++) {
      if (((i + animT / 100) & 1)) continue; // pointilles qui defilent
      float dd = rng * i / 8.0f;
      int ax = (int)(me.x + fcosf(me.aimAng) * dd) - camX;
      int ay = (int)(me.y + fsinf(me.aimAng) * dd) - camY - (int)(fsinf((float)i / 8.0f * 3.14159f) * 14.0f);
      if (ax > 0 && ax < SW && ay > 0 && ay < SH) { px(ax, ay, C_WHT); px(ax, ay + 1, C_GRE); }
    }
  }

  // ---- V5.10 : bulle d'emote au-dessus du joueur (touche + ou -) ----
  if (gEmoteT > 0 && ents[humanIdx].alive) {
    int ex = (int)ents[humanIdx].x - camX, ey = (int)ents[humanIdx].y - camY - 28;
    rRect(ex - 13, ey - 11, 26, 22, 7, C_WHT);
    fRect(ex - 3, ey + 10, 6, 4, C_WHT);
    drawEmoteIcon(ex, ey, gEmoteKind);
  }

  // ---- HUD Siege : barre de vie geante du MEGA ROBOT ----
  if (mode == MODE_SIEGE || mode == MODE_BOSSRUSH)
    for (int i = 0; i < entCount; i++) {
      Ent &b = ents[i];
      if (!b.alive || !b.isBoss) continue;
      fRect(SW / 2 - 100, 28, 200, 10, C_BLK);
      fRect(SW / 2 - 99, 30, (int)(198 * b.hp / b.maxHP), 6, C_REDD);
      fRect(SW / 2 - 99, 30, (int)(198 * b.hp / b.maxHP), 2, KDColor::RGB24(0xE8444E));
      drawTextC(40, (mode == MODE_BOSSRUSH) ? ((gBossN >= 2) ? "MEGA ROBOT 3/3" : ((gBossN == 1) ? "MEGA ROBOT 2/3" : "MEGA ROBOT 1/3")) : "MEGA ROBOT", C_WHT);
    }
  // V5.16 : bannieres "ELIMINATOIRE !" (A-MORT) et "BOSS SUIVANT !" (Boss Rush)
  if (gKOBannerT > 0) {
    if (mode == MODE_BOSSRUSH) drawTextC(50, (gBossN >= 2) ? "BOSS FINAL !" : "BOSS SUIVANT !", C_YEL);
    else drawTextC(50, "ELIMINATOIRE !", C_RED);
  }

  // compte a rebours 3-2-1-GO au lancement du match
  if (startCountdown > -800) {
    int n = (startCountdown + 999) / 1000;
    fRect(SW / 2 - 60, 84, 120, 60, C_BLK);
    fRect(SW / 2 - 58, 86, 116, 56, C_PANEL);
    if (n > 0) drawNum(SW / 2 - 12, 104, n, C_YEL);
    else drawTextC(104, "GO!", C_GREEN);
  }
  // ecran de reaparition du joueur (en solo/duo/KO sans respawn : on regarde son equipe jouer)
  if (!me.alive) {
    bool ffaMode = (mode == MODE_SOLO || mode == MODE_DUO || mode == MODE_KO);
    fRect(SW / 2 - 96, 90, 192, 44, C_BLK);
    fRect(SW / 2 - 94, 92, 188, 40, C_PANEL);
    if (ffaMode) {
      drawTextC(96, "ELIMINE!", C_RED);
      drawTextC(112, "TON COEQUIPIER JOUE...", C_GRE);
    } else {
      drawTextC(96, "REAPPARITION", C_WHT);
      drawNum(SW / 2 - 12, 114, (me.respawnT + 999) / 1000, C_YEL);
    }
  }
  // bandeau bas : rappel des touches a gauche, score du mode a droite
  fRect(0, SH - 14, SW, 14, KDColor::RGB24(0x111827));
  drawText(4, SH - 12, "OK:ATK RET:ULTI HOME:QUIT", C_GRE);
  if (gFpsShow) drawNum(SW / 2 - 24, SH - 12, (int)(1000.0f / fmaxf(1.0f, gFrameMs)), C_GRE); // V5.16 : compteur FPS discret
  if (mode == MODE_GEM) {
    drawNum(SW - 72, SH - 12, teamGems[0], C_BLUE);
    drawText(SW - 48, SH - 12, "-", C_WHT);
    drawNum(SW - 38, SH - 12, teamGems[1], C_RED);
  } else if (mode == MODE_BALL) {
    int g0 = 0, g1 = 0;
    for (int i = 0; i < entCount; i++)
      if (ents[i].team == 0) g0 = fmaxf(g0, ents[i].goals);
      else g1 = fmaxf(g1, ents[i].goals);
    drawNum(SW - 72, SH - 12, g0, C_BLUE);
    drawText(SW - 48, SH - 12, "-", C_WHT);
    drawNum(SW - 38, SH - 12, g1, C_RED);
  } else if (mode == MODE_BOUNTY) {
    drawNum(SW - 72, SH - 12, teamStars[0], C_GOLD);
    drawText(SW - 48, SH - 12, "-", C_WHT);
    drawNum(SW - 38, SH - 12, teamStars[1], C_RED);
  } else if (mode == MODE_HEIST) {
    drawText(SW - 72, SH - 12, "S", C_GRE);
    drawNum(SW - 60, SH - 12, (int)(safes[0].hp / 1000), C_BLUE);
  } else if (mode == MODE_ZONE) {
    int m = 0;
    for (int z = 0; z < MAX_ZONE; z++) if (zones[z].prog > 0) m++;
    drawText(SW - 72, SH - 12, "Z", C_WHT);
    drawNum(SW - 60, SH - 12, m, C_GREEN);
  } else if (mode == MODE_SURVIE) { // V5.13 : vague affichee en bas a droite (plus de HUD haut)
    drawText(SW - 78, SH - 12, "VAGUE:", C_YEL);
    drawNum(SW - 20, SH - 12, waveN, C_WHT);
  } else if (mode == MODE_KO) {
    int a0 = 0, a1 = 0;
    for (int i = 0; i < entCount; i++)
      if (ents[i].alive) { if (ents[i].team == 0) a0++; else a1++; }
    drawNum(SW - 72, SH - 12, a0, C_BLUE);
    drawText(SW - 48, SH - 12, "-", C_WHT);
    drawNum(SW - 38, SH - 12, a1, C_RED);
  } else {
    int na = 0;
    for (int i = 0; i < entCount; i++) if (ents[i].alive) na++;
    drawText(SW - 72, SH - 12, "V", C_WHT);
    drawNum(SW - 60, SH - 12, na, C_YEL);
  }
  blit();
}

// ------------------------- Quetes -------------------------------------
// 3 quetes actives, remplacees quand terminees. Repondees en jeu.
// (structure Quest et compteurs declares plus haut, pres du killfeed)

static void rollQuest(Quest &q) {
  int t = rnd(5);
  q.type = t;
  switch (t) {
    case 0: q.goal = 1 + rnd(2); q.reward = 80; break;   // matchs gagnes
    case 1: q.goal = 3 + rnd(5); q.reward = 60; break;   // eliminations
    case 2: q.goal = 5 + rnd(6); q.reward = 50; break;   // gemmes
    case 3: q.goal = 3 + rnd(4); q.reward = 40; break;   // supers
    default: q.goal = 2 + rnd(2); q.reward = 120; break;  // serie de victoires
  }
  q.prog = 0;
}
static const char* questDesc(Quest &q, char *buf) {
  const char *base;
  switch (q.type) {
    case 0: base = "GAGNE %d MATCHS"; break;
    case 1: base = "%d ELIMINATIONS"; break;
    case 2: base = "RAMASSE %d GEMMES"; break;
    case 3: base = "UTILISE %d SUPERS"; break;
    default: base = "%d VICTOIRES DE SUITE"; break;
  }
  (void)base;
  // formatage manuel simple
  int g = q.goal, pos = 0;
  const char *prefix[] = {"GAGNE ", "ELIMINE ", "RAMASSE ", "SUPER ", "VICTOIRES "};
  const char *suffix[] = {" MATCHS", " FOIS", " GEMMES", " FOIS", " DE SUITE"};
  const char *p = prefix[q.type], *s = suffix[q.type];
  while (*p) buf[pos++] = *p++;
  char tmp[8]; int tn = 0;
  do { tmp[tn++] = '0' + g % 10; g /= 10; } while (g);
  while (tn > 0) buf[pos++] = tmp[--tn];
  while (*s) buf[pos++] = *s++;
  buf[pos] = 0;
  return buf;
}
static void initQuests() {
  killCounter = gemCounter = superCounter = winStreak = 0;
  for (int i = 0; i < 3; i++) rollQuest(quests[i]);
}
static void questScreen() {
  waitRelease();
  { // dessin unique (ecran statique, pas de redraw a 60fps)
    bgPanel();
    drawTextC(8, "QUETES DU JOUR", C_YEL);
    fRect(SW / 2 - 60, 22, 120, 3, C_BTND);
    for (int i = 0; i < 3; i++) {
      Quest &q = quests[i];
      int y = 44 + i * 52;
      bool done = q.prog >= q.goal;
      rRect(30, y, SW - 60, 42, 8, done ? C_GREEN : KDColor::RGB24(0x2B3A55));
      char buf[32];
      drawText(40, y + 6, questDesc(q, buf), done ? C_BLK : C_WHT);
      // barre de progression
      fRect(40, y + 20, 180, 8, C_BLK);
      fRect(41, y + 21, (int)(178 * fminf(1.0f, (float)q.prog / q.goal)), 6, done ? C_GOLD : C_CYN);
      drawNum(226, y + 18, q.prog, C_WHT);
      drawText(254, y + 18, "SUR", C_GRE);
      drawNum(294, y + 18, q.goal, C_WHT);
      // recompense
      oCirc(46, y + 36, 4, C_YEL, C_BTND);
      drawNum(54, y + 32, q.reward, done ? C_BLK : C_YEL);
      if (done) drawText(80, y + 32, "COMPLETEE!", C_BLK);
    }
    drawTextC(SH - 14, "OK RETOUR", C_GRE);
    blit();
  }
  while (true) {
    pollKeys();
    if (keyOK && !prevOK) { prevOK = true; return; }
    prevOK = keyOK;
    Ion::Timing::msleep(60);
  }
}
static void updateQuests(int result) {
  // appelee a la fin de chaque match
  if (result == 1) {
    winStreak++;
  } else if (result < 0) {
    winStreak = 0;
  }
  for (int i = 0; i < 3; i++) {
    Quest &q = quests[i];
    bool wasDone = q.prog >= q.goal;
    switch (q.type) {
      case 0: if (result == 1) q.prog++; break;
      case 1: q.prog = killCounter; break;
      case 2: q.prog = gemCounter; break;
      case 3: q.prog = superCounter; break;
      default: q.prog = (winStreak >= q.goal) ? q.goal : winStreak; break;
    }
    if (!wasDone && q.prog >= q.goal) {
      acc.coins += q.reward;
      rollQuest(q); // nouvelle quete
      saveAcc(); // les quetes et pieces gagnees persistent
    }
  }
  saveAcc();
}

// ------------------------- Menus --------------------------------------
static const char **gMenuDesc = nullptr; // descriptions optionnelles des items du menu
static int gPortrait = -1; // V5.14 : portrait du brawler affiche sur le menu principal (-1 = aucun)
static bool gTitleAnim = false; // V5.11 : defile de brawlers derriere le menu principal
static int menuPick(const char *title, const char **items, int n, int start) {
  int sel = start;
  bool pU = false, pD = false;
  int drawn = -1, lastPhase = -1;
  waitRelease();
  while (true) {
    // V5.14 "framebuffer partiel" : l'ecran titre n'est redessine que quand l'anim
    // avance vraiment d'un cran (animT/40) — les menus classiques ne redessinent que si sel bouge
    int phase = gTitleAnim ? (int)(animT / 40) : 0;
    if (drawn != sel || phase != lastPhase) { // redessine seulement quand la selection bouge (perf)
      lastPhase = phase;
      bgPanel();
      if (gTitleAnim) { // V5.11 : defile de brawlers en bas de l'ecran titre
        for (int s = 0; s < 6; s++) {
          int sx = (animT / 12 + s * 70) % (SW + 80) - 40;
          drawBrawlerSprite(sx, SH - 10, 11, (s * 9 + animT / 400) % 55, 90, false, (int)(animT / 90));
        }
        // V5.14 : PORTRAIT du brawler du menu principal, en haut a droite
        if (gPortrait >= 0) {
          rRect(SW - 74, 26, 68, 74, 6, C_PANEL2);
          rRect(SW - 72, 28, 64, 70, 4, KDColor::RGB24(0x2B3A55));
          drawBrawlerSprite(SW - 40, 88, 17, gPortrait, 90, false, (int)(animT / 130));
          const char *nm = BRAWLERS[gPortrait].name;
          drawText(SW - 70 + (64 - textW(nm)) / 2, 32, nm, C_YEL);
          int ssn = seasonIdx();
          if (ssn) drawText(SW - textW(SEASON_NAMES[ssn]) - 4, 6, SEASON_NAMES[ssn], ssn == 1 ? C_ORG : C_WHT); // V5.16 : saison discrete en haut a droite
        }
      }
      { // V5.16 : la saison s'affiche meme sans portrait
        int ssn2 = seasonIdx();
        if (ssn2 && gPortrait < 0) drawText(SW - textW(SEASON_NAMES[ssn2]) - 4, 6, SEASON_NAMES[ssn2], ssn2 == 1 ? C_ORG : C_WHT);
      }
      drawTextC(6, title, C_YEL);
      fRect(SW / 2 - 60, 20, 120, 3, C_BTND);
      int first = (sel > 4) ? sel - 4 : 0;
      if (first > n - 9) first = fmaxf(0, n - 9);
      for (int i = first; i < n && i < first + 9; i++) {
        int y = 28 + (i - first) * 19;
        if (i == sel) rRect(36, y, SW - 72, 16, 6, C_YEL);
        else rRect(36, y, SW - 72, 16, 6, C_BTN);
        drawTextC(y + 3, items[i], i == sel ? C_BLK : C_WHT);
      }
      if (gMenuDesc && gMenuDesc[sel]) drawTextC(206, gMenuDesc[sel], C_GRE); // description du mode
      drawTextC(SH - 14, "OK CHOISIR   BACK RETOUR", C_GRE);
      blit();
      drawn = sel;
    }
    pollKeys();
    if (kUp && !pU && sel > 0) sel--; // front montant seulement : 1 appui = 1 cran
    if (kDown && !pD && sel < n - 1) sel++;
    pU = kUp; pD = kDown;
    if (keyOK && !prevOK) { prevOK = true; return sel; }
    if (keyBack && !prevBack) { prevBack = true; return -1; }
    prevOK = keyOK; prevBack = keyBack;
    if (gTitleAnim) animT += 50; // l'ecran titre defile en continu
    Ion::Timing::msleep(50);
  }
}

static void trophyRoadScreen() {
  waitRelease();
  { // dessin unique (ecran statique)
    bgPanel();
    drawTextC(6, "ROUTE DES TROPHEES", C_YEL);
    fRect(SW / 2 - 60, 20, 120, 3, C_BTND);
    int tt = totalTrophies();
    drawText(20, 30, "TOTAL:", C_WHT);
    fRect(96, 28, 10, 5, C_GOLD); fRect(98, 34, 8, 2, C_GOLD); fRect(101, 37, 3, 5, C_GOLD);
    drawNum(114, 30, tt, C_YEL);
    drawText(20, 48, "BRAWLERS:", C_WHT);
    drawNum(150, 48, unlockedBrawlers, C_GREEN);
    drawText(180, 48, "SUR 50", C_GRE);
    drawText(20, 66, "PIECES:", C_WHT);
    oCirc(110, 68, 4, C_YEL, C_BTND);
    drawNum(120, 66, acc.coins, C_YEL);

    int next = -1;
    for (int i = 0; i < 55; i++)
      if (!isUnlocked(i)) { next = i; break; }
    drawTextC(76, "PROCHAIN DEBLOCAGE", C_WHT);
    if (next >= 0) {
      const BrawlerDef &d = BRAWLERS[next];
      rRect(SW / 2 - 110, 84, 220, 80, 8, KDColor::RGB24(RAR_COL[d.rar]));
      drawBrawlerSprite(SW / 2 - 80, 122, 16, next, 90, false);
      drawText(SW / 2 - 55, 92, d.name, C_WHT);
      drawText(SW / 2 - 55, 106, RAR_NAME[d.rar], KDColor::RGB24(RAR_COL[d.rar]));
      drawText(SW / 2 - 55, 122, "A", C_WHT);
      drawNum(SW / 2 - 41, 122, d.unlockTroph, C_YEL);
      drawText(SW / 2 - 55, 138, "TROPHEES", C_GRE);
      int prog = fminf(1.0f, (float)tt / d.unlockTroph);
      fRect(SW / 2 - 55, 152, 160, 8, C_BLK);
      fRect(SW / 2 - 54, 153, (int)(158 * prog), 6, C_GREEN);
    } else {
      drawTextC(110, "TOUT EST DEBLOQUE!", C_GOLD);
    }
    drawTextC(SH - 14, "OK RETOUR", C_GRE);
    blit();
  }
  // reste affiche jusqu'a un vrai appui OK (avant : l'ecran sautait aussitot)
  while (true) {
    pollKeys();
    if (keyOK && !prevOK) { prevOK = true; return; }
    if (keyBack && !prevBack) { prevBack = true; return; }
    prevOK = keyOK; prevBack = keyBack;
    Ion::Timing::msleep(60);
  }
}

// ecran de selection de personnage : liste + grand portrait style BS
// ---- V5.11 : rangs par brawler (Bronze -> Prestige, tous les 1000 trophees) ----
static KDColor rankCol(int bIdx) {
  int t = acc.b[bIdx].trophies;
  if (t >= 1000) return C_PUR;                     // PRESTIGE
  if (t >= 500) return KDColor::RGB24(0x50C8E8);   // DIAMANT
  if (t >= 250) return C_GOLD;                     // OR
  if (t >= 100) return KDColor::RGB24(0xC0C0C0);   // ARGENT
  if (t >= 10) return KDColor::RGB24(0xB08A50);    // BRONZE
  return C_DGREY;
}
static const char *rankName(int bIdx) {
  int t = acc.b[bIdx].trophies;
  if (t >= 1000) return "PRESTIGE";
  if (t >= 500) return "DIAMANT";
  if (t >= 250) return "OR";
  if (t >= 100) return "ARGENT";
  if (t >= 10) return "BRONZE";
  return "DEBUTANT";
}

// ---- V5.11 : choix du pseudo au premier lancement (grille de caracteres) ----
static void namePicker() {
  static const char *chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_";
  const int NC = 39;
  char name[9]; name[0] = 0; int len = 0, sel = 0;
  bool pU = false, pD = false, pL = false, pR = false;
  waitRelease();
  while (true) {
    bgPanel();
    drawTextC(6, "TON PSEUDO ?", C_YEL);
    fRect(SW / 2 - 60, 20, 120, 3, C_BTND);
    fRect(SW / 2 - 64, 30, 128, 18, C_BTN);
    drawText(SW / 2 - len * 4, 34, name, C_BLK); // pseudo en construction
    for (int i = 0; i < NC; i++) {
      int cx = 40 + (i % 8) * 30, cy = 58 + (i / 8) * 22;
      if (i == sel) fRect(cx - 2, cy - 2, 24, 18, C_YEL);
      char s[2] = { chars[i], 0 };
      drawText(cx + 7, cy + 2, s, (i == sel) ? C_BLK : C_WHT);
    }
    drawTextC(178, "OK AJOUTER  BACK FIN", C_GRE);
    blit();
    pollKeys();
    if (kUp && !pU && sel >= 8) sel -= 8;
    if (kDown && !pD && sel + 8 < NC) sel += 8;
    if (kLeft && !pL && sel > 0) sel--;
    if (kRight && !pR && sel < NC - 1) sel++;
    pU = kUp; pD = kDown; pL = kLeft; pR = kRight;
    if (keyOK && !prevOK && len < 8) { prevOK = true; name[len++] = chars[sel]; name[len] = 0; }
    if (keyBack && !prevBack) { // BACK : on valide le pseudo (ou on garde SHELLY par defaut)
      prevBack = true;
      if (len > 0) { strcpy(acc.name, name); saveAcc(); }
      return;
    }
    prevOK = keyOK; prevBack = keyBack;
    Ion::Timing::msleep(60);
  }
}

// V5.16 : TRI de la grille de brawlers — + pour changer de critere
static int gSortMode = 0;
static const char *SORT_NAMES[5] = {"NORMAL", "MAX TROPHEES", "RARETE", "UTILISATION", "NIVEAU MAX"};
static int brawlerPicker(int startSel, const char *title = "CHOISIS TON BRAWLER") {
  int favB = -1; // V5.16 : trophée d'or anime sur le brawler le plus joué
  { int best = 0;
    for (int i = 0; i < 55; i++) if (acc.pMatches[i] > best) { best = acc.pMatches[i]; favB = i; }
  }
  int curB = (startSel >= 0 && startSel < 55) ? startSel : 0; // brawler actuellement selectionne
  bool pU = false, pD = false, pL = false, pR = false, pPl = false;
  waitRelease();
  while (true) {
    // ordre d'affichage selon le tri choisi (reconstruit a chaque redraw, le + change le critere)
    int ord[55];
    for (int i = 0; i < 55; i++) ord[i] = i;
    for (int i = 0; i < 55; i++)
      for (int j = i + 1; j < 55; j++) {
        float ki, kj;
        switch (gSortMode) {
          case 1: ki = acc.b[ord[i]].trophies; kj = acc.b[ord[j]].trophies; break;
          case 2: ki = BRAWLERS[ord[i]].rar; kj = BRAWLERS[ord[j]].rar; break;
          case 3: ki = acc.pMatches[ord[i]]; kj = acc.pMatches[ord[j]]; break;
          case 4: ki = acc.b[ord[i]].level; kj = acc.b[ord[j]].level; break;
          default: ki = -ord[i]; kj = -ord[j]; break;
        }
        if (kj > ki) { int t2 = ord[i]; ord[i] = ord[j]; ord[j] = t2; }
      }
    int sel = 0; // position dans la grille triee
    for (int i = 0; i < 55; i++) if (ord[i] == curB) { sel = i; break; }
    bgPanel();
    drawTextC(6, title, C_YEL);
    fRect(SW / 2 - 60, 20, 120, 3, C_BTND);
    // ---- grille de cartes facon Brawl Stars : 5 colonnes x 4 rangees, 3 pages ----
    int page = sel / 20;
    drawText(4, 6, "PAGE", C_GRE); drawNum(34, 6, page + 1, C_WHT); drawText(48, 6, "/3", C_GRE);
    drawText(SW - 96, 6, "TRI:", C_GRE); drawText(SW - textW(SORT_NAMES[gSortMode]) - 4, 6, SORT_NAMES[gSortMode], C_CYN); // V5.16
    for (int i = page * 20; i < 55 && i < page * 20 + 20; i++) {
      int idx = i - page * 20;
      int bx = 4 + (idx % 5) * 63, by = 28 + (idx / 5) * 36;
      int bi = ord[i]; // V5.16 : la grille suit le tri
      const BrawlerDef &d = BRAWLERS[bi];
      bool un = isUnlocked(bi);
      fRect(bx + 2, by + 2, 58, 32, un ? KDColor::RGB24(RAR_COL[d.rar]) : KDColor::RGB24(0x232330));
      if (i == sel) { // carte selectionnee : liseret dore
        fRect(bx + 2, by + 2, 58, 2, C_YEL); fRect(bx + 2, by + 32, 58, 2, C_YEL);
        fRect(bx + 2, by + 2, 2, 32, C_YEL); fRect(bx + 58, by + 2, 2, 32, C_YEL);
      }
      drawBrawlerSprite(bx + 30, by + 14 + (animT / 400) % 2, 7, bi, 90, false);
      if (un) { fRect(bx + 4, by + 23, 14, 9, C_YEL); drawNum(bx + 6, by + 25, acc.b[bi].level, C_BLK); }
      else drawText(bx + 26, by + 12, "?", C_GRE);
      if (acc.skin[bi] > 0 && (acc.skinOwned[bi >> 3] & (1 << (bi & 7)))) fRect(bx + 54, by + 4, 4, 4, C_MAG); // skin equipe
      if (un) { // badge de rang (V5.11) : point de couleur, "P" dore si PRESTIGE
        if (acc.b[bi].trophies >= 1000) drawText(bx + 4, by + 3, "P", C_GOLD);
        else if (acc.b[bi].trophies >= 10) fRect(bx + 5, by + 5, 4, 4, rankCol(bi));
      }
      if (bi == favB && (animT / 350) % 2 == 0) { // V5.16 : TROPHEE D'OR anime sur le plus joue
        fRect(bx + 52, by + 3, 5, 3, C_GOLD); fRect(bx + 53, by + 7, 3, 2, C_GOLD); fRect(bx + 54, by + 9, 2, 2, C_GOLD);
      }
    }
    // ---- bandeau du brawler selectionne ----
    int bi2 = ord[sel];
    const BrawlerDef &d = BRAWLERS[bi2];
    bool un = isUnlocked(bi2);
    fRect(4, 176, 312, 42, KDColor::RGB24(0x1A2233));
    fRect(4, 176, 312, 2, C_BTND);
    drawBrawlerSprite(30, 197, 11, bi2, 90, false);
    drawText(50, 180, d.name, un ? C_YEL : C_GRE);
    if (un) {
      drawText(50, 196, RAR_NAME[d.rar], KDColor::RGB24(RAR_COL[d.rar]));
      drawText(140, 196, rankName(bi2), rankCol(bi2)); // rang du brawler (V5.11)
      fRect(216, 180, 10, 5, C_GOLD); fRect(218, 186, 8, 2, C_GOLD);
      drawNum(232, 179, acc.b[bi2].trophies, C_WHT);
      fRect(284, 179, 24, 13, C_YEL);
      drawNum(291, 182, acc.b[bi2].level, C_BLK);
      drawText(216, 196, "OK CHOISIR", C_GREEN);
    } else {
      drawText(50, 196, "A DEBLOQUER A", C_WHT);
      drawNum(146, 196, d.unlockTroph, C_YEL);
      drawText(170, 196, "TROPHEES", C_GRE);
    }
    drawTextC(212, "BACK RETOUR", C_GRE);
    blit();
    animT += 100;
    pollKeys();
    if (kUp && !pU && sel >= 5) sel -= 5;    // front montant : 1 appui = 1 carte
    if (kDown && !pD && sel < 50) sel += 5;
    if (kLeft && !pL && sel > 0) sel--;
    if (kRight && !pR && sel < 54) sel++;
    if (kPlus && !pPl) { gSortMode = (gSortMode + 1) % 5; } // V5.16 : + change le critere de tri
    pU = kUp; pD = kDown; pL = kLeft; pR = kRight; pPl = kPlus;
    curB = ord[sel]; // on retient le brawler, pas la case
    if (keyOK && !prevOK && isUnlocked(curB)) { prevOK = true; return curB; }
    if (keyBack && !prevBack) { prevBack = true; return -1; }
    prevOK = keyOK; prevBack = keyBack;
    Ion::Timing::msleep(60);
  }
}

// ---- V5.10 : carrieres (stats persistantes) ----
static void careerScreen() {
  waitRelease();
  { // dessin unique (ecran statique)
    bgPanel();
    drawTextC(8, "CARRIERE", C_YEL);
    fRect(SW / 2 - 60, 24, 120, 3, C_BTND);
    int wr = acc.matches > 0 ? acc.wins * 100 / acc.matches : 0;
    const char *labs[6] = {"PARTIES", "VICTOIRES", "DEFAITES", "TAUX DE VICTOIRE", "KILLS TOTAUX", "BRAWLER FAVORI"};
    for (int i = 0; i < 6; i++) {
      int y = 42 + i * 20;
      rRect(16, y - 2, 150, 15, 4, KDColor::RGB24(0x2B3A55));
      drawText(22, y, labs[i], C_WHT);
      rRect(176, y - 2, 128, 15, 4, C_BTN);
      if (i == 3) { drawText(246, y, "%", C_BLK); drawNum(226, y, wr, C_BLK); }
      else if (i == 5) { int fav = 0; for (int b = 1; b < 55; b++) if (acc.b[b].trophies > acc.b[fav].trophies) fav = b; drawText(182, y, BRAWLERS[fav].name, C_BLK); }
      else { int v = (i == 0) ? acc.matches : (i == 1) ? acc.wins : (i == 2) ? acc.losses : acc.killsTotal; drawNum(290, y, v, C_BLK); }
    }
    rRect(SW / 2 - 70, 190, 140, 24, 8, C_YEL);
    drawTextC(196, "OK RETOUR", C_BLK);
    blit();
    prevOK = true;
    while (true) { pollKeys(); if (keyOK && !prevOK) break; prevOK = keyOK; Ion::Timing::msleep(60); } // V5.17 FIX : attend un VRAI appui OK (avant : l'ecran clignotait et sortait aussitot)
    prevOK = false;
  }
}

// ---- V5.10 : boutique de skins (AZUR / OR / OMBRE, achetables en pieces) ----
static const int SKIN_COST[4] = {0, 400, 800, 1200};
static void shopScreen(int myBrawler) {
  const char *names[55]; int idx[55], n = 0;
  for (int i = 0; i < 55; i++) if (isUnlocked(i)) { names[n] = BRAWLERS[i].name; idx[n++] = i; }
  if (n == 0) return;
  waitRelease();
  int s = menuPick("BOUTIQUE : BRAWLER", names, n, myBrawler < n ? myBrawler : 0);
  if (s < 0) return;
  int b = idx[s];
  waitRelease();
  const char *sk[4] = {"NORMAL (GRATUIT)", "AZUR - 400 PIECES", "OR - 800 PIECES", "OMBRE - 1200 PIECES"};
  int c = menuPick(BRAWLERS[b].name, sk, 4, 0);
  if (c < 0) return;
  bool owned = (c == 0) || (acc.skinOwned[b >> 3] & (1 << (b & 7)));
  if (c > 0 && !owned) {
    if (acc.coins < SKIN_COST[c]) { // pas assez de pieces
      waitRelease();
      bgPanel();
      drawTextC(90, "PAS ASSEZ DE PIECES!", C_RED);
      drawTextC(110, "GAGNE DES MATCHS POUR", C_WHT);
      drawTextC(124, "EN ACHETER PLUS", C_WHT);
      blit();
      prevOK = true;
      while (true) { pollKeys(); if (keyOK && !prevOK) break; prevOK = keyOK; Ion::Timing::msleep(60); } // V5.17 FIX : attend un VRAI appui OK (avant : l'ecran clignotait et sortait aussitot)
      return;
    }
    acc.coins -= SKIN_COST[c];
    acc.skinOwned[b >> 3] |= (uint8_t)(1 << (b & 7));
  }
  acc.skin[b] = (uint8_t)c;
  saveAcc();
  // apercu du skin choisi
  waitRelease();
  bgPanel();
  drawTextC(8, BRAWLERS[b].name, C_YEL);
  drawTextC(24, sk[c], KDColor::RGB24(RAR_COL[BRAWLERS[b].rar]));
  gSkinTint = acc.skin[b];
  drawBrawlerSprite(SW / 2, 96, 26, b, 90, false);
  gSkinTint = 0;
  shadow(SW / 2, 128, 20, 5);
  drawTextC(190, "SKIN EQUIPE!", C_GREEN);
  rRect(SW / 2 - 70, 200, 140, 20, 8, C_YEL);
  drawTextC(205, "OK RETOUR", C_BLK);
  blit();
  prevOK = true;
  while (true) { pollKeys(); if (keyOK && !prevOK) break; prevOK = keyOK; Ion::Timing::msleep(60); } // V5.17 FIX : attend un VRAI appui OK (avant : l'ecran clignotait et sortait aussitot)
  prevOK = false;
}

// ---- V5.10 : emotes (bulle au-dessus du joueur, touches + et -) ----
static void emoteScreen() {
  waitRelease();
  int p = menuPick("EMOTE TOUCHE +", EMOTE_NAMES, 7, acc.emoteP);
  if (p >= 0) { acc.emoteP = (uint8_t)p; saveAcc(); }
  waitRelease();
  int m = menuPick("EMOTE TOUCHE -", EMOTE_NAMES, 7, acc.emoteM);
  if (m >= 0) { acc.emoteM = (uint8_t)m; saveAcc(); }
}

// ecran des parametres : rappel des touches
static void settingsScreen() {
  waitRelease();
  { // dessin unique
    bgPanel();
    drawTextC(8, "PARAMETRES", C_YEL);
    fRect(SW / 2 - 60, 22, 120, 3, C_BTND);
    drawTextC(32, "TOUCHES DU JEU", C_CYN);
    const char *acts[5] = {"SE DEPLACER", "ATTAQUE", "ULTI", "QUITTER", "RETOUR MENU"};
    const char *keys[5] = {"FLECHES", "OK / VAR", "RETOUR", "HOME", "BACK"};
    for (int i = 0; i < 5; i++) {
      int y = 40 + i * 15;
      rRect(16, y - 2, 150, 14, 4, KDColor::RGB24(0x2B3A55));
      drawText(22, y, acts[i], C_WHT);
      rRect(186, y - 2, 120, 14, 4, C_BTN);
      drawText(192, y, keys[i], C_BLK);
    }
    drawTextC(158, "+ NIV  / STAR", C_GRE);
    rRect(SW / 2 - 70, 190, 140, 24, 8, C_YEL);
    drawTextC(196, "OK RETOUR", C_BLK);
    blit();
  }
  while (true) {
    pollKeys();
    if (keyOK && !prevOK) { prevOK = true; return; }
    if (keyBack && !prevBack) { prevBack = true; return; }
    prevOK = keyOK; prevBack = keyBack;
    Ion::Timing::msleep(60);
  }
}

static void brawlerMenu(int myBrawler) {
  while (true) {
    int sel = brawlerPicker(myBrawler);
    if (sel < 0) return;
    myBrawler = sel;
    waitRelease(); // sinon le OK du picker refermait la fiche aussitot
    while (true) {
      const BrawlerDef &d = BRAWLERS[sel];
      BrawlerAccount &a = acc.b[sel];
      bool unlocked = isUnlocked(sel);
      float mul = lvlMul(a.level);
      int cost = upgradeCost(a.level);
      bgPanel();

      int bob = (animT / 400) % 2;
      rRect(18, 26, 62, 70, 8, unlocked ? KDColor::RGB24(RAR_COL[d.rar]) : C_DGREY);
      if (unlocked) {
        drawBrawlerSprite(50, 60 - bob, 22, sel, 90, false);
        fRect(62, 38, 30, 20, C_YEL); fRect(62, 52, 30, 6, C_BTND);
        drawNum(68, 41, a.level, C_BLK);
      } else {
        shCircRGB(50, 60 - bob, 22, 0x444444, 0x222222, C_GRE);
        drawText(40, 56, "?", C_GRE);
        drawText(22, 102, "A", C_GRE);
        drawNum(34, 102, d.unlockTroph, C_YEL);
        drawText(70, 102, "TROPHEES", C_GRE);
      }
      shadow(50, 88, 16, 4);
      drawText(90, 30, d.name, unlocked ? C_YEL : C_GRE);
      drawText(90, 46, RAR_NAME[d.rar], unlocked ? KDColor::RGB24(RAR_COL[d.rar]) : C_GRE);
      if (unlocked) {
        drawText(90, 62, "DEG", C_WHT);
        drawNum(130, 62, (int)(d.baseDmg * mul * ((a.starPower && d.star == STAR_DMG) ? 1.1f : 1)), C_ORG);
        drawText(180, 62, "PORTEE", C_WHT);
        drawNum(258, 62, RNG(d.range), C_CYN);
        drawTextC(80, BRAWLER_BIOS[sel], C_GRE);
      }
      fRect(230, 28, 10, 6, C_GOLD); fRect(233, 35, 8, 3, C_GOLD); fRect(236, 39, 3, 6, C_GOLD);
      drawNum(250, 30, a.trophies, C_WHT);
      oCirc(236, 56, 4, C_YEL, C_BTND); drawNum(250, 52, acc.coins, C_WHT);

      if (unlocked) {
        // super du brawler
        rRect(10, 104, 300, 28, 5, KDColor::RGB24(0x2B3A55));
        drawText(16, 108, "SUPER", C_YEL);
        drawText(16, 120, SUPER_NAMES[sel], C_WHT);
        // pouvoir star : 1 ligne pleine largeur
        rRect(10, 136, 300, 16, 4, a.starPower ? C_PUR : KDColor::RGB24(0x3A1F55));
        drawText(16, 139, "STAR", a.starPower ? C_WHT : C_GRE);
        drawText(70, 139, a.starPower ? curStarName(sel) : "NON", a.starPower ? C_WHT : C_GRE);
        if (a.starPower) drawNum(284, 139, a.starChoice + 1, C_WHT);
        // V5.16 : stats de carriere AVEC CE brawler
        drawText(16, 158, "MATCHS:", C_WHT); drawNum(80, 158, acc.pMatches[sel], C_WHT);
        drawText(116, 158, "VICT:", C_GREEN); drawNum(156, 158, acc.pWins[sel], C_GREEN);
        drawText(200, 158, "KILLS:", C_ORG); drawNum(248, 158, acc.pKills[sel], C_ORG);
        drawText(16, 172, "DEGATS:", C_YEL); drawNum(84, 172, acc.pDmg[sel] * 100, C_YEL); // stockes en centaines

        // boutons d'achat
        cost = upgradeCost(a.level);
        if (a.level < 11) {
          rRect(6, 196, 74, 32, 5, (acc.coins >= cost) ? C_YEL : KDColor::RGB24(0x5A4A9A));
          drawText(12, 199, "+ NIV", C_BLK); drawNum(12, 215, cost, C_BLK);
        } else {
          rRect(6, 196, 74, 32, 5, C_GREEN);
          drawText(12, 207, "NIV MAX", C_BLK);
        }
        if (a.level >= 3 && !a.starPower) { rRect(84, 196, 74, 32, 5, (acc.coins >= 150) ? C_PUR : KDColor::RGB24(0x3A1F55)); drawText(90, 199, "/ STAR", C_WHT); drawNum(90, 215, 150, C_WHT); }
        else if (a.starPower) { rRect(84, 196, 74, 32, 5, C_PUR); drawText(90, 207, "/ CHANGE", C_WHT); }
      } else {
        drawTextC(120, "BRAWLER NON DEBLOQUE", C_GRE);
        drawTextC(140, "GAGNE DES TROPHEES POUR", C_WHT);
        drawTextC(156, "LE DEBLOQUER!", C_WHT);
      }
      blit();
      pollKeys();
      animT += 80;
      if (unlocked) {
        // achats par front montant : 1 appui = 1 achat, plus de double comptage
        static bool pPlus = false, pDiv = false;
        bool doPlus = kPlus && !pPlus, doDiv = kDiv && !pDiv;
        pPlus = kPlus; pDiv = kDiv;
        if (a.level < 11 && acc.coins >= cost && doPlus)
          { acc.coins -= cost; a.level++; saveAcc(); }
        if (a.level >= 3 && !a.starPower && acc.coins >= 150 && doDiv)
          { acc.coins -= 150; a.starPower = true; a.starChoice = 0; saveAcc(); }
        else if (a.starPower && doDiv)
          { a.starChoice ^= 1; saveAcc(); }
      }
      if (keyOK || keyBack) { prevOK = keyOK; prevBack = keyBack; break; }
      prevOK = keyOK; prevBack = keyBack;
      Ion::Timing::msleep(80);
    }
  }
}

// ------------------------- Resultat + star drop -----------------------
// V5.13b : ecran dedie quand un match (ou un star drop) debloque un NOUVEAU BRAWLER
static void newBrawlerScreen(int bIdx) {
  if (bIdx < 0 || bIdx >= 55) return;
  const BrawlerDef &d = BRAWLERS[bIdx];
  waitRelease();
  while (true) {
    bgPanel();
    rRect(SW / 2 - 120, 12, 240, 30, 8, KDColor::RGB24(RAR_COL[d.rar]));
    drawTextC(22, "NOUVEAU BRAWLER !", C_BLK);
    drawBrawlerSprite(SW / 2, 100, 26, bIdx, 90, false, (int)(animT / 90)); // grand sprite anime
    drawTextC(148, d.name, C_YEL);
    drawTextC(168, "DISPONIBLE DANS LE MENU BRAWLERS", C_WHT);
    rRect(SW / 2 - 70, 196, 140, 24, 8, C_YEL);
    drawTextC(202, "OK CONTINUER", C_BLK);
    blit();
    animT += 50;
    pollKeys();
    if (keyOK && !prevOK) { prevOK = true; break; }
    prevOK = keyOK;
    Ion::Timing::msleep(60);
  }
  waitRelease();
}

static void resultScreen(int result, int myBrawler) {
  int coins = 0, troph = 0;
  if (result == 1) { coins = 50 + rnd(50); troph = 8; }
  else if (result == 0) { coins = 20 + rnd(20); troph = -2; }
  else { coins = 8 + rnd(12); troph = 0; }
  int starDrop = (result == 1 && rnd(100) < 60) ? 20 + rnd(60) : 0;
  // ---- V5.10 : trophees bonus selon la perf perso + stats de carriere ----
  int perfBonus = 0;
  {
    int myK = 0; float myD = 0, best = 0;
    for (int i = 0; i < entCount; i++) {
      if (ents[i].isPet) continue;
      if (ents[i].isHuman) { myK += ents[i].kills; myD += ents[i].dmgDone; }
      if (ents[i].dmgDone > best) best = ents[i].dmgDone;
    }
    if (result == 1) { if (myD >= best && best > 0) perfBonus += 2; if (myK >= 3) perfBonus += 1; troph += perfBonus; }
    acc.matches++;
    if (result == 1) acc.wins++; else if (result == -1) acc.losses++;
    acc.killsTotal += myK;
    if (mode == MODE_SURVIE && waveN > acc.bestWave) acc.bestWave = waveN; // record de vagues (V5.11)
    // V5.16 : stats PAR brawler (sauvees une seule fois, ici, a la fin du match)
    if (myBrawler >= 0 && myBrawler < 55) {
      if (acc.pMatches[myBrawler] < 255) acc.pMatches[myBrawler]++;
      if (result == 1 && acc.pWins[myBrawler] < 255) acc.pWins[myBrawler]++;
      acc.pKills[myBrawler] = (uint16_t)fminf(65535, acc.pKills[myBrawler] + myK);
      acc.pDmg[myBrawler] = (uint16_t)fminf(65535, acc.pDmg[myBrawler] + (int)(myD / 100.0f)); // en centaines
    }
  }
  acc.coins += coins; // le star drop ne s'ajoute qu'a l'ouverture du coffre
  acc.b[myBrawler].trophies += troph;
  if (acc.b[myBrawler].trophies < 0) acc.b[myBrawler].trophies = 0;
  // IA adaptative : apres une defaite elle durcit, apres une victoire elle faiblit
  gAIDyn = fmaxf(0.55f, fminf(1.6f, gAIDyn + (result == 1 ? -0.06f : (result == -1 ? 0.08f : 0.0f))));
  int newUnlock = -1;
  refreshUnlocks(&newUnlock);
  updateQuests(result);
  saveAcc(); // persistance des trophees / pieces apres chaque match

  const char *msg = (result == 1) ? "VICTOIRE!" : (result == 0) ? "EGALITE" : "DEFAITE";
  KDColor col = (result == 1) ? C_YEL : (result == 0) ? C_WHT : C_RED;
  bool opened = (starDrop <= 0); // pas de coffre si pas de star drop
  int dropUnlock = -1; // star drop rare : peut debloquer un brawleur
  waitRelease();
  while (true) {
    bgPanel();
    rRect(SW / 2 - 100, 20, 200, 42, 10, col);
    drawTextC(34, msg, C_BLK);
    oCirc(SW / 2 - 30, 84, 8, C_YEL, C_BTND);
    drawText(SW / 2 - 44, 80, "+", C_WHT);
    drawNum(SW / 2 - 16, 80, coins, C_WHT);
    fRect(SW / 2 + 20, 76, 10, 5, C_GOLD); fRect(SW / 2 + 22, 81, 6, 2, C_GOLD); fRect(SW / 2 + 24, 83, 2, 5, C_GOLD);
    drawNum(SW / 2 + 36, 80, troph, troph >= 0 ? C_YEL : C_RED);
    if (perfBonus > 0) drawText(SW / 2 + 60, 80, (perfBonus >= 3) ? "MVP+K" : "MVP", C_MAG);
    if (result == 1 && winStreak >= 2) { drawTextC(66, "SERIE DE VICTOIRES", C_YEL); drawNum(SW / 2 + 74, 66, winStreak, C_YEL); } // V5.11
    if (mode == MODE_SURVIE) { drawTextC(66, "VAGUE:", C_CYN); drawNum(SW / 2 + 34, 66, waveN, C_WHT); drawText(SW / 2 + 58, 66, "RECORD:", C_GRE); drawNum(SW / 2 + 114, 66, acc.bestWave, C_YEL); }
    if (starDrop > 0) {
      drawTextC(104, "STAR DROP!", C_MAG);
      if (!opened) {
        // coffre ferme : attend un appui OK
        rRect(SW / 2 - 16, 114, 32, 24, 4, C_PUR);
        fRect(SW / 2 - 16, 123, 32, 3, C_MAG);
        oCirc(SW / 2, 126, 3, C_YEL, C_BLK);
        drawTextC(146, "APPUIE OK POUR OUVRIR", C_WHT);
      } else {
        // coffre ouvert : pieces bonus tombent (+ brawleur debloque si chanceux)
        fRect(SW / 2 - 18, 108, 36, 4, C_WHT);
        rRect(SW / 2 - 18, 112, 36, 26, 4, C_MAG);
        drawText(SW / 2 - 16, 120, "+", C_WHT);
        drawNum(SW / 2 - 8, 120, starDrop, C_CYN);
        if (dropUnlock >= 0) {
          drawTextC(146, "BRAWLER DEBLOQUE!", C_GOLD);
          drawTextC(158, BRAWLERS[dropUnlock].name, C_YEL);
        } else drawTextC(146, "PIECES BONUS!", C_CYN);
      }
    }
    // ---- V5.9 : top degats de la partie (stats de fin de match) ----
    {
      int top[3] = {-1, -1, -1};
      for (int r = 0; r < 3; r++)
        for (int i = 0; i < entCount; i++) {
          Ent &e = ents[i];
          if (e.isPet) continue;
          if (i == top[0] || i == top[1] || i == top[2]) continue;
          if (top[r] < 0 || e.dmgDone > ents[top[r]].dmgDone) top[r] = i;
        }
      drawTextC(152, "TOP DEGATS    K   DMG", C_GRE);
      for (int r = 0; r < 3 && top[r] >= 0; r++) {
        Ent &e = ents[top[r]];
        KDColor tc = (e.team == ents[humanIdx].team) ? C_CYN : C_REDD;
        int y = 162 + r * 9;
        drawText(64, y, BRAWLERS[e.brawlerIdx].name, tc);
        drawNum(150, y, e.kills, C_WHT);
        drawNum(180, y, (int)(e.dmgDone / 1000.0f), C_YEL);
        drawText(196 + ((int)(e.dmgDone / 1000.0f) > 9 ? 8 : 0), y, "K", C_YEL);
        if (e.cubes > 0) { drawNum(216, y, e.cubes, C_PUR); }
      }
    }
    if (newUnlock >= 0) {
      const BrawlerDef &d = BRAWLERS[newUnlock];
      int ubY = (starDrop > 0) ? ((dropUnlock >= 0 && opened) ? 166 : 158) : 148;
      rRect(SW / 2 - 110, ubY, 220, 24, 8, KDColor::RGB24(RAR_COL[d.rar]));
      drawTextC(ubY + 3, "NOUVEAU BRAWLER!", C_BLK);
      drawTextC(ubY + 14, d.name, C_WHT);
    }
    rRect(SW / 2 - 84, 190, 168, 24, 8, C_YEL);
    drawTextC(196, opened ? "OK CONTINUER  + REJOUER" : "OK OUVRIR", C_BLK);
    // ---- V5.14 : FEUX D'ARTIFICE de particules quand on a GAGNE ----
    if (result == 1) {
      animT += 60;
      for (int fw = 0; fw < 3; fw++) { // 3 fusees en decalage, tir indefiniment
        int age = (animT + fw * 700) % 2100;
        if (age >= 1400) continue; // pause entre deux tirs
        int fx = 50 + (fw * 89 + (animT / 2100) * 47) % (SW - 100);
        int fy = 30 + (fw * 61 + (animT / 2100) * 31) % 70;
        int rad = age / 50;
        KDColor fc = (fw == 0) ? C_YEL : ((fw == 1) ? C_MAG : C_CYN);
        for (int a = 0; a < 360; a += 24) {
          px(fx + (int)(rad * fcosf(a * 0.01745f)), fy + (int)(rad * fsinf(a * 0.01745f)), fc);
          if (age < 800) px(fx + (int)(rad * 0.55f * fcosf((a + 12) * 0.01745f)), fy + (int)(rad * 0.55f * fsinf((a + 12) * 0.01745f)), C_WHT);
        }
      }
    }
    blit();
    pollKeys();
    if (keyOK && !prevOK) {
      prevOK = true;
      if (opened) {
        gReplay = kPlus;
        // V5.13b : ecran dedie si ce match a debloque un nouveau brawler (star drop inclus)
        if (newUnlock >= 0) newBrawlerScreen(newUnlock);
        else if (dropUnlock >= 0) newBrawlerScreen(dropUnlock);
        return; // + maintenu = rejouer le meme mode tout de suite
      }
      acc.coins += starDrop; // recompense ajoutee seulement a l'ouverture
      // tres faible chance (4%) que le star drop debloque un brawleur
      if (rnd(100) < 4) {
        int locked[55], nl = 0;
        for (int i = 0; i < 55; i++) if (!isUnlocked(i)) locked[nl++] = i;
        if (nl > 0) { dropUnlock = locked[rnd(nl)]; acc.b[dropUnlock].level = 1; }
      }
      saveAcc();
      opened = true;
    }
    prevOK = keyOK;
    Ion::Timing::msleep(60);
  }
}

static void playMatch(int myBrawler, int selMode) {
  setupMatch(selMode, myBrawler);
  prevOK = prevShift = prevMinus = prevBack = prevBacksp = false; keyBack = false; matchResult = -2;
  while (matchResult == -2) {
    int t0 = (int)Ion::Timing::millis();
    updateMatch(1.0f / (gFps60 ? 60.0f : 30.0f)); // V5.14 : option 60 FPS
    renderMatch();
    int frameMs = (int)Ion::Timing::millis() - t0;
    gFrameMs = (gFrameMs * 3.0f + frameMs) / 4.0f; // V5.16 : EMA pour le compteur FPS
    int target = gFps60 ? 16 : 33;
    Ion::Timing::msleep(frameMs < target ? target - frameMs : 1);
  }
  if (matchResult != 2) {
    // V5.16 : RALENTI dramatique ~1 s apres le dernier coup, avant l'ecran resultat
    int slowT = 0;
    while (slowT < 900) {
      int t0 = (int)Ion::Timing::millis();
      updateMatch(1.0f / 90.0f); // 3x plus lent : on savoure le moment
      renderMatch();
      int frameMs = (int)Ion::Timing::millis() - t0;
      Ion::Timing::msleep(frameMs < 33 ? 33 - frameMs : 1);
      slowT += 33;
    }
    resultScreen(matchResult, myBrawler); // 2 = abandon via HOME
  }
}

// ------------------------- Mode Entrainement ---------------------------
// Terrain a cibles : tire sur les cibles qui bougent, mesure ta precision.
// V5.16 : SELECTION DES MODES EN TUILES facon Brawl Stars (nom + description conserves)
static int modeTileScreen() {
  waitRelease();
  int sel = 0;
  bool pU = false, pD = false, pL = false, pR = false;
  while (true) {
    // ---- rendu ----
    bgPanel();
    drawTextC(6, "MODE DE JEU", C_YEL);
    for (int i = 0; i < MODE_COUNT; i++) {
      int bx = 8 + (i % 3) * 105, by = 22 + (i / 3) * 40;
      bool on = (i == sel);
      rRect(bx, by, 98, 36, 6, on ? C_YEL : KDColor::RGB24(0x2B3A55));
      int ix = bx + 49, iy = by + 18;
      KDColor ic = on ? C_BLK : C_WHT;
      switch (i) { // icone du mode en primitives
        case MODE_GEM: fRect(ix-5, iy-5, 10, 3, C_GEM); fRect(ix-3, iy-8, 6, 3, C_GEM); fRect(ix-3, iy-2, 6, 3, C_GEM); fRect(ix-1, iy+1, 2, 4, C_GEM); break;
        case MODE_BALL: fCirc(ix, iy, 8, on ? C_BLK : C_WHT); fRect(ix-7, iy-1, 14, 2, on ? C_YEL : C_BLK); break;
        case MODE_SOLO: fCirc(ix, iy, 7, ic); fRect(ix-4, iy-3, 2, 2, C_BLK); fRect(ix+2, iy-3, 2, 2, C_BLK); fRect(ix-2, iy+3, 4, 2, C_BLK); break;
        case MODE_DUO: fCirc(ix-4, iy, 5, ic); fCirc(ix+4, iy, 5, ic); break;
        case MODE_HEIST: fRect(ix-7, iy-6, 14, 12, on ? C_BLK : C_GOLD); fRect(ix-4, iy-2, 8, 5, on ? C_YEL : C_BLK); break;
        case MODE_BOUNTY: fRect(ix-1, iy-8, 2, 16, C_GOLD); fRect(ix-8, iy-1, 16, 2, C_GOLD); fRect(ix-6, iy-6, 2, 2, C_GOLD); fRect(ix+4, iy-6, 2, 2, C_GOLD); fRect(ix-6, iy+4, 2, 2, C_GOLD); fRect(ix+4, iy+4, 2, 2, C_GOLD); break;
        case MODE_KO: for (int s2 = -6; s2 <= 6; s2++) { px(ix + s2, iy + s2, C_RED); px(ix + s2, iy - s2, C_RED); } break;
        case MODE_ZONE: oCirc(ix, iy, 8, C_ORG, on ? C_BLK : C_PANEL); fCirc(ix, iy, 3, C_ORG); break;
        case MODE_SIEGE: case MODE_BOSSRUSH: fRect(ix-8, iy-6, 16, 10, KDColor::RGB24(0x8A8A9A)); fRect(ix-6, iy-3, 3, 3, C_RED); fRect(ix+3, iy-3, 3, 3, C_RED); fRect(ix-8, iy+4, 16, 2, KDColor::RGB24(0x5A5A6A)); break;
        case MODE_DUEL: for (int s2 = -6; s2 <= 6; s2++) { px(ix + s2, iy + s2, ic); px(ix + s2, iy - s2, ic); } break;
        case MODE_SURVIE: for (int s2 = -8; s2 <= 8; s2 += 4) { px(ix + s2, iy - 2, ic); px(ix + s2 + 1, iy + 2, ic); px(ix + s2 + 2, iy + 6, ic); } break;
      }
      if (i == MODE_BOSSRUSH) fRect(ix-6, iy-8, 4, 3, C_GOLD); // couronne du boss rush
    }
    // nom + description du mode selectionne
    fRect(0, 186, SW, 34, KDColor::RGB24(0x1A2233));
    fRect(0, 186, SW, 2, C_BTND);
    drawTextC(192, MODE_NAMES[sel], C_YEL);
    drawTextC(206, MODE_DESC[sel], C_WHT);
    blit();
    pollKeys();
    if (keyBack && !prevBack) { prevBack = true; return -1; }
    if (keyOK && !prevOK) { prevOK = true; return sel; }
    if (kUp && !pU) sel = (sel + MODE_COUNT - 3) % MODE_COUNT;
    if (kDown && !pD) sel = (sel + 3) % MODE_COUNT;
    if (kLeft && !pL) sel = (sel + MODE_COUNT - 1) % MODE_COUNT;
    if (kRight && !pR) sel = (sel + 1) % MODE_COUNT;
    pU = kUp; pD = kDown; pL = kLeft; pR = kRight;
    prevOK = keyOK; prevBack = keyBack;
    Ion::Timing::msleep(60);
  }
}

static void trainingMode(int myBrawler) {
  struct Target { float x, y, vx, hp, maxHP; bool alive; };
  Target tg[4];
  for (int i = 0; i < 4; i++) {
    tg[i].x = 180 + i * 40; tg[i].y = 60 + (i % 2) * 100;
    tg[i].vx = ((i % 2) ? 1 : -1) * (40 + i * 10);
    tg[i].maxHP = 4000 + i * 1000; tg[i].hp = tg[i].maxHP;
    tg[i].alive = true;
  }
  const BrawlerDef &d = BRAWLERS[myBrawler];
  float px_ = 60, py_ = SH / 2;
  int shots = 0, hits = 0, dmgTotal = 0;
  float myHP = d.baseHP * 2.0f; const float myMaxHP = myHP; // V5.16 : les cibles te blessent
  int tgtFireT[4] = {1600, 1600, 1600, 1600};
  float ammo = 3, aim = 0;
  if (myBrawler == 16 || myBrawler == 24) ammo = ammoMax(myBrawler); // V5.16 : Carl/Bea : 1 munition meme a l'entrainement
  Ent dummy; memset(&dummy, 0, sizeof(dummy));
  // projectiles simplifies du mode entrainement
  struct TProj { bool active; float x, y, vx, vy, travelled, range, dmg; };
  TProj tp[16];
  for (int i = 0; i < 16; i++) tp[i].active = false;
  prevOK = prevBack = false; keyBack = false;
  int trainT = 0;
  while (true) {
    pollKeys();
    if (keyBack && !prevBack) { prevBack = true; return; }
    float dt = 1.0f / 30.0f;
    trainT += 33; animT += 33;
    // deplacement
    float spd = 130.0f;
    if (kRight) px_ = fminf(SW - 30, px_ + spd * dt);
    if (kLeft) px_ = fmaxf(20, px_ - spd * dt);
    if (kUp) aim = 0;
    if (kDown) aim = 3.14159f;
    // visee auto vers la cible la plus proche
    float best = 1e9, ax = aim;
    for (int i = 0; i < 4; i++)
      if (tg[i].alive) {
        float dx = tg[i].x - px_, dy = tg[i].y - py_, dd = dx * dx + dy * dy;
        if (dd < best) { best = dd; ax = atan2f(dy, dx); }
      }
    aim = ax;
    if (keyOK && !prevOK && ammo >= 1) {
      shots++;
      ammo -= 1;
      for (int i = 0; i < 16; i++)
        if (!tp[i].active) {
          tp[i].active = true; tp[i].x = px_ + 10; tp[i].y = py_;
          tp[i].vx = fcosf(aim) * d.projSpeed; tp[i].vy = fsinf(aim) * d.projSpeed;
          tp[i].travelled = 0; tp[i].range = RNG(d.range); tp[i].dmg = d.baseDmg;
          break;
        }
    }
    prevOK = keyOK;
    ammo = fminf(ammoMax(myBrawler), ammo + dt * 1000.0f / d.reloadMs);
    // deplacement des cibles + projectiles
    for (int i = 0; i < 4; i++)
      if (tg[i].alive) {
        tg[i].x += tg[i].vx * dt;
        if (tg[i].x < 120 || tg[i].x > SW - 20) tg[i].vx = -tg[i].vx;
        if (tg[i].hp <= 0) { tg[i].alive = false; dmgTotal += tg[i].maxHP; }
      }
    // V5.16 : ENTRAINEMENT+ — les cibles RIPLENT (250 degats toutes les 1.6 s si tu es a portee)
    for (int i = 0; i < 4; i++)
      if (tg[i].alive) {
        float dx = px_ - tg[i].x, dy = py_ - tg[i].y;
        if (dx * dx + dy * dy < 230 * 230) {
          tgtFireT[i] -= 33;
          if (tgtFireT[i] <= 0) {
            tgtFireT[i] = 1600;
            spawnParts(tg[i].x + 12, tg[i].y, 1, 5);
            spawnParts(px_, py_, 0, 4);
            myHP -= 250;
          }
        }
      }
    if (myHP <= 0) { myHP = myMaxHP; spawnParts(px_, py_, 2, 8); } // respawn
    for (int i = 0; i < 16; i++)
      if (tp[i].active) {
        tp[i].x += tp[i].vx * dt; tp[i].y += tp[i].vy * dt;
        tp[i].travelled += sqrtf(tp[i].vx * tp[i].vx + tp[i].vy * tp[i].vy) * dt;
        if (tp[i].travelled > tp[i].range) { tp[i].active = false; continue; }
        for (int j = 0; j < 4; j++)
          if (tg[j].alive) {
            float dx = tg[j].x - tp[i].x, dy = tg[j].y - tp[i].y;
            if (dx * dx + dy * dy < 14 * 14) {
              tg[j].hp -= tp[i].dmg; hits++; dmgTotal += (int)tp[i].dmg;
              addPop(tp[i].x, tp[i].y - 12, (int)tp[i].dmg);
              tp[i].active = false; break;
            }
          }
      }
    // toutes les cibles respawnent quand tout est detruit
    bool allDead = true;
    for (int i = 0; i < 4; i++) if (tg[i].alive) allDead = false;
    if (allDead)
      for (int i = 0; i < 4; i++) { tg[i].alive = true; tg[i].hp = tg[i].maxHP; }
    // ---- rendu ----
    fRect(0, 0, SW, SH, KDColor::RGB24(0x4E8A5A));
    for (int ty = 0; ty < SH; ty += TILE)
      for (int tx = 0; tx < SW; tx += TILE)
        if (((tx + ty) / TILE) & 1) fRect(tx, ty, TILE, TILE, KDColor::RGB24(0x447A4E));
    fRect(0, 0, 100, SH, KDColor::RGB24(0x3A3A4A));
    fRect(100, 0, 3, SH, C_BLK);
    for (int i = 0; i < 4; i++)
      if (tg[i].alive) {
        int bx = (int)tg[i].x, by = (int)tg[i].y;
        oCirc(bx, by, 12, C_ORG, C_BLK);
        fCirc(bx, by, 7, C_WHT);
        fCirc(bx, by, 3, C_RED);
        // barre de vie de la cible
        fRect(bx - 12, by - 20, 25, 5, C_BLK);
        fRect(bx - 11, by - 19, (int)(23 * tg[i].hp / tg[i].maxHP), 3, C_YEL);
      }
    for (int i = 0; i < 16; i++)
      if (tp[i].active) oCirc((int)tp[i].x, (int)tp[i].y, 3, C_YEL, C_BLK);
    // joueur
    drawBrawlerSprite((int)px_, (int)py_, 9, myBrawler, (int)(aim * 57.2957795f), false, trainT);
    // munitions
    for (int a = 0; a < 3; a++) {
      int filled = (int)(fmaxf(0, fminf(1, ammo - a)) * 4);
      fRect((int)px_ - 8 + a * 6, (int)py_ + 12, 5, 2, C_BLK);
      fRect((int)px_ - 8 + a * 6, (int)py_ + 12, filled, 2, C_ORG);
    }
    for (int i = 0; i < MAX_POP; i++)
      if (pops[i].active) {
        pops[i].life -= 33; pops[i].y -= 0.7f;
        if (pops[i].life <= 0) pops[i].active = false;
        else drawNum((int)pops[i].x, (int)pops[i].y, pops[i].val, C_WHT);
      }
    // panneau de stats
    drawText(8, 6, "ENTRAINEMENT", C_YEL);
    drawText(8, 24, "TIRS:", C_WHT); drawNum(76, 24, shots, C_WHT);
    drawText(8, 40, "TOUCHES:", C_WHT); drawNum(112, 40, hits, C_CYN);
    drawText(8, 56, "PRECISION:", C_WHT);
    if (shots > 0) drawNum(132, 56, hits * 100 / shots, C_GREEN);
    else drawText(132, 56, "0", C_GREEN);
    drawText(8, 72, "DEGATS:", C_WHT); drawNum(96, 72, dmgTotal, C_ORG);
    drawText(8, 88, "DPS:", C_GREEN);
    if (trainT > 1500) drawNum(52, 88, dmgTotal / (trainT / 1000), C_GREEN); // V5.16 : compteur DPS
    else drawText(52, 88, "...", C_GREEN);
    // barre de vie du joueur (les cibles ripent)
    fRect(8, 104, 80, 8, C_BLK);
    fRect(9, 105, (int)(78 * myHP / myMaxHP), 6, C_GREEN);
    drawTextC(SH - 14, "OK TIR  BACK QUITTER", C_GRE);
    blit();
    Ion::Timing::msleep(20);
  }
}


// ====================  V5.26 : AMONG US MICRO — 6 CARTES, BFS ANTI-BLOCAGE, VOTES, SABOTAGES  ========================
// FLECHES: bouger | OK: TUER (imposteur) / FAIRE UNE TACHE (equipage)
 // EXE: ventilo/vitals | +: VOTE | -: ZOOM | DIV: sabotage (imposteur)
#define AUW 60
#define AUH 45
#define AU_MAXBOT 14
// ================== V5.26 : LES 6 CARTES ==================
static const char *AU_M1[AUH] = {
"############################################################",
"############################################################",
"############################################################",
"######################................#########..........###",
"####.........#########..T.....B.......#########..T.......###",
"####.........#########...................................###",
"####.T.......#########...................................###",
"####.....................................................###",
"####..................................#########..........###",
"####..................................#########..........###",
"####.........#########................###############...####",
"####.........#########................###############...####",
"####.........#########............T...###############...####",
"####.V.......#########................###############...####",
"####.........###########........#####################...####",
"######...###############........#####################...####",
"######...###############........#####################...####",
"######...###############........#####################...####",
"######...###############.....T..####################......##",
"######...###############........####################....V.##",
"###........#############........#...........########......##",
"###........#############...######.........T.########......##",
"###.T......#############...######.........................##",
"###........#############...######.........................##",
"###........#############...######.........................##",
"###........#...............######...........########....T.##",
"###..........V.............######.V.........########......##",
"###........................######...........########...#####",
"###........................##########...############...#####",
"###........#...............##########...############...#####",
"###........#...............##########...############...#####",
"############...............##########...############...#####",
"############....................#####...######...........###",
"############..T.................###.........##.........V.###",
"############....................###.T.......##...........###",
"############................................##...........###",
"#####################....................................###",
"#####################....................................###",
"#####################...........###......................###",
"#####################...........###.........##...........###",
"#####################..T........##############..T........###",
"#####################...........##############...........###",
"############################################################",
"############################################################",
"############################################################",
};
static const char *AU_M2[AUH] = {
"############################################################",
"############################################################",
"############################################################",
"############################################################",
"############################################################",
"############################################################",
"########............####################............########",
"########............####################............########",
"########............####################............########",
"########............................................########",
"########............................................########",
"########............................................########",
"########............###########...######............########",
"########............###########...######............########",
"########............###########...######............########",
"##########...##################...##############...#########",
"##########...##################...##############...#########",
"##########...##################...##############...#########",
"##########...###########..............##########...#########",
"##########...###########..............##########...#########",
"##########...###########..............##########...#########",
"##########...###########..............##########...#########",
"##########...###########..............##########...#########",
"##########...###########..............##########...#########",
"##########...###########..............##########...#########",
"##########...###########..............##########...#########",
"##########...###########..............##########...#########",
"##########...###########..............##########...#########",
"##########...##################...##############...#########",
"##########...##################...##############...#########",
"########............###########...######............########",
"########............###########...######............########",
"########............###########...######............########",
"########............###########...######............########",
"########............................................########",
"########............................................########",
"########............................................########",
"########............####################............########",
"########............####################............########",
"############################################################",
"############################################################",
"############################################################",
"############################################################",
"############################################################",
"############################################################",
};
static const char *AU_M3[AUH] = {
"############################################################",
"############################################################",
"############################################################",
"############################################################",
"####............########..........######..........##########",
"####............########..........######..........##########",
"####............########..........######..........##########",
"####..............................................##########",
"####..............................................##########",
"####..............................................##########",
"####............########..........######..........##########",
"####............########..........######..........##########",
"########...#################...#############...#############",
"########...#################...#############...#############",
"########...#################...#############...#############",
"########...#################...#############...#############",
"####............############...#########..........##########",
"####............############...#########..........##########",
"####............####............########..........##########",
"####............####............########..........##########",
"####............................########..........##########",
"####..............................................##########",
"####..............................................##########",
"####............####..............................##########",
"####............####............########..........##########",
"####............####............###############...##########",
"########...#########............###############...##########",
"########...#################...################...##########",
"########...#################...################...##########",
"########...#################...################...##########",
"####............########............###########...##########",
"####............########............###########...##########",
"####............########............########..........######",
"####............########............########..........######",
"####..................................................######",
"####..................................................######",
"####..................................................######",
"####............########............########..........######",
"####............########............########..........######",
"############################################..........######",
"############################################..........######",
"############################################################",
"############################################################",
"############################################################",
"############################################################",
};
static const char *AU_M4[AUH] = {
"############################################################",
"############################################################",
"############################################################",
"############################################################",
"####............########............########............####",
"####............########............########............####",
"####............########............########............####",
"####....................................................####",
"####....................................................####",
"####....................................................####",
"####............########............########............####",
"####............########............########............####",
"########...#################...#################...#########",
"########...#################...#################...#########",
"########...#################...#################...#########",
"########...#################...#################...#########",
"########...#################...#################...#########",
"########...#################...#################...#########",
"########...#############............############...#########",
"########...#############............############...#########",
"####..............######............########............####",
"####..............######............########............####",
"####..............######............########............####",
"####..............######............########............####",
"####....................................................####",
"####....................................................####",
"####....................................................####",
"####..............######............########............####",
"####..............######............########............####",
"####..............######............########............####",
"############...#############...###############...###########",
"############...#############...###############...###########",
"############...#############...###############...###########",
"############...#############...###############...###########",
"##########.....................###....................######",
"##########..................######....................######",
"##########..................######....................######",
"##########............................................######",
"##########............................................######",
"##########............................................######",
"##########..................######....................######",
"##########..................######....................######",
"############################################################",
"############################################################",
"############################################################",
};
static const char *AU_M5[AUH] = {
"############################################################",
"############################################################",
"############################################################",
"############################################################",
"##########################............######################",
"##########################............######################",
"##########################............######################",
"##############.............................#################",
"##############.............................#################",
"##############.............................#################",
"##########################............######################",
"##########################............######################",
"##############################...###########################",
"##############################...###########################",
"##############################...###########################",
"##############################...###########################",
"##########..........##########...#######..........##########",
"##########..........##########...#######..........##########",
"##########..........##########...#######..........##########",
"##########........................................##########",
"##########........................................##########",
"##########........................................##########",
"##########..........##########...#######..........##########",
"##########..........##########...#######..........##########",
"#############...##############...###########...#############",
"#############...##############...###########...#############",
"#############...##############...###########...#############",
"#############...##############...###########...#############",
"#############...##..........##...###...........#############",
"##################..........########..........##############",
"##################..........########..........##############",
"##################............................##############",
"##################............................##############",
"##################............................##############",
"##################..........########..........##############",
"##################..........########..........##############",
"######################...###############...#################",
"######################...###############...#################",
"######################...#..........####...#################",
"##########################..........########################",
"##########################..........########################",
"##########################..........########################",
"##########################..........########################",
"############################################################",
"############################################################",
};
static const char *AU_M6[AUH] = {
"############################################################",
"############################################################",
"############################################################",
"############################################################",
"##########################............######################",
"##########################............######################",
"####............##########............######################",
"####............##########............######################",
"####....................................................####",
"####....................................................####",
"####....................................................####",
"####............##########............########..........####",
"####............##########............########..........####",
"####............###############...############..........####",
"####............###############...############..........####",
"#########...###################...############..........####",
"#########...###################...############..........####",
"#########...###################...##########################",
"#########...###################...##########################",
"#########...###################...##########################",
"#########...###################...##########################",
"#########...###################...##########################",
"#########...##################............##################",
"#########...##################............##################",
"#########...##################.................#############",
"#########...##################.................#############",
"########..............########.................#############",
"########..............########............##################",
"########..............########............####..........####",
"########................................................####",
"########................................................####",
"########................................................####",
"########..............########################..........####",
"########..............########################..........####",
"########................................................####",
"########................................................####",
"######################..................................####",
"##############################################..........####",
"############################################################",
"############################################################",
"############################################################",
"############################################################",
"############################################################",
"############################################################",
"############################################################",
};
// ---- V5.26 : metadata des 6 cartes (taches, ventilos, bouton, cameras, vitals, spawn, couleurs)
static const char **AU_MAPS[6] = { AU_M1, AU_M2, AU_M3, AU_M4, AU_M5, AU_M6 };
static const char *AU_MAPNAMES[6] = { "SKELD", "MIRA", "POLUS", "AIRSHIP", "SOUS-MARIN", "FUNGLE" };
static const int AU_TXS[6][12] = { {5,24,34,49,56,48,36,23,14,4,42,29}, {27,14,49,12,47,32,13,48,11,46,27,12}, {7,30,43,8,27,46,9,32,47,10,27,46}, {7,30,53,6,31,46,17,42,7,30,53,10}, {29,16,43,24,39,36,31,16,43,24,39,36}, {7,32,49,10,37,52,9,34,49,18,39,52} };
static const int AU_TYS[6][12] = { {6,4,12,4,25,40,34,40,33,22,21,18}, {20,8,8,33,33,21,10,10,35,35,23,12}, {6,6,7,19,21,20,34,34,38,9,7,7}, {6,6,6,23,21,24,37,37,9,9,9,25}, {6,18,19,31,32,35,8,21,18,30,31,35}, {8,6,11,29,25,32,10,8,14,30,27,35} };
static const int AU_VXS[6][5] = { {5,13,55,56,34}, {12,49,14,43,28}, {28,43,10,23,46}, {28,53,8,27,52}, {14,43,20,43,36}, {30,49,12,33,52} };
static const int AU_VYS[6][5] = { {13,26,33,19,26}, {8,8,33,34,22}, {6,7,19,22,21}, {6,6,23,22,24}, {18,19,32,32,35}, {6,11,29,26,33} };
// V5.26b : POI recalcules par BFS (case praticable ET atteignable depuis le spawn)
static const int AU_BTX[6] = {29,35,29,29,23,36};
static const int AU_BTY[6] = {14,21,15,15,21,22};
static const int AU_VITX[6] = {12,8,8,10,18,8};
static const int AU_VITY[6] = {35,38,38,38,35,35};
static const int AU_SPAWX[6] = {29,29,29,29,29,30};
static const int AU_SPAWY[6] = {20,21,21,21,21,22};
static const uint32_t AU_WALLC[6] = {0x2E3A4A, 0x3A4A5A, 0x8A5C3B, 0x4A4A55, 0x1E3A5A, 0x2E5A2E};
static const uint32_t AU_FLOOC[6] = {0x8B94A3, 0xA8D8B0, 0xE8E4DA, 0x9BA3B0, 0x6E8FA8, 0xB0C98A};
// la carte courante (pointeurs mis en place au lancement de la partie)
static const char **AU_MAP;
static const int *AU_TX, *AU_TY, *AU_VX, *AU_VY;
static float auBtnX, auBtnY;
static int auVitXc, auVitYc;
static uint32_t auWallC, auFlooC;
// couleurs des 14 PNJ possibles (le joueur reste cyan/rouge)
static const uint32_t AU_BOTC[AU_MAXBOT] = {0x132ED1,0x117F2D,0xED54BA,0xF07D0D,0xF5C518,0x3F474E,0xD6E0F0,0x6B2FBB,0x8A5C22,0x50EF39,0x6B2B3C,0x8397A7,0xE8A15C,0xF0E7A8};
static const char *AU_NAMES[AU_MAXBOT] = {"BLEU","VERT","ROSE","ORANGE","JAUNE","NOIR","BLANC","VIOLET","MARRON","LIME","BORDEAUX","GRIS","PECHE","BANANE"};
static void drawHat(int x, int y, int h, KDColor c) { // V5.30 : petit chapeau au-dessus du bean
  if (h == 0) return; // tete nue
  if (h == 1) { // cone
    fRect(x - 1, y - 11, 3, 2, c);
    fRect(x, y - 13, 1, 2, c);
  } else if (h == 2) { // casquette
    fRect(x - 5, y - 10, 10, 3, c);
    fRect(x + 3, y - 8, 3, 2, c);
  } else if (h == 3) { // haut-de-forme
    fRect(x - 4, y - 12, 8, 5, c);
    fRect(x - 6, y - 8, 12, 2, c);
  } else { // antenne
    fRect(x, y - 13, 1, 4, C_WHT);
    fRect(x - 1, y - 15, 3, 2, c);
  }
}
static int auDoorTrapAt(int vcx, int vcy) { // V5.32 IA : ferme les portes AUTOUR de la proie (renvoie 1 si piege pose)
  auDoorN = 0;
  for (int r = 1; r <= 4; r++) {
    for (int dy2 = -r; dy2 <= r; dy2++)
      for (int dx2 = -r; dx2 <= r; dx2++) {
        if (abs(dx2) < r && abs(dy2) < r) continue; // on ne scanne que la bordure du rayon
        int X = vcx + dx2, Y = vcy + dy2;
        if (X < 1 || Y < 1 || X >= AUW - 1 || Y >= AUH - 1 || auCell(X, Y) == '#') continue;
        int walls = (auCell(X - 1, Y) == '#') + (auCell(X + 1, Y) == '#') + (auCell(X, Y - 1) == '#') + (auCell(X, Y + 1) == '#');
        if (walls < 2) continue; // il faut un pincement
        int ax = (auCell(X - 1, Y) != '#' || auCell(X + 1, Y) != '#') ? 1 : 0;
        int ay = 1 - ax;
        for (int k = -1; k <= 1; k++) {
          int X2 = X + ax * k, Y2 = Y + ay * k;
          if (auDoorN < 8 && X2 >= 0 && Y2 >= 0 && X2 < AUW && Y2 < AUH && auCell(X2, Y2) != '#') auDoorC[auDoorN++] = Y2 * AUW + X2;
        }
        return 1; // un seul point de fermeture suffit
      }
  }
  return 0;
}
static void auDoorSabotage() { // V5.30 : ferme un point de pincement de couloir (3 cases)
  auDoorN = 0;
  for (int tries = 0; tries < 60 && auDoorN == 0; tries++) {
    int cx = rnd(AUW), cy = rnd(AUH);
    if (auCell(cx, cy) == '#') continue;
    int walls = (auCell(cx - 1, cy) == '#') + (auCell(cx + 1, cy) == '#') + (auCell(cx, cy - 1) == '#') + (auCell(cx, cy + 1) == '#');
    if (walls < 2) continue; // pas un pincement
    int ddx2 = (auCell(cx - 1, cy) != '#' || auCell(cx + 1, cy) != '#') ? 1 : 0; // axe du couloir
    int ddy2 = 1 - ddx2;
    for (int k = -1; k <= 1; k++) {
      int X = cx + ddx2 * k, Y = cy + ddy2 * k;
      if (auDoorN < 8 && X >= 0 && Y >= 0 && X < AUW && Y < AUH && auCell(X, Y) != '#') auDoorC[auDoorN++] = Y * AUW + X;
    }
  }
}
static char auCell(int cx, int cy) {
  if (cx < 0 || cy < 0 || cx >= AUW || cy >= AUH) return '#';
  if (auDoorN > 0) for (int k = 0; k < auDoorN; k++) if (auDoorC[k] == cy * AUW + cx) return '#'; // V5.30 : porte fermee
  return AU_MAP[cy][cx];
}
static bool auSolidPx(float x, float y) { return auCell((int)(x / 8.0f), (int)(y / 8.0f)) == '#'; }
static bool auClearLine(float x0, float y0, float x1, float y1) { // ligne de visee libre ?
  float dx = x1 - x0, dy = y1 - y0, dl = sqrtf(dx * dx + dy * dy);
  if (dl < 1.0f) return true;
  int steps = (int)(dl / 4.0f) + 1;
  for (int i = 1; i < steps; i++) {
    float t = (float)i / (float)steps;
    if (auSolidPx(x0 + dx * t, y0 + dy * t)) return false;
  }
  return true;
}
static bool auFree(float x, float y) { // les 4 coins du bean
  if (auSolidPx(x - 3.5f, y - 3.5f) || auSolidPx(x + 3.5f, y - 3.5f)) return false;
  if (auSolidPx(x - 3.5f, y + 4.0f) || auSolidPx(x + 3.5f, y + 4.0f)) return false;
  return true;
}
// un petit crewmate/imposteur : corps arrondi + visiere + sac a dos
static void drawBean(int x, int y, KDColor col, bool left) {
  KDColor dark = KDColor::RGB24(0x1B1B25);
  fRect(x - 5, y + 4, 4, 3, col); fRect(x + 2, y + 4, 4, 3, col); // jambes
  fRect(left ? x + 4 : x - 8, y - 4, 4, 8, col);                 // sac a dos
  fCirc(x, y - 1, 6, col);
  oCirc(x, y - 1, 6, col, dark);
  fRect(left ? x - 5 : x - 1, y - 4, 6, 4, KDColor::RGB24(0x9ACAD5)); // visiere
  fRect(left ? x - 5 : x - 1, y - 4, 6, 1, KDColor::RGB24(0xC6F0F7));
}
static void drawCorpse(int x, int y, KDColor col) {
  oCirc(x - 2, y + 2, 5, col, KDColor::RGB24(0x1B1B25));
  fRect(x - 5, y + 1, 3, 2, KDColor::RGB24(0xF2F2F2)); // os qui depasse
  fCirc(x - 6, y, 2, KDColor::RGB24(0xF2F2F2));
  fCirc(x - 6, y + 3, 2, KDColor::RGB24(0xF2F2F2));
}
static void auLine(int x0, int y0, int x1, int y1, KDColor c) { // Bresenham
  int dx = x1 - x0, dy = y1 - y0, sx = (dx > 0) - (dx < 0), sy = (dy > 0) - (dy < 0);
  dx = dx < 0 ? -dx : dx; dy = dy < 0 ? -dy : dy;
  int err = dx - dy;
  while (true) {
    if (x0 >= 0 && y0 >= 0 && x0 < SW && y0 < SH) px(x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = err * 2;
    if (e2 > -dy) { err -= dy; x0 += sx; }
    if (e2 < dx) { err += dx; y0 += sy; }
  }
}

struct AuBot {
  float x, y, dx, dy;      // position + destination finale
  int path[96], pi, pn;    // V5.26 : chemin BFS case par case (aucun mur a traverser)
  int state;               // 0 marche, 1 tache, 3 panique
  float t;                 // timer de tache
  bool alive, panic, witness, imp;
  float flee, stuck, spdF;   // V5.26b : vitesse individuelle (0.85 a 1.15)
  int accuse;              // dernier vu sur la scene (-1, 0..13 bot, 14 joueur)
  int tasks[4], taskI;
  bool ghost;
};
static AuBot auB[AU_MAXBOT];
static int auBotN = 5;      // V5.26 : nombre de PNJ (5 a 14, joueurs 6 a 15)
static float auBodyX[16], auBodyY[16]; static uint32_t auBodyC[16]; static int auBodyN = 0;
static int auDoorC[8]; static int auDoorN = 0; // V5.30 : portes fermees (overlay, la carte n'est pas modifiee)
static int auHats[AU_MAXBOT + 1]; // V5.30 : chapeaux (0 rien, 1 cone, 2 casquette, 3 haut-de-forme, 4 antenne)
static bool auMeImp = false; // V5.32 : le joueur est-il imposteur ? (interdit de voter contre un complice)
static bool auVotable(int s) { // V5.32 : un imposteur ne peut JAMAIS voter contre son complice
  if (s == AU_MAXBOT) return true; // PASSER (securite)
  return (s < auBotN) && auB[s].alive && !(auMeImp && auB[s].imp);
}
static int auBodySus[16];
static float auTaskBar = 0.0f;

// ---- V5.26 : BFS sur la grille — remplace les waypoints (plus jamais de mur traverse) ----
static int auPar[AUW * AUH];
static int auQ[AUW * AUH];
static void auBFS(int sx, int sy) {
  for (int i = 0; i < AUW * AUH; i++) auPar[i] = -2;
  int head = 0, tail = 0;
  if (sx < 0 || sy < 0 || sx >= AUW || sy >= AUH || auCell(sx, sy) == '#') return;
  auQ[tail++] = sy * AUW + sx; auPar[sy * AUW + sx] = -1;
  while (head < tail) {
    int c = auQ[head++]; int x = c % AUW, y = c / AUW;
    if (x > 0 && auPar[c - 1] == -2 && auCell(x - 1, y) != '#') { auPar[c - 1] = c; auQ[tail++] = c - 1; }
    if (x < AUW - 1 && auPar[c + 1] == -2 && auCell(x + 1, y) != '#') { auPar[c + 1] = c; auQ[tail++] = c + 1; }
    if (y > 0 && auPar[c - AUW] == -2 && auCell(x, y - 1) != '#') { auPar[c - AUW] = c; auQ[tail++] = c - AUW; }
    if (y < AUH - 1 && auPar[c + AUW] == -2 && auCell(x, y + 1) != '#') { auPar[c + AUW] = c; auQ[tail++] = c + AUW; }
  }
}
static void auSetDest(AuBot &b, float tx, float ty) {
  b.dx = tx; b.dy = ty;
  int sx = (int)(b.x / 8.0f), sy = (int)(b.y / 8.0f);
  int gx = (int)(tx / 8.0f), gy = (int)(ty / 8.0f);
  if (gx < 0 || gy < 0 || gx >= AUW || gy >= AUH) { b.pn = 0; b.pi = 0; return; }
  auBFS(sx, sy);
  b.pn = 0; b.pi = 0;
  int c = gy * AUW + gx;
  if (auPar[c] == -2) { b.pn = 0; b.pi = 0; b.dx = b.x; b.dy = b.y; return; } // V5.26b : injoignable -> arret propre (jamais gele)
  static int rev[AUW * AUH];
  int n = 0;
  while (c >= 0 && n < AUW * AUH) { rev[n++] = c; c = auPar[c]; }
  for (int i = n - 2; i >= 0 && b.pn < 96; i--) b.path[b.pn++] = rev[i];
}
static void auBotWalk(AuBot &b, float dt, float spd) {
  if (!auFree(b.x, b.y)) { // coince dans un mur : on le remet sur la case libre la plus proche
    int bx = (int)(b.x / 8.0f), by = (int)(b.y / 8.0f);
    for (int r = 0; r <= 2; r++)
      for (int dy2 = -r; dy2 <= r; dy2++)
        for (int dx2 = -r; dx2 <= r; dx2++) {
          int X = bx + dx2, Y = by + dy2;
          if (X >= 0 && Y >= 0 && X < AUW && Y < AUH && auCell(X, Y) != '#') {
            b.x = X * 8.0f + 4.0f; b.y = Y * 8.0f + 4.0f;
            auSetDest(b, b.dx, b.dy);
            return;
          }
        }
    return;
  }
  float tx, ty;
  if (b.pi < b.pn) { int c = b.path[b.pi]; tx = (c % AUW) * 8.0f + 4.0f; ty = (c / AUW) * 8.0f + 4.0f; }
  else { tx = b.dx; ty = b.dy; }
  float ddx = tx - b.x, ddy = ty - b.y, dl = sqrtf(ddx * ddx + ddy * ddy);
  if (dl < 2.0f) { if (b.pi < b.pn) b.pi++; return; }
  float nx = b.x + ddx / dl * spd * dt, ny = b.y + ddy / dl * spd * dt;
  if (auFree(nx, ny)) { b.x = nx; b.y = ny; b.stuck = 0; return; }
  if (auFree(nx, b.y)) { b.x = nx; b.stuck = 0; return; }
  if (auFree(b.x, ny)) { b.y = ny; b.stuck = 0; return; }
  // V5.28 IA : CONTOURNEMENT — il LONGE le mur au lieu de rester bloque
  float ux = ddx / dl, uy = ddy / dl; // direction voulue
  float oxp = -uy, oyp = ux;          // perpendiculaire (glisse le long du mur)
  float step = spd * dt;
  if (auFree(b.x + oxp * step, b.y + oyp * step)) { b.x += oxp * step; b.y += oyp * step; b.stuck = 0; return; }
  if (auFree(b.x - oxp * step, b.y - oyp * step)) { b.x -= oxp * step; b.y -= oyp * step; b.stuck = 0; return; }
  b.stuck += dt;
  if (b.stuck > 0.5f) { // secours rapide : il se repositionne sur la case LIBRE voisine la plus proche du but
    b.stuck = 0;
    int bx = (int)(b.x / 8.0f), by = (int)(b.y / 8.0f);
    float bestD = 1e9; int bestX = -1, bestY = -1;
    for (int dy2 = -1; dy2 <= 1; dy2++)
      for (int dx2 = -1; dx2 <= 1; dx2++) {
        int X = bx + dx2, Y = by + dy2;
        if (X < 0 || Y < 0 || X >= AUW || Y >= AUH || auCell(X, Y) == '#') continue;
        float fx = X * 8.0f + 4.0f, fy = Y * 8.0f + 4.0f;
        float ddx2 = fx - b.dx, ddy2 = fy - b.dy;
        float d2 = ddx2 * ddx2 + ddy2 * ddy2;
        if (d2 < bestD) { bestD = d2; bestX = X; bestY = Y; }
      }
    if (bestX >= 0) { b.x = bestX * 8.0f + 4.0f; b.y = bestY * 8.0f + 4.0f; auSetDest(b, b.dx, b.dy); }
  }
}
static bool auBotArrived(AuBot &b) {
  float ddx = b.dx - b.x, ddy = b.dy - b.y;
  return (b.pi >= b.pn) && (ddx * ddx + ddy * ddy < 4.0f);
}
static void auPanicTo(AuBot &b) { // il fonce au bouton pour signaler
  b.panic = true; b.state = 3;
  auSetDest(b, auBtnX, auBtnY);
}
static void auNextTask(AuBot &b) { // sa tache suivante comme destination
  b.state = 0;
  if (b.taskI < 4) {
    // V5.32 IA : SERIE LOGIQUE — il va a la tache la plus PROCHE d'abord (trajet naturel)
    int bestK = b.taskI; float bdd = 1e9;
    for (int k = b.taskI; k < 4; k++) {
      float tx2 = AU_TX[b.tasks[k]] * 8.0f + 4.0f, ty2 = AU_TY[b.tasks[k]] * 8.0f + 4.0f;
      float d2 = (tx2 - b.x) * (tx2 - b.x) + (ty2 - b.y) * (ty2 - b.y);
      if (d2 < bdd) { bdd = d2; bestK = k; }
    }
    if (bestK != b.taskI) { int tmp = b.tasks[b.taskI]; b.tasks[b.taskI] = b.tasks[bestK]; b.tasks[bestK] = tmp; }
    // V5.28 IA : ANTI-BOUCHON — deux bots ne visent pas la meme station en meme temps
    for (int tries = 0; tries < 3 && b.taskI < 3; tries++) {
      float tx = AU_TX[b.tasks[b.taskI]] * 8.0f + 4.0f, ty = AU_TY[b.tasks[b.taskI]] * 8.0f + 4.0f;
      bool taken = false;
      for (int w = 0; w < auBotN; w++) {
        if (&auB[w] == &b || !auB[w].alive) continue;
        float wx = auB[w].dx - tx, wy = auB[w].dy - ty;
        if (wx * wx + wy * wy < 16.0f) { taken = true; break; }
      }
      if (!taken) break;
      int swap = b.taskI + 1 + rnd(3 - b.taskI);
      int tmp = b.tasks[b.taskI]; b.tasks[b.taskI] = b.tasks[swap]; b.tasks[swap] = tmp;
    }
    auSetDest(b, AU_TX[b.tasks[b.taskI]] * 8.0f + 4.0f, AU_TY[b.tasks[b.taskI]] * 8.0f + 4.0f);
  }
  else { // taches finies : il erre vers une case au hasard
    for (int tries = 0; tries < 20; tries++) {
      int cx = rnd(AUW), cy = rnd(AUH);
      if (auCell(cx, cy) != '#') { auSetDest(b, cx * 8.0f + 4.0f, cy * 8.0f + 4.0f); return; }
    }
    auSetDest(b, auBtnX, auBtnY);
  }
}
// ---- V5.26 : interfaces Among Us ----
static int auVoteScreen() { // renvoie l'index du bot vote, -1 si passer/annuler
  waitRelease();
  int nSlots = auBotN + 1; // les bots + PASSER
  int sel = 0; int res = -2;
  bool pU = false, pD = false, pL = false, pR = false, pO = false, pB = false;
  while (res == -2) {
    pollKeys();
    int col = sel % 5;
    if (kLeft && !pL && col > 0) { int s = sel - 1; while (s != auBotN && !auVotable(s)) s--; sel = s; }
    if (kRight && !pR && col < 4 && sel + 1 < nSlots) { int s = sel + 1; while (s != auBotN && !auVotable(s)) s++; if (s < nSlots) sel = s; }
    if (kUp && !pU && sel >= 5) { int s = sel - 5; if (auVotable(s)) sel = s; }
    if (kDown && !pD && sel + 5 < nSlots) { int s = sel + 5; if (auVotable(s)) sel = s; }
    if (keyOK && !pO) res = (sel == auBotN) ? -1 : (auVotable(sel) ? sel : -1);
    if (keyBack && !pB) res = -1;
    pU = kUp; pD = kDown; pL = kLeft; pR = kRight; pO = keyOK; pB = keyBack;
    fRect(0, 0, SW, SH, KDColor::RGB24(0x1B1B25));
    fRect(0, 0, SW, 20, KDColor::RGB24(0x2E3A4A)); fRect(0, 18, SW, 2, KDColor::RGB24(0xC51111));
    drawTextC(7, "QUI EST L'IMPOSTEUR ?", KDColor::RGB24(0xF5C518));
    for (int i = 0; i < nSlots; i++) {
      int bx = 4 + (i % 5) * 63, by = 26 + (i / 5) * 62;
      if (i == auBotN) { // PASSER, centre en bas
        bx = 134; by = 26 + 3 * 62;
        if (by > SH - 40) by = SH - 40;
      }
      rRect(bx, by, 56, 56, 6, (i == sel) ? C_YEL : KDColor::RGB24(0x252C3A));
      if (i == auBotN) { drawText(bx + 13, by + 26, "PASSER", (i == sel) ? C_YEL : KDColor::RGB24(0x8B94A3)); continue; }
      int cx = bx + 28, cy = by + 22;
      if (auB[i].alive) {
        drawBean(cx, cy, KDColor::RGB24(AU_BOTC[i]), false);
        drawHat(cx, cy, auHats[i], KDColor::RGB24(AU_BOTC[i])); // V5.30 : son chapeau sur l'ecran de vote
        if (auMeImp && auB[i].imp) fRing(cx, cy + 10, 7, KDColor::RGB24(0xC51111)); // V5.32 : complice visible mais NON votable
        drawText(bx + 4, by + 44, AU_NAMES[i], (i == sel) ? C_YEL : KDColor::RGB24(0xD6E0F0));
      } else {
        drawCorpse(cx, cy, KDColor::RGB24(AU_BOTC[i]));
        drawText(bx + 12, by + 44, "MORT", C_GRE);
      }
    }
    drawTextC(210, "OK:VOTER   RETOUR:PASSER", C_GRE);
    blit();
    Ion::Timing::msleep(30);
  }
  waitRelease();
  return res;
}
static void auEjectScreen(int bi, bool wasImp, int impsLeft) { // bi = -1 pour TOI
  const char *nm = (bi < 0) ? "TOI" : AU_NAMES[bi];
  uint32_t col = (bi < 0) ? 0x38FEDC : AU_BOTC[bi];
  int t = 0;
  while (t < 2800) {
    pollKeys();
    if (keyOK) break;
    fRect(0, 0, SW, SH, KDColor::RGB24(0x0A0A14));
    for (int s = 0; s < 40; s++) px((s * 53 + t / 25) % SW, (s * 37 + s * s) % SH, KDColor::RGB24(0x8B94A3));
    drawBean(SW - t / 10, 96 + (t / 40) % 10, KDColor::RGB24(col), false);
    drawTextC(30, "EJECTION...", C_WHT);
    drawTextC(130, nm, KDColor::RGB24(col));
    drawTextC(150, wasImp ? "ETAIT L'IMPOSTEUR !" : "N'ETAIT PAS L'IMPOSTEUR", wasImp ? KDColor::RGB24(0xC51111) : C_WHT);
    drawTextC(172, "IMPOSTEURS RESTANTS:", C_GRE);
    drawNum(SW / 2 + 92, 172, impsLeft, C_ORG);
    drawTextC(200, "OK:CONTINUER", C_GRE);
    blit();
    Ion::Timing::msleep(30);
    t += 30;
  }
  waitRelease();
}
static bool auWiringGame(int taskIdx) { // V5.26 : CABLAGE — renvoie true si complete
  waitRelease();
  static const uint32_t WC[3] = {0xC51111, 0xF5C518, 0x38FEDC};
  int perm[3] = {0, 1, 2};
  int s = taskIdx * 7 + 5;
  for (int i = 2; i > 0; i--) { int j = s % (i + 1); s = s * 13 + 5; int tmp = perm[i]; perm[i] = perm[j]; perm[j] = tmp; }
  bool done[3] = {false, false, false};
  int cur = 0, sel = 0, flashT = 0;
  bool ok = false, pU = false, pD = false, pO = false, pB = false;
  while (!ok) {
    pollKeys();
    if (kUp && !pU && sel > 0) sel--;
    if (kDown && !pD && sel < 2) sel++;
    if (keyOK && !pO) {
      if (perm[sel] == cur) { done[cur] = true; while (cur < 3 && done[cur]) cur++; if (cur >= 3) ok = true; }
      else flashT = 400;
    }
    if (keyBack && !pB) { waitRelease(); return false; }
    pU = kUp; pD = kDown; pO = keyOK; pB = keyBack;
    fRect(40, 40, 240, 130, KDColor::RGB24(0x1B1B25));
    if (flashT > 0) { flashT -= 33; fRect(40, 40, 240, 3, KDColor::RGB24(0xC51111)); fRect(40, 167, 240, 3, KDColor::RGB24(0xC51111)); }
    drawTextC(48, "CABLAGE : RELIE LES FILS", C_YEL);
    for (int w = 0; w < 3; w++) { // fils a gauche (ordre fixe), bornes a droite (melangees)
      int y = 80 + w * 30;
      fRect(70, y - 3, 10, 10, KDColor::RGB24(WC[w]));
      fRect(240, y - 3, 10, 10, KDColor::RGB24(WC[perm[w]]));
      if (w == sel) drawText(230, y - 3, ">", C_YEL);
      if (done[w]) auLine(80, y, 240, 80 + perm[w] * 30, KDColor::RGB24(WC[w]));
    }
    drawTextC(178, "FLECHES:CHOISIR  OK:BRANCHER", C_GRE);
    drawTextC(196, "RETOUR:ABANDONNER", C_GRE);
    blit();
    Ion::Timing::msleep(33);
  }
  waitRelease();
  return true;
}
static void auVitalsOverlay() { // etat de chaque PNJ en direct
  while (true) {
    pollKeys();
    if (!kExp || keyBack) break;
    bgPanel();
    drawTextC(16, "SIGNES VITAUX", C_YEL);
    int ry = 36;
    for (int i = 0; i < auBotN && ry < 200; i++) {
      drawText(24, ry, AU_NAMES[i], KDColor::RGB24(AU_BOTC[i]));
      drawText(120, ry, auB[i].alive ? "VIVANT" : "MORT", auB[i].alive ? KDColor::RGB24(0x38E05C) : KDColor::RGB24(0xC51111));
      ry += 13;
    }
    drawTextC(206, "EXE:QUITTER", C_GRE);
    blit();
    Ion::Timing::msleep(30);
  }
  waitRelease();
}

static void amongUsGame() {
  // ================== LOBBY V5.26 : joueurs, imposteurs, carte ==================
  int players = 10, imps = 2, mapSel = 0;
  {
    int row = 0; bool pU = false, pD = false, pL = false, pR = false, pOK = false, pB = false;
    waitRelease();
    bool started = false;
    while (!started) {
      pollKeys();
      if (kUp && !pU && row > 0) row--;
      if (kDown && !pD && row < 2) row++;
      if (kLeft && !pL) {
        if (row == 0 && players > 5) players--;
        if (row == 1 && imps > 1) imps--;
        if (row == 2 && mapSel > 0) mapSel--;
      }
      if (kRight && !pR) {
        if (row == 0 && players < 15) players++;
        if (row == 1 && imps < 3 && imps * 2 < players - 1) imps++;
        if (row == 2 && mapSel < 5) mapSel++;
      }
      if (keyOK && !pOK) { started = true; }
      if (keyBack && !pB) return;
      if (kHome) return;
      pU = kUp; pD = kDown; pL = kLeft; pR = kRight; pOK = keyOK; pB = keyBack;
      bgPanel();
      drawTextC(12, "AMONG US MICRO", C_YEL);
      for (int r = 0; r < 3; r++) fRect(36, 40 + r * 24, 248, 20, (r == row) ? KDColor::RGB24(0x2E3A4A) : KDColor::RGB24(0x1B1B25));
      drawText(56, 45, "< JOUEURS", C_WHT); drawText(216, 45, ">", C_WHT); drawNum(236, 45, players, C_YEL);
      drawText(56, 69, "< IMPOSTEURS", C_WHT); drawText(216, 69, ">", C_WHT); drawNum(236, 69, imps, C_YEL);
      drawText(56, 93, "< CARTE", C_WHT); drawText(216, 93, ">", C_WHT); drawText(236, 93, AU_MAPNAMES[mapSel], C_YEL);
      rRect(SW / 2 - 70, 140, 140, 22, 8, C_YEL);
      drawTextC(146, "OK JOUER", C_BLK);
      drawTextC(180, "RETOUR:ANNULER", C_GRE);
      blit();
      Ion::Timing::msleep(40);
    }
    waitRelease();
  }
  // ================== MISE EN PLACE ==================
  auBotN = players - 1;
  for (int i = 0; i <= auBotN; i++) auHats[i] = rnd(5); // V5.30 : chapeaux au hasard (le dernier = joueur)
  AU_MAP = AU_MAPS[mapSel];
  AU_TX = AU_TXS[mapSel]; AU_TY = AU_TYS[mapSel];
  AU_VX = AU_VXS[mapSel]; AU_VY = AU_VYS[mapSel];
  auBtnX = AU_BTX[mapSel] * 8.0f + 4.0f; auBtnY = AU_BTY[mapSel] * 8.0f + 4.0f;
  auVitXc = AU_VITX[mapSel]; auVitYc = AU_VITY[mapSel];
  auWallC = AU_WALLC[mapSel]; auFlooC = AU_FLOOC[mapSel];
  auBodyN = 0; auTaskBar = 0.0f;
  float px = AU_SPAWX[mapSel] * 8.0f + 4.0f, py = AU_SPAWY[mapSel] * 8.0f + 4.0f;
  int playerDir = 1, pTasks[4], pTaskI = 0;
  float pTaskProg = 0.0f, killCd = 6.0f, ventCd = 0.0f, gTime = 0.0f, meetT = 0.0f, introT = 3.0f;
  float lightT = 0.0f, sabCd = 0.0f, fixProg = 0.0f; // V5.26 : sabotage lumieres
  float doorT = 0.0f, o2T = 0.0f, o2ProgA = 0.0f, o2ProgB = 0.0f; // V5.30 : portes + oxygene
  int o2FixA = 0, o2FixB = 0; // V5.30 : les 2 points de reparation O2
  bool gSus = false, pGhost = false, doTally = false;
  int myVote = -1;
  float auZoom = 1.0f;
  float fxT = 0, fxX = 0, fxY = 0; int fxType = 0; // V5.29 : animations (kill, tache ok)
  float dieT = 0; // V5.29 : animation de TA mort
  for (int i = 0; i < 4; i++) { // MES taches (si equipage)
    int t = rnd(12);
    for (int j = 0; j < i; j++) if (pTasks[j] == t) { t = (t + 1 + rnd(11)) % 12; j = -1; }
    pTasks[i] = t;
  }
  bool meImp = (rnd(100) < (100 * imps / players));
  auMeImp = meImp; // V5.32 : l'ecran de vote saura qu'on ne vote pas contre un complice
  int impsAlive = imps;
  auBFS(AU_SPAWX[mapSel], AU_SPAWY[mapSel]); // pour repartir les spawns
  int impBotsLeft = meImp ? imps - 1 : imps;
  for (int i = 0; i < auBotN; i++) {
    AuBot &b = auB[i];
    b.alive = true; b.panic = false; b.witness = false; b.flee = 0; b.stuck = 0;
    b.accuse = -1; b.ghost = false; b.imp = false; b.taskI = 0; b.t = 0; b.pi = 0; b.pn = 0;
    b.spdF = 0.85f + rnd(4) * 0.1f;
    b.x = px; b.y = py;
    for (int tries = 0; tries < 40; tries++) { // spawn sur une case atteignable
      int c = rnd(AUW * AUH);
      if (auPar[c] != -2) { b.x = (c % AUW) * 8.0f + 4.0f; b.y = (c / AUW) * 8.0f + 4.0f; break; }
    }
    for (int k = 0; k < 4; k++) { // SES 4 taches personnelles
      int t = rnd(12);
      for (int j = 0; j < k; j++) if (b.tasks[j] == t) { t = (t + 1 + rnd(11)) % 12; j = -1; }
      b.tasks[k] = t;
    }
    auNextTask(b);
  }
  for (int g = 0; g < 200 && impBotsLeft > 0; g++) { int i = rnd(auBotN); if (!auB[i].imp) { auB[i].imp = true; impBotsLeft--; } }
  int crew = players - imps;
  float totalTasks = crew * 4.0f; // V5.26 : l'EQUIPAGE entier doit finir
  bool pOK = false, pExp = false, pBack = false, pPlus = false, pMn = false, pDiv = false;
  int endScreen = 0; // 1 imp gagne, 2 taches finies, 3 imp ejete, 4 abandon, 5 equipage gagne
  float dt = 0.033f;
  float visR = 100.0f;
  while (!endScreen) {
    pollKeys();
    if (kHome) { endScreen = 4; break; }
    gTime += dt;
    if (introT > 0) { // revelation du role
      introT -= dt;
      if (keyOK && !pOK) introT = 0;
      fRect(0, 90, SW, 60, KDColor::RGB24(0x1B1B25));
      drawTextC(100, AU_MAPNAMES[mapSel], C_GRE);
      drawTextC(112, meImp ? "TU ES IMPOSTEUR" : "TU ES MEMBRE D'EQUIPAGE", meImp ? KDColor::RGB24(0xC51111) : KDColor::RGB24(0x38FEDC));
      drawTextC(128, meImp ? "ELIMINE TOUT LE MONDE" : "FAIS TES 4 TACHES", C_GRE);
      pOK = keyOK; pExp = kExp; pBack = keyBack; pPlus = kPlus; pMn = kMinus; pDiv = kDiv;
      blit();
      Ion::Timing::msleep(30);
      continue;
    }
    if (dieT > 0) { // V5.29 : ANIMATION quand TU MEURS (flash rouge + tremblement)
      dieT -= dt;
      if (dieT <= 0) { pGhost = true; continue; } // V5.29 : tu deviens FANTOME (comme le vrai jeu)
      fRect(0, 0, SW, SH, KDColor::RGB24(0x1B1B25));
      int shake = (dieT > 1.1f) ? (rnd(7) - 3) : 0; // ca tremble au moment du kill
      if (((int)(dieT * 8.0f)) % 2) fRect(0, 0, SW, SH, KDColor::RGB24(0x3A0A0A)); // flash rouge
      drawBean(SW / 2 + shake, 84 + shake, meImp ? KDColor::RGB24(0xC51111) : KDColor::RGB24(0x38FEDC), false);
      fRing(SW / 2 + shake, 84 + shake, 9, KDColor::RGB24(0xC51111)); // aura de mort
      drawTextC(120 + shake, "TU AS ETE ELIMINE !", KDColor::RGB24(0xC51111));
      drawTextC(140, "l'imposteur rode encore...", C_GRE);
      drawTextC(160, "devient fantome pour finir tes taches", C_GRE);
      pOK = keyOK; pExp = kExp; pBack = keyBack; pPlus = kPlus; pMn = kMinus; pDiv = kDiv;
      blit();
      Ion::Timing::msleep(30);
      continue;
    }
    // ---- timers ----
    killCd -= dt; if (killCd < 0) killCd = 0;
    ventCd -= dt; if (ventCd < 0) ventCd = 0;
    sabCd -= dt; if (sabCd < 0) sabCd = 0;
    lightT -= dt; if (lightT < 0) lightT = 0;
    if (doorT > 0) { doorT -= dt; if (doorT <= 0) { auDoorN = 0; for (int q = 0; q < auBotN; q++) auB[q].pn = 0; } } // V5.30 : les portes se rouvrent, les bots recalculent
    if (o2T > 0) { o2T -= dt; if (o2T <= 0) { endScreen = 6; } } // V5.30 : oxygene epuise = victoire imposteur
    visR = (lightT > 0 && !meImp) ? (45.0f + ((int)(gTime * 5.0f) % 2) * 10.0f) : 100.0f; // V5.29 : vision qui GRESILLE pendant le sabotage
    if (meetT > 0) { // REUNION : tout le monde attend le vote
      meetT -= dt;
      if (meetT <= 0) doTally = true;
    } else {
      // ---- deplacement joueur ----
      float spd = 68.0f * dt;
      float dx = 0, dy = 0;
      if (kLeft) dx -= spd;
      if (kRight) dx += spd;
      if (kUp) dy -= spd;
      if (kDown) dy += spd;
      if (dx < 0) playerDir = 0;
      if (dx > 0) playerDir = 1;
      if (pGhost) { // le fantome traverse TOUT (mais pas la carte)
        px += dx; py += dy;
        if (px < 4) px = 4;
        if (px > AUW * 8 - 4) px = AUW * 8 - 4;
        if (py < 4) py = 4;
        if (py > AUH * 8 - 4) py = AUH * 8 - 4;
      } else {
        if (dx && auFree(px + dx, py)) px += dx;
        if (dy && auFree(px, py + dy)) py += dy;
      }
      if (kMinus && !pMn) auZoom = (auZoom < 1.0f) ? 1.0f : 0.5f; // zoom arriere
      // ---- OK : TUER (imposteur), TACHE (equipage), BOUTON D'URGENCE ----
      if (keyOK) {
        if (meImp && !pGhost) {
          if (!pOK && killCd <= 0) {
            float bd = 1e9; int bi = -1;
            for (int i = 0; i < auBotN; i++) {
              if (!auB[i].alive || auB[i].imp) continue; // V5.28 : JAMAIS son complice imposteur
              float ddx = auB[i].x - px, ddy = auB[i].y - py;
              float d2 = ddx * ddx + ddy * ddy;
              if (d2 < bd) { bd = d2; bi = i; }
            }
            if (bi >= 0 && bd < 17 * 17) {
              auB[bi].alive = false; auB[bi].ghost = true;
              if (auBodyN < 16) {
                auBodyX[auBodyN] = auB[bi].x; auBodyY[auBodyN] = auB[bi].y;
                auBodyC[auBodyN] = AU_BOTC[bi];
                auBodySus[auBodyN] = AU_MAXBOT; auBodyN++; // c'est TOI le dernier vu
              }
              auKills++; killCd = 10.0f;
              fxT = 0.5f; fxType = 1; fxX = auB[bi].x; fxY = auB[bi].y; // V5.29 : ANIMATION de kill
              for (int w = 0; w < auBotN; w++) { // TEMOIN a moins de 48 px : il accuse LE KILLEUR
                if (!auB[w].alive || auB[w].imp) continue; // V5.28 : un imposteur ne panique JAMAIS
                float wx = auB[w].x - px, wy = auB[w].y - py;
                if (wx * wx + wy * wy < 48 * 48) { auB[w].witness = true; auB[w].accuse = AU_MAXBOT; auPanicTo(auB[w]); }
              }
              bool anyAlive = false;
              for (int i = 0; i < auBotN; i++) if (auB[i].alive) anyAlive = true;
              if (!anyAlive) endScreen = 1;
            }
          }
        } else if (!meImp && pTaskI < 4) { // FAIRE SA TACHE
          float tx = AU_TX[pTasks[pTaskI]] * 8.0f + 4.0f, ty = AU_TY[pTasks[pTaskI]] * 8.0f + 4.0f;
          float ddx = tx - px, ddy = ty - py;
          if (ddx * ddx + ddy * ddy < 13 * 13) {
            if (pTasks[pTaskI] % 3 == 0) { // V5.26 : une tache sur trois = CABLAGE
              if (!pOK && auWiringGame(pTasks[pTaskI])) {
                pTaskI++; auTaskBar += 1.0f / totalTasks;
                fxT = 0.45f; fxType = 2; fxX = px; fxY = py; // V5.29 : ANIMATION tache ok
              }
            } else {
              pTaskProg += dt / 4.0f;
              if (pTaskProg >= 1.0f) { pTaskProg = 0; pTaskI++; auTaskBar += 1.0f / totalTasks;
                fxT = 0.45f; fxType = 2; fxX = px; fxY = py; } // V5.29 : ANIMATION tache ok
            }
          } else { pTaskProg = 0; }
        }
        // BOUTON D'URGENCE (equipage vivant, pas en reunion)
        if (!meImp && !pGhost && !pOK && meetT <= 0) {
          float bdx = auBtnX - px, bdy = auBtnY - py;
          if (bdx * bdx + bdy * bdy < 14 * 14) meetT = 12.0f;
        }
        // REPARER LES LUMIERES (salle des vitals) quand sabotage actif
        if (!meImp && !pGhost && lightT > 0) {
          float fdx = auVitXc * 8.0f + 4.0f - px, fdy = auVitYc * 8.0f + 4.0f - py;
          if (fdx * fdx + fdy * fdy < 13 * 13) {
            fixProg += dt / 2.0f;
            if (fixProg >= 1.0f) { fixProg = 0; lightT = 0; }
          } else fixProg = 0;
        } else fixProg = 0;
        // V5.30 : reparer l'OXYGENE — 2 POINTS (vitals + bouton), tache a deux par excellence
        if (!meImp && !pGhost && o2T > 0) {
          float adx = auVitXc * 8.0f + 4.0f - px, ady = auVitYc * 8.0f + 4.0f - py;
          float bdx2 = auBtnX - px, bdy2 = auBtnY - py;
          if (o2FixA == 0 && adx * adx + ady * ady < 13 * 13) {
            o2ProgA += dt / 2.0f;
            if (o2ProgA >= 1.0f) { o2ProgA = 0; o2FixA = 1; if (o2FixB) o2T = 0; }
          } else if (o2FixB == 0 && bdx2 * bdx2 + bdy2 * bdy2 < 13 * 13) {
            o2ProgB += dt / 2.0f;
            if (o2ProgB >= 1.0f) { o2ProgB = 0; o2FixB = 1; if (o2FixA) o2T = 0; }
          }
        }
      } else fixProg = 0;
      // ---- EXE : ventilos (imposteur) / VITALS (equipage) ----
      if (meImp && !pGhost && kExp && !pExp && ventCd <= 0) {
        for (int i = 0; i < 5; i++) {
          float vx = AU_VX[i] * 8.0f + 4.0f, vy = AU_VY[i] * 8.0f + 4.0f;
          float ddx = vx - px, ddy = vy - py;
          if (ddx * ddx + ddy * ddy < 11 * 11) {
            int j = (i + 1 + rnd(4)) % 5;
            px = AU_VX[j] * 8.0f + 4.0f; py = AU_VY[j] * 8.0f + 4.0f;
            ventCd = 1.2f; break;
          }
        }
      }
      if (!meImp && !pGhost && kExp) { // V5.29 : plus de cameras, EXE = VITALS
        float vdx = auVitXc * 8.0f + 4.0f - px, vdy = auVitYc * 8.0f + 4.0f - py;
        if (vdx * vdx + vdy * vdy < 14 * 14) auVitalsOverlay();
      }
      // ---- DIV : SABOTAGE DES LUMIERES (imposteur joueur) ----
      if (meImp && !pGhost && kDiv && !pDiv && sabCd <= 0) { // V5.30 : sabotage ALEATOIRE parmi 3
        int s = rnd(3);
        if (s == 0) lightT = 25.0f;
        else if (s == 1) {
          auDoorSabotage();
          for (int k = 0; k < auDoorN; k++) { // jamais sur TA case ni celle d'un bot
            int dc = auDoorC[k];
            bool onMe = ((int)(px / 8.0f) == dc % AUW && (int)(py / 8.0f) == dc / AUW);
            bool onBot = false;
            for (int q = 0; q < auBotN; q++) if (auB[q].alive && (int)(auB[q].x / 8.0f) == dc % AUW && (int)(auB[q].y / 8.0f) == dc / AUW) onBot = true;
            if (onMe || onBot) { auDoorC[k] = auDoorC[auDoorN - 1]; auDoorN--; k--; }
          }
          doorT = 10.0f;
        }
        else { o2T = 30.0f; o2FixA = 0; o2FixB = 0; }
        sabCd = 35.0f;
      }
    }
    // ---- VOTE (+) : reunion instantanee avec interface de choix ----
    if (kPlus && !pPlus && !endScreen && !pGhost) {
      if (gSus) { pGhost = true; gSus = false; auEjectScreen(-1, meImp, impsAlive - (meImp ? 1 : 0)); }
      else {
        int v = auVoteScreen();
        if (v >= 0 || meetT > 0) { myVote = v; doTally = true; }
      }
    }
    // ---- pause ----
    if (keyBack && !pBack && !endScreen) {
      waitRelease();
      bool resumed = false;
      while (!resumed) {
        pollKeys();
        if (keyOK) { waitRelease(); resumed = true; }
        else if (keyBack) { endScreen = 4; break; }
        Ion::Timing::msleep(40);
      }
    }
    // ---- qui traine pres de chaque corps ? ----
    for (int c = 0; c < auBodyN; c++)
      for (int v = 0; v <= auBotN; v++) {
        float vx0 = (v == auBotN) ? px : auB[v].x, vy0 = (v == auBotN) ? py : auB[v].y;
        bool va = (v == auBotN) ? !pGhost : auB[v].alive;
        if (!va) continue;
        float ddx = auBodyX[c] - vx0, ddy = auBodyY[c] - vy0;
        if (ddx * ddx + ddy * ddy < 40 * 40) auBodySus[c] = (v == auBotN) ? AU_MAXBOT : v;
      }
    // ================== BOOTS : IA des PNJ ==================
    if (meetT <= 0 && !doTally) {
      for (int i = 0; i < auBotN; i++) {
        AuBot &b = auB[i];
        if (!b.alive) { // FANTOME errant : traverse les murs
          b.t -= dt;
          if (b.t <= 0) {
            b.t = 1.5f + rnd(20) * 0.1f;
            b.dx = rnd(3) - 1; b.dy = rnd(3) - 1;
            if (!b.dx && !b.dy) b.dx = 1;
          }
          float mv = 45.0f * dt;
          b.x += b.dx * mv; b.y += b.dy * mv;
          if (b.x < 4 || b.x > AUW * 8 - 4) { b.dx = -b.dx; b.x += b.dx * mv * 2; }
          if (b.y < 4 || b.y > AUH * 8 - 4) { b.dy = -b.dy; b.y += b.dy * mv * 2; }
          if (b.taskI < 4) { b.taskI = 4; } // ses taches sont mortes avec lui
          continue;
        }
        b.t -= dt;
        if (b.stuck > 0) b.stuck -= dt;
        if (b.flee > 0) b.flee -= dt;
        if (b.panic) { // panique : fonce au bouton d'urgence
          if (b.pn == 0 || b.dx != auBtnX || b.dy != auBtnY) auSetDest(b, auBtnX, auBtnY);
          if (auBotArrived(b)) {
            b.panic = false;
            meetT = 12.0f; // REUNION declenchee par le bot
          }
          auBotWalk(b, dt, 60.0f * b.spdF);
          continue;
        }
        // decouverte d'un corps ?
        for (int c = 0; c < auBodyN; c++) {
          float ddx = auBodyX[c] - b.x, ddy = auBodyY[c] - b.y;
          if (ddx * ddx + ddy * ddy < 44 * 44 && b.t <= 0) {
            b.t = 4.0f; // ne re-decouvre pas tout de suite
            if (b.imp) {
              if (auBodySus[c] != i) { // V5.32 IA : ACTING — le non-tueur panique comme un innocent au bouton
                b.panic = true; b.state = 3;
                auSetDest(b, auBtnX, auBtnY);
              } else { // le TUEUR, lui, s'ecarte discretement
                b.flee = 4.0f;
                int rx = rnd(AUW), ry = rnd(AUH);
                if (auPar[ry * AUW + rx] != -2) auSetDest(b, rx * 8.0f + 4.0f, ry * 8.0f + 4.0f);
              }
            } else {
              int sus = auBodySus[c];
              if (sus == i) sus = -1;
              if (sus >= 0 && sus != AU_MAXBOT && !auB[sus].alive) sus = -1; // suspect deja mort
              if (sus == AU_MAXBOT && pGhost) sus = -1; // le fantome joueur n'est plus montrable
              b.accuse = sus; b.witness = (sus >= 0);
              auPanicTo(b);
            }
            break;
          }
        }
        if (b.panic) continue;
        if (b.state == 2) { // V5.27 IA : PAUSE (il s'arrete, regarde autour : plus vivant)
          if (b.t <= 0) b.state = 0;
          else continue;
        }
        if (b.imp && b.flee <= 0) { // IMPOSTEUR PNJ
          if (lightT <= 0 && doorT <= 0 && o2T <= 0 && rnd(1500) == 0) { // V5.30 : sabotage spontane varie
            int s = rnd(3);
            if (s == 0) lightT = 25.0f;
            else if (s == 1) {
              auDoorSabotage();
              for (int k = 0; k < auDoorN; k++) { // jamais sur TA case ni celle d'un bot
                int dc = auDoorC[k];
                bool onMe = ((int)(px / 8.0f) == dc % AUW && (int)(py / 8.0f) == dc / AUW);
                bool onBot = false;
                for (int q = 0; q < auBotN; q++) if (auB[q].alive && (int)(auB[q].x / 8.0f) == dc % AUW && (int)(auB[q].y / 8.0f) == dc / AUW) onBot = true;
                if (onMe || onBot) { auDoorC[k] = auDoorC[auDoorN - 1]; auDoorN--; k--; }
              }
              doorT = 10.0f;
            }
            else { o2T = 30.0f; o2FixA = 0; o2FixB = 0; }
          }
          if (o2T > 0 && b.state == 0 && rnd(200) == 0) { // V5.32 IA : FAUX REPARATEUR — il se plante sur un point O2
            b.state = 5; b.t = 6.0f + rnd(60) * 0.1f;
            float fx2, fy2;
            if (rnd(2)) { fx2 = auBtnX; fy2 = auBtnY; } else { fx2 = auVitXc * 8.0f + 4.0f; fy2 = auVitYc * 8.0f + 4.0f; }
            auSetDest(b, fx2, fy2); // il fait GENRE (ne repare jamais reellement)
          }
          if (b.state == 5) { // V5.32 IA : reste poste sur le point, puis reprend la chasse
            if (b.t <= 0) b.state = 0;
            else { auBotWalk(b, dt, 55.0f * b.spdF); auBotArrived(b); continue; }
          }
          if (b.state == 4) { // V5.27 IA : EMBUSCADE en couloir (jamais longtemps)
            if (b.t <= 0) b.state = 0;
            else continue; // immobile, il guette
          }
          if (rnd(1000) == 0 && b.state == 0) { // V5.27 IA : il se poste en embuscade 3-5 secondes
            b.state = 4; b.t = 3.0f + rnd(20) * 0.1f;
            b.pn = 0; b.dx = b.x; b.dy = b.y;
            continue;
          }
          if (b.taskI >= 4 || b.t > 0) { // taches finies ou "pause" : TRAQUE le plus proche
            float bd = 1e9; int vic = -1;
            bool vicIsPlayer = false;
            for (int v = 0; v < auBotN; v++) {
              if (v == i || !auB[v].alive || auB[v].imp) continue;
              float ddx = auB[v].x - b.x, ddy = auB[v].y - b.y;
              float d2 = ddx * ddx + ddy * ddy;
              if (d2 < bd) { bd = d2; vic = v; }
            }
            if (!meImp && !pGhost) {
              float ddx = px - b.x, ddy = py - b.y;
              float d2 = ddx * ddx + ddy * ddy;
              if (d2 < bd) { bd = d2; vicIsPlayer = true; }
            }
            // victime isolee (pas de temoin a 70 px) ?
            if (vic >= 0 || vicIsPlayer) {
              float vx0 = vicIsPlayer ? px : auB[vic].x, vy0 = vicIsPlayer ? py : auB[vic].y;
              bool isolated = true;
              for (int w = 0; w < auBotN; w++) {
                if (w == i || !auB[w].alive || w == vic) continue;
                float wx = auB[w].x - vx0, wy = auB[w].y - vy0;
                if (wx * wx + wy * wy < 70 * 70) { isolated = false; break; }
              }
              if (!vicIsPlayer && !pGhost) {
                float wx = px - vx0, wy = py - vy0;
                if (wx * wx + wy * wy < 70 * 70) isolated = false;
              }
              if (!vicIsPlayer && !isolated && bd < 50 * 50 && doorT <= 0 && o2T <= 0 && lightT <= 0 && rnd(100) < 25) {
                // V5.32 IA : PIEGE AUX PORTES — mais SEULEMENT si un vent est a portee pour disparaitre apres
                bool ventNear = false;
                for (int v = 0; v < 5; v++) {
                  float vdx2 = AU_VX[v] * 8.0f + 4.0f - vx0, vdy2 = AU_VY[v] * 8.0f + 4.0f - vy0;
                  if (vdx2 * vdx2 + vdy2 * vdy2 < 110 * 110) { ventNear = true; break; }
                }
                if (ventNear && auDoorTrapAt((int)(vx0 / 8.0f), (int)(vy0 / 8.0f))) doorT = 10.0f;
              }
              if (isolated && bd < 26 * 26) { // KILL !
                if (vicIsPlayer) { dieT = 1.6f; break; } // V5.29 : ANIMATION de mort avant l'ecran de fin
                auB[vic].alive = false; auB[vic].ghost = true;
                fxT = 0.5f; fxType = 1; fxX = auB[vic].x; fxY = auB[vic].y; // V5.29 : ANIMATION de kill PNJ
                if (auBodyN < 16) {
                  auBodyX[auBodyN] = auB[vic].x; auBodyY[auBodyN] = auB[vic].y;
                  auBodyC[auBodyN] = AU_BOTC[vic]; auBodySus[auBodyN] = i; auBodyN++;
                }
                b.flee = 4.0f;
                if (rnd(100) < 30 || doorT > 0) { // V5.26b IA : 30% de s'echapper par un VENT (100% si il vient de pieger aux portes)
                  int v = rnd(5);
                  b.x = AU_VX[v] * 8.0f + 4.0f; b.y = AU_VY[v] * 8.0f + 4.0f;
                }
                { // V5.32 IA : ALIBI — apres le kill il va se montrer pres d'une TACHE (aucun indice visuel, ca ne compte pas)
                  int tk = rnd(12);
                  auSetDest(b, AU_TX[tk] * 8.0f + 4.0f, AU_TY[tk] * 8.0f + 4.0f);
                }
                b.t = 1.5f;
              } else if (isolated && lightT <= 0 && rnd(100) < 20) { // V5.27 IA : il coupe les LUMIERES avant de frapper
                lightT = 25.0f;
              } else if (bd < 300 * 300) { // approche
                if (b.pn == 0 || b.dx != vx0 || b.dy != vy0) auSetDest(b, vx0, vy0);
                // V5.32 IA : TENAILLE — un complice proche converge sur la MEME victime ( jamais l'inverse !)
                for (int q = 0; q < auBotN; q++) {
                  if (q == i || !auB[q].alive || !auB[q].imp || auB[q].panic || auB[q].flee > 0) continue;
                  float cdx2 = auB[q].x - vx0, cdy2 = auB[q].y - vy0;
                  if (cdx2 * cdx2 + cdy2 * cdy2 < 120 * 120 && (auB[q].pn == 0 || auB[q].dx != vx0 || auB[q].dy != vy0))
                    auSetDest(auB[q], vx0, vy0);
                }
              }
            }
          } else { // fait SEMBLANT de faire ses taches
            float tx = AU_TX[b.tasks[b.taskI]] * 8.0f + 4.0f, ty = AU_TY[b.tasks[b.taskI]] * 8.0f + 4.0f;
            if (b.pn == 0 || b.dx != tx || b.dy != ty) auSetDest(b, tx, ty);
            if (auBotArrived(b)) {
              // V5.32 IA : COLLABORATION MORTELLE — un crew fait la tache A COTE ? il l'achève
              int cvic = -1; float cvx = 0, cvy = 0;
              for (int v = 0; v < auBotN; v++) {
                if (!auB[v].alive || auB[v].imp) continue;
                float ddx2 = auB[v].x - b.x, ddy2 = auB[v].y - b.y;
                if (ddx2 * ddx2 + ddy2 * ddy2 < 26 * 26) { cvic = v; cvx = auB[v].x; cvy = auB[v].y; break; }
              }
              if (cvic >= 0 && rnd(100) < 60) {
                auB[cvic].alive = false; auB[cvic].ghost = true;
                if (auBodyN < 16) { auBodyX[auBodyN] = cvx; auBodyY[auBodyN] = cvy; auBodyC[auBodyN] = AU_BOTC[cvic]; auBodySus[auBodyN] = i; auBodyN++; }
                fxT = 0.5f; fxType = 1; fxX = cvx; fxY = cvy;
                for (int w = 0; w < auBotN; w++) { // temoins eventuels
                  if (!auB[w].alive || auB[w].imp || w == cvic) continue;
                  float wx2 = auB[w].x - cvx, wy2 = auB[w].y - cvy;
                  if (wx2 * wx2 + wy2 * wy2 < 48 * 48) { auB[w].witness = true; auB[w].accuse = i; auPanicTo(auB[w]); }
                }
                if (rnd(100) < 30) { int v2 = rnd(5); b.x = AU_VX[v2] * 8.0f + 4.0f; b.y = AU_VY[v2] * 8.0f + 4.0f; } // vent
                b.flee = 4.0f;
              }
              b.taskI++; b.t = 2.0f + rnd(30) * 0.1f;
            }
          }
        } else if (!b.imp) { // CREW PNJ
          if (o2T > 0) { // V5.30 : OXYGENE — le crew file au point pas encore repare
            float ax = auVitXc * 8.0f + 4.0f, ay = auVitYc * 8.0f + 4.0f;
            float tx2 = ax, ty2 = ay;
            if (o2FixA) { tx2 = auBtnX; ty2 = auBtnY; }
            else if (!o2FixB) {
              float dA = (b.x - ax) * (b.x - ax) + (b.y - ay) * (b.y - ay);
              float dB = (b.x - auBtnX) * (b.x - auBtnX) + (b.y - auBtnY) * (b.y - auBtnY);
              if (dB < dA) { tx2 = auBtnX; ty2 = auBtnY; }
            }
            if (b.pn == 0 || b.dx != tx2 || b.dy != ty2) auSetDest(b, tx2, ty2);
            if (auBotArrived(b)) {
              if (tx2 == auBtnX && ty2 == auBtnY) { if (!o2FixB) o2FixB = 1; }
              else { if (!o2FixA) o2FixA = 1; }
              if (o2FixA && o2FixB) o2T = 0;
              b.t = 1.0f;
            }
            auBotWalk(b, dt, 55.0f * b.spdF);
            continue;
          }
          if (lightT > 0) { // sabotage actif : le plus proche va reparer
            float myd = (b.x - auVitXc * 8.0f - 4.0f) * (b.x - auVitXc * 8.0f - 4.0f) + (b.y - auVitYc * 8.0f - 4.0f) * (b.y - auVitYc * 8.0f - 4.0f);
            bool iGo = true;
            for (int w = 0; w < auBotN; w++) {
              if (w == i || !auB[w].alive || auB[w].imp) continue;
              float wd = (auB[w].x - auVitXc * 8.0f - 4.0f) * (auB[w].x - auVitXc * 8.0f - 4.0f) + (auB[w].y - auVitYc * 8.0f - 4.0f) * (auB[w].y - auVitYc * 8.0f - 4.0f);
              if (wd < myd) { iGo = false; break; }
            }
            if (iGo) {
              if (b.pn == 0 || b.dx != auVitXc * 8.0f + 4.0f || b.dy != auVitYc * 8.0f + 4.0f) auSetDest(b, auVitXc * 8.0f + 4.0f, auVitYc * 8.0f + 4.0f);
              if (auBotArrived(b)) { lightT = 0; }
              auBotWalk(b, dt, 55.0f * b.spdF);
              continue;
            }
          }
          if (b.taskI < 4) {
            float tx = AU_TX[b.tasks[b.taskI]] * 8.0f + 4.0f, ty = AU_TY[b.tasks[b.taskI]] * 8.0f + 4.0f;
            if (b.pn == 0 || b.dx != tx || b.dy != ty) auSetDest(b, tx, ty);
            if (auBotArrived(b)) {
              if (b.state == 1) { b.t -= dt; if (b.t <= 0) { b.taskI++; auTaskBar += 1.0f / totalTasks;
                if (rnd(100) < 25) { b.state = 2; b.t = 0.8f + rnd(15) * 0.1f; } // V5.27 : pause cafe parfois
              } }
              else { b.state = 1; b.t = 4.0f; }
            } else b.state = 0;
          } else { // ses taches sont finies : erre tranquille
            if (auBotArrived(b) || b.pn == 0) {
              int rx = rnd(AUW), ry = rnd(AUH);
              if (rnd(100) < 40) { // V5.27 IA : petite BALADE LOCALE (allers-retours) 40% du temps
                int ccx = (int)(b.x / 8.0f), ccy = (int)(b.y / 8.0f);
                rx = ccx + rnd(13) - 6; ry = ccy + rnd(13) - 6;
                if (rx < 0) rx = 0;
                if (ry < 0) ry = 0;
                if (rx >= AUW) rx = AUW - 1;
                if (ry >= AUH) ry = AUH - 1;
              }
              if (auPar[ry * AUW + rx] != -2) auSetDest(b, rx * 8.0f + 4.0f, ry * 8.0f + 4.0f);
            }
          }
        }
        auBotWalk(b, dt, 55.0f * b.spdF);
        auBotArrived(b);
      }
    }
    // ---- conditions de victoire ----
    if (!endScreen) {
      int crewN = (!meImp && !pGhost ? 1 : 0);
      int impN = (meImp && !pGhost ? 1 : 0);
      for (int i = 0; i < auBotN; i++) {
        if (!auB[i].alive) continue;
        if (auB[i].imp) impN++; else crewN++;
      }
      if (crewN == 0) endScreen = 1;
      else if (impN == 0) endScreen = 3;
      else if (auTaskBar >= 0.999f) endScreen = meImp ? 2 : 5;
    }
    // ================== DEPOUILLEMENT DES VOTES ==================
    if (doTally && !endScreen) {
      int votes[AU_MAXBOT + 1] = {0};
      if (myVote >= 0 && myVote <= auBotN) votes[myVote]++;
      // mon vote sur le joueur ?
      if (myVote == AU_MAXBOT) votes[AU_MAXBOT]++; // impossible via l'interface, securite
      int wVote = -1;
      for (int i = 0; i < auBotN; i++) { // les temoins accusent
        AuBot &b = auB[i];
        if (!b.alive || b.imp) continue;
        if (b.witness && b.accuse >= 0 && b.accuse <= auBotN) {
          if (b.accuse < auBotN && !auB[b.accuse].alive) continue;
          if (rnd(100) < 20) continue; // V5.27 IA : temoin HESITANT (20% s'abstient : "pas sur...")
          votes[b.accuse]++;
          if (wVote < 0) wVote = b.accuse;
        }
      }
      for (int i = 0; i < auBotN; i++) { // les autres suivent ou s'abstiennent
        AuBot &b = auB[i];
        if (!b.alive || b.imp) continue;
        if (b.witness && b.accuse >= 0) continue;
        if (gSus) { votes[AU_MAXBOT]++; continue; } // mefiance generale contre TOI
        if (wVote >= 0 && rnd(100) < 70) votes[wVote]++;
        else if (rnd(100) < 35) {
          int cand = rnd(auBotN);
          if (auB[cand].alive && !auB[cand].imp) votes[cand]++;
        }
      }
      // V5.27 IA : les imposteurs PNJ COUVRENT les leurs (deflection discrete, jamais suspects)
      for (int i = 0; i < auBotN; i++) {
        AuBot &b = auB[i];
        if (!b.alive || !b.imp) continue;
        int lead = -1, lv = 0;
        for (int c = 0; c <= auBotN; c++) if (votes[c] > lv) { lv = votes[c]; lead = c; }
        if (lead < 0 || lead == i || (lead < auBotN && !auB[lead].imp)) continue; // il n'agit QUE si un COMPLICE mene les votes
        int cand = -1, cv = 0;
        for (int c = 0; c <= auBotN; c++) { // l'innocent le plus accuse
          if (c == i || c == lead) continue;
          if (c < auBotN && (!auB[c].alive || auB[c].imp)) continue;
          if (votes[c] > cv) { cv = votes[c]; cand = c; }
        }
        if (cand >= 0 && rnd(100) < 60) votes[cand]++; // 60% du temps seulement : discret
      }
      int best = -1, bestV = 0;
      for (int c = 0; c <= auBotN; c++) {
        if (c == AU_MAXBOT && (meImp || pGhost)) continue;
        if (c < auBotN && !auB[c].alive) continue;
        if (votes[c] > bestV) { bestV = votes[c]; best = c; }
      }
      if (best == AU_MAXBOT) { // TOI ejecte
        pGhost = true;
        auEjectScreen(-1, meImp, impsAlive - (meImp ? 1 : 0));
        if (meImp) impsAlive--;
      } else if (best >= 0) { // un bot ejecte
        bool wasImp = auB[best].imp;
        auB[best].alive = false; auB[best].ghost = true;
        auEjectScreen(best, wasImp, impsAlive - (wasImp ? 1 : 0));
        if (wasImp) impsAlive--;
        if (myVote == best && !wasImp) gSus = true; // FAUSSE ACCUSATION
        if (impsAlive <= 0) endScreen = 3;
      }
      // cleanup de la reunion
      auBodyN = 0;
      myVote = -1; doTally = false; meetT = 0;
      for (int i = 0; i < auBotN; i++) {
        auB[i].panic = false; auB[i].witness = false; auB[i].accuse = -1;
        auB[i].state = 0; auB[i].t = 0;
        if (auB[i].alive && !auB[i].imp && auB[i].taskI < 4) auNextTask(auB[i]);
      }
      killCd = 10.0f; ventCd = 0; sabCd = 15.0f;
    }
    // ================== RENDU ==================
    float z = auZoom; // 1.0 ou 0.5 (zoom arriere)
    float ts = 8.0f * z;
    float vpW = SW / z, vpH = (SH - 40) / z;
    float camX = px - vpW / 2, camY = py - vpH / 2;
    if (camX < 0) camX = 0;
    if (camX > AUW * 8 - vpW) camX = AUW * 8 - vpW;
    if (camY < 0) camY = 0;
    if (camY > AUH * 8 - vpH) camY = AUH * 8 - vpH;
    if (vpW > AUW * 8) camX = (AUW * 8 - vpW) / 2;
    if (vpH > AUH * 8) camY = (AUH * 8 - vpH) / 2;
    fRect(0, 0, SW, SH - 40, KDColor::RGB24(0x232833));
    // carte (spans de cellules identiques)
    int c0x = (int)(camX / 8) - 1, c1x = (int)((camX + vpW) / 8) + 1;
    int c0y = (int)(camY / 8) - 1, c1y = (int)((camY + vpH) / 8) + 1;
    if (c0x < 0) c0x = 0;
    if (c0y < 0) c0y = 0;
    if (c1x > AUW - 1) c1x = AUW - 1;
    if (c1y > AUH - 1) c1y = AUH - 1;
    for (int cy = c0y; cy <= c1y; cy++) {
      int cx = c0x;
      while (cx <= c1x) {
        if (auCell(cx, cy) == ' ') { cx++; continue; }
        int sx = cx;
        while (cx <= c1x && auCell(cx, cy) != ' ') cx++;
        fRect((sx * 8 - camX) * z, (cy * 8 - camY) * z, (cx - sx) * ts, ts, KDColor::RGB24(auFlooC));
        for (int k = sx; k < cx; k++)
          if (auCell(k, cy) == '#') fRect((k * 8 - camX) * z, (cy * 8 - camY) * z, ts, ts, KDColor::RGB24(auWallC));
      }
    }
    // taches / vents / bouton / spot cameras / vitals
    int blinkT = ((int)(gTime * 3.0f)) % 2; // V5.29 : les taches CLIGNOTENT
    for (int i = 0; i < 12; i++) {
      float tx = AU_TX[i] * 8.0f - camX, ty = AU_TY[i] * 8.0f - camY;
      bool mine = (!meImp && pTaskI < 4 && pTasks[pTaskI] == i); // MA tache en cours
      if (mine) fRect(tx * z - z, ty * z - z, ts + 2 * z, ts + 2 * z, C_YEL); // toujours allumee en JAUNE
      else if (blinkT) fRect(tx * z, ty * z, ts, ts, KDColor::RGB24(0x2FBF71));
      else fRect(tx * z + z, ty * z + z, ts - 2 * z, ts - 2 * z, KDColor::RGB24(0x1E7A47)); // presque eteinte
    }
    for (int i = 0; i < 5; i++) {
      float vx = AU_VX[i] * 8.0f - camX, vy = AU_VY[i] * 8.0f - camY;
      fRing((int)((vx + 4) * z), (int)((vy + 4) * z), (int)(5 * z), meImp && !pGhost ? KDColor::RGB24(0x888899) : C_GRE);
    }
    fRect((auBtnX - 6 - camX) * z, (auBtnY - 6 - camY) * z, 12 * z, 12 * z, KDColor::RGB24(0xC51111));
    for (int k = 0; k < auDoorN; k++) { // V5.30 : PORTES fermees en rouge sombre
      int dc = auDoorC[k];
      fRect((int)((dc % AUW) * 8.0f - camX) * z, (int)((dc / AUW) * 8.0f - camY) * z, ts, ts, KDColor::RGB24(0x8A2B2B));
      fRect((int)((dc % AUW) * 8.0f - camX) * z + z, (int)((dc / AUW) * 8.0f - camY) * z + z, ts - 2 * z, ts - 2 * z, KDColor::RGB24(0xB04040));
    }
    if (o2T > 0) { // V5.30 : les 2 points de reparation O2 (vert = repare, rouge clignotant = a faire)
      KDColor oc = ((int)(gTime * 3.0f)) % 2 ? KDColor::RGB24(0xC51111) : KDColor::RGB24(0x601010);
      oCirc((int)((auVitXc * 8 + 4 - camX) * z), (int)((auVitYc * 8 + 4 - camY) * z), (int)(6 * z), KDColor::RGB24(o2FixA ? 0x2FBF71 : 0xC51111), o2FixA ? KDColor::RGB24(0x1B4D2E) : oc);
      oCirc((int)((auBtnX - camX) * z), (int)((auBtnY - camY) * z), (int)(6 * z), KDColor::RGB24(o2FixB ? 0x2FBF71 : 0xC51111), o2FixB ? KDColor::RGB24(0x1B4D2E) : oc);
    }
    fCirc((int)((auVitXc * 8 + 4 - camX) * z), (int)((auVitYc * 8 + 4 - camY) * z), (int)(5 * z), KDColor::RGB24(0x38FEDC));
    if (lightT > 0) fRing((int)((auVitXc * 8 + 4 - camX) * z), (int)((auVitYc * 8 + 4 - camY) * z), (int)(8 * z), KDColor::RGB24(0xF07D0D));
    // cadavres (visibles seulement dans le champ de vision)
    for (int c = 0; c < auBodyN; c++) {
      float d = (auBodyX[c] - px) * (auBodyX[c] - px) + (auBodyY[c] - py) * (auBodyY[c] - py);
      if (!pGhost && d > visR * visR) continue;
      if (!pGhost && !auClearLine(px, py, auBodyX[c], auBodyY[c])) continue;
      drawCorpse((auBodyX[c] - camX) * z, (auBodyY[c] - camY) * z, KDColor::RGB24(auBodyC[c]));
    }
    // PNJ vivants
    for (int i = 0; i < auBotN; i++) {
      AuBot &b = auB[i];
      if (!b.alive) {
        if (pGhost) { // les fantomes voient les fantomes
          fRing((int)((b.x - camX) * z), (int)((b.y - camY) * z), (int)(5.0f * z), KDColor::RGB24(AU_BOTC[i]));
        }
        continue;
      }
      float d = (b.x - px) * (b.x - px) + (b.y - py) * (b.y - py);
      if (!pGhost && (d > visR * visR || !auClearLine(px, py, b.x, b.y))) continue;
      drawBean((b.x - camX) * z, (b.y - camY) * z, KDColor::RGB24(AU_BOTC[i]), false);
      drawHat((int)((b.x - camX) * z), (int)((b.y - camY) * z), auHats[i], KDColor::RGB24(AU_BOTC[i])); // V5.30 : son chapeau
      if (meImp && !pGhost && b.imp) fRing((int)((b.x - camX) * z), (int)((b.y - camY) * z + 10), 7, KDColor::RGB24(0xC51111)); // V5.28 : MARQUE DE RECONNAISSANCE entre imposteurs
    }
    // joueur
    if (pGhost) {
      fRing((int)((px - camX) * z), (int)((py - camY) * z), (int)(6.0f * z), C_WHT);
      fCirc((int)((px - camX) * z), (int)((py - camY) * z - 2), (int)(2.0f * z), C_WHT);
    } else {
      drawBean((px - camX) * z, (py - camY) * z, meImp ? KDColor::RGB24(0xC51111) : KDColor::RGB24(0x38FEDC), (playerDir == 0));
      drawHat((int)((px - camX) * z), (int)((py - camY) * z), auHats[auBotN], meImp ? KDColor::RGB24(0xC51111) : KDColor::RGB24(0x38FEDC)); // V5.30 : TON chapeau
    }
    // ---- POLYGONE DE VISION (raycast arrete par les murs) ----
    if (!pGhost) {
      float ray[40][2];
      for (int r = 0; r < 40; r++) {
        float ang = r * 0.15708f;
        float c = fcosf(ang), s = fsinf(ang);
        float d = 5.0f;
        while (d < visR) {
          if (auSolidPx(px + c * d, py + s * d)) break;
          d += 5.0f;
        }
        if (d > visR) d = visR;
        ray[r][0] = (px + c * d - camX) * z;
        ray[r][1] = (py + s * d - camY) * z;
      }
      for (int r = 0; r < 40; r++) {
        int r2 = (r + 1) % 40;
        auLine(ray[r][0], ray[r][1], ray[r2][0], ray[r2][1], KDColor::RGB24(0xFFF3A8));
      }
    }
    // ---- V5.29 : ANIMATIONS EN JEU ----
    if (fxT > 0) {
      fxT -= dt;
      float prog = 1.0f - fxT * 2.0f; if (prog > 1.0f) prog = 1.0f;
      if (fxType == 1) { // KILL : onde de choc rouge + eclaboussures
        fRing((int)((fxX - camX) * z), (int)((fxY - camY) * z), (int)((6 + prog * 24) * z), KDColor::RGB24(0xC51111));
        fRing((int)((fxX - camX) * z), (int)((fxY - camY) * z), (int)((3 + prog * 12) * z), C_WHT);
        for (int p = 0; p < 6; p++) { // gouttes qui volent
          float ang = p * 1.047f;
          float d = prog * 22;
          fRect((int)((fxX + fcosf(ang) * d - camX) * z), (int)((fxY + fsinf(ang) * d - camY) * z), 2, 2, KDColor::RGB24(0xC51111));
        }
      }
      if (fxType == 2) { // TACHE OK : etoiles jaunes qui montent
        for (int p = 0; p < 5; p++) {
          float ox2 = (p - 2) * 5.0f;
          float oy2 = fxT * 26.0f - 6.0f;
          fRect((int)((fxX + ox2 - camX) * z), (int)((fxY - oy2 - camY) * z), 2, 2, C_YEL);
        }
        drawTextC(56, "+1 TACHE !", ((int)(fxT * 8.0f)) % 2 ? C_YEL : C_WHT);
      }
    }
    if (!meImp && pTaskI < 4) { // V5.30 : FLECHE jaune qui pointe vers ta tache en cours
      float ttx = AU_TX[pTasks[pTaskI]] * 8.0f + 4.0f, tty = AU_TY[pTasks[pTaskI]] * 8.0f + 4.0f;
      float adx3 = ttx - px, ady3 = tty - py;
      float adl = sqrtf(adx3 * adx3 + ady3 * ady3);
      if (adl > 26.0f) {
        float ux3 = adx3 / adl, uy3 = ady3 / adl;
        int ax3 = (int)((px - camX + ux3 * 13.0f) * z), ay3 = (int)((py - camY + uy3 * 13.0f) * z);
        px(ax3, ay3, C_YEL); // pointe
        px(ax3 - (int)(uy3 * 3), ay3 + (int)(ux3 * 3), C_YEL); // ailes de la fleche
        px(ax3 + (int)(uy3 * 3), ay3 - (int)(ux3 * 3), C_YEL);
        px((int)((px - camX + ux3 * 10.0f) * z), (int)((py - camY + uy3 * 10.0f) * z), C_YEL);
      }
    }
    // ================== HUD ==================
    fRect(0, SH - 40, SW, 40, KDColor::RGB24(0x1B1B25));
    // barre de taches globale
    fRect(8, SH - 36, 144, 8, KDColor::RGB24(0x2A2F3A));
    fRect(8, SH - 36, (int)(144 * auTaskBar), 8, KDColor::RGB24(0x2FBF71));
    drawNum(158, SH - 37, (int)(auTaskBar * 100), C_GRE); drawText(180, SH - 37, "%", C_GRE);
    // equipage vivant
    int aliveN = (!pGhost ? 1 : 0);
    for (int i = 0; i < auBotN; i++) if (auB[i].alive) aliveN++;
    drawText(8, SH - 24, "EQ:", C_WHT); drawNum(28, SH - 24, aliveN, C_WHT);
    drawNum(44, SH - 24, (int)gTime, C_GRE); drawText(76, SH - 24, "s", C_GRE);
    // V5.26b : COMPAS DE TACHE — direction et distance de la prochaine tache
    if (!meImp && pTaskI < 4) {
      int tcx = AU_TX[pTasks[pTaskI]], tcy = AU_TY[pTasks[pTaskI]];
      int dcx = tcx - (int)(px / 8.0f), dcy = tcy - (int)(py / 8.0f);
      int dist = (int)sqrtf((float)(dcx * dcx + dcy * dcy));
      const char *dir = "O";
      if (dcy <= -abs(dcx) && abs(dcy) > 0) dir = "N";
      else if (dcy >= abs(dcx)) dir = "S";
      else if (dcx > 0) dir = "E";
      drawText(200, SH - 24, "TACHE:", C_YEL);
      drawNum(252, SH - 24, dist, C_YEL);
      drawText(272, SH - 24, "CASES", C_GRE);
      drawText(296, SH - 24, dir, C_YEL);
    }
    if (gSus) drawTextC(4, "SUS!", KDColor::RGB24(0xC51111));
    if (meetT > 0) {
      drawTextC(16, "REUNION ! VOTE (+)", ((int)(gTime * 4.0f)) % 2 ? KDColor::RGB24(0xF07D0D) : C_WHT); // V5.29 : banniere qui clignote
      drawNum(296, 16, (int)meetT + 1, C_YEL);
    }
    if (o2T > 0) { // V5.30 : compte a rebours OXYGENE en gros
      char obuf[36];
      snprintf(obuf, 36, "OXYGENE : %ds - REPARER 2 POINTS !", (int)o2T + 1);
      drawTextC(42, obuf, ((int)(gTime * 4.0f)) % 2 ? KDColor::RGB24(0xC51111) : C_WHT);
    }
    if (lightT > 0) {
      drawTextC(28, "LUMIERES SABOTEES !", ((int)(gTime * 3.0f)) % 2 ? KDColor::RGB24(0xF07D0D) : C_WHT); // V5.29 : alerte clignotante
      if (((int)(gTime * 3.0f)) % 2) { fRect(0, 0, SW, 3, KDColor::RGB24(0xC51111)); fRect(0, SH - 43, SW, 3, KDColor::RGB24(0xC51111)); } // bordure alarme
    }
    // ligne du bas selon le role
    if (pGhost) {
      drawTextC(SH - 12, "FANTOME : OK:TACHE  MURS LIBRES", C_WHT);
      if (pTaskI < 4) { drawText(220, SH - 37, "T", C_GRE); drawNum(230, SH - 37, pTaskI + 1, C_ORG); }
    } else if (meImp) {
      drawTextC(SH - 12, "OK:TUER EXE:VENT DIV:SABOTAGE +:VOTE -:ZOOM", C_WHT);
      if (killCd > 0) { fRect(8, SH - 6, 60, 4, KDColor::RGB24(0x2A2F3A)); fRect(8, SH - 6, (int)(60 * (1.0f - killCd / 10.0f)), 4, KDColor::RGB24(0xC51111)); }
      if (sabCd > 0) { fRect(72, SH - 6, 60, 4, KDColor::RGB24(0x2A2F3A)); fRect(72, SH - 6, (int)(60 * (1.0f - sabCd / 35.0f)), 4, KDColor::RGB24(0xF07D0D)); }
    } else {
      if (pTaskI < 4) {
        drawTextC(SH - 12, "OK:TACHE +:VOTE -:ZOOM", C_WHT);
        drawNum(300, SH - 12, pTaskI + 1, C_ORG);
        if (pTaskProg > 0) { fRect(200, SH - 6, 80, 4, KDColor::RGB24(0x2A2F3A)); fRect(200, SH - 6, (int)(80 * pTaskProg), 4, C_YEL); }
      } else drawTextC(SH - 12, "TACHES PERSO OK : AIDE L'EQUIPAGE", C_WHT);
      if (fixProg > 0) { fRect(200, SH - 6, 80, 4, KDColor::RGB24(0x2A2F3A)); fRect(200, SH - 6, (int)(80 * fixProg), 4, KDColor::RGB24(0x38FEDC)); }
    }
    pOK = keyOK; pExp = kExp; pBack = keyBack; pPlus = kPlus; pMn = kMinus; pDiv = kDiv;
    blit();
    Ion::Timing::msleep(30);
  }
  // ================== ECRAN DE FIN ==================
  bool won = (endScreen == 6) || (endScreen == 1 && meImp) || (!meImp && (endScreen == 2 || endScreen == 3 || endScreen == 5));
  if (won) auWins++;
  if (meImp && endScreen == 1 && (auBestS == 0 || gTime < auBestS)) auBestS = (int)gTime;
  saveAcc();
  waitRelease();
  bool prevOK = false;
  while (true) {
    pollKeys();
    if (keyOK && !prevOK) { waitRelease(); break; }
    prevOK = keyOK;
    bgPanel();
    drawTextC(20, won ? "VICTOIRE !" : "DEFAITE...", won ? C_YEL : KDColor::RGB24(0xC51111));
    if (endScreen == 1) drawTextC(48, meImp ? "EQUIPAGE ELIMINE" : "TOUT L'EQUIPAGE EST MORT", C_WHT);
    if (endScreen == 2) drawTextC(48, meImp ? "L'EQUIPAGE A FINI SES TACHES" : "TOUTES LES TACHES SONT FINIES", C_WHT);
    if (endScreen == 3) drawTextC(48, meImp ? "UN TEMOIN T'A SIGNALE !" : "L'IMPOSTEUR A ETE EJECTE", C_WHT);
    if (endScreen == 4) drawTextC(48, "ABANDON", C_WHT);
    if (endScreen == 5) drawTextC(48, "TOUT L'EQUIPAGE A FINI SES TACHES", C_WHT);
    if (endScreen == 6) drawTextC(48, "L'OXYGENE S'EST EPUISE !", KDColor::RGB24(0xC51111));
    drawTextC(76, AU_MAPNAMES[mapSel], C_GRE);
    char sbuf[40];
    snprintf(sbuf, 40, "TA TACHE:%d/4", pTaskI); drawTextC(96, sbuf, C_WHT);
    snprintf(sbuf, 40, "TACHES EQUIPE:%d%%", (int)(auTaskBar * 100)); drawTextC(112, sbuf, C_WHT);
    snprintf(sbuf, 40, "TEMPS:%ds", (int)gTime); drawTextC(128, sbuf, C_WHT);
    snprintf(sbuf, 40, "VICTOIRES AU:%d", auWins); drawTextC(148, sbuf, C_YEL);
    snprintf(sbuf, 40, "KILLS AU:%d", auKills); drawTextC(164, sbuf, C_ORG);
    rRect(SW / 2 - 70, 190, 140, 22, 8, C_YEL);
    drawTextC(196, "OK RETOUR", C_BLK);
    blit();
    Ion::Timing::msleep(30);
  }
}


// ======================= VAMPIRE SURVIVORS MINI (V5.32) ==============
// Survie : les ennemis arrivent par vagues croissantes, l'arme tire TOUTE
// SEULE sur le plus proche. Ramasse les ames (XP), choisis tes bonus a
// chaque niveau, survis le plus longtemps possible. Boss toutes les 60 s.
static void vsGame() {
  // ---- donnees des 5 types d'ennemis : chauve-souris, squelette, zombie, fantome, boss
  static const float VS_SPD[5] = { 48.0f, 34.0f, 24.0f, 40.0f, 15.0f };
  static const int  VS_HP0[5]  = { 10, 18, 32, 14, 260 };
  static const int  VS_DMG[5]  = { 8, 10, 14, 12, 25 };
  static const int  VS_RAD[5]  = { 3, 4, 5, 4, 12 };
  static const int  VS_XP[5]   = { 1, 2, 3, 2, 15 };
  static const KDColor VS_COL[5] = { KDColor::RGB24(0xB44BFF), KDColor::RGB24(0xE8E8E8),
                                     KDColor::RGB24(0x3DDC55), KDColor::RGB24(0x35D4E8),
                                     KDColor::RGB24(0xFF3B3B) };
  static const char *VS_UPN[7] = { "DEGATS +35%", "CADENCE +25%", "VITESSE +15%",
                                   "PV MAX +25", "+1 PROJECTILE", "AIMANT +40%", "REGEN +0.5/S" };
  enum { VS_NE = 46, VS_NP = 24, VS_NG = 40 }; // quotas memoire fixes
  static float ex[VS_NE], ey[VS_NE], ew[VS_NE]; static int ehp[VS_NE]; static uint8_t etp[VS_NE];
  static float ppx[VS_NP], ppy[VS_NP], pvx[VS_NP], pvy[VS_NP]; static int plife[VS_NP];
  static float gx[VS_NG], gy[VS_NG]; static uint8_t gxp[VS_NG];
  int ne = 0;
  // ---- joueur et stats
  float vpx = SW / 2.0f, vpy = 130.0f;
  int maxHp = 100, hp = 100, lvl = 1, xp = 0, xpNeed = 7, kills = 0, bossN = 0;
  int uDmg = 0, uRate = 0, uSpd = 0, uHp = 0, uMul = 0, uMag = 0, uReg = 0;
  float fireT = 0.0f, invT = 0.0f, regT = 0.0f, gameTime = 0.0f, spawnT = 0.0f, bossMsgT = 0.0f;
  int bossNext = 60;
  bool faceR = true;
  waitRelease();
  bool prevBk = false;
  int lastMs = (int)Ion::Timing::millis();
  bool dead = false;
  while (true) { // ================= boucle principale =================
    int nowMs = (int)Ion::Timing::millis();
    float dt = (nowMs - lastMs) / 1000.0f; lastMs = nowMs;
    if (dt > 0.05f) dt = 0.05f; if (dt <= 0.0f) dt = 0.0001f;
    if (!dead) {
      gameTime += dt; invT -= dt; regT += dt; bossMsgT -= dt;
      // ---- deplacement joueur (fleches, vitesse selon bonus)
      float spd = 85.0f * (1.0f + 0.15f * uSpd);
      float dx = (kRight ? 1.0f : 0.0f) - (kLeft ? 1.0f : 0.0f);
      float dy = (kDown ? 1.0f : 0.0f) - (kUp ? 1.0f : 0.0f);
      if (dx != 0.0f || dy != 0.0f) {
        float n = sqrtf(dx * dx + dy * dy); vpx += dx / n * spd * dt; vpy += dy / n * spd * dt;
        if (dx > 0.0f) faceR = true; else if (dx < 0.0f) faceR = false;
      }
      if (vpx < 6) vpx = 6; if (vpx > SW - 6) vpx = SW - 6;
      if (vpy < 34) vpy = 34; if (vpy > SH - 8) vpy = SH - 8;
      // ---- regen
      if (uReg > 0 && regT >= 2.0f) { regT -= 2.0f; hp += uReg; if (hp > maxHp) hp = maxHp; }
      // ---- apparitions (rythme croissant) + boss toutes les 60 s
      if (gameTime >= bossNext) { // BOSS !
        bossNext += 60; bossN++;
        if (ne < VS_NE) { int s = rnd(4); float sx = (s == 0) ? -10.0f : (s == 1) ? SW + 10.0f : (s == 2) ? rnd(SW) * 1.0f : rnd(SW) * 1.0f;
          float sy = (s == 2) ? -10.0f : (s == 3) ? SH + 10.0f : rnd(SH) * 1.0f;
          ex[ne] = sx; ey[ne] = sy; ew[ne] = 0; etp[ne] = 4; ehp[ne] = VS_HP0[4] + (bossN - 1) * 160; ne++; }
        bossMsgT = 2.5f;
      }
      spawnT += dt;
      float sInt = 1.1f - gameTime * 0.011f; if (sInt < 0.30f) sInt = 0.30f;
      while (spawnT >= sInt) {
        spawnT -= sInt;
        if (ne < VS_NE) {
          int t = 0; int r = rnd(100); // types deplaces selon le temps de survie
          if (gameTime > 25 && r < 30) t = 1;
          if (gameTime > 50 && r >= 30 && r < 50) t = 2;
          if (gameTime > 80 && r >= 50 && r < 65) t = 3;
          int s = rnd(4);
          ex[ne] = (s == 0) ? -8.0f : (s == 1) ? SW + 8.0f : (s == 2) ? rnd(SW) * 1.0f : rnd(SW) * 1.0f;
          ey[ne] = (s == 2) ? -8.0f : (s == 3) ? SH + 8.0f : rnd(SH) * 1.0f;
          ew[ne] = rnd(628) / 100.0f; etp[ne] = (uint8_t)t;
          ehp[ne] = (int)(VS_HP0[t] * (1.0f + gameTime / 55.0f)); ne++;
        }
      }
      // ---- tir automatique sur l'ennemi le plus proche
      fireT -= dt;
      float fireCd = 0.75f / (1.0f + 0.25f * uRate);
      int dmg = (int)(10 * (1.0f + 0.35f * uDmg));
      int nproj = 1 + uMul; if (nproj > 5) nproj = 5;
      if (fireT <= 0.0f) {
        float best = 1e9f; int bi = -1;
        for (int i = 0; i < ne; i++) { float ddx = ex[i] - vpx, ddy = ey[i] - vpy; float d2 = ddx * ddx + ddy * ddy; if (d2 < best) { best = d2; bi = i; } }
        if (bi >= 0) {
          fireT = fireCd;
          float base = atan2f(ey[bi] - vpy, ex[bi] - vpx);
          for (int k = 0; k < nproj; k++) {
            int slot = -1;
            for (int j = 0; j < VS_NP; j++) { if (plife[j] <= 0) { slot = j; break; } }
            if (slot < 0) break;
            float a = base + (k - (nproj - 1) / 2.0f) * 0.16f;
            ppx[slot] = vpx; ppy[slot] = vpy; pvx[slot] = fcosf(a) * 260.0f; pvy[slot] = fsinf(a) * 260.0f; plife[slot] = 130; // 0.65 s
          }
        }
      }
      // ---- projectiles
      for (int i = 0; i < VS_NP; i++) {
        if (plife[i] <= 0) continue;
        plife[i]--; ppx[i] += pvx[i] * dt; ppy[i] += pvy[i] * dt;
        if (ppx[i] < -4 || ppx[i] > SW + 4 || ppy[i] < -4 || ppy[i] > SH + 4) { plife[i] = 0; continue; }
        for (int j = 0; j < ne; j++) {
          float ddx = ex[j] - ppx[i], ddy = ey[j] - ppy[i];
          int rr = VS_RAD[etp[j]] + 2;
          if (ddx * ddx + ddy * ddy < rr * rr) {
            plife[i] = 0; ehp[j] -= dmg;
            if (ehp[j] <= 0) { // ennemi tue : ame (XP) la ou il est tombe
              kills++;
              int gs = -1;
              for (int g = 0; g < VS_NG; g++) { if (gxp[g] == 0) { gs = g; break; } }
              if (gs < 0) gs = 0; // plus de place : on ecrase la premiere ame
              gx[gs] = ex[j]; gy[gs] = ey[j]; gxp[gs] = (uint8_t)VS_XP[etp[j]];
              if (gxp[gs] == 0) gxp[gs] = 1;
              if (etp[j] == 4) { hp += 20; if (hp > maxHp) hp = maxHp; } // le boss soigne
              ne--;
              ex[j] = ex[ne]; ey[j] = ey[ne]; ehp[j] = ehp[ne]; etp[j] = etp[ne]; ew[j] = ew[ne];
              j--; if (j < -1) j = -1;
            }
            break;
          }
        }
      }
      // ---- ennemis : avancent vers le joueur (+ zigzag des chauves-souris)
      for (int i = 0; i < ne; i++) {
        float ddx = vpx - ex[i], ddy = vpy - ey[i];
        float n = sqrtf(ddx * ddx + ddy * ddy); if (n < 1.0f) n = 1.0f;
        float s = VS_SPD[etp[i]];
        if (etp[i] == 0) { // chauve-souris : ondule
          ew[i] += dt * 6.0f;
          float w = fsinf(ew[i]) * 30.0f;
          ex[i] += (ddx / n * s - ddy / n * w) * dt;
          ey[i] += (ddy / n * s + ddx / n * w) * dt;
        } else { ex[i] += ddx / n * s * dt; ey[i] += ddy / n * s * dt; }
        // contact : degats au joueur (immunite 0.6 s apres un coup)
        int rr = VS_RAD[etp[i]] + 5;
        ddx = vpx - ex[i]; ddy = vpy - ey[i];
        if (invT <= 0.0f && ddx * ddx + ddy * ddy < rr * rr) { hp -= VS_DMG[etp[i]]; invT = 0.6f; if (hp <= 0) { hp = 0; dead = true; } }
      }
      // ---- ames : attirees par l'aimant, ramassees a moins de 7 px
      float mag = 26.0f * (1.0f + 0.4f * uMag);
      for (int i = 0; i < VS_NG; i++) {
        if (gxp[i] == 0) continue;
        float ddx = vpx - gx[i], ddy = vpy - gy[i];
        float d = sqrtf(ddx * ddx + ddy * ddy);
        if (d < mag && d > 0.5f) { float pull = 190.0f * dt / (d * 0.05f + 1.0f); if (pull > d) pull = d; gx[i] += ddx / d * pull; gy[i] += ddy / d * pull; d = sqrtf((vpx - gx[i]) * (vpx - gx[i]) + (vpy - gy[i]) * (vpy - gy[i])); }
        if (d < 7.0f) { xp += gxp[i]; gxp[i] = 0; }
      }
      // ---- level up ?
      while (xp >= xpNeed) {
        xp -= xpNeed; lvl++; xpNeed = 4 + lvl * 3;
        // choix de 3 bonus differents parmi ceux encore disponibles
        int pool[7], pn = 0;
        for (int i = 0; i < 7; i++) { if (i == 4 && uMul >= 4) continue; pool[pn++] = i; }
        int ch[3];
        for (int i = 0; i < 3 && i < pn; i++) {
          int c; bool ok;
          do { c = pool[rnd(pn)]; ok = true; for (int j = 0; j < i; j++) if (ch[j] == c) ok = false; } while (!ok);
          ch[i] = c;
        }
        int nch = (pn < 3) ? pn : 3, sel = 0;
        waitRelease(); bool pU2 = false, pD2 = false, pOK2 = false, pBk2 = false;
        while (true) { // ecran de choix de bonus (jeu en pause)
          pollKeys();
          if (kUp && !pU2 && sel > 0) sel--;
          if (kDown && !pD2 && sel < nch - 1) sel++;
          pU2 = kUp; pD2 = kDown;
          if (keyOK && !pOK2) { waitRelease(); break; }
          if (keyBack && !pBk2) { waitRelease(); sel = rnd(nch); break; } // retour = bonus aleatoire
          pOK2 = keyOK; pBk2 = keyBack;
          bgPanel();
          drawTextC(16, "NIVEAU SUPERIEUR !", C_YEL);
          char lb[24]; snprintf(lb, 24, "NIVEAU %d", lvl); drawTextC(40, lb, C_WHT);
          for (int i = 0; i < nch; i++) {
            rRect(SW / 2 - 110, 62 + i * 34, 220, 28, 6, sel == i ? C_YEL : KDColor::RGB24(0x3A3A4A));
            drawTextC(70 + i * 34, VS_UPN[ch[i]], sel == i ? C_BLK : C_WHT);
          }
          drawTextC(180, "OK POUR CHOISIR", C_GRE);
          blit(); Ion::Timing::msleep(30);
        }
        int c = ch[sel];
        if (c == 0) uDmg++; else if (c == 1) uRate++; else if (c == 2) uSpd++;
        else if (c == 3) { uHp++; maxHp += 25; hp += 25; if (hp > maxHp) hp = maxHp; }
        else if (c == 4) uMul++; else if (c == 5) uMag++; else uReg++;
      }
      // ---- pause (RETOUR)
      if (keyBack && !prevBk) {
        waitRelease(); bool pOK3 = false, pBk3 = false;
        while (true) {
          pollKeys();
          if (keyOK && !pOK3) { waitRelease(); break; }
          if (keyBack && !pBk3) { waitRelease(); return; }
          pOK3 = keyOK; pBk3 = keyBack;
          bgPanel();
          drawTextC(60, "PAUSE", C_YEL);
          drawTextC(100, "OK REPRENDRE", C_WHT);
          drawTextC(130, "RETOUR QUITTER", C_GRE);
          blit(); Ion::Timing::msleep(30);
        }
      }
    }
    prevBk = keyBack;
    if (kHome) return;
    // ================= rendu =================
    fRect(0, 0, SW, SH, KDColor::RGB24(0x14101E)); // sol sombre violet
    for (int yy = 44; yy < SH; yy += 24) for (int xx = 12; xx < SW; xx += 24) fRect(xx, yy, 2, 2, KDColor::RGB24(0x241A38)); // cailloux
    // barre d'XP (bleu, pleine largeur) + PV (rouge) + stats
    fRect(0, 0, SW, 5, KDColor::RGB24(0x0A0812));
    int xw = (int)((float)SW * xp / xpNeed); if (xw > SW) xw = SW;
    fRect(0, 0, xw, 5, C_CYN);
    int hpw = 80 * hp / maxHp;
    fRect(4, 12, 80, 6, KDColor::RGB24(0x3A0A0A)); fRect(4, 12, hpw, 6, C_RED);
    drawText(90, 10, "PV", C_GRE);
    char hb[16]; snprintf(hb, 16, "%d", hp); drawText(120, 10, hb, C_WHT);
    char tb[16]; snprintf(tb, 16, "T:%d", (int)gameTime); drawText(148, 10, tb, C_YEL);
    char kb[16]; snprintf(kb, 16, "N%d K%d", lvl, kills); drawText(212, 10, kb, C_WHT);
    if (bossMsgT > 0 && ((int)(bossMsgT * 6) & 1) == 0) drawTextC(28, "UN BOSS ARRIVE !", C_RED);
    // ennemis
    for (int i = 0; i < ne; i++) {
      int t = etp[i]; KDColor c = VS_COL[t];
      if (t == 4) { // boss : gros cercle, yeux, barre de vie
        oCirc((int)ex[i], (int)ey[i], 12, c, KDColor::RGB24(0x6D0000));
        fRect((int)ex[i] - 5, (int)ey[i] - 4, 3, 3, C_WHT); fRect((int)ex[i] + 2, (int)ey[i] - 4, 3, 3, C_WHT);
        int bw = ehp[i] * 28 / (VS_HP0[4] + (bossN - 1) * 160); if (bw < 0) bw = 0; if (bw > 28) bw = 28;
        fRect((int)ex[i] - 14, (int)ey[i] - 20, 28, 3, KDColor::RGB24(0x3A0A0A));
        fRect((int)ex[i] - 14, (int)ey[i] - 20, bw, 3, C_RED);
      } else if (t == 0) { // chauve-souris : corps + ailes qui battent
        fRect((int)ex[i] - 7, (int)ey[i] - 2 + ((int)(gameTime * 10) & 1), 4, 2, KDColor::RGB24(0x6A1FA0));
        fRect((int)ex[i] + 3, (int)ey[i] - 2 - ((int)(gameTime * 10) & 1), 4, 2, KDColor::RGB24(0x6A1FA0));
        fCirc((int)ex[i], (int)ey[i], 3, c);
      } else if (t == 2) { // zombie : cercle + yeux rouges
        fCirc((int)ex[i], (int)ey[i], 5, c);
        fRect((int)ex[i] - 3, (int)ey[i] - 2, 2, 2, C_RED); fRect((int)ex[i] + 1, (int)ey[i] - 2, 2, 2, C_RED);
      } else if (t == 3) { // fantome : cercle + franges
        fCirc((int)ex[i], (int)ey[i], 4, c);
        fRect((int)ex[i] - 4, (int)ey[i] + 3, 2, 2, c); fRect((int)ex[i], (int)ey[i] + 4, 2, 2, c); fRect((int)ex[i] + 2, (int)ey[i] + 3, 2, 2, c);
        fRect((int)ex[i] - 2, (int)ey[i] - 2, 2, 2, KDColor::RGB24(0x0A5A6A));
      } else { // squelette : cercle blanc + yeux noirs
        fCirc((int)ex[i], (int)ey[i], 4, c);
        fRect((int)ex[i] - 2, (int)ey[i] - 2, 2, 2, C_BLK); fRect((int)ex[i], (int)ey[i] - 2, 2, 2, C_BLK);
      }
    }
    // ames (XP) : petites etoiles bleues
    for (int i = 0; i < VS_NG; i++) {
      if (gxp[i] == 0) continue;
      int sz = (gxp[i] >= 3) ? 3 : 2;
      fRect((int)gx[i] - sz, (int)gy[i] - sz, sz * 2, sz * 2, C_CYN);
      px((int)gx[i], (int)gy[i] - sz - 1, C_WHT); px((int)gx[i], (int)gy[i] + sz, C_WHT);
    }
    // projectiles : eclats jaunes
    for (int i = 0; i < VS_NP; i++) if (plife[i] > 0) { fRect((int)ppx[i] - 1, (int)ppy[i] - 1, 3, 3, C_YEL); px((int)ppx[i], (int)ppy[i], C_WHT); }
    // joueur (clignote s'il est blesse) + visee
    bool flash = (invT > 0.0f) && (((int)(invT * 12) & 1) == 0);
    if (!flash) {
      fCirc((int)vpx, (int)vpy, 5, C_YEL);
      int ed = faceR ? 1 : -1;
      fRect((int)vpx + ed * 1 - 1, (int)vpy - 3, 2, 2, C_BLK); fRect((int)vpx + ed * 3 - 1, (int)vpy - 3, 2, 2, C_BLK);
      fRect((int)vpx - 2, (int)vpy + 2, 4, 1, KDColor::RGB24(0xB08000));
    }
    blit();
    // ---- mort : ecran de fin + records
    if (dead) {
      int tS = (int)gameTime;
      bool recT = (vsBestT == 0 || tS > vsBestT);
      bool recL = (lvl > vsBestLvl);
      if (recT) vsBestT = tS;
      if (recL) vsBestLvl = lvl;
      vsKillsT += kills;
      saveAcc();
      waitRelease();
      bool pOK4 = false, pBk4 = false;
      while (true) {
        pollKeys();
        if (keyOK && !pOK4) { // rejouer
          waitRelease();
          ne = 0;
          for (int i = 0; i < VS_NG; i++) gxp[i] = 0;
          for (int i = 0; i < VS_NP; i++) plife[i] = 0;
          vpx = SW / 2.0f; vpy = 130.0f;
          maxHp = 100; hp = 100; lvl = 1; xp = 0; xpNeed = 7; kills = 0; bossN = 0;
          uDmg = uRate = uSpd = uHp = uMul = uMag = uReg = 0;
          fireT = invT = regT = gameTime = spawnT = bossMsgT = 0.0f; bossNext = 60;
          dead = false; lastMs = (int)Ion::Timing::millis();
          break;
        }
        if (keyBack && !pBk4) { waitRelease(); return; }
        pOK4 = keyOK; pBk4 = keyBack;
        bgPanel();
        drawTextC(16, "TU ES TOMBE...", C_RED);
        char b1[32]; snprintf(b1, 32, "SURVIE : %d S", tS); drawTextC(48, b1, recT ? C_YEL : C_WHT);
        if (recT) drawTextC(66, "NOUVEAU RECORD !", C_YEL);
        char b2[32]; snprintf(b2, 32, "NIVEAU %d  KILLS %d", lvl, kills); drawTextC(88, b2, C_WHT);
        if (recL) drawTextC(106, "NIVEAU RECORD !", C_YEL);
        char b3[32]; snprintf(b3, 32, "RECORD : %d S", vsBestT); drawTextC(128, b3, C_GRE);
        char b4[32]; snprintf(b4, 32, "KILLS TOTAUX : %d", vsKillsT); drawTextC(146, b4, C_GRE);
        rRect(SW / 2 - 120, 176, 110, 22, 8, C_YEL); drawText(40 + (110 - textW("REJOUER")) / 2, 182, "REJOUER", C_BLK);
        rRect(SW / 2 + 10, 176, 110, 22, 8, KDColor::RGB24(0x3A3A4A)); drawText(170 + (110 - textW("MENU")) / 2, 182, "MENU", C_WHT);
        blit(); Ion::Timing::msleep(30);
      }
    }
    pollKeys();
    Ion::Timing::msleep(16); // ~60 FPS
  }
}


// ======================= TOWER DEFENSE MINI (V5.32) ===================
// Pose des tours sur la grille pour arreter les vagues : le chemin en S
// traverse la carte, chaque ennemi qui atteint la base te coute des PV.
// FLECHES: curseur, OK: poser ou ameliorer une tour, RETOUR: lancer la vague
// en avance (bonus 15$). Boss toutes les 5 vagues. Les tours tirent seules.
static void tdGame() {
  enum { TD_GC = 10, TD_GR = 5, TD_CS = 30, TD_GX = 10, TD_GY = 28 }; // grille 10x5, cases de 30 px
  static const int TD_WC[7] = { 0, 0, 4, 4, 8, 8, 9 }; // chemin en S : colonnes des points de passage
  static const int TD_WR[7] = { 0, 4, 4, 0, 0, 4, 4 }; // lignes des points de passage
  enum { TD_NE = 30, TD_NP = 40 }; // quotas memoire fixes
  static float tex[TD_NE], tey[TD_NE], tspd[TD_NE], tslw[TD_NE];
  static int thp[TD_NE], twp[TD_NE]; static uint8_t ttp[TD_NE];
  static float pjx[TD_NP], pjy[TD_NP], pvx2[TD_NP], pvy2[TD_NP];
  static int plife2[TD_NP], psp2[TD_NP], pjd[TD_NP];
  static uint8_t tdT[50], tdL[50]; static float tdCd[50];
  static bool onPath[50]; // cellules occupees par le chemin
  static const int TD_EHP[4]  = { 30, 18, 90, 380 };   // PV de base : normal, rapide, tank, boss
  static const int TD_ESP[4]  = { 24, 44, 14, 9 };     // vitesse (px/s)
  static const int TD_ERW[4]  = { 8, 10, 22, 60 };     // or a la mort
  static const int TD_ERD[4]  = { 4, 3, 5, 9 };        // rayon
  static const KDColor TD_ECOL[4] = { KDColor::RGB24(0x3DDC55), KDColor::RGB24(0xFFC928), KDColor::RGB24(0x9A9AA8), KDColor::RGB24(0xB44BFF) };
  static const int TD_TPR[4] = { 0, 50, 90, 70 };      // prix des tours
  static const int TD_TRG[4] = { 0, 60, 55, 50 };      // portee
  static const int TD_TDM[4] = { 0, 7, 16, 3 };        // degats de base
  static const int TD_TDM2[4] = { 0, 5, 9, 2 };        // degats gagnes par niveau
  static const float TD_TRT[4] = { 0, 0.45f, 1.0f, 0.7f }; // cadence (s)
  static const KDColor TD_TCOL[4] = { C_WHT, KDColor::RGB24(0x35D4E8), KDColor::RGB24(0xFF8327), KDColor::RGB24(0xA8D8F0) };
  static const char *TD_TN[4] = { "", "FLECHE", "CANON", "GLACE" };
  static const char *TD_TDESC[4] = { "", "RAPIDE", "ZONE", "RALENTIT" };
  int money = 120, lives = 10, wave = 0, kills = 0, spawnLeft = 0, bossLeft = 0, ne = 0;
  int ccx = 1, ccy = 3;
  float spawnT2 = 0.0f, interT = 6.0f; bool inWave = false, over = false;
  bool pL2 = false, pR2 = false, pU2 = false, pD2 = false, pOK2 = false, pBk2 = false;
  bool fresh = true;
  waitRelease();
  int lastMs = (int)Ion::Timing::millis();
  while (true) { // ================= boucle principale =================
    if (fresh) { // (re)demarrage d'une partie
      fresh = false;
      money = 120; lives = 10; wave = 0; kills = 0; spawnLeft = 0; bossLeft = 0; ne = 0;
      ccx = 1; ccy = 3; spawnT2 = 0.0f; interT = 6.0f; inWave = false; over = false;
      for (int i = 0; i < 50; i++) { tdT[i] = 0; tdL[i] = 0; tdCd[i] = 0.0f; onPath[i] = false; }
      for (int w = 0; w < 6; w++) { // trace du chemin : chaque segment marque ses cellules
        int x0 = TD_WC[w], y0 = TD_WR[w], x1 = TD_WC[w + 1], y1 = TD_WR[w + 1];
        int sx = (x1 > x0) ? 1 : -1, sy = (y1 > y0) ? 1 : -1;
        int x = x0, y = y0;
        while (x != x1) { onPath[y * TD_GC + x] = true; x += sx; }
        while (y != y1) { onPath[y * TD_GC + x] = true; y += sy; }
      }
      onPath[TD_WR[6] * TD_GC + TD_WC[6]] = true; // dernier point : la base
      for (int i = 0; i < TD_NP; i++) plife2[i] = 0;
      lastMs = (int)Ion::Timing::millis();
    }
    int nowMs = (int)Ion::Timing::millis();
    float dt = (nowMs - lastMs) / 1000.0f; lastMs = nowMs;
    if (dt > 0.05f) dt = 0.05f; if (dt <= 0.0f) dt = 0.0001f;
    pollKeys();
    // ---- curseur sur la grille
    if (kLeft && !pL2 && ccx > 0) ccx--;
    if (kRight && !pR2 && ccx < TD_GC - 1) ccx++;
    if (kUp && !pU2 && ccy > 0) ccy--;
    if (kDown && !pD2 && ccy < TD_GR - 1) ccy++;
    int cur = ccy * TD_GC + ccx;
    if (keyOK && !pOK2) {
      waitRelease();
      if (tdT[cur] == 0 && !onPath[cur]) { // ---- boutique de tours
        int sel = 1;
        while (true) {
          pollKeys();
          if (kUp && !pU2 && sel > 1) sel--;
          if (kDown && !pD2 && sel < 3) sel++;
          pU2 = kUp; pD2 = kDown;
          if (keyOK && !pOK2) { // achat si assez d'or
            waitRelease();
            if (money >= TD_TPR[sel]) { money -= TD_TPR[sel]; tdT[cur] = (uint8_t)sel; tdL[cur] = 1; tdCd[cur] = 0.0f; }
            break;
          }
          if (keyBack && !pBk2) { waitRelease(); break; }
          pOK2 = keyOK; pBk2 = keyBack;
          bgPanel();
          drawTextC(16, "CHOISIR UNE TOUR", C_YEL);
          for (int i = 1; i <= 3; i++) {
            char sbuf[40]; snprintf(sbuf, 40, "%s : %s  %d$", TD_TN[i], TD_TDESC[i], TD_TPR[i]);
            rRect(SW / 2 - 130, 50 + i * 34, 260, 28, 6, sel == i ? C_YEL : KDColor::RGB24(0x3A3A4A));
            drawTextC(57 + i * 34, sbuf, sel == i ? C_BLK : C_WHT);
          }
          char mbuf[24]; snprintf(mbuf, 24, "TON OR : %d$", money); drawTextC(170, mbuf, C_GOLD);
          drawTextC(192, "OK ACHETER  RETOUR ANNULE", C_GRE);
          blit(); Ion::Timing::msleep(30);
        }
      } else if (tdT[cur] > 0) { // ---- amelioration de tour (NV1->2 : 40$, NV2->3 : 80$)
        if (tdL[cur] < 3) {
          int cost = 40 * tdL[cur];
          if (money >= cost) { money -= cost; tdL[cur]++; }
        }
      }
    }
    // ---- RETOUR : lancer la vague en avance (bonus 15$) sinon decompte auto
    if (keyBack && !pBk2 && !inWave && !over) {
      waitRelease(); money += 15; interT = 0.0f;
    }
    pL2 = kLeft; pR2 = kRight; pU2 = kUp; pD2 = kDown; pOK2 = keyOK; pBk2 = keyBack;
    if (kHome) return;
    if (!over) {
      // ---- debut de vague (auto apres decompte, ou RETOUR)
      if (!inWave && interT <= 0.0f) {
        inWave = true; wave++; spawnLeft = 5 + wave * 2; bossLeft = (wave % 5 == 0) ? 1 : 0; spawnT2 = 0.0f;
      }
      if (!inWave) interT -= dt;
      // ---- apparitions des ennemis (0.8 s d'intervalle)
      if (inWave && spawnLeft > 0) {
        spawnT2 -= dt;
        if (spawnT2 <= 0.0f) {
          spawnT2 = 0.8f; spawnLeft--;
          if (ne < TD_NE) {
            int t = 0;
            if (bossLeft > 0 && spawnLeft < 3) { t = 3; bossLeft--; } // le boss clot la vague
            else if (wave >= 4 && rnd(5) == 0) t = 2;
            else if (wave >= 2 && rnd(100) < 35) t = 1;
            tex[ne] = TD_GX + TD_WC[0] * TD_CS + TD_CS / 2.0f; tey[ne] = TD_GY + TD_WR[0] * TD_CS + TD_CS / 2.0f;
            twp[ne] = 1; ttp[ne] = (uint8_t)t; tslw[ne] = 0.0f;
            tspd[ne] = TD_ESP[t] * (1.0f + wave * 0.03f);
            thp[ne] = (int)(TD_EHP[t] * (1.0f + (wave - 1) * 0.25f)); ne++;
          }
        }
      }
      // ---- avancee des ennemis le long du chemin
      for (int i = 0; i < ne; i++) {
        float sp = tspd[i]; if (tslw[i] > 0.0f) { tslw[i] -= dt; sp *= 0.5f; }
        float cx2 = TD_GX + TD_WC[twp[i]] * TD_CS + TD_CS / 2.0f;
        float cy2 = TD_GY + TD_WR[twp[i]] * TD_CS + TD_CS / 2.0f;
        float ddx = cx2 - tex[i], ddy = cy2 - tey[i];
        float n = sqrtf(ddx * ddx + ddy * ddy);
        if (n <= sp * dt + 1.0f) { // point de passage atteint
          tex[i] = cx2; tey[i] = cy2; twp[i]++;
          if (twp[i] > 6) { // ENNEMI DANS LA BASE : des PV en moins
            lives -= (ttp[i] == 3) ? 3 : 1;
            ne--; tex[i] = tex[ne]; tey[i] = tey[ne]; tspd[i] = tspd[ne]; tslw[i] = tslw[ne];
            thp[i] = thp[ne]; twp[i] = twp[ne]; ttp[i] = ttp[ne];
            i--; if (lives <= 0) { lives = 0; over = true; }
          }
        } else { tex[i] += ddx / n * sp * dt; tey[i] += ddy / n * sp * dt; }
      }
      // ---- tir des tours sur l'ennemi le plus proche dans la portee
      for (int c = 0; c < 50; c++) {
        if (tdT[c] == 0) continue;
        tdCd[c] -= dt;
        if (tdCd[c] > 0.0f) continue;
        int cx2 = TD_GX + (c % TD_GC) * TD_CS + TD_CS / 2;
        int cy2 = TD_GY + (c / TD_GC) * TD_CS + TD_CS / 2;
        float rg = TD_TRG[tdT[c]] + 7 * (tdL[c] - 1);
        int bi = -1; float bd = rg * rg;
        for (int i = 0; i < ne; i++) {
          float ddx = tex[i] - cx2, ddy = tey[i] - cy2; float d2 = ddx * ddx + ddy * ddy;
          if (d2 < bd) { bd = d2; bi = i; }
        }
        if (bi >= 0) {
          tdCd[c] = TD_TRT[tdT[c]] / (1.0f + 0.2f * (tdL[c] - 1));
          float a = atan2f(tey[bi] - cy2, tex[bi] - cx2);
          int slot = -1;
          for (int j = 0; j < TD_NP; j++) { if (plife2[j] <= 0) { slot = j; break; } }
          if (slot >= 0) {
            pjx[slot] = cx2; pjy[slot] = cy2; pvx2[slot] = fcosf(a) * 200.0f; pvy2[slot] = fsinf(a) * 200.0f;
            plife2[slot] = 90; psp2[slot] = tdT[c]; pjd[slot] = TD_TDM[tdT[c]] + TD_TDM2[tdT[c]] * (tdL[c] - 1);
          }
        }
      }
      // ---- projectiles : vol, impact, zone d'effet du canon, gel de la glace
      for (int i = 0; i < TD_NP; i++) {
        if (plife2[i] <= 0) continue;
        plife2[i]--; pjx[i] += pvx2[i] * dt; pjy[i] += pvy2[i] * dt;
        if (pjx[i] < 0 || pjx[i] > SW || pjy[i] < 24 || pjy[i] > 180) { plife2[i] = 0; continue; }
        bool hit = false;
        for (int j = 0; j < ne; j++) {
          float ddx = tex[j] - pjx[i], ddy = tey[j] - pjy[i];
          int rr = TD_ERD[ttp[j]] + 3;
          if (ddx * ddx + ddy * ddy < rr * rr) { hit = true; break; }
        }
        if (hit) {
          if (psp2[i] == 2) { // obus : explose et touche tout dans un rayon de 22 px
            for (int j = 0; j < ne; j++) {
              float ddx = tex[j] - pjx[i], ddy = tey[j] - pjy[i];
              if (ddx * ddx + ddy * ddy < 22.0f * 22.0f) thp[j] -= pjd[i];
            }
          } else {
            for (int j = 0; j < ne; j++) { // cherche l'ennemi touche
              float ddx = tex[j] - pjx[i], ddy = tey[j] - pjy[i];
              int rr = TD_ERD[ttp[j]] + 3;
              if (ddx * ddx + ddy * ddy < rr * rr) {
                thp[j] -= pjd[i];
                if (psp2[i] == 3) tslw[j] = 2.0f; // tour de glace : ralenti 2 s
                break;
              }
            }
          }
          plife2[i] = 0;
        }
      }
      // ---- ramasse les ennemis morts (or + kills)
      for (int j = ne - 1; j >= 0; j--) {
        if (thp[j] <= 0) {
          kills++; money += TD_ERW[ttp[j]];
          ne--; tex[j] = tex[ne]; tey[j] = tey[ne]; tspd[j] = tspd[ne]; tslw[j] = tslw[ne];
          thp[j] = thp[ne]; twp[j] = twp[ne]; ttp[j] = ttp[ne];
        }
      }
      // ---- fin de vague : bonus d'or puis decompte avant la suivante
      if (inWave && spawnLeft == 0 && ne == 0) { inWave = false; money += 30 + wave * 5; interT = 8.0f; }
    }
    // ================= rendu =================
    fRect(0, 0, SW, SH, KDColor::RGB24(0x1A2028));
    fRect(0, 0, SW, 24, KDColor::RGB24(0x10161E)); fRect(0, 24, SW, 2, C_BTND); // HUD haut
    char mb[20]; snprintf(mb, 20, "OR:%d", money); drawText(8, 8, mb, C_GOLD);
    char lb2[20]; snprintf(lb2, 20, "PV:%d", lives); drawText(120, 8, lb2, C_RED);
    char wb[20]; snprintf(wb, 20, "VAGUE %d", wave); drawText(230, 8, wb, C_YEL);
    // ---- grille (chemin plus clair, cases constructibles sombres)
    for (int gy = 0; gy < TD_GR; gy++) for (int gx = 0; gx < TD_GC; gx++) {
      int pxl = TD_GX + gx * TD_CS, pyl = TD_GY + gy * TD_CS;
      bool onP = onPath[gy * TD_GC + gx];
      fRect(pxl, pyl, TD_CS - 1, TD_CS - 1, onP ? KDColor::RGB24(0x4E4234) : KDColor::RGB24(0x242E3A));
      fRect(pxl, pyl, TD_CS - 1, 2, onP ? KDColor::RGB24(0x5E5040) : KDColor::RGB24(0x2C3A4A));
    }
    // ---- portail d'entree (vert) et base a proteger (bleue)
    rRect(TD_GX + TD_WC[0] * TD_CS + 8, TD_GY + TD_WR[0] * TD_CS + 8, 14, 14, 4, C_GREEN);
    int bx = TD_GX + TD_WC[6] * TD_CS + 8, by = TD_GY + TD_WR[6] * TD_CS + 8;
    fRect(bx, by, 14, 14, KDColor::RGB24(0x2E9BFF));
    fRect(bx + 5, by + 3, 4, 8, C_RED); fRect(bx + 3, by + 5, 8, 4, C_RED); // croix rouge
    // ---- tours (couleur par type, taille selon niveau, points de niveau)
    for (int c = 0; c < 50; c++) {
      if (tdT[c] == 0) continue;
      int cx2 = TD_GX + (c % TD_GC) * TD_CS + TD_CS / 2;
      int cy2 = TD_GY + (c / TD_GC) * TD_CS + TD_CS / 2;
      int r = 6 + tdL[c];
      fCirc(cx2, cy2, r, TD_TCOL[tdT[c]]);
      fRect(cx2 - 1, cy2 - r - 3, 2, 3, KDColor::RGB24(0x606060)); // canon vers le haut
      for (int l = 0; l < tdL[c]; l++) fRect(cx2 - 4 + l * 4, cy2 + r + 2, 2, 2, C_YEL); // points de niveau
    }
    // ---- ennemis + barre de vie + marqueur de gel
    for (int i = 0; i < ne; i++) {
      int t = ttp[i];
      fCirc((int)tex[i], (int)tey[i], TD_ERD[t], TD_ECOL[t]);
      if (t == 3) { fRect((int)tex[i] - 4, (int)tey[i] - 3, 2, 2, C_WHT); fRect((int)tex[i] + 2, (int)tey[i] - 3, 2, 2, C_WHT); } // yeux du boss
      int maxhp = (int)(TD_EHP[t] * (1.0f + (wave - 1) * 0.25f)); if (maxhp < 1) maxhp = 1;
      int bw = 12 * thp[i] / maxhp; if (bw < 0) bw = 0; if (bw > 12) bw = 12;
      fRect((int)tex[i] - 6, (int)tey[i] - TD_ERD[t] - 7, 12, 2, KDColor::RGB24(0x3A0A0A));
      fRect((int)tex[i] - 6, (int)tey[i] - TD_ERD[t] - 7, bw, 2, C_RED);
      if (tslw[i] > 0.0f) fRect((int)tex[i] - 2, (int)tey[i] + TD_ERD[t] + 1, 4, 2, KDColor::RGB24(0xA8D8F0)); // marqueur de gel
    }
    // ---- projectiles
    for (int i = 0; i < TD_NP; i++) if (plife2[i] > 0)
      fRect((int)pjx[i] - 1, (int)pjy[i] - 1, 3, 3, psp2[i] == 1 ? C_CYN : (psp2[i] == 2 ? C_ORG : KDColor::RGB24(0xA8D8F0)));
    // ---- curseur (cadre jaune)
    int ux = TD_GX + ccx * TD_CS, uy = TD_GY + ccy * TD_CS;
    fRect(ux, uy, TD_CS - 1, 2, C_YEL); fRect(ux, uy + TD_CS - 3, TD_CS - 1, 2, C_YEL);
    fRect(ux, uy, 2, TD_CS - 1, C_YEL); fRect(ux + TD_CS - 3, uy, 2, TD_CS - 1, C_YEL);
    // ---- barre du bas : infos et etat de la vague
    fRect(0, 180, SW, SH - 180, KDColor::RGB24(0x10161E)); fRect(0, 180, SW, 2, C_BTND);
    char b1[40];
    if (tdT[cur] > 0) {
      if (tdL[cur] < 3) snprintf(b1, 40, "%s NV%d : AMELIORER %d$", TD_TN[tdT[cur]], tdL[cur], 40 * tdL[cur]);
      else snprintf(b1, 40, "%s NV3 : NIVEAU MAX", TD_TN[tdT[cur]]);
    } else if (!onPath[cur]) snprintf(b1, 40, "OK : POSER UNE TOUR");
    else snprintf(b1, 40, "CHEMIN : IMPOSSIBLE ICI");
    drawText(8, 190, b1, C_WHT);
    char b2[40];
    if (inWave) {
      int rest = spawnLeft + ne;
      snprintf(b2, 40, "VAGUE %d : %d RESTANTS", wave, rest);
      drawText(8, 206, b2, C_YEL);
      if (bossLeft > 0) drawText(170, 206, "UN BOSS ARRIVE !", KDColor::RGB24(0xB44BFF));
    } else {
      snprintf(b2, 40, "VAGUE %d DANS %d S", wave + 1, (int)interT + 1);
      drawText(8, 206, b2, C_GRE);
      drawText(170, 206, "RETOUR: LANCER +15$", C_GOLD);
    }
    blit();
    // ---- game over : records + ecran de fin
    if (over) {
      bool recW = (wave > tdBestW);
      if (recW) tdBestW = wave;
      tdWavesT += wave; tdKillsT += kills;
      saveAcc();
      waitRelease();
      bool pOK3 = false, pBk3 = false;
      while (true) {
        pollKeys();
        if (keyOK && !pOK3) { waitRelease(); fresh = true; break; } // rejouer
        if (keyBack && !pBk3) { waitRelease(); return; }
        pOK3 = keyOK; pBk3 = keyBack;
        bgPanel();
        drawTextC(16, "LA BASE EST TOMEE...", C_RED);
        char e1[40]; snprintf(e1, 40, "VAGUE ATTEINTE : %d", wave); drawTextC(48, e1, recW ? C_YEL : C_WHT);
        if (recW) drawTextC(66, "NOUVEAU RECORD !", C_YEL);
        char e2[40]; snprintf(e2, 40, "KILLS : %d", kills); drawTextC(88, e2, C_WHT);
        char e3[40]; snprintf(e3, 40, "RECORD VAGUE : %d", tdBestW); drawTextC(110, e3, C_GRE);
        char e4[40]; snprintf(e4, 40, "TOTAL : %dV %dK", tdWavesT, tdKillsT); drawTextC(132, e4, C_GRE);
        rRect(SW / 2 - 120, 176, 110, 22, 8, C_YEL); drawText(40 + (110 - textW("REJOUER")) / 2, 182, "REJOUER", C_BLK);
        rRect(SW / 2 + 10, 176, 110, 22, 8, KDColor::RGB24(0x3A3A4A)); drawText(170 + (110 - textW("MENU")) / 2, 182, "MENU", C_WHT);
        blit(); Ion::Timing::msleep(30);
      }
    }
    Ion::Timing::msleep(16); // ~60 FPS
  }
}


// ======================= OTHELLO MINI (V5.32) =========================
// Reversi 8x8 contre l'IA : noir = toi, blanc = l'IA. Choisis ton niveau
// (FACILE, MOYEN, DIFFICILE), tes coups legaux clignotent en jaune.
static int otLegal(uint8_t *b, int i, int p) { // nombre de pions retournes si p joue en i (0 = illegal)
  if (b[i] != 0) return 0;
  static const int DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
  static const int DY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
  int x = i % 8, y = i / 8, q = (p == 1) ? 2 : 1, flips = 0;
  for (int d = 0; d < 8; d++) {
    int cx = x + DX[d], cy = y + DY[d], run = 0;
    while (cx >= 0 && cx < 8 && cy >= 0 && cy < 8 && b[cy * 8 + cx] == q) { run++; cx += DX[d]; cy += DY[d]; }
    if (run > 0 && cx >= 0 && cx < 8 && cy >= 0 && cy < 8 && b[cy * 8 + cx] == p) flips += run;
  }
  return flips;
}
static int otMoves(uint8_t *b, int p, int *out) { // liste des coups legaux de p
  int n = 0;
  for (int i = 0; i < 64; i++) if (otLegal(b, i, p) > 0) out[n++] = i;
  return n;
}
static void otPlay(uint8_t *b, int i, int p) { // pose le pion et retourne les encadrements
  static const int DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
  static const int DY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
  int x = i % 8, y = i / 8, q = (p == 1) ? 2 : 1;
  b[i] = (uint8_t)p;
  for (int d = 0; d < 8; d++) {
    int cx = x + DX[d], cy = y + DY[d], run = 0;
    while (cx >= 0 && cx < 8 && cy >= 0 && cy < 8 && b[cy * 8 + cx] == q) { run++; cx += DX[d]; cy += DY[d]; }
    if (run > 0 && cx >= 0 && cx < 8 && cy >= 0 && cy < 8 && b[cy * 8 + cx] == p) {
      int fx = x + DX[d], fy = y + DY[d];
      while (fx != cx || fy != cy) { b[fy * 8 + fx] = (uint8_t)p; fx += DX[d]; fy += DY[d]; }
    }
  }
}
static const int otW[64] = { // poids positionnels : coins forts, bords moyens, adjacents aux coins faibles
  9, 2, 5, 5, 5, 5, 2, 9,  2, 1, 3, 3, 3, 3, 1, 2,  5, 3, 4, 4, 4, 4, 3, 5,
  5, 3, 4, 4, 4, 4, 3, 5,  5, 3, 4, 4, 4, 4, 3, 5,  5, 3, 4, 4, 4, 4, 3, 5,
  2, 1, 3, 3, 3, 3, 1, 2,  9, 2, 5, 5, 5, 5, 2, 9
};
static int otScore(uint8_t *b) { // evaluation : + = bon pour l'IA (blanc)
  int s = 0;
  for (int i = 0; i < 64; i++) { if (b[i] == 2) s += otW[i]; else if (b[i] == 1) s -= otW[i]; }
  return s;
}
static int otSearch(uint8_t *b, int depth, int alpha, int beta, int p) { // minimax alpha-beta
  int mv[64], n = otMoves(b, p, mv);
  if (n == 0) {
    int mv2[64];
    if (otMoves(b, (p == 1) ? 2 : 1, mv2) == 0) { // fin de partie : difference de pions, tres lourde
      int bl = 0, wh = 0;
      for (int i = 0; i < 64; i++) { if (b[i] == 1) bl++; else if (b[i] == 2) wh++; }
      return (wh - bl) * 100;
    }
    return otSearch(b, depth, alpha, beta, (p == 1) ? 2 : 1); // passe sans consommer de profondeur
  }
  if (depth <= 0) return otScore(b);
  if (p == 2) { // l'IA maximise
    int best = -1000000;
    for (int k = 0; k < n; k++) {
      uint8_t nb[64];
      for (int i = 0; i < 64; i++) nb[i] = b[i];
      otPlay(nb, mv[k], 2);
      int v = otSearch(nb, depth - 1, alpha, beta, 1);
      if (v > best) best = v;
      if (best > alpha) alpha = best;
      if (alpha >= beta) break; // coupe
    }
    return best;
  }
  int best = 1000000; // le joueur minimise
  for (int k = 0; k < n; k++) {
    uint8_t nb[64];
    for (int i = 0; i < 64; i++) nb[i] = b[i];
    otPlay(nb, mv[k], 1);
    int v = otSearch(nb, depth - 1, alpha, beta, 2);
    if (v < best) best = v;
    if (best < beta) beta = best;
    if (beta <= alpha) break; // coupe
  }
  return best;
}
static void otDraw(uint8_t *b, int ccx, int ccy, int *pm, int np, const char *msg, KDColor mc, int lvlN) {
  fRect(0, 0, SW, SH, KDColor::RGB24(0x1A2028));
  int x0 = 72, y0 = 28, cs = 22; // plateau 176x176 centre
  for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
    fRect(x0 + x * cs, y0 + y * cs, cs - 1, cs - 1, KDColor::RGB24(0x3D7A20));
    fRect(x0 + x * cs, y0 + y * cs, cs - 1, 2, KDColor::RGB24(0x2E5A16));
  }
  for (int i = 0; i < 64; i++) { // pions poses
    if (b[i] == 0) continue;
    int cx2 = x0 + (i % 8) * cs + cs / 2, cy2 = y0 + (i / 8) * cs + cs / 2;
    if (b[i] == 1) { fCirc(cx2, cy2, 8, C_GRE); fCirc(cx2, cy2, 7, KDColor::RGB24(0x151515)); }
    else fCirc(cx2, cy2, 8, C_WHT);
  }
  int t2 = (int)Ion::Timing::millis();
  for (int k = 0; k < np; k++) // coups legaux : petits points jaunes qui clignotent
    if (((t2 / 250) & 1) == 0) fRect(x0 + (pm[k] % 8) * cs + cs / 2 - 2, y0 + (pm[k] / 8) * cs + cs / 2 - 2, 4, 4, C_YEL);
  int ux = x0 + ccx * cs, uy = y0 + ccy * cs; // curseur jaune
  fRect(ux, uy, cs - 1, 2, C_YEL); fRect(ux, uy + cs - 3, cs - 1, 2, C_YEL);
  fRect(ux, uy, 2, cs - 1, C_YEL); fRect(ux + cs - 3, uy, 2, cs - 1, C_YEL);
  int bl = 0, wh = 0;
  for (int i = 0; i < 64; i++) { if (b[i] == 1) bl++; else if (b[i] == 2) wh++; }
  fRect(0, 208, SW, SH - 208, KDColor::RGB24(0x10161E));
  char sb[32]; snprintf(sb, 32, "NOIR %d - %d BLANC", bl, wh); drawText(6, 212, sb, C_WHT);
  char tb2[32]; snprintf(tb2, 32, "NV IA %d", lvlN); drawText(240, 212, tb2, C_GRE);
  drawTextC(6, msg, mc); // message d'etat en haut, au-dessus du plateau
  blit();
}
static void otGame() {
  static uint8_t bo[64];
  static const int OT_DEPTHS[3] = { 1, 3, 5 };
  waitRelease();
  int lvl = 0;
  { // ---- choix du niveau de l'IA
    static const char *OT_LV[3] = { "FACILE (IA RAPIDE)", "MOYEN", "DIFFICILE (IA LONGUE)" };
    int sel = 1; bool pU2 = false, pD2 = false, pOK2 = false, pBk2 = false;
    while (true) {
      pollKeys();
      if (kUp && !pU2 && sel > 0) sel--;
      if (kDown && !pD2 && sel < 2) sel++;
      if (keyOK && !pOK2) { waitRelease(); lvl = sel; break; }
      if (keyBack && !pBk2) { waitRelease(); return; }
      pU2 = kUp; pD2 = kDown; pOK2 = keyOK; pBk2 = keyBack;
      bgPanel();
      drawTextC(16, "OTHELLO : NIVEAU IA", C_YEL);
      drawTextC(44, "NOIR = TOI, BLANC = IA", C_GRE);
      for (int i = 0; i < 3; i++) {
        rRect(SW / 2 - 130, 70 + i * 34, 260, 28, 6, sel == i ? C_YEL : KDColor::RGB24(0x3A3A4A));
        drawTextC(77 + i * 34, OT_LV[i], sel == i ? C_BLK : C_WHT);
      }
      drawTextC(186, "OK POUR JOUER", C_GRE);
      blit(); Ion::Timing::msleep(30);
    }
  }
  int depth = OT_DEPTHS[lvl];
  bool fresh = true;
  while (true) { // une partie complete
    if (fresh) {
      fresh = false;
      for (int i = 0; i < 64; i++) bo[i] = 0;
      bo[27] = 2; bo[28] = 1; bo[35] = 1; bo[36] = 2; // disposition de depart
    }
    int turn = 1, ccx = 3, ccy = 3; // noir commence
    bool pL2 = false, pR2 = false, pU2 = false, pD2 = false, pOK2 = false, pBk2 = false;
    bool quit = false;
    while (true) { // tour par tour
      int pm[64], ai[64];
      int np = otMoves(bo, 1, pm), na = otMoves(bo, 2, ai);
      if (np == 0 && na == 0) break; // partie finie : plus aucun coup nulle part
      if (turn == 1 && np == 0) { // le joueur doit passer
        waitRelease();
        bool pOK3 = false;
        while (true) { pollKeys(); if (keyOK && !pOK3) { waitRelease(); break; } pOK3 = keyOK;
          bgPanel(); drawTextC(90, "TU PASSES !", C_YEL); drawTextC(120, "OK POUR CONTINUER", C_GRE); blit(); Ion::Timing::msleep(30); }
        turn = 2; continue;
      }
      if (turn == 2) { // ---- tour de l'IA
        if (na == 0) { // l'IA passe
          waitRelease();
          bool pOK3 = false;
          while (true) { pollKeys(); if (keyOK && !pOK3) { waitRelease(); break; } pOK3 = keyOK;
            bgPanel(); drawTextC(90, "L'IA PASSE !", C_YEL); drawTextC(120, "OK POUR CONTINUER", C_GRE); blit(); Ion::Timing::msleep(30); }
          turn = 1; continue;
        }
        otDraw(bo, ccx, ccy, pm, np, "L'IA REFLECHIT...", C_GRE, lvl + 1); // affiche avant de calculer
        int best = -1000000, bm = ai[0];
        for (int k = 0; k < na; k++) {
          uint8_t nb[64];
          for (int i = 0; i < 64; i++) nb[i] = bo[i];
          otPlay(nb, ai[k], 2);
          int v = otSearch(nb, depth - 1, -1000000, 1000000, 1);
          if (v > best) { best = v; bm = ai[k]; }
        }
        otPlay(bo, bm, 2);
        turn = 1; continue;
      }
      // ---- tour du joueur : fleches pour le curseur, OK pour poser
      pollKeys();
      if (kLeft && !pL2 && ccx > 0) ccx--;
      if (kRight && !pR2 && ccx < 7) ccx++;
      if (kUp && !pU2 && ccy > 0) ccy--;
      if (kDown && !pD2 && ccy < 7) ccy++;
      if (keyBack && !pBk2) { waitRelease(); quit = true; break; } // abandon
      if (keyOK && !pOK2) {
        int idx = ccy * 8 + ccx;
        if (otLegal(bo, idx, 1) > 0) { otPlay(bo, idx, 1); waitRelease(); turn = 2; }
        // coup illegal : rien, on laisse le curseur la ou il est
      }
      pL2 = kLeft; pR2 = kRight; pU2 = kUp; pD2 = kDown; pOK2 = keyOK; pBk2 = keyBack;
      if (kHome) return;
      otDraw(bo, ccx, ccy, pm, np, "A TOI DE JOUER", C_YEL, lvl + 1);
      Ion::Timing::msleep(30);
    }
    if (quit) return;
    // ---- fin de partie : denombrement et resultat
    int bl = 0, wh = 0;
    for (int i = 0; i < 64; i++) { if (bo[i] == 1) bl++; else if (bo[i] == 2) wh++; }
    int res = (bl > wh) ? 1 : ((wh > bl) ? 2 : 0); // 1 = joueur gagne, 2 = IA gagne, 0 = nul
    if (res == 1) otWins++;
    else if (res == 2) otLosses++;
    else otDraws++;
    saveAcc();
    waitRelease();
    bool pOK3 = false, pBk3 = false;
    while (true) {
      pollKeys();
      if (keyOK && !pOK3) { waitRelease(); fresh = true; break; } // rejouer au meme niveau
      if (keyBack && !pBk3) { waitRelease(); return; }
      pOK3 = keyOK; pBk3 = keyBack;
      bgPanel();
      char r1[32];
      snprintf(r1, 32, "NOIR %d - %d BLANC", bl, wh); drawTextC(40, r1, C_WHT);
      if (res == 1) drawTextC(16, "TU AS GAGNE !", C_YEL);
      else if (res == 2) drawTextC(16, "L'IA GAGNE...", C_RED);
      else drawTextC(16, "MATCH NUL", C_GRE);
      char r2[40]; snprintf(r2, 40, "VICTOIRES %d DEFAITES %d NULS %d", otWins, otLosses, otDraws);
      drawTextC(80, r2, C_GRE);
      rRect(SW / 2 - 120, 176, 110, 22, 8, C_YEL); drawText(40 + (110 - textW("REJOUER")) / 2, 182, "REJOUER", C_BLK);
      rRect(SW / 2 + 10, 176, 110, 22, 8, KDColor::RGB24(0x3A3A4A)); drawText(170 + (110 - textW("MENU")) / 2, 182, "MENU", C_WHT);
      blit(); Ion::Timing::msleep(30);
    }
  }
}

// ======================= DONJON ROGUELIKE MINI (V5.32) =================
// Explore des donjons generes aleatoirement, bats les monstres, ramasse
// les potions et l'or, descends le plus loin possible. Tour par tour :
// FLECHES pour bouger/frapper, OK pour boire une potion.
static void dgGame() {
  enum { DGW = 20, DGH = 12, DGT = 16, DGY = 14, DG_NE = 14 }; // grille 20x12, tuiles 16 px
  static uint8_t dgMap[DGW * DGH]; // 0 sol, 1 mur, 2 escalier, 3 potion, 4 or, 5 epee
  static int ex2[DG_NE], ey2[DG_NE], ehp2[DG_NE]; static uint8_t etp2[DG_NE];
  static bool seen[DGW * DGH]; static int dist[DGW * DGH];
  static int qx[DGW * DGH], qy[DGW * DGH];
  static const int DG_EHP[4] = { 4, 6, 9, 26 };
  static const int DG_EAT[4] = { 1, 2, 3, 5 };
  static const char *DG_EN[4] = { "RAT", "GELATINE", "SQUELETTE", "BOSS" };
  static const KDColor DG_ECOL[4] = { KDColor::RGB24(0x9A7A5A), KDColor::RGB24(0x3DDC55), KDColor::RGB24(0xE8E8E8), KDColor::RGB24(0xC51111) };
  int px2 = 1, py2 = 1, ne = 0, hp = 20, maxHp = 20, atk = 3, pots = 1, gold = 0, floor = 1, kills = 0;
  bool newRun = true, genFloor = false;
  waitRelease();
  bool pU2 = false, pD2 = false, pL2 = false, pR2 = false, pOK2 = false, pBk2 = false;
  char log1[40];
  snprintf(log1, 40, "ETAGE 1 : TROUVE L'ESCALIER");
  while (true) { // ================= boucle principale =================
    if (newRun) { // ---- nouvelle partie
      newRun = false;
      px2 = 1; py2 = 1; ne = 0; hp = 20; maxHp = 20; atk = 3; pots = 1; gold = 0; floor = 1; kills = 0;
      snprintf(log1, 40, "ETAGE 1 : TROUVE L'ESCALIER");
      genFloor = true;
    }
    if (genFloor) { // ---- generation de l'etage (murs aleatoires, tout est toujours atteignable)
      genFloor = false;
      for (int y = 0; y < DGH; y++) for (int x = 0; x < DGW; x++)
        dgMap[y * DGW + x] = (x == 0 || y == 0 || x == DGW - 1 || y == DGH - 1 || rnd(100) < 13) ? 1 : 0;
      px2 = 1 + rnd(DGW - 2); py2 = 1 + rnd(DGH - 2);
      dgMap[py2 * DGW + px2] = 0;
      for (int i = 0; i < DGW * DGH; i++) seen[i] = false; // acces : onde depuis le joueur
      int qh = 0, qt = 0;
      qx[qt] = px2; qy[qt] = py2; qt++; seen[py2 * DGW + px2] = true; dist[py2 * DGW + px2] = 0;
      while (qh < qt) {
        int cx2 = qx[qh], cy2 = qy[qh]; qh++;
        static const int DX[4] = { 1, -1, 0, 0 }; static const int DY[4] = { 0, 0, 1, -1 };
        for (int d = 0; d < 4; d++) {
          int nx = cx2 + DX[d], ny = cy2 + DY[d];
          if (nx < 0 || ny < 0 || nx >= DGW || ny >= DGH) continue;
          if (dgMap[ny * DGW + nx] == 1 || seen[ny * DGW + nx]) continue;
          seen[ny * DGW + nx] = true; dist[ny * DGW + nx] = dist[cy2 * DGW + cx2] + 1;
          qx[qt] = nx; qy[qt] = ny; qt++;
        }
      }
      int far = 0, fx = px2, fy = py2; // l'escalier au point le plus loin du joueur
      for (int y = 1; y < DGH - 1; y++) for (int x = 1; x < DGW - 1; x++)
        if (seen[y * DGW + x] && dist[y * DGW + x] > far) { far = dist[y * DGW + x]; fx = x; fy = y; }
      dgMap[fy * DGW + fx] = 2;
      int placed = 0, guard = 0; // potions : uniquement sur des cases atteignables
      while (placed < 3 && guard++ < 300) {
        int x = 1 + rnd(DGW - 2), y = 1 + rnd(DGH - 2);
        if (seen[y * DGW + x] && dgMap[y * DGW + x] == 0) { dgMap[y * DGW + x] = 3; placed++; }
      }
      placed = 0; guard = 0; // tas d'or
      while (placed < 3 && guard++ < 300) {
        int x = 1 + rnd(DGW - 2), y = 1 + rnd(DGH - 2);
        if (seen[y * DGW + x] && dgMap[y * DGW + x] == 0) { dgMap[y * DGW + x] = 4; placed++; }
      }
      if (rnd(100) < 60) { // une epee d'amelioration, 60% du temps
        int x = 1 + rnd(DGW - 2), y = 1 + rnd(DGH - 2);
        if (seen[y * DGW + x] && dgMap[y * DGW + x] == 0) dgMap[y * DGW + x] = 5;
      }
      int nEn = 3 + floor; if (nEn > DG_NE) nEn = DG_NE; // les monstres
      if (floor % 5 == 0) nEn = DG_NE; // etage du boss : pack complet, le boss clot la generation
      placed = 0; guard = 0;
      while (placed < nEn && guard++ < 600) {
        int x = 1 + rnd(DGW - 2), y = 1 + rnd(DGH - 2);
        if (!seen[y * DGW + x] || dgMap[y * DGW + x] != 0) continue;
        if (x == px2 && y == py2) continue;
        int dMan = (x > px2 ? x - px2 : px2 - x) + (y > py2 ? y - py2 : py2 - y);
        if (dMan <= 5) continue; // jamais colles au joueur au depart
        int t = 0;
        if (floor >= 2 && rnd(100) < 40) t = 1;
        if (floor >= 3 && rnd(100) < 25) t = 2;
        if (floor % 5 == 0 && placed == nEn - 1) t = 3;
        ex2[placed] = x; ey2[placed] = y; etp2[placed] = (uint8_t)t;
        ehp2[placed] = DG_EHP[t] + (floor - 1) * (t == 3 ? 8 : 1); placed++;
      }
      ne = placed;
    }
    // ---- entrees : un appui = une action (tour par tour)
    pollKeys();
    bool acted = false;
    int nx = px2, ny = py2;
    if (kLeft && !pL2) nx = px2 - 1;
    else if (kRight && !pR2) nx = px2 + 1;
    else if (kUp && !pU2) ny = py2 - 1;
    else if (kDown && !pD2) ny = py2 + 1;
    if (kHome) { pL2 = kLeft; pR2 = kRight; pU2 = kUp; pD2 = kDown; return; }
    if (keyBack && !pBk2) { waitRelease(); return; } // abandon
    if (keyOK && !pOK2) { // boire une potion (ca ne reveille pas les monstres)
      waitRelease();
      if (pots > 0 && hp < maxHp) { pots--; hp += 8; if (hp > maxHp) hp = maxHp; snprintf(log1, 40, "TU BOIS : +8 PV"); }
      else snprintf(log1, 40, pots > 0 ? "PV AU MAXIMUM" : "PLUS DE POTIONS !");
      acted = false;
    }
    if (nx != px2 || ny != py2) { // ---- deplacement ou attaque au corps a corps
      int tgt = -1;
      for (int i = 0; i < ne; i++) if (ex2[i] == nx && ey2[i] == ny) { tgt = i; break; }
      if (tgt >= 0) { // frappe le monstre
        ehp2[tgt] -= atk;
        snprintf(log1, 40, "TU FRAPPES LE %s : -%d", DG_EN[etp2[tgt]], atk);
        if (ehp2[tgt] <= 0) {
          kills++;
          snprintf(log1, 40, "%s MORT !", DG_EN[etp2[tgt]]);
          ne--; ex2[tgt] = ex2[ne]; ey2[tgt] = ey2[ne]; ehp2[tgt] = ehp2[ne]; etp2[tgt] = etp2[ne];
        }
        acted = true;
      } else if (dgMap[ny * DGW + nx] != 1) { // case praticable : on avance
        px2 = nx; py2 = ny; acted = true;
        if (dgMap[py2 * DGW + px2] == 2) { // ---- ESCALIER : etage suivant
          floor++;
          hp += 4; if (hp > maxHp) hp = maxHp;
          gold += 10;
          genFloor = true;
          snprintf(log1, 40, "DESCENTE A L'ETAGE %d", floor);
          pL2 = kLeft; pR2 = kRight; pU2 = kUp; pD2 = kDown; pOK2 = keyOK; pBk2 = keyBack;
          continue; // regenere avant de rendre (saute le tour des monstres)
        } else if (dgMap[py2 * DGW + px2] == 3) { dgMap[py2 * DGW + px2] = 0; pots++; snprintf(log1, 40, "POTION RAMASSEE"); }
        else if (dgMap[py2 * DGW + px2] == 4) { dgMap[py2 * DGW + px2] = 0; gold += 25; snprintf(log1, 40, "+25 OR !"); }
        else if (dgMap[py2 * DGW + px2] == 5) { dgMap[py2 * DGW + px2] = 0; atk++; snprintf(log1, 40, "EPEE : ATK %d", atk); }
      }
    }
    pL2 = kLeft; pR2 = kRight; pU2 = kUp; pD2 = kDown; pOK2 = keyOK; pBk2 = keyBack;
    if (acted) { // ---- tour des monstres apres ton action
      for (int i = 0; i < ne; i++) {
        int dx = px2 - ex2[i], dy = py2 - ey2[i];
        int adx = (dx > 0) ? dx : -dx, ady = (dy > 0) ? dy : -dy;
        if (adx + ady == 1) { // adjacent : il frappe
          hp -= DG_EAT[etp2[i]];
          snprintf(log1, 40, "LE %s TE FRAPPE : -%d", DG_EN[etp2[i]], DG_EAT[etp2[i]]);
          if (hp <= 0) { hp = 0; break; }
        } else { // sinon il s'approche : axe le plus long d'abord, l'autre en secours
          int sx = (dx > 0) ? 1 : -1, sy = (dy > 0) ? 1 : -1;
          int tx = ex2[i], ty = ey2[i];
          if (dx != 0 && adx >= ady) tx += sx; else if (dy != 0) ty += sy;
          bool wall = (dgMap[ty * DGW + tx] == 1);
          bool occ = (tx == px2 && ty == py2);
          if (!occ) for (int j = 0; j < ne; j++) if (j != i && ex2[j] == tx && ey2[j] == ty) { occ = true; break; }
          if (wall || occ) { // premier axe bloque : essai l'autre axe
            tx = ex2[i]; ty = ey2[i];
            if (dx != 0 && adx >= ady) { if (dy != 0) ty += sy; }
            else if (dx != 0) tx += sx;
            if (tx != ex2[i] || ty != ey2[i]) {
              wall = (dgMap[ty * DGW + tx] == 1);
              occ = (tx == px2 && ty == py2);
              if (!occ) for (int j = 0; j < ne; j++) if (j != i && ex2[j] == tx && ey2[j] == ty) { occ = true; break; }
              if (wall || occ) { tx = ex2[i]; ty = ey2[i]; } // bloque des deux cotes : il attend
            }
          }
          if (tx != ex2[i] || ty != ey2[i]) { ex2[i] = tx; ey2[i] = ty; }
        }
      }
    }
    // ---- mort du joueur
    if (hp <= 0) {
      bool recF = (floor > rgBestF);
      if (recF) rgBestF = floor;
      rgKillsT += kills; rgFloorsT += floor;
      saveAcc();
      waitRelease();
      bool pOK3 = false, pBk3 = false;
      while (true) {
        pollKeys();
        if (keyOK && !pOK3) { waitRelease(); newRun = true; break; } // rejouer
        if (keyBack && !pBk3) { waitRelease(); return; }
        pOK3 = keyOK; pBk3 = keyBack;
        bgPanel();
        drawTextC(16, "TU ES MORT...", C_RED);
        char d1[40]; snprintf(d1, 40, "ETAGE %d  OR %d  KILLS %d", floor, gold, kills); drawTextC(48, d1, C_WHT);
        if (recF) drawTextC(66, "NOUVEAU RECORD !", C_YEL);
        char d2[40]; snprintf(d2, 40, "RECORD : ETAGE %d", rgBestF); drawTextC(88, d2, C_GRE);
        char d3[40]; snprintf(d3, 40, "TOTAL : %dE %dK", rgFloorsT, rgKillsT); drawTextC(110, d3, C_GRE);
        rRect(SW / 2 - 120, 176, 110, 22, 8, C_YEL); drawText(40 + (110 - textW("REJOUER")) / 2, 182, "REJOUER", C_BLK);
        rRect(SW / 2 + 10, 176, 110, 22, 8, KDColor::RGB24(0x3A3A4A)); drawText(170 + (110 - textW("MENU")) / 2, 182, "MENU", C_WHT);
        blit(); Ion::Timing::msleep(30);
      }
      continue; // retour au jeu avec newRun
    }
    // ================= rendu =================
    fRect(0, 0, SW, SH, KDColor::RGB24(0x14101E));
    for (int y = 0; y < DGH; y++) for (int x = 0; x < DGW; x++) {
      int t = dgMap[y * DGW + x];
      int pxl = x * DGT, pyl = DGY + y * DGT;
      if (t == 1) { fRect(pxl, pyl, DGT, DGT, KDColor::RGB24(0x6A5A4A)); fRect(pxl, pyl, DGT, 3, KDColor::RGB24(0x8A7A6A)); }
      else fRect(pxl, pyl, DGT, DGT, KDColor::RGB24(0x241A30));
      if (t == 2) { fRect(pxl + 4, pyl + 2, 8, 12, KDColor::RGB24(0x7A5432)); fRect(pxl + 3, pyl + 4, 10, 2, C_YEL); fRect(pxl + 3, pyl + 9, 10, 2, C_YEL); } // escalier
      else if (t == 3) { fRect(pxl + 6, pyl + 3, 4, 6, C_RED); fRect(pxl + 5, pyl + 9, 6, 4, KDColor::RGB24(0xC51111)); } // potion
      else if (t == 4) { fCirc(pxl + 8, pyl + 8, 4, C_GOLD); } // or
      else if (t == 5) { fRect(pxl + 7, pyl + 2, 2, 9, C_WHT); fRect(pxl + 5, pyl + 9, 6, 2, C_WHT); fRect(pxl + 6, pyl + 11, 4, 3, KDColor::RGB24(0x8A6D00)); } // epee
    }
    for (int i = 0; i < ne; i++) { // monstres
      int pxl = ex2[i] * DGT, pyl = DGY + ey2[i] * DGT;
      int r = (etp2[i] == 3) ? 7 : 5;
      fCirc(pxl + 8, pyl + 8, r, DG_ECOL[etp2[i]]);
      fRect(pxl + 5, pyl + 6, 2, 2, C_RED); fRect(pxl + 9, pyl + 6, 2, 2, C_RED); // yeux
      int mx = DG_EHP[etp2[i]] + (floor - 1) * (etp2[i] == 3 ? 8 : 1); if (mx < 1) mx = 1;
      int bw = 10 * ehp2[i] / mx; if (bw < 0) bw = 0; if (bw > 10) bw = 10;
      fRect(pxl + 3, pyl + 1, 10, 2, KDColor::RGB24(0x3A0A0A));
      fRect(pxl + 3, pyl + 1, bw, 2, C_RED);
    }
    int pxl = px2 * DGT, pyl = DGY + py2 * DGT; // joueur
    fCirc(pxl + 8, pyl + 8, 5, C_YEL);
    fRect(pxl + 5, pyl + 6, 2, 2, C_BLK); fRect(pxl + 9, pyl + 6, 2, 2, C_BLK);
    fRect(0, 0, SW, 14, KDColor::RGB24(0x10161E)); fRect(0, 14, SW, 2, C_BTND); // HUD haut
    char h1[48]; snprintf(h1, 48, "PV%d ATK%d P%d OR%d E%d", hp, atk, pots, gold, floor);
    drawText(6, 3, h1, C_WHT);
    fRect(0, 206, SW, SH - 206, KDColor::RGB24(0x10161E)); fRect(0, 206, SW, 2, C_BTND); // barre du bas
    drawText(6, 212, log1, C_WHT);
    blit();
    Ion::Timing::msleep(40);
  }
}


// ------------------------- LANCEUR : 6 jeux dans l'app ----------------

static int launcherScreen() {
  int sel = 0, drawn = -1;
  bool pL = false, pR = false, pU = false, pD = false, pOK = false, pB = false;
  waitRelease();
  while (true) {
    pollKeys();
    if (kLeft && !pL && sel > 0) sel--;
    if (kRight && !pR && sel < 5) sel++;
    if (kUp && !pU && sel >= 3) sel -= 3;
    if (kDown && !pD && sel + 3 < 6) sel += 3;
    if (keyOK && !pOK) { waitRelease(); return sel; }
    if (keyBack && !pB) { waitRelease(); return -1; }
    if (kHome) return -1;
    if (drawn != sel) {
      drawn = sel;
      for (int y = 0; y < SH; y++) { int x = 0; while (x < SW) { int band = (x + y * 2) / 40; int xe = (band + 1) * 40 - y * 2 - 1; if (xe >= SW) xe = SW - 1; fillSpan(y, x, xe, (band % 2) ? KDColor::RGB24(0x151A22) : KDColor::RGB24(0x1B222D)); x = xe + 1; } }
      drawTextC(8, "MICRO GAMES : 6 JEUX", C_YEL);
      fRect(SW / 2 - 60, 20, 120, 2, C_BTND);
      static const KDColor HIL[6] = { KDColor::RGB24(0xF5C518), KDColor::RGB24(0xC51111), KDColor::RGB24(0xB44BFF),
                                      KDColor::RGB24(0x35D4E8), KDColor::RGB24(0x3DDC55), KDColor::RGB24(0xFF8327) };
      for (int i = 0; i < 6; i++) { // 2 rangees de 3 cartes de 98 px
        int cx0 = 3 + (i % 3) * 108, cy0 = (i < 3) ? 30 : 114;
        rRect(cx0, cy0, 98, 78, 10, sel == i ? HIL[i] : KDColor::RGB24(0x2E3A4A));
        fRect(cx0 + 4, cy0 + 4, 90, 70, sel == i ? KDColor::RGB24(0x22282F) : KDColor::RGB24(0x1B222D));
        int icx = cx0 + 49, icy = cy0 + 24;
        if (i == 0) { oCirc(icx, icy, 12, KDColor::RGB24(0xF5C518), KDColor::RGB24(0x8A6D00)); fRect(icx - 8, icy + 10, 16, 3, KDColor::RGB24(0x8A6D00)); }
        else if (i == 1) drawBean(icx, icy, KDColor::RGB24(0xC51111), false);
        else if (i == 2) { oCirc(icx, icy, 12, KDColor::RGB24(0xB44BFF), KDColor::RGB24(0x5A1F78)); fRect(icx - 5, icy - 3, 2, 2, C_RED); fRect(icx + 3, icy - 3, 2, 2, C_RED); fRect(icx - 3, icy + 6, 6, 2, C_WHT); }
        else if (i == 3) { fRect(icx - 6, icy - 6, 12, 16, KDColor::RGB24(0x9A9AA8)); fRect(icx - 8, icy - 10, 16, 5, KDColor::RGB24(0x35D4E8)); fRect(icx - 3, icy + 12, 2, 2, C_YEL); fRect(icx + 1, icy + 12, 2, 2, C_YEL); }
        else if (i == 4) { fRect(icx - 12, icy - 12, 24, 24, KDColor::RGB24(0x3D7A20)); fRect(icx - 2, icy - 12, 3, 24, KDColor::RGB24(0x2E5A16)); fRect(icx - 12, icy - 2, 24, 3, KDColor::RGB24(0x2E5A16)); fCirc(icx - 6, icy - 6, 3, KDColor::RGB24(0x151515)); fCirc(icx + 6, icy + 6, 3, C_WHT); }
        else { fRect(icx - 12, icy - 12, 24, 24, KDColor::RGB24(0x6A5A4A)); fRect(icx - 4, icy - 2, 8, 14, KDColor::RGB24(0x7A5432)); fRect(icx - 12, icy - 12, 24, 3, KDColor::RGB24(0x8A7A6A)); fCirc(icx + 8, icy - 6, 2, C_GOLD); }
      }
      drawText(6 + 49 - textW("BRAWL") / 2, 30 + 62, "BRAWL", C_WHT); drawText(6 + 49 - textW("MICRO") / 2, 30 + 74, "MICRO", C_GRE);
      drawText(114 + 49 - textW("AMONG") / 2, 30 + 62, "AMONG", C_WHT); drawText(114 + 49 - textW("US") / 2, 30 + 74, "US", C_GRE);
      drawText(222 + 49 - textW("VAMPIRE") / 2, 30 + 62, "VAMPIRE", C_WHT); drawText(222 + 49 - textW("MINI") / 2, 30 + 74, "MINI", C_GRE);
      drawText(6 + 49 - textW("TOWER") / 2, 114 + 62, "TOWER", C_WHT); drawText(6 + 49 - textW("DEF") / 2, 114 + 74, "DEF", C_GRE);
      drawText(114 + 49 - textW("OTHELLO") / 2, 114 + 62, "OTHELLO", C_WHT); drawText(114 + 49 - textW("VS IA") / 2, 114 + 74, "VS IA", C_GRE);
      drawText(222 + 49 - textW("DONJON") / 2, 114 + 62, "DONJON", C_WHT); drawText(222 + 49 - textW("ROGUE") / 2, 114 + 74, "ROGUE", C_GRE);
      static const char *HINTS[6] = { "COMBAT 3V3 ARCADE", "IMPOSTEUR OU EQUIPAGE ?", "SURVIS AUX VAGUES !", "DEFENDS TA BASE !", "REVERSI CONTRE L'IA", "DESCENDS LE DONJON !" };
      drawTextC(212, HINTS[sel], C_GRE);
    }
    pL = kLeft; pR = kRight; pU = kUp; pD = kDown; pOK = keyOK; pB = keyBack;
    Ion::Timing::msleep(40);
  }
}
// ------------------------- Point d'entree ------------------------------
void ion_main(int argc, char *argv[]) {
  (void)argc; (void)argv;
  initAcc(); // charge le compte ET les quetes sauvegardees (initQuests si vierge)
  if (!acc.name[0]) { namePicker(); waitRelease(); } // pseudo au premier lancement (V5.11)
  int myBrawler = 0;
  const char *mainItems[10] = {"JOUER", "ENTRAINEMENT", "BRAWLERS", "BOUTIQUE", "CARRIERE", "EMOTES", "QUETES DU JOUR", "ROUTE DES TROPHEES", "PARAMETRES", "AIDE"};
  while (true) { // V5.18 : LANCEUR — le projet contient SIX jeux maintenant ! (V5.32 : Vampire, Tower Defense, Othello, Donjon)
    int game = launcherScreen();
    waitRelease();
    if (game < 0) return; // BACK au lanceur = quitter l'app
    if (game == 1) { amongUsGame(); waitRelease(); continue; } // AMONG US MICRO
    if (game == 2) { vsGame(); waitRelease(); continue; } // VAMPIRE SURVIVORS MINI (V5.32)
    if (game == 3) { tdGame(); waitRelease(); continue; } // TOWER DEFENSE MINI (V5.32)
    if (game == 4) { otGame(); waitRelease(); continue; } // OTHELLO MINI (V5.32)
    if (game == 5) { dgGame(); waitRelease(); continue; } // DONJON ROGUELIKE MINI (V5.32)
  while (true) { // menu BRAWL MICROSTARS
    gTitleAnim = true; // brawlers qui defilent derriere le menu principal (V5.11)
    gPortrait = myBrawler; // V5.14 : portrait du brawler courant au menu principal
    int choice = menuPick("BRAWL MICROSTARS", mainItems, 10, 0);
    gTitleAnim = false;
    if (choice < 0) break; // V5.18 : BACK = retour au LANCEUR (plus de sortie directe)
    waitRelease();
    if (choice == 0) {
      int selMode = modeTileScreen(); // V5.16 : selection des modes en TUILES facon BS
      if (selMode < 0) { waitRelease(); continue; }
      waitRelease();
      if (selMode == MODE_DUEL) { // DUEL : on choisit SES 3 brawlers, l'IA a aussi les siens
        const char *duelTitles[3] = {"TON BRAWLER 1/3", "TON BRAWLER 2/3", "TON BRAWLER 3/3"};
        bool ok = true;
        int prev = myBrawler;
        for (int i = 0; i < 3 && ok; i++) {
          int p = brawlerPicker(prev, duelTitles[i]);
          if (p < 0) ok = false;
          else { duelQ[0][i] = p; duelAcc[0][i] = p; prev = p; }
          waitRelease();
        }
        if (!ok) continue;
        myBrawler = duelQ[0][0];
        int pool[55], pn = 0;
        for (int i = 0; i < 55; i++) if (isUnlocked(i)) pool[pn++] = i;
        if (pn == 0) { duelQ[1][0] = duelQ[1][1] = duelQ[1][2] = 0; pn = 1; }
        for (int i = 0; i < 3; i++) { duelQ[1][i] = pool[rnd(pn)]; duelAcc[1][i] = -1; }
      } else {
        // selection du brawler : grille de cartes facon Brawl Stars
        int picked = brawlerPicker(myBrawler);
        if (picked < 0) { waitRelease(); continue; }
        myBrawler = picked;
      }
      waitRelease();
      do { // rejouer instantane : la touche + sur l'ecran de resultat relance le match
        gReplay = false;
        playMatch(myBrawler, selMode);
      } while (gReplay);
      waitRelease();
    } else if (choice == 1) {
      trainingMode(myBrawler);
    } else if (choice == 2) {
      brawlerMenu(myBrawler);
    } else if (choice == 3) {
      shopScreen(myBrawler);
    } else if (choice == 4) {
      careerScreen();
    } else if (choice == 5) {
      emoteScreen();
    } else if (choice == 6) {
      questScreen();
    } else if (choice == 7) {
      trophyRoadScreen();
    } else if (choice == 8) {
      // V5.13 : le pseudo se change des PARAMETRES ; V5.14 : option 60 FPS
      static char fpsLbl[16], fps2Lbl[16];
      fpsLbl[0] = 0; strcat(fpsLbl, gFps60 ? "60 FPS: OUI" : "60 FPS: NON");
      fps2Lbl[0] = 0; strcat(fps2Lbl, gFpsShow ? "FPS ECRAN: OUI" : "FPS ECRAN: NON");
      const char *setItems[4] = {"TOUCHES DU JEU", "CHANGER PSEUDO", fpsLbl, fps2Lbl};
      waitRelease();
      int sp = menuPick("PARAMETRES", setItems, 4, 0);
      if (sp == 0) settingsScreen();
      else if (sp == 1) { namePicker(); waitRelease(); } // BACK vide = pseudo inchangé
      else if (sp == 2) { gFps60 = !gFps60; saveAcc(); waitRelease(); } // V5.14 : bascule 30/60 FPS persistee
      else if (sp == 3) { gFpsShow = !gFpsShow; saveAcc(); waitRelease(); } // V5.16 : compteur FPS persiste
    } else {
      bgPanel();
      drawTextC(8, "AIDE", C_YEL);
      fRect(SW / 2 - 60, 22, 120, 3, C_BTND);
      const char *lines[6] = {
        "FLECHES: BOUGER", "OK/VAR: ATTAQUE",
        "RETOUR: ULTI", "HOME: QUITTER",
        "GAGNE DES TROPHEES POUR", "DEBLOQUER DES BRAWLERS"
      };
      for (int i = 0; i < 6; i++) drawTextC(38 + i * 17, lines[i], C_WHT);
      rRect(SW / 2 - 70, 184, 140, 24, 8, C_YEL);
      drawTextC(190, "OK RETOUR", C_BLK);
      blit();
      prevOK = true;
      while (true) { pollKeys(); if (keyOK && !prevOK) break; prevOK = keyOK; Ion::Timing::msleep(60); } // V5.17 FIX : attend un VRAI appui OK (avant : l'ecran clignotait et sortait aussitot)
      prevOK = false;
    }
  } // fin du menu BRAWL MICROSTARS
  } // fin du lanceur (V5.18)
}

// --- Entry point reel : sans main, --gc-sections supprime tout le jeu ---
// np_app_begin/end (depuis epsilon_app.h) : maintient Home bloque pendant
// le jeu et rend la memoire proprement a Epsilon en sortant.
extern "C" int main(int argc, char *argv[]) {
  np_app_begin();
  initTrig(); // tables cos/sin precalculees (V5.10)
  ion_main(argc, argv);
  return np_app_end();
}
