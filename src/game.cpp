/* ================================================================
 * TYMBRAWL — Brawl Stars-like pour NumWorks (app externe EADK .nwa)
 * 10 brawlers, niveaux de power 1-11 + points de power + trophees,
 * menu anime, selection de personnage, Showdown 1+9 bots,
 * zone qui retrecit, caisses, cubes, super charge, particules,
 * sprites diriges, ombres, screen shake.
 * API : EADK public (eadk.h fourni par nwlink).
 * ================================================================ */
#include <eadk.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---------------- Ecran / monde ---------------- */
#define SCREEN_W 320
#define SCREEN_H 240
#define HUD_H    16
#define VIEW_H   (SCREEN_H - HUD_H)

#define TILE      8
#define MAP_W     48
#define MAP_H     40
#define WORLD_W   (MAP_W * TILE)
#define WORLD_H   (MAP_H * TILE)

#define N_ACTORS  10
#define N_BULLETS 96
#define N_CUBES   48
#define N_PART    96

#define BULLET_LIFE    60
#define SHOOT_RANGE_MAX 190

/* Zone */
#define ZONE_GRACE    750
#define ZONE_TICK     5
#define ZONE_SHRINK   1
#define ZONE_MIN      56
#define ZONE_DMG_TICK 30
#define ZONE_DMG      2

/* Tuiles */
#define T_GRASS 0
#define T_WALL  1
#define T_BUSH  2
#define T_CRATE 3

/* Etats du jeu */
#define S_TITLE   0
#define S_SELECT  1
#define S_GAME    2
#define S_END     3

/* Couleurs RGB565 */
#define RGB(r,g,b) ((uint16_t)((((r)&0xF8)<<8)|(((g)&0xFC)<<3)|(((b)&0xF8)>>3)))
static const uint16_t C_BG     = RGB(16,14,28);
static const uint16_t C_GRASS  = RGB(46,112,46);
static const uint16_t C_GRASS2 = RGB(40,100,40);
static const uint16_t C_WALL   = RGB(96,96,110);
static const uint16_t C_WALLL  = RGB(140,140,158);
static const uint16_t C_WALLD  = RGB(56,56,68);
static const uint16_t C_BUSH   = RGB(22,66,22);
static const uint16_t C_BUSHL  = RGB(34,92,34);
static const uint16_t C_CRATE  = RGB(146,98,38);
static const uint16_t C_CRATEL = RGB(196,150,80);
static const uint16_t C_CRATED = RGB(96,60,20);
static const uint16_t C_BULLET = RGB(255,240,180);
static const uint16_t C_CUBE   = RGB(255,208,40);
static const uint16_t C_ZONE   = RGB(235,70,70);
static const uint16_t C_HUD    = RGB(12,10,20);
static const uint16_t C_WHITE  = RGB(255,255,255);
static const uint16_t C_GREY   = RGB(160,160,170);
static const uint16_t C_HPBG   = RGB(110,18,18);
static const uint16_t C_HPGREEN= RGB(70,225,70);
static const uint16_t C_OUT    = RGB(18,16,24);
static const uint16_t C_SHADOW = RGB(28,52,28);
static const uint16_t C_GOLD   = RGB(255,204,40);
static const uint16_t C_PURPLE = RGB(150,80,240);
static const uint16_t C_SUPER  = RGB(255,230,80);

/* ================================================================ */
/* Brawlers                                                          */
/* ================================================================ */

struct BrawlerDef {
  const char * name;
  uint16_t color;
  int hp, dmg, range, cd, speed, bspd, pellets; /* bspd = vitesse balle */
  const char * desc;
};

static const BrawlerDef BRAWLERS[10] = {
  { "TYMBO",   RGB(60,200,255), 100, 12, 110, 20, 2, 6, 1, "EQUILIBRE"      },
  { "RAFALE",  RGB(240,120,50), 110,  6,  62, 30, 2, 6, 4, "FUSIL A POMPE"  },
  { "LONGIS",  RGB(120,90,240),  70, 30, 180, 55, 2, 9, 1, "SNIPER"         },
  { "BLINZY",  RGB(255,220,60),  80,  5,  82,  8, 2, 8, 1, "MITRAILLEUSE"   },
  { "MAGNUS",  RGB(200,60,60),  180, 15,  48, 25, 2, 5, 1, "TANK"           },
  { "VOLT",    RGB(90,240,220),  70,  8,  72, 14, 3, 7, 1, "RAPIDE"         },
  { "SPYRO",   RGB(90,220,90),   75, 16, 140, 38, 2, 5, 1, "LANCEUR"        },
  { "NEKO",    RGB(250,130,180), 85, 20,  42, 18, 3, 6, 1, "ASSASSIN"       },
  { "BASTION", RGB(150,150,170),150, 10,  92, 26, 1, 5, 2, "LOURD"          },
  { "PIXEL",   RGB(230,90,240),  75, 14, 130, 24, 2, 6, 1, "MAGE"           },
};

#define MAX_POWER 11
#define PP_COST(l) (10 + 10 * (l))   /* cout en points de power du niveau l -> l+1 */

/* ---------------- Etat meta (session) ---------------- */
static int g_brawler;                 /* brawler choisi par le joueur */
static int g_power[10];               /* niveau de power 1..11 */
static int g_pp;                      /* points de power */
static int g_troph[10];               /* trophees par brawler */
static int g_state = S_TITLE;
static int g_sel = 0;                 /* selection dans le menu */
static int g_lastRank = 0, g_lastCubes = 0, g_lastTroph = 0, g_lastPP = 0;

/* ---------------- Etat partie ---------------- */
struct Actor {
  int x, y;
  int hp, maxhp;
  int cubes, super;
  int cd;
  bool alive;
  bool bot;
  int brawler;          /* index dans BRAWLERS */
  int fx, fy;           /* derniere direction de regard */
  int wx, wy;           /* waypoint */
  int anim;             /* animation de marche */
};

struct Bullet {
  int x, y;
  float dx, dy;
  int owner, life, dmg;
  bool super;
  bool alive;
};

struct Cube { int x, y; bool alive; };
struct Part { int x, y, vx, vy, life; uint16_t c; bool alive; };

