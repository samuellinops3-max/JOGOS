// Teste Limpo de Controlo da Cobra - PS3 (PSL1GHT)
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

#define WORLD  4000.0f
#define MAXSEG 100
#define PI     3.14159265f

typedef struct {
    float x[MAXSEG], y[MAXSEG];
    int len;
    float ang, tgt;
    u32 col;
} Snake;

static gcmContextData *ctx;
static u32 W, H;
static u32 *fb[2];
static u32 fboff[2];
static u32 *back;
static int cur = 0, first_flip = 1;
static volatile int running = 1;

static Snake player;
static float player_target_ang = 0.0f;
static float camx = WORLD / 2, camy = WORLD / 2;

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
    {'A', ".#." "#.#" "###" "#.#" "#.#"}, {'C', "###" "#.." "#.." "#.." "###"},
    {'E', "###" "#.." "###" "#.." "###"}, {'I', "###" ".#." ".#." ".#." "###"},
    {'L', "#.." "#.." "#.." "#.." "###"}, {'O', "###" "#.#" "#.#" "#.#" "###"},
    {'P', "###" "#.#" "###" "#.." "#.."}, {'R', "##." "#.#" "##." "#.#" "#.#"},
    {'S', "###" "#.." "###" "..#" "###"}, {'T', "###" ".#." ".#." ".#." ".#."},
    {'U', "#.#" "#.#" "#.#" "#.#" "###"}, {'V', "#.#" "#.#" "#.#" "#.#" ".#."},
};

static const char *find_glyph(char c)
{
    for (unsigned i = 0; i < sizeof(glyphs) / sizeof(glyphs[0]); i++)
        if (glyphs[i].c == c) return glyphs[i].p;
    return NULL;
}

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

static void init_game(void)
{
    player.len = 25;
    float x = WORLD / 2;
    float y = WORLD / 2;
    player.ang = 0.0f;
    player.tgt = 0.0f;
    player.col = 0x0055D6FF; // Ciano vivo

    for (int j = 0; j < player.len; j++) {
        player.x[j] = x - (float)j * 5.0f;
        player.y[j] = y;
    }
}

static void read_pad(void)
{
    padInfo info;
    padData d;
    ioPadGetInfo(&info);
    if (info.status[0]) {
        ioPadGetData(0, &d);

        // 1. Leitura direta e sem filtros complexos do Analógico L3
        int ax = (int)d.ANA_L_H - 128;
        int ay = (int)d.ANA_L_V - 128;

        // Zona morta simples de 15 para evitar drift
        if (ax < -15 || ax > 15 || ay < -15 || ay > 15) {
            player_target_ang = atan2f((float)ay, (float)ax);
        }

        // 2. Setinhas com prioridade total se premidas
        if (d.BTN_LEFT)  player_target_ang = PI;
        if (d.BTN_RIGHT) player_target_ang = 0.0f;
        if (d.BTN_UP)    player_target_ang = -PI / 2.0f;
        if (d.BTN_DOWN)  player_target_ang = PI / 2.0f;
    }
}

static void update_game(void)
{
    player.tgt = player_target_ang;

    // Suavização do ângulo da cabeça
    float diff = player.tgt - player.ang;
    while (diff > PI) diff -= 2 * PI;
    while (diff < -PI) diff += 2 * PI;
    player.ang += diff * 0.15f;

    // Movimento da cabeça
    player.x[0] += cosf(player.ang) * 4.0f;
    player.y[0] += sinf(player.ang) * 4.0f;

    // Seguir o corpo
    for (int j = 1; j < player.len; j++) {
        float dx = player.x[j - 1] - player.x[j];
        float dy = player.y[j - 1] - player.y[j];
        float dist = sqrtf(dx * dx + dy * dy);
        if (dist > 5.0f) {
            float f = (dist - 5.0f) / dist;
            player.x[j] += dx * f * 0.8f;
            player.y[j] += dy * f * 0.8f;
        }
    }

    // Limites do mapa
    if (player.x[0] < 50) player.x[0] = 50;
    if (player.x[0] > WORLD - 50) player.x[0] = WORLD - 50;
    if (player.y[0] < 50) player.y[0] = 50;
    if (player.y[0] > WORLD - 50) player.y[0] = WORLD - 50;

    // Câmara a seguir a cobra
    camx += (player.x[0] - camx) * 0.1f;
    camy += (player.y[0] - camy) * 0.1f;
}

static void render(void)
{
    float sc = (float)W / 1280.0f;
    float hw = W / 2.0f, hh = H / 2.0f;
    u32 n = W * H;

    // Fundo escuro limpo
    for (u32 i = 0; i < n; i++) back[i] = 0x000B1018;

    // Desenhar a cobra
    for (int j = player.len - 1; j >= 0; j--) {
        int px = (int)((player.x[j] - camx) * sc + hw);
        int py = (int)((player.y[j] - camy) * sc + hh);
        circle(px, py, (int)(12 * sc), (j == 0) ? 0x00FFFFFF : player.col);
    }

    // Texto no ecrã para sabermos que está a correr
    draw_text("TESTE DE CONTROLO - ANALOGICO E SETINHAS", 30, 20, 2, 0x0055D6FF);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    init_screen();
    ioPadInit(7);
    sysUtilRegisterCallback(0, sysutil_cb, NULL);
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
