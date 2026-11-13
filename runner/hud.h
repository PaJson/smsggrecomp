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

#endif
