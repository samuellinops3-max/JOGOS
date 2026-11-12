// Cobra Arena - Clássico e Perfeito para PS3 (PSL1GHT)
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

typedef struct {
    int x, y;
} Point;

#define MAX_SNAKE_LEN 200
Point snake[MAX_SNAKE_LEN];
int snake_len = 5;
int dx = 1, dy = 0;
Point food;
int score = 0;
int game_over = 0;

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

static void spawn_food(void)
{
    food.x = (rand() % 30) * 30 + 100;
    food.y = (rand() % 18) * 30 + 100;
}

static void init_game(void)
{
    snake_len = 5;
    for (int i = 0; i < snake_len; i++) {
        snake[i].x = 300 - (i * 30);
        snake[i].y = 300;
    }
    dx = 1;
    dy = 0;
    score = 0;
    game_over = 0;
    spawn_food();
}

static void read_pad(void)
{
    padInfo info;
    padData d;
    ioPadGetInfo(&info);
    if (info.status[0] || info.status[1]) {
        int p = info.status[0] ? 0 : 1;
        ioPadGetData(p, &d);
        
        if ((d.BTN_LEFT || d.ANA_L_H < 50) && dx == 0) { dx = -1; dy = 0; }
        if ((d.BTN_RIGHT || d.ANA_L_H > 200) && dx == 0) { dx = 1; dy = 0; }
        if ((d.BTN_UP || d.ANA_L_V < 50) && dy == 0) { dx = 0; dy = -1; }
        if ((d.BTN_DOWN || d.ANA_L_V > 200) && dy == 0) { dx = 0; dy = 1; }
        
        if (game_over && d.BTN_CROSS) {
            init_game();
        }
    }
}

static void update_game(void)
{
    if (game_over) return;

    for (int i = snake_len - 1; i > 0; i--) {
        snake[i] = snake[i - 1];
    }
    snake[0].x += dx * 30;
    snake[0].y += dy * 30;

    if (abs(snake[0].x - food.x) < 15 && abs(snake[0].y - food.y) < 15) {
        score += 10;
        if (snake_len < MAX_SNAKE_LEN) snake_len++;
        spawn_food();
    }

    if (snake[0].x < 50 || snake[0].x >= W - 50 || snake[0].y < 50 || snake[0].y >= H - 50) {
        game_over = 1;
    }

    for (int i = 1; i < snake_len; i++) {
        if (snake[0].x == snake[i].x && snake[0].y == snake[i].y) {
            game_over = 1;
        }
    }
}

static void render(void)
{
    memset(back, 0, W * H * 4);

    fill_rect(50, 50, W - 100, H - 100, 0x00111927);
    fill_rect(45, 45, W - 90, 5, 0x00FFFFFF);
    fill_rect(45, H - 50, W - 90, 5, 0x00FFFFFF);
    fill_rect(45, 45, 5, H - 90, 0x00FFFFFF);
    fill_rect(W - 50, 45, 5, H - 90, 0x00FFFFFF);

    fill_rect(food.x, food.y, 25, 25, 0x00FF4D4D);

    for (int i = 0; i < snake_len; i++) {
        fill_rect(snake[i].x, snake[i].y, 28, 28, (i == 0) ? 0x0055D6FF : 0x0066FF66);
    }
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    init_screen();
    ioPadInit(7);
    sysUtilRegisterCallback(0, sysutil_cb, NULL);
    srand(20261109);
    init_game();

    while (running) {
        sysUtilCheckCallback();
        read_pad();
        update_game();
        render();
        present();
        usleep(120000);
    }
    return 0;
}
