// ===================================================================
//  LE ROYAUME OUBLIE v1.0 — Aventure top-down (facon Zelda) pour NumWorks
//  App externe Epsilon (.nwa via nwlink / EADK), un seul fichier.
//
//  L'ETOILE du royaume a ete volee par le GOLEM DES Cavernes.
//  Explore le monde, trouve la cle, ouvre la porte scellee,
//  vaincs le Golem et rends l'etoile a l'Ancien du village.
//
//  CONTROLES :
//    Fleches : bouger | OK / var : épée + parler + ouvrir coffres
//    HOME : retour au titre (abandon de la partie en cours)
//
//  TECHNIQUE : framebuffer 8 bits a palette (71 Ko, sous la limite RAM
//  d'installation ~148 Ko), tables de donnees en flash (const),
//  18 salles reliees par des portes aux quatre directions.
// ===================================================================

#include <ion.h>
extern "C" {
#include <eadk.h>
}
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "epsilon_files.h"
#include <cstdio>

#define SW 320
#define SH 240

static uint16_t seed = 12345;
static int rnd(int n) { seed = (uint16_t)(seed * 25173 + 13849); return (seed >> 4) % (n > 0 ? n : 1); }

// ---------------- Moteur : framebuffer 8 bits a palette ----------------
static uint8_t VRAM[SW * SH]; // 71 Ko
static KDColor PAL[256]; static int palN = 0;
static KDColor palLastC; static uint8_t palLastI = 0; static bool palHasLast = false;
static uint8_t palIdx(KDColor c) {
  if (palHasLast && c.red() == palLastC.red() && c.green() == palLastC.green() && c.blue() == palLastC.blue()) return palLastI;
  for (int i = 0; i < palN; i++)
    if (PAL[i].red() == c.red() && PAL[i].green() == c.green() && PAL[i].blue() == c.blue()) {
      palLastC = c; palLastI = (uint8_t)i; palHasLast = true; return (uint8_t)i;
    }
  if (palN < 256) { PAL[palN] = c; palLastC = c; palLastI = (uint8_t)palN; palHasLast = true; return (uint8_t)palN++; }
  int bi = 0; long bd = 1L << 30;
  for (int i = 0; i < 256; i++) {
    long dr = PAL[i].red() - c.red(), dg = PAL[i].green() - c.green(), db = PAL[i].blue() - c.blue();
    long d = 4 * dr * dr + 9 * dg * dg + db * db;
    if (d < bd) { bd = d; bi = i; }
  }
  palLastC = c; palLastI = (uint8_t)bi; palHasLast = true; return (uint8_t)bi;
}
static void px(int x, int y, KDColor c) {
  if (x >= 0 && x < SW && y >= 0 && y < SH) VRAM[(long)y * SW + x] = palIdx(c);
}
static void fRect(int x, int y, int w, int h, KDColor c) {
  if (w < 0) { x += w; w = -w; } if (h < 0) { y += h; h = -h; }
  if (x < 0) { w += x; x = 0; } if (y < 0) { h += y; y = 0; }
  if (x + w > SW) w = SW - x;
  if (y + h > SH) h = SH - y;
  if (w <= 0 || h <= 0) return;
  uint8_t pi = palIdx(c);
  for (int j = 0; j < h; j++) {
    uint8_t *d = VRAM + (long)(y + j) * SW + x;
    for (int i = 0; i < w; i++) d[i] = pi;
  }
}
static void fCirc(int cx, int cy, int r, KDColor c) {
  for (int j = -r; j <= r; j++)
    for (int i = -r; i <= r; i++)
      if (i * i + j * j <= r * r) px(cx + i, cy + j, c);
}
static void blit() {
  static KDColor up[SW];
  for (int y = 0; y < SH; y++) {
    const uint8_t *src = VRAM + (long)y * SW;
    for (int x = 0; x < SW; x++) up[x] = (src[x] < palN) ? PAL[src[x]] : KDColor::RGB24(0);
    Ion::Display::pushRect(KDRect(0, y, SW, 1), up);
  }
}

// ---------------- Police 5x5 (petite, nette) + grande ----------------
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
static void glyphRuns(int x, int y, uint8_t row, int nc, KDColor c, int s) {
  if (!row) return;
  int col = 0;
  while (col < nc) {
    if (row & (0x10 >> col)) {
      int st = col;
      while (col < nc && (row & (0x10 >> col))) col++;
      fRect(x + st * s, y, (col - st) * s, s, c);
    } else col++;
  }
}
static void drawTextSmall(int x, int y, const char *s, KDColor c) {
  while (*s) {
    char ch = *s++;
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch == ' ') { x += 4; continue; }
    if (ch == '-') { fRect(x, y + 2, 4, 1, c); x += 6; continue; }
    if (ch == '!') { fRect(x, y, 1, 5, c); x += 4; continue; }
    if (ch == ':') { fRect(x, y, 1, 1, c); fRect(x, y + 3, 1, 1, c); x += 4; continue; }
    if (ch == '.') { fRect(x, y + 4, 1, 1, c); x += 4; continue; }
    if (ch == '\'') { fRect(x, y, 1, 2, c); x += 4; continue; }
    if (ch == '?') { fRect(x, y, 1, 2, c); fRect(x + 2, y, 1, 1, c); fRect(x + 1, y + 2, 1, 2, c); x += 5; continue; }
    if (ch == ',') { fRect(x, y + 3, 1, 1, c); fRect(x, y + 4, 1, 1, c); x += 4; continue; }
    if (ch >= 'A' && ch <= 'Z') {
      for (int r = 0; r < 5; r++) glyphRuns(x, y + r, FONTA[ch - 'A'][r], 5, c, 1);
      x += 6;
    } else if (ch >= '0' && ch <= '9') {
      for (int r = 0; r < 5; r++) glyphRuns(x, y + r, FONT5[ch - '0'][r], 5, c, 1);
      x += 6;
    } else x += 6;
  }
}
static int textWS(const char *s) { int w = 0; for (const char *p = s; *p; p++) w += (*p == ' ') ? 4 : 6; return w; }
static void drawTextC(int y, const char *s, KDColor c) { drawTextSmall((SW - textWS(s)) / 2, y, s, c); }
static void drawTextBig(int x, int y, const char *s, KDColor c) { // police 2x pour le titre
  while (*s) {
    char ch = *s++;
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch == ' ') { x += 12; continue; }
    if (ch >= 'A' && ch <= 'Z') {
      for (int r = 0; r < 5; r++) glyphRuns(x, y + r * 2, FONTA[ch - 'A'][r], 5, c, 2);
      x += 12;
    } else if (ch >= '0' && ch <= '9') {
      for (int r = 0; r < 5; r++) glyphRuns(x, y + r * 2, FONT5[ch - '0'][r], 5, c, 2);
      x += 12;
    } else x += 12;
  }
}

// ---------------- Touches ----------------
static bool keyOK, kUp, kDown, kLeft, kRight, kHome;
static void pollKeys() {
  Ion::Keyboard::State st = Ion::Keyboard::getState();
  using K = Ion::Keyboard::Key;
  kUp = st.keyDown(K::Up); kDown = st.keyDown(K::Down);
  kLeft = st.keyDown(K::Left); kRight = st.keyDown(K::Right);
  keyOK = st.keyDown(K::OK) || st.keyDown(K::Var) || st.keyDown(K::EXE);
  kHome = st.keyDown(K::Home);
}
static void waitRelease() {
  while (true) {
    pollKeys();
    if (!keyOK) return;
    Ion::Timing::msleep(30);
  }
}

