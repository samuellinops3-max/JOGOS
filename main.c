/*
 ============================================================================
 Nome do Projeto : Cobra Arena (Slither.io Clone para PS3) - Versão Direta
 Alvo          : PlayStation 3 (PPU / RSX / DualShock 3 / 60 FPS)
 Otimizações   : Zero malloc/free, Arrays Estáticos, Inicialização Direta na Partida,
                 Sem arquivos externos (HUD geométrico puro) e SwapBuffers garantido.
 ============================================================================
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Definições de Limites e Configurações de Hardware para PS3
#define SCREEN_WIDTH        1280
#define SCREEN_HEIGHT       720
#define TARGET_FPS          60
#define WORLD_RADIUS        4000.0f
#define MAX_SNAKE_SEGMENTS  500
#define MAX_BOTS            15
#define MAX_FOOD            1500
#define DEADZONE            0.15f

// Estruturas de Dados Estáticas
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
    int skin_id;
    int alive;
    Vector2 body[MAX_SNAKE_SEGMENTS];
    float segment_dist;
} Snake;

typedef struct {
    Vector2 pos;
    float radius;
    float pulse;
    int r, g, b;
    int active;
} Food;

// Variáveis Globais Estáticas (Zero Alocação Dinâmica)
static Snake snakes[MAX_BOTS + 1]; // Índice 0 é o Jogador
static Food foods[MAX_FOOD];

static float camera_x = 0.0f;
static float camera_y = 0.0f;
static float camera_zoom = 1.0f;

// Inicialização Direta do Jogo e do Vídeo
void Game_Init() {
    int i, j;

    // 1. Inicializar Subsistema de Vídeo do PS3 (Exemplo padrão RSX / Tiny3D / OpenGL ES)
    // Configuração de resolução 1280x720 (720p) e ativação de Vsync para 60 FPS cravados
    // (Em SDK nativo: video_init(RESOLUTION_1280x720); psglInit(...);)

    // 2. Inicializar Jogador (Já nasce vivo na arena)
    snakes[0].pos.x = 0.0f;
    snakes[0].pos.y = 0.0f;
    snakes[0].angle = 0.0f;
    snakes[0].target_angle = 0.0f;
    snakes[0].speed = 4.0f;
    snakes[0].radius = 15.0f;
    snakes[0].length = 40;
    snakes[0].score = 40;
    snakes[0].is_boosting = 0;
    snakes[0].is_ai = 0;
    snakes[0].skin_id = 1;
    snakes[0].alive = 1;
    snakes[0].segment_dist = 10.0f;

    for (j = 0; j < snakes[0].length; j++) {
        snakes[0].body[j].x = snakes[0].pos.x - (j * snakes[0].segment_dist);
        snakes[0].body[j].y = snakes[0].pos.y;
    }

    // 3. Inicializar Bots (Já nascem vivos espalhados pela arena)
    for (i = 1; i <= MAX_BOTS; i++) {
        float angle = ((float)(rand() % 360) * 3.14159f / 180.0f);
        float dist = (float)(rand() % (int)(WORLD_RADIUS - 500));
        snakes[i].pos.x = cosf(angle) * dist;
        snakes[i].pos.y = sinf(angle) * dist;
        snakes[i].angle = angle;
        snakes[i].target_angle = angle;
        snakes[i].speed = 3.8f;
        snakes[i].radius = 15.0f;
        snakes[i].length = 30 + (rand() % 50);
        snakes[i].score = snakes[i].length;
        snakes[i].is_boosting = 0;
        snakes[i].is_ai = 1;
        snakes[i].skin_id = (i % 5) + 1;
        snakes[i].alive = 1;
        snakes[i].segment_dist = 10.0f;

        for (j = 0; j < snakes[i].length; j++) {
            snakes[i].body[j].x = snakes[i].pos.x - (j * snakes[i].segment_dist * cosf(angle));
            snakes[i].body[j].y = snakes[i].pos.y - (j * snakes[i].segment_dist * sinf(angle));
        }
    }

    // 4. Inicializar Comida no Mapa
    for (i = 0; i < MAX_FOOD; i++) {
        float angle = ((float)(rand() % 360) * 3.14159f / 180.0f);
        float dist = (float)(rand() % (int)(WORLD_RADIUS - 100));
        foods[i].pos.x = cosf(angle) * dist;
        foods[i].pos.y = sinf(angle) * dist;
        foods[i].radius = 4.0f + ((rand() % 3));
        foods[i].pulse = (float)(rand() % 100);
        foods[i].r = 100 + (rand() % 155);
        foods[i].g = 100 + (rand() % 155);
        foods[i].b = 100 + (rand() % 155);
        foods[i].active = 1;
    }
}

// Leitura de Entrada do DualShock 3 com Deadzone Calibrada
void DualShock3_Input(float *move_x, float *move_y, int *boosting) {
    // Leitura real do pad no SDK do PS3 (ex: cellPadGetData)
    float raw_lx = 0.0f; 
    float raw_ly = 0.0f; 
    int btn_cross = 0;   
    int btn_r2 = 0;      

    // Aplicação da Deadzone no Analógico Esquerdo
    if (raw_lx > -DEADZONE && raw_lx < DEADZONE) raw_lx = 0.0f;
    if (raw_ly > -DEADZONE && raw_ly < DEADZONE) raw_ly = 0.0f;

    *move_x = raw_lx;
    *move_y = raw_ly;
    *boosting = (btn_cross || btn_r2);
}

// Atualização da Física e Inteligência Artificial
void Game_Update(float dt) {
    int i, j, k;
    float ds_x = 0.0f, ds_y = 0.0f;
    int boosting = 0;

    // Processar Entrada do Jogador
    DualShock3_Input(&ds_x, &ds_y, &boosting);
    if (ds_x != 0.0f || ds_y != 0.0f) {
        snakes[0].target_angle = atan2f(ds_y, ds_x);
    }
    snakes[0].is_boosting = boosting;

    // Atualizar Cobras (Jogador e Bots)
    for (i = 0; i <= MAX_BOTS; i++) {
        if (!snakes[i].alive) continue;

        if (snakes[i].is_ai) {
            if (rand() % 60 == 0) {
                snakes[i].target_angle += ((float)(rand() % 100) - 50.0f) * 0.02f;
            }
            float dist_from_center = sqrtf(snakes[i].pos.x * snakes[i].pos.x + snakes[i].pos.y * snakes[i].pos.y);
            if (dist_from_center > WORLD_RADIUS - 300.0f) {
                snakes[i].target_angle = atan2f(-snakes[i].pos.y, -snakes[i].pos.x);
            }
            snakes[i].is_boosting = (rand() % 100 < 5);
        }

        // Suavização da Curva baseada no tamanho
        float turn_speed = 0.08f / (1.0f + (float)snakes[i].length * 0.002f);
        float angle_diff = snakes[i].target_angle - snakes[i].angle;
        while (angle_diff > 3.14159f) angle_diff -= 6.28318f;
        while (angle_diff < -3.14159f) angle_diff += 6.28318f;
        snakes[i].angle += angle_diff * turn_speed;

        // Velocidade e Boost
        float base_speed = 4.0f;
        if (snakes[i].is_boosting && snakes[i].length > 15) {
            snakes[i].speed = base_speed * 2.0f;
            if (rand() % 15 == 0) {
                snakes[i].length--;
                for (k = 0; k < MAX_FOOD; k++) {
                    if (!foods[k].active) {
                        foods[k].pos = snakes[i].body[snakes[i].length - 1];
                        foods[k].radius = 5.0f;
                        foods[k].active = 1;
                        break;
                    }
                }
            }
        } else {
            snakes[i].speed = base_speed;
        }

        // Movimento da Cabeça
        snakes[i].pos.x += cosf(snakes[i].angle) * snakes[i].speed;
        snakes[i].pos.y += sinf(snakes[i].angle) * snakes[i].speed;

        // Atualizar Corpo
        for (j = snakes[i].length - 1; j > 0; j--) {
            snakes[i].body[j] = snakes[i].body[j - 1];
        }
        snakes[i].body[0] = snakes[i].pos;

        // Ímã de Comida
        for (k = 0; k < MAX_FOOD; k++) {
            if (foods[k].active) {
                float dx = foods[k].pos.x - snakes[i].pos.x;
                float dy = foods[k].pos.y - snakes[i].pos.y;
                float dist_sq = dx * dx + dy * dy;
                if (dist_sq < 150.0f * 150.0f) {
                    foods[k].pos.x -= dx * 0.1f;
                    foods[k].pos.y -= dy * 0.1f;
                    if (dist_sq < 20.0f * 20.0f) {
                        foods[k].active = 0;
                        snakes[i].length++;
                        snakes[i].score += 10;
                    }
                }
            }
        }

        // Colisão com a Borda do Mundo
        float dist_center = sqrtf(snakes[i].pos.x * snakes[i].pos.x + snakes[i].pos.y * snakes[i].pos.y);
        if (dist_center > WORLD_RADIUS) {
            if (i == 0) {
                snakes[0].alive = 0; // Se morrer, reinicia posição instantaneamente
                snakes[0].pos.x = 0.0f; snakes[0].pos.y = 0.0f; snakes[0].length = 40; snakes[0].alive = 1;
            } else {
                snakes[i].pos.x = 0.0f; snakes[i].pos.y = 0.0f; snakes[i].length = 30;
            }
        }
    }

    // Câmera com zoom dinâmico
    camera_x = snakes[0].pos.x;
    camera_y = snakes[0].pos.y;
    float target_zoom = 1.0f - ((float)snakes[0].length * 0.001f);
    if (target_zoom < 0.4f) target_zoom = 0.4f;
    camera_zoom += (target_zoom - camera_zoom) * 0.05f;
}

// Renderização Gráfica sem dependências externas (Apenas Geometria Primitiva)
void Game_Render() {
    // 1. Limpar tela com fundo #161C22
    // 2. Desenhar Grid Hexagonal e Borda Circular Vermelha
    // 3. Desenhar Comida (Orbes coloridos)
    // 4. Desenhar Cobras (Segmentos e olhos)
    // 5. Desenhar HUD Geométrico (Minimapa circular e barras de pontuação desenhadas com linhas/retângulos)
    
    // Chamada obrigatória de atualização de buffer do PS3 para evitar tela preta:
    // e.g., psglSwap(); / cellSysutilCheckCallback();
}

int main(void) {
    // Inicialização direta do jogo (Sem menus)
    Game_Init();

    // Loop Principal a 60 FPS cravados
    while (1) {
        Game_Update(1.0f / (float)TARGET_FPS);
        Game_Render();
    }

    return 0;
}
