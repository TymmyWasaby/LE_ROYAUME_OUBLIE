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
static uint8_t palIdxSlow(KDColor c) {
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
static uint16_t palCK[512]; static uint8_t palCV[512]; static bool palCU[512]; // cache direct mappe : 2 Ko
static uint8_t palIdx(KDColor c) { // LA BOURSE AUX COULEURS : trouve l'index du premier coup
  // KDColor est RGB565 : red() renvoie 0-248, green() 0-252, blue() 0-248 (composantes 5-6-5 decalees)
  uint16_t key = (uint16_t)(((c.red() >> 3) << 11) | ((c.green() >> 2) << 5) | (c.blue() >> 3)); // le vrai RGB565, injectif
  int slot = key & 511;
  if (palCU[slot] && palCK[slot] == key) return palCV[slot]; // present : une seule lecture
  uint8_t r = palIdxSlow(c);
  palCK[slot] = key; palCV[slot] = r; palCU[slot] = true; // on range la couleur dans sa case
  return r;
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
static int fadePos = 15; // fondu de palette (15 = eclair), declare tot : blit() le lit
static void blit() {
  static KDColor up[SW * 8]; // 8 lignes par envoi : 30 appels d'affichage au lieu de 240
  int f = fadePos; // < 15 : l'ecran se reveille depuis le noir
  for (int y0 = 0; y0 < SH; y0 += 8) {
    int hh = (SH - y0 < 8) ? SH - y0 : 8;
    for (int y = 0; y < hh; y++) {
      const uint8_t *src = VRAM + (long)(y0 + y) * SW;
      KDColor *dst = up + (long)y * SW;
      if (f >= 15) { // cas normal : simple copie de palette, aucun calcul de fondu
        for (int x = 0; x < SW; x++) dst[x] = (src[x] < palN) ? PAL[src[x]] : KDColor::RGB24(0);
      } else { // assombrissement progressif, reconstruit en un seul RGB24
        for (int x = 0; x < SW; x++) {
          KDColor c = (src[x] < palN) ? PAL[src[x]] : KDColor::RGB24(0);
          uint32_t r = (uint32_t)((c.red() * f) / 15), g2 = (uint32_t)((c.green() * f) / 15), b2 = (uint32_t)((c.blue() * f) / 15);
          dst[x] = KDColor::RGB24((r << 16) | (g2 << 8) | b2);
        }
      }
    }
    Ion::Display::pushRect(KDRect(0, y0, SW, hh), up);
  }
  if (fadePos < 15) { fadePos += 3; if (fadePos > 15) fadePos = 15; } // le fondu va trois fois plus vite
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
static uint8_t NPC[][4] = {
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
static const char *DLG_ANC_R[8][1][3] = { // L'ANCIEN, une page nouvelle par region
  { { "Le GOLEM ebranle la colline.", "Ses rochers, renvoyes", "au souffle, sont sa faille." } },
  { { "Le GIVRE fait tomber la neige.", "Son etoile de six eclats", "se renvoie au vent." } },
  { { "La CENDRE embrase la terre.", "Ses trois braises chassent", "vite : souffle-les aussi." } },
  { { "La MOUSSE epouse la grotte.", "Spores visees puis croix :", "le vent retourne tout." } },
  { { "Le SABLE ride l'horizon.", "Son eventail de cinq jets", "s'abat de tres loin." } },
  { { "L'ARGILE ne bronche pas.", "Quatre pesantes en diagonale", "frappent large : glisse." } },
  { { "L'OMBRE se deplace sans bruit.", "Ses deux traits noirs", "volent a ton souffle." } },
  { { "PRIME garde le COEUR.", "Sa couronne d'eclats", "se renvoie, elle aussi." } }
};
static const char *DLG_ERM_R[8][1][3] = { // L'ERMITE, une page nouvelle par region
  { { "Cette grotte va au donjon.", "Prends une CLE. Puis rends", "ses rochers au GOLEM." } },
  { { "La glace reflechit tout,", "meme le vent. Le GIVRE", "craint ses propres eclats." } },
  { { "Les braises te chassent ?", "Elles ne distinguent pas", "ton souffle de leur maitre." } },
  { { "Les spores cherchent.", "Renvoie-les a la MOUSSE :", "elle les boira." } },
  { { "Le SABLE frappe large.", "Cinq jets, un eventail :", "ouvre un passage au souffle." } },
  { { "L'ARGILE est lente et lourde.", "Ses pesantes font mal :", "evite, souffle, recommence." } },
  { { "L'OMBRE surgit pres de toi.", "Ses traits suivent", "la direction du vent." } },
  { { "PRIME reunit les gestes", "des sept Gardiens. Tu es", "le vent : renvoie tout." } }
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
  { "Alors le GOLEM, pour la sauver,", "la brisa en HUIT ECLATS et en", "confia sept aux GARDIENS du monde." },
  { "Il garda le COEUR, au plus", "profond. Le royaume ne vit que", "des voleurs. Les heros echouerent." },
  { "L'Ancien invoqua les ESPRITS", "du vent. Le dernier... c'est toi.", "HUIT regions. HUIT gardiens." }
};
static const char *ENDING[8][3] = {
  { "LES HUIT ECLATS SE REUNISSENT", "AUTOUR DU COEUR, et l'ETOILE", "renait, entiere, immense." },
  { "Elle remonte, doucement,", "plus vive qu'avant. Le royaume", "respire enfin." },
  { "Et tu murmures la verite :", "huit gardiens aimants, zero", "voleur. Le royaume ecouta." },
  { "L'Ancien ferme les yeux :", "\"Nous avons maudit huit", "gardiens. Pardonnez-nous.\"" },
  { "On grava sur huit steles :", "\"Ils garderent sa lumiere comme", "elle les gardait du noir.\"" },
  { "Et quelque part, tres bas,", "dans la poussiere du donjon,", "huit geants reposent en paix." },
  { "L'etoile brille desormais un", "peu plus fort chaque nuit.", "Comme huit remerciements." },
  { "FIN - LE ROYAUME OUBLIE", "MERCI D'AVOIR JOUE !", "" }
};
// les intros des 8 regions (montrees au passage d'une region a l'autre)
static const char *CHAPI[8][3][3] = {
  { { "Le royaume de Tallia.", "Huit regions, huit gardiens.", "Ton aventure commence." },
    { "", "", "" },
    { "", "", "" } },
  { { "REGION DU GIVRE.", "Les steles chantent le lac", "gele et ses aurores." },
    { "Ici veille le COLOSSE DE", "GIVRE, deuxieme Gardien.", "Ses rochers sont des glacons." },
    { "Renvoie-les : la glace", "brise la glace. Le vent", "n'a jamais froid." } },
  { { "REGION DE LA CENDRE.", "Terre brulee sous un ciel", "de braise ancienne." },
    { "La SENTINELLE DE CENDRE", "patrouille depuis mille ans.", "Ses yeux sont des charbons." },
    { "Meme la pierre peut", "s'epuiser a veiller. Sois", "rapide, et distant." } },
  { { "REGION DE LA MOUSSE.", "Tout ce qui tombe ici", "finit par fleurir." },
    { "Le COLOSSE DE MOUSSE", "arrosait l'eclat comme un", "plant d'etoile." },
    { "Ses fleurs te suivront", "jusqu'au bout. Elles savent", "que tu viens aider." } },
  { { "REGION DU SABLE.", "Des dunes qui gardent la", "forme du vent. Comme toi." },
    { "Le GARDIEN DE SABLE scelle", "l'eclat sous une tempete", "de pierre seche." },
    { "Pousse la tempete contre", "elle-meme. Le desert", "n'appartient qu'au vent." } },
  { { "REGION DE L'ARGILE.", "Ici naissaient les statues", "du royaume de Tallia." },
    { "L'IDOLE D'ARGILE fut la", "premiere a accepter un eclat.", "Par devotion." },
    { "Fends-la avec respect :", "elle attend ce coup depuis", "le premier jour." } },
  { { "REGION DE L'OMBRE.", "Le royaume evitait ces", "grottes. Les Gardiens non." },
    { "Le TITAN D'OMBRE enfile", "l'eclat comme une lampe", "au bout d'un fil." },
    { "Renvoie sa nuit sur lui :", "meme l'ombre n'echappe", "pas au vent." } },
  { { "LA DERNIERE REGION.", "Le donjon d'origine, la ou", "tout a commence." },
    { "Au fond attend GOLEM PRIME,", "le premier Gardien, celui du", "COEUR de l'etoile." },
    { "Il connait ton nom depuis", "ton premier souffle ici.", "Sois digne. Sois doux." } }
};
// ce que chante le vent a chaque eclat recolte
static const char *DLG_ECLATS[7][2][3] = {
  { { "LE PREMIER ECLAT !", "Il palpite dans tes mains,", "chaud et vivant." },
    { "Les steles disaient vrai :", "le Gardien ne gardait pas", "TOUTE l'etoile... un eclat." } },
  { { "DEUXIEME ECLAT !", "Le Colosse de Givre rend", "son dernier flocon." },
    { "Six Gardiens encore veillent", "chacun sur un eclat. Le vent", "continue son chemin." } },
  { { "TROISIEME ECLAT !", "La Sentinelle de Cendre", "s'endort en poussiere grise." },
    { "Les heros des steles", "n'avaient pas compris. Toi,", "tu commences a comprendre." } },
  { { "QUATRIEME ECLAT !", "Le Colosse de Mousse devient", "un lit de fleurs." },
    { "Il jardinait pour l'etoile,", "parle le vent. Tous ces", "geants l'aimaient, aussi." } },
  { { "CINQUIEME ECLAT !", "Le Gardien de Sable s'effrite", "en une dune douce." },
    { "Trois regions encore,", "et le coeur de tout cela", "attend au fond." } },
  { { "SIXIEME ECLAT !", "L'Idole d'Argile se fend,", "sereine, presque soulagee." },
    { "Elle murmurait depuis des", "siecles : \"Venez. Nous", "vieillissons tous.\"" } },
  { { "SEPTIEME ECLAT !", "Le Titan d'Ombre se dissout", "dans la lumiere du soir." },
    { "Une seule region reste.", "L'origine. La ou le premier", "Gardien tient le COEUR." } }
};
// les 8 Gardiens : nom court (barre de vie) et palette {sombre, corps, arete, oeil, touche}
static const char *BN[8] = { "GOLEM", "GIVRE", "CENDRE", "MOUSSE", "SABLE", "ARGILE", "OMBRE", "PRIME" };
static const uint32_t BSK[8][5] = {
  { 0x6E6E76, 0x9A9AA2, 0xB0B0B0, 0xE86820, 0x3A5A34 }, // GOLEM de pierre
  { 0x3A5A80, 0x6A8AB0, 0xA8C4DC, 0x40D8F0, 0xB8E8F8 },  // COLOSSE DE GIVRE
  { 0x5A4038, 0x8A6858, 0xB09080, 0xFF3818, 0x3A2822 },  // SENTINELLE DE CENDRE
  { 0x2E5A28, 0x5A8A48, 0x88B868, 0xF0E850, 0x1E3818 },  // COLOSSE DE MOUSSE
  { 0x8A7448, 0xC4A868, 0xE8D8A0, 0xFFB020, 0x8A6A3A },  // GARDIEN DE SABLE
  { 0x7A4830, 0xB06840, 0xD89060, 0xF05020, 0x5A3020 },  // IDOLE D'ARGILE
  { 0x2A2438, 0x4A4058, 0x6A5A78, 0xB03AF0, 0x1A1626 },  // TITAN D'OMBRE
  { 0x8A6A20, 0xE8C858, 0xFFF0A0, 0xFFF8D0, 0x6A5A18 }   // GOLEM PRIME, PORTE-ETOILE
};

// ---------------- Etat du jeu ----------------
static int mode; // 0 titre, 1 intro, 2 jeu, 3 defaite, 4 epilogue
static int room; // salle courante 0..17
static float plx, ply, pvx, pvy; // position + knockback
static int pdir, swing, pifr, brth, brthT; // direction, animation de souffle, invincibilite, jauge de souffle
static int pLvl = 1, pXp, pKills0; // LE SOUFFLE MURIT : niveau, experience, kills deja comptes
static int php, pmax, pcoin, pkey; static bool hasStar, bossDead, gameDone;
static bool hasFlask, hasEcho, hasPlume; // tresors secrets
static unsigned titleSc; // grain de hasard : le temps passe sur le titre
static uint8_t chestOpen[18], doorOpen[18]; static bool visited[18]; static bool pmov;
static int frame, shake; // fadePos est declare plus haut, avant blit()
static unsigned playT; static int deaths; static uint32_t steleRead; // stats d'aventure (ticks de 16 ms, morts, steles lues)
static int kills; // monstres souffles cette partie
// medailles et records de session (remis a zero a chaque lancement de l'app)
static unsigned killsS, bestT; static int runsDone, ngPlus, ngRun, medals; static int chap; static bool chapGo; // chapitre 0..7, eclat pret a emmener

// ---------------- Sauvegarde reelle (portage de NumWorks Extapp Storage, par Yaya-Cout) ----------------
// L'EADK officiel n'expose pas le stockage ; cette bibliotheque libre lit et ecrit
// la memoire de l'atelier directement (comme les emulateurs et KhiCAS).
// Le fichier bsnumworks.nws contient l'aventure : plus de code a taper.
static uint32_t savRev32(uint32_t v) {
  return (((v & 0xFF) << 24) | ((v & 0xFF00) << 8) | ((v & 0xFF0000) >> 8) | ((v & 0xFF000000u) >> 24));
}
static bool savValid(const uint32_t *a) { return *a == savRev32(0xBADD0BEEu); }
static uint8_t savModel() { // modele de machine : 1 = N0110/N0115, 2 = N0120
  uint32_t so; uint32_t sp = (uint32_t)&so;
  if (sp >= 0x20000000u && sp <= 0x2003FFFFu) return 1;
  if (sp >= 0x24000000u && sp <= 0x2404FFFFu) return 2;
  return 0;
}
static uint32_t savUserland() {
  if (savModel() == 1) { // deux adresses possibles selon la version d'Epsilon
    if (*(uint32_t *)(*(uint32_t *)0x20000004 + 0x20000 - 0x8) == savRev32(0xFEEDC0DEu)) return *(uint32_t *)0x20000004 + 0x20000 - 0x8;
    return *(uint32_t *)0x20000004 + 0x10000 - 0x8;
  }
  return *(uint32_t *)0x24000004 + 0x20000 - 0x8;
}
static uint32_t savAddress() { return *(uint32_t *)(savUserland() + 0xC); }
static uint32_t savSize() { return *(uint32_t *)(savUserland() + 0x10); }
static const char *SAVF = "bsnumworks.nws";
static bool savRead(char *dst, uint32_t len) { // lit le fichier de sauvegarde dans dst
  uint32_t a = savAddress(), end2 = a + savSize();
  if (!savValid((const uint32_t *)a)) return false;
  char *o = (char *)a + 4;
  while ((uint32_t)o < end2) {
    uint16_t sz = *(uint16_t *)o;
    if (sz == 0) break;
    if (strcmp(o + 2, SAVF) == 0) {
      uint16_t ns = (uint16_t)(strlen(o + 2) + 1);
      if ((uint32_t)sz - 2 - ns != len) return false; // fichier inattendu : on ignore
      memcpy(dst, o + 2 + ns, len);
      return true;
    }
    o += sz;
  }
  return false;
}
static char *savNextFree() { // l'adresse ou le prochain fichier de l'atelier doit s'ecrire
  uint32_t a = savAddress(), end2 = a + savSize();
  if (!savValid((const uint32_t *)a)) return 0;
  char *o = (char *)a + 4;
  while ((uint32_t)o < end2) {
    if (*(uint16_t *)o == 0) return o;
    o += *(uint16_t *)o;
  }
  return (char *)end2;
}
static bool savEraseOld() { // retire l'ancienne sauvegarde de l'atelier
  uint32_t a = savAddress(), end2 = a + savSize();
  if (!savValid((const uint32_t *)a)) return false;
  char *o = (char *)a + 4;
  while ((uint32_t)o < end2) {
    uint16_t sz = *(uint16_t *)o;
    if (sz == 0) return false;
    if (strcmp(o + 2, SAVF) == 0) {
      char *nf = savNextFree();
      memmove(o, o + sz, (uint32_t)(nf - (o + sz)));
      memset(nf - sz, 0, sz);
      return true;
    }
    o += sz;
  }
  return false;
}
static bool savStore(const char *src, uint32_t len) { // ecrit le fichier a l'emplacement libre
  char *nf = savNextFree();
  if (!nf) return false;
  uint32_t need = 2 + strlen(SAVF) + 1 + len;
  if (savAddress() + savSize() < (uint32_t)nf + need) return false; // atelier plein : pas de sauvegarde
  *(uint16_t *)nf = (uint16_t)need;
  memcpy(nf + 2, SAVF, strlen(SAVF) + 1);
  memcpy(nf + 2 + strlen(SAVF) + 1, src, len);
  memset(nf + need, 0, (uint32_t)((savAddress() + savSize()) - ((uint32_t)nf + need)));
  return true;
}
// ---------------- Le plan de la region : la premiere est un rectangle, les autres non ----------------
static uint8_t NB[18][4]; static int8_t MPX[18], MPY[18]; static uint16_t wSeed; // voisins + positions
static void buildLayout() { // dessine le plan du monde ; region 0 : rectangle, sinon le monde se deroule
  wSeed = seed; // note le grain de depart : au chargement, ce plan se redessine pareil
  for (int r = 0; r < 18; r++) { MPX[r] = -99; MPY[r] = -99; for (int d = 0; d < 4; d++) NB[r][d] = 255; }
  if (chap == 0) { // LA PREMIERE REGION : le plan du croquis, sept salles, une cle, un boss
    const int8_t px7[7] = { 0, 1, 0, 1, 0, 1, 0 }; // 2 colonnes...
    const int8_t py7[7] = { 0, 0, 1, 1, 2, 2, 3 }; // ...3 rangees, le boss un cran plus bas
    const uint8_t rm7[7] = { 7, 0, 1, 4, 2, 3, 11 }; // depart=village, cle=4, X=arene
    for (int k = 0; k < 7; k++) { MPX[rm7[k]] = px7[k]; MPY[rm7[k]] = py7[k]; }
  } else {
    MPX[7] = 0; MPY[7] = 0; // le village au coeur du monde
    int order[17], on = 0;
    for (int r = 0; r < 18; r++) if (r != 7) order[on++] = r;
    for (int i = 16; i > 0; i--) { int j = rnd(i + 1); int t = order[i]; order[i] = order[j]; order[j] = t; } // melange
    for (int k = 0; k < 17; k++) {
      int r = order[k], bx = 99, by = 99, bd = 999;
      for (int p = 0; p < 18; p++) { // le cote libre le plus proche du village : le monde reste compact
        if (MPX[p] == -99) continue;
        for (int d = 0; d < 4; d++) {
          int nx = MPX[p] + ((d == 1) ? 1 : ((d == 0) ? -1 : 0));
          int ny = MPY[p] + ((d == 3) ? 1 : ((d == 2) ? -1 : 0));
          if (nx < -3 || nx > 3 || ny < -3 || ny > 3) continue; // le monde tient dans la mini-carte
          bool occ = false;
          for (int q = 0; q < 18; q++) if (MPX[q] == (int8_t)nx && MPY[q] == (int8_t)ny) { occ = true; break; }
          if (occ) continue;
          int md = nx * nx + ny * ny;
          if (md < bd || (md == bd && rnd(2))) { bd = md; bx = nx; by = ny; }
        }
      }
      if (bx == 99) { // filet de securite : la premiere case libre en spirale
        for (int yy = -3; yy <= 3 && bx == 99; yy++) for (int xx = -3; xx <= 3 && bx == 99; xx++) {
          bool occ = false;
          for (int q = 0; q < 18; q++) if (MPX[q] == (int8_t)xx && MPY[q] == (int8_t)yy) { occ = true; break; }
          if (!occ) { bx = xx; by = yy; }
        }
      }
      MPX[r] = (int8_t)bx; MPY[r] = (int8_t)by;
    }
  }
  for (int r = 0; r < 18; r++) // on en deduit qui touche qui
    for (int s = 0; s < 18; s++) {
      if (r == s || MPX[s] == -99 || MPX[r] == -99) continue;
      if (MPX[s] == MPX[r] - 1 && MPY[s] == MPY[r]) NB[r][0] = (uint8_t)s;
      if (MPX[s] == MPX[r] + 1 && MPY[s] == MPY[r]) NB[r][1] = (uint8_t)s;
      if (MPY[s] == MPY[r] - 1 && MPX[s] == MPX[r]) NB[r][2] = (uint8_t)s;
      if (MPY[s] == MPY[r] + 1 && MPX[s] == MPX[r]) NB[r][3] = (uint8_t)s;
    }
}
// dialogue en cours
static const char *(*dgp)[3]; // pointeur vers un tableau de pages
static int dgN, dgI; static bool dlgOn, dlgEnd; // dlgEnd : apres la derniere page, fin du jeu

// monstres vivants (le boss est EN[0], type 4)
static struct En { float x, y, vx, vy; int type, hp, t, st, dir; bool alive, flash; } EN[14];
static int enN;
static struct Bu { float x, y, vx, vy; int fr; bool on; } BU[6]; // fr : projectile renvoye par le souffle
static struct Pk { float x, y; int t; bool on; } PK[8]; // 0 coeur, 1 piece, 2 etoile
static uint8_t CN[50][3]; static uint8_t CNON[50]; // 50 pieces semees dans la region : { salle, tuileX, tuileY } + prises ou non
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

// ---------------- Sauvegarde complete : le monde tel que tu l'as laisse ----------------
static uint8_t svB[5192]; // l'image complete de la partie
static const uint32_t SAV_MAGIC = 0x5A8E1102u; // nouveau format : tout est dedans
static bool savOk;
static void svPut(int o, const void *p, int n) { memcpy(svB + o, p, n); }
static void svGet(int o, void *p, int n) { memcpy(p, svB + o, n); }
static void saveGame() { // tout y est : region, salle, position exacte, bourse, objets, coffres, pots brises
  uint32_t sd = wSeed, pt = playT, ks = killsS, bt = bestT, kl = (uint32_t)kills, sr = steleRead;
  uint32_t fl2 = (hasStar ? 1u : 0u) | (hasFlask ? 2u : 0u) | (hasEcho ? 4u : 0u) | (hasPlume ? 8u : 0u) |
                 (bossDead ? 16u : 0u) | (chapGo ? 32u : 0u) | (ngRun ? 64u : 0u);
  int32_t c = chap, m = medals, ng = ngPlus, rm = room, hp = php, pm = pmax, pc = pcoin, pk = pkey, de = deaths, rd = runsDone;
  float px2 = plx, py2 = ply;
  svPut(0, &SAV_MAGIC, 4); svPut(4, &sd, 4); svPut(8, &c, 4); svPut(12, &m, 4); svPut(16, &ng, 4); svPut(20, &rm, 4);
  svPut(24, &hp, 4); svPut(28, &pm, 4); svPut(32, &pc, 4); svPut(36, &pk, 4); svPut(40, &de, 4); svPut(76, &rd, 4);
  svPut(44, &pt, 4); svPut(48, &ks, 4); svPut(52, &bt, 4); svPut(56, &kl, 4); svPut(60, &sr, 4);
  svPut(64, &px2, 4); svPut(68, &py2, 4); svPut(72, &fl2, 4);
  svPut(80, chestOpen, 18); svPut(98, doorOpen, 18); svPut(116, visited, 18);
  svPut(134, ESWN, 18); svPut(152, ESW, 270);
  svPut(422, RMO, 4536); // chaque pot brise, chaque rocher cede, chaque fissure ouverte
  int32_t lv = pLvl, xp = pXp;
  svPut(4958, &lv, 4); svPut(4962, &xp, 4); // le souffle et son experience
  svPut(4966, NPC, 24); // ou chaque PNJ se tenait : la sauvegarde est totale
  svPut(4990, CN, 150); svPut(5140, CNON, 50); // les 50 pieces semees, prises ou pas
  savEraseOld(); // s'il y avait deja une sauvegarde, on la remplace
  savStore((const char *)svB, 5190);
}
static void loadGame() { // au demarrage : y a-t-il une sauvegarde complete a remonter ?
  savOk = false;
  if (!savRead((char *)svB, 5190)) return;
  uint32_t mg; svGet(0, &mg, 4);
  if (mg != SAV_MAGIC) return; // ancien format ou fichier inconnu : on repart a neuf
  int32_t c, m, ng, rm; svGet(8, &c, 4); svGet(12, &m, 4); svGet(16, &ng, 4); svGet(20, &rm, 4);
  if (c < 0 || c > 7 || m < 0 || m > 127 || (ng != 0 && ng != 1) || rm < 0 || rm > 17) return; // sauvegarde folle : on ignore
  savOk = true;
}
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
  if (c == 't' || c == 'r' || c == 'w' || c == ' ' || c == '#' || c == 'D' || c == 'C' || c == 'F' || c == 'P' || c == 'B' || c == 'b') return true;
  return false; // les PNJ ne bloquent plus : l'ermite ne peut plus t'enfermer
}
static void drawTile(int tx, int ty, int ox, int oy) {
  char c = tileAt(tx, ty);
  int x = tx * TS + ox, y = TOPOFF + ty * TS + oy;
  bool frz = (chap == 1); // REGION DU GIVRE : neige et glace
  bool cdr = (chap == 2); // REGION DE LA CENDRE : terre brulee
  bool mou = (chap == 3); // REGION DE LA MOUSSE : tout y fleurit
  bool sab = (chap == 4); // REGION DU SABLE : dunes sans fin
  bool arg = (chap == 5); // REGION DE L'ARGILE : le pays des statues
  bool omb = (chap == 6); // REGION DE L'OMBRE : les grottes interdites
  bool prm = (chap == 7); // LA DERNIERE REGION : le donjon d'origine
  KDColor g1 = frz ? KDColor::RGB24(0xD8E4F0) : (cdr ? KDColor::RGB24(0x4A4038) : (mou ? KDColor::RGB24(0x3E7030) : (sab ? KDColor::RGB24(0xD8B878) : (arg ? KDColor::RGB24(0xC08058) : (omb ? KDColor::RGB24(0x2A2434) : (prm ? KDColor::RGB24(0x3A3648) : C_GRASS)))))),
          g2 = frz ? KDColor::RGB24(0xC0D0E4) : (cdr ? KDColor::RGB24(0x403A34) : (mou ? KDColor::RGB24(0x2E5A24) : (sab ? KDColor::RGB24(0xC8A868) : (arg ? KDColor::RGB24(0xA87048) : (omb ? KDColor::RGB24(0x221E2C) : (prm ? KDColor::RGB24(0x322E40) : C_GRAS2)))))),
          g3 = frz ? KDColor::RGB24(0x8AA0BC) : (cdr ? KDColor::RGB24(0x2A2420) : (mou ? KDColor::RGB24(0x1E4016) : (sab ? KDColor::RGB24(0xA88850) : (arg ? KDColor::RGB24(0x805538) : (omb ? KDColor::RGB24(0x16121E) : (prm ? KDColor::RGB24(0x24201C) : C_DGRN))))));
  if (c == '.' || c == 'f' || c == 'p' || c == 'P' || c == 'c') {
    if (c == 'p') { // chemin : bordures qui fondent dans l'herbe
      fRect(x, y, TS, TS, C_PATH);
      px(x + 2, y + 5, KDColor::RGB24(0xB0904E)); px(x + 9, y + 11, KDColor::RGB24(0xB0904E));
      px(x + 13, y + 3, KDColor::RGB24(0xB0904E));
      char n;
      n = tileAt(tx, ty - 1); if (n != 'p' && n != ' ') fRect(x, y, TS, 2, g2);
      n = tileAt(tx, ty + 1); if (n != 'p' && n != ' ') fRect(x, y + 14, TS, 2, g2);
      n = tileAt(tx - 1, ty); if (n != 'p' && n != ' ') fRect(x, y, 2, TS, g2);
      n = tileAt(tx + 1, ty); if (n != 'p' && n != ' ') fRect(x + 14, y, 2, TS, g2);
    } else {
      fRect(x, y, TS, TS, ((tx * 7 + ty * 13) & 3) ? g1 : g2);
      int h2 = (tx * 5 + ty * 3) & 15;
      px(x + 3 + (tx & 3), y + 9, g3); px(x + 11, y + 4 + (ty & 2), g3);
      if (h2 == 0) { px(x + 8, y + 6, g3); px(x + 7, y + 7, g3); px(x + 9, y + 7, g3); }
      else if (h2 == 5) { px(x + 5, y + 12, KDColor::RGB24(0x7CCE6A)); px(x + 12, y + 6, KDColor::RGB24(0x7CCE6A)); } // brins clairs
    }
    if (c == 'f') { // fleurs qui ondulent
      int sw = ((frame >> 4) + tx) & 1;
      fRect(x + 4 + sw, y + 10, 1, 3, g3); fRect(x + 11 - sw, y + 9, 1, 3, g3);
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
    fRect(x, y, TS, TS, ((tx * 7 + ty * 13) & 3) ? g1 : g2);
    fCirc(x + 9, y + 12, 6, KDColor::RGB24(0x1E4A18));
    fRect(x + 6, y + 9, 4, 6, KDColor::RGB24(0x6A4A2A));
    fRect(x + 6, y + 9, 1, 6, KDColor::RGB24(0x523A20));
    int sw = ((frame >> 5) + ty) & 1;
    fCirc(x + 8 + sw, y + 6, 7, KDColor::RGB24(0x16421E)); // masse sombre
    fCirc(x + 7 + sw, y + 5, 5, C_TREE);                   // volume
    fCirc(x + 5 + sw, y + 3, 3, C_TREE2);                  // lumiere
    px(x + 9 + sw, y + 2, KDColor::RGB24(0x4A9E48));
    if (((tx * 3 + ty) & 7) == 2) { px(x + 5 + sw, y + 8, C_RED); px(x + 10 + sw, y + 5, C_RED); } // baies
    if (frz) { fCirc(x + 6 + sw, y + 2, 3, KDColor::RGB24(0xE8F0FA)); fCirc(x + 10 + sw, y + 5, 2, KDColor::RGB24(0xE8F0FA)); fCirc(x + 8 + sw, y + 1, 2, KDColor::RGB24(0xF8FAFE)); } // sapin enneige
    if (cdr) { // arbre calcine : couronne sombre, braises qui veillent
      fCirc(x + 7 + sw, y + 5, 4, KDColor::RGB24(0x2A2018));
      fCirc(x + 5 + sw, y + 3, 2, KDColor::RGB24(0x3A2C20));
      if (((frame >> 3) + tx + ty) & 3) { px(x + 6 + sw, y + 4, C_ORG); px(x + 9 + sw, y + 6, C_RED); }
    }
    if (mou) { // arbre gorge de seve : couronne lumineuse, liane, rosee
      fCirc(x + 7 + sw, y + 4, 3, KDColor::RGB24(0x5A9E48));
      fRect(x + 4 + sw, y + 7, 1, 4, KDColor::RGB24(0x4A8E38)); // liane qui pend
      if (((frame >> 4) + tx) & 1) px(x + 9 + sw, y + 3, KDColor::RGB24(0xB0E890)); // rosee qui brille
    }
    if (sab) { // palmier : houppier defrise, regime de dattes
      fCirc(x + 8 + sw, y + 4, 3, KDColor::RGB24(0x5A9E48));
      px(x + 5 + sw, y + 5, KDColor::RGB24(0x5A9E48)); px(x + 11 + sw, y + 5, KDColor::RGB24(0x5A9E48)); // palmes
      px(x + 7 + sw, y + 6, KDColor::RGB24(0x8A5A28)); px(x + 8 + sw, y + 7, KDColor::RGB24(0x8A5A28)); // dattes
    }
    if (arg) { // arbre d'atelier : couronne d'ocre, coulee d'argile sur l'ecorce
      fCirc(x + 7 + sw, y + 4, 3, KDColor::RGB24(0xB07850));
      px(x + 5 + sw, y + 6, KDColor::RGB24(0xB07850)); px(x + 9 + sw, y + 5, KDColor::RGB24(0xB07850));
      fRect(x + 6, y + 11, 1, 3, KDColor::RGB24(0xC98050)); // coulee
    }
    if (omb) { // arbre d'ombre : couronne de brume, un wisp l'habite
      fCirc(x + 7 + sw, y + 4, 3, KDColor::RGB24(0x3A2E4E));
      if (((frame >> 3) + tx * 3) & 7) { px(x + 6 + sw, y + 3, C_MAG); px(x + 9 + sw, y + 5, C_PUR); } // le wisp veille
    }
    if (prm) { // arbre mineral : couronne de pierre, reflet du coeur
      fCirc(x + 7 + sw, y + 4, 3, KDColor::RGB24(0x4A4458));
      if (((frame >> 4) + tx) & 1) px(x + 8 + sw, y + 3, C_GOLD); // le coeur eclaire tout
    }
    px(x + 7, y + 14, g3);
  } else if (c == 'w') { // eau profonde : houle croisee, bulles, nenuphars
    fRect(x, y, TS, TS, frz ? KDColor::RGB24(0x74A4D4) : (cdr ? KDColor::RGB24(0x4A3020) : (mou ? KDColor::RGB24(0x3E6E52) : (sab ? KDColor::RGB24(0x4AA4C4) : (arg ? KDColor::RGB24(0xB07048) : (omb ? KDColor::RGB24(0x1A1626) : (prm ? KDColor::RGB24(0x282438) : C_WTR))))))); // gele ; huile ; etang ; oasis ; argile ; encre ; eau du fond
    fRect(x + 1, y + 1, 14, 14, frz ? KDColor::RGB24(0x28507E) : (cdr ? KDColor::RGB24(0x301E14) : (mou ? KDColor::RGB24(0x1E4636) : (sab ? KDColor::RGB24(0x1E6A8A) : (arg ? KDColor::RGB24(0x704028) : (omb ? KDColor::RGB24(0x12101C) : (prm ? KDColor::RGB24(0x1C1826) : KDColor::RGB24(0x103050)))))))); // les grandes profondeurs
    fRect(x + 2, y + 2, 12, 12, frz ? KDColor::RGB24(0x1C3E66) : (cdr ? KDColor::RGB24(0x241610) : (mou ? KDColor::RGB24(0x16382A) : (sab ? KDColor::RGB24(0x145876) : (arg ? KDColor::RGB24(0x583020) : (omb ? KDColor::RGB24(0x0C0A14) : (prm ? KDColor::RGB24(0x141020) : KDColor::RGB24(0x0E2438))))))));
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
      fCirc(x + 8, y + 8, 4, KDColor::RGB24(0x2E6A2E)); px(x + 9, y + 7, g3); px(x + 6, y + 9, KDColor::RGB24(0x4A9E48));
    }
    char n;
    n = tileAt(tx, ty - 1); if (n != 'w' && n != ' ') { fRect(x, y, TS, 2, KDColor::RGB24(0x8AB8F0)); if (((tx + (frame >> 4)) & 1)) { px(x + 3, y + 1, C_WHT); px(x + 11, y + 1, C_WHT); } }
    n = tileAt(tx, ty + 1); if (n != 'w' && n != ' ') { fRect(x, y + 14, TS, 2, KDColor::RGB24(0x5A82C0)); if (((tx + (frame >> 4)) & 1)) px(x + 6, y + 14, KDColor::RGB24(0x8AB8F0)); }
    n = tileAt(tx - 1, ty); if (n != 'w' && n != ' ') { fRect(x, y, 2, TS, KDColor::RGB24(0x8AB8F0)); if (((ty + (frame >> 3)) & 1)) { px(x + 1, y + 4, C_WHT); px(x + 1, y + 11, C_WHT); } }
    n = tileAt(tx + 1, ty); if (n != 'w' && n != ' ') { fRect(x + 14, y, 2, TS, KDColor::RGB24(0x5A82C0)); if (((ty + (frame >> 3)) & 1)) px(x + 14, y + 8, KDColor::RGB24(0x8AB8F0)); }
  } else if (c == 'r') { // rocher : face sombre + lumiere + mousse
    fRect(x, y, TS, TS, ((tx * 7 + ty * 13) & 3) ? g1 : g2);
    fCirc(x + 9, y + 12, 6, KDColor::RGB24(0x1E4A18));
    fCirc(x + 8, y + 10, 6, KDColor::RGB24(0x4E4E56));  // face sombre
    fCirc(x + 8, y + 8, 6, C_ROCK);
    fCirc(x + 6, y + 7, 3, KDColor::RGB24(0xB0B0B0));
    px(x + 5, y + 5, KDColor::RGB24(0xD0D0D0));
    px(x + 11, y + 12, C_ROCK2); px(x + 6, y + 12, C_ROCK2);
    if (((tx * 5 + ty) & 7) == 3) { px(x + 10, y + 9, KDColor::RGB24(0x3A5A34)); px(x + 11, y + 8, KDColor::RGB24(0x3A5A34)); }
    if (frz) fCirc(x + 6, y + 6, 3, KDColor::RGB24(0xE8F0FA)); // chapeau de neige
    if (cdr && (((tx * 3 + ty) & 7) == 5)) { px(x + 9, y + 6, C_ORG); px(x + 8, y + 7, C_RED); px(x + 10, y + 7, C_YEL); } // rocher encore chaud
    if (mou) { fCirc(x + 7, y + 7, 3, KDColor::RGB24(0x4E8E3E)); if (((tx * 7 + ty) & 7) == 4) { fRect(x + 4, y + 10, 1, 2, C_WHT); px(x + 4, y + 9, C_RED); px(x + 3, y + 9, C_RED); } } // mousse epaisse + petit champignon
    if (sab) { fCirc(x + 8, y + 12, 4, KDColor::RGB24(0xC8A868)); px(x + 8, y + 11, KDColor::RGB24(0xD8B878)); } // dune enterree a moitie
    if (arg && (((tx * 5 + ty) & 7) < 3)) { // fragment de statue : un vieil oeil sculpte
      fRect(x + 6, y + 4, 4, 1, KDColor::RGB24(0x8A5A38)); // arcade
      px(x + 7, y + 6, KDColor::RGB24(0x6A4228)); px(x + 9, y + 6, KDColor::RGB24(0x6A4228)); // les yeux
      px(x + 8, y + 8, KDColor::RGB24(0x6A4228)); // le nez
    }
    if (omb && (((tx * 3 + ty) & 7) == 2)) { px(x + 7, y + 7, C_PUR); px(x + 8, y + 8, C_MAG); px(x + 6, y + 9, C_PUR); } // veines lumineuses dans la pierre
    if (prm && (((tx * 5 + ty) & 7) == 3)) { px(x + 7, y + 5, C_GOLD); px(x + 8, y + 6, C_YEL); } // eclat du coeur dans la roche
  } else if (c == 'B' || c == 'b') { // ROCHER FISSURE : il cede au souffle, deux etats
    fRect(x, y, TS, TS, ((tx * 7 + ty * 13) & 3) ? g1 : g2);
    fCirc(x + 8, y + 11, 6, KDColor::RGB24(0x4E4E56)); // face sombre
    fCirc(x + 8, y + 9, 6, C_ROCK);
    fCirc(x + 6, y + 8, 3, KDColor::RGB24(0xB0B0B0)); // lumiere
    px(x + 5, y + 6, KDColor::RGB24(0xD0D0D0));
    if (c == 'B') { // premiere fissure : un trait fin en zigzag
      px(x + 8, y + 3, KDColor::RGB24(0x2A2A2E)); px(x + 9, y + 5, KDColor::RGB24(0x2A2A2E));
      px(x + 8, y + 7, KDColor::RGB24(0x2A2A2E)); px(x + 9, y + 9, KDColor::RGB24(0x2A2A2E));
    } else { // deuxieme fissure : large trait, eclats, et ca brille : ca veut ceder
      fRect(x + 8, y + 2, 1, 8, KDColor::RGB24(0x1E1E22));
      px(x + 7, y + 4, KDColor::RGB24(0x1E1E22)); px(x + 10, y + 6, KDColor::RGB24(0x1E1E22)); px(x + 7, y + 8, KDColor::RGB24(0x1E1E22));
      px(x + 4, y + 5, C_ROCK2); px(x + 12, y + 8, C_ROCK2);
      if ((frame >> 3) & 1) px(x + 8, y + 10, C_YEL);
    }
  } else if (c == '#' || c == 'F') { // mur de briques (+ torche)
    fRect(x, y, TS, TS, C_WALL);
    fRect(x, y, TS, 1, frz ? KDColor::RGB24(0xC8D8E8) : (cdr ? KDColor::RGB24(0x6A5A54) : (mou ? KDColor::RGB24(0x5E7054) : (sab ? KDColor::RGB24(0xC8A878) : (arg ? KDColor::RGB24(0xB89068) : (omb ? KDColor::RGB24(0x4A3E5E) : (prm ? KDColor::RGB24(0xC8A850) : KDColor::RGB24(0x565664)))))))); // arete eclairee (neige ; braise ; lichen ; sable ; cuite ; violet ; or du coeur)
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
    fRect(x, y, TS, TS, isDun(room) ? C_FLR : g1);
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
    fRect(x, y, TS, TS, isDun(room) ? C_FLR : g1);
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
    EN[enN].hp = ((ESW[room][i][0] >= 2) ? 2 : 1) + ngPlus; // ROYAUME NOIR : monstres plus durs
    EN[enN].t = rnd(60); EN[enN].st = 0; EN[enN].dir = 0;
    EN[enN].alive = true; EN[enN].flash = false;
    enN++;
  }
  if (room == 11 && !bossDead) { // LE GOLEM
    EN[0].x = 160; EN[0].y = TOPOFF + 64; EN[0].vx = EN[0].vy = 0;
    EN[0].type = 4 + chap; EN[0].hp = 16 + chap * 3 + ngPlus * 8; EN[0].t = 0; EN[0].st = 0; EN[0].dir = 0; // le Gardien de la region
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
  // l'objet ne doit pas tomber dans un arbre ou un rocher : on pose sur la case libre la plus proche
  int tx = (int)(x / TS), ty = (int)((y - TOPOFF) / TS);
  if (tx < 0 || tx >= MW || ty < 0 || ty >= MH || solid(tx, ty)) {
    int bd = 99, bx = tx, by = ty;
    for (int rr = ty - 2; rr <= ty + 2; rr++)
      for (int cc = tx - 2; cc <= tx + 2; cc++) {
        if (cc < 0 || cc >= MW || rr < 0 || rr >= MH || solid(cc, rr)) continue;
        int d = (cc - tx) * (cc - tx) + (rr - ty) * (rr - ty);
        if (d < bd) { bd = d; bx = cc; by = rr; }
      }
    x = bx * TS + 8; y = TOPOFF + by * TS + 8; // au centre de la case libre trouvee
  }
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
  if (php <= 0) { deaths++; mode = 3; }
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
        e->alive = false; kills++;
        poof(e->x, e->y, C_WTR2, 12); poof(e->x, e->y - 4, C_WHT, 5);
        if (rnd(2)) dropPickup(e->x, e->y - 6, -1);
        done = true;
      } else if (c == 'F') { // BRULE DANS UNE TORCHE
        e->alive = false; kills++;
        poof(e->x, e->y, C_ORG, 12); poof(e->x, e->y - 4, C_YEL, 6);
        if (rnd(2)) dropPickup(e->x, e->y - 6, -1);
        done = true;
      } else if (c != '.' && c != ',' && c != 'p' && c != 'f' && c != 'c' && c != 'P') { // PERCUTE UN MUR
        e->hp -= hasPlume ? 3 : 2;
        poof(e->x, e->y, C_WHT, 7); shake = 3;
        if (e->hp <= 0) {
          e->alive = false; kills++; poof(e->x, e->y, C_PUR, 12);
          if (rnd(2)) dropPickup(e->x, e->y - 6, -1);
        } else { e->st = 99; e->t = 0; }
        done = true;
      }
      if (!done) { // emporte de plein vol : il cogne le GARDIEN de toute la force du souffle
        for (int j = 0; j < 14 && !done; j++) {
          if (!EN[j].alive || EN[j].type < 4) continue;
          float ddx = EN[j].x - e->x, ddy = EN[j].y - e->y;
          if (ddx * ddx + ddy * ddy < 324) {
            EN[j].hp -= 1 + pLvl / 2; // un souffle murit cogne bien plus fort
            e->alive = false; kills++;
            poof(e->x, e->y, C_WHT, 12); shake = 6;
            if (EN[j].hp <= 0) { EN[j].alive = false; bossDead = true; kills++; shake = 14; dropPickup(EN[j].x, EN[j].y + 6, 2); }
            done = true;
          }
        }
      }
      if (!done) {
        for (int j = 0; j < 14 && !done; j++) { // deux volants se percutent
          if (j == i || !EN[j].alive || EN[j].st != 98) continue;
          float ddx = EN[j].x - e->x, ddy = EN[j].y - e->y;
          if (ddx * ddx + ddy * ddy < 144) {
            e->hp--; EN[j].hp--;
            poof((e->x + EN[j].x) / 2, (e->y + EN[j].y) / 2, C_WHT, 8);
            if (e->hp <= 0) { e->alive = false; kills++; poof(e->x, e->y, C_PUR, 10); if (rnd(2)) dropPickup(e->x, e->y, -1); }
            else { e->st = 99; e->t = 0; }
            if (EN[j].hp <= 0) { EN[j].alive = false; kills++; poof(EN[j].x, EN[j].y, C_PUR, 10); if (rnd(2)) dropPickup(EN[j].x, EN[j].y, -1); }
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
    } else if (e->type >= 4) { // LE GARDIEN : saute puis frappe le sol
      if (e->st == 0 && e->t % (90 - chap * 6) == 0) { // chaque region : cycles plus presses
        if (chap == 6) { // L'OMBRE : elle se deplace pres de toi sans prevenir
          poof(e->x, e->y, C_PUR, 10);
          float a2 = (float)rnd(100) * 0.0628f;
          float nx2 = plx - cosf(a2) * 56.0f, ny2 = ply - sinf(a2) * 56.0f;
          if (nx2 > 24 && nx2 < SW - 24 && ny2 > TOPOFF + 24 && ny2 < TOPOFF + MH * TS - 24) { e->x = nx2; e->y = ny2; }
          poof(e->x, e->y, C_PUR, 10);
          float dx = plx - e->x, dy = ply - e->y;
          float d = sqrtf(dx * dx + dy * dy); if (d < 1) d = 1;
          e->vx = dx / d * 2.6f; e->vy = dy / d * 2.6f; e->st = 10;
        } else {
          float dx = plx - e->x, dy = ply - e->y;
          float d = sqrtf(dx * dx + dy * dy); if (d < 1) d = 1;
          float bs = 3.2f + chap * 0.25f; // charge de plus en plus vive
          e->vx = dx / d * bs; e->vy = dy / d * bs; e->st = 10;
        }
      }
      if (e->st >= 10) {
        e->st++;
        e->vx *= 0.92f; e->vy *= 0.92f;
        if (e->st == 34) { // FRAPPE : chaque GARDIEN frappe a sa maniere
          shake = 10; poof(e->x, e->y + 10, KDColor::RGB24(BSK[chap][3]), 14); // eclats aux couleurs du Gardien
          float dx = plx - e->x, dy = ply - e->y;
          float d = sqrtf(dx * dx + dy * dy); if (d < 1) d = 1;
          float ang = atan2f(dy, dx);
          float rv = 1.5f + chap * 0.15f;
          for (int k = 0; k < 6; k++) { // on remplit jusqu'a six projectiles
            int bb; for (bb = 0; bb < 6; bb++) if (!BU[bb].on) break;
            if (bb >= 6) break;
            float a2 = ang; float sv = rv; int nk = 2; // par defaut : le GOLEM, deux shards
            if (chap == 1) { a2 = (float)k * 1.047f; sv = rv * 0.7f; nk = 6; } // GIVRE : etoile de six eclats lents
            else if (chap == 2) { if (k >= 3) break; a2 = ang + (k - 1) * 0.35f; sv = rv * 1.5f; nk = 3; } // CENDRE : trois braises vives en chasse
            else if (chap == 3) { a2 = (k < 3) ? ang + (k - 1) * 0.3f : (float)(k - 3) * 1.5708f; nk = 6; } // MOUSSE : trois spores traqueuses + une croix
            else if (chap == 4) { if (k >= 5) break; a2 = ang + (k - 2) * 0.28f; sv = rv * 1.3f; nk = 5; } // SABLE : large eventail de cinq jets
            else if (chap == 5) { if (k >= 4) break; a2 = 0.7854f + (float)k * 1.5708f; sv = rv * 0.8f; nk = 4; } // ARGILE : quatre pesantes en diagonale
            else if (chap == 6) { if (k >= 2) break; a2 = ang + (k ? 0.9f : -0.9f); sv = rv * 1.2f; nk = 2; } // OMBRE : deux traits noirs qui traquent
            else if (chap == 7) { a2 = ang + (float)k * 1.047f; sv = rv * 1.2f; nk = 6; } // PRIME : couronne complete, six eclats
            else a2 = ang + (k ? 0.5f : -0.5f);
            (void)nk;
            BU[bb].x = e->x; BU[bb].y = e->y; BU[bb].vx = cosf(a2) * sv; BU[bb].vy = sinf(a2) * sv; BU[bb].fr = 0; BU[bb].on = true;
          }
        }
        if (e->st > 40) e->st = 0;
      }
      if (e->hp <= 8 && e->t % 240 == 120 && enN < 12) { // a mi-vie : appelle des limaces
        struct En *s = &EN[enN++];
        s->x = e->x + (rnd(40) - 20); s->y = e->y + (rnd(40) - 20);
        s->type = chap & 3; s->vx = s->vy = 0; // le Gardien appelle un monstre de sa region
        s->hp = 1; s->t = 0; s->st = 0; s->dir = 0; s->alive = true; s->flash = false;
      }
    }
    if (e->type != 1) { // les autres respectent les murs
      float nx = e->x + e->vx, ny = e->y + e->vy;
      float r = (e->type >= 4) ? 10 : 6;
      if (e->st != 99) { // etourdi : immobile
        if (walkable(nx, e->y, r)) e->x = nx; else e->vx = 0;
        if (walkable(e->x, ny, r)) e->y = ny; else e->vy = 0;
      }
      if (e->x < 6) e->x = 6;
      if (e->x > SW - 6) e->x = SW - 6;
      if (e->y < TOPOFF + 6) e->y = TOPOFF + 6;
      if (e->y > TOPOFF + MH * TS - 6) e->y = TOPOFF + MH * TS - 6;
    }
    // contact : le monstre blesse
    float er = (e->type >= 4) ? 13 : 7;
    if (e->st == 0 && plx > e->x - er - 6 && plx < e->x + er + 6 && ply > e->y - er - 6 && ply < e->y + er + 6)
      hurtPlayer(e->type >= 4 ? 2 : 1, e->x, e->y);
  }
  // projectiles (fr = renvoye par le souffle : blesse les monstres)
  for (int b = 0; b < 6; b++) if (BU[b].on) {
    BU[b].x += BU[b].vx; BU[b].y += BU[b].vy;
    int tx = (int)(BU[b].x / TS), ty = (int)((BU[b].y - TOPOFF) / TS);
    if (BU[b].x < 2 || BU[b].x > SW - 2 || BU[b].y < TOPOFF + 2 || BU[b].y > TOPOFF + MH * TS - 2 || solid(tx, ty)) { BU[b].on = false; continue; }
    if (BU[b].fr) { // rocher renvoye : cherche un monstre a percuter
      for (int i = 0; i < 14; i++) if (EN[i].alive) {
        float er2 = (EN[i].type >= 4) ? 16 : 8;
        if (BU[b].x > EN[i].x - er2 && BU[b].x < EN[i].x + er2 && BU[b].y > EN[i].y - er2 && BU[b].y < EN[i].y + er2) {
          BU[b].on = false;
          EN[i].hp -= 2 + pLvl / 2; // plus le souffle est fort, plus l'objet renvoye fait mal
          poof(BU[b].x, BU[b].y, C_YEL, 6);
          if (EN[i].type >= 4) shake = 5;
          if (EN[i].hp <= 0) {
            EN[i].alive = false; kills++;
            poof(EN[i].x, EN[i].y, C_PUR, 12);
            if (EN[i].type >= 4) { bossDead = true; shake = 14; dropPickup(EN[i].x, EN[i].y + 6, 2); }
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
  float rg = 16.0f + pLvl * 6.0f; if (rg > 62.0f) rg = 62.0f; if (hasEcho) rg = 80.0f; // la PIERRE D'ECHO porte encore plus loin
  float rr = rg * rg;
  float pf = 2.0f + pLvl * 0.45f; if (pf > 6.0f) pf = 6.0f; if (hasPlume) pf = 7.2f; // la PLUME force l'ouragan : au debut, on ne pousse pas loin
  // les monstres du cone partent en vol (le golem est trop lourd)
  for (int i = 0; i < 14; i++) {
    struct En *e = &EN[i];
    if (!e->alive || e->type >= 4) continue; // le Gardien est trop lourd pour le souffle
    float ex = e->x - plx, ey = e->y - ply;
    float d2 = ex * ex + ey * ey;
    if (d2 < 1 || d2 > rr) continue;
    float d = sqrtf(d2);
    if ((ex * dx + ey * dy) / d < 0.5f) continue; // hors de l'eventail large (120 degres)
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
    if ((bx * dx + by * dy) / d < 0.45f) continue;
    BU[b].fr = 1;
    int gi = -1;
    for (int i = 0; i < 14; i++) if (EN[i].alive && EN[i].type >= 4) { gi = i; break; }
    if (gi >= 0) {
      float gx = EN[gi].x - BU[b].x, gy = EN[gi].y - BU[b].y;
      float gd = sqrtf(gx * gx + gy * gy); if (gd < 1) gd = 1;
      BU[b].vx = gx / gd * 2.6f; BU[b].vy = gy / gd * 2.6f;
    } else { BU[b].vx = dx * 2.6f; BU[b].vy = dy * 2.6f; }
    poof(BU[b].x, BU[b].y, KDColor::RGB24(0x9AD8E8), 5);
  }
  // le souffle brise les pots et fait ceder les rochers fissures
  for (int pr = -1; pr <= 1; pr++) { // l'eventail balaie trois cases devant l'esprit
    float cx = plx + dx * 18 - dy * pr * 12, cy = ply + dy * 18 + dx * pr * 12;
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
  int fy = y - 1 + bob; // ancre du corps : la tete monte avec le bob
  fCirc(x, fy, 5, KDColor::RGB24(0x1E3A4A)); // silhouette : contour sombre qui lit la forme
  fCirc(x, fy - 1, 4, KDColor::RGB24(0x7AC8DC)); // corps de brume claire
  fRect(x - 4, fy, 8, 4, KDColor::RGB24(0x5AA8C0)); // nuage dense du bas
  px(x - 3, fy + 4, KDColor::RGB24(0x3A788C)); px(x - 1, fy + 4, KDColor::RGB24(0x5AA8C0)); px(x + 1, fy + 4, KDColor::RGB24(0x3A788C)); px(x + 3, fy + 4, KDColor::RGB24(0x5AA8C0)); // ourlet en volutes
  fRect(x - 3, fy - 3, 6, 4, KDColor::RGB24(0xD4F0FC)); // visage lumineux sous la capuche
  fRect(x - 1, fy - 6, 3, 3, KDColor::RGB24(0x7AC8DC)); // pointe de la capuche
  px(x, fy - 7, KDColor::RGB24(0xD4F0FC)); px(x, fy - 8, KDColor::RGB24(0x9AD8E8)); // plumeteau du sommet
  int ex = (pdir == 1) ? 1 : ((pdir == 3) ? -1 : 0);
  int ey = (pdir == 0) ? -1 : ((pdir == 2) ? 1 : 0);
  px(x - 2 + ex, fy - 1 + ey, C_WHT); px(x + 2 + ex, fy - 1 + ey, C_WHT); // yeux blancs
  px(x - 2 + ex, fy + ey, KDColor::RGB24(0x1E3A4A)); px(x + 2 + ex, fy + ey, KDColor::RGB24(0x1E3A4A)); // pupilles
  px(x - 1, fy - 2 + ey, KDColor::RGB24(0x9AD8E8)); px(x + 1, fy - 2 + ey, KDColor::RGB24(0x9AD8E8)); // reflets sous les yeux
  for (int k = 1; k <= 3; k++) { // queue en tourbillon derriere l'esprit
    int tx2 = x - (int)(dx * k * 4) + (int)(((frame >> 2) + k) % 3) - 1;
    int ty2 = fy + 3 - k * 2 + (int)(sinf((frame + k * 40) * 0.13f) * 2.0f);
    fCirc(tx2, ty2, 3 - k, KDColor::RGB24(k == 1 ? 0x7AC8DC : ((k == 2) ? 0x5AA8C0 : 0x3A788C)));
  }
  if (swing > 0) { // SOUFFLE EN EVENTAIL : trois arcs qui s'ouvrent devant l'esprit
    int wp = (12 - swing) * 2;
    for (int a = 1; a <= 3; a++) { // arcs concentriques de l'eventail, du coeur au bord
      int rr2 = wp + 2 + a * 4;
      KDColor ac = (a == 1) ? C_WHT : ((a == 2) ? C_CYN : KDColor::RGB24(0x7AC8DC));
      for (int k = -3; k <= 3; k++) { // chaque arc couvre +/- 45 degres
        float ang = k * 0.26f;
        float ca = cosf(ang), sa = sinf(ang);
        int ax = x + (int)(rr2 * (dx * ca - dy * sa));
        int ay = y + (int)(rr2 * (dy * ca + dx * sa));
        px(ax, ay, ac);
        if (a < 3) px(ax - (int)dx, ay - (int)dy, ac); // arc du coeur un peu epais
      }
    }
    for (int k = -1; k <= 1; k += 2) { // wisps qui filent sur les bords du vent
      int sp2 = (frame * 2) & 7;
      float ca = cosf(k * 0.5f), sa = sinf(k * 0.5f);
      int ax = x + (int)((wp + 4 + sp2) * (dx * ca - dy * sa));
      int ay = y + (int)((wp + 4 + sp2) * (dy * ca + dx * sa));
      px(ax, ay, C_WHT);
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
    } else if (e->type >= 4) { // LE GARDIEN : palette de sa region, fissure a mesure qu'il souffre
      int bt = e->type - 4;
      KDColor bD = KDColor::RGB24(BSK[bt][0]), bB = KDColor::RGB24(BSK[bt][1]), bL = KDColor::RGB24(BSK[bt][2]), bE = KDColor::RGB24(BSK[bt][3]), bM = KDColor::RGB24(BSK[bt][4]);
      int hop = (e->st >= 10 && e->st < 34) ? ((e->st < 20) ? -(e->st - 10) : -8 + (e->st - 20) / 2) : 0;
      fCirc(x + 1, y + 13, 13, KDColor::RGB24(0x2E2E38)); // grosse ombre
      int aw = (e->st == 34) ? 4 : 0; // les bras s'abattent au slam
      if (bt == 0) { // LE GOLEM : h colosse de pierre stratee, le classique
        fRect(x - 9, y + 4 + hop, 6, 9, bD); fRect(x + 3, y + 4 + hop, 6, 9, bD); // jambes
        fRect(x - 9, y + 11 + hop, 6, 2, bB); fRect(x + 3, y + 11 + hop, 6, 2, bB); // pieds
        fRect(x - 12, y - 14 + hop, 24, 20, bD);      // torse monumental
        fRect(x - 12, y - 14 + hop, 24, 2, bL); // epaules eclairees
        fRect(x - 10, y - 12 + hop, 20, 1, bB); fRect(x - 10, y - 7 + hop, 20, 1, bB); fRect(x - 10, y - 2 + hop, 20, 1, bB); // strates
        px(x - 7, y - 10 + hop, bB); px(x + 6, y - 9 + hop, bB); px(x + 8, y + 0 + hop, bD); px(x - 5, y + 1 + hop, bD);
        px(x - 11, y - 13 + hop, bM); px(x + 9, y - 13 + hop, bM); // mousse sur les epaules
        fRect(x - 8, y - 12 + hop, 16, 2, bB); // arcade sourcilleuse
        KDColor gl = ((e->t >> 3) & 1) ? bE : bL;
        fRect(x - 6, y - 9 + hop, 4, 3, gl); fRect(x + 2, y - 9 + hop, 4, 3, gl); // yeux ardents
        px(x - 4, y - 8 + hop, C_WHT); px(x + 3, y - 8 + hop, C_WHT);
        fRect(x - 3, y - 3 + hop, 6, 2, bB); px(x - 2, y - 2 + hop, bD); px(x + 2, y - 2 + hop, bD); // machoire
        fRect(x - 16, y - 8 + hop + aw, 5, 13, bD); fRect(x + 11, y - 8 + hop + aw, 5, 13, bD); // bras
        fRect(x - 16, y + 5 + hop + aw, 5, 3, bB); fRect(x + 11, y + 5 + hop + aw, 5, 3, bB); // poings
        if (aw && ((frame >> 2) & 1)) { fRect(x - 16, y + 8 + hop, 5, 1, bE); fRect(x + 11, y + 8 + hop, 5, 1, bE); } // impact du slam
      } else if (bt == 1) { // LE GIVRE : cristal taill en pointe, aretes nettes
        fRect(x - 8, y - 12 + hop, 16, 24, bD); // bloc de glace
        fRect(x - 6, y - 14 + hop, 12, 26, bB);
        fRect(x - 4, y - 16 + hop, 8, 30, bL); // coeur translucide
        fRect(x - 2, y - 18 + hop, 4, 3, bB); px(x, y - 19 + hop, C_WHT); // pointe
        fRect(x - 4, y - 12 + hop, 1, 12, bL); fRect(x + 3, y - 12 + hop, 1, 12, bL); // aretes verticales
        fRect(x - 6, y - 6 + hop, 12, 1, bD); // faille horizontale
        fRect(x - 12, y - 4 + hop, 5, 12, bB); fRect(x + 7, y - 4 + hop, 5, 12, bB); // epines laterales
        px(x - 14, y - 6 + hop, C_WHT); px(x + 14, y - 2 + hop, C_WHT); px(x - 13, y + 8 + hop, bL); // eclats qui volent
        fRect(x - 5, y - 9 + hop, 3, 2, ((e->t >> 3) & 1) ? bE : bL); fRect(x + 2, y - 9 + hop, 3, 2, ((e->t >> 3) & 1) ? bE : bL); // yeux givre
        fRect(x - 2, y - 2 + hop, 4, 1, bD); // bouche figee
      } else if (bt == 2) { // LA CENDRE : charbon vout, couronne de flammes
        fCirc(x, y - 2 + hop, 11, bD); // corps de charbon
        fCirc(x, y - 5 + hop, 9, bB);
        fRect(x - 6, y + 4 + hop, 12, 5, bD); // base craquelee
        for (int k = 0; k < 5; k++) { // la couronne de flammes danse
          int fx2 = x - 8 + k * 4, fh = 3 + ((frame + k * 3) & 3);
          px(fx2, y - 13 - fh + hop, C_ORG); px(fx2, y - 12 - fh + hop, C_RED); px(fx2, y - 11 - fh + hop, C_YEL);
        }
        fRect(x - 5, y - 4 + hop, 2, 8, bE); fRect(x + 2, y - 2 + hop, 2, 6, bE); // veines de braise au flanc
        fRect(x - 16, y - 2 + hop + aw, 4, 10, bD); fRect(x + 12, y - 2 + hop + aw, 4, 10, bD); // bras de charbon
        fRect(x - 5, y - 7 + hop, 3, 2, C_YEL); fRect(x + 2, y - 7 + hop, 3, 2, C_YEL); // yeux braise
        px(x - 4, y - 7 + hop, C_WHT); px(x + 3, y - 7 + hop, C_WHT);
      } else if (bt == 3) { // LA MOUSSE : motte vivante coiffee d'un champignon
        fCirc(x, y + 2 + hop, 12, bD); // la motte
        fCirc(x, y - 1 + hop, 11, bB);
        fCirc(x - 5, y - 6 + hop, 5, bL); fCirc(x + 6, y - 3 + hop, 4, bL); // touffes claires
        fRect(x - 3, y - 14 + hop, 8, 3, bE); // chapeau du champignon
        fRect(x - 1, y - 11 + hop, 3, 5, bL); // pied
        px(x, y - 13 + hop, C_WHT); px(x + 2, y - 12 + hop, C_WHT); px(x - 2, y - 12 + hop, C_WHT); // pois
        fRect(x - 12, y - 2 + hop, 2, 8, bL); fRect(x + 11, y - 4 + hop, 2, 10, bL); // lianes pendantes
        px(x - 11, y - 3 + hop, bL); px(x + 12, y - 5 + hop, bL);
        fRect(x - 5, y - 4 + hop, 3, 2, bE); fRect(x + 2, y - 4 + hop, 3, 2, bE); // yeux jaunes
        fRect(x - 2, y + 1 + hop, 5, 1, bD); // bouche humide
      } else if (bt == 4) { // LE SABLE : djinn a l'oeil unique, corps qui s'coule
        fCirc(x, y + 4 + hop, 13, bD); // la dune qui le porte
        fCirc(x, y + 4 + hop, 10, bB);
        fRect(x - 4, y - 12 + hop, 8, 16, bB); // torse qui se leve du sable
        fRect(x - 3, y - 10 + hop, 6, 14, bL);
        fRect(x - 2, y - 15 + hop, 5, 4, bL); // tete voilee
        px(x, y - 16 + hop, bM); px(x + 1, y - 15 + hop, bM); // bijou
        fRect(x - 9, y - 8 + hop, 6, 2, bD); fRect(x + 3, y - 6 + hop, 6, 2, bD); // bras ecouls
        fRect(x - 1, y - 8 + hop, 5, 2, ((e->t >> 3) & 1) ? bE : bD); // l'oeil unique
        px(x + 3, y - 7 + hop, C_WHT);
        for (int k = 0; k < 4; k++) { // le sable tourbillonne autour
          float a4 = (frame + k * 24) * 0.11f;
          px(x + (int)(cosf(a4) * 15), y + 3 + hop + (int)(sinf(a4) * 5), bL);
        }
      } else if (bt == 5) { // L'ARGILE : idole totem au visage grave, bras gregaires
        fRect(x - 10, y - 14 + hop, 20, 26, bD); // stle-corps
        fRect(x - 8, y - 12 + hop, 16, 22, bB);
        fRect(x - 10, y - 14 + hop, 20, 2, bL); fRect(x - 10, y + 10 + hop, 20, 2, bL); // socles
        fRect(x - 7, y - 11 + hop, 5, 1, bM); fRect(x + 2, y - 11 + hop, 5, 1, bM); // arcade gravee
        fRect(x - 6, y - 9 + hop, 4, 3, bE); fRect(x + 2, y - 9 + hop, 4, 3, bE); // yeux sculptes
        px(x - 5, y - 8 + hop, C_WHT); px(x + 3, y - 8 + hop, C_WHT);
        fRect(x - 1, y - 3 + hop, 3, 4, bM); // orifice de la bouche
        fRect(x - 16, y - 6 + hop + aw, 5, 14, bB); fRect(x + 11, y - 6 + hop + aw, 5, 14, bB); // bras jointifs
        fRect(x - 18, y - 6 + hop + aw, 2, 2, bL); fRect(x + 16, y - 6 + hop + aw, 2, 2, bL); // mains plates
        px(x, y - 16 + hop, bM); // la pierre de tete
      } else if (bt == 6) { // L'OMBRE : spectre aux yeux magenta, bas effiloch
        fCirc(x, y - 4 + hop, 11, bD); // fume dense
        fCirc(x, y - 6 + hop, 8, bB);
        fRect(x - 6, y + 2 + hop, 3, 9, bD); fRect(x - 1, y + 3 + hop, 3, 11, bB); fRect(x + 4, y + 2 + hop, 3, 8, bD); // bas effiloch
        fRect(x - 5, y - 8 + hop, 3, 3, bE); fRect(x + 2, y - 8 + hop, 3, 3, bE); // yeux magenta sans pupille
        fRect(x - 2, y - 3 + hop, 5, 1, bB); // sourire grave
        for (int k = 0; k < 3; k++) { // des wisps tournoient autour du spectre
          float a4 = (frame + k * 40) * 0.09f;
          px(x + (int)(cosf(a4) * 13), y - 4 + hop + (int)(sinf(a4) * 9), k ? bD : bL);
        }
        if (((frame >> 3) & 1)) { px(x - 10, y - 12 + hop, bE); px(x + 10, y - 10 + hop, bE); } // miasmes
      } else { // GOLEM PRIME : cuirasse d'or, coeur-etoile qui pulse
        fRect(x - 12, y - 12 + hop, 24, 24, bD); // cuirasse
        fRect(x - 10, y - 10 + hop, 20, 20, bB);
        fRect(x - 10, y - 12 + hop, 20, 2, bL); fRect(x - 10, y + 8 + hop, 20, 2, bL); // liserets
        fRect(x - 12, y - 15 + hop, 3, 3, bL); fRect(x + 9, y - 15 + hop, 3, 3, bL); // pointes d'paules
        int sr2 = 4 + (((frame >> 3) & 1) ? 2 : 0); // le coeur-etoile pulse
        fRect(x - 1, y + 1 + hop - sr2, 3, sr2 * 2, bM); fRect(x - sr2, y + hop, sr2 * 2, 3, bM);
        fRect(x - 2, y - 1 + hop, 5, 5, bM); px(x, y + hop, C_WHT);
        fRect(x - 6, y - 8 + hop, 4, 3, ((e->t >> 3) & 1) ? bE : bL); fRect(x + 2, y - 8 + hop, 4, 3, ((e->t >> 3) & 1) ? bE : bL); // yeux d'or
        px(x - 4, y - 7 + hop, C_WHT); px(x + 3, y - 7 + hop, C_WHT);
        fRect(x - 16, y - 8 + hop + aw, 4, 14, bD); fRect(x + 12, y - 8 + hop + aw, 4, 14, bB); // bras d'or
        fRect(x - 16, y + 6 + hop + aw, 4, 3, bL); fRect(x + 12, y + 6 + hop + aw, 4, 3, bL); // poings clairs
      }
      if (e->st >= 10 && e->st < 34) { // PREPARATION : chaque Gardien annonce sa frappe a sa facon
        int wu = e->st - 10; // 0..23, la montee
        if (bt == 0) { fRect(x - 16, y - 16 + hop - wu / 3, 5, 4, bL); fRect(x + 11, y - 16 + hop - wu / 3, 5, 4, bL); } // GOLEM : les poings montent
        else if (bt == 1) { for (int q = 0; q < 3; q++) { float aa = wu * 0.26f + q * 2.09f; px(x + (int)(cosf(aa) * (10 + wu / 2)), y - 6 + hop + (int)(sinf(aa) * (10 + wu / 2)), bE); } } // GIVRE : eclats en orbite
        else if (bt == 2) { int hr = 8 + wu; if (hr > 26) hr = 26; for (int q = 0; q < 12; q++) { float aa = q * 0.5236f; px(x + (int)(cosf(aa) * hr), y + hop + (int)(sinf(aa) * hr), ((q + wu) & 1) ? C_ORG : C_YEL); } } // CENDRE : halo de braise
        else if (bt == 3) { int infl = (wu / 6) & 1; fRect(x - 14 - infl, y - 2 + hop, 28 + infl * 2, 16, ((e->t >> 2) & 1) ? bB : bL); } // MOUSSE : se gonfle
        else if (bt == 4) { int sp = 4 + (wu & 3); fRect(x - sp, y - 10 + hop, sp * 2, 3, bE); fRect(x - sp, y + 6 + hop, sp * 2, 3, bE); } // SABLE : la colonne tournoie
        else if (bt == 5) { fRect(x - 18, y + 8 + hop - wu / 2, 5, 8, bL); fRect(x + 13, y + 8 + hop - wu / 2, 5, 8, bL); } // ARGILE : les mains se levent
        else if (bt == 6) { for (int q = 0; q < 4; q++) if (((frame >> 2) + q) & 1) px(x - 9 + q * 6, y - 14 + hop - (wu & 7), bM); } // OMBRE : vacillement
        else { for (int q = 0; q < 6; q++) { float aa = wu * 0.31f + q * 1.047f; px(x + (int)(cosf(aa) * 16), y + hop + (int)(sinf(aa) * 16), bE); } } // PRIME : rayons du coeur
      }
      int hpm = 16 + chap * 3 + ngPlus * 8;
      if (e->hp <= hpm * 13 / 16) { px(x - 5, y + 3 + hop, C_BLK); px(x - 4, y + 4 + hop, C_BLK); px(x + 5, y + 1 + hop, C_BLK); }
      if (e->hp <= hpm * 9 / 16) { fRect(x - 9, y - 10 + hop, 1, 6, C_BLK); px(x - 8, y - 5 + hop, C_BLK); fRect(x + 7, y - 4 + hop, 1, 7, C_BLK); px(x - 7, y + 8 + hop, C_BLK); px(x + 6, y + 9 + hop, C_BLK); }
      if (e->hp <= hpm * 5 / 16) { fRect(x - 12, y - 3 + hop, 8, 1, C_BLK); px(x - 4, y - 2 + hop, C_BLK); px(x - 3, y - 1 + hop, C_BLK); fRect(x + 9, y - 12 + hop, 1, 5, C_BLK); fRect(x + 8, y + 2 + hop, 3, 1, C_BLK); }
      if (e->hp <= hpm * 3 / 16) { fRect(x - 2, y - 10 + hop, 5, 1, C_BLK); fRect(x + 7, y + 6 + hop, 1, 5, C_BLK); px(x - 1, y - 7 + hop, C_BLK); px(x + 1, y - 7 + hop, C_BLK); }
      if (e->hp <= hpm * 5 / 16 && ((frame >> 3) & 1)) { px(x - 6, y - 4 + hop, bE); px(x + 6, y + 0 + hop, bE); px(x, y - 7 + hop, bE); px(x + 7, y + 8 + hop, bE); } // veines ardentes qui percent
    }
  }
  for (int b = 0; b < 6; b++) if (BU[b].on) { // rochers ardents en rotation
    int bx = (int)BU[b].x + ox, by = (int)BU[b].y + oy;
    int rr = (frame >> 2) & 3;
    fCirc(bx, by, 4, KDColor::RGB24(BSK[chap][0]));      // rocher brut, teinte de la region
    fCirc(bx - 1, by - 1, 2, KDColor::RGB24(BSK[chap][1])); // facette eclairee
    px(bx - 2, by + 1, C_ROCK2);
    fCirc(bx, by, 3, ((frame >> 3) & 1) ? C_ORG : C_RED);  // coeur ardent qui perce
    if (BU[b].fr) fCirc(bx, by, 2, C_YEL);                // renvoye par le souffle : coeur blanc
    px(bx - (int)(BU[b].vx * 2), by - (int)(BU[b].vy * 2), C_ORG);  // trainee de flammes en trois crans
    px(bx - (int)(BU[b].vx * 4), by - (int)(BU[b].vy * 4), C_RED);
    px(bx - (int)(BU[b].vx * 6), by - (int)(BU[b].vy * 6), ((frame >> 2) & 1) ? C_ORG : KDColor::RGB24(0x8A3A18));
    if (rr == 0) px(bx + 3, by - 3, C_YEL);                // etincelle arrachee
  }
  for (int i = 0; i < 50; i++) if (CNON[i] && CN[i][0] == room) { // pieces semees dans cette salle
    int x = CN[i][1] * TS + 8 + ox, y = TOPOFF + CN[i][2] * TS + 8 + oy + (((frame >> 3) & 1) ? 1 : 0);
    int sp = (frame >> 3) & 3;
    fRect(x - 3, y - 3, 6, 6, C_GOLD);
    fRect(x - 1 - (sp >> 1), y - 2, 2, 4, KDColor::RGB24(0xB8962A));
    if (sp == 0) px(x - 1, y - 1, C_WHT);
    if (sp == 2) px(x - 3, y - 3, C_WHT);
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
  { // NIVEAU DU SOUFFLE : il grandit a chaque monstre emporte par le vent
    char lb[6]; snprintf(lb, 6, "N%d", pLvl);
    drawTextSmall(10, 17, lb, C_CYN);
    fRect(28, 17, 30, 3, KDColor::RGB24(0x22364A));
    fRect(28, 17, (pXp * 30) / (pLvl * 8), 3, C_CYN); // l'experience jusqu'au prochain niveau
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
  { // chrono discret, pour les chasseurs de record
    int s2 = (int)(playT / 62);
    char tb2[10]; snprintf(tb2, 10, "%d:%d", s2 / 60, s2 % 60);
    drawTextSmall(100, 10, tb2, KDColor::RGB24(0x6A7590));
  }
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
  // MINI-CARTE : brouillard de guerre, seule l'explore se dessine, la forme varie par region
  fRect(268, 6, 32, 22, KDColor::RGB24(0x0C0E16));
  {
    int mnx = 99, mny = 99, mxx = -99, mxy = -99;
    for (int r2 = 0; r2 < 18; r2++) if (visited[r2] || r2 == room) {
      if (MPX[r2] < mnx) mnx = MPX[r2]; if (MPY[r2] < mny) mny = MPY[r2];
      if (MPX[r2] > mxx) mxx = MPX[r2]; if (MPY[r2] > mxy) mxy = MPY[r2];
    }
    if (mxx >= mnx) { // l'inconnu reste cache : le monde n'existe pas tant qu'on n'y est pas alle
      int cs2 = 3;
      while ((mxx - mnx + 1) * cs2 > 30 || (mxy - mny + 1) * cs2 > 20) cs2--;
      int ox3 = 270 + (30 - (mxx - mnx + 1) * cs2) / 2, oy3 = 7 + (20 - (mxy - mny + 1) * cs2) / 2;
      for (int r2 = 0; r2 < 18; r2++) {
        if (!visited[r2] && r2 != room) continue;
        int mx = ox3 + (MPX[r2] - mnx) * cs2, my = oy3 + (MPY[r2] - mny) * cs2;
        if (r2 == room) fRect(mx, my, cs2, cs2, ((frame >> 4) & 1) ? C_YEL : C_WHT); // ou tu es
        else fRect(mx, my, cs2 - 1, cs2 - 1, isDun(r2) ? C_PUR : KDColor::RGB24(0x6A7590)); // ou tu es alle
      }
    }
  }
  // barre de vie du GARDIEN de la region
  if (room == 11 && !bossDead && EN[0].alive && EN[0].type >= 4) {
    float bh = 16.0f + chap * 3 + ngPlus * 8;
    fRect(88, 29, 144, 10, KDColor::RGB24(0x0C0E16));
    fRect(90, 30, 140, 8, C_BLK);
    drawTextSmall(92, 31, BN[chap], C_WHT);
    fRect(90, 30, 140, 1, KDColor::RGB24(0x3A1A1A));
    fRect(92, 32, (int)(136.0f * EN[0].hp / bh), 4, C_RED);
    fRect(92, 32, (int)(136.0f * EN[0].hp / bh), 1, KDColor::RGB24(0xF08080));
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
    steleRead |= (1u << room); // pour le bilan final
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
      else if (chap > 0) startDlg(DLG_ANC_R[chap], 1); // chaque region, une parole nouvelle
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
      else if (chap > 0) startDlg(DLG_ERM_R[chap], 1); // il parle du Gardien en place
      else startDlg(DLG_ERMITE, 4);
    }
    return true;
  }
  return false;
}

// ---------------- Boucle de jeu ----------------
static void playTick() {
  playT += 2; // deux pas par frame : meme a bas fps, le chrono reste proche du temps reel
  if (kills > pKills0) { // chaque monstre emporte par le vent fait grandir le souffle
    pXp += kills - pKills0; pKills0 = kills;
    if (pXp >= pLvl * 8) { pXp -= pLvl * 8; pLvl++; poof(plx, ply - 12, C_YEL, 14); poof(plx, ply - 12, C_WHT, 8); } // NIVEAU SUP
  }
  // ----- dialogue en cours : on avance, le jeu est en pause -----
  if (dlgOn) {
    dlgTick();
    return;
  }
  if (chapGo && !dlgOn) { // l'eclat est en poche : la region suivante appelle
    chapGo = false;
    mode = (chap >= 7) ? 4 : 5; // 5 : region suivante ; 4 : le coeur, la fin
    return;
  }
  pollKeys();
  if (kHome) { saveGame(); mode = 0; return; } // retour au titre : on sauvegarde d'abord
  // ----- mouvement -----
  float ox = plx, oy = ply;
  float sp = 2.3f; // le vent va vite : 2.3 pixels par frame au lieu de 1.5
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
  int nb2; // le plan de la region decide des voisins : plus de rectangle obligatoire
  if (plx < 8 && (nb2 = NB[room][0]) < 18) { room = nb2; plx = SW - 12; spawnEnemies(); }
  else if (plx > SW - 8 && (nb2 = NB[room][1]) < 18) { room = nb2; plx = 12; spawnEnemies(); }
  else if (ply < TOPOFF + 8 && (nb2 = NB[room][2]) < 18) { room = nb2; ply = TOPOFF + MH * TS - 12; spawnEnemies(); }
  else if (ply > TOPOFF + MH * TS - 8 && (nb2 = NB[room][3]) < 18) { room = nb2; ply = TOPOFF + 12; spawnEnemies(); }
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
  for (int i = 0; i < 50; i++) if (CNON[i] && CN[i][0] == room) { // piece semees au sol
    float cx = CN[i][1] * TS + 8.0f, cy = TOPOFF + CN[i][2] * TS + 8.0f;
    if (cx > plx - 8 && cx < plx + 8 && cy > ply - 8 && cy < ply + 8) {
      pcoin += 5; CNON[i] = 0; poof(cx, cy, C_YEL, 8);
    }
  }
  for (int i = 0; i < 8; i++) if (PK[i].on) {
    if (PK[i].x > plx - 8 && PK[i].x < plx + 8 && PK[i].y > ply - 8 && PK[i].y < ply + 8) {
      if (PK[i].t == 0) { if (php < pmax) php += 2; if (php > pmax) php = pmax; }
      else if (PK[i].t == 1) pcoin += 5;
      else { // UN ECLAT DU GARDIEN
        hasStar = true;
        chapGo = true;
        if (chap < 7) startDlg(DLG_ECLATS[chap], 2); // le septieme eclat declenche la fin
        else startDlg(DLG_ETOILE, 3);
      }
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
  } else if (chap == 1) { // REGION DU GIVRE : la neige tombe en silence
    for (int i = 0; i < 14; i++) {
      int sx2 = ((i * 53 + frame * ((i & 1) ? 2 : 3)) % (SW + 10)) - 5 + ox;
      int sy2 = TOPOFF + ((i * 89 + frame * ((i & 2) ? 2 : 4)) % (MH * TS)) + oy;
      px(sx2, sy2, (i & 3) ? KDColor::RGB24(0xE8F0FA) : KDColor::RGB24(0xB8C8DC));
      int sy3 = TOPOFF + ((i * 89 + 60 + frame * ((i & 2) ? 2 : 4)) % (MH * TS)) + oy;
      px(sx2 - 4, sy3, KDColor::RGB24(0xC8D8E8));
    }
  } else if (chap == 2) { // REGION DE LA CENDRE : cendres grises, braises montantes
    for (int i = 0; i < 10; i++) { // cendres qui tombent lentement
      int sx2 = ((i * 61 + frame * 2) % (SW + 8)) - 4 + ox;
      int sy2 = TOPOFF + ((i * 47 + frame * 3) % (MH * TS)) + oy;
      px(sx2, sy2, KDColor::RGB24(0x6A5A50));
      px(sx2 + 2, TOPOFF + ((i * 47 + 90 + frame * 3) % (MH * TS)) + oy, KDColor::RGB24(0x5A4A42));
    }
    for (int i = 0; i < 6; i++) { // braises qui montent et s'eteignent
      int bx3 = 20 + ((i * 71 + frame) % (SW - 40)) + ox;
      int by3 = TOPOFF + (MH * TS) - ((i * 37 + frame * 4) % (MH * TS)) + oy;
      if (((frame >> 2) + i) & 3) px(bx3, by3, C_ORG); else px(bx3, by3, C_RED);
      px(bx3, by3 + 3, KDColor::RGB24(0x6A3020));
    }
  } else if (chap == 3) { // REGION DE LA MOUSSE : spores lumineuses, petals blancs
    for (int i = 0; i < 10; i++) { // spores qui derivent en tournant
      int sx2 = ((i * 67 + frame) % (SW + 20)) - 10 + ox;
      int sy2 = TOPOFF + ((i * 53 + (frame >> 1) * ((i & 1) ? 1 : -1)) % (MH * TS)) + oy;
      if (((frame >> 3) + i) & 3) px(sx2, sy2, KDColor::RGB24(0xB0E890)); else px(sx2, sy2, KDColor::RGB24(0x7ACE6A));
    }
    for (int i = 0; i < 5; i++) { // petals blancs qui tombent
      int px3 = ((i * 89 + frame * 2) % (SW + 10)) - 5 + ox;
      int py3 = TOPOFF + ((i * 71 + frame * 2) % (MH * TS)) + oy;
      px(px3, py3, C_WHT); px(px3, py3 + 1, KDColor::RGB24(0xE8C8D8));
    }
  } else if (chap == 4) { // REGION DU SABLE : rideaux de vent, herbes roulantes
    for (int i = 0; i < 3; i++) { // rideaux de sable qui traversent l'ecran
      int ry2 = TOPOFF + 24 + i * 56;
      for (int k = 0; k < 26; k++) {
        int sx2 = ((k * 13 + i * 37 + frame * 4) % (SW + 20)) - 10 + ox;
        px(sx2, ry2 + ((k + frame) & 3), KDColor::RGB24(0xE8CC94));
      }
    }
    for (int i = 0; i < 2; i++) { // herbes roulantes poussees par le vent
      int bx3 = ((i * 97 + frame * 5) % (SW + 30)) - 15 + ox;
      int by3 = TOPOFF + MH * TS - 14 + oy;
      fCirc(bx3, by3, 3, KDColor::RGB24(0xA88850)); px(bx3 + 2, by3 - 2, KDColor::RGB24(0xC8A868));
    }
  } else if (chap == 5) { // REGION DE L'ARGILE : poussiere d'atelier, eclats de poterie
    for (int i = 0; i < 8; i++) { // poussiere ocre en suspension
      int sx2 = ((i * 73 + (frame >> 1)) % (SW + 16)) - 8 + ox;
      int sy2 = TOPOFF + ((i * 59 + (frame >> 2) * ((i & 1) ? 1 : -1)) % (MH * TS)) + oy;
      px(sx2, sy2, KDColor::RGB24(0xD8A878));
    }
    for (int i = 0; i < 4; i++) { // eclats de poterie qui glissent au sol
      int ex2 = ((i * 83 + frame * 3) % (SW + 12)) - 6 + ox;
      int ey2 = TOPOFF + MH * TS - 6 + oy;
      px(ex2, ey2, KDColor::RGB24(0xB06840)); px(ex2 + 1, ey2, KDColor::RGB24(0xC98050));
    }
  } else if (chap == 6) { // REGION DE L'OMBRE : wisps violets, suie qui tombe
    for (int i = 0; i < 6; i++) { // wisps qui derivent comme des ames
      int sx2 = 20 + ((i * 79 + (int)(sinf(frame * 0.02f + i * 1.3f) * 40.0f)) % (SW - 40)) + ox;
      int sy2 = TOPOFF + 10 + ((i * 61 + (frame >> 1) * ((i & 1) ? 1 : -1)) % (MH * TS - 20)) + oy;
      if (((frame >> 3) + i) & 3) { px(sx2, sy2, C_PUR); px(sx2, sy2 - 1, C_MAG); } else px(sx2, sy2, KDColor::RGB24(0x4A3E6A));
    }
    for (int i = 0; i < 6; i++) { // suie qui tombe des voutes
      int sx2 = ((i * 67 + frame) % (SW + 10)) - 5 + ox;
      int sy2 = TOPOFF + ((i * 71 + frame * 2) % (MH * TS)) + oy;
      px(sx2, sy2, KDColor::RGB24(0x1A1626));
    }
  } else if (chap == 7) { // LA DERNIERE REGION : poussiere d'or, braises d'etoile
    for (int i = 0; i < 8; i++) { // poussiere d'or en suspension
      int sx2 = ((i * 71 + (frame >> 1)) % (SW + 16)) - 8 + ox;
      int sy2 = TOPOFF + ((i * 57 + (frame >> 2) * ((i & 1) ? 1 : -1)) % (MH * TS)) + oy;
      px(sx2, sy2, ((frame >> 3) & 1) ? C_YEL : KDColor::RGB24(0x8A7A3A));
    }
    for (int i = 0; i < 5; i++) { // braises d'etoile qui montent du fond du donjon
      int bx3 = 20 + ((i * 83 + frame) % (SW - 40)) + ox;
      int by3 = TOPOFF + MH * TS - ((i * 41 + frame * 3) % (MH * TS)) + oy;
      px(bx3, by3, C_YEL); px(bx3, by3 + 2, KDColor::RGB24(0x8A6A20));
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
static void statsScreen() { // le bilan de l'aventure : stats puis medailles
  waitRelease();
  int pg = 0;
  while (true) {
    pollKeys();
    if (keyOK) { waitRelease(); pg++; if (pg >= 2) return; }
    fRect(0, 0, SW, SH, KDColor::RGB24(0x0A0E1A));
    starfield(frame++);
    fRect(46, 58, SW - 92, 130, KDColor::RGB24(0x101626));
    fRect(46, 58, SW - 92, 1, C_YEL); fRect(46, 187, SW - 92, 1, C_YEL);
    if (pg == 0) {
      drawTextC(68, "LE BILAN DE TON AVENTURE", C_YEL);
      int sec = (int)((playT * 32u) / 1000u);
      char tb[24]; snprintf(tb, 24, "TEMPS : %d MIN %d S", sec / 60, sec % 60);
      drawTextC(90, tb, C_WHT);
      char mb[24]; snprintf(mb, 24, "MORTS : %d", deaths);
      drawTextC(104, mb, (deaths == 0) ? C_YEL : C_WHT);
      int rel = (hasFlask ? 1 : 0) + (hasEcho ? 1 : 0) + (hasPlume ? 1 : 0);
      char rb[24]; snprintf(rb, 24, "RELIQUES : %d/3", rel);
      drawTextC(118, rb, (rel == 3) ? C_YEL : C_WHT);
      int st = ((steleRead >> 3) & 1) + ((steleRead >> 7) & 1) + ((steleRead >> 10) & 1) + ((steleRead >> 11) & 1);
      char sb[24]; snprintf(sb, 24, "STELES LUES : %d/4", st);
      drawTextC(132, sb, (st == 4) ? C_YEL : C_WHT);
      int ch = 0; for (int i = 0; i < 18; i++) if (chestOpen[i]) ch++;
      char cb2[24]; snprintf(cb2, 24, "COFFRES : %d/8", ch);
      drawTextC(146, cb2, (ch == 8) ? C_YEL : C_WHT);
      int vr = 0; for (int i = 0; i < 18; i++) if (visited[i]) vr++;
      char vb[24]; snprintf(vb, 24, "SALLES EXPLOREES : %d/18", vr);
      drawTextC(160, vb, (vr == 18) ? C_YEL : C_WHT);
      char kb[24]; snprintf(kb, 24, "MONSTRES SOUFFLES : %d", kills);
      drawTextC(174, kb, C_WHT);
      if ((frame >> 4) & 1) drawTextC(204, "OK : LES MEDAILLES", C_GRE);
    } else if (pg == 1) { // les medailles de la session
      drawTextC(68, "LES MEDAILLES DU VENT", C_YEL);
      for (int m = 0; m < 7; m++) {
        int y = 88 + m * 15;
        const char *nm;
        if (m == 0) nm = "SOUFFLE PUR";
        else if (m == 1) nm = "SAGE DU ROYAUME";
        else if (m == 2) nm = "EXPLORATEUR";
        else if (m == 3) nm = "CHASSEUR";
        else if (m == 4) nm = "ECLAIR";
        else if (m == 5) nm = "COEUR NOIR";
        else nm = "LEGENDE DU VENT";
        bool ok = (medals >> m) & 1;
        drawTextSmall(52, y, nm, ok ? C_YEL : KDColor::RGB24(0x6A7590));
        if (m == 3) { char pb[14]; snprintf(pb, 14, "%d/100", (int)killsS); drawTextSmall(252, y, pb, ok ? C_YEL : KDColor::RGB24(0x6A7590)); }
        else drawTextSmall(252, y, ok ? "GAGNEE" : "A FAIRE", ok ? C_YEL : KDColor::RGB24(0x6A7590));
      }
      if ((frame >> 4) & 1) drawTextC(204, "OK : RETOUR AU TITRE", C_GRE);
    }
    blit(); Ion::Timing::msleep(40);
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
    if (bestT) { char rb3[28]; int s3 = (int)(bestT / 62); snprintf(rb3, 28, "RECORD SESSION : %d MIN %d S", s3 / 60, s3 % 60); drawTextC(150, rb3, C_GRE); }
    if (ngPlus) drawTextC(161, "ROYAUME NOIR : LE MONDE SE LEVE PLUS LOURD", C_RED);
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
  int g = GAT[r]; (void)g; // le plan de la region (NB) remplace la grille fixe
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
  // STYLE DE LA SALLE : quatre ecritures differentes, tirees au sort
  int stl = rnd(4);
  if (stl == 1) { // salle en rangees : deux haies ou murets avec des trous
    for (int tx = 2; tx < MW - 2; tx++) {
      if ((tx & 3) != 3 && M[4][tx] == base) M[4][tx] = wall;
      if ((tx & 3) != 1 && M[7][tx] == base) M[7][tx] = wall;
    }
  } else if (stl == 2) { // salle en patios : quatre blocs poses en quincunx
    for (int j = 0; j < 4; j++) {
      int bx2 = 3 + (j & 1) * 11, by2 = 3 + (j >> 1) * 5;
      for (int a = 0; a < 2; a++) for (int b = 0; b < 2; b++)
        if (M[by2 + b][bx2 + a] == base) M[by2 + b][bx2 + a] = wall;
    }
  } else if (stl == 3) { // salle en travers : deux murailles en diagonale
    for (int k = 0; k < 7; k++) {
      if (2 + k < MH - 1 && 4 + k < MW - 1 && (k & 1) && M[2 + k][4 + k] == base) M[2 + k][4 + k] = wall;
      if (2 + k < MH - 1 && 14 - k > 0 && (k & 1) && M[2 + k][14 - k] == base) M[2 + k][14 - k] = wall;
    }
  }
  // croisement garanti : toutes les ouvertures restent reliees
  for (int ty = 1; ty < MH - 1; ty++) { M[ty][9] = base; M[ty][10] = base; }
  for (int tx = 1; tx < MW - 1; tx++) { M[5][tx] = base; M[6][tx] = base; }
  if (NB[r][2] < 18) { M[0][9] = base; M[0][10] = base; } else { M[0][9] = wall; M[0][10] = wall; }
  if (NB[r][3] < 18) { M[11][9] = base; M[11][10] = base; } else { M[11][9] = wall; M[11][10] = wall; }
  if (NB[r][1] < 18) { M[5][19] = base; M[6][19] = base; } else { M[5][19] = wall; M[6][19] = wall; }
  if (NB[r][0] < 18) { M[5][0] = base; M[6][0] = base; } else { M[5][0] = wall; M[6][0] = wall; }
  if (NB[r][3] == 11) { M[11][9] = 'D'; M[11][10] = 'D'; } // la porte scellee du boss, ou qu'elle soit
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

static void fixLanes(int r) { // les ouvertures des salles fixes suivent le plan de la region
  bool dn = isDun(r);
  char base = dn ? ',' : '.', wall = dn ? '#' : 't';
  char (*M)[21] = RMO[r];
  if (NB[r][2] < 18) { M[0][9] = base; M[0][10] = base; } else { M[0][9] = wall; M[0][10] = wall; }
  if (NB[r][3] < 18) { M[11][9] = base; M[11][10] = base; } else { M[11][9] = wall; M[11][10] = wall; }
  if (NB[r][1] < 18) { M[5][19] = base; M[6][19] = base; } else { M[5][19] = wall; M[6][19] = wall; }
  if (NB[r][0] < 18) { M[5][0] = base; M[6][0] = base; } else { M[5][0] = wall; M[6][0] = wall; }
  if (NB[r][3] == 11) { M[11][9] = 'D'; M[11][10] = 'D'; } // la porte scellee du boss, ou qu'elle soit
}
static void fillWorld() { // genere les 18 salles d'une region et pose les monstres
  buildLayout(); // d'abord le plan : qui touche qui, ou chaque salle se dessine
  for (int r = 0; r < 18; r++) { // salles procedurales, sauf la grotte (3), le village (7) et l'arene (11)
    if (r == 3 || r == 7 || r == 11) {
      for (int ty = 0; ty < 12; ty++) { memcpy(RMO[r][ty], RM[r][ty], 20); RMO[r][ty][20] = 0; }
      fixLanes(r); // leurs ouvertures suivent le plan, elles aussi
    } else genRoom(r);
  }
  for (int r = 0; r < 18; r++) { // placement des monstres dans le monde genere
    ESWN[r] = 0;
    for (int k = 0; k < EDEN[r] + ngPlus; k++) { // ROYAUME NOIR : un monstre de plus par salle
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
  // PNJ : dans chaque region, chacun trouve une nouvelle place (mais jamais sur un mur)
  for (int n = 0; n < 5; n++) { // 5 PNJ : on les sort de leurs habitudes
    if (NPC[n][0] > 17) break; // sentinelle
    int r = NPC[n][0];
    for (int tries = 0; tries < 60; tries++) {
      int tx = 1 + rnd(MW - 2), ty = 1 + rnd(MH - 2);
      char c = RMO[r][ty][tx];
      if (c != '.' && c != ',' && c != 'f' && c != 'p') continue;
      if (tx >= 9 && tx <= 11) continue; // on laisse le passage du milieu
      bool near = false; // pas colles l'un a l'autre
      for (int m = 0; m < n && !near; m++)
        if (NPC[m][0] == r && abs(NPC[m][1] - tx) + abs(NPC[m][2] - ty) < 3) near = true;
      if (near) continue;
      NPC[n][1] = tx; NPC[n][2] = ty; break; // nouvelle adresse
    }
  }
  // PIECES PAR REGION : 40 sur la premiere carte, 50 ensuite
  {
    int goal = (chap == 0) ? 40 : 50, placed = 0;
    for (int tries = 0; tries < 900 && placed < goal; tries++) {
      int r = -1;
      for (int q = 0; q < 40 && r < 0; q++) { int cand = rnd(18); if (MPX[cand] != -99) r = cand; }
      if (r < 0) break; // pas de salle libre (impossible, mais on ne sait jamais)
      int tx = 1 + rnd(MW - 2), ty = 1 + rnd(MH - 2);
      char c = RMO[r][ty][tx];
      if (c != '.' && c != ',' && c != 'f' && c != 'p') continue;
      if (tx >= 9 && tx <= 11) continue; // pas pile sur la route principale
      bool dup = false; // une seule piece par case
      for (int q = 0; q < placed && !dup; q++)
        if (CN[q][0] == (uint8_t)r && CN[q][1] == (uint8_t)tx && CN[q][2] == (uint8_t)ty) dup = true;
      if (dup) continue;
      CN[placed][0] = r; CN[placed][1] = tx; CN[placed][2] = ty;
      CNON[placed] = 1; placed++;
    }
    while (placed < 50) { CN[placed][0] = 255; CNON[placed] = 0; placed++; } // reliquat inutilise
  }
  spawnEnemies();
}

// ---------------- Nouvelle partie ----------------
static void newGame() {
  seed = (uint16_t)(titleSc * 7919u + 1u); // grain du titre : chaque monde est neuf
  fadePos = 0; // le monde se reveille depuis le noir
  room = 7; // le village
  plx = 160; ply = TOPOFF + 96;
  pdir = 2; swing = 0; pifr = 0; brth = 5; brthT = 0; pvx = pvy = 0;
  pLvl = 1; pXp = 0; pKills0 = 0; // le souffle repart tout neuf
  php = 6; pmax = 6; pcoin = 0; pkey = 0;
  hasStar = false; bossDead = false; gameDone = false;
  hasFlask = false; hasEcho = false; hasPlume = false;
  playT = 0; deaths = 0; steleRead = 0; kills = 0; ngRun = ngPlus; chap = 0; chapGo = false; // la premiere des huit regions
  for (int i = 0; i < 18; i++) { chestOpen[i] = 0; doorOpen[i] = 0; visited[i] = (i == 7); }
  for (int i = 0; i < 36; i++) PT[i].life = 0;
  dlgOn = false; dlgEnd = false;
  fillWorld();
}
static void regenChapter() { // la region suivante : meme royaume, autre monde, autre Gardien
  seed = (uint16_t)(seed * 7919u + chap * 131u + 17u); // chaque region a sa graine
  fadePos = 0; // le monde se rematerialise
  room = 7; plx = 160; ply = TOPOFF + 96;
  hasStar = false; bossDead = false;
  for (int i = 0; i < 18; i++) { chestOpen[i] = 0; doorOpen[i] = 0; visited[i] = (i == 7); }
  dlgOn = false; dlgEnd = false;
  fillWorld();
}

// ---------------- Reprendre la partie sauvegardee ----------------
static void applySave() { // CONTINUER : tout remonte, jusqu'au pot que tu as brise
  newGame();
  int32_t v; svGet(8, &v, 4); chap = v; svGet(12, &v, 4); medals = v; svGet(16, &v, 4); ngPlus = v;
  uint32_t sd; svGet(4, &sd, 4); seed = (uint16_t)sd;
  buildLayout(); // le plan de la region se redessine exactement comme il etait
  svGet(20, &v, 4); room = v; svGet(24, &v, 4); php = v; svGet(28, &v, 4); pmax = v;
  svGet(32, &v, 4); pcoin = v; svGet(36, &v, 4); pkey = v; svGet(40, &v, 4); deaths = v;
  svGet(76, &v, 4); runsDone = v;
  svGet(4958, &v, 4); pLvl = v; if (pLvl < 1) pLvl = 1; svGet(4962, &v, 4); pXp = v; // le souffle a garde son niveau
  uint32_t u; svGet(44, &u, 4); playT = u; svGet(48, &u, 4); killsS = u; svGet(52, &u, 4); bestT = u;
  svGet(56, &u, 4); kills = (int)u; pKills0 = kills; svGet(60, &u, 4); steleRead = u;
  float px2, py2; svGet(64, &px2, 4); svGet(68, &py2, 4); plx = px2; ply = py2; // l'endroit exact ou tu etais
  uint32_t fl2; svGet(72, &fl2, 4);
  hasStar = fl2 & 1; hasFlask = fl2 & 2; hasEcho = fl2 & 4; hasPlume = fl2 & 8;
  bossDead = fl2 & 16; chapGo = fl2 & 32; ngRun = fl2 & 64;
  svGet(80, chestOpen, 18); svGet(98, doorOpen, 18); svGet(116, visited, 18);
  svGet(134, ESWN, 18); svGet(152, ESW, 270);
  svGet(422, RMO, 4536); // la carte telle que tu l'avais laissee
  svGet(4966, NPC, 24); // les PNJ etaient ici, pas ailleurs
  svGet(4990, CN, 150); svGet(5140, CNON, 50); // les pieces deja ramassees le restent
  fadePos = 0; pifr = 0; swing = 0; pvx = pvy = 0; dlgOn = false; dlgEnd = false; chapGo = (bool)(fl2 & 32);
  spawnEnemies();
}
static int menuScreen() { // 0 : CONTINUER (s'il y a une sauvegarde) ; 1 : nouvelle partie ; -1 : retour titre
  waitRelease();
  int sel = 0;
  while (true) {
    pollKeys();
    if (kHome) { waitRelease(); return -1; }
    if (savOk && (kUp || kDown)) { waitRelease(); sel = 1 - sel; }
    if (keyOK) { waitRelease(); return sel; }
    fRect(0, 0, SW, SH, KDColor::RGB24(0x0A0E1A));
    starfield(frame++);
    drawTextC(80, "LE ROYAUME OUBLIE", C_YEL);
    if (savOk) {
      int32_t rc; svGet(8, &rc, 4);
      char cb[30]; snprintf(cb, 30, "CONTINUER - REGION %d", (int)rc + 1);
      drawTextC(110, cb, sel == 0 ? C_WHT : KDColor::RGB24(0x6A7590));
      drawTextC(126, "NOUVELLE PARTIE", sel == 1 ? C_WHT : KDColor::RGB24(0x6A7590));
      px(96, 112 + sel * 16, C_YEL); px(95, 112 + sel * 16, C_YEL); px(97, 112 + sel * 16, C_YEL); // fleche
    } else {
      drawTextC(110, "NOUVELLE PARTIE", C_WHT);
      px(96, 112, C_YEL); px(95, 112, C_YEL); px(97, 112, C_YEL); // fleche
    }
    blit(); Ion::Timing::msleep(40);
  }
}

// ---------------- Boucle principale ----------------
void ion_main(int argc, char *argv[]) {
  (void)argc; (void)argv;
  while (true) {
    mode = 0;
    titleScreen();
    loadGame(); // on regarde si l'atelier garde une sauvegarde
    int ms2 = menuScreen();
    if (ms2 < 0) continue; // HOME au menu : retour au titre
    if (savOk && ms2 == 0) { // CONTINUER : la sauvegarde remonte toute seule, rien a taper
      applySave();
      if (chap > 0) pagesScreen(CHAPI[chap], 3, "CONTINUER L'AVENTURE");
      else pagesScreen(INTRO, 7, "COMMENCER L'AVENTURE");
    } else { // NOUVELLE PARTIE (l'ancienne sauvegarde reste jusqu'a la prochaine)
      newGame();
      pagesScreen(INTRO, 7, "COMMENCER L'AVENTURE");
    }
    mode = 2;
    while (true) {
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
      if (mode == 3) { // defaite : ranime au village de la region
        gameOverScreen();
        php = pmax; plx = 160; ply = TOPOFF + 96; room = 7; pifr = 0; fadePos = 0;
        spawnEnemies();
        saveGame(); // les morts comptent, la sauvegarde aussi
        mode = 2;
        continue;
      }
      if (mode == 5) { // eclat pris : la region suivante appelle
        chap++;
        regenChapter();
        pagesScreen(CHAPI[chap], 3, "CONTINUER L'AVENTURE");
        saveGame(); // la sauvegarde suit l'aventure, automatiquement
        mode = 2;
        continue;
      }
      break; // mode 4 : le coeur, la fin ; ou HOME : retour au titre
    }
    if (mode == 4) { // fin d'une vraie partie : medailles, record, et le Royaume Noir se prepare
      int rel3 = (hasFlask ? 1 : 0) + (hasEcho ? 1 : 0) + (hasPlume ? 1 : 0);
      int st3 = ((steleRead >> 3) & 1) + ((steleRead >> 7) & 1) + ((steleRead >> 10) & 1) + ((steleRead >> 11) & 1);
      int vr3 = 0; for (int i = 0; i < 18; i++) if (visited[i]) vr3++;
      if (deaths == 0) medals |= 1;      // SOUFFLE PUR
      if (rel3 == 3 && st3 == 4) medals |= 2; // SAGE DU ROYAUME
      if (vr3 == 18) medals |= 4;        // EXPLORATEUR
      killsS += kills; if (killsS >= 100) medals |= 8; // CHASSEUR
      if (playT < 75000) medals |= 16;   // ECLAIR : moins de 20 minutes
      if (ngRun) medals |= 32;           // COEUR NOIR
      if ((medals & 63) == 63) medals |= 64; // LEGENDE DU VENT
      if (!bestT || playT < bestT) bestT = playT;
      runsDone++; ngPlus = 1; // la prochaine partie sera en ROYAUME NOIR
      pagesScreen(ENDING, 8, "FIN");
      statsScreen();
      saveGame(); // le Royaume Noir debloque est aussi sauvegarde
    }
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
