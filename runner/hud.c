/*
 * hud.c - in-game settings overlay (see hud.h). Self-contained: own 5x7 font, draws straight into
 * the ARGB framebuffer that the host presents. No SDL dependency (the host forwards keys).
 */
#include "hud.h"
#include <string.h>
#include <stdio.h>

/* ---- 5x7 font, ASCII 32..126 (lower case with descenders in an 8th row). Each glyph: 8 rows, bit 4 =
 * leftmost pixel. Drawn for this project (no external font data). */
static const uint8_t k_font[96][8] = {
    {0,0,0,0,0,0,0,0},                                  /* ' ' */
    {0x04,0x04,0x04,0x04,0x04,0x00,0x04,0x00},             /* ! */
    {0x0A,0x0A,0x00,0x00,0x00,0x00,0x00,0x00},             /* " */
    {0x0A,0x1F,0x0A,0x0A,0x0A,0x1F,0x0A,0x00},             /* # */
    {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04,0x00},             /* $ */
    {0x18,0x19,0x02,0x04,0x08,0x13,0x03,0x00},             /* % */
    {0x08,0x14,0x14,0x08,0x15,0x12,0x0D,0x00},             /* & */
    {0x04,0x04,0x00,0x00,0x00,0x00,0x00,0x00},             /* ' */
    {0x02,0x04,0x08,0x08,0x08,0x04,0x02,0x00},             /* ( */
    {0x08,0x04,0x02,0x02,0x02,0x04,0x08,0x00},             /* ) */
    {0x00,0x04,0x15,0x0E,0x15,0x04,0x00,0x00},             /* * */
    {0x00,0x04,0x04,0x1F,0x04,0x04,0x00,0x00},             /* + */
    {0x00,0x00,0x00,0x00,0x06,0x04,0x08,0x00},             /* , */
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00,0x00},             /* - */
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00},             /* . */
    {0x00,0x01,0x02,0x04,0x08,0x10,0x00,0x00},             /* / */
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E,0x00},             /* 0 */
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E,0x00},             /* 1 */
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F,0x00},             /* 2 */
    {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E,0x00},             /* 3 */
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02,0x00},             /* 4 */
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E,0x00},             /* 5 */
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E,0x00},             /* 6 */
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08,0x00},             /* 7 */
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E,0x00},             /* 8 */
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C,0x00},             /* 9 */
    {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00,0x00},             /* : */
    {0x00,0x0C,0x0C,0x00,0x0C,0x04,0x08,0x00},             /* ; */
    {0x02,0x04,0x08,0x10,0x08,0x04,0x02,0x00},             /* < */
    {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00,0x00},             /* = */
    {0x08,0x04,0x02,0x01,0x02,0x04,0x08,0x00},             /* > */
    {0x0E,0x11,0x01,0x02,0x04,0x00,0x04,0x00},             /* ? */
    {0x0E,0x11,0x01,0x0D,0x15,0x15,0x0E,0x00},             /* @ */
    {0x0E,0x11,0x11,0x11,0x1F,0x11,0x11,0x00},             /* A */
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E,0x00},             /* B */
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E,0x00},             /* C */
    {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C,0x00},             /* D */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F,0x00},             /* E */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10,0x00},             /* F */
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F,0x00},             /* G */
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11,0x00},             /* H */
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E,0x00},             /* I */
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C,0x00},             /* J */
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11,0x00},             /* K */
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F,0x00},             /* L */
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11,0x00},             /* M */
    {0x11,0x11,0x19,0x15,0x13,0x11,0x11,0x00},             /* N */
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E,0x00},             /* O */
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10,0x00},             /* P */
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D,0x00},             /* Q */
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11,0x00},             /* R */
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E,0x00},             /* S */
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04,0x00},             /* T */
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E,0x00},             /* U */
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04,0x00},             /* V */
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0A,0x00},             /* W */
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11,0x00},             /* X */
    {0x11,0x11,0x11,0x0A,0x04,0x04,0x04,0x00},             /* Y */
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F,0x00},             /* Z */
    {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E,0x00},             /* [ */
    {0x00,0x10,0x08,0x04,0x02,0x01,0x00,0x00},             /* \ */
    {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E,0x00},             /* ] */
    {0x04,0x0A,0x11,0x00,0x00,0x00,0x00,0x00},             /* ^ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x1F,0x00},             /* _ */
    {0x08,0x04,0x00,0x00,0x00,0x00,0x00,0x00},              /* ` */
    {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F,0x00},              /* a */
    {0x10,0x10,0x16,0x19,0x11,0x11,0x1E,0x00},              /* b */
    {0x00,0x00,0x0E,0x10,0x10,0x11,0x0E,0x00},              /* c */
    {0x01,0x01,0x0D,0x13,0x11,0x11,0x0F,0x00},              /* d */
    {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E,0x00},              /* e */
    {0x06,0x09,0x08,0x1C,0x08,0x08,0x08,0x00},              /* f */
    {0x00,0x00,0x0F,0x11,0x11,0x0F,0x01,0x0E},              /* g */
    {0x10,0x10,0x16,0x19,0x11,0x11,0x11,0x00},              /* h */
    {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E,0x00},              /* i */
    {0x02,0x00,0x06,0x02,0x02,0x02,0x12,0x0C},              /* j */
    {0x10,0x10,0x12,0x14,0x18,0x14,0x12,0x00},              /* k */
    {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E,0x00},              /* l */
    {0x00,0x00,0x1A,0x15,0x15,0x11,0x11,0x00},              /* m */
    {0x00,0x00,0x16,0x19,0x11,0x11,0x11,0x00},              /* n */
    {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E,0x00},              /* o */
    {0x00,0x00,0x1E,0x11,0x11,0x1E,0x10,0x10},              /* p */
    {0x00,0x00,0x0F,0x11,0x11,0x0F,0x01,0x01},              /* q */
    {0x00,0x00,0x16,0x19,0x10,0x10,0x10,0x00},              /* r */
    {0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E,0x00},              /* s */
    {0x08,0x08,0x1C,0x08,0x08,0x09,0x06,0x00},              /* t */
    {0x00,0x00,0x11,0x11,0x11,0x13,0x0D,0x00},              /* u */
    {0x00,0x00,0x11,0x11,0x11,0x0A,0x04,0x00},              /* v */
    {0x00,0x00,0x11,0x11,0x15,0x15,0x0A,0x00},              /* w */
    {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11,0x00},              /* x */
    {0x00,0x00,0x11,0x11,0x11,0x0F,0x01,0x0E},              /* y */
    {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F,0x00},              /* z */
    {0x02,0x04,0x04,0x08,0x04,0x04,0x02,0x00},              /* { */
    {0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x00},              /* | */
    {0x08,0x04,0x04,0x02,0x04,0x04,0x08,0x00},              /* } */
    {0x00,0x00,0x08,0x15,0x02,0x00,0x00,0x00},              /* ~ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},              /* DEL */
};

