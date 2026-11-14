/*
 * function_finder.h — static reachability analysis for the SMS/GG recompiler.
 *
 * Seeds from the fixed Z80 vectors (reset/RST/IRQ/NMI) plus game.toml extras
 * and jump tables, then walks reachable code: every CALL/JP/JR target that is
 * statically known and lands in ROM becomes a candidate function or internal
 * label. Function boundaries are where a CALL points; internal labels are
 * intra-function jump targets. (See PRINCIPLES #16: ROM is ground truth; a
 * wrong start address on a variable-width ISA decodes to garbage.)
 *
 * NOTE on banking: addresses in the paged region ($4000-$BFFF) are resolved
 * against the bank the function finder believes is mapped there (default
 * identity for the fixed region and the reset-time mapping 0/1/2). Per-call
 * bank context is a known limitation tracked for the post-ROM phase; for the
 * fixed-bank-heavy Sonic engines the reset mapping covers the bulk.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "rom_parser.h"

typedef enum {
    FUNC_SRC_VECTOR  = 1 << 0,  /* reset / RST / IRQ / NMI */
    FUNC_SRC_CALL    = 1 << 1,  /* CALL target */
    FUNC_SRC_JUMP    = 1 << 2,  /* JP/JR target outside the current function */
    FUNC_SRC_CONFIG  = 1 << 3,  /* game.toml [functions].extra */
    FUNC_SRC_TABLE   = 1 << 4,  /* jump-table entry */
    FUNC_SRC_MANIFEST= 1 << 5,  /* runtime dispatch-manifest seed (ROM-CRC verified) */
} FuncSource;

typedef struct {
    uint16_t addr;        /* Z80 entry address */
    int      bank;        /* bank mapped into addr's slot (>=0), or -1 = fixed/identity */
    char     name[32];    /* func_BBBB or configured name */
    uint8_t  source;      /* OR of FuncSource */
    bool     is_entry;    /* standalone function (vs internal label) */
} FuncEntry;

typedef struct {
    FuncEntry *items;
    int        count;
    int        cap;
} FuncList;

/* Address marked as data (not code) — bytes here are never decoded. */
typedef struct { uint16_t addr; int bank; } DataAddr;

void funclist_init(FuncList *fl);
void funclist_free(FuncList *fl);
/* Add or merge a function entry; returns the index. */
int  funclist_add(FuncList *fl, uint16_t addr, int bank, const char *name, uint8_t source, bool is_entry);
int  funclist_find(const FuncList *fl, uint16_t addr, int bank);
/* Visible boundaries for a trace's physical ROM mapping; limit is a snapshot
 * of fl->count and also the output capacity. Other-bank aliases never cut code. */
int ff_visible_entries(const FuncList *fl,uint16_t start,int bank,uint16_t *out,int limit);

/* Seed list with vectors + configured extras, then walk reachable code. The
 * blacklist marks promoted-but-not-code addresses to drop. */
void ff_seed_vectors(FuncList *fl);
void ff_discover(const SmsRom *rom, FuncList *fl,
                 const uint16_t *blacklist, int blacklist_count);

/* ---- per-function trace (shared by finder and code generator) ---- */
#include "z80_decoder.h"

typedef struct { uint16_t addr; Z80Insn insn; } TracedInsn;

typedef struct {
    uint16_t    start;
    TracedInsn *insns;  int insn_count, insn_cap;
    uint16_t   *labels; int label_count, label_cap;   /* internal jump targets */
    bool        truncated;                              /* hit unmapped/illegal */
    const SmsRom *rom; int bank;                        /* set by trace_function (for look-ahead at jump targets) */
    bool        complete;                               /* tracing finished, insns sorted by address */
} TraceResult;

/* Trace one function body from `start`, following fall-through and intra-
 * function jumps. `entries` (sorted addrs) are treated as external (tail
 * calls / not followed). Caller frees with trace_free(). */
void trace_function(const SmsRom *rom, uint16_t start, int bank,
                    const uint16_t *entries, int entry_count, TraceResult *out);
void trace_free(TraceResult *t);
/* Is `addr` an internal label of this trace (needs a C label emitted)? */
bool trace_is_label(const TraceResult *t, uint16_t addr);

/* The Z80 has no `call (hl)`, so games synthesize it as `ld rr,ret ; push rr ;
 * jp (hl)` - a computed CALL that returns to `ret`. If the instruction at
 * `jp_addr` is such a computed jump and the two preceding instructions form
 * that idiom, return true and set *cont_addr to the pushed return address (the
 * continuation). Used by both the tracer (to follow the continuation) and the
 * code generator (to emit `call_by_address(...); goto L_cont;`). */
