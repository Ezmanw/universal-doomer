/* Dump a few resources through the pd_res_* API to PPM files. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "portadoom.h"

static void save16(const char *path, const uint16_t *img, int w, int h)
{
    const uint8_t *pal = pd_palette_base();
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++)
    {
        uint8_t px[3] = { 0, 255, 255 };   /* transparent = cyan */
        if (img[i] != 0xffff)
            memcpy(px, &pal[img[i] * 3], 3);
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb");
    fseek(f, 0, SEEK_END);
    size_t len = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *wad = malloc(len);
    fread(wad, 1, len, f);
    fclose(f);

    pd_config_t cfg = { 0 };
    cfg.heap_size = pd_recommended_heap_size();
    cfg.heap = malloc(cfg.heap_size);
    cfg.iwad_name = argv[1];
    cfg.iwad_data = wad;
    cfg.iwad_size = len;
    pd_init(&cfg, NULL);

    static uint16_t img[512 * 512];
    int w, h, lo, to, flip;
    if (pd_res_texture("STARTAN3", &w, &h, img, sizeof(img) / 2))
    {
        printf("STARTAN3 %dx%d\n", w, h);
        save16("out/tex_startan3.ppm", img, w, h);
    }
    if (pd_res_texture("MIDGRATE", &w, &h, img, sizeof(img) / 2))
    {
        printf("MIDGRATE %dx%d\n", w, h);
        save16("out/tex_midgrate.ppm", img, w, h);
    }
    if (pd_res_sprite("TROO", 0, 1, &w, &h, &lo, &to, &flip, img, sizeof(img) / 2))
    {
        printf("TROO A1 %dx%d offs %d,%d flip %d\n", w, h, lo, to, flip);
        save16("out/spr_troo.ppm", img, w, h);
    }
    int samples, rate;
    if (pd_res_sound("pistol", &samples, &rate))
        printf("pistol: %d samples @ %d Hz\n", samples, rate);
    size_t mid = pd_res_music_midi("D_E1M1", NULL, 0);
    printf("D_E1M1 as MIDI: %zu bytes\n", mid);
    return 0;
}