int hud_text(uint32_t *fb, int w, int h, int x, int y, const char *s, uint32_t argb){
    for (; *s; s++, x += 6){
        int c = (unsigned char)*s;
        if (c < 32 || c > 127) c = '?';
        const uint8_t *g = k_font[c - 32];
        for (int r = 0; r < 8; r++)
            for (int b = 0; b < 5; b++)
                if (g[r] & (0x10 >> b)){
                    int px = x + b, py = y + r;
                    if (px >= 0 && py >= 0 && px < w && py < h) fb[py * w + px] = argb;
                }
    }
    return x;
}

int hud_text_scaled(uint32_t *fb, int w, int h, int x, int y, const char *s, uint32_t argb, int k){
    if (k <= 1) return hud_text(fb, w, h, x, y, s, argb);
    for (; *s; s++, x += 6 * k){
        int c = (unsigned char)*s;
        if (c < 32 || c > 127) c = '?';
        const uint8_t *g = k_font[c - 32];
        for (int r = 0; r < 8 * k; r++)
            for (int b = 0; b < 5 * k; b++)
                if (g[r / k] & (0x10 >> (b / k))){
                    int px = x + b, py = y + r;
                    if (px >= 0 && py >= 0 && px < w && py < h) fb[py * w + px] = argb;
                }
    }
    return x;
}

