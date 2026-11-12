// Cobra Arena - Estilo Slither.io Otimizado para PS3 (PSL1GHT)
// Versao 3: cobras com skins aleatorias, minimapa menor, comida em forma de bolo 🎂
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <malloc.h>
#include <ppu-types.h>
#include <sysutil/sysutil.h>
#include <sysutil/video.h>
#include <rsx/rsx.h>
#include <io/pad.h>

#define CB_SIZE   (0x100000)
#define HOST_SIZE (32 * 1024 * 1024)

#define WORLD  12000.0f   // mapa grande
#define MAXSEG 900        // cobras podem ficar bem compridas
#define NSN    35         // mais cobras no mapa
#define NPEL   4000       // mais bolos espalhados
#define SEGD   5.0f       // distancia entre segmentos
#define PI     3.14159265f
#define NSKIN  5          // quantidade de skins diferentes

typedef struct {
    float x[MAXSEG], y[MAXSEG];
    int len, alive, respawn, boost, grow, score, tick, inv;
    float ang, tgt;
    u32 col, col2;   // cor principal e cor secundaria da skin
    int skin;        // padrao da skin (0..NSKIN-1)
} Snake;

typedef struct {
    float x, y;
    int size, active;
    u32 col;
} Pellet;

static const u32 palette[8] = {
    0x00FF4D4D, 0x0055D6FF, 0x0066FF66, 0x00FFD23F,
    0x00C77DFF, 0x00FF8FAB, 0x0000E5C0, 0x00FF9F43
};

static gcmContextData *ctx;
static u32 W, H;
static u32 *fb[2];
static u32 fboff[2];
static u32 *back;
static int cur = 0, first_flip = 1;
static volatile int running = 1;

static void sysutil_cb(u64 status, u64 param, void *usrdata)
{
    (void)param; (void)usrdata;
    if (status == SYSUTIL_EXIT_GAME) running = 0;
}

static void init_screen(void)
{
    void *host = memalign(1024 * 1024, HOST_SIZE);
    rsxInit(&ctx, CB_SIZE, HOST_SIZE, host);

    videoState st;
    videoGetState(VIDEO_PRIMARY, 0, &st);
    u32 resid = st.displayMode.resolution;
    if (videoGetResolutionAvailability(VIDEO_PRIMARY, VIDEO_RESOLUTION_720, VIDEO_ASPECT_AUTO, 0))
        resid = VIDEO_RESOLUTION_720;

    videoResolution res;
    videoGetResolution(resid, &res);
    W = res.width;
    H = res.height;

    videoConfiguration vc;
    memset(&vc, 0, sizeof(vc));
    vc.resolution = resid;
    vc.format = VIDEO_BUFFER_FORMAT_XRGB;
    vc.aspect = VIDEO_ASPECT_AUTO;
    vc.pitch = W * 4;
    videoConfigure(VIDEO_PRIMARY, &vc, NULL, 0);
    videoGetState(VIDEO_PRIMARY, 0, &st);

    gcmSetFlipMode(GCM_FLIP_VSYNC);

    for (int i = 0; i < 2; i++) {
        fb[i] = (u32 *)rsxMemalign(64, W * H * 4);
        gcmAddressToOffset(fb[i], &fboff[i]);
        gcmSetDisplayBuffer(i, fboff[i], W * 4, W, H);
    }
    back = (u32 *)memalign(64, W * H * 4);
    memset(back, 0, W * H * 4);
    gcmResetFlipStatus();
}

static void wait_flip(void)
{
    while (gcmGetFlipStatus() != 0) usleep(200);
    gcmResetFlipStatus();
}

static void present(void)
{
    if (!first_flip) wait_flip();
    else gcmResetFlipStatus();
    memcpy(fb[cur], back, W * H * 4);
    gcmSetFlip(ctx, cur);
    rsxFlushBuffer(ctx);
    gcmSetWaitFlip(ctx);
    cur ^= 1;
    first_flip = 0;
}

static void fill_rect(int x, int y, int w, int h, u32 c)
{
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w, y1 = y + h;
    if (x1 > (int)W) x1 = W;
    if (y1 > (int)H) y1 = H;
    for (int yy = y0; yy < y1; yy++) {
        u32 *p = back + yy * W;
        for (int xx = x0; xx < x1; xx++) p[xx] = c;
    }
}

