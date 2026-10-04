#include "gfx.h"
#include "font.h"
#include "serial.h"
#include "string.h"
#include "stdint.h"

static gfx_context_t context;
static int initialized = 0;

static inline uint32_t compose_color(gfx_context_t *fb, uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << fb->red_mask_shift) |
           ((uint32_t)g << fb->green_mask_shift) |
           ((uint32_t)b << fb->blue_mask_shift);
}

static inline uint32_t rgb_to_fb(gfx_context_t *fb, uint32_t rgb) {
    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >> 8) & 0xFF;
    uint8_t b = rgb & 0xFF;
    return compose_color(fb, r, g, b);
}

int gfx_init(struct limine_framebuffer *fb_response) {
    if (!fb_response) {
        serial_write("[GFX] Invalid framebuffer response\n");
        return -1;
    }
    
    context.addr = fb_response->address;
    context.width = fb_response->width;
    context.height = fb_response->height;
    context.pitch = fb_response->pitch;
    context.bpp = fb_response->bpp;
    context.red_mask_size = fb_response->red_mask_size;
    context.red_mask_shift = fb_response->red_mask_shift;
    context.green_mask_size = fb_response->green_mask_size;
    context.green_mask_shift = fb_response->green_mask_shift;
    context.blue_mask_size = fb_response->blue_mask_size;
    context.blue_mask_shift = fb_response->blue_mask_shift;
    
    initialized = 1;
    
    serial_write("[GFX] Framebuffer: ");
    serial_write_dec(context.width);
    serial_write("x");
    serial_write_dec(context.height);
    serial_write(" pitch=");
    serial_write_dec(context.pitch);
    serial_write(" bpp=");
    serial_write_dec(context.bpp);
    serial_write("\n");
    
    return 0;
}

gfx_context_t *gfx_get_context(void) {
    return initialized ? &context : NULL;
}

void gfx_put_pixel(gfx_context_t *fb, int x, int y, uint32_t color) {
    if (!fb || x < 0 || x >= (int)fb->width || y < 0 || y >= (int)fb->height) return;
    uint8_t *row = (uint8_t *)fb->addr + y * fb->pitch;
    uint32_t *pixel = (uint32_t *)(row + x * 4);
    *pixel = color;
}

void gfx_fill_rect(gfx_context_t *fb, int x, int y, int w, int h, uint32_t color) {
    if (!fb || w <= 0 || h <= 0) return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = (x + w > (int)fb->width) ? (int)fb->width : (x + w);
    int y1 = (y + h > (int)fb->height) ? (int)fb->height : (y + h);
    if (x0 >= x1 || y0 >= y1) return;
    
    int clipped_w = x1 - x0;
    for (int row = y0; row < y1; row++) {
        uint8_t *line = (uint8_t *)fb->addr + row * fb->pitch;
        uint32_t *pixels = (uint32_t *)(line + x0 * 4);
        for (int col = 0; col < clipped_w; col++) {
            pixels[col] = color;
        }
    }
}

void gfx_fill_screen(gfx_context_t *fb, uint32_t color) {
    if (!fb) return;
    gfx_fill_rect(fb, 0, 0, fb->width, fb->height, color);
}

void gfx_hline(gfx_context_t *fb, int x, int y, int w, uint32_t color) {
    gfx_fill_rect(fb, x, y, w, 1, color);
}

void gfx_vline(gfx_context_t *fb, int x, int y, int h, uint32_t color) {
    gfx_fill_rect(fb, x, y, 1, h, color);
}

void gfx_draw_line(gfx_context_t *fb, int x0, int y0, int x1, int y1, uint32_t color) {
    if (!fb) return;
    int dx = x1 - x0, dy = y1 - y0;
    int sx = (dx > 0) ? 1 : -1, sy = (dy > 0) ? 1 : -1;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    int err = dx - dy;
    while (1) {
        gfx_put_pixel(fb, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

void gfx_draw_rect(gfx_context_t *fb, int x, int y, int w, int h, uint32_t color) {
    if (!fb || w <= 0 || h <= 0) return;
    gfx_hline(fb, x, y, w, color);
    gfx_hline(fb, x, y + h - 1, w, color);
    gfx_vline(fb, x, y, h, color);
    gfx_vline(fb, x + w - 1, y, h, color);
}

void gfx_blit(gfx_context_t *fb, int x, int y, int w, int h, const uint32_t *pixels) {
    if (!fb || !pixels || w <= 0 || h <= 0 || w > 0xFFFF || h > 0xFFFF) return;
    int src_x = 0, src_y = 0, dst_x = x, dst_y = y, copy_w = w, copy_h = h;
    if (dst_x < 0) { src_x = -dst_x; copy_w += dst_x; dst_x = 0; }
    if (dst_y < 0) { src_y = -dst_y; copy_h += dst_y; dst_y = 0; }
    if (dst_x + copy_w > (int)fb->width) copy_w = (int)fb->width - dst_x;
    if (dst_y + copy_h > (int)fb->height) copy_h = (int)fb->height - dst_y;
    if (copy_w <= 0 || copy_h <= 0) return;
    
    for (int row = 0; row < copy_h; row++) {
        const uint32_t *src_row = pixels + (src_y + row) * w + src_x;
        uint8_t *dst_row = (uint8_t *)fb->addr + (dst_y + row) * fb->pitch;
        uint32_t *dst_pixels = (uint32_t *)(dst_row + dst_x * 4);
        for (int col = 0; col < copy_w; col++) {
            dst_pixels[col] = src_row[col];
        }
    }
}

void gfx_draw_char(gfx_context_t *fb, int x, int y, char c, uint32_t fg, uint32_t bg) {
    if (!fb || x + 8 <= 0 || x >= (int)fb->width || y + 16 <= 0 || y >= (int)fb->height) return;
    const uint8_t *glyph = &font_8x16_data[(uint8_t)c * 16];
    for (int row = 0; row < 16; row++) {
        int py = y + row;
        if (py < 0 || py >= (int)fb->height) continue;
        uint8_t bits = glyph[row];
        uint8_t *fb_row = (uint8_t *)fb->addr + py * fb->pitch;
        for (int col = 0; col < 8; col++) {
            int px = x + col;
            if (px < 0 || px >= (int)fb->width) continue;
            uint32_t color = (bits & (0x80 >> col)) ? fg : bg;
            uint32_t *pixel = (uint32_t *)(fb_row + px * 4);
            *pixel = color;
        }
    }
}

void gfx_draw_string(gfx_context_t *fb, int x, int y, const char *str, uint32_t fg, uint32_t bg) {
    if (!fb || !str) return;
    int start_x = x;
    while (*str) {
        if (*str == 10) { x = start_x; y += 16; }
        else { gfx_draw_char(fb, x, y, *str, fg, bg); x += 8; }
        str++;
    }
}