/* ---- side panel ---- */
static HudPanelDraw s_panel_draw;
static bool         s_panel_on;
void hud_set_panel(HudPanelDraw draw){ s_panel_draw = draw; }
void hud_set_panel_enabled(bool on){ s_panel_on = on; }
bool hud_panel_active(void){ return s_panel_draw && s_panel_on; }
static void (*s_panel_cb)(bool);
static bool s_start_free;
void hud_set_on_panel_toggle(void (*cb)(bool on)){ s_panel_cb = cb; }
void hud_toggle_panel(void){
    if (!s_panel_draw) return;
    s_panel_on = !s_panel_on;
    if (s_panel_cb) s_panel_cb(s_panel_on);
}
void hud_set_start_free(bool free){ s_start_free = free; }
bool hud_start_opens(void){ return s_start_free && s_panel_draw; }
void hud_panel_render(uint32_t *buf, int w, int h){
    if (s_panel_draw) s_panel_draw(buf, w, h);
    else for (int i = 0; i < w * h; i++) buf[i] = 0xFF000000u;
}

/* ---- high-resolution layers (drawn by the SDL host) ---- */
#define MAX_LAYERS 64
static HudLayer s_layers[MAX_LAYERS];
static int      s_nlayers;
static bool     s_hires;
bool hud_hires(void){ return s_hires; }
void hud_set_hires(bool on){ s_hires = on; }

static double s_turbo = 4.0;
void   hud_set_turbo_speed(double mult){ s_turbo = (mult > 0.05) ? mult : 4.0; }
double hud_turbo_speed(void){ return s_turbo; }

static uint8_t s_extra;
uint8_t hud_extra_buttons(void){ return s_extra; }
void    hud_set_extra_buttons(uint8_t mask){ s_extra = mask; }
void hud_image(int space, float x, float y, float w, float h, const uint32_t *img, int iw, int ih,
               int sx, int sy, int sw, int sh, bool wrap, uint32_t gen){
    if (!s_hires || !img || s_nlayers >= MAX_LAYERS || iw <= 0 || ih <= 0 || sw <= 0 || sh <= 0) return;
    HudLayer *l = &s_layers[s_nlayers++];
    memset(l, 0, sizeof *l);
    l->space = space; l->x = x; l->y = y; l->w = w; l->h = h;
    l->img = img; l->iw = iw; l->ih = ih; l->sx = sx; l->sy = sy; l->sw = sw; l->sh = sh; l->wrap = wrap; l->gen = gen;
}
void hud_rect(int space, float x, float y, float w, float h, uint32_t argb){
    if (!s_hires || s_nlayers >= MAX_LAYERS) return;
    HudLayer *l = &s_layers[s_nlayers++];
    memset(l, 0, sizeof *l);
    l->space = space; l->x = x; l->y = y; l->w = w; l->h = h; l->argb = argb;
}
void hud_marker(int space, float cx, float cy, float r, float rim, uint32_t fill_argb, uint32_t rim_argb){
    if (!s_hires || s_nlayers >= MAX_LAYERS) return;
    HudLayer *l = &s_layers[s_nlayers++];
    memset(l, 0, sizeof *l);
    l->space = space; l->x = cx; l->y = cy; l->w = r; l->h = rim; l->argb = fill_argb;
    l->marker = true; l->rim_argb = rim_argb;
}
const HudLayer *hud_layers(int *n){ *n = s_nlayers; return s_layers; }
void hud_layers_clear(void){ s_nlayers = 0; }

/* ---- state ----
 * Page 0 = the main options (hud_set_options); further pages are option pages (hud_add_option_page)
 * or drawn pages (hud_add_page / hud_add_page_ex). Option pages keep their own selection; -1 = the
 * tab row (only when there is more than one page). */
#define MAX_PAGES 8
typedef struct {
    const char      *name;
    const HudOption *opts; int nopts; int sel;     /* option page */
    HudPageDraw      draw;                         /* drawn page (NULL for option pages) */
    void           (*key)(int key);
    bool             own_lr;                       /* drawn page takes Left/Right too */
} HudPage;
static const char *s_title = "SETTINGS";
static int       (*s_info)(char lines[][41], int max);
static void      (*s_on_close)(void);
static bool        s_open;
static HudPage     s_pages[MAX_PAGES] = { { "OPTIONS" } };
static int         s_npages = 1;                   /* including page 0 */
static int         s_page;
static int         s_main;                         /* tab index of the built-in OPTIONS page */
static int       (*s_open_page)(void);

