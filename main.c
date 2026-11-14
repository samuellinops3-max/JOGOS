// Slither.io Clone - PS3 Homebrew - PSL1GHT + SDL 1.2 - 1280x720
// Controles: Analogico Esq = direcao | X ou R2 = Turbo (Boost)
// Zero malloc - apenas arrays estaticos globais

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <SDL/SDL.h>
#include <sysutil/sysutil.h>
#include <io/pad.h>

#define SCREEN_W 1280
#define SCREEN_H 720
#define WORLD_W 6000
#define WORLD_H 6000
#define MAX_SNAKES 12
#define MAX_SEG 220
#define MAX_FOOD 900
#define MAX_BOTS 11
#define MAX_PARTICLES 300

static SDL_Surface *screen = NULL;
static SDL_Joystick *joy = NULL;

// ---------- Estruturas estaticas ----------
typedef struct { float x, y; int type; int pulse; int alive; } Food;
typedef struct { float x, y; } Seg;
typedef struct {
    float x, y;
    float angle;
    float targetAngle;
    float speed;
    float radius;
    int length;
    int alive;
    int isPlayer;
    int score;
    int boost;
    int hue;
    int respawnTimer;
    int aiTimer;
    float aiAngle;
    Seg segs[MAX_SEG];
    char name[16];
} Snake;

static Snake snakes[MAX_SNAKES];
static Food foods[MAX_FOOD];
static float camX = 0, camY = 0;
static int playerScore = 0;
static int playerKills = 0;
static int gameTime = 0;
static int deadTimer = 0;
static int showLeaderboard = 1;

static const char *namesList[12] = {"VOCE","BOT-1","VIPER","SNAKY","COBRA","PYTHON","NAGA","KAA","HYDRA","MAMBA","ANACONDA","BOA"};

// ---------- Utils ----------
static inline float clampf(float v, float a, float b){ if(v<a) return a; if(v>b) return b; return v; }
static inline float dist2(float x1,float y1,float x2,float y2){ float dx=x2-x1, dy=y2-y1; return sqrtf(dx*dx+dy*dy); }
static inline float angleLerp(float a, float b, float t){
    float d = b - a;
    while(d > M_PI) d -= 2*M_PI;
    while(d < -M_PI) d += 2*M_PI;
    return a + d * t;
}
static int randRange(int a, int b){ return a + (rand() % (b-a+1)); }

// ---------- Pixel drawing direto ----------
static inline void setPixel(int x, int y, Uint32 col){
    if(x<0||x>=SCREEN_W||y<0||y>=SCREEN_H) return;
    Uint32 *pixels = (Uint32*)screen->pixels;
    pixels[y*SCREEN_W + x] = col;
}
static inline Uint32 RGB(int r,int g,int b){ return (0xFFu<<24)|((Uint32)r<<16)|((Uint32)g<<8)|(Uint32)b; }
static Uint32 hueToRGB(int hue){
    float h = (hue % 360) / 60.0f;
    int i = (int)h;
    float f = h - i;
    int p = 0, q = (int)(255*(1-f)), t = (int)(255*f);
    int r=255,g=255,b=255;
    if(i==0){ r=255; g=t; b=0; }
    else if(i==1){ r=q; g=255; b=0; }
    else if(i==2){ r=0; g=255; b=t; }
    else if(i==3){ r=0; g=q; b=255; }
    else if(i==4){ r=t; g=0; b=255; }
    else { r=255; g=0; b=q; }
    return RGB(r,g,b);
}
static void fillRect(int x,int y,int w,int h,Uint32 col){
    for(int yy=y; yy<y+h; yy++) for(int xx=x; xx<x+w; xx++) setPixel(xx,yy,col);
}
static void drawFilledCircle(int cx,int cy,int r,Uint32 col){
    if(r<=0) return;
    for(int y=-r;y<=r;y++){
        int dx = (int)sqrtf((float)(r*r - y*y));
        for(int x=-dx;x<=dx;x++) setPixel(cx+x, cy+y, col);
    }
}
static void drawCircleOutline(int cx,int cy,int r,Uint32 col){
    for(int a=0;a<360;a+=2){ double rad=a*M_PI/180.0; setPixel(cx+(int)(cos(rad)*r), cy+(int)(sin(rad)*r), col); }
}
static void drawLine(int x0,int y0,int x1,int y1,Uint32 col){
    int dx=abs(x1-x0), dy=abs(y1-y0);
    int sx=x0<x1?1:-1, sy=y0<y1?1:-1, err=dx-dy;
    while(1){ setPixel(x0,y0,col); if(x0==x1&&y0==y1) break; int e2=2*err; if(e2>-dy){err-=dy;x0+=sx;} if(e2<dx){err+=dx;y0+=sy;} }
}

