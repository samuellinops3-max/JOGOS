// Cobra Arena - Controlo por Setinhas (D-Pad) Definitivo para PS3 (PSL1GHT)
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

#define WORLD  6000.0f
#define MAXSEG 600
#define NSN    14
#define NPEL   900
#define PI     3.14159265f

typedef struct {
    float x[MAXSEG], y[MAXSEG];
    int len, alive, respawn, boost, grow, score, tick, immune;
    float ang, tgt;
    u32 col;
} Snake;

typedef struct {
    float x, y;
    int size, active, type;
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
static int pelrot = 0, dead_t = 0, x_prev = 0;
static float camx = WORLD / 2, camy = WORLD / 2;

static float player_target_ang = 0.0f;
static int p_boost = 0, p_x = 0;

static float frand(void) { return (float)rand() / (float)RAND_MAX; }

static float radius_of(const Snake *s)
{
    int l = s->len > 400 ? 400 : s->len;
    return 12.0f + l * 0.05f;
}

static void add_pellet(float x, float y, int size, int type)
{
    for (int k = 0; k < NPEL; k++) {
        int i = (pelrot + k) % NPEL;
        if (!pel[i].active) {
            pel[i].x = x; pel[i].y = y; pel[i].size = size; pel[i].active = 1;
            pel[i].type = type;
            pel[i].col = type == 1 ? 0x00FFD23F : palette[rand() % 8];
            pelrot = (i + 1) % NPEL;
            return;
        }
    }
}

static void spawn_snake(int i)
{
    Snake *s = &sn[i];
    float x = 1500.0f + frand() * (WORLD - 3000.0f);
    float y = 1500.0f + frand() * (WORLD - 3000.0f);
    s->ang = frand() * 2 * PI;
    s->tgt = s->ang;
    
    if (i == 0) {
        s->len = 30;
        s->immune = 180;
    } else {
        s->len = 25 + (rand() % 70);
        s->immune = 0;
    }

    for (int j = 0; j < s->len; j++) {
        s->x[j] = x - cosf(s->ang) * 5.0f * j;
        s->y[j] = y - sinf(s->ang) * 5.0f * j;
    }
    s->alive = 1; s->respawn = 0; s->boost = 0; s->grow = 0; s->tick = 0;
    s->col = palette[(i + rand()) % 8];
}

static void kill_snake(int i)
{
    Snake *s = &sn[i];
    if (i == 0 && s->immune > 0) return;
    for (int j = 0; j < s->len; j += 2)
        add_pellet(s->x[j] + (frand() - 0.5f) * 15, s->y[j] + (frand() - 0.5f) * 15, 2, rand() % 5 == 0 ? 1 : 0);
    s->alive = 0;
    s->respawn = 180;
    if (i == 0) dead_t = 0;
}

static void bot_think(int i)
{
    Snake *s = &sn[i];
    float hx = s->x[0], hy = s->y[0];
    s->tgt += (frand() - 0.5f) * 0.3f;

    if (s->tick % 5 == 0) {
        float best = 500.0f * 500.0f;
        int bi = -1;
        for (int k = 0; k < NPEL; k++) {
            if (!pel[k].active) continue;
            float dx = pel[k].x - hx, dy = pel[k].y - hy, d = dx * dx + dy * dy;
            if (d < best) { best = d; bi = k; }
        }
        if (bi >= 0) s->tgt = atan2f(pel[bi].y - hy, pel[bi].x - hx);
    }

    if (hx < 500 || hx > WORLD - 500 || hy < 500 || hy > WORLD - 500)
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
    if (s->immune > 0) s->immune--;

    if (i == 0) {
        s->tgt = player_target_ang;
        s->boost = p_boost;
    } else {
        bot_think(i);
        s->boost = (s->len > 40 && (s->tick / 120) % 4 == 0) ? 1 : 0;
    }

    float d = s->tgt - s->ang;
    while (d > PI) d -= 2 * PI;
    while (d < -PI) d += 2 * PI;
    if (d > 0.15f) d = 0.15f;
    if (d < -0.15f) d = -0.15f;
    s->ang += d;

    float sp = 3.5f;
    if (s->boost && s->len > 15) {
        sp = 6.0f;
        if (s->tick % 8 == 0) {
            s->len--;
            add_pellet(s->x[s->len], s->y[s->len], 1, 0);
        }
    }
    s->x[0] += cosf(s->ang) * sp;
    s->y[0] += sinf(s->ang) * sp;
    
    for (int iter = 0; iter < 3; iter++) {
        for (int j = 1; j < s->len; j++) {
            float dx = s->x[j - 1] - s->x[j], dy = s->y[j - 1] - s->y[j];
            float dd = sqrtf(dx * dx + dy * dy);
            if (dd > 5.0f) {
                float f = (dd - 5.0f) / dd;
                s->x[j] += dx * f * 0.5f;
                s->y[j] += dy * f * 0.5f;
            }
        }
    }

    if (s->x[0] < 0 || s->x[0] > WORLD || s->y[0] < 0 || s->y[0] > WORLD) { kill_snake(i); return; }

    float rs = radius_of(s), er = rs + 14;
    for (int k = 0; k < NPEL; k++) {
        if (!pel[k].active) continue;
        float dx = pel[k].x - s->x[0], dy = pel[k].y - s->y[0];
        if (dx * dx + dy * dy < er * er) {
            pel[k].active = 0;
            int gain = pel[k].type == 1 ? 4 : pel[k].size;
            s->grow += gain;
            s->score += gain * 10;
        }
    }
    while (s->grow >= 2 && s->len < MAXSEG) {
        s->x[s->len] = s->x[s->len - 1];
        s->y[s->len] = s->y[s->len - 1];
        s->len++;
        s->grow -= 2;
    }
    if (s->len >= MAXSEG) s->grow = 0;

    if (s->immune > 0) return;

    for (int o = 0; o < NSN; o++) {
        if (o == i || !sn[o].alive) continue;
        float lim = (rs + radius_of(&sn[o])) * 0.7f;
        lim *= lim;
        for (int j = 0; j < sn[o].len; j++) {
            float dx = sn[o].x[j] - s->x[0], dy = sn[o].y[j] - s->y[0];
            if (dx * dx + dy * dy < lim) { kill_snake(i); return; }
        }
    }
}

static void init_game(void)
{
    memset(pel, 0, sizeof(pel));
    for (int k = 0; k < NPEL; k++) add_pellet(frand() * WORLD, frand() * WORLD, 1 + (rand() % 3 == 0), rand() % 5 == 0 ? 1 : 0);
    for (int i = 0; i < NSN; i++) { sn[i].score = 0; spawn_snake(i); }
    dead_t = 0;
}

static void read_pad(void)
{
    padInfo info;
    padData d;
    p_boost = p_x = 0;
    
    ioPadGetInfo(&info);
    if (info.status[0]) {
        ioPadGetData(0, &d);
        
        // CONTROLO DIRETO NAS SETINHAS (D-PAD) E ANALÓGICO
        if (d.BTN_LEFT)  player_target_ang = PI;       // Esquerda
        if (d.BTN_RIGHT) player_target_ang = 0.0f;     // Direita
        if (d.BTN_UP)    player_target_ang = -PI / 2;  // Cima
        if (d.BTN_DOWN)  player_target_ang = PI / 2;   // Baixo

        // Suporte opcional ao analógico caso funcione
        float ax = ((int)d.ANA_L_H - 128) / 128.0f;
        float ay = ((int)d.ANA_L_V - 128) / 128.0f;
        float mag = sqrtf(ax * ax + ay * ay);
        if (mag > 0.2f) {
            player_target_ang = atan2f(ay, ax);
        }
        
        p_boost = d.BTN_CROSS;
        p_x = d.BTN_CROSS;
    }
}

static void update_game(void)
{
    for (int i = 0; i < NSN; i++) update_snake(i);

    int count = 0;
    for (int k = 0; k < NPEL; k++) count += pel[k].active;
    for (int n = 0; n < 4 && count < NPEL - 50; n++, count++)
        add_pellet(frand() * WORLD, frand() * WORLD, 1 + (rand() % 3 == 0), rand() % 5 == 0 ? 1 : 0);

    if (sn[0].alive) {
        camx = sn[0].x[0];
        camy = sn[0].y[0];
    }

    if (!sn[0].alive) {
        dead_t++;
        if (dead_t > 60 && p_x && !x_prev) { spawn_snake(0); sn[0].score = 0; }
    }
    x_prev = p_x;
}

static void render(void)
{
    float sc = (float)W / 1280.0f;
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

    for (int k = 0; k < NPEL; k++) {
        if (!pel[k].active) continue;
        int px = (int)((pel[k].x - camx) * sc + hw);
        int py = (int)((pel[k].y - camy) * sc + hh);
        if (px >= -15 && px < (int)W + 15 && py >= -15 && py < (int)H + 15) {
            if (pel[k].type == 1) {
                fill_rect(px - 6, py - 2, 12, 5, 0x00FF9F43); 
                fill_rect(px - 4, py - 1, 8, 2, 0x00FF4D4D);  
            } else {
                circle(px, py, (int)((4 + pel[k].size * 2) * sc), pel[k].col);
                circle(px, py, (int)(2 * sc), 0x00FFFFFF);
            }
        }
    }

    for (int i = NSN - 1; i >= 0; i--) {
        Snake *s = &sn[i];
        if (!s->alive) continue;
        int r = (int)(radius_of(s) * sc);
        u32 dark = (s->col >> 1) & 0x007F7F7F;
        
        for (int j = s->len - 1; j >= 0; j--) {
            int px = (int)((s->x[j] - camx) * sc + hw);
            int py = (int)((s->y[j] - camy) * sc + hh);
            circle(px, py, r, (j % 4 < 2) ? s->col : dark);
        }
        
        int hx = (int)((s->x[0] - camx) * sc + hw);
        int hy = (int)((s->y[0] - camy) * sc + hh);
        circle(hx, hy, r + 1, s->col);
        for (int e = -1; e <= 1; e += 2) {
            int ex = hx + (int)(cosf(s->ang + e * 0.7f) * r * 0.65f);
            int ey = hy + (int)(sinf(s->ang + e * 0.7f) * r * 0.65f);
            circle(ex, ey, r * 38 / 100 + 1, 0x00FFFFFF);
            circle(ex + (int)(cosf(s->ang) * r * 15 / 100), ey + (int)(sinf(s->ang) * r * 15 / 100), r * 18 / 100 + 1, 0x00000000);
        }
    }

    int m_w = 160, m_h = 160;
    int m_x = (int)W - m_w - 24, m_y = 24;
    fill_rect(m_x - 2, m_y - 2, m_w + 4, m_h + 4, 0x00FFFFFF);
    fill_rect(m_x, m_y, m_w, m_h, 0x00141D2B);

    for (int i = 0; i < NSN; i++) {
        if (!sn[i].alive) continue;
        int mx = m_x + (int)((sn[i].x[0] / WORLD) * m_w);
        int my = m_y + (int)((sn[i].y[0] / WORLD) * m_h);
        u32 mcol = (i == 0) ? 0x0055D6FF : 0x00FF4D4D;
        fill_rect(mx - 1, my - 1, 3, 3, mcol);
    }

    char buf[32];
    int ui = (int)(4 * sc + 0.5f);
    if (ui < 3) ui = 3;

    draw_text("SCORE", 24, 20, ui, 0x0055D6FF);
    sprintf(buf, "%d", sn[0].score);
    draw_text(buf, 24, 20 + 7 * ui, ui + 2, 0x00FFD23F);

    draw_text("X: TURBO | SETINHAS: MOVER", 24, (int)H - 8 * ui, ui, 0x0000E5C0);

    if (!sn[0].alive) {
        int bw = text_width("GAME OVER", ui * 2) + 80, bh = 16 * ui + 40;
        fill_rect((int)W / 2 - bw / 2, (int)H / 2 - bh / 2, bw, bh, 0x00000000);
        draw_text("GAME OVER", (int)W / 2 - text_width("GAME OVER", ui * 2) / 2, (int)H / 2 - bh / 2 + 24, ui * 2, 0x00FF4D4D);
        if (dead_t > 60)
            draw_text("PRESS X TO RESPAWN", (int)W / 2 - text_width("PRESS X TO RESPAWN", ui) / 2, (int)H / 2 + ui * 3, ui, 0x00FFFFFF);
    }
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    init_screen();
    ioPadInit(7);
    sysUtilRegisterCallback(0, sysutil_cb, NULL);
    srand(20261111);
    init_game();

    while (running) {
        sysUtilCheckCallback();
        read_pad();
        update_game();
        render();
        present();
    }
    return 0;
}