static void circle(int cx, int cy, int r, u32 c)
{
    if (r < 1) r = 1;
    if (cx + r < 0 || cx - r >= (int)W || cy + r < 0 || cy - r >= (int)H) return;
    for (int dy = -r; dy <= r; dy++) {
        int y = cy + dy;
        if (y < 0 || y >= (int)H) continue;
        int hw = (int)sqrtf((float)(r * r - dy * dy));
        int x0 = cx - hw, x1 = cx + hw;
        if (x0 < 0) x0 = 0;
        if (x1 >= (int)W) x1 = W - 1;
        u32 *p = back + y * W;
        for (int x = x0; x <= x1; x++) p[x] = c;
    }
}

static void dot(int cx, int cy, int sz, u32 c)
{
    fill_rect(cx - sz / 2, cy - sz / 2, sz, sz, c);
}

static float clampf(float v, float a, float b)
{
    return v < a ? a : (v > b ? b : v);
}

typedef struct { char c; const char *p; } Glyph;
static const Glyph glyphs[] = {
    {'0', "###" "#.#" "#.#" "#.#" "###"}, {'1', ".#." "##." ".#." ".#." "###"},
    {'2', "###" "..#" "###" "#.." "###"}, {'3', "###" "..#" "###" "..#" "###"},
    {'4', "#.#" "#.#" "###" "..#" "..#"}, {'5', "###" "#.." "###" "..#" "###"},
    {'6', "###" "#.." "###" "#.#" "###"}, {'7', "###" "..#" "..#" "..#" "..#"},
    {'8', "###" "#.#" "###" "#.#" "###"}, {'9', "###" "#.#" "###" "..#" "###"},
    {'A', ".#." "#.#" "###" "#.#" "#.#"}, {'B', "##." "#.#" "##." "#.#" "##."},
    {'C', "###" "#.." "#.." "#.." "###"}, {'E', "###" "#.." "###" "#.." "###"},
    {'G', "###" "#.." "#.#" "#.#" "###"}, {'M', "#.#" "###" "###" "#.#" "#.#"},
    {'O', "###" "#.#" "#.#" "#.#" "###"}, {'P', "###" "#.#" "###" "#.." "#.."},
    {'R', "##." "#.#" "##." "#.#" "#.#"}, {'S', "###" "#.." "###" "..#" "###"},
    {'T', "###" ".#." ".#." ".#." ".#."}, {'U', "#.#" "#.#" "#.#" "#.#" "###"},
    {'V', "#.#" "#.#" "#.#" "#.#" ".#."}, {'X', "#.#" "#.#" ".#." "#.#" "#.#"},
};

static const char *find_glyph(char c)
{
    for (unsigned i = 0; i < sizeof(glyphs) / sizeof(glyphs[0]); i++)
        if (glyphs[i].c == c) return glyphs[i].p;
    return NULL;
}

static int text_width(const char *s, int sc) { return (int)strlen(s) * 4 * sc - sc; }

static void draw_text(const char *s, int x, int y, int sc, u32 c)
{
    for (; *s; s++) {
        const char *g = find_glyph(*s);
        if (g)
            for (int r = 0; r < 5; r++)
                for (int k = 0; k < 3; k++)
                    if (g[r * 3 + k] == '#') fill_rect(x + k * sc, y + r * sc, sc, sc, c);
        x += 4 * sc;
    }
}

static Snake sn[NSN];
static Pellet pel[NPEL];
static int pelrot = 0, dead_t = 0, x_prev = 0, frame = 0;
static float camx = WORLD / 2, camy = WORLD / 2;
static float zoom = 1.0f;
static float pad_ax = 0, pad_ay = 0, pad_mag = 0;
static int pad_boost = 0, pad_x = 0;

static float frand(void) { return (float)rand() / (float)RAND_MAX; }

// Cobras mais grossas (base 24) e ainda engrossam conforme crescem
static float radius_of(const Snake *s)
{
    int l = s->len > 700 ? 700 : s->len;
    return 24.0f + l * 0.045f;
}

// Cor do segmento j, de acordo com a skin da cobra
static u32 seg_color(const Snake *s, int j)
{
    switch (s->skin) {
    case 0:  return ((j / 3) % 2) ? s->col : s->col2;          // listras
    case 1:  return (j % 4 == 0) ? s->col2 : s->col;           // manchas
    case 2:  return (j % 6 < 3) ? s->col : s->col2;            // aneis
    case 3:  return (j % 2) ? s->col : ((s->col >> 1) & 0x007F7F7F); // escamas
    default: return s->col;                                    // lisa
    }
}