// ---------- Fonte 5x7 embutida ----------
static const unsigned char font5x7[96][5] = {
{0,0,0,0,0},{0,0,0x5F,0,0},{0,0x07,0,0x07,0},{0x14,0x7F,0x14,0x7F,0x14},{0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0,0x05,0x03,0,0},{0,0x1C,0x22,0x41,0},{0,0x41,0x22,0x1C,0},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},{0,0x50,0x30,0,0},{0x08,0x08,0x08,0x08,0x08},{0,0x60,0x60,0,0},{0x20,0x10,0x08,0x04,0x02},
{0x3E,0x51,0x49,0x45,0x3E},{0,0x42,0x7F,0x40,0},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},{0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},{0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0,0x36,0x36,0,0},{0,0x56,0x36,0,0},{0x08,0x14,0x22,0x41,0},{0x14,0x14,0x14,0x14,0x14},{0,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
{0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},{0,0x41,0x7F,0x41,0},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},{0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},
{0,0x7F,0x41,0x41,0},{0x02,0x04,0x08,0x10,0x20},{0,0x41,0x41,0x7F,0},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},{0,0x01,0x02,0x04,0},{0x20,0x54,0x54,0x54,0x78},{0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},{0x38,0x44,0x48,0x48,0x7F},{0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x0C,0x52,0x52,0x52,0x3E},{0x7F,0x08,0x04,0x04,0x78},{0,0x44,0x7D,0x40,0},{0x20,0x40,0x44,0x3D,0},{0x7F,0x10,0x28,0x44,0},{0,0x41,0x7F,0x40,0},{0x7C,0x04,0x18,0x04,0x78},{0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},{0x7C,0x14,0x14,0x14,0x08},{0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},{0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},{0x3C,0x40,0x30,0x40,0x3C},{0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},{0x44,0x64,0x54,0x4C,0x44},
{0,0x08,0x36,0x41,0},{0,0,0x7F,0,0},{0,0x41,0x36,0x08,0},{0x08,0x08,0x2A,0x1C,0x08},{0x08,0x1C,0x2A,0x08,0x08}
};
static void drawChar(int x,int y,char c,Uint32 col,int scale){
    if(c<32||c>127) c='?';
    const unsigned char *g = font5x7[c-32];
    for(int cx=0;cx<5;cx++) for(int row=0;row<7;row++) if(g[cx]&(1<<row)){
        for(int sx=0;sx<scale;sx++) for(int sy=0;sy<scale;sy++) setPixel(x+cx*scale+sx, y+row*scale+sy, col);
    }
}
static void drawText(int x,int y,const char *s,Uint32 col,int scale){
    int cx=x;
    while(*s){ if(*s==' '){ cx+=6*scale; s++; continue; } drawChar(cx,y,*s,col,scale); cx+=6*scale; s++; }
}
static void drawTextCenter(int cx,int y,const char *s,Uint32 col,int scale){
    int w = (int)strlen(s)*6*scale;
    drawText(cx-w/2, y, s, col, scale);
}

