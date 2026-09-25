#ifndef SDL2_TEST_STUB_H
#define SDL2_TEST_STUB_H
#include <cstdint>
extern "C" {

typedef uint8_t  Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;
typedef int32_t  Sint32;

#define SDL_INIT_VIDEO 0x00000020u
#define SDL_INIT_AUDIO 0x00000010u
int  SDL_Init(Uint32 flags);
int  SDL_InitSubSystem(Uint32 flags);
Uint32 SDL_WasInit(Uint32 flags);
void SDL_Quit(void);
const char* SDL_GetError(void);
void SDL_Delay(Uint32 ms);
Uint32 SDL_GetTicks(void);

typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;

#define SDL_WINDOWPOS_CENTERED 0x2FFF0000u
SDL_Window* SDL_CreateWindow(const char*, int, int, int, int, Uint32);
#define SDL_RENDERER_ACCELERATED 0x00000002u
#define SDL_RENDERER_PRESENTVSYNC 0x00000004u
SDL_Renderer* SDL_CreateRenderer(SDL_Window*, int, Uint32);

#define SDL_PIXELFORMAT_RGB565 353701890u
#define SDL_PIXELFORMAT_RGB24  386930691u
#define SDL_TEXTUREACCESS_STREAMING 1
SDL_Texture* SDL_CreateTexture(SDL_Renderer*, Uint32, int, int, int);
int SDL_UpdateTexture(SDL_Texture*, const void*, const void*, int);
int SDL_RenderClear(SDL_Renderer*);
int SDL_RenderCopy(SDL_Renderer*, SDL_Texture*, const void*, const void*);
void SDL_RenderPresent(SDL_Renderer*);

// SDL_Event : taille fixee par l'ABI SDL2 (union paddee a 56 octets).
// On ne lit que .type et .key.keysym.sym -- layout de SDL_KeyboardEvent
// repris exactement (stable depuis SDL 2.0.0).
typedef Sint32 SDL_Keycode;
typedef struct { Uint8 scancode_pad[4]; SDL_Keycode sym; Uint16 mod; Uint32 unused; } SDL_Keysym_min;
typedef struct {
    Uint32 type; Uint32 timestamp; Uint32 windowID;
    Uint8 state; Uint8 repeat; Uint8 padding2; Uint8 padding3;
    SDL_Keysym_min keysym;
} SDL_KeyboardEvent_min;
typedef union { Uint32 type; SDL_KeyboardEvent_min key; Uint8 padding[56]; } SDL_Event;
#define SDL_QUIT 0x100u
#define SDL_KEYDOWN 0x300u
#define SDL_KEYUP 0x301u
int SDL_PollEvent(SDL_Event*);

#define SDLK_UP 1073741906
#define SDLK_DOWN 1073741905
#define SDLK_LEFT 1073741904
#define SDLK_RIGHT 1073741903
#define SDLK_z 122
#define SDLK_s 115
#define SDLK_q 113
#define SDLK_d 100
#define SDLK_SPACE 32
#define SDLK_x 120
#define SDLK_RETURN 13
#define SDLK_c 99
#define SDLK_BACKSPACE 8
#define SDLK_RETURN2 0x40000059
#define SDLK_ESCAPE 27
#define SDLK_PAGEUP 1073741899
#define SDLK_PAGEDOWN 1073741902
#define SDLK_COMMA 44
#define SDLK_PERIOD 46
#define SDLK_a 97
#define SDLK_TAB 9
#define SDLK_m 109
#define SDLK_n 110
#define SDLK_v 118

typedef struct { Uint32 format; int w,h; void* pixels; int pitch; } SDL_Surface;
SDL_Surface* SDL_LoadBMP_RW(void*, int);
void* SDL_RWFromFile(const char*, const char*);
#define SDL_LoadBMP(f) SDL_LoadBMP_RW(SDL_RWFromFile(f,"rb"),1)
SDL_Surface* SDL_ConvertSurfaceFormat(SDL_Surface*, Uint32, Uint32);
void SDL_FreeSurface(SDL_Surface*);

typedef void (*SDL_AudioCallback)(void*, Uint8*, int);
typedef struct {
    int freq; Uint16 format; Uint8 channels; Uint8 silence;
    Uint16 samples; Uint16 padding; Uint32 size;
    SDL_AudioCallback callback; void* userdata;
} SDL_AudioSpec;
typedef Uint32 SDL_AudioDeviceID;
SDL_AudioSpec* SDL_LoadWAV_RW(void*, int, SDL_AudioSpec*, Uint8**, Uint32*);
#define SDL_LoadWAV(f,s,b,l) SDL_LoadWAV_RW(SDL_RWFromFile(f,"rb"),1,s,b,l)
SDL_AudioDeviceID SDL_OpenAudioDevice(const char*, int, const SDL_AudioSpec*, SDL_AudioSpec*, int);
int SDL_QueueAudio(SDL_AudioDeviceID, const void*, Uint32);
void SDL_PauseAudioDevice(SDL_AudioDeviceID, int);
void SDL_FreeWAV(Uint8*);
void SDL_CloseAudioDevice(SDL_AudioDeviceID);
void SDL_ClearQueuedAudio(SDL_AudioDeviceID);
Uint32 SDL_GetQueuedAudioSize(SDL_AudioDeviceID);

}  // extern "C"
#endif
