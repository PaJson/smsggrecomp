/*
 * hud.h - in-game settings overlay (HUD) for recompiled games.
 *
 * Generic: a game's hook code (e.g. Common/PhantasyStar/ps_qol.c, from its on_post_reset hook)
 * registers options; the runner draws them over the 256x192 frame with a built-in 5x7 font and
 * pauses the emulation while the HUD is open. Open/close: F1 (keyboard) or Back/Select (gamepad).
 * Inside: Up/Down select, Left/Right change, Enter/Esc/F1 (pad: A/B/Back) close; pages: Left/Right
 * on the tab row, Q/E or PgUp/PgDn (pad: LB/RB).
 *
 * With nothing registered the HUD still opens (shows "no options"), so every game gets it.
 */
#ifndef SMSGG_HUD_H
#define SMSGG_HUD_H
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    const char         *label;      /* shown left, upper-case recommended */
    const char *const  *choices;    /* value names shown right */
    int                 nchoices;
    int                *value;      /* index into choices, owned by the game */
    void              (*changed)(void);   /* called after *value changed (may be NULL) */
} HudOption;

/* Registration (call once, e.g. from on_post_reset). The array must stay valid. */
void hud_set_title(const char *title);
void hud_set_options(const HudOption *opts, int n);
/* Optional extra text under the options (e.g. live stats), filled on each draw: up to `max`
 * lines of up to 40 chars into lines[i]; return the number of lines. */
void hud_set_info(int (*info)(char lines[][41], int max));
/* Called when the HUD closes (e.g. to save the settings file). */
void hud_set_on_close(void (*on_close)(void));

/* Extra pages (tabs) after the built-in OPTIONS page, e.g. a map (up to 4). `draw` paints the page
 * body into the box (x0,y0)-(x1,y1) of the w x h frame; `key` (may be NULL) receives HUD_KEY_UP /
 * HUD_KEY_DOWN while the page is shown. Pages switch with Left/Right on the tab row (options page:
 * Up from the first option), directly with Left/Right on other pages, or HUD_KEY_PAGE_PREV/NEXT. */
typedef void (*HudPageDraw)(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1);
void hud_add_page(const char *name, HudPageDraw draw, void (*key)(int key));
/* Called each time the HUD opens: return the page to show (0 = options, 1.. = added pages), or -1
 * to keep the last one (e.g. Phantasy Star opens on the map page inside a dungeon). */
void hud_set_open_page(int (*f)(void));
/* Rectangle helper for pages (blend: 25% of the frame shows through). */
void hud_fill(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1, uint32_t argb, bool blend);

/* Host side. */
enum { HUD_KEY_UP, HUD_KEY_DOWN, HUD_KEY_LEFT, HUD_KEY_RIGHT, HUD_KEY_CLOSE, HUD_KEY_TOGGLE,
       HUD_KEY_PAGE_PREV, HUD_KEY_PAGE_NEXT };
bool hud_is_open(void);
void hud_key(int key);                               /* navigation / open-close */
void hud_draw(uint32_t *fb, int w, int h);           /* draw over an ARGB frame */
/* Draw `text` (5x7 font, 6 px advance) at (x,y) in `argb`; returns the end x. Usable by games
 * that want to draw their own panels. */
int  hud_text(uint32_t *fb, int w, int h, int x, int y, const char *text, uint32_t argb);
/* Same font scaled by `scale` (2 = 10x14 glyphs, 12 px advance). */
int  hud_text_scaled(uint32_t *fb, int w, int h, int x, int y, const char *text, uint32_t argb, int scale);

/* Side panel (widescreen layout, SDL window): when a game registers a panel and enables it, the
 * window becomes 16:9: the game picture (3x) on the left, an HUD_PANEL_W x HUD_PANEL_H ARGB image
 * drawn by `draw` on the right (redrawn every presented frame, also while the HUD is open). */
#define HUD_PANEL_W 256
#define HUD_PANEL_H 576
typedef void (*HudPanelDraw)(uint32_t *buf, int w, int h);
void hud_set_panel(HudPanelDraw draw);
void hud_set_panel_enabled(bool on);
bool hud_panel_active(void);                          /* registered and enabled */
void hud_panel_render(uint32_t *buf, int w, int h);   /* host: fill the panel image */

/* High-resolution layers: images and rectangles the SDL host draws at the window's REAL pixel
 * resolution on top of the scaled game picture (HUD_SPACE_GAME, framebuffer coordinates as passed
 * to hud_draw) or the side panel (HUD_SPACE_PANEL, panel coordinates). Use them for detailed
 * pictures (maps) that would lose detail in the 256-px frame / panel. Register them every frame
 * from a HUD page or panel draw; the host draws them on the next present, then forgets them.
 * Only when hud_hires() is true (SDL window); otherwise draw into the frame / panel as usual.
 *   image: ARGB pixels (iw x ih, must stay valid until the present), the source rectangle
 *          (sx, sy, sw, sh) is shown in the destination rectangle (x, y, w, h); with `wrap` the
 *          source repeats (sx/sy may be anywhere). `gen` must change whenever the pixels change
 *          (the host keeps a texture per image and only re-uploads then). Scaling: integer nearest
 *          + linear when enlarging (sharp pixels), area average when shrinking.
 *   rect:  filled rectangle, argb alpha < 255 blends. Drawn after all images of the same space. */
enum { HUD_SPACE_GAME, HUD_SPACE_PANEL };
bool hud_hires(void);
void hud_set_hires(bool on);                          /* host: hi-res layers are drawn */
void hud_image(int space, float x, float y, float w, float h, const uint32_t *img, int iw, int ih,
               int sx, int sy, int sw, int sh, bool wrap, uint32_t gen);
void hud_rect(int space, float x, float y, float w, float h, uint32_t argb);
typedef struct {
    int space; float x, y, w, h;
    const uint32_t *img; int iw, ih, sx, sy, sw, sh; bool wrap; uint32_t gen;   /* img NULL = rect */
    uint32_t argb;
} HudLayer;
const HudLayer *hud_layers(int *n);                   /* host: the queued layers, in order */
void hud_layers_clear(void);

#endif