static uint8_t  g_map[MAP_W * MAP_H];
static uint8_t  g_deco[MAP_W * MAP_H];   /* touffes d'herbe decoratives */
static uint8_t  g_crateHp[MAP_W * MAP_H];
static Actor    g_actors[N_ACTORS];
static Bullet   g_bullets[N_BULLETS];
static Cube     g_cubes[N_CUBES];
static Part     g_parts[N_PART];
static int      g_cx, g_cy;
static int      g_zx, g_zy, g_zw, g_zh;
static int      g_frame, g_rank;
static bool     g_over, g_victory;
static int      g_shake;                 /* screen shake */
static uint16_t g_flash;                 /* flash plein ecran (0 = rien) */

/* ---------------- Helpers ---------------- */
static inline int tileAt(int wx, int wy) {
  if (wx < 0 || wy < 0 || wx >= WORLD_W || wy >= WORLD_H) return T_WALL;
  return g_map[(wy / TILE) * MAP_W + (wx / TILE)];
}
static inline bool solid(int wx, int wy) {
  int t = tileAt(wx, wy);
  return t == T_WALL || t == T_CRATE;
}
static inline bool inBush(int wx, int wy) { return tileAt(wx, wy) == T_BUSH; }
static inline bool inZone(int wx, int wy) {
  return wx >= g_zx && wx < g_zx + g_zw && wy >= g_zy && wy < g_zy + g_zh;
}
static inline int dist2(int ax, int ay, int bx, int by) {
  int dx = ax - bx, dy = ay - by; return dx * dx + dy * dy;
}
static inline uint32_t rnd(uint32_t n) { return n ? eadk_random() % n : 0; }

static inline void fillRect(int x, int y, int w, int h, uint16_t c) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > SCREEN_W) w = SCREEN_W - x;
  if (y + h > SCREEN_H) h = SCREEN_H - y;
  if (w <= 0 || h <= 0) return;
  eadk_rect_t r = { (uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h };
  eadk_display_push_rect_uniform(r, (eadk_color_t)c);
}

static inline void drawText(const char * t, int x, int y, uint16_t fg, uint16_t bg, bool large) {
  eadk_point_t p = { (uint16_t)x, (uint16_t)y };
  eadk_display_draw_string(t, p, large, (eadk_color_t)fg, (eadk_color_t)bg);
}

static inline bool keyDown(uint64_t s, eadk_key_t k) { return eadk_keyboard_key_down((eadk_keyboard_state_t)s, k); }

/* Texte centre (petite police ~ 10px/char, grande ~ 15px/char) */
static void drawTextC(const char * t, int cy, uint16_t fg, uint16_t bg, bool large) {
  int w = large ? 15 : 10;
  int len = 0; while (t[len]) len++;
  drawText(t, SCREEN_W / 2 - len * w / 2, cy, fg, bg, large);
}

/* ================================================================ */
/* Particules                                                        */
/* ================================================================ */

static void spawnPart(int x, int y, uint16_t c, int n, int spd) {
  for (int i = 0; i < N_PART && n > 0; i++) {
    Part & p = g_parts[i];
    if (p.alive) continue;
    p.x = x; p.y = y;
    p.vx = (int)(spd * (rnd(100) - 50) / 50);
    p.vy = (int)(spd * (rnd(100) - 50) / 50);
    p.life = 6 + rnd(10);
    p.c = c; p.alive = true;
    n--;
  }
}

static void updateParts() {
  for (int i = 0; i < N_PART; i++) {
    Part & p = g_parts[i];
    if (!p.alive) continue;
    p.x += p.vx; p.y += p.vy;
    p.vx /= 2; p.vy /= 2;
    if (--p.life <= 0) p.alive = false;
  }
}

/* ================================================================ */
/* Carte                                                             */
/* ================================================================ */

static void genMap() {
  memset(g_map, T_GRASS, sizeof(g_map));
  memset(g_deco, 0, sizeof(g_deco));
  memset(g_crateHp, 0, sizeof(g_crateHp));
  for (int i = 0; i < MAP_W; i++) {
    g_map[i] = T_WALL; g_map[(MAP_H - 1) * MAP_W + i] = T_WALL;
  }
  for (int j = 0; j < MAP_H; j++) {
    g_map[j * MAP_W] = T_WALL; g_map[j * MAP_W + MAP_W - 1] = T_WALL;
  }
  for (int k = 0; k < 26; k++) {
    int cx = 2 + rnd(MAP_W - 4), cy = 2 + rnd(MAP_H - 4);
    uint8_t t = (k % 2) ? T_BUSH : T_WALL;
    int n = 2 + rnd(4);
    for (int i = 0; i < n; i++) {
      int x = cx + rnd(3) - 1, y = cy + rnd(3) - 1;
      if (x > 0 && x < MAP_W - 1 && y > 0 && y < MAP_H - 1) g_map[y * MAP_W + x] = t;
    }
  }
  for (int k = 0; k < 14; k++) {
    int x = 2 + rnd(MAP_W - 4), y = 2 + rnd(MAP_H - 4);
    if (g_map[y * MAP_W + x] == T_GRASS) {
      g_map[y * MAP_W + x] = T_CRATE;
      g_crateHp[y * MAP_W + x] = 20;
    }
  }
  /* Herbe decorative */
  for (int k = 0; k < 130; k++) {
    int x = 1 + rnd(MAP_W - 2), y = 1 + rnd(MAP_H - 2);
    if (g_map[y * MAP_W + x] == T_GRASS) g_deco[y * MAP_W + x] = 1 + rnd(3);
  }
}

static void spawnActor(Actor * a, int idx) {
  for (int tries = 0; tries < 200; tries++) {
    int x = TILE * (2 + rnd(MAP_W - 4)) + TILE / 2;
    int y = TILE * (2 + rnd(MAP_H - 4)) + TILE / 2;
    if (solid(x, y) || inBush(x, y)) continue;
    bool ok = true;
    for (int i = 0; i < idx; i++)
      if (g_actors[i].alive && dist2(x, y, g_actors[i].x, g_actors[i].y) < 70 * 70) { ok = false; break; }
    if (!ok) continue;
    a->x = x; a->y = y; return;
  }
  a->x = TILE * (MAP_W / 2); a->y = TILE * (MAP_H / 2);
}

static int statMul(int base, int power) { return base * (10 + power - 1) / 10; }