bool trace_computed_call(const TraceResult *t, uint16_t jp_addr, uint16_t *cont_addr);
/* Same, also returning the address of the `push` that put the continuation on the stack
 * (callers use it to detect a path-dependent push: a label between push and jp means some
 * paths reach the jp WITHOUT the push, e.g. Golvellius `$3D50 jr z,$3D60` skipping `$3D59 push bc`). */
bool trace_computed_call_push(const TraceResult *t, uint16_t jp_addr, uint16_t *cont_addr, uint16_t *push_addr);

/* Trampoline call: `ld rr,cont ; ... ; jp T` where the code at T begins with `push rr`
 * (a shared trampoline that pushes the caller-supplied continuation, e.g. Phantasy
 * Star's `ld hl,+ ; jp $851C`). Semantically a call to T whose `ret` resumes at cont.
 * Returns true and sets *cont_addr. Unlike trace_computed_call nothing is pushed
 * before the jp, so the post-return SP equals the SP at the jp. */
bool trace_trampoline_call(const TraceResult *t, uint16_t jp_addr, uint16_t *cont_addr);

/* Non-repeating block instructions LDI/LDD/INI/IND/OUTI/OUTD (ED A0 A8 A2 AA A3 AB). */
static inline bool z80_is_blockio_single(const Z80Insn *in){
    if (in->prefix!=Z80_PFX_ED) return false;
    switch (in->opcode){ case 0xA0: case 0xA8: case 0xA2: case 0xAA: case 0xA3: case 0xAB: return true; default: return false; }
}

/* `push rr ; ... ; ret`: a plain RET with an unmatched push in its block is a computed JUMP to the
 * pushed address. Returns true for that idiom; *has_cont is set (and *cont_addr valid) when a second
 * pushed `ld rr,nn16` continuation sits below it (a computed call whose target `ret`s to cont). */
bool trace_ret_jump(const TraceResult *t, uint16_t ret_addr, uint16_t *cont_addr, bool *has_cont);
/* The pushed target of such a RET when it is a constant (`ld rr,nn16 ; push rr`); discovery
 * registers it as a function entry. */
bool trace_ret_jump_target(const TraceResult *t, uint16_t ret_addr, uint16_t *tgt_addr);
/* Address of the `push` of the jump target (to detect a path-dependent push, see code_generator). */
bool trace_ret_jump_push(const TraceResult *t, uint16_t ret_addr, uint16_t *push_addr);

/* `ld rr,cont ; push rr ; call F` where F drops its own return address (`pop hl ; ret`) so
 * its RET resumes at `cont` (Phantasy Star LABEL_787B). After the call, SP == pre-call SP + 2
 * means exactly that: execution continues at cont in this function. */
bool trace_push_call(const TraceResult *t, uint16_t call_addr, uint16_t *cont_addr);

/* ---- per-slot bank tracking (shared by discovery + codegen) -------------- *
 * Linear (address-order) inference of which ROM bank is mapped into each 16 KB
 * slot, by recognising the `ld a,#n ; ld ($FFFD/$FFFE/$FFFF),a` mapper idiom.
 * The SMS Sega mapper has three switchable slots: $0000-$3FFF ($FFFD),
 * $4000-$7FFF ($FFFE), $8000-$BFFF ($FFFF). Tracking all three (not just
 * slot 2) is required: slot-1 code-banking is real (e.g. bank 3 -> $4000). */
typedef struct {
    int  slot[3];      /* current bank in slots 0/1/2 (valid iff known[slot]) */
    bool known[3];     /* slot bank statically established (else defer targets) */
    int  a_known;      /* statically-known value of register A (0..255), or -1 *
                        * if A's value is not statically determinable here.     */
    int  hl_known;     /* HL as set by an immediately preceding `ld hl,nn`, or -1.
                        * Only used for `ld hl,$FFFx ; ld (hl),n|a` mapper writes. */
} BankState;

void bankstate_init(BankState *bs, uint16_t entry, int entry_bank);
/* Step one instruction (cur), with its address-order predecessor (prev may be
 * NULL): updates a slot bank when cur completes the mapper idiom. */
void bankstate_step(BankState *bs, const TracedInsn *cur, const TracedInsn *prev);
/* Bank that a static target T should be discovered/named under. Writes the
 * funclist bank field (-1 = fixed default mapping) to *out_bank; returns false
 * if T is in RAM or is a slot-2 target with an unknown bank (defer to runtime).
 * For trace_function: the ROM bank for an address in the entry's own slot is
 * the entry bank; other slots use the default reset mapping (slot i = bank i). */
bool bankstate_target(const BankState *bs, uint16_t T, int *out_bank);
