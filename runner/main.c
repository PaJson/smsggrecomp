/*
 * main.c - headless SMS/GG runner entry point.
 *
 *   <exe> <rom.sms|rom.gg> [--frames N] [--gg]
 *
 * Loads the ROM, runs the recompiled game for N frames (default 600), and on
 * exit reports the frame count and any dispatch misses (also written to
 * dispatch_misses.log next to the working directory) so the dispatch-miss loop
 * (PRINCIPLES #13a) can resolve them. Video/audio host + always-on rings + TCP
 * (DEBUG.md) layer on top of this core later.
 */
#include "glue.h"
#include "coverage_route.h"
extern void sms_write8(uint16_t addr, uint8_t val);
#ifdef SMSGG_HAVE_GAME_LAYOUT
#include "game_spec.h"
#include "art.h"
#endif
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <stdbool.h>
#include <stdlib.h>
static bool g_automated_input;
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

static bool g_benchmark;
static int g_press_n;
static double g_speed_opt = 1.0;   /* --speed N (window mode) */
static uint64_t g_audio_frames;
static uint64_t g_benchmark_start;
static double g_route_cpu=-1;
static double cpu_seconds(void){
#ifdef _WIN32
    FILETIME a,b,k,u; ULARGE_INTEGER x,y;
    GetProcessTimes(GetCurrentProcess(), &a,&b,&k,&u);
    x.LowPart=k.dwLowDateTime; x.HighPart=k.dwHighDateTime;
    y.LowPart=u.dwLowDateTime; y.HighPart=u.dwHighDateTime;
    return (x.QuadPart+y.QuadPart)*1e-7;
#else
    return (double)clock()/CLOCKS_PER_SEC;
#endif
}
static int benchmark_frame(const uint32_t *fb, int w, int h){
    if(g_route_cpu<0 && glue_frame_count()>=g_benchmark_start)g_route_cpu=cpu_seconds();
    (void)fb; (void)w; (void)h; return 0;
}

/* --title-shot <file.png> [--title-frames N] [--title-frame F]: find the game's title screen and save
 * it (headless, no input, no save files), then quit. Used by front ends that show a picture of each
 * game when the player supplied no cover art.
 * The title = the longest stretch of a near-still, non-blank picture in the first N frames (default
 * 3600 = 60 s): a stretch lasts while fewer than 10 % of the pixels differ from its first frame
 * (title animations, blinking "PRESS START"); blank = under 5 % lit pixels or fewer than 4 colours
 * (fades, black gaps, SEGA logo on black is short anyway). From the winning stretch the frame with
 * the most pixels outside its main background colour is kept (blinking text / a logo that appears
 * late are visible; "most lit pixels" failed on Sonic Chaos' light background). Checked on all 24
 * builds of the project (Phantasy Star incl. 7 translations, Golvellius, Miracle Warriors, Sonic 1 /
 * 2 / Chaos): their titles appear between frames 100 and 1500 and win against intros and attract
 * demos. --title-frame F takes exactly frame F instead (per-game override).
 * Writes the visible area (SMS 256x192, GG 160x144); exit code 0 = written, 1 = nothing found. */
