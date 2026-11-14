/*
 * COBRA ARENA - clone do Slither.io para PS3 Homebrew (PSL1GHT + SDL 1.2)
 *
 * - Renderizacao 100% em software direto nos pixels da SDL_Surface
 * - Sem imagens, sem fontes externas, sem malloc/free
 * - DualShock 3: analogico esquerdo = direcao | X / R2 = Boost
 * - Fallback PC: mouse = direcao | botao esquerdo / espaco = Boost | ESC = sair
 *
 * Makefile: precisa linkar -lSDL e -lm (alem das libs padrao do PSL1GHT).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <SDL/SDL.h>

#ifdef __lv2ppu__
#include <sysutil/sysutil.h>
#endif

/* ------------------------------------------------------------------ */
/* Constantes                                                          */
/* ------------------------------------------------------------------ */
#define PI_F            3.14159265f
#define TWO_PI          6.28318531f

#define ARENA_R         2200.0f
#define MAX_SNAKES      16          /* indice 0 = jogador, 1..15 = bots */
#define MAX_SEG         300
#define MAX_FOOD        4096
#define AMBIENT_TARGET  2600
#define MIN_MASS        12.0f
#define MAX_MASS        900.0f
#define BASE_SPEED      150.0f
#define BOOST_MULT      2.0f
#define BOOST_COST      5.5f        /* massa perdida por segundo no boost */
#define STICK_DEADZONE  0.15f
#define BORDER_N        2600

/* ------------------------------------------------------------------ */
/* Estado global (tudo estatico)                                       */
/* ------------------------------------------------------------------ */
typedef struct {
    float x[MAX_SEG];
    float y[MAX_SEG];
    int   n;
    float ang, tgt;
    float mass, rad, spacing;
    int   alive, boosting, skin;
    float dropAcc;
    float respawnT;
    float aiT, aggro, boostT;
} Snake;

typedef struct {
    float x, y, r, val, phase, cool, life;
    unsigned char col, active, ambient;
    signed char owner;
} Food;

static Snake snakes[MAX_SNAKES];
static Food  food[MAX_FOOD];
static int   ambientCount = 0;
static int   food_hint = 0;

static float bcos[BORDER_N];
static float bsin[BORDER_N];

static SDL_Surface *screen = NULL;
static Uint32 *pix = NULL;
static int stride = 0;
static int g_w = 1280, g_h = 720;
static int rsh = 16, gsh = 8, bsh = 0;
static Uint32 amask = 0;
static volatile int g_quit = 0;

static float camx = 0.0f, camy = 0.0f, zoom = 1.0f;
static float gtime = 0.0f;
static float deadT = 0.0f;
static int   lastScore = 0;
static int   prevBoost = 0;

static const unsigned char PAL[12][3] = {
    {255, 70, 70}, {255, 150, 40}, {255, 225, 60}, {150, 235, 60},
    {60, 230, 120}, {50, 225, 225}, {70, 150, 255}, {130, 100, 255},
    {200, 90, 255}, {255, 90, 200}, {255, 255, 255}, {255, 120, 130}
};

static const char *NAMES[MAX_SNAKES] = {
    "VOCE", "ALFA", "BRAVO", "CHARLIE", "DELTA", "ECO", "FOX", "GAMA",
    "HOTEL", "INDIO", "JULIET", "KILO", "LIMA", "MIKE", "NOVA", "OSCAR"
};

/* Fonte 3x5 embutida: 0-9, A-Z, ':' e espaco */
static const unsigned char FONT[38][5] = {
    {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},
    {7,4,7,1,7},{7,4,7,5,7},{7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7},
    {2,5,7,5,5},{6,5,6,5,6},{3,4,4,4,3},{6,5,5,5,6},{7,4,6,4,7},
    {7,4,6,4,4},{3,4,5,5,3},{5,5,7,5,5},{7,2,2,2,7},{1,1,1,5,2},
    {5,5,6,5,5},{4,4,4,4,7},{5,7,7,5,5},{6,5,5,5,5},{2,5,5,5,2},
    {6,5,6,4,4},{2,5,5,6,3},{6,5,6,5,5},{3,4,2,1,6},{7,2,2,2,2},
    {5,5,5,5,7},{5,5,5,5,2},{5,5,7,7,5},{5,5,2,5,5},{5,5,2,2,2},
    {7,1,2,4,7},{0,2,0,2,0},{0,0,0,0,0}
};

/* ------------------------------------------------------------------ */
/* Utilidades                                                          */
/* ------------------------------------------------------------------ */
static unsigned int rngs = 2463534242u;

static unsigned int rnd(void)
{
    rngs ^= rngs << 13;
    rngs ^= rngs >> 17;
    rngs ^= rngs << 5;
    return rngs;
}

static float frand(void)
{
    return (float)(rnd() & 0xFFFFFF) / 16777216.0f;
}

static float frange(float a, float b)
{
    return a + (b - a) * frand();
}

static float wrap_pi(float a)
{
    while (a > PI_F) a -= TWO_PI;
    while (a < -PI_F) a += TWO_PI;
    return a;
}

static float fclampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static Uint32 pack(int r, int g, int b)
{
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    return ((Uint32)r << rsh) | ((Uint32)g << gsh) | ((Uint32)b << bsh) | amask;
}

static void int_to_str(int v, char *buf)
{
    char tmp[12];
    int n = 0, i = 0;
    if (v < 0) v = 0;
    if (v == 0) { buf[0] = '0'; buf[1] = 0; return; }
    while (v > 0) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n > 0) buf[i++] = tmp[--n];
    buf[i] = 0;
}