static void initGame() {
  genMap();
  memset(g_bullets, 0, sizeof(g_bullets));
  memset(g_cubes, 0, sizeof(g_cubes));
  memset(g_parts, 0, sizeof(g_parts));
  for (int i = 0; i < N_ACTORS; i++) {
    Actor & a = g_actors[i];
    a.bot = (i != 0);
    a.brawler = a.bot ? rnd(10) : g_brawler;
    const BrawlerDef & d = BRAWLERS[a.brawler];
    int lvl = a.bot ? 1 + rnd(5) : g_power[g_brawler];
    a.maxhp = statMul(d.hp, lvl);
    a.hp = a.maxhp;
    a.cubes = 0; a.super = 0; a.cd = 0; a.alive = true;
    a.fx = 1; a.fy = 0; a.wx = a.wy = 0; a.anim = 0;
    spawnActor(&a, i);
  }
  g_zx = 0; g_zy = 0; g_zw = WORLD_W; g_zh = WORLD_H;
  g_frame = 0; g_over = false; g_victory = false; g_rank = 0;
  g_shake = 0; g_flash = 0;
  g_cx = g_actors[0].x - SCREEN_W / 2;
  g_cy = g_actors[0].y - VIEW_H / 2;
}

/* ================================================================ */
/* Deplacement                                                       */
/* ================================================================ */

static bool canStand(int wx, int wy) {
  return !solid(wx - 3, wy - 3) && !solid(wx + 3, wy - 3) &&
         !solid(wx - 3, wy + 3) && !solid(wx + 3, wy + 3);
}

static void moveActor(Actor * a, int dx, int dy) {
  if (dx && canStand(a->x + dx, a->y)) a->x += dx;
  if (dy && canStand(a->x, a->y + dy)) a->y += dy;
  if (dx || dy) {
    a->fx = dx; a->fy = dy;
    if (++a->anim >= 8) a->anim = 0;
  }
}

static void moveActorSmart(Actor * a, float dx, float dy) {
  float len = sqrtf(dx * dx + dy * dy);
  if (len < 0.01f) return;
  dx /= len; dy /= len;
  for (int attempt = 0; attempt < 8; attempt++) {
    int sp = BRAWLERS[a->brawler].speed;
    int ix = (int)(dx * sp + (dx > 0 ? 0.5f : -0.5f));
    int iy = (int)(dy * sp + (dy > 0 ? 0.5f : -0.5f));
    if (ix == 0 && iy == 0) { ix = dx > 0 ? 1 : (dx < 0 ? -1 : 0); iy = dy > 0 ? 1 : (dy < 0 ? -1 : 0); }
    if ((ix && canStand(a->x + ix, a->y)) || (iy && canStand(a->x, a->y + iy))) {
      moveActor(a, ix, iy);
      return;
    }
    float ang = (attempt % 2 ? 1 : -1) * (0.7854f * (1 + attempt / 2));
    float c = cosf(ang), s = sinf(ang);
    float nx = dx * c - dy * s, ny = dx * s + dy * c;
    dx = nx; dy = ny;
  }
}

/* ================================================================ */
/* Tirs et degats                                                    */
/* ================================================================ */

static void shoot(int owner, float dx, float dy, bool isSuper) {
  Actor & a = g_actors[owner];
  const BrawlerDef & d = BRAWLERS[a.brawler];
  int dmg = statMul(d.dmg, a.bot ? 1 : g_power[a.brawler]) + 2 * a.cubes;
  if (isSuper) dmg *= 2;
  int pellets = isSuper ? (d.pellets + 2) : d.pellets;
  float base = atan2f(dy, dx);
  float spread = d.pellets > 1 ? 0.38f : 0.0f;
  if (isSuper) spread += 0.25f;
  for (int p = 0; p < pellets; p++) {
    float ang = base + (pellets > 1 ? ((float)p / (pellets - 1) - 0.5f) * 2 * spread : 0);
    for (int i = 0; i < N_BULLETS; i++) {
      Bullet & b = g_bullets[i];
      if (b.alive) continue;
      b.x = a.x; b.y = a.y;
      b.dx = cosf(ang) * d.bspd; b.dy = sinf(ang) * d.bspd;
      b.owner = owner; b.life = BULLET_LIFE; b.dmg = dmg;
      b.super = isSuper; b.alive = true;
      break;
    }
  }
  spawnPart(a.x + (int)(cosf(base) * 6), a.y + (int)(sinf(base) * 6),
            isSuper ? C_SUPER : C_BULLET, isSuper ? 8 : 3, 3);
  if (isSuper) { a.super = 0; g_shake = 4; }
}

static void dropCubes(int x, int y, int n) {
  for (int k = 0; k < N_CUBES && n > 0; k++) {
    if (!g_cubes[k].alive) {
      g_cubes[k].alive = true;
      g_cubes[k].x = x + rnd(9) - 4; g_cubes[k].y = y + rnd(9) - 4;
      n--;
    }
  }
}

static void damageActor(int idx, int dmg, int from) {
  Actor & a = g_actors[idx];
  if (!a.alive) return;
  a.hp -= dmg;
  spawnPart(a.x, a.y, RGB(255,80,80), 4, 3);
  if (from >= 0 && from != idx && g_actors[from].alive) {
    Actor & f = g_actors[from];
    f.super += dmg;                    /* le super charge sur degats infliges */
    if (f.super > 100) f.super = 100;
  }
  if (idx == 0) g_shake = 3;
  if (a.hp > 0) return;
  a.alive = false;
  spawnPart(a.x, a.y, BRAWLERS[a.brawler].color, 16, 5);
  dropCubes(a.x, a.y, a.cubes + 1);
  int aliveCount = 0;
  for (int i = 0; i < N_ACTORS; i++) if (g_actors[i].alive) aliveCount++;
  if (idx == 0) { g_over = true; g_rank = aliveCount + 1; g_flash = RGB(255,60,60); }
  else if (aliveCount == 1 && g_actors[0].alive) { g_over = true; g_victory = true; g_rank = 1; g_flash = C_GOLD; }
}

/* ================================================================ */
/* IA des bots                                                       */
/* ================================================================ */

