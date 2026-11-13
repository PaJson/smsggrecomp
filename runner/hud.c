/*
 * hud.c - in-game settings overlay (see hud.h). Self-contained: own 5x7 font, draws straight into
 * the ARGB framebuffer that the host presents. No SDL dependency (the host forwards keys).
 */
#include "hud.h"
#include <string.h>
#include <stdio.h>

/* ---- 5x7 font, ASCII 32..95 (lower case is drawn as upper case). Each glyph: 7 rows, bit 4 =
 * leftmost pixel. Drawn for this project (no external font data). */
static const uint8_t k_font[64][7] = {
    {0,0,0,0,0,0,0},                                  /* ' ' */
    {0x04,0x04,0x04,0x04,0x04,0x00,0x04},             /* ! */
    {0x0A,0x0A,0x00,0x00,0x00,0x00,0x00},             /* " */
    {0x0A,0x1F,0x0A,0x0A,0x0A,0x1F,0x0A},             /* # */
    {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04},             /* $ */
    {0x18,0x19,0x02,0x04,0x08,0x13,0x03},             /* % */
    {0x08,0x14,0x14,0x08,0x15,0x12,0x0D},             /* & */
    {0x04,0x04,0x00,0x00,0x00,0x00,0x00},             /* ' */
    {0x02,0x04,0x08,0x08,0x08,0x04,0x02},             /* ( */
    {0x08,0x04,0x02,0x02,0x02,0x04,0x08},             /* ) */
    {0x00,0x04,0x15,0x0E,0x15,0x04,0x00},             /* * */
    {0x00,0x04,0x04,0x1F,0x04,0x04,0x00},             /* + */
    {0x00,0x00,0x00,0x00,0x06,0x04,0x08},             /* , */
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00},             /* - */
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C},             /* . */
    {0x00,0x01,0x02,0x04,0x08,0x10,0x00},             /* / */
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},             /* 0 */
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},             /* 1 */
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},             /* 2 */
    {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},             /* 3 */
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},             /* 4 */
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},             /* 5 */
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},             /* 6 */
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},             /* 7 */
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},             /* 8 */
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},             /* 9 */
    {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00},             /* : */
    {0x00,0x0C,0x0C,0x00,0x0C,0x04,0x08},             /* ; */
    {0x02,0x04,0x08,0x10,0x08,0x04,0x02},             /* < */
    {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00},             /* = */
    {0x08,0x04,0x02,0x01,0x02,0x04,0x08},             /* > */
    {0x0E,0x11,0x01,0x02,0x04,0x00,0x04},             /* ? */
    {0x0E,0x11,0x01,0x0D,0x15,0x15,0x0E},             /* @ */
    {0x0E,0x11,0x11,0x11,0x1F,0x11,0x11},             /* A */
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},             /* B */
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},             /* C */
    {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C},             /* D */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},             /* E */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},             /* F */
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},             /* G */
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},             /* H */
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},             /* I */
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},             /* J */
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11},             /* K */
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},             /* L */
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},             /* M */
    {0x11,0x11,0x19,0x15,0x13,0x11,0x11},             /* N */
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},             /* O */
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},             /* P */
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},             /* Q */
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},             /* R */
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},             /* S */
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},             /* T */
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},             /* U */
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},             /* V */
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0A},             /* W */
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},             /* X */
    {0x11,0x11,0x11,0x0A,0x04,0x04,0x04},             /* Y */
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},             /* Z */
    {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E},             /* [ */
    {0x00,0x10,0x08,0x04,0x02,0x01,0x00},             /* \ */
    {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E},             /* ] */
    {0x04,0x0A,0x11,0x00,0x00,0x00,0x00},             /* ^ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x1F},             /* _ */
};

int hud_text(uint32_t *fb, int w, int h, int x, int y, const char *s, uint32_t argb){
    for (; *s; s++, x += 6){
        int c = (unsigned char)*s;
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c < 32 || c > 95) c = '?';
        const uint8_t *g = k_font[c - 32];
        for (int r = 0; r < 7; r++)
            for (int b = 0; b < 5; b++)
                if (g[r] & (0x10 >> b)){
                    int px = x + b, py = y + r;
                    if (px >= 0 && py >= 0 && px < w && py < h) fb[py * w + px] = argb;
                }
    }
    return x;
}