void hud_set_title(const char *t){ s_title = t ? t : "SETTINGS"; }
void hud_set_options(const HudOption *o, int n){
    HudPage *p = &s_pages[s_main];
    p->opts = o; p->nopts = n; if (p->sel >= n) p->sel = 0;
}
void hud_set_info(int (*info)(char lines[][41], int max)){ s_info = info; }
void hud_set_on_close(void (*f)(void)){ s_on_close = f; }
bool hud_is_open(void){ return s_open; }
static HudPage *new_page(const char *name){
    if (s_npages >= MAX_PAGES) return NULL;
    HudPage *p = &s_pages[s_npages++]; memset(p, 0, sizeof *p); p->name = name; return p;
}
void hud_add_page_ex(const char *name, HudPageDraw draw, void (*key)(int key), bool own_lr){
    if (!draw) return;
    HudPage *p = new_page(name); if (!p) return;
    p->draw = draw; p->key = key; p->own_lr = own_lr && key;
}
void hud_add_page(const char *name, HudPageDraw draw, void (*key)(int key)){ hud_add_page_ex(name, draw, key, false); }
void hud_add_option_page(const char *name, const HudOption *opts, int n){
    HudPage *p = new_page(name); if (!p) return;
    p->opts = opts; p->nopts = n;
}
void hud_set_open_page(int (*f)(void)){ s_open_page = f; }
int hud_page_index(const char *name){
    for (int i = 0; name && i < s_npages; i++) if (!strcmp(s_pages[i].name, name)) return i;
    return -1;
}
void hud_set_page_order(const char *const *names, int n){
    HudPage out[MAX_PAGES]; bool used[MAX_PAGES] = { false }; int k = 0, main = s_main, cur = s_page;
    for (int j = 0; j < n; j++){
        int i = hud_page_index(names[j]);
        if (i < 0 || used[i]) continue;
        used[i] = true; out[k] = s_pages[i];
        if (i == main) s_main = k;
        if (i == cur) s_page = k;
        k++;
    }
    for (int i = 0; i < s_npages; i++) if (!used[i]){
        out[k] = s_pages[i];
        if (i == main) s_main = k;
        if (i == cur) s_page = k;
        k++;
    }
    memcpy(s_pages, out, sizeof(HudPage) * (size_t)s_npages);
}
int  hud_current_page(void){ return s_open ? s_page : -1; }

static void page_step(int d){ s_page = (s_page + d + s_npages) % s_npages; }

