/*
 * host_sdl.c - SDL2 live display host (built only with -DSMS_HAVE_SDL).
 *
 * A streaming ARGB8888 texture is updated from the runner's framebuffer each
 * frame and presented. The loop is paced by a precise wall-clock frame limiter
 * (host_set_frame_cap) locked to the emulated frame rate, NOT by vsync — vsync
 * paces to the monitor's refresh, which runs the game too fast on >60 Hz
 * displays. We take the Z80/VDP framebuffer as-is (full 256x192) and crop to the
 * platform viewport (SMS = full, GG = 160x144 centred) via the RenderCopy rect.
 *
 * SDL_MAIN_HANDLED keeps our own main() (no SDL2main / WinMain shim needed).
 */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "host_sdl.h"
#include "glue.h"       /* SMS_PAD_* bit constants (the controller contract) */
#include "hud.h"        /* settings overlay: the host forwards keys to it */
#ifdef SMS_GPU
#include "host_gl.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SDL_Window   *g_win;
static SDL_Renderer *g_ren;
static SDL_Texture  *g_tex;
static SDL_Rect      g_src;     /* crop region of the framebuffer */
static int g_internal_scale;
static bool g_target_ok;        /* render targets supported (hi-res layers: sharp enlarging) */
static bool g_hidden,g_gpu;
bool host_uses_gpu(void){return g_gpu;}
static double g_work_ms[8192];
static unsigned g_work_count;
static uint64_t g_work_start;
static int compare_ms(const void *a,const void *b){double x=*(const double*)a,y=*(const double*)b;return(x>y)-(x<y);}
void host_set_internal_scale(int s){g_internal_scale=s;}
void host_set_hidden(bool h){g_hidden=h;}
void host_report(FILE *f){
    qsort(g_work_ms,g_work_count,sizeof g_work_ms[0],compare_ms);
    fprintf(f,"{\"work_samples\":%u,\"frame_work_p95_ms\":%.6f,\"frame_work_max_ms\":%.6f,\"renderer\":",
        g_work_count,g_work_count?g_work_ms[(g_work_count-1)*95/100]:0,g_work_count?g_work_ms[g_work_count-1]:0);
#ifdef SMS_GPU
    if(g_gpu)host_gl_report(f);else
#endif
    fputs("{\"gpu\":false,\"effective_scale\":1}",f);
    fputs("}\n",f);
}
bool host_dump_gpu(const char *p){
#ifdef SMS_GPU
    return g_gpu&&host_gl_dump(p);
#else
    (void)p;return false;
#endif
}

static void pad_open(int idx);   /* game controllers, see below */

bool host_init(int fb_w, int fb_h,
               int crop_x, int crop_y, int crop_w, int crop_h,
               int scale, const char *title){
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0){
        fprintf(stderr, "[host] SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == 0){     /* pads present at start-up; hot-plug via events */
        for (int i = 0; i < SDL_NumJoysticks(); i++) pad_open(i);
    }
    if (scale < 1) scale = 1;
    g_work_count=0;g_work_start=0;
    g_src.x = crop_x; g_src.y = crop_y; g_src.w = crop_w; g_src.h = crop_h;

    Uint32 flags=g_hidden?SDL_WINDOW_HIDDEN:SDL_WINDOW_SHOWN;
#if defined(SMS_GPU) && !defined(SMS_VDP_REFERENCE)
    if(g_internal_scale>0){
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
    g_win=SDL_CreateWindow(title?title:"SMS/GG",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,
        crop_w*scale,crop_h*scale,flags|SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE);
    if(g_win){
        g_gpu=host_gl_init(g_win,crop_x,crop_y,crop_w,crop_h,g_internal_scale);
        if(g_gpu)return true;
        SDL_DestroyWindow(g_win);g_win=NULL;
    }
    fprintf(stderr,"[host] GPU unavailable; portable native presentation: %s\n",SDL_GetError());
    }
#endif
    g_win = SDL_CreateWindow(title ? title : "SMS/GG recompiled",
                             SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             crop_w * scale, crop_h * scale, flags|SDL_WINDOW_RESIZABLE);
    if (!g_win){ fprintf(stderr, "[host] CreateWindow: %s\n", SDL_GetError()); return false; }

    /* No PRESENTVSYNC: the wall-clock limiter (host_set_frame_cap) owns pacing,
     * so the game speed is independent of the monitor's refresh rate. */
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED);
    if (!g_ren)  /* fall back to software if no accelerated renderer */
        g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_SOFTWARE);
    if (!g_ren){ fprintf(stderr, "[host] CreateRenderer: %s\n", SDL_GetError()); return false; }

    /* integer-scale crisply; letterbox to preserve aspect */
    SDL_RenderSetLogicalSize(g_ren, crop_w, crop_h);

    g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STREAMING, fb_w, fb_h);
    if (!g_tex){ fprintf(stderr, "[host] CreateTexture: %s\n", SDL_GetError()); return false; }
    g_target_ok = SDL_RenderTargetSupported(g_ren);
    hud_set_hires(true);
    hud_set_start_free(!glue_is_gg());                 /* SMS: no Pause button emulated -> Start is free */
    return true;
}

/* Keyboard -> SMS controller. Arrows = D-pad, Z/X = buttons 1/2 (Sonic: jump),
 * Enter = Start (Game Gear). Esc quits. */
static uint8_t g_pad;     /* live P1 mask (SMS_PAD_*) */

