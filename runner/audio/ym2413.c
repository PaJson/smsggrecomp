/* ym2413.c - see ym2413.h. */
#include "ym2413.h"
#include "emu2413.h"
#include "sms_clocks.h"
#include "sn76489.h"
#include <stdlib.h>
#include <stdio.h>
extern uint16_t g_dbg_pc;

#define OPLL_CLOCK      SMS_Z80_HZ              /* the FM unit shares the Z80 clock */
#define OPLL_DIVIDER    72                      /* one OPLL sample per 72 clocks */
#define FM_GAIN_NUM     3                       /* output gain relative to emu2413's scale */
#define FM_GAIN_DEN     2

static OPLL    *s_opll;
static uint8_t  s_addr_latch;
static uint8_t  s_ctrl;                         /* $F2 */
static uint32_t s_phase;                        /* Z80 clocks since the newest OPLL sample */
static int32_t  s_prev, s_cur;
static unsigned s_gain[14] = { 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256 };
static int      s_gain_set;                     /* any gain != 256: mix per channel (OPLL_calcStereo) */

static void apply_gains(void)
{
    s_gain_set = 0;
    for (unsigned i = 0; i < 14; i++) {
        float g[2]; g[0] = g[1] = s_gain[i] / 256.0f;
        if (s_opll) OPLL_setPanFine(s_opll, i, g);
        if (s_gain[i] != 256) s_gain_set = 1;
    }
}

void fm_set_channel_gain(unsigned ch, unsigned gain)
{
    if (ch >= 14) return;
    if (gain > 256) gain = 256;
    if (s_gain[ch] == gain) return;
    s_gain[ch] = gain;
    apply_gains();
}

void fm_reset(void)
{
    if (!s_opll) s_opll = OPLL_new(OPLL_CLOCK, OPLL_CLOCK / OPLL_DIVIDER);
    if (!s_opll) return;
    OPLL_reset(s_opll);
    apply_gains();                              /* OPLL_reset clears the per-channel pans */
    s_addr_latch = 0; s_ctrl = 0; s_phase = 0; s_prev = s_cur = 0;
    psg_set_aux_mixer(fm_mix);
}

void fm_port_write(uint8_t port, uint8_t value)
{
    if (!s_opll) fm_reset();
    if (!s_opll) return;
    {   /* diagnostic: SMS_FMLOG=<file> logs every FM register write (reg=value) */
        static FILE *fl; static int init;
        if (!init) { init = 1; const char *e = getenv("SMS_FMLOG"); if (e) fl = fopen(e, "w"); }
        if (fl && (port & 3) == 1) fprintf(fl, "%02X=%02X pc=%04X", s_addr_latch, value, g_dbg_pc), fputc(10, fl);
        else if (fl && (port & 3) == 2) fprintf(fl, "F2=%02X", value), fputc(10, fl);
    }
    switch (port & 3) {
        case 0: s_addr_latch = value; OPLL_writeIO(s_opll, 0, value); break;
        case 1: OPLL_writeIO(s_opll, 1, value); break;
        default: s_ctrl = value; break;
    }
}

uint8_t fm_port_read(uint8_t port)
{
    (void)port;
    return s_ctrl;                              /* detection: the unit reads back what was written */
}

void fm_mix(int16_t *frames, size_t n)
{
    if (!s_opll) return;
    const int psg_off = (s_ctrl & 2) != 0;
    const int fm_on   = (s_ctrl & 1) != 0;
    for (size_t i = 0; i < n; i++) {
        int32_t l = frames[i * 2], r = frames[i * 2 + 1];
        if (psg_off) l = r = 0;
        if (fm_on) {
            s_phase += 16;                      /* one PSG frame = 16 Z80 clocks */
            while (s_phase >= OPLL_DIVIDER) {
                s_phase -= OPLL_DIVIDER;
                s_prev = s_cur;
                if (s_gain_set) { int32_t o[2]; OPLL_calcStereo(s_opll, o); s_cur = o[0]; }
                else s_cur = OPLL_calc(s_opll);
            }
            int32_t s = s_prev + (int32_t)(((int64_t)(s_cur - s_prev) * (int32_t)s_phase) / OPLL_DIVIDER);
            s = s * FM_GAIN_NUM / FM_GAIN_DEN;
            l += s; r += s;
        }
        if (l > 32767) l = 32767; else if (l < -32768) l = -32768;
        if (r > 32767) r = 32767; else if (r < -32768) r = -32768;
        frames[i * 2] = (int16_t)l; frames[i * 2 + 1] = (int16_t)r;
    }
}
