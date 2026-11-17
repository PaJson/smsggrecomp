/*
 * glue.h - runner core entry points used by main.c.
 */
#ifndef GLUE_H
#define GLUE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Load a .sms/.gg image into the runner's ROM space. Returns false on error. */
bool     glue_load_rom(const char *path);

/* Writable view of the loaded ROM image (for per-game data patches applied in on_post_reset). */
uint8_t *glue_rom_image(size_t *size);
/* Append a 16 KB bank (e.g. another ROM's sound driver bank) to the ROM image; returns its bank
 * number (= old bank count), or -1. Call it before anything caches glue_rom_image() (the image is
 * reallocated). Code in such an added bank is not recompiled: calls into it (slot 2) run in the
 * hybrid interpreter silently (no dispatch-miss log, no manifest seed). */
int      glue_add_rom_bank(const uint8_t *bank16k);
int      glue_native_banks(void);                 /* banks of the loaded ROM itself */
const char *glue_rom_path(void);                  /* path of the loaded ROM (as given) */

/* Write cartridge (battery) RAM to <rom>.sav if it changed. */
void     glue_sram_flush(void);
/* Read-only view of the cartridge RAM (32 KB: page 0 then page 1), and the path of the .sav file
 * ("" when saving is off, e.g. SMS_NO_SAVE) - for game hooks that keep data next to the save. */
const uint8_t *glue_sram_image(size_t *size);
const char    *glue_save_path(void);
/* A game hook writes one byte of the cartridge RAM (offset into the 32 KB image), as the game
 * would: marks the save changed (written by glue_sram_flush, the next unmap, or at exit). */
void     glue_sram_poke(size_t off, uint8_t v);

/* Initialise bus/VDP/CPU for a fresh run. frame_limit==0 means run forever. */
void     glue_init(bool is_gg, uint64_t frame_limit);
bool     glue_is_gg(void);

/* Enter the game at the reset vector. Returns when the frame limit is hit
 * (the reset routine itself never returns; a longjmp unwinds out of it). */
void     glue_run(void);

/* Oracle reference run: execute the WHOLE game under the vendored superzazu
 * Z80 interpreter (instead of the recompiled code) over the SAME bus / VDP /
 * IO / timing. Used dev-only to diff against the recompiled run: because both
 * share the clean-room renderer, a renderer bug shows in both while a recomp
 * bug shows only in glue_run(). Frame dump / live callback / frame limit all
 * work identically. */
void     glue_run_interp(void);

/* Dump frame `frame` to `path` (PNG) when it completes. */
void     glue_set_dump(uint64_t frame, const char *path);

/* Enable a per-frame VDP-state trace (CSV: frame, FNV hashes of VRAM/CRAM/regs,
 * plus r8/r9/r0/r1). Always-on from frame 0; identical in glue_run() and
 * glue_run_interp(). Diffing a recomp trace against an --interp trace finds the
 * first frame whose VDP STATE diverges -> renderer-vs-recomp discriminator.
 * Pass NULL to disable. */
void     glue_set_vdp_trace(const char *path);
void     glue_set_psg_log(const char *path);   /* chip_ring: (cyc,val) PSG-write log */
void     glue_psg_log_close(void);

#ifdef SMS_CYC_WATCH
/* Cycle-watch oracle: record (hit_index, g_z80.cyc) each time `addr` is reached
 * (sampled in the SMS_PC hook). The offset-independent per-anchor cycle DELTA
 * is diffed against Mesen's exec-callback cycleCount (tools/oracle). */
void     glue_set_cyc_watch(uint16_t addr, const char *path);
void     glue_cyc_watch_close(void);
#endif

/* Register a live per-frame callback (e.g. the SDL host). When set, every
 * completed frame is rendered to the framebuffer and passed to `cb` as a
 * 256x192 ARGB8888 image. `cb` returns nonzero to request shutdown (the run
 * loop then unwinds cleanly, as on the frame limit). Keeps glue.c host-agnostic. */
void     glue_set_frame_callback(int (*cb)(const uint32_t *fb, int w, int h));
/* Job consumers may decline pixel materialization. Their callback receives
 * NULL pixels and consumes vdp_frame_jobs() synchronously instead. */
void     glue_set_native_output(bool needed);

/* Controller buttons (1 = pressed). The D-pad + two face buttons map to the
 * SMS controller ports $DC/$DD; START is the Game Gear's port-$00 bit-7 button.
 * PAUSE is the SMS console's Pause button: not a controller bit on the hardware but
 * an NMI (vector $0066), raised once when the bit goes from released to pressed
 * (SMS only; ignored on the Game Gear). */
enum {
    SMS_PAD_UP    = 0x01, SMS_PAD_DOWN  = 0x02,
    SMS_PAD_LEFT  = 0x04, SMS_PAD_RIGHT = 0x08,
    SMS_PAD_B1    = 0x10, SMS_PAD_B2    = 0x20,
    SMS_PAD_START = 0x40, SMS_PAD_PAUSE = 0x80
};

/* Set the live controller state for player 1 / 2 (an SMS_PAD_* bitmask). The
 * SDL host refreshes these from the keyboard each frame; headless runs leave
 * them 0, so the ports read idle. */
void     glue_set_pad1(uint8_t pressed);
void     glue_set_pad2(uint8_t pressed);

/* Headless scripted input: when set, the runner calls this once per completed
 * frame to fetch player-1's mask for the upcoming frame — deterministic input
 * for automated tests, no window required. The SDL host uses glue_set_pad1()
 * instead. Pass NULL to disable. */
void     glue_set_input_cb(uint8_t (*cb)(uint64_t frame));

/* Register an audio sink. When set, the PSG is synthesised in lockstep with
 * the VDP (on every run path) and each completed video frame's output is
 * handed to `sink` as interleaved-stereo S16 frames (L,R per frame) at
 * glue_audio_sample_rate() Hz. Pass NULL to disable (the default: headless
 * oracle/diff runs synthesise nothing). */
void     glue_set_audio_sink(void (*sink)(const int16_t *stereo_frames, size_t frame_count));
uint32_t glue_audio_sample_rate(void);

/* The emulated frame rate in Hz (NTSC: Z80 clock / cycles-per-frame ≈ 59.92).
 * The SDL host uses it to pace the window to realtime independent of the
 * monitor's refresh. */
double   glue_frame_rate(void);

uint64_t glue_frame_count(void);
int      glue_dispatch_miss_count(void);
size_t   glue_rom_size(void);
uint32_t glue_rom_crc32(void);

/* Deterministic boundary observer, after frame completion and scripted input,
 * before the next guest instruction. Nonzero requests a clean run stop. */
void glue_set_boundary_callback(int (*cb)(uint64_t frame));
bool glue_write_report(const char *path);
/* Native viewport PNG plus WRAM/VRAM/CRAM/register planes and execution JSON. */
bool glue_capture(const char *prefix);

#endif /* GLUE_H */