static uint8_t key_to_pad(SDL_Keycode k){
    switch (k){
        case SDLK_UP:     return SMS_PAD_UP;
        case SDLK_DOWN:   return SMS_PAD_DOWN;
        case SDLK_LEFT:   return SMS_PAD_LEFT;
        case SDLK_RIGHT:  return SMS_PAD_RIGHT;
        case SDLK_z:      return SMS_PAD_B1;
        case SDLK_x:      return SMS_PAD_B2;
        case SDLK_RETURN: return SMS_PAD_START;
        default:          return 0;
    }
}

/* Game controllers (SDL GameController API: Xbox/PlayStation/Switch-style pads, any SDL mapping).
 * D-pad or left stick = D-pad; A/X (south/west) = button 1, B/Y (east/north) = button 2;
 * Start = Start; right trigger held = fast-forward at hud_turbo_speed() (like TAB), left trigger
 * held = slow motion x0.5. Shoulder buttons: HUD tabs while the HUD is open, else the game's extra
 * buttons (hud_extra_buttons: LB = HUD_EXTRA_1, RB = HUD_EXTRA_2; keyboard C / V). All pads drive P1. */
static uint8_t g_pad_btn, g_pad_axis;   /* masks from controller buttons / left stick */
static uint8_t g_extra_pad, g_extra_key;
static uint8_t pad_extra(Uint8 b){
    return b == SDL_CONTROLLER_BUTTON_LEFTSHOULDER ? HUD_EXTRA_1 : b == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER ? HUD_EXTRA_2 : 0;
}
static uint8_t key_extra(SDL_Keycode k){ return k == SDLK_c ? HUD_EXTRA_1 : k == SDLK_v ? HUD_EXTRA_2 : 0; }
static bool    g_pad_turbo, g_pad_slow;
static void pad_open(int idx){
    if (SDL_IsGameController(idx)){
        SDL_GameController *gc = SDL_GameControllerOpen(idx);
        if (gc) fprintf(stderr, "[host] gamepad: %s\n", SDL_GameControllerName(gc));
    }
}
static uint8_t pad_button(Uint8 b){
    switch (b){
        case SDL_CONTROLLER_BUTTON_DPAD_UP:    return SMS_PAD_UP;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  return SMS_PAD_DOWN;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  return SMS_PAD_LEFT;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return SMS_PAD_RIGHT;
        case SDL_CONTROLLER_BUTTON_A: case SDL_CONTROLLER_BUTTON_X: return SMS_PAD_B1;
        case SDL_CONTROLLER_BUTTON_B: case SDL_CONTROLLER_BUTTON_Y: return SMS_PAD_B2;
        case SDL_CONTROLLER_BUTTON_START:      return SMS_PAD_START;
        default: return 0;
    }
}

uint8_t host_get_pad1(void){ return (uint8_t)(g_pad | g_pad_btn | g_pad_axis); }

/* HUD navigation with hold-to-repeat (own timer for keyboard AND pad; SDL key repeat is ignored).
 * A direction is "held" while any source holds it (keyboard / D-pad / left stick), so two sources
 * (e.g. a pad-to-keyboard mapper app) never fire a second step. Repeat: first after 400 ms,
 * then accelerating to every presented frame (~60 steps/s) - e.g. EXP x1 -> x100 in ~3 s. */
enum { NAV_KEY = 1, NAV_DPAD = 2, NAV_STICK = 4 };
static uint8_t  g_nav_src[4];          /* index = HUD_KEY_UP/DOWN/LEFT/RIGHT */
static int      g_nav_dir = -1;        /* the direction that auto-repeats */
static int      g_nav_reps;
static uint32_t g_nav_next;
static void nav_clear(void){ memset(g_nav_src, 0, sizeof g_nav_src); g_nav_dir = -1; }
static void nav_set(int dir, int src, bool down){
    if (dir < 0 || dir > 3) return;
    uint8_t was = g_nav_src[dir];
    g_nav_src[dir] = (uint8_t)(down ? (was | src) : (was & ~src));
    if (down && !was){ hud_key(dir); g_nav_dir = dir; g_nav_reps = 0; g_nav_next = SDL_GetTicks() + 400; }
    if (!g_nav_src[dir] && g_nav_dir == dir) g_nav_dir = -1;
}
static void nav_tick(void){
    if (g_nav_dir < 0 || !hud_is_open()) return;
    uint32_t now = SDL_GetTicks();
    if ((int32_t)(now - g_nav_next) < 0) return;
    hud_key(g_nav_dir);
    g_nav_reps++;
    int iv = 130 - 10 * g_nav_reps;               /* 120, 110, ... ms */
    if (iv < 30) iv = g_nav_reps > 24 ? 0 : 30;   /* then every frame */
    g_nav_next = now + (uint32_t)iv;
}
static int key_nav_dir(SDL_Keycode k){
    switch (k){ case SDLK_UP: return HUD_KEY_UP; case SDLK_DOWN: return HUD_KEY_DOWN;
                case SDLK_LEFT: return HUD_KEY_LEFT; case SDLK_RIGHT: return HUD_KEY_RIGHT; default: return -1; }
}
static int pad_nav_dir(Uint8 b){
    switch (b){ case SDL_CONTROLLER_BUTTON_DPAD_UP: return HUD_KEY_UP; case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return HUD_KEY_DOWN;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return HUD_KEY_LEFT; case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return HUD_KEY_RIGHT; default: return -1; }
}
/* left stick in the HUD, with hysteresis (on > 16000, off < 10000) */
static void nav_stick(int axis, int v){
    int neg = axis == SDL_CONTROLLER_AXIS_LEFTX ? HUD_KEY_LEFT : HUD_KEY_UP;
    int pos = axis == SDL_CONTROLLER_AXIS_LEFTX ? HUD_KEY_RIGHT : HUD_KEY_DOWN;
    if (v < -16000) nav_set(neg, NAV_STICK, true); else if (v > -10000) nav_set(neg, NAV_STICK, false);
    if (v >  16000) nav_set(pos, NAV_STICK, true); else if (v <  10000) nav_set(pos, NAV_STICK, false);
}
static void hud_toggle(void){ hud_key(HUD_KEY_TOGGLE); g_pad = 0; g_pad_btn = 0; g_pad_axis = 0; nav_clear(); }

