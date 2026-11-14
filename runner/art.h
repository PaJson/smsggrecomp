/*
 * art.h - optional per-game artwork supplied by the user (cover scans, title logos).
 *
 * Nothing here is part of the engine or the games: the user puts pictures into an `Images` folder
 * (next to the ROMs folder, or $SMS_IMAGES_DIR), named "<title> (<kind>).<png|jpg>", e.g.
 * "Phantasy Star (logo).png", "Phantasy Star (front).jpg". A picture belongs to every game whose
 * name starts with <title> followed by the end of the name, " (", " [", " -" or ":" (case is
 * ignored), so "Phantasy Star" covers "Phantasy Star (Japan)" but "Sonic the Hedgehog" does not
 * cover "Sonic The Hedgehog 2". The longest matching title wins. Without the folder or a matching
 * file everything works as before.
 *
 * Pixels are 0xAARRGGBB with straight (not premultiplied) alpha.
 */
#ifndef SMSGG_ART_H
#define SMSGG_ART_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* PNG / JPEG file -> ARGB pixels (free() them), NULL on failure. */
uint32_t *art_load(const char *path, int *w, int *h);
/* does `title` name the game called `name` (see above)? */
bool art_title_matches(const char *name, const char *title);
/* best "<title> (<kind>).<ext>" file in `dir` for the game `name`; false = none */
bool art_find(const char *dir, const char *name, const char *kind, char *out, size_t n);
/* the Images folder: $SMS_IMAGES_DIR, else "Images" in `start` or up to 4 parent folders of it */
bool art_images_dir(const char *start, char *out, size_t n);
/* the picture with an outline of `r` pixels in `col` around its opaque parts (soft edge); the result
 * is (w + 2r + 2) x (h + 2r + 2). Fully transparent pixels get the outline colour (no dark fringes
 * when it is filtered). */
uint32_t *art_outline(const uint32_t *img, int w, int h, int r, uint32_t col, int *ow, int *oh);
/* area-average downscale (alpha-weighted) of img to dw x dh (dw <= w, dh <= h). */
uint32_t *art_shrink(const uint32_t *img, int w, int h, int dw, int dh);
/* blend src (straight alpha) into the opaque frame fb at (x, y) */
void art_blend(uint32_t *fb, int fw, int fh, int x, int y, const uint32_t *src, int sw, int sh);

/* Runner: find, load and outline the game's logo ("<title> (logo).png") and hand it to the HUD
 * (hud_set_logo). `name` = the display name, `rom_path` = the ROM file (its name is tried too). */
void art_setup_game_logo(const char *name, const char *rom_path);

#endif
