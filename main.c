/*
 ============================================================================
 Nome do Projeto : Cobra Arena (Slither.io Clone para PS3)
 Alvo          : PlayStation 3 (PPU / DualShock 3 / 60 FPS)
 Otimizações   : Zero malloc/free no loop principal, Arrays Estáticos,
                 Deadzone Analógica, Grid Hexagonal Dinâmico e Renderização 2D/3D Otimizada.
 ============================================================================
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Definições de Limites e Configurações de Hardware para PS3
#define SCREEN_WIDTH        1920
#define SCREEN_HEIGHT       1080
#define TARGET_FPS          60
#define WORLD_RADIUS        4000.0f
#define MAX_SNAKE_SEGMENTS  500
#define MAX_BOTS            15
#define MAX_FOOD            1500
#define MAX_ORBES_EXPLOSION 100
#define DEADZONE            0.15f

// Cores Oficiais Slither.io (RGB)
#define COLOR_BG            0x16, 0x1C, 0x22
#define COLOR_BORDER        0xFF, 0x33, 0x33
#define COLOR_GRID          0x22, 0x2A, 0x33

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

typedef struct {
    Vector2 pos;
    Vector2 velocity;
    float radius;
    int r, g, b;
    int active;
} OrbeExplosao;

// Variáveis Globais Estáticas (Zero Alocação Dinâmica no Loop)
static Snake snakes[MAX_BOTS + 1]; // Índice 0 é o Jogador
static Food foods[MAX_FOOD];
static OrbeExplosao orbes_explosao[MAX_ORBES_EXPLOSION];

static float camera_x = 0.0f;
static float camera_y = 0.0f;
static float camera_zoom = 1.0f;

// Inicialização do Jogo
void Game_Init() {
    int i, j;
    
    // Inicializar Jogador
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

    // Inicializar Bots
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

    // Inicializar Comida no Mapa
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

    // Desativar orbes de explosão inicialmente
    for (i = 0; i < MAX_ORBES_EXPLOSION; i++) {
        orbes_explosao[i].active = 0;
    }
}

// Leitura de Entrada do DualShock 3 com Deadzone Calibrada
void DualShock3_Input(float *move_x, float *move_y, int *boosting) {
    // Simulação de leitura dos eixos do analógico esquerdo do DS3 (-1.0 a 1.0)
    // No SDK real do PS3, substitua por cellPadGetData
    float raw_lx = 0.0f; // Exemplo: Eixo X do Analógico Esquerdo
    float raw_ly = 0.0f; // Exemplo: Eixo Y do Analógico Esquerdo
    int btn_cross = 0;   // Botão X
    int btn_r2 = 0;      // Botão R2

    // Aplicação da Deadzone (Evitar Drift)
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

    // 1. Processar Entrada do Jogador
    DualShock3_Input(&ds_x, &ds_y, &boosting);
    if (ds_x != 0.0f || ds_y != 0.0f) {
        snakes[0].target_angle = atan2f(ds_y, ds_x);
    }
    snakes[0].is_boosting = boosting;

    // Atualizar todas as cobras (Jogador e Bots)
    for (i = 0; i <= MAX_BOTS; i++) {
        if (!snakes[i].alive) continue;

        // IA para os Bots
        if (snakes[i].is_ai) {
            // IA simples: mudar de direção periodicamente ou desviar do centro/borda
            if (rand() % 60 == 0) {
                snakes[i].target_angle += ((float)(rand() % 100) - 50.0f) * 0.02f;
            }
            // Manter dentro da arena
            float dist_from_center = sqrtf(snakes[i].pos.x * snakes[i].pos.x + snakes[i].pos.y * snakes[i].pos.y);
            if (dist_from_center > WORLD_RADIUS - 300.0f) {
                snakes[i].target_angle = atan2f(-snakes[i].pos.y, -snakes[i].pos.x);
            }
            snakes[i].is_boosting = (rand() % 100 < 5); // 5% de chance de boost
        }

        // Suavização da Curva (Quanto maior a cobra, mais lenta e aberta é a mudança de ângulo)
        float turn_speed = 0.08f / (1.0f + (float)snakes[i].length * 0.002f);
        float angle_diff = snakes[i].target_angle - snakes[i].angle;
        
        // Normalizar diferença de ângulo para -PI a PI
        while (angle_diff > 3.14159f) angle_diff -= 6.28318f;
        while (angle_diff < -3.14159f) angle_diff += 6.28318f;
        
        snakes[i].angle += angle_diff * turn_speed;

        // Velocidade base e Boost
        float base_speed = 4.0f;
        if (snakes[i].is_boosting && snakes[i].length > 15) {
            snakes[i].speed = base_speed * 2.0f;
            // Gastar tamanho no boost
            if (rand() % 15 == 0) {
                snakes[i].length--;
                // Soltar comida pela cauda durante o boost
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

        // Mover Cabeça
        snakes[i].pos.x += cosf(snakes[i].angle) * snakes[i].speed;
        snakes[i].pos.y += sinf(snakes[i].angle) * snakes[i].speed;

        // Atualizar Segmentos do Corpo (Movimento em cadeia fluido)
        for (j = snakes[i].length - 1; j > 0; j--) {
            snakes[i].body[j] = snakes[i].body[j - 1];
        }
        snakes[i].body[0] = snakes[i].pos;

        // Efeito Ímã perto da cabeça para puxar comida
        for (k = 0; k < MAX_FOOD; k++) {
            if (foods[k].active) {
                float dx = foods[k].pos.x - snakes[i].pos.x;
                float dy = foods[k].pos.y - snakes[i].pos.y;
                float dist_sq = dx * dx + dy * dy;
                if (dist_sq < 150.0f * 150.0f) { // Raio de atração do ímã
                    foods[k].pos.x -= dx * 0.1f;
                    foods[k].pos.y -= dy * 0.1f;
                    if (dist_sq < 20.0f * 20.0f) { // Comer comida
                        foods[k].active = 0;
                        snakes[i].length++;
                        snakes[i].score += 10;
                    }
                }
            }
        }

        // Colisão com a Borda Mortal do Mundo
        float dist_center = sqrtf(snakes[i].pos.x * snakes[i].pos.x + snakes[i].pos.y * snakes[i].pos.y);
        if (dist_center > WORLD_RADIUS) {
            if (i == 0) {
                // Jogador morre / Game Over
                snakes[0].alive = 0;
            } else {
                // Bot renasce
                snakes[i].pos.x = 0.0f;
                snakes[i].pos.y = 0.0f;
                snakes[i].length = 30;
            }
        }
    }

    // Atualizar Câmera (Zoom out automático conforme o jogador cresce)
    camera_x = snakes[0].pos.x;
    camera_y = snakes[0].pos.y;
    float target_zoom = 1.0f - ((float)snakes[0].length * 0.001f);
    if (target_zoom < 0.4f) target_zoom = 0.4f; // Limite máximo de zoom out
    camera_zoom += (target_zoom - camera_zoom) * 0.05f;
}

// Renderização Otimizada (Conceito Gráfico para PS3 / Tiny3D / OpenGL ES)
void Game_Render() {
    // 1. Limpar tela com cor de fundo #161C22
    // 2. Desenhar Grid Hexagonal móvel baseado em (camera_x, camera_y)
    // 3. Desenhar Borda Circular Vermelha Gigante (WORLD_RADIUS)
    // 4. Desenhar Orbes de Comida com efeito pulsante
    // 5. Desenhar Cobras (Segmentos circulares sobrepostos, olhos direcionais, brilho no boost)
    // 6. Desenhar HUD:
    //    - Minimapa circular no canto inferior direito
    //    - Placar Top 10 no canto superior direito
    //    - Contador de tamanho/pontuação no canto inferior esquerdo
}

int main(void) {
    // Inicialização do Sistema do PS3
    Game_Init();

    // Loop Principal a 60 FPS Cravados (Vsync ativado)
    while (1) {
        Game_Update(1.0f / (float)TARGET_FPS);
        Game_Render();
        
        // Sincronização de frame rate para 60 FPS no hardware do PS3
    }

    return 0;
}