#include "png_write.h"
#include "video/sms_vdp.h"           /* SMS_SCREEN_W / GG_SCREEN_W ... */
static struct {
    const char *out; uint64_t fixed, limit;
    int cx, cy, w, h; size_t n;
    uint32_t *cur, *ref, *cand, *best;
    uint64_t run_start, best_len; size_t cand_score; bool have_ref, have_cand, have_best, written;
} g_ts;
static void ts_close_run(uint64_t f){
    if (g_ts.have_cand && (!g_ts.have_best || f - g_ts.run_start > g_ts.best_len)){
        uint32_t *t=g_ts.best; g_ts.best=g_ts.cand; g_ts.cand=t;
        g_ts.best_len=f-g_ts.run_start; g_ts.have_best=true;
    }
    g_ts.have_cand=false; g_ts.cand_score=0;
}
static bool ts_write(const uint32_t *px){
    char tmp[1100]; snprintf(tmp,sizeof tmp,"%s.tmp",g_ts.out);
    if (png_write_argb(tmp,px,g_ts.w,g_ts.h,g_ts.w)!=0){ remove(tmp); return false; }
    remove(g_ts.out);
    if (rename(tmp,g_ts.out)!=0){ remove(tmp); return false; }
    return true;
}
static int title_frame_cb(const uint32_t *fb, int w, int h){
    (void)h;
    if (!fb) return 0;
    uint64_t f=glue_frame_count();
    for (int y=0;y<g_ts.h;y++) memcpy(g_ts.cur+(size_t)y*g_ts.w, fb+(size_t)(y+g_ts.cy)*w+g_ts.cx, (size_t)g_ts.w*4);
    if (g_ts.fixed){
        if (f<g_ts.fixed) return 0;
        g_ts.written=ts_write(g_ts.cur); return 1;
    }
    size_t lit=0, ncol=0, top=0; static uint32_t cnt[4096]; memset(cnt,0,sizeof cnt);
    for (size_t i=0;i<g_ts.n;i++){
        uint32_t p=g_ts.cur[i];
        if (((p>>16)&255)+((p>>8)&255)+(p&255) > 60) lit++;
        unsigned k=((p>>12)&0xF00)|((p>>8)&0xF0)|((p>>4)&0xF);
        if (!cnt[k]++) ncol++;
        if (cnt[k]>top) top=cnt[k];
    }
    size_t content=g_ts.n-top;          /* pixels not in the main background colour */
    if (g_ts.have_ref){
        size_t diff=0;
        for (size_t i=0;i<g_ts.n;i++) diff+=g_ts.cur[i]!=g_ts.ref[i];
        if (diff*10>g_ts.n){ ts_close_run(f); g_ts.have_ref=false; }
    }
    if (!g_ts.have_ref){ memcpy(g_ts.ref,g_ts.cur,g_ts.n*4); g_ts.have_ref=true; g_ts.run_start=f; }
    if (lit*20>=g_ts.n && ncol>=4 && (!g_ts.have_cand || content>g_ts.cand_score)){
        memcpy(g_ts.cand,g_ts.cur,g_ts.n*4); g_ts.cand_score=content; g_ts.have_cand=true;
    }
    if (f<g_ts.limit) return 0;
    ts_close_run(f+1);
    if (g_ts.have_best) g_ts.written=ts_write(g_ts.best);
    return 1;
}

#ifdef SMS_HAVE_SDL
#include "host_sdl.h"
#include "hud.h"
#include "video/sms_vdp.h"
/* Bridge the runner's per-frame callback to the SDL host. Returns nonzero to
 * request shutdown (glue.c then unwinds the run loop). */
static int sdl_frame_cb(const uint32_t *fb, int w, int h){
    if(g_benchmark)benchmark_frame(fb,w,h);
    /* game overlay (hud_set_overlay): drawn on a copy, the emulator's frame stays untouched */
    static uint32_t ovl[256 * 240];
    if (fb && hud_has_overlay() && w * h <= (int)(sizeof ovl / sizeof ovl[0])){
        memcpy(ovl, fb, (size_t)w * (size_t)h * sizeof(uint32_t));
        hud_draw_overlay(ovl, w, h);
        fb = ovl;
    }
    bool keep = host_present(fb, w, h);
    /* Settings HUD (hud.c, F1 / pad Back) or the quit prompt (Esc): the emulation pauses here; the
     * last frame is shown with the overlay until it is closed. Audio just runs dry (silence) meanwhile. */
    if (keep && fb && (hud_is_open() || host_quit_asking())){
        static uint32_t over[256 * 240];
        while (keep && (hud_is_open() || host_quit_asking()) && w * h <= (int)(sizeof over / sizeof over[0])){
            memcpy(over, fb, (size_t)w * (size_t)h * sizeof(uint32_t));
            hud_draw(over, w, h);
            if (host_quit_asking()) hud_draw_quit_prompt(over, w, h);
            keep = host_present(over, w, h);
        }
        if (keep) keep = host_present(fb, w, h);
    }
    if (!g_automated_input) glue_set_pad1(host_get_pad1());
    return keep ? 0 : 1;
}
#endif