static void botThink(Actor * a, int idx) {
  const BrawlerDef & d = BRAWLERS[a->brawler];
  int tx = -1, tdist = 1 << 30;
  for (int i = 0; i < N_ACTORS; i++) {
    if (i == idx || !g_actors[i].alive) continue;
    int dd = dist2(a->x, a->y, g_actors[i].x, g_actors[i].y);
    if (dd > d.range * d.range * 2) continue;
    if (inBush(g_actors[i].x, g_actors[i].y) && dd > 40 * 40) continue;
    if (dd < tdist) { tdist = dd; tx = i; }
  }
  bool outside = !inZone(a->x, a->y);
  int zcx = g_zx + g_zw / 2, zcy = g_zy + g_zh / 2;

  if (outside) {
    moveActorSmart(a, (float)(zcx - a->x), (float)(zcy - a->y));
  } else if (a->hp < a->maxhp / 3 && tx >= 0) {
    moveActorSmart(a, (float)(a->x - g_actors[tx].x), (float)(a->y - g_actors[tx].y));
  } else if (tx >= 0) {
    int dd = tdist;
    int ideal = d.range * 3 / 4;
    if (dd > ideal * ideal) {
      float jitter = (float)(rnd(41) - 20) / 20.0f;
      moveActorSmart(a, (float)(g_actors[tx].x - a->x) + jitter,
                        (float)(g_actors[tx].y - a->y) + jitter);
    } else if (rnd(4) == 0) {
      int sx = g_actors[tx].y - a->y, sy = -(g_actors[tx].x - a->x);
      if (rnd(2)) { sx = -sx; sy = -sy; }
      moveActorSmart(a, (float)sx, (float)sy);
    }
    if (a->cd <= 0 && dd < d.range * d.range) {
      float ang = atan2f((float)(g_actors[tx].y - a->y), (float)(g_actors[tx].x - a->x));
      ang += ((float)(rnd(21) - 10) / 60.0f);
      shoot(idx, cosf(ang), sinf(ang), a->super >= 100);
      a->cd = d.cd + rnd(10);
    }
  } else {
    if (a->wx == 0 || dist2(a->x, a->y, a->wx, a->wy) < 12 * 12) {
      a->wx = g_zx + 8 + rnd(g_zw > 16 ? g_zw - 16 : 1);
      a->wy = g_zy + 8 + rnd(g_zh > 16 ? g_zh - 16 : 1);
    }
    moveActorSmart(a, (float)(a->wx - a->x), (float)(a->wy - a->y));
  }
  /* Farm de caisses */
  if (a->cd <= 0 && tx < 0) {
    static const int sx8[4] = { 1, -1, 0, 0 }, sy8[4] = { 0, 0, 1, -1 };
    for (int dd = 0; dd < 4; dd++) {
      int px = a->x + sx8[dd] * 20, py = a->y + sy8[dd] * 20;
      if (tileAt(px, py) == T_CRATE) {
        shoot(idx, (float)sx8[dd], (float)sy8[dd], false);
        a->cd = d.cd;
        break;
      }
    }
  }
  if (a->cd > 0) a->cd--;
}

/* ================================================================ */
/* Balles / zone                                                     */
/* ================================================================ */

static void updateBullets() {
  for (int i = 0; i < N_BULLETS; i++) {
    Bullet & b = g_bullets[i];
    if (!b.alive) continue;
    b.x += (int)b.dx; b.y += (int)b.dy;
    b.life--;
    if (b.life <= 0 || b.x < 0 || b.y < 0 || b.x >= WORLD_W || b.y >= WORLD_H) { b.alive = false; continue; }
    int t = tileAt(b.x, b.y);
    if (t == T_WALL) { spawnPart(b.x, b.y, C_WALLL, 3, 2); b.alive = false; continue; }
    if (t == T_CRATE) {
      int ti = (b.y / TILE) * MAP_W + (b.x / TILE);
      g_crateHp[ti] = (uint8_t)(g_crateHp[ti] > b.dmg ? g_crateHp[ti] - b.dmg : 0);
      if (g_crateHp[ti] == 0) {
        g_map[ti] = T_GRASS;
        spawnPart((ti % MAP_W) * TILE + 4, (ti / MAP_W) * TILE + 4, C_CRATEL, 10, 3);
        dropCubes((ti % MAP_W) * TILE + 4, (ti / MAP_W) * TILE + 4, 1);
      }
      b.alive = false;
      continue;
    }
    for (int a = 0; a < N_ACTORS; a++) {
      if (a == b.owner || !g_actors[a].alive) continue;
      if (dist2(b.x, b.y, g_actors[a].x, g_actors[a].y) < 5 * 5) {
        damageActor(a, b.dmg, b.owner);
        b.alive = false;
        break;
      }
    }
  }
}

static void updateZone() {
  if (g_frame < ZONE_GRACE) return;
  if (g_frame % ZONE_TICK) return;
  if (g_zw > ZONE_MIN) { g_zx += ZONE_SHRINK / 2; g_zw -= ZONE_SHRINK; }
  if (g_zh > ZONE_MIN) { g_zy += ZONE_SHRINK / 2; g_zh -= ZONE_SHRINK; }
  if (g_zw < ZONE_MIN) g_zw = ZONE_MIN;
  if (g_zh < ZONE_MIN) g_zh = ZONE_MIN;
  if (g_frame % ZONE_DMG_TICK == 0) {
    for (int i = 0; i < N_ACTORS; i++) {
      Actor & a = g_actors[i];
      if (a.alive && !inZone(a.x, a.y)) damageActor(i, ZONE_DMG, -1);
    }
  }
}

/* ================================================================ */
/* Rendu du jeu                                                      */
/* ================================================================ */