/* ---- wall-clock frame limiter ----
 * Deadline-advance model: the deadline moves forward by exactly one frame period
 * each present, so the long-term rate is drift-free. We coarse-sleep (SDL_Delay)
 * to ~1 ms before the deadline, then spin the remainder for sub-ms precision. */
static double   g_perf_freq;
static double   g_period_counts;   /* frame period in perf-counter units; 0 = uncapped */
static uint64_t g_deadline;        /* perf-counter time this frame should end */

void host_set_frame_cap(double fps){
    g_perf_freq     = (double)SDL_GetPerformanceFrequency();
    g_period_counts = (fps > 0.0) ? g_perf_freq / fps : 0.0;
    g_deadline      = 0;           /* re-anchored on the next present */
}

/* Speed control: `--speed N` scales the frame cap; holding TAB (pad: right trigger) runs at
 * hud_turbo_speed() (default x4, a game's HUD option may change it) instead, holding the pad's left
 * trigger at x0.5 (the right trigger / TAB wins when both are held). Audio follows. */
static double g_speed = 1.0;
static bool   g_turbo;
void host_set_speed(double mult){ g_speed = (mult > 0.05) ? mult : 1.0; g_deadline = 0; }
static double cur_speed(void){ return (g_turbo || g_pad_turbo) ? hud_turbo_speed() : g_pad_slow ? 0.5 : g_speed; }
bool host_fast_forward(void){ double sp = cur_speed(); return sp > 1.001 || sp < 0.999; }
static void host_pace(void){
    if (g_period_counts <= 0.0) return;
    double sp = cur_speed();
    uint64_t now = SDL_GetPerformanceCounter();
    if (g_deadline == 0) g_deadline = now;                 /* first frame: anchor */
    g_deadline += (uint64_t)(g_period_counts / sp);
    /* If we fell far behind (debugger pause, hitch), resync instead of bursting
     * to catch up. */
    if (now > g_deadline + (uint64_t)(g_period_counts * 4.0))
        g_deadline = now + (uint64_t)(g_period_counts / sp);
    for (;;){
        now = SDL_GetPerformanceCounter();
        if (now >= g_deadline) break;
        double remain_ms = (double)(g_deadline - now) * 1000.0 / g_perf_freq;
        if (remain_ms > 2.0) SDL_Delay((uint32_t)(remain_ms - 1.0));   /* else spin */
    }
}

/* ---- side panel (hud.h): widescreen layout = game picture at 3x + HUD_PANEL_W x HUD_PANEL_H ---- */
static SDL_Texture *g_panel_tex;
static uint32_t     g_panel_buf[HUD_PANEL_W * HUD_PANEL_H];
static bool         g_panel_on;
static void panel_layout(bool on){
    int lw = on ? g_src.w * 3 + HUD_PANEL_W : g_src.w, lh = on ? g_src.h * 3 : g_src.h;
    if (on && !g_panel_tex)
        g_panel_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, HUD_PANEL_W, HUD_PANEL_H);
    SDL_RenderSetLogicalSize(g_ren, lw, lh);
    /* keep the window height, adapt the width (not when maximized / fullscreen: letterboxed) */
    Uint32 wf = SDL_GetWindowFlags(g_win);
    if (!(wf & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP))){
        int ww, wh, wx, wy; SDL_GetWindowSize(g_win, &ww, &wh);
        SDL_SetWindowSize(g_win, wh * lw / lh, wh);
        /* keep the wider window on its display */
        SDL_Rect ub; int d = SDL_GetWindowDisplayIndex(g_win);
        SDL_GetWindowPosition(g_win, &wx, &wy);
        if (d >= 0 && SDL_GetDisplayUsableBounds(d, &ub) == 0 && wx + wh * lw / lh > ub.x + ub.w){
            int nx = ub.x + ub.w - wh * lw / lh; if (nx < ub.x) nx = ub.x;
            SDL_SetWindowPosition(g_win, nx, wy);
        }
    }
    g_panel_on = on;
}
/* ---- high-resolution layers (hud.h): images / rectangles drawn at the window's real resolution ----
 * Per source image a cached texture (re-uploaded only when its `gen` changes). Enlarging = "sharp
 * bilinear": nearest to an integer multiple in a target texture, then linear to the real size.
 * Shrinking = area average in software to the real size (GPU linear would alias), cached by
 * (gen, source rect, size). */