// ---------- Spawn ----------
static void spawnFoodAt(int idx, float x, float y){
    foods[idx].x=x; foods[idx].y=y; foods[idx].type=randRange(0,5); foods[idx].pulse=randRange(0,60); foods[idx].alive=1;
}
static void initFoods(){
    for(int i=0;i<MAX_FOOD;i++){
        spawnFoodAt(i, (float)randRange(100, WORLD_W-100), (float)randRange(100, WORLD_H-100));
    }
}
static void initSnake(int idx, int isPlayer, float sx, float sy, int hue){
    Snake *s=&snakes[idx];
    s->x=sx; s->y=sy;
    s->angle=(float)randRange(0,628)/100.0f;
    s->targetAngle=s->angle;
    s->speed=2.2f;
    s->radius=14;
    s->length=18;
    s->alive=1;
    s->isPlayer=isPlayer;
    s->score=0;
    s->boost=0;
    s->hue=hue;
    s->respawnTimer=0;
    s->aiTimer=0;
    s->aiAngle=s->angle;
    strncpy(s->name, namesList[idx], 15); s->name[15]='\0';
    for(int i=0;i<MAX_SEG;i++){ s->segs[i].x=sx - cosf(s->angle)*i*10; s->segs[i].y=sy - sinf(s->angle)*i*10; }
}
static void initGame(){
    srand((unsigned int)time(NULL));
    initFoods();
    initSnake(0, 1, WORLD_W/2, WORLD_H/2, 120);
    for(int i=1;i<MAX_SNAKES;i++){
        initSnake(i, 0, (float)randRange(400, WORLD_W-400), (float)randRange(400, WORLD_H-400), randRange(0,360));
        snakes[i].length=randRange(15,45);
        snakes[i].score=snakes[i].length*10;
    }
    camX=snakes[0].x; camY=snakes[0].y;
    playerScore=0; playerKills=0; gameTime=0; deadTimer=0;
}
static void respawnSnake(int idx){
    Snake *s=&snakes[idx];
    s->x=(float)randRange(500, WORLD_W-500);
    s->y=(float)randRange(500, WORLD_H-500);
    s->angle=(float)randRange(0,628)/100.0f;
    s->targetAngle=s->angle;
    s->length=15;
    s->score=0;
    s->alive=1;
    s->radius=14;
    s->respawnTimer=0;
    for(int i=0;i<MAX_SEG;i++){ s->segs[i].x=s->x - cosf(s->angle)*i*10; s->segs[i].y=s->y - sinf(s->angle)*i*10; }
    if(idx==0){ deadTimer=0; playerScore=0; }
}

// ---------- Morte -> vira comida ----------
static void killSnake(int idx, int killerIdx){
    Snake *s=&snakes[idx];
    if(!s->alive) return;
    s->alive=0;
    s->respawnTimer=180;
    // Transforma cada segmento em 2 comidas
    for(int i=0;i<s->length && i<MAX_SEG;i+=2){
        for(int k=0;k<MAX_FOOD;k++){
            if(!foods[k].alive){
                spawnFoodAt(k, s->segs[i].x + (float)randRange(-12,12), s->segs[i].y + (float)randRange(-12,12));
                foods[k].type=randRange(0,5);
                break;
            }
        }
    }
    if(killerIdx>=0 && killerIdx<MAX_SNAKES && snakes[killerIdx].alive){
        snakes[killerIdx].score+=100;
        if(killerIdx==0) playerKills++;
    }
    if(idx==0){ deadTimer=120; }
}

// ---------- IA dos Bots ----------
static void updateBotAI(int idx){
    Snake *s=&snakes[idx];
    if(!s->alive) return;
    s->aiTimer--;
    // Encontra comida mais proxima
    float bestD=1e9f; float bestX=s->x, bestY=s->y;
    for(int i=0;i<MAX_FOOD;i++){
        if(!foods[i].alive) continue;
        float d=dist2(s->x,s->y,foods[i].x,foods[i].y);
        if(d<bestD){ bestD=d; bestX=foods[i].x; bestY=foods[i].y; }
    }
    float target = atan2f(bestY - s->y, bestX - s->x);
    // Desvia de outras cobras e da borda
    float dangerAngle = -999;
    for(int j=0;j<MAX_SNAKES;j++){
        if(j==idx) continue; if(!snakes[j].alive) continue;
        for(int k=0;k<snakes[j].length && k<MAX_SEG;k+=3){
            float d=dist2(s->x,s->y,snakes[j].segs[k].x,snakes[j].segs[k].y);
            if(d < 140){
                float away = atan2f(s->y - snakes[j].segs[k].y, s->x - snakes[j].segs[k].x);
                dangerAngle = away;
                break;
            }
        }
        if(dangerAngle!=-999) break;
    }
    if(s->x<300) dangerAngle=0;
    if(s->x>WORLD_W-300) dangerAngle=(float)M_PI;
    if(s->y<300) dangerAngle=(float)M_PI/2;
    if(s->y>WORLD_H-300) dangerAngle=-(float)M_PI/2;

    if(dangerAngle!=-999) s->targetAngle = dangerAngle;
    else {
        if(s->aiTimer<=0){ s->aiTimer=randRange(30,90); s->aiAngle=target + (float)randRange(-30,30)/100.0f; }
        s->targetAngle = s->aiAngle;
        // Se estiver longe, vai direto na comida
        if(bestD<400) s->targetAngle = target;
    }
    // Bots usam boost as vezes para fugir ou cacar
    s->boost = (dangerAngle!=-999 && randRange(0,100)<60) ? 1 : (bestD<150 && randRange(0,100)<20 ? 1 : 0);
    if(s->boost && s->length<12) s->boost=0;
}

