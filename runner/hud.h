/*
 * hud.h - in-game settings overlay (HUD) for recompiled games.
 *
 * Generic: a game's hook code (e.g. Common/PhantasyStar/ps_qol.c, from its on_post_reset hook)
 * registers options; the runner draws them over the 256x192 frame with a built-in 5x7 font and
 * pauses the emulation while the HUD is open. Open/close: F1 (keyboard) or Back/Select (gamepad).
 * Inside: Up/Down select, Left/Right change, Enter/Esc/F1 (pad: A/B/Back) close.
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

/* Host side. */
enum { HUD_KEY_UP, HUD_KEY_DOWN, HUD_KEY_LEFT, HUD_KEY_RIGHT, HUD_KEY_CLOSE, HUD_KEY_TOGGLE };
bool hud_is_open(void);
void hud_key(int key);                               /* navigation / open-close */
void hud_draw(uint32_t *fb, int w, int h);           /* draw over an ARGB frame */
/* Draw `text` (5x7 font, 6 px advance) at (x,y) in `argb`; returns the end x. Usable by games
 * that want to draw their own panels. */
int  hud_text(uint32_t *fb, int w, int h, int x, int y, const char *text, uint32_t argb);

#endif