static void add_pellet(float x, float y, int size)
{
    for (int k = 0; k < NPEL; k++) {
        int i = (pelrot + k) % NPEL;
        if (!pel[i].active) {
            pel[i].x = x; pel[i].y = y; pel[i].size = size; pel[i].active = 1;
            pel[i].col = palette[rand() % 8];
            pelrot = (i + 1) % NPEL;
            return;
        }
    }
}

static void spawn_snake(int i)
{
    Snake *s = &sn[i];
    float x = 1000.0f + frand() * (WORLD - 2000.0f);
    float y = 1000.0f + frand() * (WORLD - 2000.0f);
    s->ang = frand() * 2 * PI - PI;
    s->tgt = s->ang;

    if (i == 0) s->len = 30;
    else        s->len = 30 + (rand() % 120);

    for (int j = 0; j < s->len; j++) {
        s->x[j] = x - cosf(s->ang) * SEGD * j;
        s->y[j] = y - sinf(s->ang) * SEGD * j;
    }
    s->alive = 1; s->respawn = 0; s->boost = 0; s->grow = 0; s->tick = 0;
    s->inv = 90;  // 1,5s de protecao ao nascer

    // Skin aleatoria: duas cores e um padrao diferente pra cada cobra
    s->col  = palette[rand() % 8];
    s->col2 = palette[rand() % 8];
    if (s->col2 == s->col) s->col2 = palette[(rand() % 7 + 1 + (s->col - palette[0]) / 0x1) % 8];
    s->skin = rand() % NSKIN;
}

static void kill_snake(int i)
{
    Snake *s = &sn[i];
    // Quando morre vira uma chuva de bolos
    for (int j = 0; j < s->len; j += 3)
        add_pellet(s->x[j] + (frand() - 0.5f) * 25, s->y[j] + (frand() - 0.5f) * 25, 2);
    s->alive = 0;
    s->respawn = 180;
    if (i == 0) dead_t = 0;
}

static void bot_think(int i)
{
    Snake *s = &sn[i];
    float hx = s->x[0], hy = s->y[0];
    s->tgt += (frand() - 0.5f) * 0.3f;

    if ((s->tick + i) % 5 == 0) {
        float best = 700.0f * 700.0f;
        int bi = -1;
        for (int k = 0; k < NPEL; k++) {
            if (!pel[k].active) continue;
            float dx = pel[k].x - hx, dy = pel[k].y - hy, d = dx * dx + dy * dy;
            if (d < best) { best = d; bi = k; }
        }
        if (bi >= 0) s->tgt = atan2f(pel[bi].y - hy, pel[bi].x - hx);
    }

    if (hx < 600 || hx > WORLD - 600 || hy < 600 || hy > WORLD - 600)
        s->tgt = atan2f(WORLD / 2 - hy, WORLD / 2 - hx);
}

