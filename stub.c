// stub.c - o SDL do PS3 nao tem teclado, entao a gente finge que tem
#include <SDL/SDL.h>

Uint8 *SDL_GetKeyState(int *numkeys)
{
    static Uint8 keys[512] = {0};
    if (numkeys) *numkeys = 512;
    return keys;
}