static void scat(char *d, const char *s)
{
    while (*d) d++;
    while (*s) *d++ = *s++;
    *d = 0;
}

static void get_skin(int skin, int *ar, int *ag, int *ab, int *br, int *bg, int *bb)
{
    int i = skin % 12;
    int j;
    *ar = PAL[i][0]; *ag = PAL[i][1]; *ab = PAL[i][2];
    if (skin & 1) {
        j = (i + 6) % 12;
        *br = PAL[j][0]; *bg = PAL[j][1]; *bb = PAL[j][2];
    } else {
        *br = (*ar) * 55 / 100; *bg = (*ag) * 55 / 100; *bb = (*ab) * 55 / 100;
    }
}

/* ------------------------------------------------------------------ */
/* Primitivas de desenho direto nos pixels                             */
/* ------------------------------------------------------------------ */
static void fill_rect(int x, int y, int w, int h, Uint32 c)
{
    int x1 = x + w, y1 = y + h, i, j;
    Uint32 *p;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > g_w) x1 = g_w;
    if (y1 > g_h) y1 = g_h;
    for (j = y; j < y1; j++) {
        p = pix + j * stride;
        for (i = x; i < x1; i++) p[i] = c;
    }
}

static void fill_circle(int cx, int cy, int r, Uint32 c)
{
    int dy, y, dx, x0, x1, i;
    Uint32 *p;
    if (r < 1) r = 1;
    if (cx + r < 0 || cx - r >= g_w || cy + r < 0 || cy - r >= g_h) return;
    for (dy = -r; dy <= r; dy++) {
        y = cy + dy;
        if (y < 0 || y >= g_h) continue;
        dx = (int)(sqrtf((float)(r * r - dy * dy)) + 0.5f);
        x0 = cx - dx;
        x1 = cx + dx;
        if (x0 < 0) x0 = 0;
        if (x1 >= g_w) x1 = g_w - 1;
        p = pix + y * stride;
        for (i = x0; i <= x1; i++) p[i] = c;
    }
}

static void blend_circle(int cx, int cy, int r, int cr, int cg, int cb, int a)
{
    int dy, y, dx, x0, x1, i, dr, dg, db;
    Uint32 *p, d;
    if (r < 1) return;
    if (cx + r < 0 || cx - r >= g_w || cy + r < 0 || cy - r >= g_h) return;
    for (dy = -r; dy <= r; dy++) {
        y = cy + dy;
        if (y < 0 || y >= g_h) continue;
        dx = (int)sqrtf((float)(r * r - dy * dy));
        x0 = cx - dx;
        x1 = cx + dx;
        if (x0 < 0) x0 = 0;
        if (x1 >= g_w) x1 = g_w - 1;
        p = pix + y * stride;
        for (i = x0; i <= x1; i++) {
            d = p[i];
            dr = (int)((d >> rsh) & 255);
            dg = (int)((d >> gsh) & 255);
            db = (int)((d >> bsh) & 255);
            dr += ((cr - dr) * a) >> 8;
            dg += ((cg - dg) * a) >> 8;
            db += ((cb - db) * a) >> 8;
            p[i] = ((Uint32)dr << rsh) | ((Uint32)dg << gsh) | ((Uint32)db << bsh) | amask;
        }
    }
}

