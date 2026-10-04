#ifndef GFX_H
#define GFX_H

#include "stdint.h"
#include "limine.h"

typedef struct {
    void *addr;
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint32_t bpp;
    uint8_t red_mask_size, red_mask_shift;
    uint8_t green_mask_size, green_mask_shift;
    uint8_t blue_mask_size, blue_mask_shift;
} gfx_context_t;

int gfx_init(struct limine_framebuffer *fb_response);
gfx_context_t *gfx_get_context(void);

void gfx_put_pixel(gfx_context_t *fb, int x, int y, uint32_t color);
void gfx_fill_rect(gfx_context_t *fb, int x, int y, int w, int h, uint32_t color);
void gfx_fill_screen(gfx_context_t *fb, uint32_t color);
void gfx_hline(gfx_context_t *fb, int x, int y, int w, uint32_t color);
void gfx_vline(gfx_context_t *fb, int x, int y, int h, uint32_t color);
void gfx_draw_line(gfx_context_t *fb, int x0, int y0, int x1, int y1, uint32_t color);
void gfx_draw_rect(gfx_context_t *fb, int x, int y, int w, int h, uint32_t color);
void gfx_blit(gfx_context_t *fb, int x, int y, int w, int h, const uint32_t *pixels);
void gfx_draw_char(gfx_context_t *fb, int x, int y, char c, uint32_t fg, uint32_t bg);
void gfx_draw_string(gfx_context_t *fb, int x, int y, const char *str, uint32_t fg, uint32_t bg);

#endif // GFX_H