/* ---- audio output sinks --------------------------------------------------
 * glue hands us interleaved-stereo S16 frames once per video frame. We fan
 * them out to any active output: a WAV file (headless/observable dump) and/or
 * the live SDL audio device. Both are optional and independent. */
static FILE    *g_wav;
static uint32_t g_wav_data_bytes;
#ifdef SMS_HAVE_SDL
static bool     g_audio_live;
#endif

static void wav_put32(FILE *f, uint32_t v){ fputc(v&0xFF,f); fputc((v>>8)&0xFF,f); fputc((v>>16)&0xFF,f); fputc((v>>24)&0xFF,f); }
static void wav_put16(FILE *f, uint16_t v){ fputc(v&0xFF,f); fputc((v>>8)&0xFF,f); }

static bool wav_open(const char *path, uint32_t rate){
    g_wav = fopen(path, "wb");
    if (!g_wav){ fprintf(stderr,"[runner] cannot open WAV %s\n", path); return false; }
    const uint16_t ch = 2, bits = 16;
    fwrite("RIFF",1,4,g_wav); wav_put32(g_wav, 0);            /* size patched on close */
    fwrite("WAVE",1,4,g_wav);
    fwrite("fmt ",1,4,g_wav); wav_put32(g_wav, 16);
    wav_put16(g_wav, 1);                                      /* PCM */
    wav_put16(g_wav, ch);
    wav_put32(g_wav, rate);
    wav_put32(g_wav, rate * ch * (bits/8));                   /* byte rate */
    wav_put16(g_wav, (uint16_t)(ch * (bits/8)));              /* block align */
    wav_put16(g_wav, bits);
    fwrite("data",1,4,g_wav); wav_put32(g_wav, 0);            /* size patched on close */
    g_wav_data_bytes = 0;
    fprintf(stderr,"[runner] writing audio to %s (%u Hz S16 stereo)\n", path, rate);
    return true;
}
static void wav_close(void){
    if (!g_wav) return;
    fflush(g_wav);
    fseek(g_wav, 4,  SEEK_SET); wav_put32(g_wav, 36 + g_wav_data_bytes);
    fseek(g_wav, 40, SEEK_SET); wav_put32(g_wav, g_wav_data_bytes);
    fclose(g_wav); g_wav = NULL;
}

static void audio_sink(const int16_t *frames, size_t n){
    g_audio_frames += n;
    if (g_wav){
        fwrite(frames, 2*sizeof(int16_t), n, g_wav);
        g_wav_data_bytes += (uint32_t)(n * 2 * sizeof(int16_t));
    }
#ifdef SMS_HAVE_SDL
    if (g_audio_live) host_audio_submit(frames, n);
#endif
}

/* ---- headless scripted input (--press FRAME:KEYS, repeatable) ----
 * KEYS letters: U D L R (d-pad), A=button1, B=button2, S=start; empty = release.
 * Each directive sets player 1's held buttons from its frame until a later one.
 * Deterministic input for automated tests — no window needed. */
/* Growable; sorted by frame (stable) on first use, then consumed with a cursor (O(1)/frame). */
typedef struct { uint64_t frame; uint8_t mask; uint32_t seq; } PressEv;
static PressEv *g_press; static int g_press_cap;

