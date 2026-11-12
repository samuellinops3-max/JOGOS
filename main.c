// Penalti Combate Arcade - PS3 (PSL1GHT)
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

static gcmContextData *ctx;
static u32 W, H;
static u32 *fb[2];
static u32 fboff[2];
static u32 *back;
static int cur = 0, first_flip = 1;
static volatile int running = 1;

// Estado do Jogo de Penáltis
typedef enum { STATE_CHOOSE_DIR, STATE_KICKING, STATE_DEFENDING, STATE_RESULT } GameState;
static GameState state = STATE_CHOOSE_DIR;

static int player_score = 0;
static int bot_score = 0;
static int chosen_dir = 1; // 0: Esq, 1: Meio, 2: Dir
static int bot_dir = 1;
static int kick_timer = 0;
static int result_timer = 0;
static char result_msg[32] = "GOLO!";
static u32 team_color = 0x00FF3333; // Cor da camisa inicial (Vermelho/Azul)

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

typedef struct { char c; const char *p; } Glyph;
static const Glyph glyphs[] = {
    {'0', "###" "#.#" "#.#" "#.#" "###"}, {'1', ".#." "##." ".#." ".#." "###"},
    {'2', "###" "..#" "###" "#.." "###"}, {'3', "###" "..#" "###" "..#" "###"},
    {'4', "#.#" "#.#" "###" "..#" "..#"}, {'5', "###" "#.." "###" "..#" "###"},
    {'6', "###" "#.." "###" "#.#" "###"}, {'7', "###" "..#" "..#" "..#" "..#"},
    {'8', "###" "#.#" "###" "#.#" "###"}, {'9', "###" "#.#" "###" "..#" "###"},
    {'A', ".#." "#.#" "###" "#.#" "#.#"}, {'C', "###" "#.." "#.." "#.." "###"},
    {'D', "##." "#.#" "#.#" "#.#" "##."}, {'E', "###" "#.." "###" "#.." "###"},
    {'G', "###" "#.." "#.#" "#.#" "###"}, {'I', "###" ".#." ".#." ".#." "###"},
    {'K', "#.#" "#.#" "##." "#.#" "#.#"}, {'L', "#.." "#.." "#.." "#.." "###"},
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

static void read_pad(void)
{
    padInfo info;
    padData d;
    ioPadGetInfo(&info);
    if (info.status[0]) {
        ioPadGetData(0, &d);

        if (state == STATE_CHOOSE_DIR) {
            if (d.BTN_LEFT)  { chosen_dir = 0; state = STATE_KICKING; kick_timer = 0; }
            if (d.BTN_DOWN)  { chosen_dir = 1; state = STATE_KICKING; kick_timer = 0; }
            if (d.BTN_RIGHT) { chosen_dir = 2; state = STATE_KICKING; kick_timer = 0; }
            
            // Mudar cor da camisa com o botão L1 ou R1
            if (d.BTN_L1) team_color = 0x000066FF; // Azul
            if (d.BTN_R1) team_color = 0x00FF3333; // Vermelho
        } 
        else if (state == STATE_DEFENDING) {
            // A defender o penálti do bot
            if (d.BTN_LEFT)  { chosen_dir = 0; state = STATE_RESULT; result_timer = 0; }
            if (d.BTN_DOWN)  { chosen_dir = 1; state = STATE_RESULT; result_timer = 0; }
            if (d.BTN_RIGHT) { chosen_dir = 2; state = STATE_RESULT; result_timer = 0; }
        }
    }
}

static void update_game(void)
{
    if (state == STATE_KICKING) {
        kick_timer++;
        if (kick_timer > 40) {
            // Bot escolhe um canto aleatório para defender (0: esq, 1: centro, 2: dir)
            bot_dir = rand() % 3;
            if (chosen_dir == bot_dir) {
                strcpy(result_msg, "DEFENDEU O GUARDA-REDES!");
            } else {
                strcpy(result_msg, "GOLASSO!");
                player_score++;
            }
            state = STATE_RESULT;
            result_timer = 0;
        }
    } 
    else if (state == STATE_RESULT) {
        result_timer++;
        if (result_timer > 90) {
            // Passa para a vez do bot chutar (defender)
            if (player_score >= 5 || bot_score >= 5) {
                // Reset de jogo se alguém ganhar
                player_score = 0;
                bot_score = 0;
            }
            state = STATE_DEFENDING;
        }
    }
    else if (state == STATE_DEFENDING) {
        // Simulação se o bot marcar ou não baseado na escolha do jogador
        bot_dir = rand() % 3;
        // Se o jogador acertar o lado na defesa, defende!
        // (Tratado no read_pad, mas atualizamos o resultado a seguir)
    }
}

// Função específica para processar a defesa quando o user escolhe o canto
static void evaluate_defense(void)
{
    // Esta parte corre logo após o user escolher o canto para defender
    if (chosen_dir == bot_dir) {
        strcpy(result_msg, "DEFENDESTE O PENALTI!");
    } else {
        strcpy(result_msg, "O BOT MARCOU GOLO!");
        bot_score++;
    }
}

static void render(void)
{
    float sc = (float)W / 1280.0f;
    u32 n = W * H;

    // Relvado do Estádio (Verde)
    for (u32 i = 0; i < n; i++) back[i] = 0x00228B22;

    // Área de penálti e baliza ao fundo
    int gw = 600, gh = 220;
    int gx = (W - gw) / 2, gy = 80;
    
    // Fundo da baliza (rede)
    fill_rect(gx, gy, gw, gh, 0x00113311);
    // Traves da Baliza (Branco)
    fill_rect(gx - 10, gy, 10, gh, 0x00FFFFFF); // Trave esquerda
    fill_rect(gx + gw, gy, 10, gh, 0x00FFFFFF); // Trave direita
    fill_rect(gx - 10, gy - 10, gw + 20, 10, 0x00FFFFFF); // Travessão

    // Guarda-redes (Guarda-redes amarelo no centro ou a saltar)
    int gk_x = gx + gw / 2 - 20;
    int gk_y = gy + 80;
    if (state == STATE_KICKING) {
        if (bot_dir == 0) gk_x = gx + 50;
        if (bot_dir == 2) gk_x = gx + gw - 90;
    }
    fill_rect(gk_x, gk_y, 40, 80, 0x00FFD700); // Bloco do GR

    // Bola de Futebol
    int ball_x = W / 2 - 10;
    int ball_y = H - 150;
    if (state == STATE_KICKING) {
        int target_x = gx + 100;
        if (chosen_dir == 1) target_x = gx + gw / 2;
        if (chosen_dir == 2) target_x = gx + gw - 120;
        ball_x = ball_x + (int)((target_x - ball_x) * ((float)kick_timer / 40.0f));
        ball_y = ball_y - (int)(250.0f * ((float)kick_timer / 40.0f));
    }
    fill_rect(ball_x, ball_y, 20, 20, 0x00FFFFFF); // Bola branca

    // Placar / HUD
    char buf[64];
    sprintf(buf, "JOGADOR %d  X  %d BOT", player_score, bot_score);
    draw_text(buf, W / 2 - 180, 20, 3, 0x00FFFFFF);

    // Instruções no rodapé
    if (state == STATE_CHOOSE_DIR) {
        draw_text("ESCOLHE O CANTO PARA CHUTAR:", W / 2 - 240, H - 90, 2, 0x00FFFF00);
        draw_text("< ESQUERDA    |    BAIXO: MEIO    |    DIREITA >", W / 2 - 320, H - 55, 2, 0x00FFFFFF);
        draw_text("L1 / R1: MUDAR COR DA CAMISA", W / 2 - 220, H - 25, 2, 0x0000E5C0);
    } else if (state == STATE_KICKING) {
        draw_text("A BATER O PENALTI...", W / 2 - 140, H - 70, 3, 0x00FFD23F);
    } else if (state == STATE_RESULT) {
        draw_text(result_msg, W / 2 - (strlen(result_msg) * 12), H - 70, 3, 0x00FF4D4D);
    } else if (state == STATE_DEFENDING) {
        draw_text("SUA VEZ DE DEFENDER! ESCOLHE O CANTO:", W / 2 - 300, H - 90, 2, 0x00FFFF00);
        draw_text("< ESQUERDA    |    BAIXO: CENTRO    |    DIREITA >", W / 2 - 330, H - 55, 2, 0x00FFFFFF);
    }
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    init_screen();
    ioPadInit(7);
    sysUtilRegisterCallback(0, sysutil_cb, NULL);
    srand(20261111);

    while (running) {
        sysUtilCheckCallback();
        read_pad();
        update_game();
        render();
        present();
    }
    return 0;
}
