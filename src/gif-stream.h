/* Incremental GIF decoding: one composited canvas and, only for disposal 3,
 * one saved canvas. No decoded-frame cache grows with animation length. */
#include <gif_lib.h>
#include <stdio.h>

typedef struct {
    GifFileType *file;
    char *path;
    GdkPixbuf *frame, *restore;
    GifImageDesc previous;
    GraphicsControlBlock control;
    int loops; /* Additional repetitions; -1 means infinite. */
    bool first_cycle, first_frame;
    int delay;
} GifStream;

static bool gif_stream_is_file(const char *path) {
    unsigned char signature[6];
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    size_t count = fread(signature, 1, sizeof(signature), file);
    fclose(file);
    return count == 6 && (!memcmp(signature, "GIF87a", 6) || !memcmp(signature, "GIF89a", 6));
}

static void gif_stream_free(GifStream *stream) {
    if (!stream) return;
    if (stream->file) DGifCloseFile(stream->file, NULL);
    g_clear_object(&stream->frame);
    g_clear_object(&stream->restore);
    g_free(stream->path);
    g_free(stream);
}

static GifStream *gif_stream_open(const char *path) {
    GifStream *stream = g_new0(GifStream, 1);
    stream->file = DGifOpenFileName(path, NULL);
    if (!stream->file) { gif_stream_free(stream); return NULL; }
    stream->path = g_strdup(path);
    stream->first_cycle = stream->first_frame = true;
    stream->frame = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8,
        stream->file->SWidth, stream->file->SHeight);
    if (!stream->frame) { gif_stream_free(stream); return NULL; }
    gdk_pixbuf_fill(stream->frame, 0);
    return stream;
}

static void gif_stream_background(GifStream *stream, int x, int y, int width, int height,
                                  int transparent) {
    GifFileType *file = stream->file;
    guchar color[4] = {0, 0, 0, 0};
    if (transparent == NO_TRANSPARENT_COLOR && file->SColorMap &&
        file->SBackGroundColor < file->SColorMap->ColorCount) {
        GifColorType c = file->SColorMap->Colors[file->SBackGroundColor];
        color[0] = c.Red; color[1] = c.Green; color[2] = c.Blue; color[3] = 255;
    }
    for (int row = y; row < y + height; row++) {
        guchar *pixels = gdk_pixbuf_get_pixels(stream->frame) +
            row * gdk_pixbuf_get_rowstride(stream->frame) + x * 4;
        for (int column = 0; column < width; column++) memcpy(pixels + column * 4, color, 4);
    }
}