void hud_key(int key){
    if (key == HUD_KEY_SELECT){
        const HudPage *p = &s_pages[s_page];
        if (s_open && p->draw && p->own_lr && p->key){ p->key(key); return; }
        key = HUD_KEY_CLOSE;
    }
    if (key == HUD_KEY_TOGGLE || (key == HUD_KEY_CLOSE && s_open)){
        s_open = !s_open;
        if (!s_open && s_on_close) s_on_close();
        if (s_open && s_open_page){ int p = s_open_page(); if (p >= 0 && p < s_npages) s_page = p; }
        return;
    }
    if (!s_open) return;
    if (key == HUD_KEY_PAGE_PREV || key == HUD_KEY_PAGE_NEXT){ if (s_npages > 1) page_step(key == HUD_KEY_PAGE_NEXT ? 1 : -1); return; }
    HudPage *pg = &s_pages[s_page];
    bool lr = key == HUD_KEY_LEFT || key == HUD_KEY_RIGHT;
    if (pg->draw){                                     /* drawn page: U/D (and L/R if it wants) = its own */
        if (lr && !pg->own_lr) page_step(key == HUD_KEY_RIGHT ? 1 : -1);
        else if (pg->key && (!lr || pg->own_lr) && (lr || key == HUD_KEY_UP || key == HUD_KEY_DOWN)) pg->key(key);
        return;
    }
    int lo = s_npages > 1 ? -1 : 0;                    /* -1 = the tab row */
    int n = pg->nopts - lo;
    if (n <= 0) return;
    if (key == HUD_KEY_UP)   pg->sel = (pg->sel - lo + n - 1) % n + lo;
    if (key == HUD_KEY_DOWN) pg->sel = (pg->sel - lo + 1) % n + lo;
    if (lr && pg->sel < 0){ page_step(key == HUD_KEY_RIGHT ? 1 : -1); return; }
    if (lr && pg->nopts > 0){
        const HudOption *o = &pg->opts[pg->sel];
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
    const HudPage *pg = &s_pages[s_page];
    char info[8][41]; int ninfo = (s_page == s_main && s_info) ? s_info(info, 8) : 0;
    int rows = 2 + (s_npages > 1 ? 1 : 0) + (pg->nopts > 0 ? pg->nopts : 1) + (ninfo ? ninfo + 1 : 0) + 2;
    int x0 = 8, x1 = w - 8, y0 = 8, y1 = y0 + rows * 10 + 6;
    if (pg->draw || y1 > h - 4) y1 = h - 4;            /* drawn pages use the full height */
    fill(fb, w, h, x0, y0, x1, y1, 0xFF101830, true);
    fill(fb, w, h, x0, y0, x1, y0 + 1, CYAN, false); fill(fb, w, h, x0, y1 - 1, x1, y1, CYAN, false);
    fill(fb, w, h, x0, y0, x0 + 1, y1, CYAN, false); fill(fb, w, h, x1 - 1, y0, x1, y1, CYAN, false);
    int y = y0 + 5;
    hud_text(fb, w, h, x0 + 6, y, s_title, CYAN); y += 14;
    if (s_npages > 1){                                   /* tab row */
        bool on_tabs = pg->draw ? !pg->own_lr : pg->sel < 0;
        if (!pg->draw && pg->sel < 0) hud_text(fb, w, h, x0 + 4, y, ">", YEL);
        /* tabs on a strip; when it is wider than the row it scrolls so that the selected tab is
           centred (clamped at both ends), whole characters only, a triangle marks hidden tabs */
        int pos[MAX_PAGES], total = 0;
        for (int p = 0; p < s_npages; p++){ pos[p] = total; total += 6 * (int)strlen(s_pages[p].name) + 10; }
        total -= 10;
        int L = x0 + 12, R = x1 - 6, off = 0;
        bool scroll = total > R - L;
        if (scroll){
            L += 7; R -= 7;
            int sw = 6 * (int)strlen(s_pages[s_page].name);
            off = pos[s_page] + sw / 2 - (R - L) / 2;
            if (off > total - (R - L)) off = total - (R - L);
            if (off < 0) off = 0;
        }
        for (int p = 0; p < s_npages; p++){
            const char *nm = s_pages[p].name;
            uint32_t c = (p == s_page) ? (on_tabs ? YEL : WHITE) : GREY;
            int x = L + pos[p] - off, ux0 = x - 2, ux1 = x + 6 * (int)strlen(nm);
            for (const char *s = nm; *s; s++, x += 6){
                if (x < L || x + 5 > R) continue;
                char ch[2] = { *s, 0 }; hud_text(fb, w, h, x, y, ch, c);
            }
            if (p == s_page){
                if (ux0 < L - 2) ux0 = L - 2;
                if (ux1 > R) ux1 = R;
                if (ux1 > ux0) fill(fb, w, h, ux0, y + 8, ux1, y + 9, c, false);
            }
        }
        if (scroll){                                     /* 3 x 5 triangles = more tabs that way */
            for (int i = 0; i < 3; i++){
                if (off > 0) fill(fb, w, h, L - 6 + i, y + 3 - i, L - 5 + i, y + 4 + i, CYAN, false);
                if (off < total - (R - L)) fill(fb, w, h, R + 5 - i, y + 3 - i, R + 6 - i, y + 4 + i, CYAN, false);
            }
        }
        y += 14;
    }
    if (pg->draw){
        pg->draw(fb, w, h, x0 + 4, y, x1 - 4, y1 - 14);
        const char *cl = hud_start_opens() ? "F1/START CLOSE" : "F1/BACK CLOSE";
        char hint[48];
        snprintf(hint, sizeof hint, "%s   %s", pg->own_lr ? "LB/RB (Q/E) PAGE" : "LEFT/RIGHT PAGE", cl);
        hud_text(fb, w, h, x0 + 6, y1 - 12, hint, GREY);
        return;
    }
    if (pg->nopts <= 0){ hud_text(fb, w, h, x0 + 6, y, "NO OPTIONS FOR THIS GAME", GREY); y += 10; }
    for (int i = 0; i < pg->nopts; i++, y += 10){
        const HudOption *o = &pg->opts[i];
        uint32_t c = (i == pg->sel) ? YEL : WHITE;
        if (i == pg->sel) hud_text(fb, w, h, x0 + 4, y, ">", YEL);
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
    hud_text(fb, w, h, x0 + 6, y1 - 12, pg->sel < 0 ? "LEFT/RIGHT PAGE   UP/DOWN SELECT"
                                                    : "UP/DOWN SELECT   LEFT/RIGHT CHANGE", GREY);
}