static void draw_line(int x0, int y0, int x1, int y1, Uint32 c)
{
    int dx = abs(x1 - x0);
    int dy = -abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy, e2;
    for (;;) {
        if (x0 >= 0 && x0 < g_w && y0 >= 0 && y0 < g_h) pix[y0 * stride + x0] = c;
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static int glyph_index(char c)
{
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'Z') return 10 + (c - 'A');
    if (c == ':') return 36;
    return 37;
}

static int text_width(const char *s, int sc)
{
    int n = 0;
    while (s[n]) n++;
    if (n == 0) return 0;
    return n * 4 * sc - sc;
}

static void draw_text(int x, int y, const char *s, int sc, Uint32 col)
{
    int gi, row, b, bits;
    while (*s) {
        gi = glyph_index(*s);
        for (row = 0; row < 5; row++) {
            bits = FONT[gi][row];
            for (b = 0; b < 3; b++) {
                if (bits & (4 >> b)) fill_rect(x + b * sc, y + row * sc, sc, sc, col);
            }
        }
        x += 4 * sc;
        s++;
    }
}

static void draw_text_shadow(int x, int y, const char *s, int sc, Uint32 col)
{
    draw_text(x + sc, y + sc, s, sc, pack(0, 0, 0));
    draw_text(x, y, s, sc, col);
}

/* ------------------------------------------------------------------ */
/* Comida                                                              */
/* ------------------------------------------------------------------ */
static void spawn_food(float x, float y, float val, int col, int ambient, int big,
                       int owner, float cool)
{
    int k, idx;
    Food *f;
    idx = -1;
    for (k = 0; k < MAX_FOOD; k++) {
        int t = (food_hint + k) % MAX_FOOD;
        if (!food[t].active) { idx = t; food_hint = t + 1; break; }
    }
    if (idx < 0) return;
    f = &food[idx];
    f->x = x;
    f->y = y;
    f->val = val;
    f->col = (unsigned char)(col % 12);
    f->active = 1;
    f->ambient = (unsigned char)ambient;
    f->phase = frand() * TWO_PI;
    f->owner = (signed char)owner;
    f->cool = cool;
    f->life = ambient ? 1.0e9f : 60.0f;
    if (big) f->r = 6.0f + val * 2.0f;
    else f->r = 4.0f + val * 2.2f;
    if (f->r > 18.0f) f->r = 18.0f;
    if (ambient) ambientCount++;
}

static void spawn_ambient(void)
{
    float a = frand() * TWO_PI;
    float d = sqrtf(frand()) * (ARENA_R - 50.0f);
    spawn_food(cosf(a) * d, sinf(a) * d, frange(0.5f, 1.5f), (int)(rnd() % 12), 1, 0, -1, 0.0f);
}

/* ------------------------------------------------------------------ */
/* Cobras                                                              */
/* ------------------------------------------------------------------ */
static void snake_update_dims(Snake *s)
{
    int want;
    s->rad = 9.0f + sqrtf(s->mass) * 0.55f;
    if (s->rad > 26.0f) s->rad = 26.0f;
    s->spacing = s->rad * 0.62f;
    want = 8 + (int)(s->mass * 0.35f);
    if (want > MAX_SEG) want = MAX_SEG;
    if (want < 2) want = 2;
    while (s->n < want) {
        s->x[s->n] = s->x[s->n - 1];
        s->y[s->n] = s->y[s->n - 1];
        s->n++;
    }
    if (s->n > want) s->n = want;
}

static void spawn_snake(int i)
{
    Snake *s = &snakes[i];
    int tries, j, k, ok, want;
    float px = 0.0f, py = 0.0f, a, d, dx, dy;

    for (tries = 0; tries < 20; tries++) {
        a = frand() * TWO_PI;
        d = sqrtf(frand()) * (ARENA_R * 0.75f);
        px = cosf(a) * d;
        py = sinf(a) * d;
        ok = 1;
        for (j = 0; j < MAX_SNAKES && ok; j++) {
            if (j == i || !snakes[j].alive) continue;
            for (k = 0; k < snakes[j].n; k += 3) {
                dx = snakes[j].x[k] - px;
                dy = snakes[j].y[k] - py;
                if (dx * dx + dy * dy < 250.0f * 250.0f) { ok = 0; break; }
            }
        }
        if (ok) break;
    }

    s->ang = frand() * TWO_PI;
    s->tgt = s->ang;
    if (i == 0) {
        s->mass = MIN_MASS + 4.0f;
        s->skin = 4;
    } else {
        if ((rnd() % 10) == 0) s->mass = frange(80.0f, 200.0f);
        else s->mass = frange(14.0f, 45.0f);
        s->skin = (int)(rnd() % 12);
    }
    s->alive = 1;
    s->boosting = 0;
    s->dropAcc = 0.0f;
    s->aiT = frand() * 0.1f;
    s->boostT = 0.0f;
    s->aggro = frand();
    s->respawnT = 0.0f;
    s->n = 1;
    s->x[0] = px;
    s->y[0] = py;
    s->rad = 9.0f + sqrtf(s->mass) * 0.55f;
    s->spacing = s->rad * 0.62f;
    want = 8 + (int)(s->mass * 0.35f);
    if (want > MAX_SEG) want = MAX_SEG;
    for (k = 1; k < want; k++) {
        s->x[k] = s->x[k - 1] - cosf(s->ang) * s->spacing;
        s->y[k] = s->y[k - 1] - sinf(s->ang) * s->spacing;
    }
    s->n = want;

    if (i == 0) {
        camx = s->x[0];
        camy = s->y[0];
        deadT = 0.0f;
    }
}

static void kill_snake(int i)
{
    Snake *s = &snakes[i];
    int k, cnt;
    float v;
    if (!s->alive) return;
    s->alive = 0;
    s->boosting = 0;
    cnt = s->n / 2;
    if (cnt < 2) cnt = 2;
    v = (s->mass * 0.85f) / (float)cnt;
    for (k = 0; k < s->n; k += 2) {
        spawn_food(s->x[k] + frange(-s->rad, s->rad) * 0.5f,
                   s->y[k] + frange(-s->rad, s->rad) * 0.5f,
                   v, s->skin % 12, 0, 1, -1, 0.0f);
    }
    if (i == 0) {
        lastScore = (int)(s->mass * 10.0f);
        deadT = 0.0f;
    } else {
        s->respawnT = 3.0f + frand() * 3.0f;
    }
}

static void snake_move(int i, float dt)
{
    Snake *s = &snakes[i];
    float turn, diff, lim, speed, dx, dy, d, k;
    int j;

    snake_update_dims(s);

    /* curva: cobras maiores fazem curvas mais abertas */
    turn = 4.6f / (1.0f + s->mass * 0.0045f);
    if (turn < 1.7f) turn = 1.7f;
    diff = wrap_pi(s->tgt - s->ang);
    lim = turn * dt;
    if (diff > lim) diff = lim;
    if (diff < -lim) diff = -lim;
    s->ang = wrap_pi(s->ang + diff);

    if (s->boosting && s->mass <= MIN_MASS + 0.5f) s->boosting = 0;
    speed = BASE_SPEED;
    if (s->boosting) speed *= BOOST_MULT;

    s->x[0] += cosf(s->ang) * speed * dt;
    s->y[0] += sinf(s->ang) * speed * dt;

    for (j = 1; j < s->n; j++) {
        dx = s->x[j] - s->x[j - 1];
        dy = s->y[j] - s->y[j - 1];
        d = sqrtf(dx * dx + dy * dy);
        if (d > s->spacing) {
            k = s->spacing / d;
            s->x[j] = s->x[j - 1] + dx * k;
            s->y[j] = s->y[j - 1] + dy * k;
        }
    }

    if (s->boosting) {
        s->mass -= BOOST_COST * dt;
        s->dropAcc += BOOST_COST * dt;
        while (s->dropAcc >= 1.0f) {
            s->dropAcc -= 1.0f;
            spawn_food(s->x[s->n - 1], s->y[s->n - 1], 0.75f, s->skin % 12, 0, 0, i, 2.5f);
        }
    } else {
        s->dropAcc = 0.0f;
    }
    if (s->mass > MAX_MASS) s->mass = MAX_MASS;
    if (s->mass < MIN_MASS) s->mass = MIN_MASS;
}

/* ------------------------------------------------------------------ */
/* IA dos bots                                                         */
/* ------------------------------------------------------------------ */
static int danger_at(int si, float ang, float dist)
{
    Snake *s = &snakes[si];
    Snake *o;
    float px, py, dx, dy, lim, reach;
    int j, k;

    px = s->x[0] + cosf(ang) * dist;
    py = s->y[0] + sinf(ang) * dist;
    if (px * px + py * py > (ARENA_R - 70.0f) * (ARENA_R - 70.0f)) return 1;

    for (j = 0; j < MAX_SNAKES; j++) {
        if (j == si || !snakes[j].alive) continue;
        o = &snakes[j];
        dx = o->x[0] - px;
        dy = o->y[0] - py;
        reach = (float)o->n * o->spacing + o->rad + s->rad + 20.0f;
        if (dx * dx + dy * dy > reach * reach) continue;
        lim = o->rad + s->rad * 0.9f + 14.0f;
        lim *= lim;
        for (k = 0; k < o->n; k += 2) {
            dx = o->x[k] - px;
            dy = o->y[k] - py;
            if (dx * dx + dy * dy < lim) return 1;
        }
    }
    return 0;
}

static void bot_think(int si)
{
    Snake *s = &snakes[si];
    Snake *o;
    static const float offs[4] = { 0.55f, 1.15f, 1.8f, 2.5f };
    float la, a, hx, hy, dx, dy, d2, best, sc, dot, sg, bd, ex, ey;
    int k, j, found, bi, tj;

    hx = s->x[0];
    hy = s->y[0];
    la = 80.0f + s->rad * 3.0f + (s->boosting ? 70.0f : 0.0f);

    /* 1) desviar de colisoes (cobras e parede) */
    if (danger_at(si, s->ang, la) || danger_at(si, s->ang, la * 0.5f)) {
        found = 0;
        sg = (rnd() & 1) ? 1.0f : -1.0f;
        for (k = 0; k < 4 && !found; k++) {
            for (j = 0; j < 2; j++) {
                a = s->ang + ((j == 0) ? sg : -sg) * offs[k];
                if (!danger_at(si, a, la) && !danger_at(si, a, la * 0.5f)) {
                    s->tgt = a;
                    found = 1;
                    break;
                }
            }
        }
        if (!found) s->tgt = atan2f(-hy, -hx) + frange(-0.5f, 0.5f);
        else if (s->mass > 30.0f && s->boostT <= 0.0f && (rnd() % 4) == 0) s->boostT = 0.4f;
        return;
    }

    /* 2) bots agressivos tentam cortar o caminho de cobras proximas */
    if (s->aggro > 0.5f && s->mass > 25.0f) {
        tj = -1;
        bd = 450.0f * 450.0f;
        for (j = 0; j < MAX_SNAKES; j++) {
            if (j == si || !snakes[j].alive) continue;
            o = &snakes[j];
            if (o->mass > s->mass * 1.4f) continue;
            dx = o->x[0] - hx;
            dy = o->y[0] - hy;
            d2 = dx * dx + dy * dy;
            if (d2 < bd) { bd = d2; tj = j; }
        }
        if (tj >= 0) {
            o = &snakes[tj];
            sc = sqrtf(bd);
            ex = o->x[0] + cosf(o->ang) * sc * 0.7f;
            ey = o->y[0] + sinf(o->ang) * sc * 0.7f;
            s->tgt = atan2f(ey - hy, ex - hx);
            if (sc < 300.0f && s->mass > 35.0f && s->boostT <= 0.0f) s->boostT = 0.4f;
            return;
        }
    }

    /* 3) buscar comida (prefere orbes grandes e os que estao a frente) */
    bi = -1;
    best = 0.0f;
    for (k = 0; k < MAX_FOOD; k++) {
        if (!food[k].active) continue;
        if (food[k].cool > 0.0f && food[k].owner == si) continue;
        dx = food[k].x - hx;
        dy = food[k].y - hy;
        d2 = dx * dx + dy * dy;
        if (d2 > 700.0f * 700.0f) continue;
        dot = (dx * cosf(s->ang) + dy * sinf(s->ang)) / (sqrtf(d2) + 1.0f);
        sc = (food[k].val / (d2 + 900.0f)) * (1.25f + dot);
        if (sc > best) { best = sc; bi = k; }
    }
    if (bi >= 0) {
        s->tgt = atan2f(food[bi].y - hy, food[bi].x - hx);
    } else {
        s->tgt = s->ang + frange(-0.8f, 0.8f);
        if (hx * hx + hy * hy > (ARENA_R * 0.8f) * (ARENA_R * 0.8f)) s->tgt = atan2f(-hy, -hx);
    }
    if (s->mass > 40.0f && s->boostT <= 0.0f && (rnd() % 100) < 4) s->boostT = 0.5f;
}

/* ------------------------------------------------------------------ */
/* Entrada (DualShock 3 / mouse / teclado)                             */
/* ------------------------------------------------------------------ */
static void read_input(SDL_Joystick *joy, int *steer, float *tgt, int *boost)
{
    float ax, ay, m2;
    int mx, my, b;
    Uint8 mb;
    Uint8 *keys;

    *steer = 0;
    *boost = 0;

    if (joy) {
        SDL_JoystickUpdate();
        ax = (float)SDL_JoystickGetAxis(joy, 0) / 32767.0f;
        ay = (float)SDL_JoystickGetAxis(joy, 1) / 32767.0f;
        ax = fclampf(ax, -1.0f, 1.0f);
        ay = fclampf(ay, -1.0f, 1.0f);
        m2 = ax * ax + ay * ay;
        /* deadzone quadratica (compara magnitude ao quadrado) */
        if (m2 > STICK_DEADZONE * STICK_DEADZONE) {
            *tgt = atan2f(ay, ax);
            *steer = 1;
        }
        b = SDL_JoystickGetButton(joy, 14) || SDL_JoystickGetButton(joy, 0) ||
            SDL_JoystickGetButton(joy, 9);
        if (b) *boost = 1;
    } else {
        mb = SDL_GetMouseState(&mx, &my);
        ax = (float)(mx - g_w / 2);
        ay = (float)(my - g_h / 2);
        if (ax * ax + ay * ay > 25.0f) {
            *tgt = atan2f(ay, ax);
            *steer = 1;
        }
        if (mb & SDL_BUTTON(1)) *boost = 1;
    }

    keys = SDL_GetKeyState(NULL);
    if (keys) {
        if (keys[SDLK_SPACE] || keys[SDLK_RETURN]) *boost = 1;
        if (!joy) {
            ax = 0.0f; ay = 0.0f;
            if (keys[SDLK_LEFT]) ax -= 1.0f;
            if (keys[SDLK_RIGHT]) ax += 1.0f;
            if (keys[SDLK_UP]) ay -= 1.0f;
            if (keys[SDLK_DOWN]) ay += 1.0f;
            if (ax != 0.0f || ay != 0.0f) {
                *tgt = atan2f(ay, ax);
                *steer = 1;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Atualizacao do jogo                                                 */
/* ------------------------------------------------------------------ */
static void game_update(float dt, SDL_Joystick *joy)
{
    int i, j, k, steer, boost, dead;
    float tgt, dx, dy, d2, d, mag, mag2, pull, lim2, maxr, zt, f;
    Snake *s, *o;
    Food *fd;
    int spawned;

    gtime += dt;
    tgt = 0.0f;

    /* --- jogador --- */
    read_input(joy, &steer, &tgt, &boost);
    s = &snakes[0];
    if (s->alive) {
        if (steer) s->tgt = tgt;
        s->boosting = (boost && s->mass > MIN_MASS + 0.5f) ? 1 : 0;
    } else {
        deadT += dt;
        if (deadT > 1.2f && boost && !prevBoost) spawn_snake(0);
    }
    prevBoost = boost;

    /* --- bots --- */
    for (i = 1; i < MAX_SNAKES; i++) {
        s = &snakes[i];
        if (!s->alive) {
            s->respawnT -= dt;
            if (s->respawnT <= 0.0f) spawn_snake(i);
            continue;
        }
        s->aiT -= dt;
        if (s->aiT <= 0.0f) {
            bot_think(i);
            s->aiT = 0.08f + frand() * 0.08f;
        }
        s->boostT -= dt;
        s->boosting = (s->boostT > 0.0f && s->mass > MIN_MASS + 10.0f) ? 1 : 0;
    }

    /* --- movimento --- */
    for (i = 0; i < MAX_SNAKES; i++) {
        if (snakes[i].alive) snake_move(i, dt);
    }

    /* --- comida: tempo de vida, ima e consumo --- */
    for (k = 0; k < MAX_FOOD; k++) {
        fd = &food[k];
        if (!fd->active) continue;
        if (fd->cool > 0.0f) fd->cool -= dt;
        if (!fd->ambient) {
            fd->life -= dt;
            if (fd->life <= 0.0f) fd->active = 0;
        }
    }
    for (i = 0; i < MAX_SNAKES; i++) {
        s = &snakes[i];
        if (!s->alive) continue;
        mag = s->rad * 2.6f + 30.0f;
        mag2 = mag * mag;
        for (k = 0; k < MAX_FOOD; k++) {
            fd = &food[k];
            if (!fd->active) continue;
            dx = s->x[0] - fd->x;
            dy = s->y[0] - fd->y;
            d2 = dx * dx + dy * dy;
            if (d2 > mag2) continue;
            if (fd->cool > 0.0f && fd->owner == i) continue;
            d = sqrtf(d2);
            if (d < s->rad + fd->r * 0.5f) {
                s->mass += fd->val;
                if (s->mass > MAX_MASS) s->mass = MAX_MASS;
                fd->active = 0;
                if (fd->ambient) ambientCount--;
            } else {
                pull = (80.0f + (1.0f - d / mag) * 420.0f) * dt;
                if (pull > d) pull = d;
                fd->x += dx / d * pull;
                fd->y += dy / d * pull;
            }
        }
    }

    /* --- colisoes: cabeca contra corpo de outra cobra / borda --- */
    for (i = 0; i < MAX_SNAKES; i++) {
        s = &snakes[i];
        if (!s->alive) continue;
        dead = 0;
        if (s->x[0] * s->x[0] + s->y[0] * s->y[0] > ARENA_R * ARENA_R) dead = 1;
        for (j = 0; j < MAX_SNAKES && !dead; j++) {
            if (j == i || !snakes[j].alive) continue;
            o = &snakes[j];
            dx = o->x[0] - s->x[0];
            dy = o->y[0] - s->y[0];
            maxr = (float)o->n * o->spacing + o->rad + s->rad;
            if (dx * dx + dy * dy > maxr * maxr) continue;
            lim2 = o->rad * 0.9f + s->rad * 0.55f;
            lim2 *= lim2;
            for (k = 0; k < o->n; k++) {
                dx = o->x[k] - s->x[0];
                dy = o->y[k] - s->y[0];
                if (dx * dx + dy * dy < lim2) { dead = 1; break; }
            }
        }
        if (dead) kill_snake(i);
    }

    /* --- manter comida ambiente --- */
    spawned = 0;
    while (ambientCount < AMBIENT_TARGET && spawned < 16) {
        spawn_ambient();
        spawned++;
    }

    /* --- camera --- */
    s = &snakes[0];
    if (s->alive) {
        f = dt * 10.0f;
        if (f > 1.0f) f = 1.0f;
        camx += (s->x[0] - camx) * f;
        camy += (s->y[0] - camy) * f;
        zt = 1.0f / powf(1.0f + s->mass / 180.0f, 0.42f);
        f = dt * 2.5f;
        if (f > 1.0f) f = 1.0f;
        zoom += (zt - zoom) * f;
    }
}

/* ------------------------------------------------------------------ */
/* Renderizacao                                                        */
/* ------------------------------------------------------------------ */
static void draw_grid(void)
{
    static const float HC[7] = { 1.0f, 0.5f, -0.5f, -1.0f, -0.5f, 0.5f, 1.0f };
    static const float HS[7] = { 0.0f, 0.8660254f, 0.8660254f, 0.0f,
                                 -0.8660254f, -0.8660254f, 0.0f };
    const float s = 48.0f;
    float hh = 0.8660254f * s;
    float halfW = (float)g_w * 0.5f / zoom + s * 2.0f;
    float halfH = (float)g_h * 0.5f / zoom + s * 2.0f;
    float wx, wy, lim;
    int c0, c1, r0, r1, col, row, k, x0, y0, x1, y1;
    Uint32 gc = pack(34, 43, 52);

    lim = (ARENA_R + s) * (ARENA_R + s);
    c0 = (int)floorf((camx - halfW) / (1.5f * s)) - 1;
    c1 = (int)ceilf((camx + halfW) / (1.5f * s)) + 1;
    r0 = (int)floorf((camy - halfH) / (2.0f * hh)) - 1;
    r1 = (int)ceilf((camy + halfH) / (2.0f * hh)) + 1;

    for (col = c0; col <= c1; col++) {
        for (row = r0; row <= r1; row++) {
            wx = (float)col * 1.5f * s;
            wy = (float)row * 2.0f * hh + ((col & 1) ? hh : 0.0f);
            if (wx * wx + wy * wy > lim) continue;
            for (k = 0; k < 3; k++) {
                x0 = (int)((wx + HC[k] * s - camx) * zoom + (float)g_w * 0.5f);
                y0 = (int)((wy + HS[k] * s - camy) * zoom + (float)g_h * 0.5f);
                x1 = (int)((wx + HC[k + 1] * s - camx) * zoom + (float)g_w * 0.5f);
                y1 = (int)((wy + HS[k + 1] * s - camy) * zoom + (float)g_h * 0.5f);
                draw_line(x0, y0, x1, y1, gc);
            }
        }
    }
}

static void draw_border(void)
{
    float dist = sqrtf(camx * camx + camy * camy);
    float half = sqrtf((float)(g_w * g_w + g_h * g_h)) * 0.5f / zoom + 80.0f;
    int k, sx, sy, rg, rc, pulse;
    Uint32 cg, cc;

    if (ARENA_R - dist > half) return;
    rg = (int)(30.0f * zoom) + 1;
    rc = (int)(12.0f * zoom) + 1;
    pulse = (int)(40.0f + 40.0f * sinf(gtime * 3.0f));
    cg = pack(105, 0, 0);
    cc = pack(255, 50 + pulse, 50);

    for (k = 0; k < BORDER_N; k++) {
        sx = (int)((bcos[k] * ARENA_R - camx) * zoom + (float)g_w * 0.5f);
        sy = (int)((bsin[k] * ARENA_R - camy) * zoom + (float)g_h * 0.5f);
        if (sx < -rg || sx > g_w + rg || sy < -rg || sy > g_h + rg) continue;
        fill_circle(sx, sy, rg, cg);
    }
    for (k = 0; k < BORDER_N; k++) {
        sx = (int)((bcos[k] * ARENA_R - camx) * zoom + (float)g_w * 0.5f);
        sy = (int)((bsin[k] * ARENA_R - camy) * zoom + (float)g_h * 0.5f);
        if (sx < -rc || sx > g_w + rc || sy < -rc || sy > g_h + rc) continue;
        fill_circle(sx, sy, rc, cc);
    }
}

static void draw_food(void)
{
    int k, sx, sy, rr, cr, cg, cb;
    float pulse;
    Food *f;

    for (k = 0; k < MAX_FOOD; k++) {
        f = &food[k];
        if (!f->active) continue;
        sx = (int)((f->x - camx) * zoom + (float)g_w * 0.5f);
        sy = (int)((f->y - camy) * zoom + (float)g_h * 0.5f);
        if (sx < -30 || sx > g_w + 30 || sy < -30 || sy > g_h + 30) continue;
        pulse = 1.0f + 0.18f * sinf(gtime * 5.0f + f->phase);
        rr = (int)(f->r * pulse * zoom + 0.5f);
        if (rr < 2) rr = 2;
        cr = PAL[f->col][0];
        cg = PAL[f->col][1];
        cb = PAL[f->col][2];
        blend_circle(sx, sy, rr * 2, cr, cg, cb, 45);
        fill_circle(sx, sy, rr, pack(cr, cg, cb));
        if (rr >= 4) fill_circle(sx - rr / 3, sy - rr / 3, rr / 3,
                                 pack(cr + (255 - cr) / 2, cg + (255 - cg) / 2, cb + (255 - cb) / 2));
    }
}

static void draw_snake(int si)
{
    Snake *s = &snakes[si];
    int k, sx, sy, r, rk, hx, hy, side, er, pr, ex, ey;
    int ar, ag, ab, br, bg, bb;
    Uint32 cA, cB;
    float f, ca, sa, tc, ts, sw, cw;

    if (!s->alive) return;
    get_skin(s->skin, &ar, &ag, &ab, &br, &bg, &bb);
    if (s->boosting) {
        ar += (255 - ar) * 45 / 100; ag += (255 - ag) * 45 / 100; ab += (255 - ab) * 45 / 100;
        br += (255 - br) * 45 / 100; bg += (255 - bg) * 45 / 100; bb += (255 - bb) * 45 / 100;
    }
    cA = pack(ar, ag, ab);
    cB = pack(br, bg, bb);
    r = (int)(s->rad * zoom + 0.5f);
    if (r < 2) r = 2;

    /* brilho branco do boost */
    if (s->boosting) {
        for (k = s->n - 1; k >= 0; k -= 2) {
            sx = (int)((s->x[k] - camx) * zoom + (float)g_w * 0.5f);
            sy = (int)((s->y[k] - camy) * zoom + (float)g_h * 0.5f);
            if (sx < -r * 2 || sx > g_w + r * 2 || sy < -r * 2 || sy > g_h + r * 2) continue;
            blend_circle(sx, sy, r * 2, 255, 255, 255, 34);
        }
    }

    /* corpo: da cauda para a cabeca */
    for (k = s->n - 1; k >= 0; k--) {
        sx = (int)((s->x[k] - camx) * zoom + (float)g_w * 0.5f);
        sy = (int)((s->y[k] - camy) * zoom + (float)g_h * 0.5f);
        if (sx < -r || sx > g_w + r || sy < -r || sy > g_h + r) continue;
        f = (float)(s->n - 1 - k) / 10.0f;
        if (f > 1.0f) f = 1.0f;
        rk = (int)((float)r * (0.55f + 0.45f * f) + 0.5f);
        if (rk < 1) rk = 1;
        fill_circle(sx, sy, rk, ((k / 3) & 1) ? cB : cA);
    }

    /* olhos direcionais */
    hx = (int)((s->x[0] - camx) * zoom + (float)g_w * 0.5f);
    hy = (int)((s->y[0] - camy) * zoom + (float)g_h * 0.5f);
    ca = cosf(s->ang);
    sa = sinf(s->ang);
    tc = cosf(s->tgt);
    ts = sinf(s->tgt);
    er = (int)((float)r * 0.42f + 0.5f);
    if (er < 2) er = 2;
    pr = (int)((float)er * 0.55f + 0.5f);
    if (pr < 1) pr = 1;
    for (side = -1; side <= 1; side += 2) {
        sw = (float)side;
        cw = (float)r * 0.58f * sw;
        ex = hx + (int)(ca * (float)r * 0.35f - sa * cw);
        ey = hy + (int)(sa * (float)r * 0.35f + ca * cw);
        fill_circle(ex, ey, er, pack(255, 255, 255));
        fill_circle(ex + (int)(tc * (float)er * 0.4f), ey + (int)(ts * (float)er * 0.4f),
                    pr, pack(10, 10, 14));
    }
}

static void draw_minimap(void)
{
    int cx = g_w - 105, cy = g_h - 105, R = 85;
    int k, mx, my, i;
    int ar, ag, ab, br, bg, bb;
    float a;
    Snake *s;

    blend_circle(cx, cy, R, 0, 0, 0, 140);
    for (k = 0; k < 96; k++) {
        a = (float)k * TWO_PI / 96.0f;
        fill_circle(cx + (int)(cosf(a) * (float)R), cy + (int)(sinf(a) * (float)R), 2,
                    pack(255, 60, 60));
    }
    for (i = MAX_SNAKES - 1; i >= 0; i--) {
        s = &snakes[i];
        if (!s->alive) continue;
        mx = cx + (int)(s->x[0] / ARENA_R * (float)(R - 4));
        my = cy + (int)(s->y[0] / ARENA_R * (float)(R - 4));
        if (i == 0) {
            fill_circle(mx, my, 5, pack(0, 0, 0));
            fill_circle(mx, my, 3, pack(255, 255, 255));
        } else {
            get_skin(s->skin, &ar, &ag, &ab, &br, &bg, &bb);
            fill_circle(mx, my, 2, pack(ar, ag, ab));
        }
    }
}

static void draw_hud(void)
{
    char buf[48], num[16];
    int order[MAX_SNAKES], cnt = 0, i, j, t, rows, x, y, tw;
    Snake *s = &snakes[0];

    /* pontuacao / tamanho do jogador */
    if (s->alive) {
        int_to_str((int)(s->mass * 10.0f), num);
        buf[0] = 0;
        scat(buf, "TAMANHO: ");
        scat(buf, num);
        draw_text_shadow(16, g_h - 36, buf, 3, pack(255, 255, 255));
    }

    /* placar: top 5 */
    for (i = 0; i < MAX_SNAKES; i++) {
        if (snakes[i].alive) order[cnt++] = i;
    }
    for (i = 1; i < cnt; i++) {
        t = order[i];
        j = i - 1;
        while (j >= 0 && snakes[order[j]].mass < snakes[t].mass) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = t;
    }
    rows = cnt < 5 ? cnt : 5;
    draw_text_shadow(g_w - 220, 12, "PLACAR", 3, pack(255, 255, 255));
    for (i = 0; i < rows; i++) {
        j = order[i];
        buf[0] = 0;
        int_to_str(i + 1, num);
        scat(buf, num);
        scat(buf, " ");
        scat(buf, NAMES[j]);
        y = 40 + i * 20;
        draw_text_shadow(g_w - 220, y, buf, 2,
                         (j == 0) ? pack(255, 255, 255) : pack(170, 180, 190));
        int_to_str((int)(snakes[j].mass * 10.0f), num);
        draw_text_shadow(g_w - 70, y, num, 2,
                         (j == 0) ? pack(255, 255, 255) : pack(170, 180, 190));
    }

    /* tela de morte */
    if (!s->alive) {
        tw = text_width("VOCE MORREU", 8);
        draw_text_shadow((g_w - tw) / 2, g_h / 2 - 100, "VOCE MORREU", 8, pack(255, 70, 70));
        int_to_str(lastScore, num);
        buf[0] = 0;
        scat(buf, "PONTOS: ");
        scat(buf, num);
        tw = text_width(buf, 4);
        draw_text_shadow((g_w - tw) / 2, g_h / 2 - 20, buf, 4, pack(255, 255, 255));
        if (deadT > 1.2f) {
            tw = text_width("PRESSIONE X PARA JOGAR DE NOVO", 3);
            x = (g_w - tw) / 2;
            draw_text_shadow(x, g_h / 2 + 50, "PRESSIONE X PARA JOGAR DE NOVO", 3,
                             pack(255, 255, 100));
        }
    }
}

static void render(void)
{
    int i;
    fill_rect(0, 0, g_w, g_h, pack(22, 28, 34)); /* fundo #161C22 */
    draw_grid();
    draw_border();
    draw_food();
    for (i = 1; i < MAX_SNAKES; i++) draw_snake(i);
    draw_snake(0);
    draw_minimap();
    draw_hud();
}

/* ------------------------------------------------------------------ */
/* Sistema PS3                                                         */
/* ------------------------------------------------------------------ */
#ifdef __lv2ppu__
static void sysutil_cb(u64 status, u64 param, void *usrdata)
{
    (void)param;
    (void)usrdata;
    if (status == SYSUTIL_EXIT_GAME) g_quit = 1;
}
#endif

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    SDL_Joystick *joy = NULL;
    SDL_Event ev;
    Uint32 last, now;
    float dt;
    int running = 1, i;

    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) {
        if (SDL_Init(SDL_INIT_VIDEO) < 0) return 1;
    }

    screen = SDL_SetVideoMode(1280, 720, 32, SDL_SWSURFACE);
    if (!screen) {
        SDL_Quit();
        return 1;
    }
    g_w = screen->w;
    g_h = screen->h;
    pix = (Uint32 *)screen->pixels;
    stride = screen->pitch / 4;
    rsh = screen->format->Rshift;
    gsh = screen->format->Gshift;
    bsh = screen->format->Bshift;
    amask = screen->format->Amask;
    SDL_ShowCursor(SDL_DISABLE);

    if (SDL_NumJoysticks() > 0) joy = SDL_JoystickOpen(0);

#ifdef __lv2ppu__
    sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, sysutil_cb, NULL);
#endif

    rngs ^= (unsigned int)SDL_GetTicks() * 2654435761u;
    if (rngs == 0) rngs = 2463534242u;

    for (i = 0; i < BORDER_N; i++) {
        bcos[i] = cosf((float)i * TWO_PI / (float)BORDER_N);
        bsin[i] = sinf((float)i * TWO_PI / (float)BORDER_N);
    }

    memset(snakes, 0, sizeof(snakes));
    memset(food, 0, sizeof(food));
    ambientCount = 0;

    while (ambientCount < AMBIENT_TARGET) spawn_ambient();
    for (i = 0; i < MAX_SNAKES; i++) spawn_snake(i);
    camx = snakes[0].x[0];
    camy = snakes[0].y[0];
    zoom = 1.0f / powf(1.0f + snakes[0].mass / 180.0f, 0.42f);

    last = SDL_GetTicks();

    while (running && !g_quit) {
#ifdef __lv2ppu__
        sysUtilCheckCallback();
#endif
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) running = 0;
        }

        now = SDL_GetTicks();
        dt = (float)(now - last) / 1000.0f;
        last = now;
        if (dt > 0.05f) dt = 0.05f;
        if (dt < 0.001f) dt = 0.001f;

        game_update(dt, joy);

        if (SDL_MUSTLOCK(screen)) SDL_LockSurface(screen);
        render();
        if (SDL_MUSTLOCK(screen)) SDL_UnlockSurface(screen);

        SDL_Flip(screen);
    }

    if (joy) SDL_JoystickClose(joy);
    SDL_Quit();
    return 0;
}