/* Sprite d'un brawler : corps + tete + yeux + arme, oriente vers (fx,fy) */
static void drawBrawler(int sx, int sy, const Actor & a, bool walking) {
  const BrawlerDef & d = BRAWLERS[a.brawler];
  /* ombre */
  fillRect(sx - 3, sy + 4, 8, 2, C_SHADOW);
  fillRect(sx - 4, sy + 3, 10, 1, C_SHADOW);
  int bob = walking ? ((a.anim / 4) % 2) : 0;
  /* corps */
  fillRect(sx - 3, sy - 1 + bob, 7, 5, d.color);
  fillRect(sx - 3, sy - 1 + bob, 7, 1, C_OUT);
  fillRect(sx - 3, sy + 3 + bob, 7, 1, C_OUT);
  fillRect(sx - 4, sy + bob, 1, 3, C_OUT);
  fillRect(sx + 4, sy + bob, 1, 3, C_OUT);
  /* tete */
  fillRect(sx - 2, sy - 5 + bob, 5, 4, RGB(250,215,170));
  fillRect(sx - 2, sy - 5 + bob, 5, 1, C_OUT);
  /* yeux dans la direction du regard */
  int ex = 0, ey = 0;
  if (a.fx > 0) ex = 1; else if (a.fx < 0) ex = -1;
  if (a.fy > 0) ey = 1; else if (a.fy < 0) ey = -1;
  fillRect(sx - 1 + ex, sy - 4 + ey + bob, 1, 1, C_OUT);
  fillRect(sx + 1 + ex, sy - 4 + ey + bob, 1, 1, C_OUT);
  /* arme pointee vers la direction de visee */
  int wx = a.fx, wy = a.fy;
  if (wx == 0 && wy == 0) wx = 1;
  for (int s = 2; s <= 5; s++)
    fillRect(sx + wx * s - 1, sy + wy * s - 1 + bob, 2, 2, C_OUT);
  fillRect(sx + wx * 5 - 1, sy + wy * 5 - 1 + bob, 3, 3, d.color);
  /* contour du corps : barre de vie au-dessus */
  int hpw = (10 * (a.hp > 0 ? a.hp : 0)) / a.maxhp;
  fillRect(sx - 5, sy - 8 + bob, 10, 2, C_HPBG);
  fillRect(sx - 5, sy - 8 + bob, hpw, 2, a.bot ? C_HPGREEN : RGB(90,230,255));
  /* cubes portes */
  if (a.cubes) fillRect(sx + 5, sy - 8 + bob, 3, 3, C_CUBE);
  /* super pret : halo */
  if (a.super >= 100) {
    fillRect(sx - 6, sy - 1 + bob, 1, 3, C_SUPER);
    fillRect(sx + 6, sy - 1 + bob, 1, 3, C_SUPER);
    fillRect(sx - 1, sy - 7 + bob, 2, 1, C_SUPER);
  }
}

static void draw() {
  Actor & p = g_actors[0];
  g_cx = p.x - SCREEN_W / 2;
  g_cy = p.y - VIEW_H / 2;
  if (g_cx < 0) g_cx = 0;
  if (g_cx > WORLD_W - SCREEN_W) g_cx = WORLD_W - SCREEN_W;
  if (g_cy < 0) g_cy = 0;
  if (g_cy > WORLD_H - VIEW_H) g_cy = WORLD_H - VIEW_H;
  if (g_shake > 0) {
    g_cx += rnd(2 * g_shake + 1) - g_shake;
    g_cy += rnd(2 * g_shake + 1) - g_shake;
    if (g_cx < 0) g_cx = 0;
    if (g_cy < 0) g_cy = 0;
    if (g_cx > WORLD_W - SCREEN_W) g_cx = WORLD_W - SCREEN_W;
    if (g_cy > WORLD_H - VIEW_H) g_cy = WORLD_H - VIEW_H;
    g_shake--;
  }

  /* Sol */
  fillRect(0, HUD_H, SCREEN_W, VIEW_H, C_GRASS);

  /* Tuiles */
  int tx0 = g_cx / TILE, ty0 = g_cy / TILE;
  int tx1 = (g_cx + SCREEN_W) / TILE + 1, ty1 = (g_cy + VIEW_H) / TILE + 1;
  for (int ty = ty0; ty <= ty1 && ty < MAP_H; ty++) {
    for (int tx = tx0; tx <= tx1 && tx < MAP_W; tx++) {
      int idx = ty * MAP_W + tx;
      int sx = tx * TILE - g_cx, sy = HUD_H + ty * TILE - g_cy;
      uint8_t t = g_map[idx];
      if (t == T_GRASS) {
        uint8_t dc = g_deco[idx];
        if (dc) {
          fillRect(sx + 1, sy + 2, 1, 3, C_GRASS2);
          fillRect(sx + 4, sy + 1, 1, 4, C_GRASS2);
          fillRect(sx + 6, sy + 3, 1, 2, C_GRASS2);
          if (dc == 3) fillRect(sx + 3, sy + 5, 2, 1, RGB(60,130,50));
        }
        continue;
      }
      if (t == T_WALL) {
        fillRect(sx, sy, TILE, TILE, C_WALL);
        fillRect(sx, sy, TILE, 2, C_WALLL);          /* bevel clair */
        fillRect(sx, sy, 2, TILE, C_WALLL);
        fillRect(sx, sy + TILE - 2, TILE, 2, C_WALLD); /* bevel sombre */
        fillRect(sx + TILE - 2, sy, 2, TILE, C_WALLD);
      } else if (t == T_BUSH) {
        fillRect(sx, sy, TILE, TILE, C_BUSH);
        fillRect(sx + 1, sy + 1, 3, 2, C_BUSHL);
        fillRect(sx + 5, sy + 3, 2, 2, C_BUSHL);
        fillRect(sx + 2, sy + 5, 3, 2, C_BUSH);
      } else if (t == T_CRATE) {
        fillRect(sx, sy, TILE, TILE, C_CRATE);
        fillRect(sx + 1, sy + 1, 6, 1, C_CRATEL);
        fillRect(sx + 1, sy + 1, 1, 6, C_CRATEL);
        fillRect(sx + 1, sy + 6, 6, 1, C_CRATED);
        fillRect(sx + 6, sy + 1, 1, 6, C_CRATED);
        fillRect(sx + 3, sy + 2, 2, 4, C_CRATED);    /* croisillon */
      }
    }
  }

  /* Bord de zone */
  int zsx = g_zx - g_cx, zsy = HUD_H + g_zy - g_cy;
  int zex = zsx + g_zw, zey = zsy + g_zh;
  fillRect(zsx, zsy, g_zw, 1, C_ZONE);
  fillRect(zsx, zey - 1, g_zw, 1, C_ZONE);
  fillRect(zsx, zsy, 1, g_zh, C_ZONE);
  fillRect(zex - 1, zsy, 1, g_zh, C_ZONE);

  /* Cubes (avec scintillement) */
  for (int k = 0; k < N_CUBES; k++) {
    if (!g_cubes[k].alive) continue;
    int sx = g_cubes[k].x - 2 - g_cx, sy = HUD_H + g_cubes[k].y - 2 - g_cy;
    fillRect(sx, sy, 5, 5, C_CUBE);
    fillRect(sx + 1, sy + 1, 3, 3, RGB(255,240,140));
    if ((g_frame / 8 + k) % 7 == 0) fillRect(sx - 1, sy - 1, 1, 1, C_WHITE);
  }

  /* Balles (trace) */
  for (int i = 0; i < N_BULLETS; i++) {
    if (!g_bullets[i].alive) continue;
    Bullet & b = g_bullets[i];
    int sz = b.super ? 4 : 3;
    int sx = b.x - sz / 2 - g_cx, sy = HUD_H + b.y - sz / 2 - g_cy;
    fillRect(sx - (int)b.dx, sy - (int)b.dy, sz, sz, b.super ? C_SUPER : RGB(255,200,120));
    fillRect(sx, sy, sz, sz, b.super ? C_SUPER : C_BULLET);
  }

  /* Particules */
  for (int i = 0; i < N_PART; i++) {
    if (!g_parts[i].alive) continue;
    fillRect(g_parts[i].x - 1 - g_cx, HUD_H + g_parts[i].y - 1 - g_cy, 2, 2, g_parts[i].c);
  }

  /* Acteurs (joueur en dernier = au-dessus) */
  for (int i = N_ACTORS - 1; i >= 0; i--) {
    Actor & a = g_actors[i];
    if (!a.alive) continue;
    int sx = a.x - g_cx, sy = HUD_H + a.y - g_cy;
    if (sx < -10 || sy < HUD_H - 10 || sx > SCREEN_W + 10 || sy > SCREEN_H + 10) continue;
    drawBrawler(sx, sy, a, a.anim > 0 && a.anim < 8 && (a.fx || a.fy));
  }

  /* HUD */
  fillRect(0, 0, SCREEN_W, HUD_H, C_HUD);
  fillRect(0, HUD_H - 1, SCREEN_W, 1, C_OUT);
  char buf[24];
  /* Portrait mini + PV */
  fillRect(2, 3, 10, 10, BRAWLERS[p.brawler].color);
  fillRect(4, 5, 6, 3, RGB(250,215,170));
  int hpw = (52 * (p.hp > 0 ? p.hp : 0)) / p.maxhp;
  fillRect(15, 6, 52, 5, C_HPBG);
  fillRect(15, 6, hpw, 5, C_HPGREEN);
  /* Super */
  fillRect(15, 12, 52, 2, RGB(60,50,10));
  fillRect(15, 12, 52 * p.super / 100, 2, C_SUPER);
  /* Cubes */
  buf[0] = 'x'; buf[1] = (char)('0' + (p.cubes / 10) % 10); buf[2] = (char)('0' + p.cubes % 10); buf[3] = 0;
  drawText(buf, 72, 3, C_CUBE, C_HUD, false);
  /* Vivants */
  int aliveCount = 0;
  for (int i = 0; i < N_ACTORS; i++) if (g_actors[i].alive) aliveCount++;
  buf[0] = 'V'; buf[1] = ':'; buf[2] = (char)('0' + aliveCount / 10); buf[3] = (char)('0' + aliveCount % 10); buf[4] = 0;
  drawText(buf, 96, 3, C_WHITE, C_HUD, false);
  /* Zone */
  if (g_frame < ZONE_GRACE) {
    int s = (ZONE_GRACE - g_frame) / 20;
    buf[0] = 'Z'; buf[1] = ':'; buf[2] = (char)('0' + (s / 60) % 10); buf[3] = (char)('0' + (s / 10) % 6); buf[4] = (char)('0' + s % 10); buf[5] = 0;
    drawText(buf, 124, 3, C_GREY, C_HUD, false);
  } else {
    drawText("ZONE !", 124, 3, C_ZONE, C_HUD, false);
  }
  /* Annonce super pret */
  if (p.super >= 100) drawText("SUPER PRET !", 220, 3, C_SUPER, C_HUD, false);
}