/* ---- state ---- */
static const char      *s_title = "SETTINGS";
static const HudOption *s_opts;
static int              s_nopts;
static int            (*s_info)(char lines[][41], int max);
static void           (*s_on_close)(void);
static bool             s_open;
static int              s_sel;          /* options page: -1 = tab row (only when pages exist) */
#define MAX_PAGES 4
static const char      *s_page_name[MAX_PAGES];
static HudPageDraw      s_page_draw[MAX_PAGES];
static void           (*s_page_key[MAX_PAGES])(int key);
static int              s_npages;       /* extra pages; page 0 = options */
static int              s_page;
static int            (*s_open_page)(void);

void hud_set_title(const char *t){ s_title = t ? t : "SETTINGS"; }
void hud_set_options(const HudOption *o, int n){ s_opts = o; s_nopts = n; if (s_sel >= n) s_sel = 0; }
void hud_set_info(int (*info)(char lines[][41], int max)){ s_info = info; }
void hud_set_on_close(void (*f)(void)){ s_on_close = f; }
bool hud_is_open(void){ return s_open; }
void hud_add_page(const char *name, HudPageDraw draw, void (*key)(int key)){
    if (s_npages >= MAX_PAGES || !draw) return;
    s_page_name[s_npages] = name; s_page_draw[s_npages] = draw; s_page_key[s_npages] = key; s_npages++;
}
void hud_set_open_page(int (*f)(void)){ s_open_page = f; }

static void page_step(int d){ s_page = (s_page + d + s_npages + 1) % (s_npages + 1); }

void hud_key(int key){
    if (key == HUD_KEY_TOGGLE || (key == HUD_KEY_CLOSE && s_open)){
        s_open = !s_open;
        if (!s_open && s_on_close) s_on_close();
        if (s_open && s_open_page){ int p = s_open_page(); if (p >= 0 && p <= s_npages) s_page = p; }
        return;
    }
    if (!s_open) return;
    if (key == HUD_KEY_PAGE_PREV || key == HUD_KEY_PAGE_NEXT){ if (s_npages) page_step(key == HUD_KEY_PAGE_NEXT ? 1 : -1); return; }
    if (s_page > 0){                                   /* an added page: L/R = page, U/D = the page's own */
        if (key == HUD_KEY_LEFT || key == HUD_KEY_RIGHT) page_step(key == HUD_KEY_RIGHT ? 1 : -1);
        else if ((key == HUD_KEY_UP || key == HUD_KEY_DOWN) && s_page_key[s_page - 1]) s_page_key[s_page - 1](key);
        return;
    }
    int lo = s_npages ? -1 : 0;                        /* -1 = the tab row */
    int n = s_nopts - lo;
    if (n <= 0) return;
    if (key == HUD_KEY_UP)   s_sel = (s_sel - lo + n - 1) % n + lo;
    if (key == HUD_KEY_DOWN) s_sel = (s_sel - lo + 1) % n + lo;
    if ((key == HUD_KEY_LEFT || key == HUD_KEY_RIGHT) && s_sel < 0){ page_step(key == HUD_KEY_RIGHT ? 1 : -1); return; }
    if ((key == HUD_KEY_LEFT || key == HUD_KEY_RIGHT) && s_nopts > 0){
        const HudOption *o = &s_opts[s_sel];
        if (!o->value || o->nchoices <= 0) return;
        int v = *o->value + (key == HUD_KEY_RIGHT ? 1 : -1);
        if (v < 0) v = 0;
        if (v >= o->nchoices) v = o->nchoices - 1;
        if (v != *o->value){ *o->value = v; if (o->changed) o->changed(); }
    }
}

static void fill(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1, uint32_t argb, bool blend){
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++){
        if (x < 0 || y < 0 || x >= w || y >= h) continue;
        uint32_t *p = &fb[y * w + x];
        if (blend){   /* 25% of the frame shows through */
            uint32_t s = *p;
            uint32_t r = (((s >> 16) & 0xFF) + 3 * ((argb >> 16) & 0xFF)) / 4;
            uint32_t g = (((s >> 8) & 0xFF) + 3 * ((argb >> 8) & 0xFF)) / 4;
            uint32_t b = ((s & 0xFF) + 3 * (argb & 0xFF)) / 4;
            *p = 0xFF000000u | (r << 16) | (g << 8) | b;
        } else *p = argb;
    }
}