typedef struct {
    const uint32_t *img; int iw, ih; uint32_t gen; bool loaded;
    SDL_Texture *src;
    SDL_Texture *tgt; int tw, th;
    SDL_Texture *down; int dw, dh;
    uint32_t dgen; int dsx, dsy, dsw, dsh; bool dok;
    uint32_t *dbuf; size_t dcap;
    unsigned used;
} LayerTex;
#define N_LAYER_TEX 12                 /* portraits (4) + maps + logo: all fit, no re-upload per frame */
static LayerTex g_lt[N_LAYER_TEX];
static unsigned g_lt_clock;

static void lt_free(LayerTex *t){
    if (t->src) SDL_DestroyTexture(t->src);
    if (t->tgt) SDL_DestroyTexture(t->tgt);
    if (t->down) SDL_DestroyTexture(t->down);
    free(t->dbuf);
    memset(t, 0, sizeof *t);
}
static LayerTex *lt_get(const HudLayer *l){
    LayerTex *best = &g_lt[0];
    for (int i = 0; i < N_LAYER_TEX; i++){
        LayerTex *t = &g_lt[i];
        if (t->img == l->img && t->iw == l->iw && t->ih == l->ih){ t->used = ++g_lt_clock; return t; }
        if (t->used < best->used) best = t;
    }
    lt_free(best);
    best->img = l->img; best->iw = l->iw; best->ih = l->ih; best->used = ++g_lt_clock;
    return best;
}
/* copy the source rect (repeating with wrap) of texture t into d */
static void copy_wrapped(SDL_Texture *t, int iw, int ih, int sx, int sy, int sw, int sh, bool wrap, const SDL_FRect *d){
    if (!wrap){ SDL_Rect s = { sx, sy, sw, sh }; SDL_RenderCopyF(g_ren, t, &s, d); return; }
    float kx = d->w / (float)sw, ky = d->h / (float)sh;
    for (int oy = 0; oy < sh; ){
        int y = ((sy + oy) % ih + ih) % ih, hh = ih - y; if (hh > sh - oy) hh = sh - oy;
        for (int ox = 0; ox < sw; ){
            int x = ((sx + ox) % iw + iw) % iw, ww = iw - x; if (ww > sw - ox) ww = sw - ox;
            SDL_Rect s = { x, y, ww, hh };
            SDL_FRect p = { d->x + ox * kx, d->y + oy * ky, ww * kx, hh * ky };
            SDL_RenderCopyF(g_ren, t, &s, &p);
            ox += ww;
        }
        oy += hh;
    }
}
/* area-average the layer's source rect into dw x dh pixels */
static void layer_shrink(const HudLayer *l, uint32_t *out, int dw, int dh){
    static int *xs, *col; static int xcap, ccap;      /* output column bounds, source column -> image column */
    if (xcap < dw + 1){ free(xs); xs = (int*)malloc(sizeof(int) * (size_t)(dw + 1)); xcap = xs ? dw + 1 : 0; if (!xs) return; }
    if (ccap < l->sw){ free(col); col = (int*)malloc(sizeof(int) * (size_t)l->sw); ccap = col ? l->sw : 0; if (!col) return; }
    for (int x = 0; x <= dw; x++) xs[x] = (int)((long long)x * l->sw / dw);
    for (int sx = 0; sx < l->sw; sx++){
        int xx = l->sx + sx;
        col[sx] = l->wrap ? (xx % l->iw + l->iw) % l->iw : (xx >= 0 && xx < l->iw ? xx : -1);
    }
    for (int y = 0; y < dh; y++){
        int y0 = (int)((long long)y * l->sh / dh), y1 = (int)((long long)(y + 1) * l->sh / dh); if (y1 <= y0) y1 = y0 + 1;
        for (int x = 0; x < dw; x++){
            int x0 = xs[x], x1 = xs[x + 1]; if (x1 <= x0) x1 = x0 + 1;
            if (x1 > l->sw) x1 = l->sw;
            unsigned r = 0, g = 0, b = 0, n = 0, a = 0;
            for (int sy = y0; sy < y1 && sy < l->sh; sy++){
                int yy = l->sy + sy;
                if (l->wrap) yy = (yy % l->ih + l->ih) % l->ih; else if (yy < 0 || yy >= l->ih) continue;
                const uint32_t *row = l->img + (size_t)yy * l->iw;
                for (int sx = x0; sx < x1; sx++){
                    int xx = col[sx]; if (xx < 0) continue;
                    uint32_t c = row[xx];
                    if (l->alpha){ unsigned ca = c >> 24; r += (c >> 16 & 255) * ca; g += (c >> 8 & 255) * ca; b += (c & 255) * ca; a += ca; n++; }
                    else { r += c >> 16 & 255; g += c >> 8 & 255; b += c & 255; n++; }
                }
            }
            if (l->alpha){                                /* alpha-weighted colour, average alpha */
                if (a){ out[y * dw + x] = ((a + n / 2) / n) << 24 | (r / a) << 16 | (g / a) << 8 | (b / a); }
                else out[y * dw + x] = 0;
                continue;
            }
            if (n){ r /= n; g /= n; b /= n; }
            out[y * dw + x] = 0xFF000000u | r << 16 | g << 8 | b;
        }
    }
}
static void layer_image(const HudLayer *l, const SDL_FRect *d){
    float scx = 1, scy = 1; SDL_RenderGetScale(g_ren, &scx, &scy);
    float rw = d->w * scx, rh = d->h * scy;
    if (rw < 1 || rh < 1) return;
    LayerTex *t = lt_get(l);
    if (rw < l->sw * 0.999f || rh < l->sh * 0.999f){                 /* shrink: software */
        int dw = (int)(rw + 0.5f), dh = (int)(rh + 0.5f);
        if (!t->down || t->dw != dw || t->dh != dh){
            if (t->down) SDL_DestroyTexture(t->down);
            t->down = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, dw, dh);
            t->dw = dw; t->dh = dh; t->dok = false;
            if (!t->down) return;
            SDL_SetTextureScaleMode(t->down, SDL_ScaleModeLinear);
        }
        if (!t->dok || t->dgen != l->gen || t->dsx != l->sx || t->dsy != l->sy || t->dsw != l->sw || t->dsh != l->sh){
            size_t need = (size_t)dw * (size_t)dh;
            if (t->dcap < need){ free(t->dbuf); t->dbuf = (uint32_t*)malloc(need * 4); t->dcap = t->dbuf ? need : 0; if (!t->dbuf) return; }
            layer_shrink(l, t->dbuf, dw, dh);
            SDL_UpdateTexture(t->down, NULL, t->dbuf, dw * 4);
            t->dok = true; t->dgen = l->gen; t->dsx = l->sx; t->dsy = l->sy; t->dsw = l->sw; t->dsh = l->sh;
        }
        if (l->alpha) SDL_SetTextureBlendMode(t->down, SDL_BLENDMODE_BLEND);
        SDL_RenderCopyF(g_ren, t->down, NULL, d);
        return;
    }
    if (!t->src){
        t->src = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, l->iw, l->ih);
        t->loaded = false;
        if (!t->src) return;
    }
    if (!t->loaded || t->gen != l->gen){ SDL_UpdateTexture(t->src, NULL, l->img, l->iw * 4); t->gen = l->gen; t->loaded = true; }
    int k = (int)SDL_ceilf(rw / l->sw - 0.01f), ky = (int)SDL_ceilf(rh / l->sh - 0.01f);
    if (ky > k) k = ky;
    while (k > 1 && ((long)l->sw * k > 4096 || (long)l->sh * k > 4096)) k--;
    if (l->alpha) SDL_SetTextureBlendMode(t->src, SDL_BLENDMODE_BLEND);
    if (k <= 1 || !g_target_ok){
        SDL_SetTextureScaleMode(t->src, SDL_ScaleModeLinear);
        copy_wrapped(t->src, l->iw, l->ih, l->sx, l->sy, l->sw, l->sh, l->wrap, d);
        return;
    }
    int tw = l->sw * k, th = l->sh * k;
    if (!t->tgt || t->tw != tw || t->th != th){
        if (t->tgt) SDL_DestroyTexture(t->tgt);
        t->tgt = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, tw, th);
        t->tw = tw; t->th = th;
        if (!t->tgt) return;
        SDL_SetTextureScaleMode(t->tgt, SDL_ScaleModeLinear);
    }
    SDL_SetRenderTarget(g_ren, t->tgt);
    SDL_SetTextureScaleMode(t->src, SDL_ScaleModeNearest);
    SDL_FRect full = { 0, 0, (float)tw, (float)th };
    if (l->alpha){                                     /* keep the alpha: clear, copy unblended */
        SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 0); SDL_RenderClear(g_ren);
        SDL_SetTextureBlendMode(t->src, SDL_BLENDMODE_NONE);
    }
    copy_wrapped(t->src, l->iw, l->ih, l->sx, l->sy, l->sw, l->sh, l->wrap, &full);
    SDL_SetRenderTarget(g_ren, NULL);
    if (l->alpha){ SDL_SetTextureBlendMode(t->src, SDL_BLENDMODE_BLEND); SDL_SetTextureBlendMode(t->tgt, SDL_BLENDMODE_BLEND); }
    SDL_RenderCopyF(g_ren, t->tgt, NULL, d);
}
/* draw the queued layers of one space: images first, then rectangles */
static void draw_layers(int space, bool panel){
    int n; const HudLayer *ls = hud_layers(&n);
    float scx = 1, scy = 1; SDL_RenderGetScale(g_ren, &scx, &scy);
    if (scx <= 0) scx = 1;
    if (scy <= 0) scy = 1;
    float k = space == HUD_SPACE_GAME && panel ? 3.0f : 1.0f;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < n; i++){
            const HudLayer *l = &ls[i];
            if (l->space != space || (pass == 0) != (l->img != NULL)) continue;
            SDL_FRect d;
            if (space == HUD_SPACE_GAME){
                d.x = (l->x - g_src.x) * k; d.y = (l->y - g_src.y) * k; d.w = l->w * k; d.h = l->h * k;
            } else { d.x = (float)(g_src.w * 3) + l->x; d.y = l->y; d.w = l->w; d.h = l->h; }
            if (l->img) layer_image(l, &d);
            else if (l->marker){
                /* one centre in whole window pixels, sizes in whole pixels: both squares share it */
                float s = (space == HUD_SPACE_GAME ? k : 1.0f) * scx;
                int R = (int)SDL_roundf(l->w * s), E = (int)SDL_roundf(l->h * s);
                if (R < 2) R = 2;
                if (E < 1) E = 1;
                float cx = SDL_floorf(d.x * scx + 0.5f), cy = SDL_floorf(d.y * scy + 0.5f);
                SDL_FRect o = { (cx - R - E) / scx, (cy - R - E) / scy, (float)(2 * (R + E)) / scx, (float)(2 * (R + E)) / scy };
                SDL_FRect in = { (cx - R) / scx, (cy - R) / scy, (float)(2 * R) / scx, (float)(2 * R) / scy };
                SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(g_ren, (uint8_t)(l->rim_argb >> 16), (uint8_t)(l->rim_argb >> 8), (uint8_t)l->rim_argb, 255);
                SDL_RenderFillRectF(g_ren, &o);
                SDL_SetRenderDrawColor(g_ren, (uint8_t)(l->argb >> 16), (uint8_t)(l->argb >> 8), (uint8_t)l->argb, 255);
                SDL_RenderFillRectF(g_ren, &in);
            }
            else {
                /* whole window pixels: each edge rounded on its own (even borders for snapped shapes) */
                float x0 = SDL_roundf(d.x * scx), x1 = SDL_roundf((d.x + d.w) * scx);
                float y0 = SDL_roundf(d.y * scy), y1 = SDL_roundf((d.y + d.h) * scy);
                d.x = x0 / scx; d.w = (x1 - x0) / scx; d.y = y0 / scy; d.h = (y1 - y0) / scy;
                uint8_t a = (uint8_t)(l->argb >> 24);
                SDL_SetRenderDrawBlendMode(g_ren, a == 255 ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(g_ren, (uint8_t)(l->argb >> 16), (uint8_t)(l->argb >> 8), (uint8_t)l->argb, a);
                SDL_RenderFillRectF(g_ren, &d);
                SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_NONE);
            }
        }
}