/* ================================================================ */
/* Menus                                                             */
/* ================================================================ */

/* Portrait agrandi x4 d'un brawler (30x30) */
static void drawPortrait(int px, int py, int bi, uint16_t bg) {
  const BrawlerDef & d = BRAWLERS[bi];
  /* corps */
  fillRect(px + 4, py + 16, 22, 12, d.color);
  fillRect(px + 4, py + 16, 22, 2, C_OUT);
  fillRect(px + 4, py + 26, 22, 2, C_OUT);
  fillRect(px + 2, py + 18, 2, 8, C_OUT);
  fillRect(px + 26, py + 18, 2, 8, C_OUT);
  /* tete */
  fillRect(px + 8, py + 4, 14, 12, RGB(250,215,170));
  fillRect(px + 8, py + 4, 14, 2, C_OUT);
  fillRect(px + 8, py + 14, 14, 2, C_OUT);
  fillRect(px + 6, py + 6, 2, 8, C_OUT);
  fillRect(px + 22, py + 6, 2, 8, C_OUT);
  /* yeux */
  fillRect(px + 11, py + 9, 3, 3, C_OUT);
  fillRect(px + 17, py + 9, 3, 3, C_OUT);
  fillRect(px + 12, py + 10, 1, 1, C_WHITE);
  fillRect(px + 18, py + 10, 1, 1, C_WHITE);
  /* sourire */
  fillRect(px + 12, py + 14, 6, 1, C_OUT);
  /* bandeau couleur du brawler */
  fillRect(px + 8, py + 2, 14, 3, d.color);
  /* fond */
  fillRect(px - 2, py - 2, 34, 2, bg);
}

static void drawBar(int x, int y, int w, int val, int maxv, uint16_t c) {
  fillRect(x, y, w, 5, RGB(40,40,50));
  if (maxv > 0) fillRect(x, y, w * val / maxv, 5, c);
  fillRect(x, y, w, 1, C_OUT);
  fillRect(x, y + 4, w, 1, C_OUT);
}

