// Mob Control PS3 - Estilo Arcade (PSL1GHT)
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

/* ---------- video ---------- */
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

/* ---------- desenho ---------- */
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

/* fonte 3x5 minima */
typedef struct { char c; const char *p; } Glyph;
static const Glyph glyphs[] = {
    {'0', "###" "#.#" "#.#" "#.#" "###"}, {'1', ".#." "##." ".#." ".#." "###"},
    {'2', "###" "..#" "###" "#.." "###"}, {'3', "###" "..#" "###" "..#" "###"},
    {'4', "#.#" "#.#" "###" "..#" "..#"}, {'5', "###" "#.." "###" "..#" "###"},
    {'6', "###" "#.." "###" "#.#" "###"}, {'7', "###" "..#" "..#" "..#" "..#"},
    {'8', "###" "#.#" "###" "#.#" "###"}, {'9', "###" "#.#" "###" "..#" "###"},
    {'A', ".#." "#.#" "###" "#.#" "#.#"}, {'B', "##." "#.#" "##." "#.#" "##."},
    {'C', "###" "#.." "#.." "#.." "###"}, {'E', "###" "#.." "###" "#.." "###"},
    {'F', "###" "#.." "##." "#.." "#.."}, {'G', "###" "#.." "#.#" "#.#" "###"},
    {'H', "#.#" "#.#" "###" "#.#" "#.#"}, {'I', "###" ".#." ".#." ".#." "###"},
    {'L', "#.." "#.." "#.." "#.." "###"}, {'M', "#.#" "#.#" "###" "#.#" "#.#"},
    {'O', "###" "#.#" "#.#" "#.#" "###"}, {'P', "###" "#.#" "###" "#.." "#.."},
    {'R', "##." "#.#" "##." "#.#" "#.#"}, {'S', "###" "#.." "###" "..#" "###"},
    {'T', "###" ".#." ".#." ".#." ".#."}, {'U', "#.#" "#.#" "#.#" "#.#" "###"},
    {'+', ".#." "###" ".#." "..." "..."}, {'X', "#.#" "#.#" ".#." "#.#" "#.#"},
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

/* ---------- VARIÁVEIS DO JOGO MOB CONTROL ---------- */
#define MAX_MOBS 100

typedef struct {
    float x, y;
    int active;
} Mob;

static Mob mobs[MAX_MOBS];
static float cannon_x = 640.0f;
static int mob_count = 12;
static int score = 0;
static int enemy_base_hp = 500;
static int fire_cooldown = 0;

static void init_game(void)
{
    cannon_x = 640.0f;
    mob_count = 15;
    score = 0;
    enemy_base_hp = 500;
    memset(mobs, 0, sizeof(mobs));
}

static void update_game(void)
{
    // Movimento dos mobs para cima rumo à base inimiga
    for (int i = 0; i < MAX_MOBS; i++) {
        if (mobs[i].active) {
            mobs[i].y -= 6.0f;
            
            // Colisão com a base inimiga no topo (y < 150)
            if (mobs[i].y < 150.0f) {
                mobs[i].active = 0;
                enemy_base_hp--;
                score += 10;
                if (enemy_base_hp < 0) enemy_base_hp = 500; // Reseta base se destruir
            }
        }
    }

    if (fire_cooldown > 0) fire_cooldown--;
}

static void read_pad(void)
{
    padInfo info;
    padData d;
    ioPadGetInfo(&info);
    if (info.status[0]) {
        ioPadGetData(0, &d);

        // CONTROLO NAS SETINHAS (D-PAD) - SUPER FÁCIL E PRECISO!
        if (d.BTN_LEFT) {
            cannon_x -= 12.0f;
            if (cannon_x < 250.0f) cannon_x = 250.0f;
        }
        if (d.BTN_RIGHT) {
            cannon_x += 12.0f;
            if (cannon_x > 1030.0f) cannon_x = 1030.0f;
        }

        // BOTÃO X: Dispara bonecos azuis para cima
        if (d.BTN_CROSS && fire_cooldown == 0) {
            for (int i = 0; i < MAX_MOBS; i++) {
                if (!mobs[i].active) {
                    mobs[i].x = cannon_x + (rand() % 40 - 20);
                    mobs[i].y = (float)H - 180.0f;
                    mobs[i].active = 1;
                    break;
                }
            }
            fire_cooldown = 6; // Taxa de disparo
        }
    }
}

static void render(void)
{
    u32 n = W * H;
    // Fundo estilo deserto / arena do Mob Control
    for (u32 i = 0; i < n; i++) back[i] = 0x002C3E50;

    // Pista central de corrida
    fill_rect(200, 0, 880, H, 0x0034495E);

    // --- BASE INIMIGA NO TOPO ---
    fill_rect(340, 50, 600, 90, 0x00E74C3C);
    draw_text("BASE INIMIGA", 480, 75, 4, 0x00FFFFFF);

    char buf[32];
    sprintf(buf, "HP: %d", enemy_base_hp);
    draw_text(buf, 540, 110, 3, 0x00FFD23F);

    // --- PORTAS MULTIPLICADORAS NO CAMINHO ---
    // Porta Azul (+10)
    fill_rect(380, 350, 220, 50, 0x003498DB);
    draw_text("+ 10", 440, 365, 4, 0x00FFFFFF);

    // Porta Dourada (X2)
    fill_rect(680, 350, 220, 50, 0x00F1C40F);
    draw_text("X 2", 760, 365, 4, 0x00111111);

    // --- DESENHAR OS MOBS (Bonecos Azuis) ---
    for (int i = 0; i < MAX_MOBS; i++) {
        if (mobs[i].active) {
            // Boneco azul simples (cabeça e corpo)
            circle((int)mobs[i].x, (int)mobs[i].y - 8, 6, 0x0055D6FF); // Cabeça
            fill_rect((int)mobs[i].x - 4, (int)mobs[i].y - 2, 8, 12, 0x003498DB); // Corpo
        }
    }

    // --- CANHÃO NA BASE (Controlado pelas Setinhas) ---
    fill_rect((int)cannon_x - 35, (int)H - 140, 70, 50, 0x002980B9);
    circle((int)cannon_x, (int)H - 140, 25, 0x0055D6FF);

    // --- HUD / PLACAR ---
    draw_text("SCORE", 40, 40, 3, 0x00FFFFFF);
    sprintf(buf, "%d", score);
    draw_text(buf, 40, 70, 4, 0x00FFD23F);

    // Instruções no rodapé
    draw_text("USE AS SETINHAS (ESQ / DIR) PARA MOVER O CANHAO", 180, (int)H - 70, 3, 0x00BDC3C7);
    draw_text("APERTE X PARA DISPARAR EXERCITO", 320, (int)H - 40, 3, 0x0055D6FF);
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