// ---------------- Couleurs ----------------
#define C_WHT    KDColor::RGB24(0xFFFFFF)
#define C_BLK    KDColor::RGB24(0x101010)
#define C_YEL    KDColor::RGB24(0xFFC928)
#define C_ORG    KDColor::RGB24(0xFF8327)
#define C_RED    KDColor::RGB24(0xE04040)
#define C_DRED   KDColor::RGB24(0x7A1E1E)
#define C_GREEN  KDColor::RGB24(0x3DDC55)
#define C_DGRN   KDColor::RGB24(0x1E7A2E)
#define C_GRASS  KDColor::RGB24(0x4A9E3C)
#define C_GRAS2  KDColor::RGB24(0x3E8430)
#define C_PATH   KDColor::RGB24(0xC9A96A)
#define C_TREE   KDColor::RGB24(0x1E5A28)
#define C_TREE2  KDColor::RGB24(0x2E7A38)
#define C_WTR    KDColor::RGB24(0x2E5FA8)
#define C_WTR2   KDColor::RGB24(0x4A7FD4)
#define C_ROCK   KDColor::RGB24(0x8A8A8A)
#define C_ROCK2  KDColor::RGB24(0x6A6A6A)
#define C_WALL   KDColor::RGB24(0x3A3A44)
#define C_WALL2  KDColor::RGB24(0x4A4A56)
#define C_FLR    KDColor::RGB24(0x5A5A64)
#define C_FLR2   KDColor::RGB24(0x666670)
#define C_GOLD   KDColor::RGB24(0xFFD700)
#define C_CHEST  KDColor::RGB24(0x8A5A2A)
#define C_CHEST2 KDColor::RGB24(0xB48048)
#define C_PUR    KDColor::RGB24(0x9A5AD4)
#define C_SKIN   KDColor::RGB24(0xF0C8A0)
#define C_CYN    KDColor::RGB24(0x3AC8D4)
#define C_MAG    KDColor::RGB24(0xD45A9E)
#define C_GRE    KDColor::RGB24(0x8A8A8A)
#define C_TUNIC  KDColor::RGB24(0x2E9E4E)

