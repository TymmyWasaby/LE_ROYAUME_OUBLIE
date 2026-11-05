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
  int f = fadePos; // < 15 : l'ecran se reveille depuis le noir
  for (int y = 0; y < SH; y++) {
    const uint8_t *src = VRAM + (long)y * SW;
    for (int x = 0; x < SW; x++) {
      KDColor c = (src[x] < palN) ? PAL[src[x]] : KDColor::RGB24(0);
      if (f < 15) c = KDColor::RGB24((c.red() * f) / 15, (c.green() * f) / 15, (c.blue() * f) / 15);
      up[x] = c;
    }
    Ion::Display::pushRect(KDRect(0, y, SW, 1), up);
  }
  if (fadePos < 15) fadePos++;
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
  { // 3 : colline et grotte (portes E=grotte, O, S) + STELE du chevalier
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
  "t....f....L....f...t",
  "ttttttttt..ttttttttt" },
  { // 4 : hall du donjon (portes O, E, S) + coffre CLE
  "F##################F",
  "#,,,,,,,,,,,,,,,,,,#",
  "#,,##,,,,,,##,,,,,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  "#,,,,,,##,,,,,,,,,,#",
  ",,,,,,,,,,,,,,,,,,,,",
  ",,,,,,,,,,,,,,,,,,,,",
  "#,,,,,,##,,,,P,,,,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  "#,,##,,,,,,,C,,,,,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  "#########,,#########" },
  { // 5 : couloir scelle (portes O, S verrouillee D)
  "F##################F",
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
  { // 7 : LE VILLAGE (portes N, S, E, O) + coffre PIECES + STELE de la chute
  "ttttttttt..ttttttttt",
  "t....ppppppppp.P...t",
  "t....p.......p.....t",
  "t..rrp.......p..C..t",
  "t..rrp.......p.....t",
  "....................",
  "................L...",
  "t....p.......p.....t",
  "t....p..rr..p......t",
  "t....p..rr..p...P..t",
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
  { // 10 : salle des gardes (portes N, S) + STELE des heros
  "F########,,########F",
  "#,,,,,,,,,,,,,,,,,,#",
  "#,,,,##,,,,,,##,,,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  "#,,,,,,,P,,,,,P,,,,#",
  "#,,,,,,,,L,,,,,,,,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  "#,,,,##,,,,,,##,,,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  "#########,,#########" },
  { // 11 : LE GOLEM (porte N) + STELE du gardien
  "F########,,########F",
  "#,,L,,,,,,,,,,,,,,,#",
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
  "F########,,########F",
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
  "#,,,,,,,,P,,,,,,,,,#",
  "#,,,,##,,,,,,##,,,,#",
  "#,,,,,,,,C,,,,,,,,,#",
  "#,,,,,,,,,,,,,,,,,,#",
  "####################" },
};
// contenu des coffres par salle : 0 rien, 1 pieces, 2 cle, 3 recipient de coeur
static const uint8_t CH[18] = { 0,0,0,0, 2,0,0, 1,0,0, 0,0, 0,0,0,0, 3, 1 };
// ouvertures par salle : N=1 S=2 E=4 O=8 (le monde reste fixe, les salles non)
static const uint8_t GAT[18] = { 6,14,14,14, 14,10, 7,15,15,11, 3,1, 5,13,13,9, 5,8 };
// densite d'ennemis par salle (le GOLEM est en 11)
static const uint8_t EDEN[18] = { 2,2,2,1, 2,2, 2,1,2,2, 3,0, 2,2,2,2, 2,2 };
// coffres des alcoves secretes : 1 pieces, 4 fiole d'air, 5 pierre d'echo, 6 plume
static const uint8_t SCH[18] = { 0,0,0,0, 0,0, 0,0,4,1, 0,0, 0,5,1,6, 0,0 };
// spawns generes a chaque partie : { type, X, Y } par salle
static uint8_t ESW[18][5][3]; static uint8_t ESWN[18];
// PNJ : { salle, tuileX, tuileY, id } — 0 Ancien, 1 Marchande, 3 Gamin, 4 Ermite
static const uint8_t NPC[][4] = {
  { 7, 12, 6, 0 }, { 7, 14, 8, 1 }, { 7, 5, 8, 2 }, { 3, 10, 9, 3 }, { 7, 16, 8, 4 },
  { 255, 0, 0, 0 }
};

// ---------------- Dialogues ----------------
static const char *DLG_ANCIEN[5][3] = {
  { "AH... UN ESPRIT DU VENT !", "Le dernier esprit. Alors il", "reste un espoir..." },
  { "Notre ETOILE a disparu dans", "les cavernes du nord. Sans", "elle, le royaume se meurt." },
  { "Sa lumiere guidait nos", "moissons, nos reves, nos", "chansons. Le noir ronge tout." },
  { "Ton SOUFFLE (OK) repousse", "tout. Fais percuter les monstres", "contre murs, torches ou l'eau !" },
  { "Un secret : certains rochers", "sont FISSURES. Souffle trois", "fois dessus... et regarde." }
};
static const char *DLG_MARQ[3][3] = {
  { "BIENVENUE, esprit du vent !", "Pour 20 PIECES, je te rends", "tous tes coeurs. Marche ?" },
  { "Reviens quand tu veux !", "Et garde un peu d'etonnement", "pour la fin... ca t'attend." }
};
static const char *DLG_GAMIN[3][3] = {
  { "Le gamin du village :", "Avant, on entendait le GOLEM", "pleurer, la nuit, tres loin." },
  { "Papa dit qu'un voleur", "pleure pas. Alors c'est quoi,", "un geant qui sanglote ?" },
  { "Si tu le vois un jour...", "sois gentil avec lui,", "d'accord ? Promis ?" }
};
static const char *DLG_ERMITE[4][3] = {
  { "L'ermite de la colline :", "Cette grotte mene au donjon.", "Emporte une CLE, ou n'entre pas." },
  { "Le GOLEM pese trop pour ton", "souffle. Souffle sur SES", "rochers pour les lui renvoyer !" },
  { "Le vent chuchote que des poches", "cachent les fissures des salles.", "Insiste, souffle, fouille..." },
  { "Une chose encore : la pierre", "souffre comme nous. Frappe-le", "s'il le faut. Plains-le surtout." }
};
static const char *DLG_CONTEUSE[4][3] = {
  { "La CONTEUSE du village :", "Autrefois le GOLEM jardinait", "les fleurs du donjon. Si doux..." },
  { "L'etoile l'avait choisi, lui,", "le geant, pour veiller sur elle", "chaque nuit. Quel honneur..." },
  { "Puis sa lumiere a faibli, et", "le geant a panique. Il l'a", "emportee pour mieux la soigner." },
  { "Le royaume ment quand il dit", "'voleur'. Il ne comprend pas", "qu'on garde ce qu'on aime. Chut..." }
};
static const char *DLG_ANCIEN2[3][3] = { // l'Ancien, une fois le donjon visite
  { "Tu es entre dans le donjon...", "Alors tu as vu les", "steles. Tu as lu." },
  { "Mille fois j'ai maudit le", "golem devant tous. Mille", "fois, peut-etre a tort..." },
  { "Va, esprit. Decouvre la", "verite mieux que moi,", "qui n'ai su que juger." }
};
static const char *DLG_MARQ2[2][3] = { // la marchande, l'etoile revenue
  { "La marchande, des larmes :", "L'etoile... tu l'as ? Le", "village va vraiment revivre ?" },
  { "Tiens. Tout ce que je", "possede... vaut moins", "que ce ciel-la. Merci." }
};
static const char *DLG_GAMIN2[2][3] = { // le gamin, le golem vaincu
  { "Le gamin, les yeux ronds :", "Tu l'as vu ? Il etait", "comment ? Il etait..." },
  { "...gentil ? Je le savais !", "Je lui avais laisse un", "dessin. Il ne l'a pas froisse." }
};
static const char *DLG_ERMITE2[3][3] = { // l'ermite, le golem au repos
  { "L'ermite pose sa lanterne :", "Alors. C'est fait ?", "Gardien, ou deluge ?" },
  { "Je vivais ici quand il", "plantait ses fleurs. Je lui", "parlais. Il ecoutait." },
  { "Une seule fois il repondit :", "\"Je ne veux pas qu'elle", "ait froid dans le noir.\"" }
};
static const char *DLG_CONTEUSE2[4][3] = { // la conteuse, si tu portes une relique
  { "La conteuse, tout bas :", "Une relique ? Alors tu", "merites leur histoire." },
  { "LA FIOLE D'AIR : un souffle", "du premier matin du monde,", "exhale par l'ETOILE meme." },
  { "LA PIERRE D'ECHO : elle", "garde les voix des gardiens", "disparus. Ecoute-la, un soir." },
  { "LA PLUME : elle tomba de", "l'aile du premier geant", "qui apprit a s'envoler." }
};
static const char *DLG_STELE3[3][3] = { // la stele de la colline
  { "STELE DE LA COLLINE :", "\"Ici mourut le dernier", "chevalier de Tallia.\"" },
  { "\"Sa lame brisa sur la", "pierre. La pierre, elle,", "n'a jamais brise.\"" },
  { "Plus bas, une main plus", "douce a grave :", "\"Ecoute le vent, plutot.\"" }
};
static const char *DLG_STELE7[4][3] = { // la stele du village
  { "LA STELE DU VILLAGE :", "\"Ici tomba l'ETOILE,", "il y a mille saisons.\"" },
  { "\"Elle ne fit pas un", "cratere. Elle fit un", "jardin. Nul ne fut blesse.\"" },
  { "\"Nous l'aimames. Nous", "oubliames seulement de", "lui demander...\"" },
  { "...si elle, aussi, savait", "vieillir.\"", "- Conseil des Anciens -" }
};
static const char *DLG_STELE10[3][3] = { // la stele des gardes
  { "STELE DES GARDES :", "\"Ici veillerent les", "heros de jadis.\"" },
  { "\"Trente lames. Trente", "echecs. La porte du fond", "leur fut funeste.\"" },
  { "\"Que le vent reussisse", "ou l'acier echoua.\"", "- Capitaine Orwen -" }
};
static const char *DLG_STELE11[4][3] = { // la stele du gardien
  { "LA STELE EFFONDREE :", "\"Chambre du GARDIEN.", "Qu'il soit honore.\"" },
  { "\"Concu par l'etoile meme", "pour veiller sur son", "sommeil et son feu.\"" },
  { "Honte a qui l'appelle", "voleur.\"", "Les runes sont usees." },
  { "Quelqu'un, recemment, a", "pose des fleurs ici.", "Fraiches. Encore." }
};
static const char *DLG_FIOLE[1][3] = {
  { "UNE FIOLE D'AIR PUR !", "Ton souffle se fait profond :", "jauge maximale augmentee !" }
};
static const char *DLG_ECHO[1][3] = {
  { "LA PIERRE D'ECHO !", "Desormais ton souffle porte", "plus loin, bien plus loin." }
};
static const char *DLG_PLUME[1][3] = {
  { "UNE PLUME D'ANGE !", "Ton souffle devient ouragan :", "les monstres volent plus fort." }
};
static const char *DLG_CLE[1][3] = {
  { "TU TROUVES UNE CLE DOREE !", "Elle est chaude, comme si", "quelqu'un l'avait serree fort." }
};
static const char *DLG_COEUR[1][3] = {
  { "UN RECIPIENT DE COEUR !", "Un coeur bat la dedans,", "patient. Tes coeurs montent !" }
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
static const char *DLG_ETOILE[3][3] = {
  { "L'ETOILE DU ROYAUME !", "Elle palpite dans tes mains,", "chaude et vivante." },
  { "Le GOLEM s'effondre en", "poussiere... Sa derniere larme", "de pierre roule sur le sol." },
  { "Dans sa paume ouverte :", "des pierres chaudes faconnees", "en forme de berceau." }
};
static const char *INTRO[7][3] = {
  { "Il y a bien longtemps,", "une ETOILE est tombee du ciel", "sur le royaume de Tallia." },
  { "Elle ne brula rien. Elle", "choisit un jardin, un lac,", "un village. Elle resta." },
  { "Les moissons poussaient la", "nuit, les enfants lisaient sous", "sa lueur. Personne n'avait peur." },
  { "Mais une etoile qui brille", "s'eteint un jour, ou l'autre.", "La sienne commenca de faiblir." },
  { "Alors, du fond des cavernes,", "monta un GOLEM de pierre,", "faconne jadis pour la servir." },
  { "Les heros vinrent, lames au", "poing. La pierre ne connait ni", "peur ni blessure. Rien n'y fit." },
  { "Le royaume s'eteint, doucement.", "L'Ancien invoqua les ESPRITS", "du vent. Le dernier... c'est toi." }
};
static const char *ENDING[8][3] = {
  { "TU RENDS L'ETOILE A L'ANCIEN.", "Elle hesite une seconde...", "Puis remonte, rappelee chez elle." },
  { "Sa lumiere coule sur le village,", "les toits, le lac, les champs...", "Le royaume respire enfin." },
  { "Et tu murmures a l'Ancien la", "verite du donjon : le GOLEM", "n'etait pas un voleur." },
  { "L'Ancien ferme les yeux :", "\"Nous avons maudit notre gardien.", "Pardonne-nous, seigneur pierre.\"" },
  { "On grava sur le seuil du donjon :", "\"Il gardait sa lumiere comme", "elle le gardait du noir.\"" },
  { "Et quelque part, tres bas,", "dans la poussiere du donjon,", "quelque chose repose en paix." },
  { "L'etoile brille desormais un", "peu plus fort chaque nuit.", "Comme quelqu'un qui remercie." },
  { "FIN - LE ROYAUME OUBLIE", "MERCI D'AVOIR JOUE !", "" }
};

// ---------------- Etat du jeu ----------------
static int mode; // 0 titre, 1 intro, 2 jeu, 3 defaite, 4 epilogue
static int room; // salle courante 0..17
static float plx, ply, pvx, pvy; // position + knockback
static int pdir, swing, pifr, brth, brthT; // direction, animation de souffle, invincibilite, jauge de souffle
static int php, pmax, pcoin, pkey; static bool hasStar, bossDead, gameDone;
static bool hasFlask, hasEcho, hasPlume; // tresors secrets
static unsigned titleSc; // grain de hasard : le temps passe sur le titre
static uint8_t chestOpen[18], doorOpen[18]; static bool visited[18]; static bool pmov;
static int frame, shake; static int fadePos = 15; // fondu de palette (15 = eclair)
// dialogue en cours
static const char *(*dgp)[3]; // pointeur vers un tableau de pages
static int dgN, dgI; static bool dlgOn, dlgEnd; // dlgEnd : apres la derniere page, fin du jeu

// monstres vivants (le boss est EN[0], type 4)
static struct En { float x, y, vx, vy; int type, hp, t, st, dir; bool alive, flash; } EN[14];
static int enN;
static struct Bu { float x, y, vx, vy; int fr; bool on; } BU[6]; // fr : projectile renvoye par le souffle
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
static char RMO[18][12][21]; // copie modifiable de la carte (generee, pots brises...)
static bool isDun(int r) { return (r >= 4 && r <= 5) || r >= 10; } // salle de donjon ?
static char tileAt(int tx, int ty) {
  if (tx < 0 || tx >= MW || ty < 0 || ty >= MH) return ' ';
  return RMO[room][ty][tx];
}
static bool npcAt(int tx, int ty) {
  for (int i = 0; NPC[i][0] != 255; i++)
    if (NPC[i][0] == room && NPC[i][1] == tx && NPC[i][2] == ty) return true;
  return false;
}
static bool solid(int tx, int ty) {
  char c = tileAt(tx, ty);
  if (c == 'D' && doorOpen[room]) return false; // porte ouverte
  if (c == 't' || c == 'r' || c == 'w' || c == ' ' || c == '#' || c == 'D' || c == 'C' || c == 'F' || c == 'P' || c == 'B' || c == 'b' || c == 'L') return true;
  if (npcAt(tx, ty)) return true;
  return false;
}
static void drawTile(int tx, int ty, int ox, int oy) {
  char c = tileAt(tx, ty);
  int x = tx * TS + ox, y = TOPOFF + ty * TS + oy;
  if (c == '.' || c == 'f' || c == 'p' || c == 'P' || c == 'c') {
    if (c == 'p') { // chemin : bordures qui fondent dans l'herbe
      fRect(x, y, TS, TS, C_PATH);
      px(x + 2, y + 5, KDColor::RGB24(0xB0904E)); px(x + 9, y + 11, KDColor::RGB24(0xB0904E));
      px(x + 13, y + 3, KDColor::RGB24(0xB0904E));
      char n;
      n = tileAt(tx, ty - 1); if (n != 'p' && n != ' ') fRect(x, y, TS, 2, C_GRAS2);
      n = tileAt(tx, ty + 1); if (n != 'p' && n != ' ') fRect(x, y + 14, TS, 2, C_GRAS2);
      n = tileAt(tx - 1, ty); if (n != 'p' && n != ' ') fRect(x, y, 2, TS, C_GRAS2);
      n = tileAt(tx + 1, ty); if (n != 'p' && n != ' ') fRect(x + 14, y, 2, TS, C_GRAS2);
    } else {
      fRect(x, y, TS, TS, ((tx * 7 + ty * 13) & 3) ? C_GRASS : C_GRAS2);
      int h2 = (tx * 5 + ty * 3) & 15;
      px(x + 3 + (tx & 3), y + 9, C_DGRN); px(x + 11, y + 4 + (ty & 2), C_DGRN);
      if (h2 == 0) { px(x + 8, y + 6, C_DGRN); px(x + 7, y + 7, C_DGRN); px(x + 9, y + 7, C_DGRN); }
      else if (h2 == 5) { px(x + 5, y + 12, KDColor::RGB24(0x7CCE6A)); px(x + 12, y + 6, KDColor::RGB24(0x7CCE6A)); } // brins clairs
    }
    if (c == 'f') { // fleurs qui ondulent
      int sw = ((frame >> 4) + tx) & 1;
      fRect(x + 4 + sw, y + 10, 1, 3, C_DGRN); fRect(x + 11 - sw, y + 9, 1, 3, C_DGRN);
      px(x + 3 + sw, y + 8, C_YEL); px(x + 4 + sw, y + 8, C_ORG); px(x + 4 + sw, y + 9, C_ORG); px(x + 5 + sw, y + 9, C_YEL);
      px(x + 10 - sw, y + 7, C_WHT); px(x + 11 - sw, y + 7, C_MAG); px(x + 11 - sw, y + 8, C_WHT);
      if ((tx + ty) & 1) { px(x + 8, y + 12, C_CYN); px(x + 9, y + 12, C_CYN); } // fleur bleue
    }
    if (c == 'c') { // gueule de grotte
      fRect(x, y, TS, TS, C_ROCK);
      fRect(x + 3, y + 1, 13, 15, C_ROCK2);
      fRect(x + 5, y + 3, 11, 13, C_ROCK);
      fRect(x + 6, y + 5, 9, 11, C_BLK);
      fRect(x + 7, y + 6, 7, 9, KDColor::RGB24(0x0A0A0E)); // fond encore plus noir
      px(x + 7, y + 2, KDColor::RGB24(0xB0B0B0)); px(x + 12, y + 3, KDColor::RGB24(0xB0B0B0));
    }
    if (c == 'P') { // pot en terre cuite
      fCirc(x + 8, y + 13, 5, KDColor::RGB24(0x2A5A1E));
      fRect(x + 5, y + 3, 6, 2, KDColor::RGB24(0x8A5030));
      fRect(x + 4, y + 5, 8, 2, KDColor::RGB24(0x9A5A38));
      fRect(x + 3, y + 7, 10, 6, KDColor::RGB24(0xB06840));
      fRect(x + 4, y + 8, 8, 4, KDColor::RGB24(0xC98050));
      px(x + 5, y + 8, KDColor::RGB24(0xE0A070)); px(x + 6, y + 9, KDColor::RGB24(0xE0A070));
      fRect(x + 3, y + 12, 10, 1, KDColor::RGB24(0x8A5030));
    }
  } else if (c == 't') { // arbre : houppier en trois tons
    fRect(x, y, TS, TS, ((tx * 7 + ty * 13) & 3) ? C_GRASS : C_GRAS2);
    fCirc(x + 9, y + 12, 6, KDColor::RGB24(0x1E4A18));
    fRect(x + 6, y + 9, 4, 6, KDColor::RGB24(0x6A4A2A));
    fRect(x + 6, y + 9, 1, 6, KDColor::RGB24(0x523A20));
    int sw = ((frame >> 5) + ty) & 1;
    fCirc(x + 8 + sw, y + 6, 7, KDColor::RGB24(0x16421E)); // masse sombre
    fCirc(x + 7 + sw, y + 5, 5, C_TREE);                   // volume
    fCirc(x + 5 + sw, y + 3, 3, C_TREE2);                  // lumiere
    px(x + 9 + sw, y + 2, KDColor::RGB24(0x4A9E48));
    if (((tx * 3 + ty) & 7) == 2) { px(x + 5 + sw, y + 8, C_RED); px(x + 10 + sw, y + 5, C_RED); } // baies
    px(x + 7, y + 14, C_DGRN);
  } else if (c == 'w') { // eau profonde : houle croisee, bulles, nenuphars
    fRect(x, y, TS, TS, C_WTR);
    fRect(x + 1, y + 1, 14, 14, KDColor::RGB24(0x103050)); // les grandes profondeurs
    fRect(x + 2, y + 2, 12, 12, KDColor::RGB24(0x0E2438));
    int ph = (frame >> 3) & 3;
    for (int i = 0; i < 3; i++) { // deux trains de houle qui se croisent
      px(x + 2 + i * 5 + (ph & 3), y + 3 + i * 4 + ((ph + ty) & 1), C_WTR2);
      px(x + 3 + i * 5 + (ph & 3), y + 3 + i * 4 + ((ph + ty) & 1), C_WTR2);
      px(x + 10 - i * 3 - ((ph + 1) & 3), y + 4 + i * 4, KDColor::RGB24(0x8AB8F0));
    }
    if (((tx * 3 + ty * 5 + (frame >> 4)) & 15) == 0) { px(x + 7, y + 6, KDColor::RGB24(0xB0D8FF)); px(x + 8, y + 7, C_WHT); } // scintillement
    int bub = (frame + tx * 5 + ty * 3) & 31; // bulle qui remonte du fond
    if (bub < 14) px(x + 4 + ((ty & 1) * 6), y + 13 - bub, KDColor::RGB24(0xB0D8FF));
    if (((tx * 5 + ty * 7) & 31) == 0) { // nenuphar a la derive
      fCirc(x + 8, y + 8, 4, KDColor::RGB24(0x2E6A2E)); px(x + 9, y + 7, C_DGRN); px(x + 6, y + 9, KDColor::RGB24(0x4A9E48));
    }
    char n;
    n = tileAt(tx, ty - 1); if (n != 'w' && n != ' ') { fRect(x, y, TS, 2, KDColor::RGB24(0x8AB8F0)); if (((tx + (frame >> 4)) & 1)) { px(x + 3, y + 1, C_WHT); px(x + 11, y + 1, C_WHT); } }
    n = tileAt(tx, ty + 1); if (n != 'w' && n != ' ') { fRect(x, y + 14, TS, 2, KDColor::RGB24(0x5A82C0)); if (((tx + (frame >> 4)) & 1)) px(x + 6, y + 14, KDColor::RGB24(0x8AB8F0)); }
    n = tileAt(tx - 1, ty); if (n != 'w' && n != ' ') { fRect(x, y, 2, TS, KDColor::RGB24(0x8AB8F0)); if (((ty + (frame >> 3)) & 1)) { px(x + 1, y + 4, C_WHT); px(x + 1, y + 11, C_WHT); } }
    n = tileAt(tx + 1, ty); if (n != 'w' && n != ' ') { fRect(x + 14, y, 2, TS, KDColor::RGB24(0x5A82C0)); if (((ty + (frame >> 3)) & 1)) px(x + 14, y + 8, KDColor::RGB24(0x8AB8F0)); }
  } else if (c == 'r') { // rocher : face sombre + lumiere + mousse
    fRect(x, y, TS, TS, ((tx * 7 + ty * 13) & 3) ? C_GRASS : C_GRAS2);
    fCirc(x + 9, y + 12, 6, KDColor::RGB24(0x1E4A18));
    fCirc(x + 8, y + 10, 6, KDColor::RGB24(0x4E4E56));  // face sombre
    fCirc(x + 8, y + 8, 6, C_ROCK);
    fCirc(x + 6, y + 7, 3, KDColor::RGB24(0xB0B0B0));
    px(x + 5, y + 5, KDColor::RGB24(0xD0D0D0));
    px(x + 11, y + 12, C_ROCK2); px(x + 6, y + 12, C_ROCK2);
    if (((tx * 5 + ty) & 7) == 3) { px(x + 10, y + 9, KDColor::RGB24(0x3A5A34)); px(x + 11, y + 8, KDColor::RGB24(0x3A5A34)); }
  } else if (c == '#' || c == 'F') { // mur de briques (+ torche)
    fRect(x, y, TS, TS, C_WALL);
    fRect(x, y, TS, 1, KDColor::RGB24(0x565664));      // arete eclairee
    fRect(x, y + 14, TS, 1, KDColor::RGB24(0x1A1A22)); // base assombrie
    fRect(x, y + 7, TS, 1, KDColor::RGB24(0x2E2E38));
    fRect(x + ((ty & 1) ? 3 : 10), y, 1, 7, KDColor::RGB24(0x2E2E38));
    fRect(x + ((ty & 1) ? 10 : 4), y + 8, 1, 6, KDColor::RGB24(0x2E2E38));
    px(x + 2, y + 3, C_WALL2); px(x + 13, y + 11, C_WALL2);
    if (((tx * 5 + ty) & 7) == 0) { px(x + 6, y + 6, KDColor::RGB24(0x3A5A34)); px(x + 12, y + 2, KDColor::RGB24(0x3A5A34)); }
    if (c == 'F') { // TORCHE : flamme ronde en quatre temps
      fRect(x + 7, y + 7, 2, 7, KDColor::RGB24(0x6A4A2A));
      px(x + 6, y + 7, KDColor::RGB24(0x8A6A3A));
      int fl = (frame >> 3) & 3;
      fCirc(x + 8, y + 4, 4, C_ORG);
      fCirc(x + 8, y + 4 + (fl & 1), 2, C_YEL);
      px(x + 7, y + 3 - (fl >> 1), C_WHT); px(x + 9, y + 4, C_WHT);
      if (((frame >> 2) & 3) == 0) px(x + 8, y - 1, KDColor::RGB24(0xFFE888)); // etincelle
    }
  } else if (c == ',') { // dalle fissuree + runes rares
    fRect(x, y, TS, TS, C_FLR);
    if ((tx + ty) & 1) fRect(x, y, TS, 1, C_FLR2);
    else fRect(x, y + 8, TS, 1, C_FLR2);
    px(x + 15, y, C_WALL2); px(x, y + 15, C_WALL2);
    if (((tx * 3 + ty * 5) & 15) == 0) { // rune ancienne qui veille
      px(x + 7, y + 6, C_PUR); px(x + 8, y + 7, C_PUR); px(x + 7, y + 8, C_PUR);
      if ((frame >> 6) & 1) px(x + 8, y + 6, C_MAG);
    }
    if (((tx * 3 + ty * 5) & 7) == 0) { px(x + 4, y + 6, C_WALL2); px(x + 5, y + 7, C_WALL2); px(x + 11, y + 12, C_WALL2); }
  } else if (c == 'D') {
    fRect(x, y, TS, TS, C_WALL);
    fRect(x, y + 7, TS, 1, KDColor::RGB24(0x2E2E38));
    if (doorOpen[room]) { // ouverte : passage sombre
      fRect(x + 1, y, 14, TS, C_BLK);
      fRect(x + 3, y + 2, 10, 12, KDColor::RGB24(0x14141A));
    } else {
      fRect(x + 2, y, 12, TS, C_GOLD);
      fRect(x + 2, y, 12, 1, KDColor::RGB24(0xFFE888));
      fRect(x + 4, y + 2, 8, 13, KDColor::RGB24(0xB8962A));
      fRect(x + 4, y + 2, 8, 1, KDColor::RGB24(0xE8C858));
      fRect(x + 7, y + 6, 2, 4, C_BLK); fRect(x + 7, y + 3, 2, 2, C_BLK); // trou de serrure
      px(x + 3, y + 1, KDColor::RGB24(0xFFE888)); px(x + 12, y + 14, KDColor::RGB24(0x8A6A18));
    }
  } else if (c == 'C') {
    fRect(x, y, TS, TS, isDun(room) ? C_FLR : C_GRASS);
    fCirc(x + 8, y + 13, 6, isDun(room) ? KDColor::RGB24(0x2E2E38) : KDColor::RGB24(0x2A5A1E));
    if (chestOpen[room]) { // ouvert : vide
      fRect(x + 1, y + 7, 14, 8, C_CHEST);
      fRect(x + 1, y + 2, 14, 4, KDColor::RGB24(0x5A3A1A));
      fRect(x + 1, y + 7, 14, 1, C_CHEST2);
      fRect(x + 6, y + 9, 4, 3, C_BLK);
    } else {
      fRect(x + 1, y + 4, 14, 11, C_CHEST2); fRect(x + 1, y + 2, 14, 5, C_CHEST);
      fRect(x + 1, y + 2, 14, 1, KDColor::RGB24(0xC89058));
      fRect(x + 6, y + 6, 4, 4, C_GOLD); fRect(x + 7, y + 7, 2, 2, C_BLK); // serrure
      if (((frame >> 4) & 3) == 0) px(x + 13, y + 3, C_WHT); // eclat scintillant
    }
  } else if (c == 'L') { // STELE ANCIENNE : la memoire gravee du royaume
    fRect(x, y, TS, TS, isDun(room) ? C_FLR : C_GRASS);
    fCirc(x + 8, y + 13, 5, isDun(room) ? KDColor::RGB24(0x2E2E38) : KDColor::RGB24(0x2A5A1E)); // ombre
    fRect(x + 3, y + 3, 10, 10, C_ROCK2);   // socle
    fRect(x + 4, y + 1, 8, 12, C_ROCK);     // dalle gravee
    fRect(x + 4, y + 1, 8, 1, KDColor::RGB24(0xB0B0B0)); // arete eclairee
    int rp = ((frame >> 5) & 1);            // runes qui se reveillent
    px(x + 6, y + 4, rp ? C_PUR : C_MAG); px(x + 8, y + 5, rp ? C_MAG : C_PUR);
    px(x + 10, y + 4, rp ? C_PUR : C_MAG); px(x + 7, y + 7, C_PUR);
    px(x + 6, y + 9, rp ? C_MAG : C_PUR); px(x + 10, y + 9, rp ? C_PUR : C_MAG);
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
  int by = TOPOFF + MH * TS - 52;
  fRect(4, by - 2, SW - 8, 52, KDColor::RGB24(0x0C0E16));
  fRect(6, by, SW - 12, 48, C_BLK);
  fRect(6, by, SW - 12, 1, KDColor::RGB24(0x4A5A78));       // bordure claire
  fRect(6, by + 47, SW - 12, 1, KDColor::RGB24(0x4A5A78));
  fRect(6, by, 1, 48, KDColor::RGB24(0x4A5A78));
  fRect(6 + SW - 13, by, 1, 48, KDColor::RGB24(0x4A5A78));
  px(6, by, C_CYN); px(6 + SW - 13, by, C_CYN); px(6, by + 47, C_CYN); px(6 + SW - 13, by + 47, C_CYN); // coins magiques
  drawTextSmall(14, by + 7, dgp[0][0], C_YEL);
  drawTextSmall(14, by + 19, dgp[0][1], C_WHT);
  drawTextSmall(14, by + 31, dgp[0][2], C_WHT);
  int bb = ((frame >> 3) & 1) ? 1 : 0;
  drawTextSmall(SW - 44, by + 38 + bb, "OK", C_YEL);
}

// ---------------- Monstres ----------------
static void spawnEnemies() {
  enN = 0;
  for (int i = 0; i < 14; i++) { EN[i].alive = false; BU[i % 6].on = false; BU[i % 6].fr = 0; }
  for (int i = 0; i < 8; i++) PK[i].on = false;
  for (int i = 0; i < ESWN[room] && enN < 13; i++) {
    EN[enN].x = ESW[room][i][1] * TS + 8; EN[enN].y = TOPOFF + ESW[room][i][2] * TS + 8;
    EN[enN].type = ESW[room][i][0]; EN[enN].vx = EN[enN].vy = 0;
    EN[enN].hp = (ESW[room][i][0] >= 2) ? 2 : 1;
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
    e->t++; e->flash = (e->t & 2) && e->st == 99; // clignote quand etourdi
    if (e->st == 98) { // EMPORTÉ PAR LE SOUFFLE : il fonce jusqu'a la percussion
      e->x += e->vx; e->y += e->vy;
      e->vx *= 0.93f; e->vy *= 0.93f;
      int tx = (int)(e->x / TS), ty = (int)((e->y - TOPOFF) / TS);
      char c = tileAt(tx, ty);
      if (c == 'D' && doorOpen[room]) c = ',';
      bool done = false;
      if (c == 'w') { // PLONGE DANS L'EAU
        e->alive = false;
        poof(e->x, e->y, C_WTR2, 12); poof(e->x, e->y - 4, C_WHT, 5);
        if (rnd(2)) dropPickup(e->x, e->y - 6, -1);
        done = true;
      } else if (c == 'F') { // BRULE DANS UNE TORCHE
        e->alive = false;
        poof(e->x, e->y, C_ORG, 12); poof(e->x, e->y - 4, C_YEL, 6);
        if (rnd(2)) dropPickup(e->x, e->y - 6, -1);
        done = true;
      } else if (c != '.' && c != ',' && c != 'p' && c != 'f' && c != 'c' && c != 'P') { // PERCUTE UN MUR
        e->hp -= hasPlume ? 3 : 2;
        poof(e->x, e->y, C_WHT, 7); shake = 3;
        if (e->hp <= 0) {
          e->alive = false; poof(e->x, e->y, C_PUR, 12);
          if (rnd(2)) dropPickup(e->x, e->y - 6, -1);
        } else { e->st = 99; e->t = 0; }
        done = true;
      }
      if (!done) {
        for (int j = 0; j < 14 && !done; j++) { // deux volants se percutent
          if (j == i || !EN[j].alive || EN[j].st != 98) continue;
          float ddx = EN[j].x - e->x, ddy = EN[j].y - e->y;
          if (ddx * ddx + ddy * ddy < 144) {
            e->hp--; EN[j].hp--;
            poof((e->x + EN[j].x) / 2, (e->y + EN[j].y) / 2, C_WHT, 8);
            if (e->hp <= 0) { e->alive = false; poof(e->x, e->y, C_PUR, 10); if (rnd(2)) dropPickup(e->x, e->y, -1); }
            else { e->st = 99; e->t = 0; }
            if (EN[j].hp <= 0) { EN[j].alive = false; poof(EN[j].x, EN[j].y, C_PUR, 10); if (rnd(2)) dropPickup(EN[j].x, EN[j].y, -1); }
            else { EN[j].st = 99; EN[j].t = 0; }
            done = true;
          }
        }
        if (!done && e->vx * e->vx + e->vy * e->vy < 0.5f) e->st = 0; // le vent retombe
      }
      continue;
    }
    if (e->st == 99) { e->st++; if (e->st > 104) e->st = 0; } // etourdi apres une percussion
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
          BU[b].fr = 0; BU[b].on = true;
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
              if (bb < 6) { BU[bb].x = e->x; BU[bb].y = e->y; BU[bb].vx = cosf(a2) * 1.5f; BU[bb].vy = sinf(a2) * 1.5f; BU[bb].fr = 0; BU[bb].on = true; }
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
      if (e->st != 99) { // etourdi : immobile
        if (walkable(nx, e->y, r)) e->x = nx; else e->vx = 0;
        if (walkable(e->x, ny, r)) e->y = ny; else e->vy = 0;
      }
      if (e->x < 6) e->x = 6; if (e->x > SW - 6) e->x = SW - 6;
      if (e->y < TOPOFF + 6) e->y = TOPOFF + 6; if (e->y > TOPOFF + MH * TS - 6) e->y = TOPOFF + MH * TS - 6;
    }
    // contact : le monstre blesse
    float er = (e->type == 4) ? 13 : 7;
    if (e->st == 0 && plx > e->x - er - 6 && plx < e->x + er + 6 && ply > e->y - er - 6 && ply < e->y + er + 6)
      hurtPlayer(e->type == 4 ? 2 : 1, e->x, e->y);
  }
  // projectiles (fr = renvoye par le souffle : blesse les monstres)
  for (int b = 0; b < 6; b++) if (BU[b].on) {
    BU[b].x += BU[b].vx; BU[b].y += BU[b].vy;
    int tx = (int)(BU[b].x / TS), ty = (int)((BU[b].y - TOPOFF) / TS);
    if (BU[b].x < 2 || BU[b].x > SW - 2 || BU[b].y < TOPOFF + 2 || BU[b].y > TOPOFF + MH * TS - 2 || solid(tx, ty)) { BU[b].on = false; continue; }
    if (BU[b].fr) { // rocher renvoye : cherche un monstre a percuter
      for (int i = 0; i < 14; i++) if (EN[i].alive) {
        float er2 = (EN[i].type == 4) ? 16 : 8;
        if (BU[b].x > EN[i].x - er2 && BU[b].x < EN[i].x + er2 && BU[b].y > EN[i].y - er2 && BU[b].y < EN[i].y + er2) {
          BU[b].on = false;
          EN[i].hp -= 2;
          poof(BU[b].x, BU[b].y, C_YEL, 6);
          if (EN[i].type == 4) shake = 5;
          if (EN[i].hp <= 0) {
            EN[i].alive = false;
            poof(EN[i].x, EN[i].y, C_PUR, 12);
            if (EN[i].type == 4) { bossDead = true; shake = 14; dropPickup(EN[i].x, EN[i].y + 6, 2); }
            else if (rnd(2)) dropPickup(EN[i].x, EN[i].y, -1);
          } else { EN[i].st = 99; EN[i].t = 0; }
          break;
        }
      }
    } else if (BU[b].x > plx - 6 && BU[b].x < plx + 6 && BU[b].y > ply - 6 && BU[b].y < ply + 6) {
      BU[b].on = false; hurtPlayer(1, BU[b].x - BU[b].vx, BU[b].y - BU[b].vy);
    }
  }
}
static void blow() { // LE SOUFFLE : l'esprit repousse tout devant lui
  swing = 12;
  float dx = (pdir == 1) ? 1 : ((pdir == 3) ? -1 : 0);
  float dy = (pdir == 2) ? 1 : ((pdir == 0) ? -1 : 0);
  float rg = hasEcho ? 66.0f : 44.0f; // la PIERRE D'ECHO porte plus loin
  float rr = rg * rg;
  float pf = hasPlume ? 6.2f : 4.6f; // la PLUME d'ange force l'ouragan
  // les monstres du cone partent en vol (le golem est trop lourd)
  for (int i = 0; i < 14; i++) {
    struct En *e = &EN[i];
    if (!e->alive || e->type == 4) continue;
    float ex = e->x - plx, ey = e->y - ply;
    float d2 = ex * ex + ey * ey;
    if (d2 < 1 || d2 > rr) continue;
    float d = sqrtf(d2);
    if ((ex * dx + ey * dy) / d < 0.3f) continue; // hors du cone avant
    e->st = 98; e->t = 0;
    e->vx = (dx + ex / d * 0.7f) * pf;
    e->vy = (dy + ey / d * 0.7f) * pf;
    poof(e->x, e->y, KDColor::RGB24(0x9AD8E8), 4);
  }
  // les projectiles renvoyes (vers le golem s'il est dans la salle)
  for (int b = 0; b < 6; b++) if (BU[b].on && !BU[b].fr) {
    float bx = BU[b].x - plx, by = BU[b].y - ply;
    float d2 = bx * bx + by * by;
    if (d2 < 1 || d2 > rr) continue;
    float d = sqrtf(d2);
    if ((bx * dx + by * dy) / d < 0.2f) continue;
    BU[b].fr = 1;
    int gi = -1;
    for (int i = 0; i < 14; i++) if (EN[i].alive && EN[i].type == 4) { gi = i; break; }
    if (gi >= 0) {
      float gx = EN[gi].x - BU[b].x, gy = EN[gi].y - BU[b].y;
      float gd = sqrtf(gx * gx + gy * gy); if (gd < 1) gd = 1;
      BU[b].vx = gx / gd * 2.6f; BU[b].vy = gy / gd * 2.6f;
    } else { BU[b].vx = dx * 2.6f; BU[b].vy = dy * 2.6f; }
    poof(BU[b].x, BU[b].y, KDColor::RGB24(0x9AD8E8), 5);
  }
  // le souffle brise les pots et fait ceder les rochers fissures
  float cx = plx + dx * 18, cy = ply + dy * 18;
  int stx = (int)(cx / TS), sty = (int)((cy - TOPOFF) / TS);
  if (stx >= 0 && stx < MW && sty >= 0 && sty < MH) {
    char tc = RMO[room][sty][stx];
    if (tc == 'P') {
      RMO[room][sty][stx] = isDun(room) ? ',' : '.';
      poof(cx, cy, KDColor::RGB24(0xC98050), 10);
      if (rnd(4) == 0) dropPickup(cx, cy, 0); else if (rnd(3) == 0) dropPickup(cx, cy, 1);
    } else if (tc == 'B') { // premiere fissure : le rocher craque
      RMO[room][sty][stx] = 'b';
      poof(cx, cy, C_ROCK2, 8); shake = 3;
    } else if (tc == 'b') { // il cede : le secret s'ouvre
      RMO[room][sty][stx] = isDun(room) ? ',' : '.';
      poof(cx, cy, C_ROCK, 14); shake = 6;
      if (rnd(3) == 0) dropPickup(cx, cy, 1);
    }
  }
  // trainee de vent visible devant l'esprit
  for (int k = 1; k <= 5; k++)
    poof(plx + dx * (8 + k * 6), ply + dy * (8 + k * 6), KDColor::RGB24(0x7AC8DC), 1);
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
  float dx = (pdir == 1) ? 1 : ((pdir == 3) ? -1 : 0);
  float dy = (pdir == 2) ? 1 : ((pdir == 0) ? -1 : 0);
  int bob = ((frame >> 3) & 1) ? 1 : 0;
  int hr = 7 + (((frame >> 3) & 1) ? 1 : 0);        // halo qui pulse
  fCirc(x, y, hr, KDColor::RGB24(0x142832));
  fCirc(x + 1, y + 8, 6, KDColor::RGB24(0x1E3040)); // ombre d'esprit
  if (pifr > 0 && (frame & 2)) return; // clignote (invincible)
  for (int k = 1; k <= 3; k++) // trainee de comete
    fCirc(x - dx * k * 5, y - dy * k * 5 - k, 4 - k, KDColor::RGB24(k == 1 ? 0x3A788C : ((k == 2) ? 0x1A4A5A : 0x143A48)));
  for (int k = 0; k < 2; k++) { // wisps en orbite
    int ax = x + (int)(cosf((frame + k * 90) * 0.09f) * 9.0f);
    int ay = y + (int)(sinf((frame + k * 90) * 0.09f) * 7.0f) - 2;
    px(ax, ay, C_WHT); px(ax, ay + 1, C_CYN);
  }
  fCirc(x, y - 2 + bob, 5, KDColor::RGB24(0x9AD8E8));               // corps : bulle de vent
  fRect(x - 5, y - 4 + bob, 10, 7, KDColor::RGB24(0x9AD8E8));
  px(x - 4, y + 3 + bob, KDColor::RGB24(0x5A98B0));                 // bas effiloche en volutes
  px(x - 2, y + 5 + bob, KDColor::RGB24(0x5A98B0));
  px(x + 2, y + 5 + bob, KDColor::RGB24(0x5A98B0));
  px(x + 4, y + 3 + bob, KDColor::RGB24(0x5A98B0));
  fRect(x - 3, y - 6 + bob, 6, 4, KDColor::RGB24(0xD4F0FC));       // visage lumineux
  int ex = (pdir == 1) ? 2 : ((pdir == 3) ? -2 : 0);
  if (pdir != 0) { px(x - 2 + ex, y - 5 + bob, KDColor::RGB24(0x2A6A8A)); px(x + 2 + ex, y - 5 + bob, KDColor::RGB24(0x2A6A8A)); } // yeux
  else { px(x - 2, y - 5 + bob, KDColor::RGB24(0x2A6A8A)); px(x + 2, y - 5 + bob, KDColor::RGB24(0x2A6A8A)); }
  px(x - 1, y + 3 + bob, KDColor::RGB24(0xD4F0FC)); px(x + 1, y + 4 + bob, KDColor::RGB24(0xD4F0FC)); // paillettes
  if (swing > 0) { // SOUFFLE : cone d'air en trois fronts + spirale
    int wp = (12 - swing) * 2;
    for (int k = 0; k < 5; k++) { // front principal
      int ax = x + (int)(dx * (wp + 6)) + (int)(-dy * (k - 2) * 3);
      int ay = y + (int)(dy * (wp + 6)) + (int)(dx * (k - 2) * 3);
      px(ax, ay, k == 2 ? C_WHT : C_CYN);
      px(ax + (int)dx, ay + (int)dy, C_WHT);
    }
    for (int f2 = 1; f2 <= 2; f2++) // fronts externes plus laches
      for (int k = 1; k < 5; k += 2) {
        int ax = x + (int)(dx * (wp + 6 + f2 * 3)) + (int)(-dy * (k - 2) * 4);
        int ay = y + (int)(dy * (wp + 6 + f2 * 3)) + (int)(dx * (k - 2) * 4);
        px(ax, ay, (f2 == 2) ? KDColor::RGB24(0x5A88A8) : C_CYN);
      }
    for (int k = 0; k < 3; k++) { // particules emportees en spirale
      int sp2 = (frame + k * 3) & 7;
      int ax = x + (int)(dx * (wp + 2 + sp2)) + (int)(-dy * (sp2 - 4));
      int ay = y + (int)(dy * (wp + 2 + sp2)) + (int)(dx * (sp2 - 4));
      px(ax, ay, k == 0 ? C_WHT : C_CYN);
    }
  }
}
static void drawNpc(int x, int y, int id) {
  KDColor robe = (id == 0) ? KDColor::RGB24(0x7A5AD4) : ((id == 1) ? C_MAG : ((id == 2) ? C_CYN : ((id == 4) ? C_ORG : C_DGRN)));
  KDColor trim = (id == 0) ? C_YEL : ((id == 1) ? C_WHT : ((id == 2) ? C_ORG : ((id == 4) ? C_YEL : C_GRASS)));
  int bob = ((frame >> 4) & 1) ? 1 : 0;
  int hh = (id == 2) ? 2 : 0; // le gamin est plus petit
  fCirc(x + 1, y + 8, 6, KDColor::RGB24(0x2A4A22)); // ombre
  fRect(x - 5, y - 5 + bob + hh, 10, 12 - hh, robe);
  fRect(x - 5, y + 5 - hh + bob, 10, 1, trim); // ourlet de robe
  fRect(x - 4, y - 7 + bob + hh, 8, 3, C_SKIN);
  px(x - 2, y - 6 + bob + hh, C_BLK); px(x + 2, y - 6 + bob + hh, C_BLK);
  if (id == 0) { // L'ANCIEN : capuche profonde, barbe de trois blancs, baton a gemme
    fRect(x - 5, y - 8 + bob, 10, 2, KDColor::RGB24(0x5A3AA4));
    fRect(x - 4, y - 6 + bob, 8, 1, KDColor::RGB24(0x3A2074)); // capuche qui ombre le front
    fRect(x - 4, y - 5 + bob, 8, 4, KDColor::RGB24(0xE8E8E8)); // barbe
    px(x - 3, y - 4 + bob, KDColor::RGB24(0xC8C8C8)); px(x + 3, y - 4 + bob, KDColor::RGB24(0xB8B8B8));
    px(x - 4, y - 3 + bob, KDColor::RGB24(0xD8D8D8)); px(x + 4, y - 3 + bob, KDColor::RGB24(0xC8C8C8)); // moustaches
    fRect(x + 6, y - 7, 2, 15, KDColor::RGB24(0x6A4A2A)); // baton noueux
    px(x + 6, y - 1, KDColor::RGB24(0x523A20)); px(x + 7, y + 3, KDColor::RGB24(0x523A20));
    px(x + 7, y - 8, C_GOLD); px(x + 6, y - 9, C_YEL); // gemme qui veille
  } else if (id == 1) { // LA MARCHANDE : chapeau a plume, ceinture doree, bourse
    fRect(x - 7, y - 9 + bob, 14, 2, KDColor::RGB24(0x8A2A4E)); // large bord
    fRect(x - 3, y - 11 + bob, 6, 3, KDColor::RGB24(0x9A3A5E));
    px(x + 4, y - 11 + bob, C_WHT); px(x + 5, y - 12 + bob, C_WHT); // plume
    fRect(x - 5, y + 1 + bob, 10, 1, C_GOLD); // ceinture
    fRect(x + 4, y + 1 + bob, 3, 4, KDColor::RGB24(0x6A4A2A)); // bourse pendue
    px(x + 5, y + 4 + bob, C_GOLD);
  } else if (id == 2) { // LE GAMIN : cheveux en bataille, joues roses, bras au ciel
    fRect(x - 4, y - 8 + bob + hh, 8, 2, KDColor::RGB24(0xC87830)); // touffe
    px(x - 5, y - 7 + bob + hh, KDColor::RGB24(0xC87830)); px(x + 4, y - 7 + bob + hh, KDColor::RGB24(0xC87830));
    px(x - 4, y - 5 + bob + hh, KDColor::RGB24(0xF09878)); px(x + 3, y - 5 + bob + hh, KDColor::RGB24(0xF09878)); // joues
    px(x - 7, y - 3 + bob + hh, C_SKIN); px(x + 6, y - 3 + bob + hh, C_SKIN); // bras leves
    px(x - 7, y - 4 + bob + hh, C_SKIN); px(x + 6, y - 4 + bob + hh, C_SKIN);
  } else if (id == 3) { // L'ERMITE : capuche basse, baton, lanterne sourde
    fRect(x - 5, y - 9 + bob, 10, 3, C_DGRN);
    fRect(x - 4, y - 7 + bob, 8, 2, KDColor::RGB24(0x143A18)); // visage dans l'ombre
    px(x - 2, y - 6 + bob, C_YEL); px(x + 2, y - 6 + bob, C_YEL); // yeux brillants
    fRect(x + 6, y - 6, 2, 14, KDColor::RGB24(0x6A4A2A)); // baton
    px(x + 7, y - 6, KDColor::RGB24(0x523A20));
    px(x + 5, y + 2 + bob, KDColor::RGB24(0x8A6A3A)); px(x + 7, y + 2 + bob, KDColor::RGB24(0x8A6A3A)); // potence
    px(x + 6, y + 4 + bob, ((frame >> 4) & 1) ? C_YEL : C_ORG); // flamme de la lanterne
  } else if (id == 4) { // LA CONTEUSE : fichu noue, livre ouvert qui tourne ses pages
    fRect(x - 5, y - 9 + bob, 10, 3, KDColor::RGB24(0xC87830));
    px(x - 5, y - 6 + bob, KDColor::RGB24(0xC87830)); px(x + 4, y - 6 + bob, KDColor::RGB24(0xC87830)); // noeud du fichu
    fRect(x + 3, y + 1, 6, 5, KDColor::RGB24(0xE8E0C0)); // livre ouvert
    fRect(x + 5, y + 1, 1, 5, KDColor::RGB24(0xC8B890)); // reliure
    px(x + 4, y + 2, KDColor::RGB24(0x8A7A5A)); px(x + 4, y + 3, KDColor::RGB24(0x8A7A5A));
    px(x + 6, y + 2, KDColor::RGB24(0x8A7A5A)); px(x + 6, y + 3, KDColor::RGB24(0x8A7A5A));
    if (((frame >> 5) & 1)) px(x + 4, y + 4, KDColor::RGB24(0x8A7A5A)); // page qui tourne
  }
}
static void drawEnemies(int ox, int oy) {
  for (int i = 0; i < 14; i++) {
    if (!EN[i].alive) continue;
    struct En *e = &EN[i];
    int x = (int)e->x + ox, y = (int)e->y + oy;
    if (e->flash) continue; // flash blanc : invisible un frame sur deux
    if (e->st == 98) { // emporte par le vent : trainee cyan
      px(x - 4, y - 2, C_CYN); px(x - 6, y, C_CYN); px(x - 3, y + 2, C_CYN);
    }
    if (e->type == 0) { // limace : corps gele
      int sq = (e->t >> 3) & 1;
      fCirc(x + 1, y + 4, 5, KDColor::RGB24(0x1E4A18)); // ombre
      fRect(x - 5, y - 3 + sq, 10, 6 - sq, C_GREEN);
      fRect(x - 5, y + 1 - sq, 10, 1, C_DGRN);
      fRect(x - 3, y - 3 + sq, 4, 1, KDColor::RGB24(0x7AE864)); // reflet
      px(x - 2, y - 1, C_BLK); px(x + 2, y - 1, C_BLK);
      px(x + 1, y + 2, KDColor::RGB24(0x7AE864));
    } else if (e->type == 1) { // chauve-souris : oreilles + crocs
      int fl = (e->t >> 2) & 1;
      fRect(x - 2, y - 2, 4, 4, KDColor::RGB24(0x6A3AA0));
      px(x - 1, y - 3, KDColor::RGB24(0x6A3AA0)); px(x + 1, y - 3, KDColor::RGB24(0x6A3AA0));
      if (fl) { fRect(x - 7, y - 4, 5, 2, KDColor::RGB24(0x8A52C4)); fRect(x + 2, y - 4, 5, 2, KDColor::RGB24(0x8A52C4)); }
      else { fRect(x - 7, y - 1, 5, 2, KDColor::RGB24(0x8A52C4)); fRect(x + 2, y - 1, 5, 2, KDColor::RGB24(0x8A52C4)); }
      px(x - 1, y - 1, C_RED); px(x + 1, y - 1, C_RED);
      px(x - 1, y + 1, C_WHT); px(x + 1, y + 1, C_WHT);
    } else if (e->type == 2) { // squelette : orbites + cotes
      int wb = ((e->t >> 3) & 1) ? 1 : 0;
      fCirc(x + 1, y + 8, 5, KDColor::RGB24(0x2A4A22)); // ombre
      fRect(x - 4, y - 7, 8, 6, C_WHT);                // crane
      fRect(x - 3, y - 6, 2, 2, C_BLK); fRect(x + 1, y - 6, 2, 2, C_BLK); // orbites
      px(x - 1, y - 3, C_BLK); px(x + 1, y - 3, C_BLK);
      fRect(x - 3, y - 1, 6, 7, KDColor::RGB24(0xD8D8C8)); // corps
      fRect(x - 2, y, 4, 1, C_BLK); fRect(x - 2, y + 2, 4, 1, C_BLK);     // cotes
      fRect(x - 5, y - 1 + wb, 2, 5, C_WHT); fRect(x + 3, y - 1 - wb, 2, 5, C_WHT); // bras
      fRect(x - 4, y + 6 + wb, 3, 1, C_WHT); fRect(x + 1, y + 6 - wb, 3, 1, C_WHT); // pieds
    } else if (e->type == 3) { // tourelle : oeil vivant qui vise
      fCirc(x + 1, y + 6, 5, KDColor::RGB24(0x2E2E38)); // ombre
      fCirc(x, y, 6, C_WALL2);
      fCirc(x, y, 4, KDColor::RGB24(0x2E2E38));
      float dx = plx - e->x, dy = ply - e->y;
      float d = sqrtf(dx * dx + dy * dy); if (d < 1) d = 1;
      int ix = (int)(dx / d * 2.0f), iy = (int)(dy / d * 2.0f);
      fCirc(x + ix, y + iy, 2, C_PUR);
      px(x + ix, y + iy, ((e->t >> 3) & 1) ? C_WHT : C_YEL); // pupille qui pulse
    } else if (e->type == 4) { // LE GOLEM : colosse de strates, fissure a mesure qu'il souffre
      int hop = (e->st >= 10 && e->st < 34) ? ((e->st < 20) ? -(e->st - 10) : -8 + (e->st - 20) / 2) : 0;
      fCirc(x + 1, y + 13, 13, KDColor::RGB24(0x2E2E38)); // grosse ombre
      fRect(x - 9, y + 4 + hop, 6, 9, C_ROCK2); fRect(x + 3, y + 4 + hop, 6, 9, C_ROCK2); // jambes
      fRect(x - 9, y + 11 + hop, 6, 2, C_ROCK); fRect(x + 3, y + 11 + hop, 6, 2, C_ROCK); // pieds
      fRect(x - 12, y - 14 + hop, 24, 20, C_ROCK2);      // torse monumental
      fRect(x - 12, y - 14 + hop, 24, 2, KDColor::RGB24(0xB0B0B0)); // epaules eclairees
      fRect(x - 10, y - 12 + hop, 20, 1, C_ROCK);        // strates de granit
      fRect(x - 10, y - 7 + hop, 20, 1, C_ROCK);
      fRect(x - 10, y - 2 + hop, 20, 1, C_ROCK);
      px(x - 7, y - 10 + hop, C_ROCK); px(x + 6, y - 9 + hop, C_ROCK);
      px(x + 8, y + 0 + hop, KDColor::RGB24(0x3A3A44)); px(x - 5, y + 1 + hop, KDColor::RGB24(0x3A3A44));
      px(x - 11, y - 13 + hop, KDColor::RGB24(0x3A5A34)); px(x + 9, y - 13 + hop, KDColor::RGB24(0x3A5A34)); // mousse sur les epaules
      fRect(x - 8, y - 12 + hop, 16, 2, C_ROCK);         // arcade sourcilleuse
      int gl = ((e->t >> 3) & 1) ? C_RED : C_ORG;
      fRect(x - 6, y - 9 + hop, 4, 3, gl); fRect(x + 2, y - 9 + hop, 4, 3, gl); // yeux ardents
      px(x - 4, y - 8 + hop, C_YEL); px(x + 3, y - 8 + hop, C_YEL);    // pupilles braises
      if (e->st >= 10 && e->st < 34) { fRect(x - 6, y - 9 + hop, 4, 2, C_YEL); fRect(x + 2, y - 9 + hop, 4, 2, C_YEL); } // rage du saut
      fRect(x - 3, y - 3 + hop, 6, 2, C_ROCK); px(x - 2, y - 2 + hop, C_ROCK2); px(x + 2, y - 2 + hop, C_ROCK2); // machoire
      int aw = (e->st == 34) ? 4 : 0; // les bras s'abattent au slam
      fRect(x - 16, y - 8 + hop + aw, 5, 13, C_ROCK2); fRect(x + 11, y - 8 + hop + aw, 5, 13, C_ROCK2);
      fRect(x - 16, y - 8 + hop + aw, 5, 1, C_ROCK); fRect(x + 11, y - 8 + hop + aw, 5, 1, C_ROCK);
      fRect(x - 16, y + 5 + hop + aw, 5, 3, C_ROCK); fRect(x + 11, y + 5 + hop + aw, 5, 3, C_ROCK); // poings
      px(x - 14, y + 6 + hop + aw, KDColor::RGB24(0x3A3A44)); px(x + 13, y + 6 + hop + aw, KDColor::RGB24(0x3A3A44)); // phalanges
      if (aw && ((frame >> 2) & 1)) { fRect(x - 16, y + 8 + hop, 5, 1, C_YEL); fRect(x + 11, y + 8 + hop, 5, 1, C_YEL); } // impact du slam
      if (e->hp <= 13) { px(x - 5, y + 3 + hop, C_BLK); px(x - 4, y + 4 + hop, C_BLK); px(x + 5, y + 1 + hop, C_BLK); }
      if (e->hp <= 9) { fRect(x - 9, y - 10 + hop, 1, 6, C_BLK); px(x - 8, y - 5 + hop, C_BLK); fRect(x + 7, y - 4 + hop, 1, 7, C_BLK); px(x - 7, y + 8 + hop, C_BLK); px(x + 6, y + 9 + hop, C_BLK); }
      if (e->hp <= 5) { fRect(x - 12, y - 3 + hop, 8, 1, C_BLK); px(x - 4, y - 2 + hop, C_BLK); px(x - 3, y - 1 + hop, C_BLK); fRect(x + 9, y - 12 + hop, 1, 5, C_BLK); fRect(x + 8, y + 2 + hop, 3, 1, C_BLK); }
      if (e->hp <= 3) { fRect(x - 2, y - 10 + hop, 5, 1, C_BLK); fRect(x + 7, y + 6 + hop, 1, 5, C_BLK); px(x - 1, y - 7 + hop, C_BLK); px(x + 1, y - 7 + hop, C_BLK); }
      if (e->hp <= 5 && ((frame >> 3) & 1)) { px(x - 6, y - 4 + hop, C_RED); px(x + 6, y + 0 + hop, C_RED); px(x, y - 7 + hop, C_RED); px(x + 7, y + 8 + hop, C_ORG); } // veines de magma qui percent
    }
  }
  for (int b = 0; b < 6; b++) if (BU[b].on) { // rochers ardents en rotation
    int bx = (int)BU[b].x + ox, by = (int)BU[b].y + oy;
    int rr = (frame >> 2) & 3;
    fCirc(bx, by, 4, KDColor::RGB24(0x3A3A44));          // rocher brut
    fCirc(bx - 1, by - 1, 2, C_ROCK);                     // facette eclairee
    px(bx - 2, by + 1, C_ROCK2);
    fCirc(bx, by, 3, ((frame >> 3) & 1) ? C_ORG : C_RED);  // coeur ardent qui perce
    if (BU[b].fr) fCirc(bx, by, 2, C_YEL);                // renvoye par le souffle : coeur blanc
    px(bx - (int)(BU[b].vx * 2), by - (int)(BU[b].vy * 2), C_ORG);  // trainee de flammes en trois crans
    px(bx - (int)(BU[b].vx * 4), by - (int)(BU[b].vy * 4), C_RED);
    px(bx - (int)(BU[b].vx * 6), by - (int)(BU[b].vy * 6), ((frame >> 2) & 1) ? C_ORG : KDColor::RGB24(0x8A3A18));
    if (rr == 0) px(bx + 3, by - 3, C_YEL);                // etincelle arrachee
  }
  for (int i = 0; i < 8; i++) if (PK[i].on) {
    int x = (int)PK[i].x + ox, y = (int)PK[i].y + oy + (((frame >> 3) & 1) ? 1 : 0);
    if (PK[i].t == 0) drawHeartIcon(x - 4, y - 4, true);
    else if (PK[i].t == 1) { // piece qui brille
      int sp = (frame >> 3) & 3;
      fRect(x - 3, y - 3, 6, 6, C_GOLD);
      fRect(x - 1 - (sp >> 1), y - 2, 2, 4, KDColor::RGB24(0xB8962A));
      if (sp == 0) px(x - 1, y - 1, C_WHT);
      if (sp == 2) px(x - 3, y - 3, C_WHT);
    } else { // L'ETOILE : grosse etoile scintillante
      int r = 7 + (((frame >> 2) & 1) ? 1 : 0);
      fCirc(x + 1, y + 3, 7, KDColor::RGB24(0x6A5A18)); // halo au sol
      fRect(x - 1, y - r, 2, r * 2, C_YEL);
      fRect(x - r, y - 1, r * 2, 2, C_YEL);
      fRect(x - 3, y - 3, 6, 6, C_WHT);
      px(x - 1, y - 1, C_YEL);
      poof(x, y, C_YEL, 1);
    }
  }
}

// ---------------- HUD ----------------
static void drawHud() {
  fRect(0, 0, SW, TOPOFF, KDColor::RGB24(0x141824));
  fRect(0, TOPOFF - 2, SW, 2, KDColor::RGB24(0x2A3048));
  fRect(0, 2, SW, 1, KDColor::RGB24(0x0C0E16));
  fRect(0, 0, SW, 8, KDColor::RGB24(0x202840));
  fRect(0, 8, SW, 1, KDColor::RGB24(0x2C3448));
  int nh = (pmax + 1) / 2;
  for (int i = 0; i < nh; i++) drawHeartIcon(10 + i * 11, 8, php >= (i + 1) * 2); // coeur plein si assez de PV
  // jauge de SOUFFLE : 5 rafales, 7 avec la fiole d'air
  int bm = hasFlask ? 7 : 5;
  for (int i = 0; i < bm; i++) {
    int bx2 = 10 + i * 7;
    KDColor bc = (i < brth) ? C_CYN : KDColor::RGB24(0x22364A);
    px(bx2, 28, bc); px(bx2 + 1, 27, bc); px(bx2 + 2, 26, bc); px(bx2 + 3, 25, bc);
  }
  // tresors secrets possedes
  if (hasFlask) { fRect(206, 23, 3, 5, C_CYN); fRect(207, 21, 1, 2, C_CYN); px(206, 27, C_WHT); }
  if (hasEcho) { fCirc(216, 25, 3, C_PUR); px(216, 25, C_WHT); }
  if (hasPlume) { px(226, 27, C_WHT); px(227, 26, C_WHT); px(228, 25, C_WHT); px(229, 24, C_WHT); px(229, 23, C_WHT); px(228, 22, C_WHT); }
  // pieces qui brillent
  int sp = (frame >> 3) & 3;
  fRect(148, 10, 7, 7, C_GOLD); fRect(150, 11, 3, 5, KDColor::RGB24(0xB8962A));
  if (sp == 0) px(149, 11, C_WHT);
  char cb[8]; snprintf(cb, 8, "%d", pcoin);
  drawTextSmall(162, 10, cb, C_WHT);
  // cle
  if (pkey > 0) {
    fRect(206, 9, 7, 2, C_GOLD); fRect(206, 11, 2, 2, C_GOLD); fRect(208, 12, 2, 5, C_GOLD); px(210, 14, C_GOLD);
    char kb[8]; snprintf(kb, 8, "X%d", pkey);
    drawTextSmall(220, 10, kb, C_WHT);
  }
  // etoile
  if (hasStar) {
    int r = 5 + (((frame >> 3) & 1) ? 1 : 0);
    int sx = 244, sy = 13;
    fRect(sx - 1, sy - r, 2, r * 2, C_YEL); fRect(sx - r, sy - 1, r * 2, 2, C_YEL);
    fRect(sx - 2, sy - 2, 4, 4, C_WHT);
    drawTextSmall(258, 10, "ETOILE", C_YEL);
  }
  // MINI-CARTE : les 18 salles, seulement les visitees
  fRect(268, 6, 32, 22, KDColor::RGB24(0x0C0E16));
  for (int r = 0; r < 3; r++)
    for (int c2 = 0; c2 < 6; c2++) {
      int id = r * 6 + c2;
      int mx = 270 + c2 * 5, my = 8 + r * 6;
      if (id == room) fRect(mx - 1, my - 1, 5, 5, ((frame >> 4) & 1) ? C_YEL : C_WHT); // salle courante
      else if (visited[id]) fRect(mx, my, 3, 3, ((id == 4 || id == 5 || id >= 10) ? C_PUR : KDColor::RGB24(0x6A7590)));
      else fRect(mx, my, 3, 3, KDColor::RGB24(0x22283A));
    }
  // barre de vie du GOLEM
  if (room == 11 && !bossDead && EN[0].alive && EN[0].type == 4) {
    fRect(88, 29, 144, 10, KDColor::RGB24(0x0C0E16));
    fRect(90, 30, 140, 8, C_BLK);
    fRect(90, 30, 140, 1, KDColor::RGB24(0x3A1A1A));
    fRect(92, 32, (int)(136.0f * EN[0].hp / 16.0f), 4, C_RED);
    fRect(92, 32, (int)(136.0f * EN[0].hp / 16.0f), 1, KDColor::RGB24(0xF08080));
  }
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
    int k = CH[room] ? CH[room] : SCH[room]; // coffre d'histoire ou d'alcove secrete
    if (k == 2) { pkey++; startDlg(DLG_CLE, 1); }
    else if (k == 3) { pmax += 2; php = pmax; startDlg(DLG_COEUR, 1); }
    else if (k == 4) { hasFlask = true; startDlg(DLG_FIOLE, 1); }
    else if (k == 5) { hasEcho = true; startDlg(DLG_ECHO, 1); }
    else if (k == 6) { hasPlume = true; startDlg(DLG_PLUME, 1); }
    else { pcoin += 20; startDlg(DLG_PIECES, 1); }
    return true;
  }
  // stele de lore ?
  if (c == 'L') {
    if (room == 3) startDlg(DLG_STELE3, 3);
    else if (room == 7) startDlg(DLG_STELE7, 4);
    else if (room == 10) startDlg(DLG_STELE10, 3);
    else startDlg(DLG_STELE11, 4);
    return true;
  }
  // PNJ ?
  for (int i = 0; NPC[i][0] != 255; i++) {
    if (NPC[i][0] != room || NPC[i][1] != fx || NPC[i][2] != fy) continue;
    int id = NPC[i][3];
    if (id == 0) { // L'ANCIEN
      if (hasStar && !gameDone) { dlgEnd = true; startDlg(ENDING, 8); mode = 4; }
      else if (visited[4] || visited[5] || visited[10] || visited[11] || pkey > 0) startDlg(DLG_ANCIEN2, 3);
      else startDlg(DLG_ANCIEN, 5);
    } else if (id == 1) { // LA MARCHANDE
      if (hasStar) startDlg(DLG_MARQ2, 2);
      else if (php >= pmax) startDlg(DLG_MARQ, 2);
      else if (pcoin >= 20) { pcoin -= 20; php = pmax; poof(plx, ply - 10, C_RED, 10); startDlg(DLG_MARQ, 2); }
      else startDlg(DLG_MARQ, 1);
    } else if (id == 2) { // LE GAMIN
      if (bossDead || hasStar) startDlg(DLG_GAMIN2, 2);
      else startDlg(DLG_GAMIN, 3);
    } else if (id == 4) { // LA CONTEUSE
      if (hasFlask || hasEcho || hasPlume) startDlg(DLG_CONTEUSE2, 4);
      else startDlg(DLG_CONTEUSE, 4);
    } else { // L'ERMITE
      if (bossDead) startDlg(DLG_ERMITE2, 3);
      else startDlg(DLG_ERMITE, 4);
    }
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
  pmov = (dx != 0 || dy != 0);
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
  if (room != prevRoom) { visited[room] = 1; fadePos = 0; waitRelease(); return; }
  // ----- action : le SOUFFLE -----
  if (swing > 0) swing--;
  if (brth < (hasFlask ? 7 : 5) && ++brthT >= 26) { brthT = 0; brth++; }
  if (keyOK) {
    if (!tryInteract()) {
      if (brth > 0) { brth--; blow(); }
      waitRelease();
    }
  }
  updEnemies();
  if (pifr > 0) pifr--;
  // ----- ramassage -----
  for (int i = 0; i < 8; i++) if (PK[i].on) {
    if (PK[i].x > plx - 8 && PK[i].x < plx + 8 && PK[i].y > ply - 8 && PK[i].y < ply + 8) {
      if (PK[i].t == 0) { if (php < pmax) php += 2; if (php > pmax) php = pmax; }
      else if (PK[i].t == 1) pcoin += 5;
      else { hasStar = true; startDlg(DLG_ETOILE, 3); }
      PK[i].on = false;
    }
  }
}

static void drawRoom(int ox, int oy) {
  for (int ty = 0; ty < MH; ty++)
    for (int tx = 0; tx < MW; tx++)
      drawTile(tx, ty, ox, oy);
  if (isDun(room)) { // donjon : halos de torches + poussieres en suspension
    for (int ty = 0; ty < MH; ty++)
      for (int tx = 0; tx < MW; tx++)
        if (RMO[room][ty][tx] == 'F') {
          int lx = tx * TS + 8 + ox, ly = TOPOFF + ty * TS + 8 + oy;
          for (int a = 2; a < 22; a += 2) {
            px(lx + a, ly + (frame & 3), KDColor::RGB24(0x5A3A18));
            px(lx - a, ly - (frame & 3), KDColor::RGB24(0x5A3A18));
            px(lx + (frame & 3), ly - a, KDColor::RGB24(0x5A3A18));
            px(lx - (frame & 3), ly + a, KDColor::RGB24(0x5A3A18));
          }
        }
    for (int i = 0; i < 5; i++) { // poussieres qui flottent dans la lumiere
      int lx = ((i * 61 + frame) % (SW - 20)) + 10 + ox;
      int ly = TOPOFF + ((i * 43 + (frame >> 1) * ((i & 1) ? 1 : -1)) % (MH * TS - 8)) + 4 + oy;
      px(lx, ly, KDColor::RGB24(0x8A7A5A));
    }
  } else { // surface : feuilles au vent + lucioles
    for (int i = 0; i < 6; i++) {
      int lx = ((i * 67 + frame * 3) % (SW + 40)) - 20 + ox;
      int ly = ((i * 91 + frame * 2) % (MH * TS)) + TOPOFF + oy;
      px(lx, ly, C_DGRN); px(lx + 1, ly + 1, C_GRAS2);
    }
    for (int i = 0; i < 4; i++) { // lucioles scintillantes
      int fx2 = 20 + ((i * 77 + (int)(sinf(frame * 0.03f + i) * 30.0f)) % (SW - 40)) + ox;
      int fy2 = TOPOFF + 10 + ((i * 53 + (int)(cosf(frame * 0.02f + i * 2.0f) * 20.0f) + 60) % (MH * TS - 20)) + oy;
      if (((frame >> 4) + i) & 3) { px(fx2, fy2, KDColor::RGB24(0x6A5A18)); px(fx2, fy2 - 1, C_YEL); }
      else px(fx2, fy2, C_YEL);
    }
  }
  if (room == 11 && !bossDead) { // LE CERCLE D'INVOCATION sous le golem
    int cx2 = 160 + ox, cy2 = TOPOFF + 96 + oy;
    for (int a = 0; a < 32; a++) {
      float ang = a * 0.19635f + (((frame >> 5) & 1) ? 0.098175f : 0);
      px(cx2 + (int)(cosf(ang) * 46.0f), cy2 + (int)(sinf(ang) * 30.0f), ((a + (frame >> 3)) & 3) ? C_PUR : C_MAG);
      px(cx2 + (int)(cosf(ang) * 30.0f), cy2 + (int)(sinf(ang) * 19.0f), KDColor::RGB24(0x4A2A6A));
    }
  }
  for (int i = 0; NPC[i][0] != 255; i++)
    if (NPC[i][0] == room) drawNpc(NPC[i][1] * TS + 8 + ox, TOPOFF + NPC[i][2] * TS + 8 + oy, NPC[i][3]);
  drawEnemies(ox, oy);
  drawPlayer(ox, oy);
  updParticles();
  drawHud();
  if (dlgOn) drawDlg();
}

// ---------------- Ecrans ----------------
static void starfield(int sc) { // etoiles sans fond : le ciel est deja peint
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
    if (keyOK) { titleSc = sc; waitRelease(); return; }
    // ciel en degrade : 15 bandes du violet profond au noir
    static const uint32_t SKYB[15] = {0x1B2238, 0x181F33, 0x151C2E, 0x121929, 0x101626, 0x0E1424, 0x0C1220, 0x0A101C, 0x080E18, 0x070C14, 0x060A10, 0x05080E, 0x04060C, 0x04060C, 0x03050A};
    for (int b = 0; b < 15; b++) fRect(0, b * 8, SW, 8, KDColor::RGB24(SKYB[b]));
    starfield(sc++);
    fCirc(60, 40, 17, KDColor::RGB24(0x2A2A20)); // halo lunaire
    fCirc(60, 40, 12, KDColor::RGB24(0xE8E4D0)); // la lune
    fCirc(55, 36, 3, KDColor::RGB24(0xC8C4B0)); fCirc(64, 44, 2, KDColor::RGB24(0xC8C4B0)); px(58, 42, KDColor::RGB24(0xC8C4B0)); // crateres
    // montagnes en silhouettes
    for (int i = 0; i < 16; i++) fRect(160 - i * 8, 208 - i * 8, i * 16, 8, KDColor::RGB24(0x101828));
    for (int i = 0; i < 10; i++) { fRect(48 - i * 6, 208 - i * 6, i * 12, 8, KDColor::RGB24(0x0E1424)); fRect(252 - i * 6, 208 - i * 6, i * 12, 8, KDColor::RGB24(0x0E1424)); }
    fRect(0, 196, SW, 2, KDColor::RGB24(0x2A3048)); fRect(0, 201, SW, 1, KDColor::RGB24(0x22283A)); // brume
    // sol et collines au fond
    fRect(0, 224, SW, 16, KDColor::RGB24(0x0E1424));
    fRect(0, 222, SW, 2, KDColor::RGB24(0x161E30));
    // LE CHATEAU : tours, creneaux, ogives, herse, bannieres, douves
    fRect(112, 44, 96, 40, KDColor::RGB24(0x101626));
    fRect(104, 38, 12, 46, KDColor::RGB24(0x101626));
    fRect(204, 38, 12, 46, KDColor::RGB24(0x101626));
    fRect(112, 44, 96, 1, KDColor::RGB24(0x1E2638)); // arete du corps eclairee
    fRect(103, 38, 1, 46, KDColor::RGB24(0x1E2638)); fRect(215, 38, 1, 46, KDColor::RGB24(0x1E2638)); // flancs des tours
    for (int i = 0; i < 4; i++) { fRect(106 + i * 3, 34, 2, 4, KDColor::RGB24(0x101626)); fRect(206 + i * 3, 34, 2, 4, KDColor::RGB24(0x101626)); }
    for (int i = 0; i < 7; i++) fRect(114 + i * 14, 38, 8, 6, KDColor::RGB24(0x101626));
    for (int i = 0; i < 3; i++) { // fenetres ogivales qui s'allument une a une
      int wx2 = 126 + i * 32;
      fRect(wx2, 50, 4, 7, ((sc >> (4 + i)) & 1) ? C_YEL : KDColor::RGB24(0x2A3048));
      px(wx2 - 1, 49, KDColor::RGB24(0x2A3048)); px(wx2 + 4, 49, KDColor::RGB24(0x2A3048)); // pointes des ogives
    }
    fRect(148, 70, 24, 14, KDColor::RGB24(0x0A0E1A)); // porte monumentale
    fRect(146, 68, 28, 2, KDColor::RGB24(0x0A0E1A));
    px(147, 68, KDColor::RGB24(0x0A0E1A)); px(172, 68, KDColor::RGB24(0x0A0E1A)); // retombees de l'arche
    for (int i = 0; i < 3; i++) fRect(152 + i * 8, 70, 1, 12, KDColor::RGB24(0x1A1E2C)); // barreaux de la herse
    fRect(148, 76, 24, 1, KDColor::RGB24(0x1A1E2C));
    if (sc & 16) px(160, 78, C_YEL); // quelqu'un veille derriere la herse
    int bw2 = (sc >> 3) & 1; // bannieres qui ondulent au vent
    fRect(107, 48, 3, 10, C_DRED); fRect(110, 49 + bw2, 2, 7, C_DRED); px(112, 51 + bw2, C_GOLD);
    fRect(210, 48, 3, 10, C_DRED); fRect(207, 49 - bw2, 2, 7, C_DRED); px(205, 51 - bw2, C_GOLD);
    fRect(156, 50, 8, 8, KDColor::RGB24(0x0A0E1A)); fRect(158, 48, 4, 2, C_DRED); // banniere du donjon central
    fRect(104, 84, 112, 4, KDColor::RGB24(0x0A1420)); // douves au pied du chateau
    for (int i = 0; i < 14; i++) px(107 + i * 8 + ((sc >> 4) & 3), 85 + (i & 1), KDColor::RGB24(0x24344C)); // reflets de l'eau
    fRect(150, 84, 20, 4, KDColor::RGB24(0x4A3A28)); // pont d'acces
    fRect(154, 86, 2, 2, KDColor::RGB24(0x6A5638)); fRect(164, 86, 2, 2, KDColor::RGB24(0x6A5638)); // planches
    fCirc(151, 31, 6, KDColor::RGB24(0x322A08)); // halo de l'etoile
    fRect(148, 26, 6, 10, C_YEL); // l'etoile au-dessus du royaume
    if (sc & 8) { px(150, 26, C_WHT); px(148, 28, C_WHT); px(153, 33, C_WHT); }
    if ((sc >> 4) & 1) px(151, 24, C_YEL); // rayon qui pulse
    if ((sc % 90) > 80) { // etoile filante
      int sx2 = 300 - (sc % 90 - 80) * 30, sy2 = 10 + (sc % 90 - 80) * 8;
      px(sx2, sy2, C_WHT); px(sx2 + 3, sy2 - 1, KDColor::RGB24(0x6A7590)); px(sx2 + 6, sy2 - 2, KDColor::RGB24(0x2A3048));
    }
    drawTextBig(65, 101, "LE ROYAUME", KDColor::RGB24(0x3A2A08)); // ombre doree
    drawTextBig(75, 119, "OUBLIE", KDColor::RGB24(0x101626)); // ombre de nuit
    drawTextBig(64, 100, "LE ROYAUME", C_YEL);
    drawTextBig(74, 118, "OUBLIE", C_WHT);
    drawTextC(150, "TU ES LE VENT. TU NE TOUCHES RIEN.", C_GRE);
    if ((sc >> 3) & 1) drawTextC(170, "APPUIE SUR OK", C_WHT);
    drawTextC(196, "FLECHES : BOUGER   OK : SOUFFLE", KDColor::RGB24(0x6A7590));
    drawTextC(208, "REPOUSSE TOUT - RENVOIE LES ROCHERS", KDColor::RGB24(0x6A7590));
    blit(); Ion::Timing::msleep(40);
  }
}
static void pagesScreen(const char *pg[][3], int n, const char *label) { // pages d'histoire
  waitRelease();
  int i = 0;
  while (true) {
    pollKeys();
    if (keyOK) { waitRelease(); i++; if (i >= n) return; }
    fRect(0, 0, SW, SH, KDColor::RGB24(0x0A0E1A));
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

// ---------------- Generation procedurale des salles ----------------
// Chaque partie cree un monde inedit, mais par ZONE : foret au nord,
// plaines au centre, pres aux lacs au sud, donjon de pierre au nord-est.
static void genRoom(int r) {
  char (*M)[21] = RMO[r];
  bool dun = isDun(r);
  int zone = r / 6;
  char base = dun ? ',' : '.';
  char wall = dun ? '#' : 't';
  int g = GAT[r];
  for (int ty = 0; ty < MH; ty++)
    for (int tx = 0; tx < MW; tx++)
      M[ty][tx] = (tx == 0 || tx == MW - 1 || ty == 0 || ty == MH - 1) ? wall : base;
  // decoration eparsse propre a la zone
  for (int i = 0; i < 30; i++) {
    int tx = 1 + rnd(MW - 2), ty = 1 + rnd(MH - 2);
    if (M[ty][tx] != base) continue;
    int f = rnd(10);
    if (dun) {
      if (f < 4) { M[ty][tx] = '#'; if (tx + 1 < MW - 1 && M[ty][tx + 1] == base) M[ty][tx + 1] = '#'; }
      else if (f < 6) M[ty][tx] = 'P';
    } else if (zone == 0) { // foret densee
      if (f < 5) M[ty][tx] = 't';
      else if (f < 7 && ty + 1 < MH - 1 && M[ty + 1][tx] == base) { M[ty][tx] = 't'; M[ty + 1][tx] = 't'; }
      else if (f < 8) M[ty][tx] = 'f';
      else if (f < 9) M[ty][tx] = 'r';
    } else if (zone == 1) { // plaines fleuries
      if (f < 2) M[ty][tx] = 't';
      else if (f < 6) M[ty][tx] = 'f';
      else if (f < 7) M[ty][tx] = 'r';
      else if (f < 8) M[ty][tx] = 'P';
    } else { // pres du sud : flaques
      if (f < 4) {
        M[ty][tx] = 'w';
        if (tx + 1 < MW - 1 && M[ty][tx + 1] == base) M[ty][tx + 1] = 'w';
        if (ty + 1 < MH - 1 && M[ty + 1][tx] == base) M[ty + 1][tx] = 'w';
      } else if (f < 6) M[ty][tx] = 'f';
      else if (f < 8) M[ty][tx] = 'r';
      else if (f < 9) M[ty][tx] = 'P';
    }
  }
  // croisement garanti : toutes les ouvertures restent reliees
  for (int ty = 1; ty < MH - 1; ty++) { M[ty][9] = base; M[ty][10] = base; }
  for (int tx = 1; tx < MW - 1; tx++) { M[5][tx] = base; M[6][tx] = base; }
  if (g & 1) { M[0][9] = base; M[0][10] = base; }
  if (g & 2) { M[11][9] = base; M[11][10] = base; }
  if (g & 4) { M[5][19] = base; M[6][19] = base; }
  if (g & 8) { M[5][0] = base; M[6][0] = base; }
  if (r == 5) { M[11][9] = 'D'; M[11][10] = 'D'; } // la porte scellee du boss
  if (dun) { // torches sur le mur du haut
    M[0][2 + rnd(7)] = 'F';
    M[0][12 + rnd(6)] = 'F';
  }
  // coffre de l'histoire : pose pres des couloirs sur
  if (CH[r] > 0) {
    int sp[6][2] = { {9,4},{10,4},{9,7},{10,7},{4,4},{15,7} };
    int p = rnd(6);
    M[sp[p][1]][sp[p][0]] = 'C';
  }
  // ALCOVE SECRETE : poche mursee, entree de rocher fissure, coffre dedans
  if (SCH[r] > 0) {
    if (rnd(2)) { // a gauche
      M[1][1] = base; M[1][2] = base; M[2][1] = base; M[2][2] = base;
      M[1][3] = wall; M[3][1] = wall; M[3][2] = wall; M[3][3] = wall;
      M[2][3] = 'B';
      M[1][1] = 'C';
    } else { // a droite
      M[1][16] = base; M[1][17] = base; M[2][16] = base; M[2][17] = base;
      M[1][15] = wall; M[3][15] = wall; M[3][16] = wall; M[3][17] = wall;
      M[2][15] = 'B';
      M[1][17] = 'C';
    }
  }
}

// ---------------- Nouvelle partie ----------------
static void newGame() {
  seed = (uint16_t)(titleSc * 7919u + 1u); // grain du titre : chaque monde est neuf
  fadePos = 0; // le monde se reveille depuis le noir
  room = 7; // le village
  plx = 160; ply = TOPOFF + 96;
  pdir = 2; swing = 0; pifr = 0; brth = 5; brthT = 0; pvx = pvy = 0;
  php = 6; pmax = 6; pcoin = 0; pkey = 0;
  hasStar = false; bossDead = false; gameDone = false;
  hasFlask = false; hasEcho = false; hasPlume = false;
  for (int i = 0; i < 18; i++) { chestOpen[i] = 0; doorOpen[i] = 0; visited[i] = (i == 7); }
  for (int i = 0; i < 36; i++) PT[i].life = 0;
  dlgOn = false; dlgEnd = false;
  // MONDE GENERE : salles procedurales, sauf la grotte (3), le village (7) et l'arene (11)
  for (int r = 0; r < 18; r++) {
    if (r == 3 || r == 7 || r == 11) {
      for (int ty = 0; ty < 12; ty++) { memcpy(RMO[r][ty], RM[r][ty], 20); RMO[r][ty][20] = 0; }
    } else genRoom(r);
  }
  // placement des monstres dans le monde genere
  for (int r = 0; r < 18; r++) {
    ESWN[r] = 0;
    for (int k = 0; k < EDEN[r]; k++) {
      for (int tries = 0; tries < 60; tries++) {
        int tx = 1 + rnd(MW - 2), ty = 1 + rnd(MH - 2);
        char c = RMO[r][ty][tx];
        if (c != '.' && c != ',' && c != 'f' && c != 'p') continue;
        if (SCH[r] > 0 && ((tx < 5 && ty < 5) || (tx > 14 && ty < 5))) continue; // pas dans une alcove
        bool nearC = false; // pas contre un coffre
        for (int dy2 = -1; dy2 <= 1 && !nearC; dy2++)
          for (int dx2 = -1; dx2 <= 1; dx2++) {
            int ax2 = tx + dx2, ay2 = ty + dy2;
            char cc = (ax2 >= 0 && ax2 < MW && ay2 >= 0 && ay2 < MH) ? RMO[r][ay2][ax2] : 0;
            if (cc == 'C') { nearC = true; break; }
          }
        if (nearC) continue;
        ESW[r][ESWN[r]][0] = isDun(r) ? (rnd(2) ? 2 : 3) : rnd(2);
        ESW[r][ESWN[r]][1] = tx; ESW[r][ESWN[r]][2] = ty;
        ESWN[r]++;
        break;
      }
    }
  }
  spawnEnemies();
}

// ---------------- Boucle principale ----------------
void ion_main(int argc, char *argv[]) {
  (void)argc; (void)argv;
  while (true) {
    mode = 0;
    titleScreen();
    newGame();
    pagesScreen(INTRO, 7, "COMMENCER L'AVENTURE");
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
      php = pmax; plx = 160; ply = TOPOFF + 96; room = 7; pifr = 0; fadePos = 0;
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
    if (mode == 4) pagesScreen(ENDING, 6, "FIN"); // le joueur a rendu l'etoile
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
