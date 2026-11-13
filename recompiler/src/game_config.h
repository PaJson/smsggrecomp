/*
 * game_config.h — parsed game.toml for the SMS/GG recompiler.
 * Schema: [game] output_prefix/platform/rom/crc32, [ram_layout] (optional
 * addresses), [mapper] kind, [functions] extra[]/blacklist[].
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "rom_parser.h"

#define GC_MAX_FUNCS 8192
#define GC_ADDR_UNSET 0xFFFFFFFFu

typedef struct {
    char        output_prefix[64];
    char        display_name[96];   /* optional [game].display_name (window title); empty -> output_prefix */
    char        rom_path[260];      /* as written in game.toml (relative) */
    SmsPlatform platform;
    bool        have_crc;
    uint32_t    crc32;
    char        mapper_kind[16];

    /* [hooks] - names of per-game C functions (defined in the game dir) wired into
     * g_game_spec: `void f(void)` for on_post_reset, `void f(uint64_t frame)` for
     * on_frame_pre/on_frame_post. Empty = not set. */
    char        hook_on_post_reset[64];
    char        hook_on_frame_pre[64];
    char        hook_on_frame_post[64];

    /* [ram_layout] — GC_ADDR_UNSET if absent */
    uint32_t    game_mode_addr;
    uint32_t    vblank_count_addr;
    uint32_t    player_object_addr;

    uint16_t    extra[GC_MAX_FUNCS];     int extra_count;
    uint16_t    banked[GC_MAX_FUNCS];    int banked_count;   /* flattened (addr,bank) pairs: [functions] banked = [0x67E6, 29, ...] */
    uint16_t    blacklist[GC_MAX_FUNCS]; int blacklist_count;

    /* [jump_tables] - computed-jump targets the static finder can't resolve:
     * an array of 16-bit LE pointer tables read straight from ROM. Parallel
     * arrays addr/bank/count (bank = the bank mapped into the table's slot, and
     * the bank its targets run under; -1 = fixed). */
    uint16_t    jt_addr[64];
    int         jt_bank[64];
    int         jt_count[64];
    int         jt_page[64];      /* -1 = 16-bit pointer table; else byte table, target = (page<<8)|byte */
    int         jt_n;

    /* [insn_hooks] - per-game C hooks at single instructions (the recompiled equivalent of a ROM
     * patch trampoline): parallel arrays addr / func / resume (/ bank, -1 = any). When execution
     * reaches `addr`, `int func(void)` runs first; if it returns nonzero, execution continues at
     * `resume` (the original instructions in between are skipped), else the original instruction
     * runs. The hook reads/writes the Z80 state (g_z80) and memory itself. */
    uint16_t    ih_addr[64];
    uint16_t    ih_resume[64];
    int         ih_bank[64];
    char        ih_func[64][64];
    int         ih_n;
} GameConfig;

/* Load + validate. Returns false (with a message on stderr) on hard errors
 * (missing file, missing output_prefix/rom). */
bool game_config_load(const char *path, GameConfig *out);