static uint8_t press_keys_to_mask(const char *s){
    uint8_t m = 0;
    for (; *s; s++){
        switch (*s){
            case 'U': case 'u': m|=SMS_PAD_UP;    break;
            case 'D': case 'd': m|=SMS_PAD_DOWN;  break;
            case 'L': case 'l': m|=SMS_PAD_LEFT;  break;
            case 'R': case 'r': m|=SMS_PAD_RIGHT; break;
            case 'A': case 'a': m|=SMS_PAD_B1;    break;
            case 'B': case 'b': m|=SMS_PAD_B2;    break;
            case 'S': case 's': m|=SMS_PAD_START; break;
            case 'P': case 'p': m|=SMS_PAD_PAUSE; break;   /* SMS console Pause (NMI) */
            default: break;   /* ignore separators */
        }
    }
    return m;
}
static void press_add(const char *tok){
    if (g_press_n >= g_press_cap){
        int nc = g_press_cap ? g_press_cap * 2 : 1024;
        PressEv *n = (PressEv *)realloc(g_press, (size_t)nc * sizeof *n); if (!n) return;
        g_press = n; g_press_cap = nc;
    }
    const char *colon = strchr(tok, ':');
    g_press[g_press_n].frame = strtoull(tok, NULL, 0);
    g_press[g_press_n].mask  = press_keys_to_mask(colon ? colon+1 : "");
    g_press[g_press_n].seq   = (uint32_t)g_press_n;
    g_press_n++;
}
/* --poke FRAME:ADDR=VAL (repeatable; hex ADDR/VAL) / --poke-file (one per line): write a RAM byte
 * at a frame boundary (debug/testing). */
typedef struct { uint64_t frame; uint16_t addr; uint8_t val; uint32_t seq; } PokeEv;
static PokeEv *g_poke; static int g_poke_cap;
static int g_poke_n;
static void poke_add(const char *tok){
    char *c = NULL; uint64_t f = strtoull(tok, &c, 0);
    if (!c || *c != ':') return;
    unsigned a = 0, v = 0; if (sscanf(c+1, "%x=%x", &a, &v) != 2) return;
    if (g_poke_n >= g_poke_cap){
        int nc = g_poke_cap ? g_poke_cap * 2 : 256;
        PokeEv *n = (PokeEv *)realloc(g_poke, (size_t)nc * sizeof *n); if (!n) return;
        g_poke = n; g_poke_cap = nc;
    }
    g_poke[g_poke_n].frame=f; g_poke[g_poke_n].addr=(uint16_t)a; g_poke[g_poke_n].val=(uint8_t)v;
    g_poke[g_poke_n].seq=(uint32_t)g_poke_n; g_poke_n++;
}
static int press_cmp(const void *x, const void *y){
    const PressEv *a = x, *b = y;
    if (a->frame != b->frame) return a->frame < b->frame ? -1 : 1;
    return a->seq < b->seq ? -1 : (a->seq > b->seq);
}
static int poke_cmp(const void *x, const void *y){
    const PokeEv *a = x, *b = y;
    if (a->frame != b->frame) return a->frame < b->frame ? -1 : 1;
    return a->seq < b->seq ? -1 : (a->seq > b->seq);
}
static uint8_t press_provider(uint64_t frame){
    static int sorted, pi, ki; static uint8_t m; static uint64_t last; static int started;
    if (!sorted){
        if (g_press_n) qsort(g_press, (size_t)g_press_n, sizeof *g_press, press_cmp);
        if (g_poke_n)  qsort(g_poke,  (size_t)g_poke_n,  sizeof *g_poke,  poke_cmp);
        sorted = 1;
    }
    if (started && frame < last){ pi = ki = 0; m = 0; }    /* rewound (reset): start over */
    started = 1; last = frame;
    while (ki < g_poke_n && g_poke[ki].frame < frame) ki++;  /* missed frames are skipped, as before */
    while (ki < g_poke_n && g_poke[ki].frame == frame){ sms_write8(g_poke[ki].addr, g_poke[ki].val); ki++; }
    while (pi < g_press_n && g_press[pi].frame <= frame){ m = g_press[pi].mask; pi++; }
    return m;   /* the latest directive at/under the current frame wins */
}

static int ci_cmp(const char *a, const char *b){
    for (; *a && *b; a++, b++){
        int ca=*a, cb=*b;
        if (ca>='A'&&ca<='Z') ca+=32;
        if (cb>='A'&&cb<='Z') cb+=32;
        if (ca!=cb) return ca-cb;
    }
    return (unsigned char)*a - (unsigned char)*b;
}
static bool ends_with(const char *s, const char *suf){
    size_t ls=strlen(s), lf=strlen(suf);
    return ls>=lf && ci_cmp(s+ls-lf, suf)==0;
}