// ---------- Update Cobra ----------
static void updateSnake(int idx, float dt){
    Snake *s=&snakes[idx];
    if(!s->alive){
        s->respawnTimer--;
        if(s->respawnTimer<=0) respawnSnake(idx);
        return;
    }
    // Curva suave baseada no tamanho: cobras maiores viram mais devagar
    float turnSpeed = 0.055f + 0.045f * (1.0f - clampf((float)s->length / 220.0f, 0, 0.85f));
    s->angle = angleLerp(s->angle, s->targetAngle, turnSpeed);

    // Velocidade: base diminui levemente com tamanho, boost dobra
    float baseSpeed = 2.6f - clampf((float)s->length/220.0f,0,0.8f)*0.7f;
    float curSpeed = s->boost ? baseSpeed*2.0f : baseSpeed;
    s->speed = curSpeed;
    s->x += cosf(s->angle)*curSpeed*dt*30.0f*0.5f;
    s->y += sinf(s->angle)*curSpeed*dt*30.0f*0.5f;

    // Borda da arena: morre se bater na borda vermelha
    if(s->x<60||s->x>WORLD_W-60||s->y<60||s->y>WORLD_H-60){
        killSnake(idx, -1);
        return;
    }

    // Turbo solta comida pela cauda e encolhe
    if(s->boost && s->length>12){
        static int boostCounter=0;
        boostCounter++;
        if(boostCounter%18==0){
            s->length--;
            // Solta comida na cauda
            int tail = s->length;
            if(tail<MAX_SEG){
                for(int k=0;k<MAX_FOOD;k++){
                    if(!foods[k].alive){ spawnFoodAt(k, s->segs[tail].x, s->segs[tail].y); break; }
                }
            }
        }
    }

    // Atualiza segmentos: cada segmento segue o anterior
    float prevX=s->x, prevY=s->y;
    float segDist = 9.0f;
    for(int i=0;i<s->length && i<MAX_SEG;i++){
        float dx = s->segs[i].x - prevX;
        float dy = s->segs[i].y - prevY;
        float d = sqrtf(dx*dx+dy*dy);
        if(d>0.001f){
            float move = d - segDist;
            s->segs[i].x -= (dx/d)*move;
            s->segs[i].y -= (dy/d)*move;
        }
        prevX=s->segs[i].x; prevY=s->segs[i].y;
    }
    s->segs[0].x=s->x; s->segs[0].y=s->y;

    // Raio cresce com tamanho
    s->radius = 12 + clampf((float)s->length/22.0f,0,14);
    // Comer comida
    for(int i=0;i<MAX_FOOD;i++){
        if(!foods[i].alive) continue;
        float d=dist2(s->x,s->y,foods[i].x,foods[i].y);
        if(d < s->radius + 10){
            foods[i].alive=0;
            s->length++;
            if(s->length>=MAX_SEG) s->length=MAX_SEG-1;
            s->score+=10;
            if(s->isPlayer) playerScore=s->score;
            // Respawna comida em outro lugar
            spawnFoodAt(i, (float)randRange(80, WORLD_W-80), (float)randRange(80, WORLD_H-80));
        }
    }
}