static void drawTitle() {
  /* fond degrade vert -> sombre */
  for (int y = 0; y < SCREEN_H; y += 8)
    fillRect(0, y, SCREEN_W, 8, RGB(20 + y / 12, 60 + y / 5, 24 + y / 14));
  /* damier discret */
  for (int y = 0; y < SCREEN_H; y += 16)
    for (int x = (y / 16) % 2 * 16; x < SCREEN_W; x += 32)
      fillRect(x, y, 16, 16, RGB(30,80,30));
  /* titre avec ombre + contour, couleur qui pulse */
  uint16_t tc = (g_frame / 16) % 2 ? C_GOLD : RGB(255,230,90);
  drawTextC("TYMBRAWL", 52, C_OUT, C_BG, true);
  drawTextC("TYMBRAWL", 50, tc, C_BG, true);
  drawTextC("SHOWDOWN 10 JOUEURS", 78, C_WHITE, C_BG, false);
  /* mini arene animee : deux brawlers qui se font face */
  int bob = (g_frame / 12) % 2;
  Actor dummy;
  memset(&dummy, 0, sizeof(dummy));
  dummy.brawler = g_sel; dummy.fx = 1; dummy.fy = 0; dummy.hp = 1; dummy.maxhp = 1;
  drawBrawler(120, 150 + bob, dummy, false);
  dummy.brawler = (g_sel + 3) % 10; dummy.fx = -1;
  drawBrawler(190, 150 + 1 - bob, dummy, false);
  fillRect(158, 150, 4, 4, C_SUPER); /* etincelle entre eux */
  drawTextC("OK : JOUER", 190, C_WHITE, C_BG, false);
  drawTextC("Un hommage non-officiel a Brawl Stars", 214, C_GREY, C_BG, false);
}

static void drawSelect() {
  fillRect(0, 0, SCREEN_W, SCREEN_H, C_BG);
  /* bandeau */
  fillRect(0, 0, SCREEN_W, 22, C_PURPLE);
  drawTextC("CHOISIS TON BRAWLER", 4, C_WHITE, C_PURPLE, false);

  const BrawlerDef & d = BRAWLERS[g_sel];
  int lvl = g_power[g_sel] ? g_power[g_sel] : 1;

  /* portrait central avec cadre qui pulse si super ameliore */
  uint16_t frame = (g_frame / 10) % 2 ? RGB(90,60,150) : RGB(60,40,100);
  fillRect(136, 66, 48, 48, frame);
  drawPortrait(143, 73, g_sel, frame);

  /* nom + trophees */
  drawTextC(d.name, 30, d.color, C_BG, true);
  char buf[24];
  buf[0] = 'P'; buf[1] = 'O'; buf[2] = 'W'; buf[3] = 'E'; buf[4] = 'R'; buf[5] = ' ';
  buf[6] = lvl < 10 ? (char)('0' + lvl) : '1'; buf[7] = lvl >= 10 ? (char)('0' + lvl - 10) : 0; buf[8] = 0;
  drawTextC(buf, 118, C_SUPER, C_BG, false);

  /* pips de niveau (11) */
  for (int i = 0; i < MAX_POWER; i++) {
    uint16_t c = i < lvl ? C_SUPER : RGB(50,45,60);
    fillRect(108 + i * 10, 130, 8, 4, c);
  }

  /* stats */
  drawText("PV", 40, 146, C_WHITE, C_BG, false);
  drawBar(64, 148, 90, statMul(d.hp, lvl), 250, C_HPGREEN);
  drawText("DGT", 40, 158, C_WHITE, C_BG, false);
  drawBar(64, 160, 90, statMul(d.dmg, lvl), 40, RGB(240,90,90));
  drawText("PORT", 40, 170, C_WHITE, C_BG, false);
  drawBar(64, 172, 90, d.range, 190, RGB(90,160,240));
  drawText("VIT", 40, 182, C_WHITE, C_BG, false);
  drawBar(64, 184, 90, d.speed * 50, 160, RGB(240,200,90));

  /* trophees + points de power */
  buf[0] = 'T'; buf[1] = 'R'; buf[2] = 'O'; buf[3] = 'P'; buf[4] = 'H'; buf[5] = ':';
  int t = g_troph[g_sel];
  buf[6] = (char)('0' + (t / 100) % 10); buf[7] = (char)('0' + (t / 10) % 10); buf[8] = (char)('0' + t % 10); buf[9] = 0;
  drawText(buf, 170, 146, C_GOLD, C_BG, false);
  buf[0] = 'P'; buf[1] = 'P'; buf[2] = ':';
  int pp = g_pp;
  buf[3] = (char)('0' + (pp / 100) % 10); buf[4] = (char)('0' + (pp / 10) % 10); buf[5] = (char)('0' + pp % 10); buf[6] = 0;
  drawText(buf, 170, 158, C_PURPLE, C_BG, false);

  /* upgrade */
  if (lvl < MAX_POWER) {
    int cost = PP_COST(lvl);
    bool can = g_pp >= cost;
    buf[0] = 'E'; buf[1] = 'X'; buf[2] = 'E'; buf[3] = ':'; buf[4] = ' '; buf[5] = 0;
    char c2[6];
    c2[0] = (char)('0' + (cost / 100) % 10); c2[1] = (char)('0' + (cost / 10) % 10); c2[2] = (char)('0' + cost % 10);
    c2[3] = ' '; c2[4] = 'P'; c2[5] = 'P'; c2[6] = 0;
    strcat(buf, c2);
    drawTextC(buf, 202, can ? C_WHITE : C_GREY, C_BG, false);
    if (can) drawTextC("^ POWER +1", 214, C_SUPER, C_BG, false);
  } else {
    drawTextC("POWER MAX !", 208, C_SUPER, C_BG, false);
  }

  /* navigation */
  drawText("<", 20, 84, C_WHITE, C_BG, true);
  drawText(">", 288, 84, C_WHITE, C_BG, true);
  drawTextC("OK: JOUER   EXE: AMELIORER   BACK: MENU", 226, C_GREY, C_BG, false);
}

static void applyEndRewards() {
  /* Trophees selon le rang (comme BS : 1er +8 ... dernier -2) */
  static const int TROPH_BY_RANK[11] = { 0, 8, 6, 5, 4, 3, 2, 1, 0, -1, -2 };
  int r = g_rank > 10 ? 10 : g_rank;
  g_lastTroph = TROPH_BY_RANK[r];
  g_troph[g_brawler] += g_lastTroph;
  if (g_troph[g_brawler] < 0) g_troph[g_brawler] = 0;
  /* Points de power : 20 + 5 par cube */
  g_lastPP = 20 + 5 * g_lastCubes;
  g_pp += g_lastPP;
  g_lastRank = g_rank;
}