void hud_fill(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1, uint32_t argb, bool blend){
    fill(fb, w, h, x0, y0, x1, y1, argb, blend);
}

void hud_draw(uint32_t *fb, int w, int h){
    if (!s_open) return;
    const uint32_t WHITE = 0xFFFFFFFF, GREY = 0xFFA0A0B0, YEL = 0xFFFFE040, CYAN = 0xFF60D0FF;
    char info[8][41]; int ninfo = (s_page == 0 && s_info) ? s_info(info, 8) : 0;
    int rows = 2 + (s_npages ? 1 : 0) + (s_nopts > 0 ? s_nopts : 1) + (ninfo ? ninfo + 1 : 0) + 2;
    int x0 = 8, x1 = w - 8, y0 = 8, y1 = y0 + rows * 10 + 6;
    if (s_page > 0 || y1 > h - 4) y1 = h - 4;          /* added pages use the full height */
    fill(fb, w, h, x0, y0, x1, y1, 0xFF101830, true);
    fill(fb, w, h, x0, y0, x1, y0 + 1, CYAN, false); fill(fb, w, h, x0, y1 - 1, x1, y1, CYAN, false);
    fill(fb, w, h, x0, y0, x0 + 1, y1, CYAN, false); fill(fb, w, h, x1 - 1, y0, x1, y1, CYAN, false);
    int y = y0 + 5;
    hud_text(fb, w, h, x0 + 6, y, s_title, CYAN); y += 14;
    if (s_npages){                                       /* tab row: OPTIONS | page names */
        int x = x0 + 12;
        bool on_tabs = (s_page > 0) || s_sel < 0;
        if (s_page == 0 && s_sel < 0) hud_text(fb, w, h, x0 + 4, y, ">", YEL);
        for (int p = 0; p <= s_npages; p++){
            const char *nm = p ? s_page_name[p - 1] : "OPTIONS";
            uint32_t c = (p == s_page) ? (on_tabs ? YEL : WHITE) : GREY;
            if (p == s_page) fill(fb, w, h, x - 2, y + 8, x + 6 * (int)strlen(nm), y + 9, c, false);
            x = hud_text(fb, w, h, x, y, nm, c) + 10;
        }
        y += 14;
    }
    if (s_page > 0){
        s_page_draw[s_page - 1](fb, w, h, x0 + 4, y, x1 - 4, y1 - 14);
        hud_text(fb, w, h, x0 + 6, y1 - 12, "LEFT/RIGHT PAGE   F1/BACK CLOSE", GREY);
        return;
    }
    if (s_nopts <= 0){ hud_text(fb, w, h, x0 + 6, y, "NO OPTIONS FOR THIS GAME", GREY); y += 10; }
    for (int i = 0; i < s_nopts; i++, y += 10){
        const HudOption *o = &s_opts[i];
        uint32_t c = (i == s_sel) ? YEL : WHITE;
        if (i == s_sel) hud_text(fb, w, h, x0 + 4, y, ">", YEL);
        hud_text(fb, w, h, x0 + 12, y, o->label, c);
        if (o->value && o->nchoices > 0){
            int v = *o->value; if (v < 0) v = 0; if (v >= o->nchoices) v = o->nchoices - 1;
            char buf[48]; snprintf(buf, sizeof buf, "%s%s%s", v > 0 ? "< " : "  ", o->choices[v], v < o->nchoices - 1 ? " >" : "");
            hud_text(fb, w, h, x1 - 8 - 6 * (int)strlen(buf), y, buf, c);
        }
    }
    if (ninfo){
        y += 4;
        for (int i = 0; i < ninfo; i++, y += 10) hud_text(fb, w, h, x0 + 12, y, info[i], GREY);
    }
    hud_text(fb, w, h, x0 + 6, y1 - 12, s_sel < 0 ? "LEFT/RIGHT PAGE   UP/DOWN SELECT"
                                                 : "UP/DOWN SELECT   LEFT/RIGHT CHANGE", GREY);
}