// ===================================================================
//  LE MONDE : 18 salles de 20x12 tuiles de 16 px.
//  Portes : E/O sur les rangees de tuiles 5-6, N/S sur colonnes 9-10.
//  Legend : . herbe, f fleurs, p chemin, t arbre, r rocher, w eau,
//           # mur donjon, , sol donjon, D porte verrouillee,
//           C coffre, c grotte (vers le donjon).
// ===================================================================
static const int TS = 16, MW = 20, MH = 12, TOPOFF = 40, RW = 6, RH = 3;
static const char *RM[18][12] = {
  { // 0 : foret nord-ouest (portes E, S)
  "tttttttttttttttttttt",
  "t..................t",
  "t..f.....tt........t",
  "t.......ttt....f...t",
  "t..................t",
  "t...................",
  "t...................",
  "t....f..ttt........t",
  "t.......ttt........t",
  "t..f...............t",
  "t..................t",
  "ttttttttt..ttttttttt" },
  { // 1 : foret au lac (portes E, O, S)
  "tttttttttttttttttttt",
  "t..................t",
  "t.....wwwwww.......t",
  "t..f..wwwwww....f..t",
  "t.....wwwwww.......t",
  "....................",
  "....................",
  "t.....wwwwww.......t",
  "t..f...........f...t",
  "t..................t",
  "t..................t",
  "ttttttttt..ttttttttt" },
  { // 2 : foret nord-est (portes E, O, S)
  "tttttttttttttttttttt",
  "t..f....tt....f....t",
  "t.......ttt........t",
  "t.......ttt....f...t",
  "t..................t",
  "....................",
  "....................",
  "t....f..tt.........t",
  "t.......tt....f....t",
  "t..................t",
  "t..f............f..t",
  "ttttttttt..ttttttttt" },
  { // 3 : colline et grotte (portes E=grotte, O, S)
  "tttttttttttttttttttt",
  "t...rrr......rrr...t",
  "t..rrrr..f...rrrr..t",
  "t..rrr.......rrr...t",
  "t..................t",
  "...................c",
  "...................c",
  "t...f....rr........t",
  "t........rr....f...t",
  "t..................t",
  "t....f.........f...t",
  "ttttttttt..ttttttttt" },
  { // 4 : hall du donjon (portes O, E, S) + coffre CLE
  "####################",
  "#,,,,,,,,,,,,,,,,,,#",
  "#,,##,,,,,,##,,,,,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  "#,,,,,,##,,,,,,,,,,#",
  ",,,,,,,,,,,,,,,,,,,,",
  ",,,,,,,,,,,,,,,,,,,,",
  "#,,,,,,##,,,,,,,,,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  "#,,##,,,,,,,C,,,,,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  "#########,,#########" },
  { // 5 : couloir scelle (portes O, S verrouillee D)
  "####################",
  "#,,,,,,,,,,,,,,,,,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  "#,,##,,,,,,,,,,##,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  ",,,,,,,,,,,,,,,,,,,#",
  ",,,,,,,,,,,,,,,,,,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  "#,,##,,,,,,,,,,##,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  "#########DD#########" },
  { // 6 : plaines ouest (portes N, S, E)
  "ttttttttt..ttttttttt",
  "t..................t",
  "t...f.........f....t",
  "t..................t",
  "t......tt..........t",
  "t...................",
  "t...................",
  "t..................t",
  "t..f............f..t",
  "t..................t",
  "t..................t",
  "ttttttttt..ttttttttt" },
  { // 7 : LE VILLAGE (portes N, S, E, O) + coffre PIECES
  "ttttttttt..ttttttttt",
  "t....ppppppppp.....t",
  "t....p.......p.....t",
  "t..rrp.......p..C..t",
  "t..rrp.......p.....t",
  "....................",
  "....................",
  "t....p.......p.....t",
  "t....p..rr..p......t",
  "t....p..rr..p......t",
  "t....pppppppp......t",
  "ttttttttt..ttttttttt" },
  { // 8 : plaines aux fleurs (portes N, S, E, O)
  "ttttttttt..ttttttttt",
  "t..................t",
  "t.f.f........f.f...t",
  "t..................t",
  "t......f....f......t",
  "....................",
  "....................",
  "t..................t",
  "t.f............f.f.t",
  "t..................t",
  "t.....f......f.....t",
  "ttttttttt..ttttttttt" },
  { // 9 : crete (portes N, S, O)
  "ttttttttt..ttttttttt",
  "t...rr......rr.....t",
  "t..rrr......rrr....t",
  "t..................t",
  "t....f........f....t",
  "...................t",
  "...................t",
  "t..................t",
  "t..rrr......rrr....t",
  "t...rr......rr.....t",
  "t..................t",
  "ttttttttt..ttttttttt" },
  { // 10 : salle des gardes (portes N, S)
  "#########,,#########",
  "#,,,,,,,,,,,,,,,,,,#",
  "#,,,,##,,,,,,##,,,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  "#,,,,##,,,,,,##,,,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  "#########,,#########" },
  { // 11 : LE GOLEM (porte N)
  "#########,,#########",
  "#,,,,,,,,,,,,,,,,,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  "#,,,,##,,,,,,##,,,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  "####################" },
  { // 12 : rive (portes N, E)
  "ttttttttt..ttttttttt",
  "t..................t",
  "t..f...............t",
  "t..................t",
  "t....wwww....wwww..t",
  "t...wwwwww..wwwwww..",
  "t...wwwwww..wwwwww..",
  "t....wwww....wwww..t",
  "t..................t",
  "t..f...........f...t",
  "t..................t",
  "tttttttttttttttttttt" },
  { // 13 : pre (portes N, E, O)
  "ttttttttt..ttttttttt",
  "t..................t",
  "t..................t",
  "t.f.f..........f.f.t",
  "t..................t",
  "....................",
  "....................",
  "t..................t",
  "t...f....tt....f...t",
  "t..................t",
  "t..................t",
  "tttttttttttttttttttt" },
  { // 14 : lac sud (portes N, E, O)
  "ttttttttt..ttttttttt",
  "t.......wwww.......t",
  "t......wwwwww......t",
  "t..f...wwwwww...f..t",
  "t......wwwwww......t",
  "....................",
  "....................",
  "t......wwwwww......t",
  "t..f...wwwwww...f..t",
  "t.......wwww.......t",
  "t..................t",
  "tttttttttttttttttttt" },
  { // 15 : crete sud (portes N, O)
  "ttttttttt..ttttttttt",
  "t..................t",
  "t..rr..........rr..t",
  "t..................t",
  "t.....rr....rr.....t",
  "...................t",
  "...................t",
  "t..................t",
  "t..f............f..t",
  "t..................t",
  "t....rr......rr....t",
  "tttttttttttttttttttt" },
  { // 16 : tresor du donjon (portes N, E) + coffre COEUR
  "#########,,#########",
  "#,,,,,,,,,,,,,,,,,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  "#,,##,,,,,,,,,,##,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  "#,,,,,,,,,,,,,,,,,,,",
  "#,,,,,,,,,,,,,,,,,,,",
  "#,,,,,,,,,,,,,,,,,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  "#,,,,,,,,,,,,,,C,,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  "####################" },
  { // 17 : cache du donjon (porte O) + coffre PIECES
  "####################",
  "#,,,,,,,,,,,,,,,,,,#",
  "#,,,,##,,,,,,##,,,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  ",,,,,,,,,,,,,,,,,,,#",
  ",,,,,,,,,,,,,,,,,,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  "#,,,,##,,,,,,##,,,,#",
  "#,,,,,,,,C,,,,,,,,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  "####################" },
};
// contenu des coffres par salle : 0 rien, 1 pieces, 2 cle, 3 recipient de coeur
static const uint8_t CH[18] = { 0,0,0,0, 2,0,0, 1,0,0, 0,0, 0,0,0,0, 3, 1 };
// monstres par salle : { salle, type, tuileX, tuileY }
// types : 0 limace, 1 chauve-souris, 2 squelette, 3 tourelle
static const uint8_t SPW[][4] = {
  { 0, 0, 10, 4 }, { 0, 0, 6, 8 },
  { 1, 0, 12, 8 }, { 1, 1, 15, 2 },
  { 2, 0, 8, 9 }, { 2, 1, 14, 3 }, { 2, 1, 3, 9 },
  { 3, 0, 12, 9 },
  { 4, 2, 10, 7 }, { 4, 2, 12, 9 },
  { 6, 0, 10, 5 }, { 6, 0, 5, 3 },
  { 8, 0, 15, 8 }, { 8, 1, 8, 4 },
  { 9, 0, 10, 7 }, { 9, 1, 12, 4 },
  { 10, 3, 10, 3 }, { 10, 3, 10, 8 }, { 10, 1, 15, 5 },
  { 12, 0, 8, 8 }, { 12, 1, 12, 2 },
  { 13, 0, 5, 6 }, { 13, 0, 12, 9 },
  { 14, 1, 6, 5 }, { 14, 1, 13, 6 }, { 14, 0, 10, 1 },
  { 15, 0, 10, 6 }, { 15, 1, 14, 9 },
  { 16, 2, 8, 7 }, { 16, 1, 12, 3 },
  { 17, 2, 10, 7 },
  { 255, 0, 0, 0 }
};
// PNJ : { salle, tuileX, tuileY, id } — 0 Ancien, 1 Marchande, 3 Gamin, 4 Ermite
static const uint8_t NPC[][4] = {
  { 7, 12, 6, 0 }, { 7, 14, 8, 1 }, { 7, 5, 8, 2 }, { 3, 10, 9, 3 },
  { 255, 0, 0, 0 }
};

// ---------------- Dialogues ----------------
static const char *DLG_ANCIEN[3][3] = {
  { "AH, UN VOYAGEUR !", "Notre ETOILE a ete volee par le", "GOLEM des cavernes du nord." },
  { "Sans elle, le royaume se meurt.", "Sa lumiere guidait nos moissons,", "nos reves, nos chansons..." },
  { "Prends cette epee, hero.", "La grotte est au nord-est.", "REPOSE NOTRE ETOILE !" }
};
static const char *DLG_MARQ[3][3] = {
  { "BIENVENUE, voyageur !", "Pour 20 PIECES, je te rends", "tous tes coeurs. Marche ?" },
  { "Passe une bonne journee !", "Et merveille-toi un peu,", "ca fait du bien." }
};
static const char *DLG_GAMIN[3][3] = {
  { "Le gamin du village :", "Le GOLEM pleurait, avant.", "On entendait ses sanglots..." },
  { "Ils disent qu'il garde", "quelque chose de douloureux,", "au fond de la caverne." }
};
static const char *DLG_ERMITE[3][3] = {
  { "L'ermite de la colline :", "Cette grotte mene au donjon.", "Emporte une CLE, ou n'entre pas." },
  { "Le GOLEM dort pres de son", "coeur de pierre. Fais attention", "ou tu plantes ton epee..." }
};
static const char *DLG_CLE[1][3] = {
  { "TU TROUVES UNE CLE DOREE !", "Elle est chaude, comme si", "quelqu'un l'avait serree..." }
};
static const char *DLG_COEUR[1][3] = {
  { "UN RECIPIENT DE COEUR !", "Tes coeurs au maximum", "augmentent de un !" }
};
static const char *DLG_PIECES[1][3] = {
  { "UN COFFRE DE PIECES !", "L'or du vieux royaume", "tintinnabule dans ta poche." }
};
static const char *DLG_PORTE[1][3] = {
  { "LA PORTE S'OUVRE...", "La cle tourne. Un declic.", "Un courant d'air froid." }
};
static const char *DLG_PORTEB[1][3] = {
  { "PORTE VERROUILLEE.", "Il te faut une CLE doree.", "Cherche dans le donjon..." }
};
static const char *DLG_ETOILE[2][3] = {
  { "L'ETOILE DU ROYAUME !", "Elle palpite dans tes mains,", "tieche et vivante." },
  { "Le GOLEM s'effondre en", "poussiere... une derniere larme", "de pierre roule sur le sol." }
};
static const char *INTRO[4][3] = {
  { "Il y a bien longtemps,", "une ETOILE est tombee du ciel", "sur le royaume de Tallia." },
  { "Les moissons poussaient la nuit,", "les enfants lisaient sous sa lueur,", "personne n'avait peur du noir." },
  { "Jusqu'au jour ou, du fond des", "cavernes, un GOLEM de pierre", "est monte et l'a emportee." },
  { "Le royaume s'eteint, doucement.", "Quelqu'un doit aller au nord.", "Quelqu'un comme toi." }
};
static const char *ENDING[5][3] = {
  { "TU RENDS L'ETOILE A L'ANCIEN.", "Elle remonte, doucement,", "comme rappellee chez elle." },
  { "Sa lumiere coule sur le village,", "les toits, le lac, les champs...", "Le royaume respire enfin." },
  { "L'Ancien s'incline devant toi :", "\"Le royaume se souviendra", "de son hero.\"" },
  { "Et quelque part, tres bas,", "dans la poussiere du donjon,", "quelque chose repose en paix." },
  { "FIN - LE ROYAUME OUBLIE", "MERCI D'AVOIR JOUE !", "" }
};

// ---------------- Etat du jeu ----------------
static int mode; // 0 titre, 1 intro, 2 jeu, 3 defaite, 4 epilogue
static int room; // salle courante 0..17
static float plx, ply, pvx, pvy; // position + knockback
static int pdir, swing, pifr; // 0=N 1=E 2=S 3=O, epee, invincibilite
static int php, pmax, pcoin, pkey; static bool hasStar, bossDead, gameDone;
static uint8_t chestOpen[18], doorOpen[18];
static int frame, shake;
// dialogue en cours
static const char *(*dgp)[3]; // pointeur vers un tableau de pages
static int dgN, dgI; static bool dlgOn, dlgEnd; // dlgEnd : apres la derniere page, fin du jeu

// monstres vivants (le boss est EN[0], type 4)
static struct En { float x, y, vx, vy; int type, hp, t, st, dir; bool alive, flash; } EN[14];
static int enN;
static struct Bu { float x, y, vx, vy; bool on; } BU[6];
static struct Pk { float x, y; int t; bool on; } PK[8]; // 0 coeur, 1 piece, 2 etoile
static struct Pt { float x, y, vx, vy; int life; uint8_t cr, cg, cb; } PT[36];

// ---------------- Particules ----------------
static void poof(float x, float y, KDColor c, int n) {
  for (int i = 0; i < 36 && n > 0; i++) {
    if (PT[i].life <= 0) {
      PT[i].x = x; PT[i].y = y; PT[i].life = 14 + rnd(10);
      PT[i].vx = (rnd(100) - 50) * 0.06f; PT[i].vy = (rnd(100) - 50) * 0.06f;
      PT[i].cr = c.red(); PT[i].cg = c.green(); PT[i].cb = c.blue();
      n--;
    }
  }
}
static void updParticles() {
  for (int i = 0; i < 36; i++) if (PT[i].life > 0) {
    PT[i].life--; PT[i].x += PT[i].vx; PT[i].y += PT[i].vy;
    px((int)PT[i].x, (int)PT[i].y, KDColor::RGB24((PT[i].cr << 16) | (PT[i].cg << 8) | PT[i].cb));
  }
}

// ---------------- Carte : tuiles ----------------
static char tileAt(int tx, int ty) {
  if (tx < 0 || tx >= MW || ty < 0 || ty >= MH) return ' ';
  return RM[room][ty][tx];
}
static bool npcAt(int tx, int ty) {
  for (int i = 0; NPC[i][0] != 255; i++)
    if (NPC[i][0] == room && NPC[i][1] == tx && NPC[i][2] == ty) return true;
  return false;
}
static bool solid(int tx, int ty) {
  char c = tileAt(tx, ty);
  if (c == 'D' && doorOpen[room]) return false; // porte ouverte
  if (c == 't' || c == 'r' || c == 'w' || c == ' ' || c == '#' || c == 'D' || c == 'C') return true;
  if (npcAt(tx, ty)) return true;
  return false;
}
static void drawTile(int tx, int ty, int ox, int oy) {
  char c = tileAt(tx, ty);
  int x = tx * TS + ox, y = TOPOFF + ty * TS + oy;
  if (c == '.' || c == 'f' || c == 'p' || c == 'c') {
    if (c == 'p') fRect(x, y, TS, TS, C_PATH);
    else {
      fRect(x, y, TS, TS, ((tx * 7 + ty * 13) & 3) ? C_GRASS : C_GRAS2);
      px(x + 3 + (tx & 3), y + 9, C_DGRN); px(x + 11, y + 4 + (ty & 2), C_DGRN);
    }
    if (c == 'f') { px(x + 5, y + 7, C_YEL); px(x + 6, y + 7, C_YEL); px(x + 5, y + 8, C_YEL); px(x + 6, y + 8, C_ORG); px(x + 11, y + 11, C_WHT); px(x + 12, y + 11, C_WHT); }
    if (c == 'c') { fRect(x, y, TS, TS, C_ROCK); fRect(x + 3, y + 3, 10, 13, C_BLK); } // gueule de grotte
  } else if (c == 't') {
    fRect(x, y, TS, TS, C_GRASS);
    fRect(x + 6, y + 9, 4, 7, KDColor::RGB24(0x6A4A2A));
    fCirc(x + 8, y + 6, 7, C_TREE); fCirc(x + 6, y + 5, 4, C_TREE2); px(x + 4, y + 3, C_TREE2); px(x + 10, y + 7, C_TREE2);
  } else if (c == 'w') {
    fRect(x, y, TS, TS, C_WTR);
    int ph = (frame >> 4) & 1;
    for (int i = 0; i < 4; i++) px(x + 2 + i * 4 + ((ty + ph) & 1) * 2, y + 4 + i * 3, C_WTR2);
  } else if (c == 'r') {
    fRect(x, y, TS, TS, ((tx * 7 + ty * 13) & 3) ? C_GRASS : C_GRAS2);
    fCirc(x + 8, y + 9, 6, C_ROCK); fCirc(x + 6, y + 8, 3, C_ROCK2); px(x + 11, y + 12, C_ROCK2);
  } else if (c == '#') {
    fRect(x, y, TS, TS, C_WALL);
    fRect(x, y + 7, TS, 1, C_WALL2);
    fRect(x + ((ty & 1) ? 3 : 10), y, 1, 7, C_WALL2);
    fRect(x + ((ty & 1) ? 10 : 4), y + 8, 1, 8, C_WALL2);
  } else if (c == ',') {
    fRect(x, y, TS, TS, C_FLR);
    if ((tx + ty) & 1) fRect(x, y, TS, 1, C_FLR2);
    px(x + 15, y, C_WALL2); px(x, y + 15, C_WALL2);
  } else if (c == 'D') {
    fRect(x, y, TS, TS, C_WALL);
    if (doorOpen[room]) fRect(x, y, TS, TS, C_BLK); // ouverte : passage noir
    else {
      fRect(x + 2, y, 12, TS, C_GOLD); fRect(x + 4, y + 2, 8, 12, KDColor::RGB24(0xB8962A));
      fRect(x + 7, y + 6, 2, 4, C_BLK); fRect(x + 7, y + 3, 2, 2, C_BLK); // trou de serrure
    }
  } else if (c == 'C') {
    fRect(x, y, TS, TS, room >= 4 ? C_FLR : C_GRASS);
    if (chestOpen[room]) { // ouvert : vide
      fRect(x + 1, y + 6, 14, 9, C_CHEST); fRect(x + 1, y + 2, 14, 4, KDColor::RGB24(0x5A3A1A));
    } else {
      fRect(x + 1, y + 4, 14, 11, C_CHEST2); fRect(x + 1, y + 2, 14, 5, C_CHEST);
      fRect(x + 6, y + 6, 4, 4, C_GOLD); fRect(x + 7, y + 7, 2, 2, C_BLK); // serrure
    }
  } else {
    fRect(x, y, TS, TS, C_BLK); // ' ' et fond
  }
}

// ---------------- Dialogues : moteur ----------------
static void startDlg(const char *(*pages)[3], int n) { dgp = pages; dgN = n; dgI = 0; dlgOn = true; dlgEnd = false; waitRelease(); }
static bool dlgTick() { // true tant que le dialogue affiche ; false quand ferme
  pollKeys();
  if (keyOK) {
    waitRelease();
    dgI++;
    if (dgI >= dgN) { dlgOn = false; if (dlgEnd) gameDone = true; return false; }
    dgp = dgp + 1;
  }
  return true;
}
static void drawDlg() {
  fRect(6, TOPOFF + MH * TS - 52, SW - 12, 48, C_BLK);
  fRect(6, TOPOFF + MH * TS - 52, SW - 12, 1, C_WHT);
  fRect(6, TOPOFF + MH * TS - 5, SW - 12, 1, C_WHT);
  drawTextSmall(14, TOPOFF + MH * TS - 45, dgp[0][0], C_WHT);
  drawTextSmall(14, TOPOFF + MH * TS - 33, dgp[0][1], C_WHT);
  drawTextSmall(14, TOPOFF + MH * TS - 21, dgp[0][2], C_WHT);
  if ((frame >> 3) & 1) drawTextSmall(SW - 44, TOPOFF + MH * TS - 10, "OK", C_YEL);
}

// ---------------- Monstres ----------------
static void spawnEnemies() {
  enN = 0;
  for (int i = 0; i < 14; i++) { EN[i].alive = false; BU[i % 6].on = false; }
  for (int i = 0; i < 8; i++) PK[i].on = false;
  for (int i = 0; SPW[i][0] != 255; i++) {
    if (SPW[i][0] != room || enN >= 13) continue;
    EN[enN].x = SPW[i][2] * TS + 8; EN[enN].y = TOPOFF + SPW[i][3] * TS + 8;
    EN[enN].type = SPW[i][1]; EN[enN].vx = EN[enN].vy = 0;
    EN[enN].hp = (SPW[i][1] == 2) ? 2 : ((SPW[i][1] == 3) ? 2 : 1);
    EN[enN].t = rnd(60); EN[enN].st = 0; EN[enN].dir = 0;
    EN[enN].alive = true; EN[enN].flash = false;
    enN++;
  }
  if (room == 11 && !bossDead) { // LE GOLEM
    EN[0].x = 160; EN[0].y = TOPOFF + 64; EN[0].vx = EN[0].vy = 0;
    EN[0].type = 4; EN[0].hp = 16; EN[0].t = 0; EN[0].st = 0; EN[0].dir = 0;
    EN[0].alive = true; EN[0].flash = false;
    enN = 1;
  }
}
static bool walkable(float x, float y, float r) {
  int tx0 = (int)((x - r) / TS), tx1 = (int)((x + r) / TS);
  int ty0 = (int)((y - r - TOPOFF) / TS), ty1 = (int)((y + r - TOPOFF) / TS);
  for (int ty = ty0; ty <= ty1; ty++)
    for (int tx = tx0; tx <= tx1; tx++)
      if (solid(tx, ty)) return false;
  return true;
}
static void dropPickup(float x, float y, int forced) {
  for (int i = 0; i < 8; i++) if (!PK[i].on) {
    PK[i].x = x; PK[i].y = y; PK[i].t = forced >= 0 ? forced : (rnd(3) == 0 ? 0 : 1); PK[i].on = true;
    return;
  }
}
static void hurtPlayer(int dmg, float fx, float fy) {
  if (pifr > 0) return;
  php -= dmg; pifr = 50; shake = 6;
  float dx = plx - fx, dy = ply - fy;
  float d = dx * dx + dy * dy; if (d < 1) d = 1;
  float m = 2.6f / sqrtf(d);
  pvx = dx * m; pvy = dy * m;
  poof(plx, ply, C_RED, 8);
  if (php <= 0) mode = 3;
}
static void updEnemies() {
  for (int i = 0; i < 14; i++) {
    if (!EN[i].alive) continue;
    struct En *e = &EN[i];
    e->t++; e->flash = (e->t & 2) && e->st == 99; // clignote quand touche
    if (e->st == 99) { // recul apres coup d'epee
      e->st++;
      if (e->st > 103) e->st = 0;
    }
    float sp = 0;
    if (e->type == 0) { // limace : sautille vers le joueur
      if (e->t % 64 == 0) {
        float dx = plx - e->x, dy = ply - e->y;
        float d = sqrtf(dx * dx + dy * dy); if (d < 1) d = 1;
        e->vx = dx / d * 2.1f; e->vy = dy / d * 2.1f;
      }
      e->vx *= 0.94f; e->vy *= 0.94f;
    } else if (e->type == 1) { // chauve-souris : vole en sinusoide
      if (e->t % 50 == 0) {
        float dx = plx - e->x, dy = ply - e->y;
        float d = sqrtf(dx * dx + dy * dy); if (d < 1) d = 1;
        e->dir = (rnd(4) == 0) ? rnd(4) : ((dx > dy) ? ((dx > 0) ? 1 : 3) : ((dy > 0) ? 2 : 0));
      }
      sp = 1.1f;
      float mx = (e->dir == 1) ? sp : ((e->dir == 3) ? -sp : 0);
      float my = (e->dir == 2) ? sp : ((e->dir == 0) ? -sp : 0);
      float wob = ((e->t & 31) - 16) * 0.03f;
      if (walkable(e->x + mx, e->y + my + wob, 5)) { e->x += mx; e->y += my + wob; }
      else e->dir = rnd(4);
    } else if (e->type == 2) { // squelette : avance vers le joueur
      if (e->t % 120 < 80) {
        float dx = plx - e->x, dy = ply - e->y;
        float d = sqrtf(dx * dx + dy * dy); if (d < 1) d = 1;
        float mx = dx / d * 0.62f, my = dy / d * 0.62f;
        if (walkable(e->x + mx, e->y, 6)) e->x += mx;
        if (walkable(e->x, e->y + my, 6)) e->y += my;
      }
    } else if (e->type == 3) { // tourelle : tire vers le joueur
      if (e->t % 110 == 40) {
        for (int b = 0; b < 6; b++) if (!BU[b].on) {
          float dx = plx - e->x, dy = ply - e->y;
          float d = sqrtf(dx * dx + dy * dy); if (d < 1) d = 1;
          BU[b].x = e->x; BU[b].y = e->y; BU[b].vx = dx / d * 1.6f; BU[b].vy = dy / d * 1.6f;
          BU[b].on = true;
          break;
        }
      }
    } else if (e->type == 4) { // LE GOLEM : saute puis frappe le sol
      if (e->st == 0 && e->t % 90 == 0) {
        float dx = plx - e->x, dy = ply - e->y;
        float d = sqrtf(dx * dx + dy * dy); if (d < 1) d = 1;
        e->vx = dx / d * 3.2f; e->vy = dy / d * 3.2f; e->st = 10;
      }
      if (e->st >= 10) {
        e->st++;
        e->vx *= 0.92f; e->vy *= 0.92f;
        if (e->st == 34) { // FRAPPE : onde de choc + projectiles
          shake = 10; poof(e->x, e->y + 10, C_ROCK2, 14);
          float dx = plx - e->x, dy = ply - e->y;
          float d = sqrtf(dx * dx + dy * dy); if (d < 1) d = 1;
          for (int b = 0; b < 6; b++) if (!BU[b].on) {
            float ang = atan2f(dy, dx);
            int k;
            for (k = 0; k < 2; k++) { // deux projectiles autour de l'axe
              float a2 = ang + (k ? 0.5f : -0.5f);
              int bb;
              for (bb = 0; bb < 6; bb++) if (BU[bb].on == false) break;
              if (bb < 6) { BU[bb].x = e->x; BU[bb].y = e->y; BU[bb].vx = cosf(a2) * 1.5f; BU[bb].vy = sinf(a2) * 1.5f; BU[bb].on = true; }
            }
            break;
          }
        }
        if (e->st > 40) e->st = 0;
      }
      if (e->hp <= 8 && e->t % 240 == 120 && enN < 12) { // a mi-vie : appelle des limaces
        struct En *s = &EN[enN++];
        s->x = e->x + (rnd(40) - 20); s->y = e->y + (rnd(40) - 20);
        s->type = 0; s->vx = s->vy = 0; s->hp = 1; s->t = 0; s->st = 0; s->dir = 0; s->alive = true; s->flash = false;
      }
    }
    if (e->type != 1) { // les autres respectent les murs
      float nx = e->x + e->vx, ny = e->y + e->vy;
      float r = (e->type == 4) ? 10 : 6;
      if (e->st == 99) { e->x = nx; e->y = ny; } // recul : traverse un peu
      else {
        if (walkable(nx, e->y, r)) e->x = nx; else e->vx = 0;
        if (walkable(e->x, ny, r)) e->y = ny; else e->vy = 0;
      }
      if (e->x < 6) e->x = 6; if (e->x > SW - 6) e->x = SW - 6;
      if (e->y < TOPOFF + 6) e->y = TOPOFF + 6; if (e->y > TOPOFF + MH * TS - 6) e->y = TOPOFF + MH * TS - 6;
    }
    // contact : le monstre blesse
    float er = (e->type == 4) ? 13 : 7;
    if (e->st != 99 && plx > e->x - er - 6 && plx < e->x + er + 6 && ply > e->y - er - 6 && ply < e->y + er + 6)
      hurtPlayer(e->type == 4 ? 2 : 1, e->x, e->y);
  }
  // projectiles
  for (int b = 0; b < 6; b++) if (BU[b].on) {
    BU[b].x += BU[b].vx; BU[b].y += BU[b].vy;
    int tx = (int)(BU[b].x / TS), ty = (int)((BU[b].y - TOPOFF) / TS);
    if (BU[b].x < 2 || BU[b].x > SW - 2 || BU[b].y < TOPOFF + 2 || BU[b].y > TOPOFF + MH * TS - 2 || solid(tx, ty)) { BU[b].on = false; continue; }
    if (BU[b].x > plx - 6 && BU[b].x < plx + 6 && BU[b].y > ply - 6 && BU[b].y < ply + 6) {
      BU[b].on = false; hurtPlayer(1, BU[b].x - BU[b].vx, BU[b].y - BU[b].vy);
    }
  }
}
static void hitEnemies() { // coup d'epee : zone devant le joueur
  if (swing <= 0) return;
  float cx = plx, cy = ply;
  if (pdir == 0) cy -= 16; else if (pdir == 1) cx += 16; else if (pdir == 2) cy += 16; else cx -= 16;
  for (int i = 0; i < 14; i++) {
    if (!EN[i].alive || EN[i].st == 99) continue;
    float er = (EN[i].type == 4) ? 14 : 8;
    if (cx > EN[i].x - er - 8 && cx < EN[i].x + er + 8 && cy > EN[i].y - er - 8 && cy < EN[i].y + er + 8) {
      EN[i].hp--;
      float dx = EN[i].x - plx, dy = EN[i].y - ply;
      float d = sqrtf(dx * dx + dy * dy); if (d < 1) d = 1;
      EN[i].vx = dx / d * 3.4f; EN[i].vy = dy / d * 3.4f;
      EN[i].st = 99; EN[i].t = 0;
      poof(EN[i].x, EN[i].y, C_WHT, 5);
      if (EN[i].hp <= 0) {
        EN[i].alive = false;
        poof(EN[i].x, EN[i].y, C_PUR, 12);
        if (EN[i].type == 4) { // le GOLEM tombe
          bossDead = true; shake = 14;
          dropPickup(EN[i].x, EN[i].y + 6, 2); // L'ETOILE
        } else if (rnd(2)) dropPickup(EN[i].x, EN[i].y, -1);
      }
    }
  }
}

// ---------------- Dessin : heros, monstres, PNJ ----------------
static void drawHeartIcon(int x, int y, bool full) {
  fRect(x + 1, y, 2, 1, full ? C_RED : C_DRED);
  fRect(x + 5, y, 2, 1, full ? C_RED : C_DRED);
  fRect(x, y + 1, 8, 2, full ? C_RED : C_DRED);
  fRect(x + 1, y + 3, 6, 1, full ? C_RED : C_DRED);
  fRect(x + 2, y + 4, 4, 1, full ? C_RED : C_DRED);
  fRect(x + 3, y + 5, 2, 1, full ? C_RED : C_DRED);
  if (full) px(x + 1, y + 1, C_WHT);
}
static void drawPlayer(int ox, int oy) {
  int x = (int)plx + ox, y = (int)ply + oy;
  if (pifr > 0 && (frame & 2)) return; // clignote (invincible)
  fRect(x - 5, y - 4, 10, 10, C_TUNIC);          // tunique
  fRect(x - 5, y - 6, 10, 3, C_SKIN);           // visage
  fRect(x - 5, y - 7, 10, 2, KDColor::RGB24(0x6A4A2A)); // cheveux
  int ex = (pdir == 1) ? 3 : ((pdir == 3) ? -3 : 0);
  if (pdir != 0) { px(x - 2 + ex, y - 5, C_BLK); px(x + 2 + ex, y - 5, C_BLK); } // yeux
  fRect(x - 3, y + 6, 6, 2, KDColor::RGB24(0x3A3A3A));   // jambes
  fRect(x - 2, y + 8, 2, 1, KDColor::RGB24(0x2A2A2A)); fRect(x + 1, y + 8, 2, 1, KDColor::RGB24(0x2A2A2A));
  if (swing > 0) { // l'epee devant
    int sx = x, sy = y;
    if (pdir == 0) { sy -= 14; fRect(x - 1, sy - 4, 2, 12, C_WHT); }
    else if (pdir == 1) { sx += 14; fRect(sx - 4, y - 1, 12, 2, C_WHT); }
    else if (pdir == 2) { sy += 14; fRect(x - 1, sy - 8, 2, 12, C_WHT); }
    else { sx -= 14; fRect(sx - 8, y - 1, 12, 2, C_WHT); }
  }
}
static void drawNpc(int x, int y, int id) {
  KDColor robe = (id == 0) ? KDColor::RGB24(0x7A5AD4) : ((id == 1) ? C_MAG : ((id == 2) ? C_CYN : C_DGRN));
  int bob = ((frame >> 4) & 1) ? 1 : 0;
  fRect(x - 5, y - 5 + bob, 10, 11, robe);
  fRect(x - 4, y - 7 + bob, 8, 3, C_SKIN);
  px(x - 2, y - 6 + bob, C_BLK); px(x + 2, y - 6 + bob, C_BLK);
  if (id == 0) { fRect(x - 3, y - 8 + bob, 6, 2, KDColor::RGB24(0xE8E8E8)); } // barbe de l'Ancien
}
static void drawEnemies(int ox, int oy) {
  for (int i = 0; i < 14; i++) {
    if (!EN[i].alive) continue;
    struct En *e = &EN[i];
    int x = (int)e->x + ox, y = (int)e->y + oy;
    if (e->flash) continue; // flash blanc : invisible un frame sur deux
    if (e->type == 0) { // limace verte
      int sq = (e->t >> 3) & 1;
      fRect(x - 5, y - 3 + sq, 10, 6 - sq, C_GREEN);
      fRect(x - 5, y + 1, 10, 1, C_DGRN);
      px(x - 2, y - 1, C_BLK); px(x + 2, y - 1, C_BLK);
    } else if (e->type == 1) { // chauve-souris
      int fl = (e->t >> 2) & 1;
      fRect(x - 2, y - 2, 4, 4, C_PUR);
      if (fl) { fRect(x - 7, y - 4, 5, 2, C_PUR); fRect(x + 2, y - 4, 5, 2, C_PUR); }
      else { fRect(x - 7, y - 1, 5, 2, C_PUR); fRect(x + 2, y - 1, 5, 2, C_PUR); }
      px(x - 1, y - 1, C_RED); px(x + 1, y - 1, C_RED);
    } else if (e->type == 2) { // squelette
      fRect(x - 4, y - 7, 8, 6, C_WHT);        // crane
      px(x - 2, y - 5, C_BLK); px(x + 2, y - 5, C_BLK);
      fRect(x - 3, y - 1, 6, 7, C_WHT);        // corps
      fRect(x - 5, y - 1, 2, 5, C_WHT); fRect(x + 3, y - 1, 2, 5, C_WHT); // bras
      fRect(x - 4, y + 6, 3, 1, C_WHT); fRect(x + 1, y + 6, 3, 1, C_WHT);
    } else if (e->type == 3) { // tourelle
      fCirc(x, y, 6, C_WALL2);
      fCirc(x, y, 3, C_PUR);
      if ((e->t >> 4) & 1) fCirc(x, y, 1, C_WHT); // ame qui pulse
    } else if (e->type == 4) { // LE GOLEM
      int hop = (e->st >= 10 && e->st < 34) ? ((e->st < 20) ? -(e->st - 10) : -8 + (e->st - 20) / 2) : 0;
      fRect(x - 12, y - 14 + hop, 24, 26, C_ROCK2);
      fRect(x - 10, y - 12 + hop, 20, 10, C_ROCK);
      fRect(x - 6, y - 9 + hop, 4, 3, C_RED); fRect(x + 2, y - 9 + hop, 4, 3, C_RED); // yeux rouges
      fRect(x - 3, y - 3 + hop, 6, 2, C_ROCK);
      fRect(x - 14, y - 8 + hop, 3, 14, C_ROCK); fRect(x + 11, y - 8 + hop, 3, 14, C_ROCK); // bras
      if (bossDead == false && e->hp <= 8) { // fissures a mi-vie
        px(x - 4, y + 2 + hop, C_BLK); px(x - 3, y + 3 + hop, C_BLK); px(x + 5, y, C_BLK); px(x + 6, y + 1 + hop, C_BLK);
      }
    }
  }
  for (int b = 0; b < 6; b++) if (BU[b].on) {
    fRect((int)BU[b].x - 2 + ox, (int)BU[b].y - 2 + oy, 4, 4, C_ORG);
    px((int)BU[b].x + ox, (int)BU[b].y + oy, C_YEL);
  }
  for (int i = 0; i < 8; i++) if (PK[i].on) {
    int x = (int)PK[i].x + ox, y = (int)PK[i].y + oy + (((frame >> 3) & 1) ? 1 : 0);
    if (PK[i].t == 0) drawHeartIcon(x - 4, y - 4, true);
    else if (PK[i].t == 1) { fRect(x - 3, y - 3, 6, 6, C_GOLD); fRect(x - 1, y - 2, 2, 4, KDColor::RGB24(0xB8962A)); px(x - 1, y - 1, C_WHT); }
    else { // L'ETOILE : grosse etoile scintillante
      int r = 7 + (((frame >> 2) & 1) ? 1 : 0);
      fRect(x - 1, y - r, 2, r * 2, C_YEL);
      fRect(x - r, y - 1, r * 2, 2, C_YEL);
      fRect(x - 3, y - 3, 6, 6, C_WHT);
      poof(x, y, C_YEL, 1);
    }
  }
}

// ---------------- HUD ----------------
static void drawHud() {
  fRect(0, 0, SW, TOPOFF, KDColor::RGB24(0x141824));
  fRect(0, TOPOFF - 2, SW, 2, KDColor::RGB24(0x2A3048));
  int nh = (pmax + 1) / 2;
  for (int i = 0; i < nh; i++) drawHeartIcon(10 + i * 11, 8, php >= (i + 1) * 2); // coeur plein si assez de PV
  // pieces
  fRect(150, 10, 6, 6, C_GOLD); fRect(152, 12, 2, 3, KDColor::RGB24(0xB8962A));
  char cb[8]; snprintf(cb, 8, "%d", pcoin);
  drawTextSmall(162, 10, cb, C_WHT);
  // cle
  if (pkey > 0) { fRect(210, 9, 6, 3, C_GOLD); fRect(212, 12, 2, 2, C_GOLD); fRect(210, 14, 2, 3, C_GOLD); }
  char kb[8]; snprintf(kb, 8, "X%d", pkey);
  drawTextSmall(222, 10, kb, C_WHT);
  // etoile
  if (hasStar) { fRect(280, 8, 2, 9, C_YEL); fRect(276, 12, 10, 2, C_YEL); fRect(278, 10, 6, 5, C_WHT); }
  drawTextSmall(292, 10, "ETOILE", C_YEL);
}

// ---------------- Interactions (OK) ----------------
static bool tryInteract() {
  int tx = (int)(plx / TS), ty = (int)((ply - TOPOFF) / TS);
  int fx = tx, fy = ty;
  if (pdir == 0) fy--; else if (pdir == 1) fx++; else if (pdir == 2) fy++; else fx--;
  char c = tileAt(fx, fy);
  // coffre ?
  if (c == 'C' && !chestOpen[room]) {
    chestOpen[room] = 1;
    poof(fx * TS + 8, TOPOFF + fy * TS + 8, C_GOLD, 10);
    int k = CH[room];
    if (k == 2) { pkey++; startDlg(DLG_CLE, 1); }
    else if (k == 3) { pmax += 2; php = pmax; startDlg(DLG_COEUR, 1); }
    else { pcoin += 20; startDlg(DLG_PIECES, 1); }
    return true;
  }
  // PNJ ?
  for (int i = 0; NPC[i][0] != 255; i++) {
    if (NPC[i][0] != room || NPC[i][1] != fx || NPC[i][2] != fy) continue;
    int id = NPC[i][3];
    if (id == 0) { // L'ANCIEN
      if (hasStar && !gameDone) { dlgEnd = true; startDlg(ENDING, 5); mode = 4; }
      else startDlg(DLG_ANCIEN, 3);
    } else if (id == 1) { // LA MARCHANDE
      if (php >= pmax) startDlg(DLG_MARQ, 2);
      else if (pcoin >= 20) { pcoin -= 20; php = pmax; poof(plx, ply - 10, C_RED, 10); startDlg(DLG_MARQ, 2); }
      else startDlg(DLG_MARQ, 1);
    } else if (id == 2) startDlg(DLG_GAMIN, 2);
    else startDlg(DLG_ERMITE, 2);
    return true;
  }
  return false;
}

// ---------------- Boucle de jeu ----------------
static void playTick() {
  // ----- dialogue en cours : on avance, le jeu est en pause -----
  if (dlgOn) {
    dlgTick();
    return;
  }
  pollKeys();
  if (kHome) { mode = 0; return; } // retour au titre
  // ----- mouvement -----
  float ox = plx, oy = ply;
  float sp = 1.5f;
  float dx = (kRight ? sp : 0) - (kLeft ? sp : 0);
  float dy = (kDown ? sp : 0) - (kUp ? sp : 0);
  if (swing > 0) { dx *= 0.4f; dy *= 0.4f; } // on ralentit en frappant
  if (dx > 0) pdir = 1; else if (dx < 0) pdir = 3;
  if (dy < 0) { if (dx == 0) pdir = 0; } else if (dy > 0 && dx == 0) pdir = 2;
  float nx = plx + dx + pvx, ny = ply + dy + pvy;
  pvx *= 0.8f; pvy *= 0.8f; // amortit le knockback
  // collision : tuile de la case devant (boite 12x12)
  int tx = (int)((nx - (dx >= 0 ? 0 : 0)) / 1); // (simple : on teste les coins)
  (void)tx;
  bool okX = true, okY = true;
  {
    int c0 = (int)((nx - 6) / TS), c1 = (int)((nx + 6) / TS);
    int r0 = (int)((ply - 6 - TOPOFF) / TS), r1 = (int)((ply + 6 - TOPOFF) / TS);
    for (int rr = r0; rr <= r1 && okX; rr++)
      for (int cc = c0; cc <= c1; cc++) if (solid(cc, rr)) { okX = false; if (tileAt(cc, rr) == 'D' && !doorOpen[room]) {
        if (pkey > 0) { pkey--; doorOpen[room] = 1; startDlg(DLG_PORTE, 1); return; }
        startDlg(DLG_PORTEB, 1); return;
      } break; }
  }
  {
    int c0 = (int)((plx - 6) / TS), c1 = (int)((plx + 6) / TS);
    int r0 = (int)((ny - 6 - TOPOFF) / TS), r1 = (int)((ny + 6 - TOPOFF) / TS);
    for (int rr = r0; rr <= r1 && okY; rr++)
      for (int cc = c0; cc <= c1; cc++) if (solid(cc, rr)) { okY = false; if (tileAt(cc, rr) == 'D' && !doorOpen[room]) {
        if (pkey > 0) { pkey--; doorOpen[room] = 1; startDlg(DLG_PORTE, 1); return; }
        startDlg(DLG_PORTEB, 1); return;
      } break; }
  }
  if (okX) plx = nx;
  if (okY) ply = ny;
  (void)ox; (void)oy;
  // ----- transitions de salle -----
  int prevRoom = room;
  int ry = room / RW, rx = room % RW;
  if (plx < 8 && rx > 0) { room--; plx = SW - 12; spawnEnemies(); }
  else if (plx > SW - 8 && rx < RW - 1) { room++; plx = 12; spawnEnemies(); }
  else if (ply < TOPOFF + 8 && ry > 0) { room -= RW; ply = TOPOFF + MH * TS - 12; spawnEnemies(); }
  else if (ply > TOPOFF + MH * TS - 8 && ry < RH - 1) { room += RW; ply = TOPOFF + 12; spawnEnemies(); }
  if (room != prevRoom) { waitRelease(); return; }
  // ----- action -----
  if (swing > 0) swing--;
  if (keyOK) {
    if (!tryInteract()) { swing = 10; waitRelease(); }
  }
  hitEnemies();
  updEnemies();
  if (pifr > 0) pifr--;
  // ----- ramassage -----
  for (int i = 0; i < 8; i++) if (PK[i].on) {
    if (PK[i].x > plx - 8 && PK[i].x < plx + 8 && PK[i].y > ply - 8 && PK[i].y < ply + 8) {
      if (PK[i].t == 0) { if (php < pmax) php += 2; if (php > pmax) php = pmax; }
      else if (PK[i].t == 1) pcoin += 5;
      else { hasStar = true; startDlg(DLG_ETOILE, 2); }
      PK[i].on = false;
    }
  }
}

static void drawRoom(int ox, int oy) {
  for (int ty = 0; ty < MH; ty++)
    for (int tx = 0; tx < MW; tx++)
      drawTile(tx, ty, ox, oy);
  for (int i = 0; NPC[i][0] != 255; i++)
    if (NPC[i][0] == room) drawNpc(NPC[i][1] * TS + 8 + ox, TOPOFF + NPC[i][2] * TS + 8 + oy, NPC[i][3]);
  drawEnemies(ox, oy);
  drawPlayer(ox, oy);
  updParticles();
  drawHud();
  if (dlgOn) drawDlg();
}

// ---------------- Ecrans ----------------
static void starfield(int sc) {
  fRect(0, 0, SW, SH, KDColor::RGB24(0x0A0E1A));
  for (int i = 0; i < 40; i++) {
    int sx = (i * 89 + sc * 17) % SW, sy = ((i * 43 + sc * 11) % 200) + 8;
    if ((i + sc) & 3) px(sx, sy, KDColor::RGB24(0x6A7590)); else px(sx, sy, C_WHT);
  }
}
static void titleScreen() {
  waitRelease();
  int sc = 0;
  while (true) {
    pollKeys();
    if (keyOK) { waitRelease(); return; }
    starfield(sc++);
    fRect(120, 40, 80, 46, KDColor::RGB24(0x101626)); // silhouette du chateau
    fRect(128, 52, 12, 34, KDColor::RGB24(0x0A0E1A)); fRect(180, 52, 12, 34, KDColor::RGB24(0x0A0E1A));
    fRect(148, 26, 6, 10, C_YEL); // l'etoile au-dessus du royaume
    if (sc & 8) { px(150, 26, C_WHT); px(148, 28, C_WHT); }
    drawTextBig(64, 100, "LE ROYAUME", C_YEL);
    drawTextBig(74, 118, "OUBLIE", C_WHT);
    drawTextC(150, "UN JEU D'AVENTURE POUR NUMWORKS", C_GRE);
    if ((sc >> 3) & 1) drawTextC(170, "APPUIE SUR OK", C_WHT);
    drawTextC(196, "FLECHES : BOUGER   OK : EPEE ET PARLER", KDColor::RGB24(0x6A7590));
    drawTextC(208, "TROUVE L'ETOILE VAINE LE GOLEM", KDColor::RGB24(0x6A7590));
    blit(); Ion::Timing::msleep(40);
  }
}
static void pagesScreen(const char *pg[][3], int n, const char *label) { // pages d'histoire
  waitRelease();
  int i = 0;
  while (true) {
    pollKeys();
    if (keyOK) { waitRelease(); i++; if (i >= n) return; }
    starfield(frame++);
    fRect(20, 70, SW - 40, 80, KDColor::RGB24(0x101626));
    fRect(20, 70, SW - 40, 1, C_WHT); fRect(20, 149, SW - 40, 1, C_WHT);
    drawTextC(78, pg[i][0], C_YEL);
    drawTextC(100, pg[i][1], C_WHT);
    drawTextC(116, pg[i][2], C_WHT);
    drawTextC(132, (i < n - 1) ? "OK POUR CONTINUER" : label, KDColor::RGB24(0x6A7590));
    blit(); Ion::Timing::msleep(40);
  }
}
static void gameOverScreen() {
  waitRelease();
  while (true) {
    pollKeys();
    if (keyOK) { waitRelease(); return; }
    fRect(0, 0, SW, SH, C_BLK);
    drawTextC(70, "TU T'EVANOUIS...", C_RED);
    drawTextC(100, "L'Ancien te ranime au village.", C_WHT);
    drawTextC(116, "L'etoile compte sur toi !", C_WHT);
    if ((frame >> 3) & 1) drawTextC(150, "OK POUR CONTINUER", C_YEL);
    frame++;
    blit(); Ion::Timing::msleep(40);
  }
}

// ---------------- Nouvelle partie ----------------
static void newGame() {
  room = 7; // le village
  plx = 160; ply = TOPOFF + 96;
  pdir = 2; swing = 0; pifr = 0; pvx = pvy = 0;
  php = 6; pmax = 6; pcoin = 0; pkey = 0;
  hasStar = false; bossDead = false; gameDone = false;
  for (int i = 0; i < 18; i++) { chestOpen[i] = 0; doorOpen[i] = 0; }
  for (int i = 0; i < 36; i++) PT[i].life = 0;
  dlgOn = false; dlgEnd = false;
  spawnEnemies();
}

// ---------------- Boucle principale ----------------
void ion_main(int argc, char *argv[]) {
  (void)argc; (void)argv;
  while (true) {
    mode = 0;
    titleScreen();
    newGame();
    pagesScreen(INTRO, 4, "COMMENCER L'AVENTURE");
    mode = 2;
    while (mode == 2) {
      frame++;
      int ox = 0, oy = 0;
      if (shake > 0) { shake--; ox = rnd(7) - 3; oy = rnd(7) - 3; }
      playTick();
      if (mode != 2) break;
      drawRoom(ox, oy);
      blit();
      Ion::Timing::msleep(16);
    }
    if (mode == 3) { // defaite : ranime au village
      gameOverScreen();
      php = pmax; plx = 160; ply = TOPOFF + 96; room = 7; pifr = 0;
      spawnEnemies();
      mode = 2;
      while (mode == 2) {
        frame++;
        int ox = 0, oy = 0;
        if (shake > 0) { shake--; ox = rnd(7) - 3; oy = rnd(7) - 3; }
        playTick();
        if (mode != 2) break;
        drawRoom(ox, oy);
        blit();
        Ion::Timing::msleep(16);
      }
    }
    if (mode == 4) pagesScreen(ENDING, 5, "FIN"); // le joueur a rendu l'etoile
    // apres l'epilogue ou l'abandon (HOME) : retour au titre
  }
}

// --- Point d'entree : sans main, --gc-sections supprime tout le jeu ---
extern "C" int main(int argc, char *argv[]) {
  np_app_begin();
  palN = 1; PAL[0] = KDColor::RGB24(0); // la palette demarre sur le noir
  ion_main(argc, argv);
  return np_app_end();
}