static void update_snake(int i)
{
    Snake *s = &sn[i];
    if (!s->alive) {
        if (i > 0 && --s->respawn <= 0) spawn_snake(i);
        return;
    }
    s->tick++;
    if (s->inv > 0) s->inv--;

    if (i == 0) {
        // Só muda a direção se o analógico/direcional estiver realmente sendo usado
        if (pad_mag > 0.25f) s->tgt = atan2f(pad_ay, pad_ax);
        s->boost = pad_boost;
    } else {
        bot_think(i);
        s->boost = (s->len > 60 && (s->tick / 120) % 4 == 0) ? 1 : 0;
    }

    float d = s->tgt - s->ang;
    while (d > PI) d -= 2 * PI;
    while (d < -PI) d += 2 * PI;
    if (d > 0.08f) d = 0.08f;
    if (d < -0.08f) d = -0.08f;
    s->ang += d;
    if (s->ang > PI) s->ang -= 2 * PI;
    if (s->ang < -PI) s->ang += 2 * PI;

    float sp = 3.6f;
    if (s->boost && s->len > 20) {
        sp = 6.4f;
        if (s->tick % 6 == 0) {
            s->len--;
            add_pellet(s->x[s->len], s->y[s->len], 1);
        }
    }
    s->x[0] += cosf(s->ang) * sp;
    s->y[0] += sinf(s->ang) * sp;

    for (int j = 1; j < s->len; j++) {
        float dx = s->x[j - 1] - s->x[j], dy = s->y[j - 1] - s->y[j];
        float dd = sqrtf(dx * dx + dy * dy);
        if (dd > SEGD) {
            float f = (dd - SEGD) / dd;
            s->x[j] += dx * f;
            s->y[j] += dy * f;
        }
    }

    if (s->x[0] < 0 || s->x[0] > WORLD || s->y[0] < 0 || s->y[0] > WORLD) { kill_snake(i); return; }

    // Comer bolos
    float rs = radius_of(s), er = rs + 22;
    for (int k = 0; k < NPEL; k++) {
        if (!pel[k].active) continue;
        float dx = pel[k].x - s->x[0], dy = pel[k].y - s->y[0];
        if (dx * dx + dy * dy < er * er) {
            pel[k].active = 0;
            s->grow += pel[k].size;
            s->score += pel[k].size * 10;
        }
    }
    // Cada ponto de crescimento = 1 segmento (cresce rapido)
    while (s->grow >= 1 && s->len < MAXSEG) {
        s->x[s->len] = s->x[s->len - 1];
        s->y[s->len] = s->y[s->len - 1];
        s->len++;
        s->grow -= 1;
    }
    if (s->len >= MAXSEG) s->grow = 0;

    // Colisao com as outras cobras (pula de 2 em 2 segmentos pra ficar leve)
    if (s->inv > 0) return;
    for (int o = 0; o < NSN; o++) {
        if (o == i || !sn[o].alive) continue;
        float lim = (rs + radius_of(&sn[o])) * 0.7f;
        lim *= lim;
        for (int j = 0; j < sn[o].len; j += 2) {
            float dx = sn[o].x[j] - s->x[0], dy = sn[o].y[j] - s->y[0];
            if (dx * dx + dy * dy < lim) { kill_snake(i); return; }
        }
    }
}

static void init_game(void)
{
    memset(pel, 0, sizeof(pel));
    for (int k = 0; k < NPEL; k++) add_pellet(frand() * WORLD, frand() * WORLD, 1 + (rand() % 6 == 0));
    for (int i = 0; i < NSN; i++) { sn[i].score = 0; spawn_snake(i); }
    dead_t = 0;
}

// ---- LEITURA DO CONTROLE (CORRIGIDA) ----
// ioPadGetData às vezes volta com len == 0 (sem dado novo) e os campos vêm
// zerados, o que era lido como "analógico todo pra cima/esquerda".
// Agora só aceitamos leituras válidas e guardamos a última boa.
static padData last_pad;
static int have_pad = 0;

static void read_pad(void)
{
    padInfo info;
    padData d;
    pad_ax = pad_ay = pad_mag = 0;
    pad_boost = pad_x = 0;

    ioPadGetInfo(&info);
    if (!info.status[0]) { have_pad = 0; return; }

    memset(&d, 0, sizeof(d));
    ioPadGetData(0, &d);
    if (d.len > 0) {
        last_pad = d;
        have_pad = 1;
    }
    if (!have_pad) return;
    d = last_pad;

    float ax = ((int)d.ANA_L_H - 128) / 128.0f;
    float ay = ((int)d.ANA_L_V - 128) / 128.0f;

    if (fabsf(ax) < 0.20f) ax = 0;
    if (fabsf(ay) < 0.20f) ay = 0;

    if (d.BTN_LEFT)  ax = -1;
    if (d.BTN_RIGHT) ax = 1;
    if (d.BTN_UP)    ay = -1;
    if (d.BTN_DOWN)  ay = 1;

    pad_ax = ax; pad_ay = ay;
    pad_mag = sqrtf(ax * ax + ay * ay);

    pad_boost = d.BTN_CROSS;
    pad_x = d.BTN_CROSS;
}

static void update_game(void)
{
    for (int i = 0; i < NSN; i++) update_snake(i);

    int count = 0;
    for (int k = 0; k < NPEL; k++) count += pel[k].active;
    for (int n = 0; n < 15 && count < NPEL - 20; n++, count++)
        add_pellet(frand() * WORLD, frand() * WORLD, 1 + (rand() % 6 == 0));

    if (sn[0].alive) {
        camx = sn[0].x[0];
        camy = sn[0].y[0];
        // Zoom suave: quanto maior a cobra, mais a camera se afasta
        float target = 1.0f / (1.0f + (radius_of(&sn[0]) - 24.0f) * 0.018f);
        zoom += (target - zoom) * 0.03f;
    } else {
        dead_t++;
        if (dead_t > 60 && pad_x && !x_prev) {
            spawn_snake(0);
            sn[0].score = 0;
        }
    }
    x_prev = pad_x;
}

