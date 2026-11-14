/*
 ============================================================================
 Nome do Projeto : Cobra Arena (Slither.io Clone para PS3) - SDL / PSL1GHT Corrigido
 Alvo          : PlayStation 3 (PPU / PSL1GHT / SDL 1.2 / DualShock 3 / 60 FPS)
 Otimizações   : Zero malloc/free no loop, Arrays Estáticos, Rasterização de Pixels,
                 Leitura Real de Joystick e Sistema Completo de Colisão.
 ============================================================================
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <SDL/SDL.h>

#define SCREEN_WIDTH        1280
#define SCREEN_HEIGHT       720
#define TARGET_FPS          60
#define WORLD_RADIUS        3000.0f
#define MAX_SNAKE_SEGMENTS  400
#define MAX_BOTS            12
#define MAX_FOOD            1000
#define MAX_EXPLOSION_ORBES 300
#define DEADZONE            15000 // Escala SDL_Joystick (-32768 a 32768)

// Cores RGB
#define COLOR_BG            0x161C22
#define COLOR_BORDER        0xFF3333
#define COLOR_GRID          0x222A33

typedef struct {
    float x, y;
} Vector2;

typedef struct {
    Vector2 pos;
    float angle;
    float target_angle;
    float speed;
    float radius;
    int length;
    int score;
    int is_boosting;
    int is_ai;
    int skin_color;
    int alive;
    Vector2 body[MAX_SNAKE_SEGMENTS];
} Snake;

typedef struct {
    Vector2 pos;
    float radius;
    Uint32 color;
    int active;
} Food;

// Variáveis Globais Estáticas (Zero Alocação no Loop)
static SDL_Surface *screen = NULL;
static SDL_Joystick *joystick = NULL;

static Snake snakes[MAX_BOTS + 1]; // [0] = Jogador
static Food foods[MAX_FOOD];
static Food explosion_orbes[MAX_EXPLOSION_ORBES];

static float camera_x = 0.0f;
static float camera_y = 0.0f;
static float camera_zoom = 1.0f;

// Função auxiliar para desenhar pixels com checagem de limites (Clipping)
inline void PutPixel(SDL_Surface *surface, int x, int y, Uint32 color) {
    if (x >= 0 && x < surface->w && y >= 0 && y < surface->h) {
        Uint32 *buf = (Uint32 *)surface->pixels;
        buf[y * surface->w + x] = color;
    }
}

// Rasterizador de Círculo Preenchido
void DrawFilledCircle(SDL_Surface *surface, int cx, int cy, int radius, Uint32 color) {
    int r2 = radius * radius;
    int x, y;
    for (y = -radius; y <= radius; y++) {
        for (x = -radius; x <= radius; x++) {
            if (x * x + y * y <= r2) {
                PutPixel(surface, cx + x, cy + y, color);
            }
        }
    }
}

// Inicialização do Jogo e do Hardware SDL / PS3
int Game_Init() {
    int i, j;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) {
        printf("Erro ao inicializar SDL: %s\n", SDL_GetError());
        return 0;
    }

    screen = SDL_SetVideoMode(SCREEN_WIDTH, SCREEN_HEIGHT, 32, SDL_SWSURFACE);
    if (!screen) {
        printf("Erro ao definir modo de vídeo: %s\n", SDL_GetError());
        SDL_Quit();
        return 0;
    }

    // Inicializar Joystick (DualShock 3)
    if (SDL_NumJoysticks() > 0) {
        joystick = SDL_JoystickOpen(0);
        if (joystick) {
            SDL_JoystickEventState(SDL_ENABLE);
        }
    }

    // Inicializar Jogador
    snakes[0].pos.x = 0.0f;
    snakes[0].pos.y = 0.0f;
    snakes[0].angle = 0.0f;
    snakes[0].target_angle = 0.0f;
    snakes[0].speed = 4.0f;
    snakes[0].radius = 14.0f;
    snakes[0].length = 35;
    snakes[0].score = 35;
    snakes[0].is_boosting = 0;
    snakes[0].is_ai = 0;
    snakes[0].skin_color = 0x00FF66; // Verde Neon
    snakes[0].alive = 1;

    for (j = 0; j < snakes[0].length; j++) {
        snakes[0].body[j].x = snakes[0].pos.x - (j * 10.0f);
        snakes[0].body[j].y = snakes[0].pos.y;
    }

    // Inicializar Bots
    for (i = 1; i <= MAX_BOTS; i++) {
        float angle = ((float)(rand() % 360) * 3.14159f / 180.0f);
        float dist = (float)(rand() % (int)(WORLD_RADIUS - 400));
        snakes[i].pos.x = cosf(angle) * dist;
        snakes[i].pos.y = sinf(angle) * dist;
        snakes[i].angle = angle;
        snakes[i].target_angle = angle;
        snakes[i].speed = 3.8f;
        snakes[i].radius = 14.0f;
        snakes[i].length = 25 + (rand() % 40);
        snakes[i].score = snakes[i].length;
        snakes[i].is_boosting = 0;
        snakes[i].is_ai = 1;
        snakes[i].skin_color = 0xFF3399 + (i * 12345) % 0xFFFFFF;
        snakes[i].alive = 1;

        for (j = 0; j < snakes[i].length; j++) {
            snakes[i].body[j].x = snakes[i].pos.x - (j * 10.0f * cosf(angle));
            snakes[i].body[j].y = snakes[i].pos.y - (j * 10.0f * sinf(angle));
        }
    }

    // Inicializar Comida no Mapa
    for (i = 0; i < MAX_FOOD; i++) {
        float angle = ((float)(rand() % 360) * 3.14159f / 180.0f);
        float dist = (float)(rand() % (int)(WORLD_RADIUS - 100));
        foods[i].pos.x = cosf(angle) * dist;
        foods[i].pos.y = sinf(angle) * dist;
        foods[i].radius = 4.0f;
        foods[i].color = 0xFFFF00; // Amarelo Brilhante
        foods[i].active = 1;
    }

    for (i = 0; i < MAX_EXPLOSION_ORBES; i++) {
        explosion_orbes[i].active = 0;
    }

    return 1;
}

// Transformar cobra morta em orbes de comida espalhadas
void TriggerSnakeExplosion(Snake *s) {
    int i;
    for (i = 0; i < s->length; i += 2) {
        for (int k = 0; k < MAX_EXPLOSION_ORBES; k++) {
            if (!explosion_orbes[k].active) {
                explosion_orbes[k].pos = s->body[i];
                explosion_orbes[k].radius = 7.0f;
                explosion_orbes[k].color = s->skin_color;
                explosion_orbes[k].active = 1;
                break;
            }
        }
    }
}

// Leitura de Entrada Real do DualShock 3
void ProcessInput(float *move_x, float *move_y, int *boosting) {
    SDL_JoystickUpdate();
    
    if (joystick) {
        Sint16 axis_x = SDL_JoystickGetAxis(joystick, 0); // Eixo X Analógico Esquerdo
        Sint16 axis_y = SDL_JoystickGetAxis(joystick, 1); // Eixo Y Analógico Esquerdo

        // Aplicar Deadzone
        if (axis_x > -DEADZONE && axis_x < DEADZONE) axis_x = 0;
        if (axis_y > -DEADZONE && axis_y < DEADZONE) axis_y = 0;

        *move_x = (float)axis_x / 32767.0f;
        *move_y = (float)axis_y / 32767.0f;

        // Botão X (index 1) e R2 (index 5)
        int btn_cross = SDL_JoystickGetButton(joystick, 1); 
        int btn_r2 = SDL_JoystickGetButton(joystick, 5);
        *boosting = (btn_cross || btn_r2);
    } else {
        *move_x = 0.0f;
        *move_y = 0.0f;
        *boosting = 0;
    }
}

// Atualização de Física, IA e Colisões
void Game_Update() {
    float ds_x = 0.0f, ds_y = 0.0f;
    int boosting = 0;
    int i, j, k;

    ProcessInput(&ds_x, &ds_y, &boosting);

    if (ds_x != 0.0f || ds_y != 0.0f) {
        snakes[0].target_angle = atan2f(ds_y, ds_x);
    }
    snakes[0].is_boosting = boosting;

    // Atualizar Cobras
    for (i = 0; i <= MAX_BOTS; i++) {
        if (!snakes[i].alive) {
            if (i == 0) {
                snakes[0].pos.x = 0.0f; snakes[0].pos.y = 0.0f;
                snakes[0].length = 35; snakes[0].alive = 1;
            } else {
                float angle = ((float)(rand() % 360) * 3.14159f / 180.0f);
                snakes[i].pos.x = cosf(angle) * 1000.0f;
                snakes[i].pos.y = sinf(angle) * 1000.0f;
                snakes[i].length = 30;
                snakes[i].alive = 1;
            }
            continue;
        }

        if (snakes[i].is_ai) {
            if (rand() % 50 == 0) {
                snakes[i].target_angle += ((float)(rand() % 100) - 50.0f) * 0.03f;
            }
            float d_center = sqrtf(snakes[i].pos.x * snakes[i].pos.x + snakes[i].pos.y * snakes[i].pos.y);
            if (d_center > WORLD_RADIUS - 250.0f) {
                snakes[i].target_angle = atan2f(-snakes[i].pos.y, -snakes[i].pos.x);
            }
            snakes[i].is_boosting = (rand() % 100 < 8);
        }

        // Suavização da Curva baseada no comprimento
        float turn_speed = 0.09f / (1.0f + (float)snakes[i].length * 0.0015f);
        float angle_diff = snakes[i].target_angle - snakes[i].angle;
        while (angle_diff > 3.14159f) angle_diff -= 6.28318f;
        while (angle_diff < -3.14159f) angle_diff += 6.28318f;
        snakes[i].angle += angle_diff * turn_speed;

        float base_speed = 4.0f;
        if (snakes[i].is_boosting && snakes[i].length > 15) {
            snakes[i].speed = base_speed * 1.9f;
            if (rand() % 12 == 0) {
                snakes[i].length--;
                for (k = 0; k < MAX_FOOD; k++) {
                    if (!foods[k].active) {
                        foods[k].pos = snakes[i].body[snakes[i].length - 1];
                        foods[k].radius = 5.0f;
                        foods[k].color = snakes[i].skin_color;
                        foods[k].active = 1;
                        break;
                    }
                }
            }
        } else {
            snakes[i].speed = base_speed;
        }

        // Mover Cabeça
        snakes[i].pos.x += cosf(snakes[i].angle) * snakes[i].speed;
        snakes[i].pos.y += sinf(snakes[i].angle) * snakes[i].speed;

        // Atualizar Corpo
        for (j = snakes[i].length - 1; j > 0; j--) {
            snakes[i].body[j] = snakes[i].body[j - 1];
        }
        snakes[i].body[0] = snakes[i].pos;

        // Ímã de Comida normal
        for (k = 0; k < MAX_FOOD; k++) {
            if (foods[k].active) {
                float dx = foods[k].pos.x - snakes[i].pos.x;
                float dy = foods[k].pos.y - snakes[i].pos.y;
                float dist_sq = dx * dx + dy * dy;
                if (dist_sq < 140.0f * 140.0f) {
                    foods[k].pos.x -= dx * 0.12f;
                    foods[k].pos.y -= dy * 0.12f;
                    if (dist_sq < 22.0f * 22.0f) {
                        foods[k].active = 0;
                        snakes[i].length++;
                        snakes[i].score += 10;
                    }
                }
            }
        }

        // Ímã de Orbes de Explosão
        for (k = 0; k < MAX_EXPLOSION_ORBES; k++) {
            if (explosion_orbes[k].active) {
                float dx = explosion_orbes[k].pos.x - snakes[i].pos.x;
                float dy = explosion_orbes[k].pos.y - snakes[i].pos.y;
                float dist_sq = dx * dx + dy * dy;
                if (dist_sq < 160.0f * 160.0f) {
                    explosion_orbes[k].pos.x -= dx * 0.15f;
                    explosion_orbes[k].pos.y -= dy * 0.15f;
                    if (dist_sq < 25.0f * 25.0f) {
                        explosion_orbes[k].active = 0;
                        snakes[i].length += 3;
                        snakes[i].score += 30;
                    }
                }
            }
        }

        // Colisão com a Borda Mortal
        float d_center = sqrtf(snakes[i].pos.x * snakes[i].pos.x + snakes[i].pos.y * snakes[i].pos.y);
        if (d_center > WORLD_RADIUS) {
            TriggerSnakeExplosion(&snakes[i]);
            snakes[i].alive = 0;
        }

        // Colisão Real da Cabeça contra o Corpo de Outras Cobras
        for (int o = 0; o <= MAX_BOTS; o++) {
            if (!snakes[o].alive) continue;
            int start_seg = (i == o) ? 6 : 0;
            for (j = start_seg; j < snakes[o].length; j++) {
                float cx_diff = snakes[i].pos.x - snakes[o].body[j].x;
                float cy_diff = snakes[i].pos.y - snakes[o].body[j].y;
                float hit_dist = snakes[i].radius + snakes[o].radius - 4.0f;
                if (cx_diff * cx_diff + cy_diff * cy_diff < hit_dist * hit_dist) {
                    TriggerSnakeExplosion(&snakes[i]);
                    snakes[i].alive = 0;
                    break;
                }
            }
            if (!snakes[i].alive) break;
        }
    }

    // Câmera com Zoom Dinâmico
    camera_x = snakes[0].pos.x;
    camera_y = snakes[0].pos.y;
    float target_zoom = 1.0f - ((float)snakes[0].length * 0.0008f);
    if (target_zoom < 0.45f) target_zoom = 0.45f;
    camera_zoom += (target_zoom - camera_zoom) * 0.05f;
}

// Renderização Real na Tela
void Game_Render() {
    int i, j;

    SDL_FillRect(screen, NULL, COLOR_BG);

    int center_screen_x = SCREEN_WIDTH / 2;
    int center_screen_y = SCREEN_HEIGHT / 2;

    for (i = 0; i < MAX_FOOD; i++) {
        if (foods[i].active) {
            int sx = (int)((foods[i].pos.x - camera_x) * camera_zoom) + center_screen_x;
            int sy = (int)((foods[i].pos.y - camera_y) * camera_zoom) + center_screen_y;
            if (sx >= 0 && sx < SCREEN_WIDTH && sy >= 0 && sy < SCREEN_HEIGHT) {
                DrawFilledCircle(screen, sx, sy, (int)(foods[i].radius * camera_zoom), foods[i].color);
            }
        }
    }

    for (i = 0; i < MAX_EXPLOSION_ORBES; i++) {
        if (explosion_orbes[i].active) {
            int sx = (int)((explosion_orbes[i].pos.x - camera_x) * camera_zoom) + center_screen_x;
            int sy = (int)((explosion_orbes[i].pos.y - camera_y) * camera_zoom) + center_screen_y;
            if (sx >= 0 && sx < SCREEN_WIDTH && sy >= 0 && sy < SCREEN_HEIGHT) {
                DrawFilledCircle(screen, sx, sy, (int)(explosion_orbes[i].radius * camera_zoom), explosion_orbes[i].color);
            }
        }
    }

    for (i = 0; i <= MAX_BOTS; i++) {
        if (!snakes[i].alive) continue;

        for (j = snakes[i].length - 1; j >= 0; j--) {
            int sx = (int)((snakes[i].body[j].x - camera_x) * camera_zoom) + center_screen_x;
            int sy = (int)((snakes[i].body[j].y - camera_y) * camera_zoom) + center_screen_y;
            int r = (int)(snakes[i].radius * camera_zoom);

            if (sx + r >= 0 && sx - r < SCREEN_WIDTH && sy + r >= 0 && sy - r < SCREEN_HEIGHT) {
                DrawFilledCircle(screen, sx, sy, r, snakes[i].skin_color);
            }
        }

        int hx = (int)((snakes[i].pos.x - camera_x) * camera_zoom) + center_screen_x;
        int hy = (int)((snakes[i].pos.y - camera_y) * camera_zoom) + center_screen_y;
        float eye_dist = snakes[i].radius * 0.6f * camera_zoom;
        
        int eye1_x = hx + (int)(cosf(snakes[i].angle + 0.6f) * eye_dist);
        int eye1_y = hy + (int)(sinf(snakes[i].angle + 0.6f) * eye_dist);
        int eye2_x = hx + (int)(cosf(snakes[i].angle - 0.6f) * eye_dist);
        int eye2_y = hy + (int)(sinf(snakes[i].angle - 0.6f) * eye_dist);

        DrawFilledCircle(screen, eye1_x, eye1_y, 3, 0xFFFFFF);
        DrawFilledCircle(screen, eye2_x, eye2_y, 3, 0xFFFFFF);
    }

    int map_cx = SCREEN_WIDTH - 120;
    int map_cy = SCREEN_HEIGHT - 120;
    int map_r = 90;
    DrawFilledCircle(screen, map_cx, map_cy, map_r, 0x11161B);

    int player_mx = map_cx + (int)((snakes[0].pos.x / WORLD_RADIUS) * map_r);
    int player_my = map_cy + (int)((snakes[0].pos.y / WORLD_RADIUS) * map_r);
    DrawFilledCircle(screen, player_mx, player_my, 4, 0x00FF66);

    SDL_Flip(screen);
}

int main(int argc, char *argv[]) {
    if (!Game_Init()) {
        return -1;
    }

    while (1) {
        Uint32 frame_start = SDL_GetTicks();

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                goto cleanup;
            }
        }

        Game_Update();
        Game_Render();

        Uint32 frame_time = SDL_GetTicks() - frame_start;
        if (frame_time < 16) {
            SDL_Delay(16 - frame_time);
        }
    }

cleanup:
    if (joystick) SDL_JoystickClose(joystick);
    SDL_Quit();
    return 0;
}
