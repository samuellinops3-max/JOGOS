
// Pou PS3 - Bichinho Virtual 3D (PSL1GHT)
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
    {'L', "#.." "#.." "#.." "#.." "###"}, {'M', "#.#" "###" "###" "#.#" "#.#"},
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

/* ---------- JOGO POU ---------- */
static int pou_hunger = 80;   // Fome (0 a 100)
static int pou_energy = 90;   // Energia (0 a 100)
static int pou_coins = 150;   // Moedas
static int pou_happy = 85;    // Felicidade
static int food_x = 0, food_y = 0, food_active = 0;
static int tick_counter = 0;

static void init_pou(void)
{
    pou_hunger = 80;
    pou_energy = 90;
    pou_coins = 150;
    pou_happy = 85;
    food_active = 0;
}

static void update_pou(void)
{
    tick_counter++;
    // O Pou vai perdendo fome e energia devagar com o tempo
    if (tick_counter % 300 == 0) {
        if (pou_hunger > 0) pou_hunger--;
        if (pou_energy > 0) pou_energy--;
        if (pou_happy > 0 && pou_hunger < 30) pou_happy--;
    }
}

static void render_pou(void)
{
    u32 n = W * H;
    // Fundo aconchegante de quarto do Pou (cor creme suave)
    for (u32 i = 0; i < n; i++) back[i] = 0x002A2438;

    // Chão da sala
    fill_rect(0, (int)H - 180, W, 180, 0x005D4037);
    fill_rect(0, (int)H - 190, W, 10, 0x003E2723);

    int cx = (int)W / 2;
    int cy = (int)H / 2 + 30;
    int pou_w = 160;
    int pou_h = 140;

    // --- DESENHO DO POU (Forma piriforme clássica) ---
    // Corpo principal castanho/azeitona do Pou
    circle(cx, cy + 20, 75, 0x008D6E63);
    circle(cx, cy - 10, 70, 0x008D6E63);
    fill_rect(cx - 70, cy - 10, 140, 60, 0x008D6E63);

    // Olhos grandes do Pou
    circle(cx - 30, cy - 15, 18, 0x00FFFFFF); // Olho esquerdo fundo
    circle(cx + 30, cy - 15, 18, 0x00FFFFFF); // Olho direito fundo
    
    // pupilas pretas
    circle(cx - 28 + (pou_hunger < 30 ? 2 : 0), cy - 15, 8, 0x00111111);
    circle(cx + 32, cy - 15, 8, 0x00111111);

    // Sorriso do Pou
    fill_rect(cx - 20, cy + 25, 40, 4, 0x003E2723);
    fill_rect(cx - 24, cy + 20, 4, 5, 0x003E2723);
    fill_rect(cx + 20, cy + 20, 4, 5, 0x003E2723);

    // --- COMIDA NO ECRÃ (SE DISPONÍVEL) ---
    if (food_active) {
        // Desenho de um bolo / comida 🍔
        fill_rect(food_x - 15, food_y - 8, 30, 16, 0x00FFD23F);
        fill_rect(food_x - 12, food_y - 14, 24, 6, 0x00FF7043);
        circle(food_x, food_y - 14, 5, 0x00FF4D4D); // Cereja em cima
    }

    // --- PAINEL DE ESTADOS (HUD) ---
    // Barra de Fome
    draw_text("FOME", 40, 40, 3, 0x00FFFFFF);
    fill_rect(130, 42, 200, 20, 0x00424242);
    fill_rect(130, 42, pou_hunger * 2, 20, 0x0066BB6A);

    // Barra de Energia
    draw_text("ENERGIA", 40, 80, 3, 0x00FFFFFF);
    fill_rect(130, 82, 200, 20, 0x00424242);
    fill_rect(130, 82, pou_energy * 2, 20, 0x0042A5F5);

    // Moedas e Felicidade
    char buf[32];
    sprintf(buf, "COINS: %d", pou_coins);
    draw_text(buf, (int)W - 320, 40, 3, 0x00FFD23F);

    sprintf(buf, "HAP: %d", pou_happy);
    draw_text(buf, (int)W - 320, 80, 3, 0x00FF8FAB);

    // Menu de Ajuda / Comandos no Rodapé
    draw_text("X: DAR COMIDA  |  O: DORMIR/ACORDAR  |  START: SAIR", 60, (int)H - 60, 3, 0x00B0BEC5);
}

static void read_pad(void)
{
    padInfo info;
    padData d;
    ioPadGetInfo(&info);
    if (info.status[0]) {
        ioPadGetData(0, &d);

        // Botão X: Dá comida ao Pou (aumenta fome e moedas)
        if (d.BTN_CROSS) {
            if (pou_hunger < 100) {
                pou_hunger += 2;
                if (pou_hunger > 100) pou_hunger = 100;
                pou_coins += 5;
                pou_happy += 1;
                if (pou_happy > 100) pou_happy = 100;
            }
            food_x = (int)W / 2 + (rand() % 100 - 50);
            food_y = (int)H / 2 + 100;
            food_active = 1;
        }

        // Botão Bolinha (O): Descansar / Dormir (recupera energia)
        if (d.BTN_CIRCLE) {
            if (pou_energy < 100) {
                pou_energy += 3;
                if (pou_energy > 100) pou_energy = 100;
            }
        }
    }
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    init_screen();
    ioPadInit(7);
    sysUtilRegisterCallback(0, sysutil_cb, NULL);
    srand(20261111);
    init_pou();

    while (running) {
        sysUtilCheckCallback();
        read_pad();
        update_pou();
        render_pou();
        present();
    }
    return 0;
}
