/*
 * hud.h - in-game settings overlay (HUD) for recompiled games.
 *
 * Generic: a game's hook code (e.g. Common/PhantasyStar/ps_qol.c, from its on_post_reset hook)
 * registers options; the runner draws them over the 256x192 frame with a built-in 5x7 font and
 * pauses the emulation while the HUD is open. Open/close: F1 (keyboard); gamepad: Start on SMS games
 * (the SMS Pause button is not emulated), Back/Select on Game Gear games (Start is the game's);
 * Back toggles the side panel in games that have one (as F2 does), otherwise it opens the HUD.
 * Inside: Up/Down select, Left/Right change, Esc/X/F1 (pad: B/Start) close, Enter/Space/Z (pad: A) confirm
 * (A / Z = the SMS button 1 = confirm, B / X = button 2 = back, as in the games)
 * (pages that do not use it: close); pages: Left/Right
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

/* Extra pages (tabs) after the built-in OPTIONS page, in the order added (up to 7).
 * Option page: a list of options like the OPTIONS page, with its own selection.
 * Drawn page (e.g. a map): `draw` paints the page body into the box (x0,y0)-(x1,y1) of the w x h
 * frame; `key` (may be NULL) receives HUD_KEY_UP / HUD_KEY_DOWN while the page is shown, and with
 * `own_lr` also HUD_KEY_LEFT / HUD_KEY_RIGHT (then only HUD_KEY_PAGE_PREV/NEXT leave the page).
 * Pages switch with Left/Right on the tab row (option pages: Up from the first option), directly
 * with Left/Right on drawn pages without own_lr, or HUD_KEY_PAGE_PREV/NEXT (LB/RB, Q/E). */
typedef void (*HudPageDraw)(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1);
void hud_add_page(const char *name, HudPageDraw draw, void (*key)(int key));
void hud_add_page_ex(const char *name, HudPageDraw draw, void (*key)(int key), bool own_lr);
void hud_add_option_page(const char *name, const HudOption *opts, int n);
int  hud_current_page(void);                          /* page shown (tab index), -1 = HUD closed */
/* Tab order by page name (the built-in page is "OPTIONS"); pages not named keep their order after
 * the named ones. Page indices (hud_current_page, hud_page_index, open page) follow the new order.
 * The tab row scrolls (selected tab centred) when the names do not fit. */
void hud_set_page_order(const char *const *names, int n);
int  hud_page_index(const char *name);                /* tab index of a page, -1 = none */
/* Called each time the HUD opens: return the page to show (tab index, see hud_page_index), or -1
 * to keep the last one (e.g. Phantasy Star opens on the map page inside a dungeon). */
void hud_set_open_page(int (*f)(void));
/* Fast-forward (hold TAB or the pad's right trigger): the speed multiplier while it is
 * held (default 4; e.g. 0.5 = slow motion; the left trigger is always x0.5). A game may offer it as a HUD option. */
void   hud_set_turbo_speed(double mult);
/* Extra buttons for game features beyond the SMS pad (held state, read by the game each frame;
 * not delivered while the HUD is open): HUD_EXTRA_1 = pad LB / key C, HUD_EXTRA_2 = pad RB / key V. */
enum { HUD_EXTRA_1 = 1, HUD_EXTRA_2 = 2 };
uint8_t hud_extra_buttons(void);
void    hud_set_extra_buttons(uint8_t mask);          /* host */
double hud_turbo_speed(void);
/* Overlay over the game picture while the HUD is closed (e.g. a status line "AUTO ATTACK: ON"):
 * the host copies each presented frame and calls `draw` on the copy (it may draw nothing). */
void hud_set_overlay(void (*draw)(uint32_t *fb, int w, int h));
bool hud_has_overlay(void);
void hud_draw_overlay(uint32_t *fb, int w, int h);    /* host */
/* Rectangle helper for pages (blend: 25% of the frame shows through). */
void hud_fill(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1, uint32_t argb, bool blend);

/* Host side. */
enum { HUD_KEY_UP, HUD_KEY_DOWN, HUD_KEY_LEFT, HUD_KEY_RIGHT, HUD_KEY_CLOSE, HUD_KEY_TOGGLE,
       HUD_KEY_PAGE_PREV, HUD_KEY_PAGE_NEXT,
       HUD_KEY_SELECT };   /* confirm: goes to a drawn page with own_lr, elsewhere it closes the HUD */
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
/* Toggle the panel from a button (pad Back / F2); `cb` (may be NULL) is told the new state so the
 * game can keep its own setting in step. */
void hud_toggle_panel(void);
void hud_set_on_panel_toggle(void (*cb)(bool on));
/* Pad mapping: Start opens the HUD when the host says Start is free (SMS: the Pause button is not
 * emulated); Back toggles the side panel when a game registered one, otherwise Back opens the HUD
 * too (Game Gear: Start is the game's, Back = HUD). */
void hud_set_start_free(bool free);                   /* host */
bool hud_start_opens(void);
bool hud_has_panel(void);
/* SMS: pad Back = the console's Pause even though the game has a side panel (the panel is then
 * toggled with F2 or the game's own option). Default: Back toggles the panel when there is one. */
void hud_set_back_pause(bool on);
bool hud_back_pauses(void);                           /* SMS pad Back = Pause (no panel, or set above) */
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
/* image with transparency (straight alpha, e.g. a logo), whole image into (x, y, w, h) */
void hud_image_alpha(int space, float x, float y, float w, float h, const uint32_t *img, int iw, int ih, uint32_t gen);
/* marker: a filled square centred on (cx, cy) with a rim around it; r = half the square's size,
 * rim = rim width (both in `space` units). The host snaps the centre and both sizes to whole window
 * pixels (square at least 4 px, rim at least 1 px), so the rim is equally thick on all four sides. */
void hud_marker(int space, float cx, float cy, float r, float rim, uint32_t fill_argb, uint32_t rim_argb);
typedef struct {
    int space; float x, y, w, h;
    const uint32_t *img; int iw, ih, sx, sy, sw, sh; bool wrap; uint32_t gen;   /* img NULL = rect */
    uint32_t argb;
    bool marker; uint32_t rim_argb;                   /* hud_marker: x, y = centre, w = r, h = rim */
    bool alpha;                                       /* img has transparency (hud_image_alpha) */
} HudLayer;
const HudLayer *hud_layers(int *n);                   /* host: the queued layers, in order */
void hud_layers_clear(void);

/* Game logo: the user's "Images/<title> (logo).png" (see art.h), outlined; the HUD header shows
 * it in place of the game name. hud_set_logo takes ownership (malloc'ed ARGB, straight alpha). */
void hud_set_logo(uint32_t *img, int w, int h);
const uint32_t *hud_logo(int *w, int *h);             /* NULL = none */
/* Draw the logo fitted into (x, y, bw, bh) of `space` (never enlarged beyond its own size, vertically
 * centred, horizontally centred with `centre`, else left-aligned): a hi-res layer when available,
 * else blended into fb (the frame or panel buffer, fw x fh). Returns the drawn width (0 = no logo). */
float hud_draw_logo(uint32_t *fb, int fw, int fh, int space, float x, float y, float bw, float bh, bool centre);

#endif