/* test: SMS_WINDOW_SHOT=<file.ppm> [SMS_WINDOW_SHOT_FRAME=n] writes the composed window of the n-th
 * presented frame (default 1) as a PPM (works with --hidden); SMS_WINDOW_SHOT_GFRAME=n instead takes
 * the first presented frame at or after emulated frame n; SMS_WINDOW_SHOT_EXIT=1 quits after it */
extern uint64_t glue_frame_count(void);
static bool g_shot_quit;
static void window_shot(void){
    static long n = -1, target = 1, gtarget = -1; static const char *path;
    if (n < 0){ path = getenv("SMS_WINDOW_SHOT"); const char *f = getenv("SMS_WINDOW_SHOT_FRAME"); if (f) target = atol(f);
                const char *g = getenv("SMS_WINDOW_SHOT_GFRAME"); if (g) gtarget = atol(g); n = 0; }
    if (!path) return;
    if (gtarget >= 0){ if (n || (long)glue_frame_count() < gtarget) return; n = 1; }
    else if (++n != target) return;
    if (getenv("SMS_WINDOW_SHOT_EXIT")) g_shot_quit = true;
    int w, h; if (SDL_GetRendererOutputSize(g_ren, &w, &h) != 0 || w <= 0 || h <= 0) return;
    uint32_t *px = (uint32_t*)malloc((size_t)w * (size_t)h * 4); if (!px) return;
    if (SDL_RenderReadPixels(g_ren, NULL, SDL_PIXELFORMAT_ARGB8888, px, w * 4) == 0){
        FILE *f = fopen(path, "wb");
        if (f){
            fprintf(f, "P6\n%d %d\n255\n", w, h);
            for (int i = 0; i < w * h; i++){ uint8_t c[3] = { (uint8_t)(px[i] >> 16), (uint8_t)(px[i] >> 8), (uint8_t)px[i] }; fwrite(c, 1, 3, f); }
            fclose(f);
        }
    }
    free(px);
}