// Bolo estilo emoji 🎂: duas camadas, cobertura escorrendo, vela e chama
static void draw_cake(int x, int y, int u, u32 frost, u32 sponge)
{
    int sp = u < 2 ? 1 : u / 2;
    fill_rect(x - 5 * u, y + 3 * u, 10 * u, u, 0x00E8E8E8);            // prato
    fill_rect(x - 4 * u, y, 8 * u, 3 * u, sponge);                     // camada de baixo
    fill_rect(x - 4 * u, y, 8 * u, u, frost);                          // cobertura de baixo
    fill_rect(x - 4 * u, y + u, u, u, frost);                          // pingo esquerdo
    fill_rect(x + 3 * u, y + u, u, u, frost);                          // pingo direito
    fill_rect(x - 3 * u, y - 3 * u, 6 * u, 3 * u, sponge);             // camada de cima
    fill_rect(x - 3 * u, y - 3 * u, 6 * u, u, frost);                  // cobertura de cima
    fill_rect(x - 3 * u, y - 2 * u, u, u, frost);                      // pingo de cima
    fill_rect(x + 2 * u, y - 2 * u, u, u, frost);                      // pingo de cima
    fill_rect(x - 2 * u, y - u, sp, sp, 0x00FFFFFF);                   // confete
    fill_rect(x + 1 * u, y + u, sp, sp, 0x00FF4D4D);                   // confete
    fill_rect(x - sp / 2, y - 6 * u, sp, 3 * u, 0x00FFFFFF);           // vela
    circle(x, y - 7 * u, (u / 2) + 1, 0x00FF9F1C);                     // chama
    circle(x, y - 7 * u, (u / 4) + 1, 0x00FFF176);                     // miolo da chama
}

// Minimapa menor, no canto superior direito, pra nao atrapalhar a tela
static void draw_minimap(float sc, float hw, float hh, float base)
{
    int msz = (int)(170 * base);
    if (msz < 110) msz = 110;
    int mar = (int)(14 * base);
    int mx = (int)W - msz - mar, my = mar;
    float ms = (float)msz / WORLD;

    fill_rect(mx - 2, my - 2, msz + 4, msz + 4, 0x00C8D4E0);  // borda
    fill_rect(mx, my, msz, msz, 0x00080D14);                  // fundo

    // Retangulo da area visivel na tela
    int rx0 = mx + (int)(clampf(camx - hw / sc, 0, WORLD) * ms);
    int rx1 = mx + (int)(clampf(camx + hw / sc, 0, WORLD) * ms);
    int ry0 = my + (int)(clampf(camy - hh / sc, 0, WORLD) * ms);
    int ry1 = my + (int)(clampf(camy + hh / sc, 0, WORLD) * ms);
    u32 vc = 0x00406080;
    fill_rect(rx0, ry0, rx1 - rx0, 1, vc);
    fill_rect(rx0, ry1 - 1, rx1 - rx0, 1, vc);
    fill_rect(rx0, ry0, 1, ry1 - ry0, vc);
    fill_rect(rx1 - 1, ry0, 1, ry1 - ry0, vc);

    int hs = (int)(3 * base + 0.5f);
    if (hs < 3) hs = 3;

    // Bots: so a cabeca, pra nao poluir o minimapa
    for (int i = NSN - 1; i >= 1; i--) {
        Snake *s = &sn[i];
        if (!s->alive) continue;
        dot(mx + (int)(s->x[0] * ms), my + (int)(s->y[0] * ms), hs, s->col);
    }

    // Jogador por ultimo, em branco
    if (sn[0].alive) {
        Snake *s = &sn[0];
        int px = mx + (int)(s->x[0] * ms), py = my + (int)(s->y[0] * ms);
        dot(px, py, hs + 3, 0x00000000);
        dot(px, py, hs + 1, 0x00FFFFFF);
    }
}

