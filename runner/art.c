/*
 * art.c - optional per-game artwork (see art.h). Images are decoded with stb_image
 * (runner/external/stb, public domain / MIT).
 */
#include "art.h"
#include "hud.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <dirent.h>
#include <sys/stat.h>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#ifdef _WIN32
#define STBI_WINDOWS_UTF8
#endif
#include "external/stb/stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

uint32_t *art_load(const char *path, int *w, int *h)
{
    int n = 0;
    unsigned char *p = stbi_load(path, w, h, &n, 4);
    if (!p) return NULL;
    size_t cnt = (size_t)*w * (size_t)*h;
    uint32_t *out = (uint32_t *)malloc(cnt * 4);
    if (out)
        for (size_t i = 0; i < cnt; i++)
            out[i] = (uint32_t)p[i * 4 + 3] << 24 | (uint32_t)p[i * 4] << 16 | (uint32_t)p[i * 4 + 1] << 8 | p[i * 4 + 2];
    stbi_image_free(p);
    return out;
}

bool art_title_matches(const char *name, const char *title)
{
    size_t l = strlen(title);
    if (!l || strlen(name) < l) return false;
    for (size_t i = 0; i < l; i++)
        if (tolower((unsigned char)name[i]) != tolower((unsigned char)title[i])) return false;
    const char *r = name + l;
    return !r[0] || r[0] == ':' || (r[0] == ' ' && (r[1] == '(' || r[1] == '[' || r[1] == '-'));
}

static bool is_dir(const char *p) { struct stat st; return stat(p, &st) == 0 && (st.st_mode & S_IFDIR); }

static bool find_in(const char *dir, const char *name, const char *kind, char *out, size_t n)
{
    if (!dir || !name || !name[0]) return false;
    DIR *d = opendir(dir);
    if (!d) return false;
    char tag[40]; snprintf(tag, sizeof tag, " (%s)", kind);
    size_t tl = strlen(tag), best = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        const char *f = e->d_name, *dot = strrchr(f, '.');
        if (!dot) continue;
        if (_stricmp(dot, ".png") && _stricmp(dot, ".jpg") && _stricmp(dot, ".jpeg")) continue;
        size_t stem = (size_t)(dot - f);
        if (stem <= tl || _strnicmp(f + stem - tl, tag, tl)) continue;
        char title[256]; size_t ttl = stem - tl;
        if (ttl >= sizeof title) continue;
        memcpy(title, f, ttl); title[ttl] = 0;
        if (ttl > best && art_title_matches(name, title)) { best = ttl; snprintf(out, n, "%s/%s", dir, f); }
    }
    closedir(d);
    return best > 0;
}

/* Images/<title> (front).jpg ... or in a platform subfolder: Images/sms/<title> (front).jpg (one level) */
bool art_find(const char *dir, const char *name, const char *kind, char *out, size_t n)
{
    if (find_in(dir, name, kind, out, n)) return true;
    if (!dir || !name || !name[0]) return false;
    DIR *d = opendir(dir);
    if (!d) return false;
    struct dirent *e; bool ok = false;
    while (!ok && (e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char sub[1100]; snprintf(sub, sizeof sub, "%s/%s", dir, e->d_name);
        if (is_dir(sub)) ok = find_in(sub, name, kind, out, n);
    }
    closedir(d);
    return ok;
}

bool art_images_dir(const char *start, char *out, size_t n)
{
    const char *env = getenv("SMS_IMAGES_DIR");
    if (env && env[0]) { if (!is_dir(env)) return false; snprintf(out, n, "%s", env); return true; }
    if (!start || !start[0]) start = ".";
    char d[1024]; snprintf(d, sizeof d, "%s", start);
    for (char *c = d; *c; c++) if (*c == '\\') *c = '/';
    for (int up = 0; up <= 4; up++) {
        char p[1100]; snprintf(p, sizeof p, "%s/Images", d);
        if (is_dir(p)) { snprintf(out, n, "%s", p); return true; }
        char *s = strrchr(d, '/');
        if (s && s[1] && strcmp(s + 1, "..") && strcmp(s + 1, ".")) *s = 0;   /* go to the parent */
        else if (strlen(d) + 4 < sizeof d) strcat(d, "/..");
        else break;
    }
    return false;
}

uint32_t *art_outline(const uint32_t *img, int w, int h, int r, uint32_t col, int *ow, int *oh)
{
    int p = r + 1, W = w + 2 * p, H = h + 2 * p;
    uint32_t *out = (uint32_t *)malloc((size_t)W * H * 4);
    float *oa = (float *)calloc((size_t)W * H, sizeof(float));
    int *ox = (int *)malloc(sizeof(int) * (size_t)(2 * p + 1) * (2 * p + 1) * 2);
    float *ow8 = (float *)malloc(sizeof(float) * (size_t)(2 * p + 1) * (2 * p + 1));
    if (!out || !oa || !ox || !ow8) { free(out); free(oa); free(ox); free(ow8); return NULL; }
    int no = 0;                                         /* disk offsets with a soft edge weight */
    for (int dy = -p; dy <= p; dy++)
        for (int dx = -p; dx <= p; dx++) {
            float wgt = (float)r + 0.5f - sqrtf((float)(dx * dx + dy * dy));
            if (wgt <= 0) continue;
            ox[2 * no] = dx; ox[2 * no + 1] = dy; ow8[no++] = wgt > 1 ? 1 : wgt;
        }
    for (int y = 0; y < h; y++)                        /* spread every source alpha over the disk */
        for (int x = 0; x < w; x++) {
            float a = (float)(img[(size_t)y * w + x] >> 24) / 255.f;
            if (a <= 0) continue;
            for (int k = 0; k < no; k++) {
                float v = a * ow8[k];
                float *t = &oa[(size_t)(y + p + ox[2 * k + 1]) * W + x + p + ox[2 * k]];
                if (v > *t) *t = v;
            }
        }
    float cr = (float)(col >> 16 & 255), cg = (float)(col >> 8 & 255), cb = (float)(col & 255);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int sx = x - p, sy = y - p;
            uint32_t s = (sx >= 0 && sy >= 0 && sx < w && sy < h) ? img[(size_t)sy * w + sx] : 0;
            float a = (float)(s >> 24) / 255.f, o = oa[(size_t)y * W + x] * (1 - a), A = a + o;
            uint32_t c;
            if (A <= 0.0005f) c = col & 0x00FFFFFFu;
            else {
                float R = ((float)(s >> 16 & 255) * a + cr * o) / A, G = ((float)(s >> 8 & 255) * a + cg * o) / A, B = ((float)(s & 255) * a + cb * o) / A;
                c = (uint32_t)(A * 255.f + 0.5f) << 24 | (uint32_t)(R + 0.5f) << 16 | (uint32_t)(G + 0.5f) << 8 | (uint32_t)(B + 0.5f);
            }
            out[(size_t)y * W + x] = c;
        }
    free(oa); free(ox); free(ow8);
    *ow = W; *oh = H;
    return out;
}