static void drawEnd() {
  fillRect(0, 0, SCREEN_W, SCREEN_H, C_BG);
  /* bandeau resultat */
  uint16_t bc = g_victory ? C_GOLD : RGB(120,30,30);
  fillRect(0, 30, SCREEN_W, 40, bc);
  drawTextC(g_victory ? "VICTOIRE ROYALE !" : "ELIMINE !", 42, C_WHITE, bc, true);
  /* rang */
  char buf[24];
  buf[0] = 'R'; buf[1] = 'A'; buf[2] = 'N'; buf[3] = 'K'; buf[4] = ':';
  int r = g_rank > 10 ? 10 : g_rank;
  buf[5] = ' '; buf[6] = r == 10 ? '1' : (char)('0' + r); buf[7] = r == 10 ? '0' : 0; buf[8] = 0;
  drawTextC(buf, 86, C_WHITE, C_BG, false);
  /* portrait du brawler joue */
  drawPortrait(143, 104, g_brawler, RGB(60,40,100));
  /* gains */
  buf[0] = '+'; buf[1] = g_lastTroph >= 0 ? ' ' : '-';
  int at = g_lastTroph < 0 ? -g_lastTroph : g_lastTroph;
  buf[2] = (char)('0' + at / 10); buf[3] = (char)('0' + at % 10); buf[4] = ' '; buf[5] = 'T'; buf[6] = 'R'; buf[7] = 'O'; buf[8] = 'P'; buf[9] = 0;
  drawTextC(buf, 142, g_lastTroph >= 0 ? C_GOLD : C_GREY, C_BG, false);
  buf[0] = '+'; buf[1] = (char)('0' + g_lastPP / 100); buf[2] = (char)('0' + (g_lastPP / 10) % 10); buf[3] = (char)('0' + g_lastPP % 10);
  buf[4] = ' '; buf[5] = 'P'; buf[6] = 'P'; buf[7] = 0;
  drawTextC(buf, 156, C_PURPLE, C_BG, false);
  drawTextC("OK: CONTINUER", 196, C_WHITE, C_BG, false);
}

/* ================================================================ */
/* Boucle principale                                                 */
/* ================================================================ */

static uint64_t g_prevKeys = 0;
static bool pressed(uint64_t st, eadk_key_t k) {
  return keyDown(st, k) && !keyDown(g_prevKeys, k);
}

int main(int argc, char * argv[]) {
  (void)argc; (void)argv;
  memset(g_power, 0, sizeof(g_power));
  memset(g_troph, 0, sizeof(g_troph));
  g_pp = 40;
  g_brawler = 0;

  while (1) {
    uint64_t st = eadk_keyboard_scan();

    if (g_state == S_TITLE) {
      drawTitle();
      g_frame++;
      if (pressed(st, eadk_key_ok) || pressed(st, eadk_key_exe)) {
        g_state = S_SELECT;
        g_sel = g_brawler;
        g_frame = 0;
      }
    }
    else if (g_state == S_SELECT) {
      drawSelect();
      g_frame++;
      if (pressed(st, eadk_key_left))  g_sel = (g_sel + 9) % 10;
      if (pressed(st, eadk_key_right)) g_sel = (g_sel + 1) % 10;
      if (pressed(st, eadk_key_ok)) {
        g_brawler = g_sel;
        initGame();
        g_state = S_GAME;
      }
      if (pressed(st, eadk_key_exe)) {
        int lvl = g_power[g_sel] ? g_power[g_sel] : 1;
        int cost = PP_COST(lvl);
        if (lvl < MAX_POWER && g_pp >= cost) {
          g_pp -= cost;
          g_power[g_sel] = lvl + 1;
          g_flash = C_SUPER;
        }
      }
      if (pressed(st, eadk_key_back)) g_state = S_TITLE;
    }
    else if (g_state == S_GAME) {
      Actor & p = g_actors[0];
      if (!g_over) {
        const BrawlerDef & d = BRAWLERS[p.brawler];
        int dx = 0, dy = 0;
        if (keyDown(st, eadk_key_left))  dx -= d.speed;
        if (keyDown(st, eadk_key_right)) dx += d.speed;
        if (keyDown(st, eadk_key_up))    dy -= d.speed;
        if (keyDown(st, eadk_key_down))  dy += d.speed;
        moveActor(&p, dx, dy);

        if ((keyDown(st, eadk_key_ok) || keyDown(st, eadk_key_exe)) && p.cd <= 0) {
          int tx = -1, tdist = d.range * d.range;
          for (int i = 1; i < N_ACTORS; i++) {
            if (!g_actors[i].alive) continue;
            int dd = dist2(p.x, p.y, g_actors[i].x, g_actors[i].y);
            if (inBush(g_actors[i].x, g_actors[i].y) && dd > 40 * 40) continue;
            if (dd < tdist) { tdist = dd; tx = i; }
          }
          float fx = 0, fy = 0;
          bool isSuper = p.super >= 100;
          if (tx >= 0) {
            fx = (float)(g_actors[tx].x - p.x); fy = (float)(g_actors[tx].y - p.y);
          } else if (dx || dy) {
            fx = (float)dx; fy = (float)dy;
          } else {
            fx = (float)p.fx; fy = (float)p.fy;
            if (fx == 0 && fy == 0) fx = 1;
          }
          shoot(0, fx, fy, isSuper);
          p.cd = d.cd;
        }
        if (p.cd > 0) p.cd--;

        for (int i = 1; i < N_ACTORS; i++)
          if (g_actors[i].alive) botThink(&g_actors[i], i);

        /* cubes */
        for (int i = 0; i < N_ACTORS; i++) {
          Actor & a = g_actors[i];
          if (!a.alive) continue;
          for (int k = 0; k < N_CUBES; k++) {
            if (!g_cubes[k].alive) continue;
            if (dist2(a.x, a.y, g_cubes[k].x, g_cubes[k].y) < 7 * 7) {
              g_cubes[k].alive = false;
              a.cubes++;
              a.maxhp += 10; a.hp += 10;
              if (i == 0) spawnPart(a.x, a.y, C_CUBE, 6, 3);
            }
          }
        }

        updateBullets();
        updateZone();
        updateParts();

        g_frame++;
        draw();

        if (g_flash) {
          fillRect(0, 0, SCREEN_W, SCREEN_H, g_flash);
          g_flash = 0;
        }
      } else {
        /* fin de partie : recompenses une seule fois */
        applyEndRewards();
        g_state = S_END;
      }
    }
    else if (g_state == S_END) {
      drawEnd();
      if (pressed(st, eadk_key_ok) || pressed(st, eadk_key_back)) {
        g_state = S_SELECT;
        g_sel = g_brawler;
      }
    }

    g_prevKeys = st;
    eadk_timing_msleep(g_state == S_GAME ? 40 : 30);
  }
}