bool host_present(const uint32_t *fb, int fb_w, int fb_h){
#ifdef SMS_GPU
    if(g_gpu)host_gl_present(vdp_frame_jobs());else
#endif
    {
    if (!g_tex) return false;
    bool panel = hud_panel_active();
    if (panel != g_panel_on) panel_layout(panel);
    SDL_UpdateTexture(g_tex, NULL, fb, fb_w * (int)sizeof(uint32_t));
    SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255);       /* hi-res layers leave their colour set */
    SDL_RenderClear(g_ren);
    if (panel && g_panel_tex){
        SDL_Rect game = { 0, 0, g_src.w * 3, g_src.h * 3 }, side = { g_src.w * 3, 0, HUD_PANEL_W, HUD_PANEL_H };
        if (side.h != game.h) side.h = game.h;
        SDL_RenderCopy(g_ren, g_tex, &g_src, &game);
        draw_layers(HUD_SPACE_GAME, true);
        hud_panel_render(g_panel_buf, HUD_PANEL_W, HUD_PANEL_H);
        SDL_UpdateTexture(g_panel_tex, NULL, g_panel_buf, HUD_PANEL_W * (int)sizeof(uint32_t));
        SDL_RenderCopy(g_ren, g_panel_tex, NULL, &side);
        draw_layers(HUD_SPACE_PANEL, true);
    } else {
        SDL_RenderCopy(g_ren, g_tex, &g_src, NULL);
        draw_layers(HUD_SPACE_GAME, false);
    }
    hud_layers_clear();
    window_shot();
    SDL_RenderPresent(g_ren);
    }
    (void)fb_h;

    bool quit = g_shot_quit;
    SDL_Event e;
    while (SDL_PollEvent(&e)){
        if (e.type == SDL_QUIT) quit = true;
        else if (e.type == SDL_RENDER_TARGETS_RESET || e.type == SDL_RENDER_DEVICE_RESET)
            for (int i = 0; i < N_LAYER_TEX; i++){ g_lt[i].loaded = false; g_lt[i].dok = false; }   /* re-upload */
        else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE){
            /* A game-owned auxiliary window (tagged with window data "aux", e.g. the auto-map)
             * just hides itself; closing the main window quits. */
            SDL_Window *cw = SDL_GetWindowFromID(e.window.windowID);
            if (cw && SDL_GetWindowData(cw, "aux")) SDL_HideWindow(cw);
            else quit = true;
        }
        else if (e.type == SDL_CONTROLLERDEVICEADDED) pad_open(e.cdevice.which);
        else if (e.type == SDL_CONTROLLERDEVICEREMOVED){     /* pad timed out / unplugged: release all */
            g_pad_btn = 0; g_pad_axis = 0; g_pad_turbo = g_pad_slow = false; g_extra_pad = 0; nav_clear();
        }
        /* ---- settings HUD (hud.c): F1 / pad Start (SMS + panel, hud_start_opens) or Back toggles;
           while open it owns the keys. Pad Back / F2 = side panel on / off (hud_toggle_panel). ---- */
        else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F1){ if (!e.key.repeat) hud_toggle(); }
        else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F2){ if (!e.key.repeat) hud_toggle_panel(); }
        /* F11 or Alt+Enter: fullscreen (desktop resolution, letterboxed) <-> window */
        else if (e.type == SDL_KEYDOWN && !e.key.repeat && (e.key.keysym.sym == SDLK_F11
                 || (e.key.keysym.sym == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT)))){
            bool fs = (SDL_GetWindowFlags(g_win) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
            SDL_SetWindowFullscreen(g_win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
        }
        else if (e.type == SDL_CONTROLLERBUTTONDOWN && e.cbutton.button == SDL_CONTROLLER_BUTTON_BACK){
            if (hud_start_opens() && hud_has_panel()) hud_toggle_panel(); else hud_toggle();
        }
        else if (e.type == SDL_CONTROLLERBUTTONDOWN && e.cbutton.button == SDL_CONTROLLER_BUTTON_START && hud_start_opens()) hud_toggle();
        else if (hud_is_open() && e.type == SDL_KEYDOWN){
            if (e.key.repeat) continue;                 /* own repeat timer (nav_tick) */
            switch (e.key.keysym.sym){
                case SDLK_ESCAPE: case SDLK_z: case SDLK_BACKSPACE: hud_key(HUD_KEY_CLOSE); nav_clear(); break;
                case SDLK_RETURN: case SDLK_SPACE: case SDLK_x:          /* confirm (X = the game's button 2) */
                    hud_key(HUD_KEY_SELECT); if (!hud_is_open()) nav_clear(); break;
                case SDLK_q: case SDLK_PAGEUP:   hud_key(HUD_KEY_PAGE_PREV); break;   /* HUD pages (tabs) */
                case SDLK_e: case SDLK_PAGEDOWN: hud_key(HUD_KEY_PAGE_NEXT); break;
                default: nav_set(key_nav_dir(e.key.keysym.sym), NAV_KEY, true); break;
            }
        }
        else if (hud_is_open() && e.type == SDL_KEYUP) nav_set(key_nav_dir(e.key.keysym.sym), NAV_KEY, false);
        else if (hud_is_open() && e.type == SDL_CONTROLLERBUTTONDOWN){
            switch (e.cbutton.button){
                case SDL_CONTROLLER_BUTTON_A: case SDL_CONTROLLER_BUTTON_START: hud_key(HUD_KEY_CLOSE); nav_clear(); break;
                case SDL_CONTROLLER_BUTTON_B:                    /* confirm (B = the game's button 2) */
                    hud_key(HUD_KEY_SELECT); if (!hud_is_open()) nav_clear(); break;
                case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  hud_key(HUD_KEY_PAGE_PREV); break;
                case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: hud_key(HUD_KEY_PAGE_NEXT); break;
                default: nav_set(pad_nav_dir(e.cbutton.button), NAV_DPAD, true); break;
            }
        }
        else if (hud_is_open() && e.type == SDL_CONTROLLERBUTTONUP) nav_set(pad_nav_dir(e.cbutton.button), NAV_DPAD, false);
        else if (hud_is_open() && e.type == SDL_CONTROLLERAXISMOTION){
            if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX || e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) nav_stick(e.caxis.axis, e.caxis.value);
        }
        else if (e.type == SDL_CONTROLLERBUTTONDOWN){ g_pad_btn |= pad_button(e.cbutton.button); g_extra_pad |= pad_extra(e.cbutton.button); }
        else if (e.type == SDL_CONTROLLERBUTTONUP){ g_pad_btn &= (uint8_t)~pad_button(e.cbutton.button); g_extra_pad &= (uint8_t)~pad_extra(e.cbutton.button); } else if (e.type == SDL_CONTROLLERAXISMOTION){
            const int dz = 12000; int v = e.caxis.value;
            if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX)
                g_pad_axis = (uint8_t)((g_pad_axis & ~(SMS_PAD_LEFT|SMS_PAD_RIGHT)) | (v < -dz ? SMS_PAD_LEFT : v > dz ? SMS_PAD_RIGHT : 0));
            else if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY)
                g_pad_axis = (uint8_t)((g_pad_axis & ~(SMS_PAD_UP|SMS_PAD_DOWN)) | (v < -dz ? SMS_PAD_UP : v > dz ? SMS_PAD_DOWN : 0));
            else if (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)
                g_pad_turbo = v > 16000;
            else if (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT)
                g_pad_slow = v > 16000;
        }
        else if (e.type == SDL_KEYDOWN){
            if (e.key.keysym.sym == SDLK_ESCAPE) quit = true;
            else if (e.key.keysym.sym == SDLK_TAB) g_turbo = true;
            else { g_pad |= key_to_pad(e.key.keysym.sym); g_extra_key |= key_extra(e.key.keysym.sym); }
        } else if (e.type == SDL_KEYUP){
            if (e.key.keysym.sym == SDLK_TAB) g_turbo = false;
            g_pad &= (uint8_t)~key_to_pad(e.key.keysym.sym);
            g_extra_key &= (uint8_t)~key_extra(e.key.keysym.sym);
        }
    }
    if (hud_is_open()) g_extra_pad = g_extra_key = 0;     /* the HUD owns LB/RB; nothing stays held after it */
    hud_set_extra_buttons((uint8_t)(g_extra_pad | g_extra_key));
    nav_tick();

    uint64_t end=SDL_GetPerformanceCounter();
    if(g_work_start && g_work_count<8192)g_work_ms[g_work_count++]=(end-g_work_start)*1000.0/SDL_GetPerformanceFrequency();
    host_pace();      /* hold the loop to realtime (display-refresh-independent) */
    g_work_start=SDL_GetPerformanceCounter();
    return !quit;
}