// ---------- Colisao fatal ----------
static void checkCollisions(){
    for(int i=0;i<MAX_SNAKES;i++){
        if(!snakes[i].alive) continue;
        for(int j=0;j<MAX_SNAKES;j++){
            if(i==j) continue;
            if(!snakes[j].alive) continue;
            // Cabeca de i vs corpo de j (pula os 4 primeiros segmentos da propria cobra)
            for(int k=4;k<snakes[j].length && k<MAX_SEG;k++){
                float d=dist2(snakes[i].x, snakes[i].y, snakes[j].segs[k].x, snakes[j].segs[k].y);
                float killDist = snakes[j].radius + snakes[i].radius*0.55f;
                if(d < killDist){
                    killSnake(i, j);
                    break;
                }
            }
            if(!snakes[i].alive) break;
        }
    }
}

// ---------- Desenho ----------
static void drawBackground(){
    // Fundo escuro #161C22
    fillRect(0,0,SCREEN_W,SCREEN_H, RGB(0x16,0x1C,0x22));
    // Grid hexagonal
    int hexR=60;
    int hexW=(int)(hexR*1.732f);
    int hexH=hexR*3/2;
    int offX = -(int)camX % hexW;
    int offY = -(int)camY % hexH;
    Uint32 gridCol = RGB(0x1E,0x2A,0x33);
    for(int gy=-hexH; gy<SCREEN_H+hexH; gy+=hexH){
        for(int gx=-hexW; gx<SCREEN_W+hexW; gx+=hexW){
            int cx = gx + offX + ((gy/hexH)%2==0 ? 0 : hexW/2);
            int cy = gy + offY;
            // Desenha hexagono com 6 linhas
            for(int e=0;e<6;e++){
                double a1=(e*60)*M_PI/180.0;
                double a2=((e+1)*60)*M_PI/180.0;
                int x1=cx+(int)(cos(a1)*hexR), y1=cy+(int)(sin(a1)*hexR);
                int x2=cx+(int)(cos(a2)*hexR), y2=cy+(int)(sin(a2)*hexR);
                drawLine(x1,y1,x2,y2, gridCol);
            }
        }
    }
    // Borda vermelha da arena
    int bx0=(int)(0-camX+SCREEN_W/2), by0=(int)(0-camY+SCREEN_H/2);
    int bx1=(int)(WORLD_W-camX+SCREEN_W/2), by1=(int)(WORLD_H-camY+SCREEN_H/2);
    if(bx0>-20||by0>-20||bx1<SCREEN_W+20||by1<SCREEN_H+20){
        Uint32 red=RGB(255,40,40);
        for(int x=bx0;x<=bx1;x+=4){ setPixel(x,by0,red); setPixel(x,by0+1,red); setPixel(x,by1,red); setPixel(x,by1+1,red); }
        for(int y=by0;y<=by1;y+=4){ setPixel(bx0,y,red); setPixel(bx0+1,y,red); setPixel(bx1,y,red); setPixel(bx1+1,y,red); }
        drawCircleOutline(bx0,by0,10,red);
    }
}
static void drawFoods(){
    for(int i=0;i<MAX_FOOD;i++){
        if(!foods[i].alive) continue;
        int sx=(int)(foods[i].x - camX + SCREEN_W/2);
        int sy=(int)(foods[i].y - camY + SCREEN_H/2);
        if(sx<-30||sx>SCREEN_W+30||sy<-30||sy>SCREEN_H+30) continue;
        float pulse = 1.0f + 0.25f*sinf((gameTime + foods[i].pulse)*0.08f);
        int r = (int)(6*pulse)+3;
        Uint32 col = hueToRGB(foods[i].type*55 + 20);
        drawFilledCircle(sx,sy,r,col);
        drawFilledCircle(sx-2,sy-2,r/2, RGB(255,255,255));
        // Brilho pulsante
        if((gameTime%60)<30) drawCircleOutline(sx,sy,r+2, col);
    }
}
static void drawSnakes(){
    // Desenha do menor para o maior para sobrepor certo: desenha bots primeiro, player por ultimo
    for(int order=0; order<2; order++){
        for(int i=0;i<MAX_SNAKES;i++){
            if(!snakes[i].alive) continue;
            if(order==0 && snakes[i].isPlayer) continue;
            if(order==1 && !snakes[i].isPlayer) continue;
            Snake *s=&snakes[i];
            Uint32 bodyCol = hueToRGB(s->hue);
            Uint32 darkCol = hueToRGB((s->hue+40)%360);
            // Segmentos circulares sobrepostos - da cauda para cabeca
            for(int k=s->length-1;k>=0;k--){
                if(k>=MAX_SEG) continue;
                int sx=(int)(s->segs[k].x - camX + SCREEN_W/2);
                int sy=(int)(s->segs[k].y - camY + SCREEN_H/2);
                if(sx<-50||sx>SCREEN_W+50||sy<-50||sy>SCREEN_H+50) continue;
                int rr=(int)s->radius;
                // Alterna cor para efeito listrado
                Uint32 c = (k%2==0)? bodyCol : darkCol;
                drawFilledCircle(sx,sy,rr,c);
                // Brilho 3D no segmento
                drawFilledCircle(sx-rr/3, sy-rr/3, rr/3, RGB(255,255,255));
                if(k==s->length-1){
                    drawFilledCircle(sx,sy,rr-2, darkCol);
                }
            }
            // Cabeca
            int hx=(int)(s->x - camX + SCREEN_W/2);
            int hy=(int)(s->y - camY + SCREEN_H/2);
            drawFilledCircle(hx,hy,(int)s->radius+2, bodyCol);
            // Olhos na cabeca
            float perp = s->angle + (float)M_PI/2;
            int eyeOff = (int)(s->radius*0.55f);
            int fwd = (int)(s->radius*0.6f);
            int ex1 = hx + (int)(cosf(s->angle)*fwd + cosf(perp)*eyeOff);
            int ey1 = hy + (int)(sinf(s->angle)*fwd + sinf(perp)*eyeOff);
            int ex2 = hx + (int)(cosf(s->angle)*fwd - cosf(perp)*eyeOff);
            int ey2 = hy + (int)(sinf(s->angle)*fwd - sinf(perp)*eyeOff);
            drawFilledCircle(ex1,ey1,(int)s->radius/2+3, RGB(255,255,255));
            drawFilledCircle(ex2,ey2,(int)s->radius/2+3, RGB(255,255,255));
            drawFilledCircle(ex1+(int)(cosf(s->angle)*4), ey1+(int)(sinf(s->angle)*4), (int)s->radius/4+2, RGB(0,0,0));
            drawFilledCircle(ex2+(int)(cosf(s->angle)*4), ey2+(int)(sinf(s->angle)*4), (int)s->radius/4+2, RGB(0,0,0));
            drawFilledCircle(ex1+3,ey1-3,2, RGB(255,255,255));
            drawFilledCircle(ex2+3,ey2-3,2, RGB(255,255,255));
            // Nome
            drawTextCenter(hx, hy - (int)s->radius - 18, s->name, RGB(255,255,255), 2);
            // Efeito boost
            if(s->boost){
                drawCircleOutline(hx,hy,(int)s->radius+8, RGB(255,255,0));
            }
        }
    }
}
static void drawMinimap(){
    int mw=170, mh=110;
    int mx=SCREEN_W-mw-18, my=SCREEN_H-mh-18;
    fillRect(mx,my,mw,mh, RGB(0,0,0));
    fillRect(mx+2,my+2,mw-4,mh-4, RGB(0x16,0x1C,0x22));
    drawCircleOutline(mx+mw/2, my+mh/2, 2, RGB(255,255,255));
    for(int i=0;i<MAX_SNAKES;i++){
        if(!snakes[i].alive) continue;
        int px = mx + (int)(snakes[i].x / WORLD_W * mw);
        int py = my + (int)(snakes[i].y / WORLD_H * mh);
        Uint32 c = snakes[i].isPlayer ? RGB(255,255,0) : hueToRGB(snakes[i].hue);
        drawFilledCircle(px,py, snakes[i].isPlayer?4:3, c);
   