int main(int argc, char **argv){
    const char *rom = NULL;
    uint64_t frames = 600;
    int force_gg = -1;
    uint64_t dump_frame = 0;
    const char *dump_out = NULL;
    const char *vdp_trace = NULL;   /* per-frame VDP-state CSV (oracle diff) */
    const char *psg_log = NULL;     /* chip_ring: (cyc,val) PSG-write log */
    bool want_window = false;
    int  win_scale = 3;
    bool interp = false;       /* oracle: run the reference superzazu interpreter */
    const char *audio_wav = NULL;   /* dump PSG output to this WAV */
    bool mute = false;              /* suppress live SDL audio (window build) */
    const char *route = NULL, *report = NULL;
    const char *benchmark_out = NULL;
    const char *gpu_report = NULL, *gpu_dump = NULL;
    int internal_scale=0; /* ordinary native SDL until internal resolution is requested */
    bool hidden=false;
    double start_cpu;
#ifdef SMS_CYC_WATCH
    const char *cyc_watch_file = NULL; uint16_t cyc_watch_addr = 0;
#endif

    bool frames_set = false;
    for (int i=1;i<argc;i++){
        if (strcmp(argv[i],"--frames")==0 && i+1<argc){ frames = strtoull(argv[++i],NULL,0); frames_set = true; }
        else if (strcmp(argv[i],"--gg")==0) force_gg = 1;
        else if (strcmp(argv[i],"--sms")==0) force_gg = 0;
        else if (strcmp(argv[i],"--dump-frame")==0 && i+1<argc) dump_frame = strtoull(argv[++i],NULL,0);
        else if (strcmp(argv[i],"--dump-out")==0 && i+1<argc) dump_out = argv[++i];
        else if (strcmp(argv[i],"--vdp-trace")==0 && i+1<argc) vdp_trace = argv[++i];
        else if (strcmp(argv[i],"--psg-log")==0 && i+1<argc) psg_log = argv[++i];
        else if (strcmp(argv[i],"--force-display")==0){ extern int g_vdp_force_display; g_vdp_force_display=1; }
        else if (strcmp(argv[i],"--sprite-limit")==0 && i+1<argc){ extern int g_vdp_sprite_limit; g_vdp_sprite_limit=(int)strtol(argv[++i],NULL,10); }
        else if (strcmp(argv[i],"--interp")==0) interp = true;
        else if (strcmp(argv[i],"--audio-wav")==0 && i+1<argc) audio_wav = argv[++i];
        else if (strcmp(argv[i],"--mute")==0) mute = true;
        else if (strcmp(argv[i],"--benchmark")==0) g_benchmark = true;
        else if (strcmp(argv[i],"--benchmark-start")==0 && i+1<argc) g_benchmark_start=strtoull(argv[++i],NULL,10);
        else if (strcmp(argv[i],"--benchmark-output")==0 && i+1<argc) benchmark_out=argv[++i];
        else if (strcmp(argv[i],"--internal-scale")==0 && i+1<argc){
            internal_scale=(int)strtol(argv[++i],NULL,10);
            if(internal_scale!=1&&internal_scale!=2&&internal_scale!=4&&internal_scale!=8&&internal_scale!=16){
                fprintf(stderr,"internal scale must be 1, 2, 4, 8 or 16\n");return 2;
            }
        }
        else if (strcmp(argv[i],"--hidden")==0) hidden=true;
        else if (strcmp(argv[i],"--gpu-report")==0 && i+1<argc) gpu_report=argv[++i];
        else if (strcmp(argv[i],"--gpu-dump")==0 && i+1<argc) gpu_dump=argv[++i];
#ifdef SMS_CYC_WATCH
        else if (strcmp(argv[i],"--cyc-watch")==0 && i+2<argc){
            cyc_watch_addr = (uint16_t)strtoul(argv[++i],NULL,0); cyc_watch_file = argv[++i]; }
#endif
        else if (strcmp(argv[i],"--press")==0 && i+1<argc) press_add(argv[++i]);
        else if (strcmp(argv[i],"--poke")==0 && i+1<argc) poke_add(argv[++i]);
        else if (strcmp(argv[i],"--poke-file")==0 && i+1<argc){   /* one FRAME:ADDR=VAL per line, # comments */
            FILE *pf=fopen(argv[++i],"r"); char ln[128];
            if (!pf){ fprintf(stderr,"[runner] cannot open poke file\n"); return 2; }
            while (fgets(ln,sizeof ln,pf)){ if (ln[0]>='0' && ln[0]<='9'){ ln[strcspn(ln,"\r\n")]=0; poke_add(ln); } }
            fclose(pf); }
        else if (strcmp(argv[i],"--speed")==0 && i+1<argc) g_speed_opt = atof(argv[++i]);
        else if (strcmp(argv[i],"--press-file")==0 && i+1<argc){   /* one FRAME:KEYS per line, # comments */
            FILE *pf=fopen(argv[++i],"r"); char ln[128];
            if (!pf){ fprintf(stderr,"[runner] cannot open press file\n"); return 2; }
            while (fgets(ln,sizeof ln,pf)){ if (ln[0]>='0' && ln[0]<='9'){ ln[strcspn(ln,"\r\n")]=0; press_add(ln); } }
            fclose(pf); }
        else if (strcmp(argv[i],"--title-shot")==0 && i+1<argc) g_ts.out=argv[++i];
        else if (strcmp(argv[i],"--title-frames")==0 && i+1<argc) g_ts.limit=strtoull(argv[++i],NULL,0);
        else if (strcmp(argv[i],"--title-frame")==0 && i+1<argc) g_ts.fixed=strtoull(argv[++i],NULL,0);
        else if (strcmp(argv[i],"--route")==0 && i+1<argc) route=argv[++i];
        else if (strcmp(argv[i],"--report")==0 && i+1<argc) report=argv[++i];
        else if (strcmp(argv[i],"--window")==0){
            want_window = true;
            if (i+1<argc && argv[i+1][0] != '-') win_scale = (int)strtol(argv[++i],NULL,0);
        }
        else if (argv[i][0] != '-') rom = argv[i];
        else {fprintf(stderr,"[runner] unknown or incomplete arg: %s\n", argv[i]);return 2;}
    }
    if (g_ts.out){            /* title shot: headless, never read / write saves, no art, own frame limit */
#ifdef _WIN32
        _putenv("SMS_NO_SAVE=1"); _putenv("SMS_NO_ART=1");
#else
        setenv("SMS_NO_SAVE","1",1); setenv("SMS_NO_ART","1",1);
#endif
        if (!g_ts.limit) g_ts.limit=3600;
        want_window=false; frames_set=true; frames=(g_ts.fixed ? g_ts.fixed : g_ts.limit)+2;
    }
    if (!rom){
        fprintf(stderr,"usage: %s <rom.sms|rom.gg> [--frames N] [--gg|--sms] "
                       "[--window [scale]] [--audio-wav out.wav] [--mute] "
                       "[--press FRAME:KEYS] [--route route.txt] [--report out.json] [--interp]\n", argv[0]);
        return 2;
    }

    bool is_gg = (force_gg >= 0) ? (force_gg==1) : ends_with(rom, ".gg");
#ifdef SMSGG_HAVE_GAME_LAYOUT
    is_gg=g_game_spec.platform==GAME_PLATFORM_GG;
    if (force_gg>=0 && (force_gg==1)!=is_gg){
        fprintf(stderr,"[runner] platform override does not match this generated build\n");return 2;
    }
#endif

    /* In windowed mode, default to run-until-closed unless --frames was given. */
    if (want_window && !frames_set) frames = 0;
    {   extern bool g_quiet_exit;            /* played, not tested: no end-of-run statistics */
        g_quiet_exit = want_window && !frames_set && !route && !getenv("SMS_STATS"); }

    fprintf(stderr,"[runner] SMS/GG recompiled runner (%s)\n",
            want_window ? "windowed" : "headless");
    if (!glue_load_rom(rom)) return 1;
    if (route){
        if (g_press_n || !coverage_route_load(route,glue_rom_crc32())) return 2;
        if (frames_set && coverage_route_last_frame()>frames){coverage_route_close();return 2;}
        if (!frames_set)frames=coverage_route_last_frame()+1;
    }
    fprintf(stderr,"[runner] loaded %s (%llu bytes), platform=%s, running %s\n",
            rom, (unsigned long long)glue_rom_size(), is_gg?"GG":"SMS",
            frames ? "to frame limit" : "until window close");

    glue_init(is_gg, frames);
#ifdef SMSGG_HAVE_GAME_LAYOUT
    if (g_game_spec.on_post_reset) g_game_spec.on_post_reset();   /* per-game hook: ROM loaded, CPU reset */
#endif
    if (route){
        g_automated_input=true;
        glue_set_boundary_callback(coverage_route_frame);
        if (coverage_route_frame(0)){coverage_route_close();return 1;}
    }
    if (g_benchmark) glue_set_frame_callback(benchmark_frame);
    if (g_ts.out){
        g_ts.w = is_gg ? GG_SCREEN_W : SMS_SCREEN_W; g_ts.h = is_gg ? GG_SCREEN_H : SMS_SCREEN_H;
        g_ts.cx = (SMS_SCREEN_W-g_ts.w)/2; g_ts.cy = (SMS_SCREEN_H-g_ts.h)/2;
        g_ts.n = (size_t)g_ts.w*g_ts.h;
        g_ts.cur=malloc(g_ts.n*4); g_ts.ref=malloc(g_ts.n*4); g_ts.cand=malloc(g_ts.n*4); g_ts.best=malloc(g_ts.n*4);
        if (!g_ts.cur || !g_ts.ref || !g_ts.cand || !g_ts.best) return 1;
        glue_set_native_output(true);
        glue_set_frame_callback(title_frame_cb);
    }
    if (dump_out) glue_set_dump(dump_frame ? dump_frame : frames, dump_out);
    if (vdp_trace) glue_set_vdp_trace(vdp_trace);
    if (psg_log) glue_set_psg_log(psg_log);
#ifdef SMS_CYC_WATCH
    if (cyc_watch_file){
        glue_set_cyc_watch(cyc_watch_addr, cyc_watch_file);
        fprintf(stderr,"[runner] cyc-watch anchor=0x%04X -> %s\n", cyc_watch_addr, cyc_watch_file);
    }
#endif
    if (g_press_n || g_poke_n){
        g_automated_input=true;
        glue_set_input_cb(press_provider);
        fprintf(stderr,"[runner] scripted input: %d directive(s)\n", g_press_n);
    }

#ifdef SMSGG_HAVE_GAME_LAYOUT
    art_setup_game_logo(g_game_spec.display_name, glue_rom_path());   /* optional Images/<title> (logo).png */
#endif

    if (want_window){
        (void)win_scale;   /* used only in the SDL block below */
#ifdef SMS_HAVE_SDL
        /* SMS shows the full 256x192; GG shows a 160x144 centred crop. */
        host_set_internal_scale(internal_scale);
        host_set_hidden(hidden);
        char win_title[160];
        snprintf(win_title, sizeof win_title, "%s - recompiled", g_game_spec.display_name);
        int cx = is_gg ? (SMS_SCREEN_W-GG_SCREEN_W)/2 : 0;
        int cy = is_gg ? (SMS_SCREEN_H-GG_SCREEN_H)/2 : 0;
        int cw = is_gg ? GG_SCREEN_W : SMS_SCREEN_W;
        int ch = is_gg ? GG_SCREEN_H : SMS_SCREEN_H;
        if (host_init(SMS_SCREEN_W, SMS_SCREEN_H, cx, cy, cw, ch, win_scale,
                      win_title)){
            glue_set_frame_callback(sdl_frame_cb);
            glue_set_native_output(!host_uses_gpu());
            host_set_frame_cap(g_benchmark ? 0 : glue_frame_rate());
            host_set_speed(g_speed_opt);
        } else
            fprintf(stderr,"[runner] SDL window init failed; running headless\n");
#else
        fprintf(stderr,"[runner] --window requested but built without SDL "
                       "(-DSMS_HAVE_SDL); running headless\n");
#endif
    }

    /* Audio outputs (independent; either/both may be active). */
    bool audio_on = false;
#ifdef SMS_HAVE_SDL
    if (want_window && !mute){
        if (host_audio_init(glue_audio_sample_rate())){ g_audio_live = true; audio_on = true; }
    }
#else
    (void)mute;
#endif
    if (audio_wav){
        if (wav_open(audio_wav, glue_audio_sample_rate())) audio_on = true;
    }
    if (audio_on || g_benchmark) glue_set_audio_sink(audio_sink);

    start_cpu = cpu_seconds();
    if (interp){
        fprintf(stderr,"[runner] ORACLE reference mode (superzazu interpreter)\n");
        glue_run_interp();
    } else {
        glue_run();
    }
#ifdef _WIN32
    /* Started by a front end (the launcher sets SMS_FRONTEND_PID)? While this window still has the
       focus, allow the front end to take the foreground back: Windows only lets the foreground program
       hand it on, and once this window is gone the launcher would come back without the focus. */
    {   const char *fp = getenv("SMS_FRONTEND_PID");
        if (fp && want_window) AllowSetForegroundWindow((DWORD)strtoul(fp, NULL, 10)); }
#endif
    glue_sram_flush();

    if (benchmark_out){
        FILE *f=fopen(benchmark_out,"w");
        if (!f){ perror(benchmark_out); return 1; }
        fprintf(f,"{\"frames\":%llu,\"cpu_ms\":%.6f,\"audio_frames\":%llu,\"dispatch_misses\":%d,\"route_frames\":%llu,\"route_cpu_ms\":%.6f}\n",
            (unsigned long long)glue_frame_count(),(cpu_seconds()-start_cpu)*1000,
            (unsigned long long)g_audio_frames,glue_dispatch_miss_count(),
            (unsigned long long)(glue_frame_count()>g_benchmark_start?glue_frame_count()-g_benchmark_start:0),
            g_route_cpu>=0?(cpu_seconds()-g_route_cpu)*1000:0);
        fclose(f);
    }
#ifdef SMS_HAVE_SDL
    if(gpu_report){FILE *f=fopen(gpu_report,"w");if(!f)return 1;host_report(f);fclose(f);}
    if(gpu_dump&&!host_dump_gpu(gpu_dump)){fprintf(stderr,"GPU dump failed\n");return 1;}
    if (g_audio_live) host_audio_shutdown();
    if (want_window) host_shutdown();
#endif
    wav_close();
    if (psg_log) glue_psg_log_close();
#ifdef SMS_CYC_WATCH
    if (cyc_watch_file) glue_cyc_watch_close();
#endif

    {   extern bool g_quiet_exit;
        if (g_quiet_exit) fprintf(stderr,"[runner] game closed after %.1f minutes; dispatch misses: %d\n",
                                  (double)glue_frame_count() / 3600.0, glue_dispatch_miss_count());
        else fprintf(stderr,"[runner] stopped after %llu frames; dispatch misses: %d\n",
                     (unsigned long long)glue_frame_count(), glue_dispatch_miss_count()); }
    if (g_ts.out){
        if (g_ts.written) fprintf(stderr,"[runner] title shot -> %s (%s)\n", g_ts.out,
                                  g_ts.fixed ? "fixed frame" : "longest still picture");
        else fprintf(stderr,"[runner] title shot: no picture found, nothing written\n");
        return g_ts.written ? 0 : 1;
    }
    bool ok=!coverage_route_failed();
    if (report && !glue_write_report(report))ok=false;
    coverage_route_close();
    return ok?0:1;
}