uint32_t *art_shrink(const uint32_t *img, int w, int h, int dw, int dh)
{
    if (dw < 1 || dh < 1) return NULL;
    uint32_t *out = (uint32_t *)malloc((size_t)dw * dh * 4);
    if (!out) return NULL;
    for (int y = 0; y < dh; y++) {
        int y0 = (int)((long long)y * h / dh), y1 = (int)((long long)(y + 1) * h / dh); if (y1 <= y0) y1 = y0 + 1;
        for (int x = 0; x < dw; x++) {
            int x0 = (int)((long long)x * w / dw), x1 = (int)((long long)(x + 1) * w / dw); if (x1 <= x0) x1 = x0 + 1;
            unsigned long long r = 0, g = 0, b = 0, a = 0, n = 0;
            for (int sy = y0; sy < y1 && sy < h; sy++)
                for (int sx = x0; sx < x1 && sx < w; sx++) {
                    uint32_t c = img[(size_t)sy * w + sx]; unsigned ca = c >> 24;
                    r += (c >> 16 & 255) * ca; g += (c >> 8 & 255) * ca; b += (c & 255) * ca; a += ca; n++;
                }
            uint32_t v = 0;
            if (a) v = (uint32_t)((a + n / 2) / n) << 24 | (uint32_t)(r / a) << 16 | (uint32_t)(g / a) << 8 | (uint32_t)(b / a);
            else if (n) v = img[(size_t)y0 * w + x0] & 0x00FFFFFFu;
            out[(size_t)y * dw + x] = v;
        }
    }
    return out;
}

void art_blend(uint32_t *fb, int fw, int fh, int x, int y, const uint32_t *src, int sw, int sh)
{
    for (int j = 0; j < sh; j++)
        for (int i = 0; i < sw; i++) {
            int X = x + i, Y = y + j;
            if (X < 0 || Y < 0 || X >= fw || Y >= fh) continue;
            uint32_t s = src[(size_t)j * sw + i], a = s >> 24;
            if (!a) continue;
            uint32_t *d = &fb[(size_t)Y * fw + X];
            if (a == 255) { *d = s | 0xFF000000u; continue; }
            uint32_t R = ((s >> 16 & 255) * a + (*d >> 16 & 255) * (255 - a)) / 255;
            uint32_t G = ((s >> 8 & 255) * a + (*d >> 8 & 255) * (255 - a)) / 255;
            uint32_t B = ((s & 255) * a + (*d & 255) * (255 - a)) / 255;
            *d = 0xFF000000u | R << 16 | G << 8 | B;
        }
}

void art_setup_game_logo(const char *name, const char *rom_path)
{
    if (getenv("SMS_NO_ART")) return;
    char dir[1100], rom_dir[1024] = ".", rom_name[256] = "";
    if (rom_path && rom_path[0]) {
        snprintf(rom_dir, sizeof rom_dir, "%s", rom_path);
        for (char *c = rom_dir; *c; c++) if (*c == '\\') *c = '/';
        char *s = strrchr(rom_dir, '/');
        if (s) { snprintf(rom_name, sizeof rom_name, "%s", s + 1); *s = 0; }
        else { snprintf(rom_name, sizeof rom_name, "%s", rom_dir); snprintf(rom_dir, sizeof rom_dir, "."); }
        char *dot = strrchr(rom_name, '.'); if (dot) *dot = 0;
    }
    if (!art_images_dir(rom_dir, dir, sizeof dir) && !art_images_dir(".", dir, sizeof dir)) return;
    char path[1400];
    if (!(name && art_find(dir, name, "logo", path, sizeof path)) && !art_find(dir, rom_name, "logo", path, sizeof path)) return;
    int w, h; uint32_t *img = art_load(path, &w, &h);
    if (!img) { fprintf(stderr, "[art] cannot read %s\n", path); return; }
    int r = (w > h ? w : h) / 100; if (r < 2) r = 2;
    int ow, oh; uint32_t *o = art_outline(img, w, h, r, 0xFFFFFFFFu, &ow, &oh);
    free(img);
    if (!o) return;
    hud_set_logo(o, ow, oh);                            /* the HUD keeps it */
    if (getenv("SMS_ART_LOG")) fprintf(stderr, "[art] logo %s (%d x %d)\n", path, ow, oh);
}
