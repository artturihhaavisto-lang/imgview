#include <gdk-pixbuf/gdk-pixbuf.h>
#include <stdbool.h>
#include <string.h>
#include "../src/gif-stream.h"

static void frame(GifFileType *file, int x, int y, int width, int height,
                  int disposal, int transparent, bool interlaced, int index) {
    GraphicsControlBlock control = {disposal, false, 3, transparent};
    GifByteType block[4];
    EGifGCBToExtension(&control, block);
    g_assert_cmpint(EGifPutExtension(file, GRAPHICS_EXT_FUNC_CODE, 4, block), ==, GIF_OK);
    g_assert_cmpint(EGifPutImageDesc(file, x, y, width, height, interlaced, NULL), ==, GIF_OK);
    GifPixelType row[8];
    const int starts[] = {0, 4, 2, 1}, steps[] = {8, 8, 4, 2};
    for (int pass = 0; pass < (interlaced ? 4 : 1); pass++) {
        for (int line = interlaced ? starts[pass] : 0; line < height;
             line += interlaced ? steps[pass] : 1) {
            memset(row, interlaced ? 1 + line % 3 : index, width);
            if (transparent >= 0 && x == 0 && y == 0) row[line % width] = transparent;
            g_assert_cmpint(EGifPutLine(file, row, width), ==, GIF_OK);
        }
    }
}

static void pixel(GifStream *stream, int x, int y, int r, int g, int b, int a) {
    guchar *p = gdk_pixbuf_get_pixels(stream->frame) +
        y * gdk_pixbuf_get_rowstride(stream->frame) + x * 4;
    g_assert_cmpint(p[0], ==, r); g_assert_cmpint(p[1], ==, g);
    g_assert_cmpint(p[2], ==, b); g_assert_cmpint(p[3], ==, a);
}

int main(void) {
    const char *path = "build/gif-disposal.gif";
    GifColorType colors[] = {{0,0,0}, {255,0,0}, {0,255,0}, {0,0,255}};
    ColorMapObject *map = GifMakeMapObject(4, colors);
    GifFileType *file = EGifOpenFileName(path, false, NULL);
    g_assert_nonnull(file);
    EGifSetGifVersion(file, true);
    g_assert_cmpint(EGifPutScreenDesc(file, 8, 8, 2, 0, map), ==, GIF_OK);
    GifFreeMapObject(map);
    GifByteType repeat[] = {1, 1, 0};
    g_assert_cmpint(EGifPutExtensionLeader(file, APPLICATION_EXT_FUNC_CODE), ==, GIF_OK);
    g_assert_cmpint(EGifPutExtensionBlock(file, 11, "NETSCAPE2.0"), ==, GIF_OK);
    g_assert_cmpint(EGifPutExtensionBlock(file, 3, repeat), ==, GIF_OK);
    g_assert_cmpint(EGifPutExtensionTrailer(file), ==, GIF_OK);
    frame(file, 0, 0, 8, 8, DISPOSE_DO_NOT, -1, false, 1);
    frame(file, 0, 0, 2, 2, DISPOSE_PREVIOUS, 0, false, 3);
    frame(file, 4, 4, 2, 2, DISPOSE_BACKGROUND, 0, false, 2);
    frame(file, 7, 7, 1, 1, DISPOSE_DO_NOT, -1, false, 3);
    frame(file, 0, 0, 8, 8, DISPOSE_DO_NOT, -1, true, 0);
    g_assert_cmpint(EGifCloseFile(file, NULL), ==, GIF_OK);
    g_assert_true(gif_stream_is_file(path));
    GifStream *stream = gif_stream_open(path);
    g_assert_nonnull(stream);
    for (int cycle = 0; cycle < 2; cycle++) {
        for (int i = 0; i < 5; i++) {
            g_assert_cmpint(gif_stream_next(stream), ==, 1);
            g_assert_cmpint(stream->delay, ==, 30);
            g_assert_cmpint(stream->file->ImageCount, ==, 0);
            g_assert_null(stream->file->SavedImages);
            if (i == 0) pixel(stream, 7, 7, 255, 0, 0, 255);
            if (i == 1) {
                pixel(stream, 0, 0, 255, 0, 0, 255);
                pixel(stream, 1, 0, 0, 0, 255, 255);
            }
            if (i == 2) {
                pixel(stream, 1, 0, 255, 0, 0, 255);
                pixel(stream, 4, 4, 0, 255, 0, 255);
            }
            if (i == 3) {
                pixel(stream, 4, 4, 0, 0, 0, 0);
                pixel(stream, 7, 7, 0, 0, 255, 255);
            }
            if (i == 4) for (int y = 0; y < 8; y++) {
                int c = y % 3;
                pixel(stream, 0, y, c == 0 ? 255 : 0, c == 1 ? 255 : 0,
                    c == 2 ? 255 : 0, 255);
            }
        }
    }
    g_assert_cmpint(gif_stream_next(stream), ==, 0);
    gif_stream_free(stream);
    /* A single-frame GIF without a looping extension stops after one frame,
     * and its local palette must override the logical screen palette. */
    path = "build/gif-local.gif";
    file = EGifOpenFileName(path, false, NULL);
    g_assert_nonnull(file);
    map = GifMakeMapObject(4, colors);
    g_assert_cmpint(EGifPutScreenDesc(file, 8, 8, 2, 0, map), ==, GIF_OK);
    GifFreeMapObject(map);
    colors[1] = (GifColorType){0, 0, 255};
    map = GifMakeMapObject(4, colors);
    g_assert_cmpint(EGifPutImageDesc(file, 0, 0, 8, 8, false, map), ==, GIF_OK);
    GifFreeMapObject(map);
    GifPixelType row[8];
    memset(row, 1, sizeof(row));
    for (int i = 0; i < 8; i++) g_assert_cmpint(EGifPutLine(file, row, 8), ==, GIF_OK);
    g_assert_cmpint(EGifCloseFile(file, NULL), ==, GIF_OK);
    stream = gif_stream_open(path);
    g_assert_nonnull(stream);
    g_assert_cmpint(gif_stream_next(stream), ==, 1);
    pixel(stream, 0, 0, 0, 0, 255, 255);
    g_assert_cmpint(gif_stream_next(stream), ==, 0);
    gif_stream_free(stream);
    FILE *source = fopen(path, "rb");
    FILE *broken = fopen("build/gif-truncated.gif", "wb");
    g_assert_nonnull(source); g_assert_nonnull(broken);
    unsigned char bytes[40];
    g_assert_cmpuint(fread(bytes, 1, sizeof(bytes), source), ==, sizeof(bytes));
    g_assert_cmpuint(fwrite(bytes, 1, sizeof(bytes), broken), ==, sizeof(bytes));
    fclose(source); fclose(broken);
    stream = gif_stream_open("build/gif-truncated.gif");
    if (stream) {
        g_assert_cmpint(gif_stream_next(stream), ==, -1);
        gif_stream_free(stream);
    }
    g_assert_null(gif_stream_open("/nonexistent/test.gif"));
    g_print("PASS: GIF transparency, partial frames, disposal 2/3, interlacing, finite loops, local palette, malformed input, bounded cache\n");
    return 0;
}
