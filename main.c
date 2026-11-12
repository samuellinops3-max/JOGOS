// Pipa Combate 3D - PS3 (PSL1GHT) Versão Definitiva
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

#define WORLD_W   1280.0f
#define WORLD_H   720.0f
#define MAX_ENEMIES 6
#define PI        3.14159265f

typedef struct {
    float x, y;
    float vx, vy;
    float angle;
    int active;
    int score_val;
} PipaBot;

static gcmContextData *ctx;
static u32 W, H;
static u32 *fb[2];
static u32 fboff[2];
static u32 *back;
static int cur = 0, first_flip = 1;
static volatile int running = 1;

// Estado do Jogador (Pipa)
static float player_x = 640.0f;
static float player_y = 360.0f;
static float player_vx = 0.0f;
static float player_vy = 0.0f;
static float player_angle = 0.0f;
static int player_score = 0;
static int player_line_pulled = 0; // Botão R1 (Puxar linha / Arrancada)
static int player_cutting = 0;    // Botão X (Dar bote / Cortar)

// Inimigos (Outras pipas no céu)
static PipaBot enemies[MAX_ENEMIES];

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

static void draw_kite(int cx, int cy, float angle, u32 body_col, u32 tail_col)
{
    // Desenha uma pipa estilizada em losango com rabiola simples
    int size = 20;
    // Rabiola (cauda) para baixo
    for (int t = 1; t <= 6; t++) {
        int tx = cx - (int)(sinf(angle) * t * 6);
        int ty = cy + t * 8 + (int)(t * sinf(t) * 2);
        fill_rect(tx - 1, ty - 1, 3, 3, tail_col);
    }
    // Corpo da Pipa (Losango aproximado por blocos rápidos)
    fill_rect(cx - 3, cy - size, 6, size * 2, body_col);
    fill_rect(cx - size / 2, cy - 3, size, 6, body_col);
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
    {'O', "###" "#.#" "#.#" "#.#" "###"}, {'P', "###" "#.#" "###" "#.." "#.."},
    {'R', "##." "#.#" "##." "#.#" "#.#"}, {'S', "###" "#.." "###" "..#" "###"},
    {'T', "###" ".#." ".#." ".#." ".#."}, {'U', "#.#" "#.#" "#.#" "#.#" "###"},
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
    player_x = 640.0f;
    player_y = 360.0f;
    player_score = 0;

    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemies[i].x = 100.0f + (rand() % 1080);
        enemies[i].y = 100.0f + (rand() % 400);
        enemies[i].vx = (rand() % 3 - 1) * 1.5f;
        enemies[i].vy = (rand() % 3 - 1) * 1.0f;
        enemies[i].active = 1;
        enemies[i].score_val = 10 + (rand() % 20);
    }
}

static void read_pad(void)
{
    padInfo info;
    padData d;
    ioPadGetInfo(&info);
    if (info.status[0]) {
        ioPadGetData(0, &d);

        float move_x = 0.0f;
        float move_y = 0.0f;

        // 1. Controlo por Setinhas (D-PAD)
        if (d.BTN_LEFT)  move_x = -1.0f;
        if (d.BTN_RIGHT) move_x = 1.0f;
        if (d.BTN_UP)    move_y = -1.0f;
        if (d.BTN_DOWN)  move_y = 1.0f;

        // 2. Controlo por Analógico L3 (com zona morta limpa)
        float ax = ((int)d.ANA_L_H - 128) / 128.0f;
        float ay = ((int)d.ANA_L_V - 128) / 128.0f;
        if (fabsf(ax) > 0.15f || fabsf(ay) > 0.15f) {
            move_x = ax;
            move_y = ay;
        }

        float speed = 4.0f;
        player_line_pulled = d.BTN_R1 ? 1 : 0;
        player_cutting = d.BTN_CROSS ? 1 : 0;

        if (player_line_pulled) {
            speed = 7.5f; // Puxada de linha / Arrancada
        }

        player_vx = move_x * speed;
        player_vy = move_y * speed;
        player_angle = move_x * 0.4f; // Inclinação da pipa
    }
}

static void update_game(void)
{
    // Atualiza posição do jogador com limites no ecrã
    player_x += player_vx;
    player_y += player_vy;

    if (player_x < 50.0f) player_x = 50.0f;
    if (player_x > WORLD_W - 50.0f) player_x = WORLD_W - 50.0f;
    if (player_y < 50.0f) player_y = 50.0f;
    if (player_y > WORLD_H - 100.0f) player_y = WORLD_H - 100.0f;

    // Atualiza inimigos (pipas rivais a voar)
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) {
            // Respawn da pipa inimiga após ser cortada
            enemies[i].x = 100.0f + (rand() % 1080);
            enemies[i].y = 50.0f + (rand() % 200);
            enemies[i].active = 1;
            continue;
        }

        enemies[i].x += enemies[i].vx;
        enemies[i].y += enemies[i].vy;

        // Movimento de vai e vem no vento
        if (enemies[i].x < 50.0f || enemies[i].x > WORLD_W - 50.0f) enemies[i].vx *= -1.0f;
        if (enemies[i].y < 40.0f || enemies[i].y > WORLD_H - 200.0f) enemies[i].vy *= -1.0f;

        // Sistema de Combate / Corte (se o jogador estiver a dar bote 'X' e encostar na pipa rival)
        float dx = enemies[i].x - player_x;
        float dy = enemies[i].y - player_y;
        float dist_sq = dx * dx + dy * dy;

        if (dist_sq < (35.0f * 35.0f)) {
            // Pipa cortada!
            player_score += enemies[i].score_val;
            enemies[i].active = 0; 
        }
    }
}

static void render(void)
{
    float sc = (float)W / 1280.0f;
    u32 n = W * H;

    // 1. Céu azul degradê simulado (fundo)
    for (u32 i = 0; i < n; i++) back[i] = 0x003388EE;

    // Nuvens simples de fundo fixas
    fill_rect(100, 80, 180, 40, 0x0088CCFF);
    fill_rect(850, 120, 220, 50, 0x0088CCFF);
    fill_rect(500, 60, 150, 35, 0x0088CCFF);

    // Linha da pipa do jogador (ligada à base inferior central do ecrã)
    int base_origin_x = (int)W / 2;
    int base_origin_y = (int)H;
    int p_draw_x = (int)(player_x * sc);
    int p_draw_y = (int)(player_y * sc);

    // Desenha as pipas inimigas
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) continue;
        int ex = (int)(enemies[i].x * sc);
        int ey = (int)(enemies[i].y * sc);
        draw_kite(ex, ey, 0.0f, 0x00FF3333, 0x00FFFF33); // Pipa vermelha inimiga
    }

    // Desenha a Pipa do Jogador
    draw_kite(p_draw_x, p_draw_y, player_angle, 0x0000FFCC, 0x00FF33FF); // Pipa ciano/azul do jogador

    // HUD / Placar no canto superior
    char buf[32];
    int ui = 4;
    draw_text("PIPA COMBATE 3D", 30, 20, ui, 0x00FFFFFF);
    sprintf(buf, "PONTOS: %d", player_score);
    draw_text(buf, 30, 50, ui, 0x00FFD23F);

    draw_text("R1: PUXAR LINHA  |  X: CORTAR  |  SETINHAS / L3: MOVER", 30, (int)H - 40, 2, 0x00FFFF00);
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