static void render(void)
{
    frame++;
    float base = (float)W / 1280.0f;
    float sc = base * zoom;
    float hw = W / 2.0f, hh = H / 2.0f;
    u32 n = W * H;
    for (u32 i = 0; i < n; i++) back[i] = 0x000B1018;

    int sx0 = (int)((0 - camx) * sc + hw), sx1 = (int)((WORLD - camx) * sc + hw);
    int sy0 = (int)((0 - camy) * sc + hh), sy1 = (int)((WORLD - camy) * sc + hh);
    u32 red = 0x004A0E15;
    if (sx0 > 0) fill_rect(0, 0, sx0, H, red);
    if (sx1 < (int)W) fill_rect(sx1, 0, W - sx1, H, red);
    if (sy0 > 0) fill_rect(0, 0, W, sy0, red);
    if (sy1 < (int)H) fill_rect(0, sy1, W, H - sy1, red);

    float gx0 = floorf((camx - hw / sc) / 120.0f) * 120.0f;
    float gy0 = floorf((camy - hh / sc) / 120.0f) * 120.0f;
    for (float gx = gx0; gx < camx + hw / sc; gx += 120.0f) {
        if (gx < 0 || gx > WORLD) continue;
        for (float gy = gy0; gy < camy + hh / sc; gy += 120.0f) {
            if (gy < 0 || gy > WORLD) continue;
            fill_rect((int)((gx - camx) * sc + hw), (int)((gy - camy) * sc + hh), 3, 3, 0x001A2636);
        }
    }

    // Comida em formato de bolo (balancando de leve)
    for (int k = 0; k < NPEL; k++) {
        if (!pel[k].active) continue;
        int px = (int)((pel[k].x - camx) * sc + hw), py = (int)((pel[k].y - camy) * sc + hh);
        if (px < -60 || px >= (int)W + 60 || py < -60 || py >= (int)H + 60) continue;
        int u = (int)((pel[k].size >= 2 ? 4.0f : 2.5f) * sc + 0.5f);
        if (u < 2) u = 2;
        int bob = (int)(sinf(frame * 0.08f + k * 0.7f) * u * 0.6f);
        u32 sponge = pel[k].size >= 2 ? 0x00704020 : 0x00E2B070;
        draw_cake(px, py + bob, u, pel[k].col, sponge);
    }

    // Cobras com skins (pula segmentos fora da tela; bots desenham de 3 em 3 pra ganhar FPS)
    for (int i = NSN - 1; i >= 0; i--) {
        Snake *s = &sn[i];
        if (!s->alive) continue;
        int r = (int)(radius_of(s) * sc);
        if (r < 3) r = 3;

        int step = (i == 0) ? 2 : 3;
        for (int j = s->len - 1; j >= 0; j -= step) {
            int px = (int)((s->x[j] - camx) * sc + hw), py = (int)((s->y[j] - camy) * sc + hh);
            if (px < -r || px > (int)W + r || py < -r || py > (int)H + r) continue;
            u32 c = seg_color(s, j);
            // Piscando enquanto esta protegida
            if (s->inv > 0 && (frame / 4) % 2 == 0) c = s->col;
            circle(px, py, r, c);
        }

        int hx = (int)((s->x[0] - camx) * sc + hw), hy = (int)((s->y[0] - camy) * sc + hh);
        circle(hx, hy, r + 2, s->col);
        for (int e = -1; e <= 1; e += 2) {
            int ex = hx + (int)(cosf(s->ang + e * 0.7f) * r * 0.65f);
            int ey = hy + (int)(sinf(s->ang + e * 0.7f) * r * 0.65f);
            circle(ex, ey, r * 40 / 100 + 1, 0x00FFFFFF);
            circle(ex + (int)(cosf(s->ang) * r * 15 / 100), ey + (int)(sinf(s->ang) * r * 15 / 100), r * 20 / 100 + 1, 0x00000000);
        }
    }

    // Minimapa no canto superior direito
    draw_minimap(sc, hw, hh, base);

    char buf[32];
    int ui = (int)(4 * base + 0.5f);
    if (ui < 3) ui = 3;
    draw_text("SCORE", 24, 20, ui, 0x00FFFFFF);
    sprintf(buf, "%d", sn[0].score);
    draw_text(buf, 24, 20 + 7 * ui, ui + 2, 0x00FFD23F);
    draw_text("X: TURBO", 24, (int)H - 8 * ui, ui, 0x0000E5C0);

    if (!sn[0].alive) {
        int bw = text_width("GAME OVER", ui * 2) + 80, bh = 16 * ui + 40;
        fill_rect((int)W / 2 - bw / 2, (int)H / 2 - bh / 2, bw, bh, 0x00000000);
        draw_text("GAME OVER", (int)W / 2 - text_width("GAME OVER", ui * 2) / 2, (int)H / 2 - b