void host_shutdown(void){
#ifdef SMS_GPU
    host_gl_shutdown();
#endif
    g_gpu=false;
    if (g_panel_tex) SDL_DestroyTexture(g_panel_tex);
    g_panel_tex = NULL; g_panel_on = false;
    for (int i = 0; i < N_LAYER_TEX; i++) lt_free(&g_lt[i]);
    hud_set_hires(false);
    if (g_tex) SDL_DestroyTexture(g_tex);
    if (g_ren) SDL_DestroyRenderer(g_ren);
    if (g_win) SDL_DestroyWindow(g_win);
    g_tex = NULL; g_ren = NULL; g_win = NULL;
    SDL_Quit();
}

/* ---- audio ---- */
static SDL_AudioDeviceID g_adev;
static SDL_AudioStream  *g_astream;   /* PSG src_rate stereo -> device rate */
static uint32_t g_audio_src_rate;
static int      g_audio_cur_rate;
static int      g_audio_dev_rate = 48000;

bool host_audio_init(uint32_t src_rate){
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0){
        fprintf(stderr, "[host] SDL audio init failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq     = 48000;
    want.format   = AUDIO_S16SYS;
    want.channels = 2;
    want.samples  = 1024;
    want.callback = NULL;                  /* we push via SDL_QueueAudio */
    g_adev = SDL_OpenAudioDevice(NULL, 0, &want, &have,
                                 SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!g_adev){
        fprintf(stderr, "[host] SDL_OpenAudioDevice: %s\n", SDL_GetError());
        return false;
    }
    g_astream = SDL_NewAudioStream(AUDIO_S16SYS, 2, (int)src_rate,
                                   have.format, have.channels, have.freq);
    if (!g_astream){
        fprintf(stderr, "[host] SDL_NewAudioStream: %s\n", SDL_GetError());
        SDL_CloseAudioDevice(g_adev); g_adev = 0;
        return false;
    }
    g_audio_src_rate = src_rate; g_audio_cur_rate = (int)src_rate; g_audio_dev_rate = have.freq;
    SDL_PauseAudioDevice(g_adev, 0);       /* start playback */
    fprintf(stderr, "[host] audio: PSG %u Hz -> device %d Hz stereo\n",
            src_rate, have.freq);
    return true;
}

/* Audio follows the emulation speed (TAB turbo, --speed N): the same samples per frame are
 * resampled as if they were recorded at src_rate*speed, so fast-forward plays pitched up instead of
 * going silent, and the queue stays balanced. The stream is rebuilt whenever the speed changes. */
static void audio_follow_speed(void){
    double sp = cur_speed();
    int rate = (int)((double)g_audio_src_rate * sp + 0.5);
    if (rate < 1000) rate = 1000;
    if (rate == g_audio_cur_rate) return;
    SDL_AudioStream *ns = SDL_NewAudioStream(AUDIO_S16SYS, 2, rate, AUDIO_S16SYS, 2, g_audio_dev_rate);
    if (!ns) return;
    if (g_astream) SDL_FreeAudioStream(g_astream);
    g_astream = ns; g_audio_cur_rate = rate;
    SDL_ClearQueuedAudio(g_adev);
}

void host_audio_submit(const int16_t *stereo_frames, size_t frame_count){
    if (!g_astream || !g_adev || frame_count == 0) return;
    audio_follow_speed();
    if (SDL_GetQueuedAudioSize(g_adev) > (uint32_t)(g_audio_dev_rate * 4 / 2)) return;   /* > 0.5 s queued: drop */
    SDL_AudioStreamPut(g_astream, stereo_frames,
                       (int)(frame_count * 2 * sizeof(int16_t)));
    int avail;
    while ((avail = SDL_AudioStreamAvailable(g_astream)) > 0){
        uint8_t tmp[8192];
        int want = avail > (int)sizeof(tmp) ? (int)sizeof(tmp) : avail;
        int got = SDL_AudioStreamGet(g_astream, tmp, want);
        if (got <= 0) break;
        SDL_QueueAudio(g_adev, tmp, (uint32_t)got);
    }
}

void host_audio_shutdown(void){
    if (g_astream){ SDL_FreeAudioStream(g_astream); g_astream = NULL; }
    if (g_adev){ SDL_CloseAudioDevice(g_adev); g_adev = 0; }
}