/* Returns 1 for a decoded frame, 0 at normal end, -1 on malformed input. */
static int gif_stream_next(GifStream *stream) {
    GraphicsControlBlock control = {DISPOSAL_UNSPECIFIED, false, 10, NO_TRANSPARENT_COLOR};
    bool restarted = false;
    for (;;) {
        GifFileType *file = stream->file;
        GifRecordType type;
        if (DGifGetRecordType(file, &type) == GIF_ERROR) return -1;
        if (type == TERMINATE_RECORD_TYPE) {
            if (!stream->loops || restarted || stream->first_frame) return 0;
            if (stream->loops > 0) stream->loops--;
            DGifCloseFile(stream->file, NULL);
            stream->file = DGifOpenFileName(stream->path, NULL);
            if (!stream->file) return -1;
            if (stream->file->SWidth != gdk_pixbuf_get_width(stream->frame) ||
                stream->file->SHeight != gdk_pixbuf_get_height(stream->frame)) return -1;
            stream->first_cycle = false;
            stream->first_frame = true;
            g_clear_object(&stream->restore);
            restarted = true;
            control = (GraphicsControlBlock){DISPOSAL_UNSPECIFIED, false, 10, NO_TRANSPARENT_COLOR};
            continue;
        }
        if (type == EXTENSION_RECORD_TYPE) {
            int code;
            GifByteType *block;
            if (DGifGetExtension(file, &code, &block) == GIF_ERROR) return -1;
            bool looping = code == APPLICATION_EXT_FUNC_CODE && block && block[0] == 11 &&
                (!memcmp(block + 1, "NETSCAPE2.0", 11) || !memcmp(block + 1, "ANIMEXTS1.0", 11));
            if (code == GRAPHICS_EXT_FUNC_CODE && block && block[0] == 4 &&
                DGifExtensionToGCB(4, block + 1, &control) == GIF_ERROR) return -1;
            while (block) {
                if (DGifGetExtensionNext(file, &block) == GIF_ERROR) return -1;
                if (looping && stream->first_cycle && block && block[0] >= 3 && block[1] == 1) {
                    int loops = block[2] | (block[3] << 8);
                    stream->loops = loops ? loops : -1;
                }
            }
            continue;
        }
        if (type != IMAGE_DESC_RECORD_TYPE || DGifGetImageDesc(file) == GIF_ERROR) return -1;
        GifImageDesc image = file->Image;
        if (image.Width <= 0 || image.Height <= 0 || image.Left < 0 || image.Top < 0 ||
            image.Left + image.Width > file->SWidth || image.Top + image.Height > file->SHeight) return -1;
        ColorMapObject *map = image.ColorMap ? image.ColorMap : file->SColorMap;
        if (!map) return -1;
        if (stream->first_frame) {
            gif_stream_background(stream, 0, 0, file->SWidth, file->SHeight, control.TransparentColor);
        } else if (stream->control.DisposalMode == DISPOSE_BACKGROUND) {
            gif_stream_background(stream, stream->previous.Left, stream->previous.Top,
                stream->previous.Width, stream->previous.Height, stream->control.TransparentColor);
        } else if (stream->control.DisposalMode == DISPOSE_PREVIOUS && stream->restore) {
            gdk_pixbuf_copy_area(stream->restore, 0, 0, file->SWidth, file->SHeight, stream->frame, 0, 0);
        }
        g_clear_object(&stream->restore);
        if (control.DisposalMode == DISPOSE_PREVIOUS) {
            stream->restore = gdk_pixbuf_copy(stream->frame);
            if (!stream->restore) return -1;
        }
        GifPixelType *line = g_try_malloc(image.Width);
        if (!line) return -1;
        const int starts[] = {0, 4, 2, 1}, steps[] = {8, 8, 4, 2};
        bool valid = true;
        for (int pass = 0; pass < (image.Interlace ? 4 : 1) && valid; pass++) {
            int start = image.Interlace ? starts[pass] : 0;
            int step = image.Interlace ? steps[pass] : 1;
            for (int y = start; y < image.Height && valid; y += step) {
                if (DGifGetLine(file, line, image.Width) == GIF_ERROR) { valid = false; break; }
                guchar *pixels = gdk_pixbuf_get_pixels(stream->frame) +
                    (image.Top + y) * gdk_pixbuf_get_rowstride(stream->frame) + image.Left * 4;
                for (int x = 0; x < image.Width; x++) {
                    int index = line[x];
                    if (index == control.TransparentColor) continue;
                    if (index >= map->ColorCount) { valid = false; break; }
                    GifColorType c = map->Colors[index];
                    pixels[x * 4] = c.Red; pixels[x * 4 + 1] = c.Green;
                    pixels[x * 4 + 2] = c.Blue; pixels[x * 4 + 3] = 255;
                }
            }
        }
        g_free(line);
        /* giflib's low-level descriptor reader also retains metadata. */
        GifFreeSavedImages(file);
        file->ImageCount = 0;
        if (!valid) return -1;
        stream->previous = image;
        stream->previous.ColorMap = NULL;
        stream->control = control;
        stream->first_frame = false;
        stream->delay = MAX(control.DelayTime * 10, 20);
        return 1;
    }
}
