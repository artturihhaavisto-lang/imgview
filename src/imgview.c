#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <gdk/gdkkeysyms.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>
#include "gif-stream.h"

#define ZOOM_STEP 1.25
#define ZOOM_MIN 0.005
#define ZOOM_MAX 32.0
#define DRAG_THRESHOLD 5.0
#define VOLUME_MAX 2.0
#define VOLUME_STEP 0.02

static const char *CSS =
    "window { background: #0e0e0e; }"
    "window decoration { box-shadow: none; border: 1px solid #242424; border-radius: 0; }"
    "#app-header {"
    "  background: #101010; background-image: none; color: #aaa;"
    "  border: none; border-bottom: 1px solid #242424; border-radius: 0;"
    "  box-shadow: none; min-height: 24px; padding: 0 3px 0 8px;"
    "}"
    "#app-header label { font-family: monospace; font-size: 11px; font-weight: normal; }"
    "#app-brand { color: #666; }"
    "#app-header button {"
    "  background: transparent; background-image: none; color: #888;"
    "  border: none; border-radius: 2px; box-shadow: none;"
    "  min-width: 22px; min-height: 20px; padding: 0; margin: 1px;"
    "}"
    "#app-header button:hover { background: #282828; color: #ddd; }"
    "#app-header button:active { background: #333; }"
    "#app-header button label { font-size: 14px; }"
    "#app-header button.close:hover { background: #542e2e; color: #eee; }"
    "#metadata-panel { background: rgba(16,16,16,0.98); border-left: 1px solid #303030; padding: 14px; }"
    "#metadata-panel label { font-family: monospace; font-size: 11px; color: #999; }"
    "#metadata-panel #metadata-title { color: #ddd; font-size: 12px; }"
    "#playlist-panel { background: rgba(16,16,16,0.98); border-right: 1px solid #303030; }"
    "#playlist-panel label { font-family: monospace; font-size: 11px; color: #aaa; }"
    "#playlist-panel list { background: transparent; }"
    "#playlist-panel row { padding: 5px 8px; border-bottom: 1px solid #202020; }"
    "#playlist-panel row:hover { background: #242424; }"
    "#playlist-panel row:selected { background: #303030; }"
    "#playlist-panel #playlist-type { color: #666; font-size: 10px; }"
    "#volume-level trough { background: #282828; border: none; padding: 0; min-height: 3px; }"
    "#volume-level block { border: none; border-radius: 0; min-height: 3px; box-shadow: none; }"
    "#volume-level block.filled { background: #888; background-image: none; }"
    "#volume-level block.empty { background: #282828; background-image: none; }"
    "#statusbar {"
    "  background: rgba(12,12,12,0.95);"
    "  border-top: 1px solid #202020;"
    "  padding: 1px 8px;"
    "}"
    "#statusbar label {"
    "  font-family: monospace;"
    "  font-size: 11px;"
    "  color: #666;"
    "}"
    "#st-name { color: #bbb; }"
    "#st-index { color: #555; }"
    "#st-dims { color: #666; }"
    "#st-zoom { color: #888; min-width: 34px; }"
    "#video-progress { padding: 3px 0; min-height: 6px; }"
    "#video-progress trough, #video-progress highlight, #video-progress slider {"
    "  background-image: none; border: none; box-shadow: none;"
    "  outline: none; border-radius: 0;"
    "}"
    "#video-progress trough { min-height: 2px; background-color: #282828; }"
    "#video-progress highlight { min-height: 2px; background-color: #777; }"
    "#video-progress slider {"
    "  min-width: 6px; min-height: 6px; margin: -2px 0; background-color: #999;"
    "}"
    "#video-progress:hover highlight { background-color: #999; }"
    "#video-progress slider:hover, #video-progress slider:active { background-color: #ccc; }"
    "#video-progress:disabled highlight, #video-progress:disabled slider { background-color: #444; }";

typedef struct {
    GtkWidget *area;
    GdkPixbuf *pixbuf;
    GdkPixbufAnimation *animation;
    GdkPixbufAnimationIter *animation_iter;
    GifStream *gif_stream;
    guint animation_id;
    GstElement *player;
    GstElement *video_sink;
    GstTagList *tags;
    double video_fps;
    bool audio_only;
    GstBus *video_bus;
    GdkPixbuf *video_frame;
    guint video_id;
    bool paused;
    bool ended;
    bool muted;
    double volume;
    int rotation;
    bool flipped;
    bool fullscreen;
    cairo_surface_t *image_surface;
    cairo_surface_t *scaled_surface;
    double scaled_zoom;
    int scaled_device_scale;
    cairo_pattern_t *checker_pattern;
    double zoom;
    double video_fit_scale;
    double ox;
    double oy;
    bool dragging;
    bool drag_moved;
    double drag_x;
    double drag_y;
    double drag_ox;
    double drag_oy;
} Canvas;

typedef struct {
    GtkWidget *name;
    GtkWidget *index;
    GtkWidget *dims;
    GtkWidget *zoom;
    GtkWidget *playback;
    GtkWidget *progress;
    GtkWidget *volume_label;
    GtkWidget *volume_level;
    GtkWidget *volume_box;
    bool scrubbing;
    GtkWidget *box;
} StatusBar;

typedef struct {
    GtkWidget *window;
    GtkWidget *header;
    GtkWidget *fullscreen_header;
    GtkWidget *header_revealer;
    GtkWidget *bottom_revealer;
    GtkWidget *metadata_revealer;
    GtkWidget *metadata_label;
    char *metadata_file_text;
    gint64 metadata_refresh_time;
    GtkWidget *playlist_revealer, *playlist_list, *playlist_scroll;
    GPtrArray *playlist_paths;
    char *playlist_dir;
    guint thumbnail_next;
    GtkWidget *overlay;
    GtkWidget *content_box;
    GtkWidget *status_host;
    guint hover_id;
    bool windowed_status_visible;
    Canvas canvas;
    StatusBar status;
    GPtrArray *paths;
    char *pending_scan_dir;
    char *pending_scan_path;
    guint index;
    guint slideshow_id;
    guint scan_id;
    bool fullscreen;
    int window_width, window_height;
    int window_x, window_y;
    bool has_window_position;
    bool window_maximized;
    guint window_save_id;
    guint native_resize_id;
    gint64 native_resize_deadline;
} ImgView;

static void playlist_refresh(ImgView *view);
static void playlist_thumbnails(ImgView *view);

static bool is_video_ext(const char *path) {
    const char *ext = strrchr(path, '.');
    static const char *exts[] = {
        ".mp4", ".m4v", ".mkv", ".webm", ".mov", ".avi", ".ogv",
        ".mpeg", ".mpg", ".m2v", ".ts", ".mts", ".m2ts", ".wmv", ".flv", ".3gp",
        ".mpe", ".m1v", ".m2p", ".m2t", ".vob", ".mxf", ".asf", ".f4v",
        ".3g2", ".divx", ".qt", ".rm", ".rmvb", ".dv", ".nut", ".ivf",
        ".h264", ".264", ".h265", ".265", ".hevc", ".av1", NULL
    };
    for (int i = 0; ext && exts[i]; i++) {
        if (g_ascii_strcasecmp(ext, exts[i]) == 0) return true;
    }
    return false;
}

static bool is_audio_ext(const char *path) {
    const char *ext = strrchr(path, '.');
    const char *exts[] = {".mp3", ".flac", ".wav", ".ogg", ".oga", ".opus",
        ".m4a", ".aac", ".aiff", ".aif", ".wma", ".alac", ".ape", NULL};
    for (int i = 0; ext && exts[i]; i++)
        if (!g_ascii_strcasecmp(ext, exts[i])) return true;
    return false;
}

static bool is_image_ext(const char *path) {
    if (is_video_ext(path) || is_audio_ext(path)) return true;
    const char *ext = strrchr(path, '.');
    if (!ext) {
        return false;
    }

    static const char *exts[] = {
        ".png", ".jpg", ".jpeg", ".gif", ".webp", ".bmp",
        ".tiff", ".tif", ".ico", ".xpm", ".ppm", ".pgm", ".pbm",
        NULL
    };

    for (int i = 0; exts[i]; i++) {
        if (g_ascii_strcasecmp(ext, exts[i]) == 0) {
            return true;
        }
    }
    /* Include every format provided by the system's image loaders. Cache the
     * extension set once so directory scanning stays cheap. */
    static gsize supported_extensions;
    if (g_once_init_enter(&supported_extensions)) {
        GHashTable *extensions = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
        GSList *formats = gdk_pixbuf_get_formats();
        for (GSList *item = formats; item; item = item->next) {
            if (gdk_pixbuf_format_is_disabled(item->data)) continue;
            char **names = gdk_pixbuf_format_get_extensions(item->data);
            for (int i = 0; names[i]; i++)
                g_hash_table_add(extensions, g_ascii_strdown(names[i], -1));
            g_strfreev(names);
        }
        g_slist_free(formats);
        g_once_init_leave(&supported_extensions, (gsize)extensions);
    }
    const char *base = strrchr(path, G_DIR_SEPARATOR);
    const char *suffix = strchr(base ? base + 1 : path, '.');
    for (; suffix; suffix = strchr(suffix + 1, '.')) {
        char *lower = g_ascii_strdown(suffix + 1, -1);
        bool supported = g_hash_table_contains((GHashTable *)supported_extensions, lower);
        g_free(lower);
        if (supported) return true;
    }
    return false;
}

static bool is_image_file(const char *path) {
    return is_image_ext(path) && g_file_test(path, G_FILE_TEST_IS_REGULAR);
}

static const char *path_basename_ptr(const char *path) {
    const char *base = strrchr(path, G_DIR_SEPARATOR);
    return base ? base + 1 : path;
}

static gint compare_paths(gconstpointer a, gconstpointer b) {
    const char *pa = *(const char * const *)a;
    const char *pb = *(const char * const *)b;
    return g_ascii_strcasecmp(path_basename_ptr(pa), path_basename_ptr(pb));
}

static GPtrArray *new_path_array(void) {
    return g_ptr_array_new_with_free_func(g_free);
}

static void add_dir_images(GPtrArray *paths, const char *dir) {
    GDir *gdir = g_dir_open(dir, 0, NULL);
    if (!gdir) {
        return;
    }

    const char *name = NULL;
    while ((name = g_dir_read_name(gdir)) != NULL) {
        if (!is_image_ext(name)) {
            continue;
        }
        char *path = g_build_filename(dir, name, NULL);
        if (g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
            g_ptr_array_add(paths, path);
        } else {
            g_free(path);
        }
    }
    g_dir_close(gdir);
    g_ptr_array_sort(paths, compare_paths);
}

static void resolve_args(
    int argc,
    char **argv,
    GPtrArray **out_paths,
    guint *out_start,
    char **out_pending_scan_dir,
    char **out_pending_scan_path
) {
    GPtrArray *paths = new_path_array();
    *out_start = 0;
    *out_pending_scan_dir = NULL;
    *out_pending_scan_path = NULL;

    if (argc <= 1) {
        *out_paths = paths;
        return;
    }

    if (argc == 2) {
        char *abs = g_canonicalize_filename(argv[1], NULL);
        if (g_file_test(abs, G_FILE_TEST_IS_DIR)) {
            add_dir_images(paths, abs);
            g_free(abs);
            *out_paths = paths;
            return;
        }

        if (is_image_file(abs)) {
            *out_pending_scan_dir = g_path_get_dirname(abs);
            *out_pending_scan_path = g_strdup(abs);
            g_ptr_array_add(paths, abs);
            *out_paths = paths;
            return;
        }
        g_free(abs);
    }

    for (int i = 1; i < argc; i++) {
        char *abs = g_canonicalize_filename(argv[i], NULL);
        if (g_file_test(abs, G_FILE_TEST_IS_DIR)) {
            add_dir_images(paths, abs);
            g_free(abs);
        } else if (is_image_file(abs)) {
            g_ptr_array_add(paths, abs);
        } else {
            g_free(abs);
        }
    }

    *out_paths = paths;
}

static double clamp_double(double value, double min, double max) {
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static cairo_pattern_t *create_checker_pattern(void) {
    const int sq = 20;
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, sq * 2, sq * 2);
    cairo_t *cr = cairo_create(surface);

    cairo_set_source_rgb(cr, 0.095, 0.095, 0.095);
    cairo_rectangle(cr, 0, 0, sq, sq);
    cairo_rectangle(cr, sq, sq, sq, sq);
    cairo_fill(cr);

    cairo_set_source_rgb(cr, 0.065, 0.065, 0.065);
    cairo_rectangle(cr, sq, 0, sq, sq);
    cairo_rectangle(cr, 0, sq, sq, sq);
    cairo_fill(cr);

    cairo_destroy(cr);

    cairo_pattern_t *pattern = cairo_pattern_create_for_surface(surface);
    cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);
    cairo_surface_destroy(surface);
    return pattern;
}

static void canvas_clear_image(Canvas *canvas) {
    canvas->dragging = false;
    canvas->drag_moved = false;
    if (canvas->video_id) {
        g_source_remove(canvas->video_id);
        canvas->video_id = 0;
    }
    if (canvas->player) gst_element_set_state(canvas->player, GST_STATE_NULL);
    g_clear_object(&canvas->video_bus);
    g_clear_object(&canvas->player);
    g_clear_object(&canvas->video_sink);
    g_clear_pointer(&canvas->tags, gst_tag_list_unref);
    canvas->video_fps = 0;
    canvas->audio_only = false;
    canvas->video_fit_scale = 0;
    g_clear_object(&canvas->video_frame);
    canvas->paused = false;
    canvas->ended = false;
    if (canvas->animation_id) {
        g_source_remove(canvas->animation_id);
        canvas->animation_id = 0;
    }
    g_clear_object(&canvas->animation_iter);
    g_clear_object(&canvas->animation);
    g_clear_pointer(&canvas->gif_stream, gif_stream_free);
    g_clear_object(&canvas->pixbuf);
    g_clear_pointer(&canvas->image_surface, cairo_surface_destroy);
    g_clear_pointer(&canvas->scaled_surface, cairo_surface_destroy);
    canvas->rotation = 0;
    canvas->flipped = false;
}

static void canvas_update_surface(Canvas *canvas) {
    g_clear_pointer(&canvas->scaled_surface, cairo_surface_destroy);
    g_clear_pointer(&canvas->image_surface, cairo_surface_destroy);
    if (!canvas->pixbuf) {
        return;
    }

    GdkWindow *window = gtk_widget_get_window(canvas->area);
    canvas->image_surface = gdk_cairo_surface_create_from_pixbuf(canvas->pixbuf, 1, window);
}

static void canvas_fit(Canvas *canvas, bool force) {
    if (!canvas->pixbuf) {
        return;
    }

    GtkAllocation alloc;
    gtk_widget_get_allocation(canvas->area, &alloc);
    if (alloc.width <= 1 || alloc.height <= 1) {
        return;
    }

    int pw = gdk_pixbuf_get_width(canvas->pixbuf);
    int ph = gdk_pixbuf_get_height(canvas->pixbuf);
    double scale = fmin((double)alloc.width / pw, (double)alloc.height / ph);
    bool locked = canvas->fullscreen && canvas->player;
    if (canvas->player) {
        /* Keep the user's zoom relative to a fitted video, rather than to
         * fixed source pixels, as the viewing area grows or shrinks. */
        if (force || locked || canvas->video_fit_scale <= 0)
            canvas->zoom = scale;
        else canvas->zoom *= scale / canvas->video_fit_scale;
        canvas->video_fit_scale = scale;
    } else {
        scale = fmin(scale, 1.0);
        if (force || pw * canvas->zoom > alloc.width || ph * canvas->zoom > alloc.height)
            canvas->zoom = scale;
    }
    canvas->ox = (alloc.width - pw * canvas->zoom) / 2.0;
    canvas->oy = (alloc.height - ph * canvas->zoom) / 2.0;
    gtk_widget_queue_draw(canvas->area);
}

/* GTK3's animation API is deprecated in recent GdkPixbuf versions, but
 * remains available and keeps playback compatible with older GTK3 systems. */
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
static void canvas_update_frame(Canvas *canvas) {
    GdkPixbuf *frame = canvas->video_frame ? canvas->video_frame : canvas->gif_stream
        ? canvas->gif_stream->frame : canvas->animation_iter
        ? gdk_pixbuf_animation_iter_get_pixbuf(canvas->animation_iter)
        : gdk_pixbuf_animation_get_static_image(canvas->animation);
    GdkPixbuf *transformed = canvas->rotation
        ? gdk_pixbuf_rotate_simple(frame, canvas->rotation) : g_object_ref(frame);
    if (canvas->flipped) {
        GdkPixbuf *flipped = gdk_pixbuf_flip(transformed, TRUE);
        g_object_unref(transformed);
        transformed = flipped;
    }
    g_clear_object(&canvas->pixbuf);
    canvas->pixbuf = transformed;
    canvas_update_surface(canvas);
    gtk_widget_queue_draw(canvas->area);
}

static gboolean canvas_animation_tick(gpointer data);

static void canvas_schedule_frame(Canvas *canvas) {
    int delay = canvas->gif_stream ? canvas->gif_stream->delay
        : gdk_pixbuf_animation_iter_get_delay_time(canvas->animation_iter);
    if (delay >= 0) {
        canvas->animation_id = g_timeout_add(MAX(delay, 1), canvas_animation_tick, canvas);
    }
}

static gboolean canvas_animation_tick(gpointer data) {
    Canvas *canvas = data;
    canvas->animation_id = 0;
    if (canvas->gif_stream) {
        int next = gif_stream_next(canvas->gif_stream);
        if (next > 0) {
            canvas_update_frame(canvas);
            canvas_schedule_frame(canvas);
        } else if (next < 0) {
            g_warning("GIF decoding stopped: malformed or unreadable animation");
        }
        return G_SOURCE_REMOVE;
    }
    if (gdk_pixbuf_animation_iter_advance(canvas->animation_iter, NULL)) {
        canvas_update_frame(canvas);
    }
    canvas_schedule_frame(canvas);
    return G_SOURCE_REMOVE;
}

static char *canvas_load(Canvas *canvas, const char *path) {
    if (gif_stream_is_file(path)) {
        canvas_clear_image(canvas);
        canvas->gif_stream = gif_stream_open(path);
        if (!canvas->gif_stream || gif_stream_next(canvas->gif_stream) != 1) {
            canvas_clear_image(canvas);
            gtk_widget_queue_draw(canvas->area);
            return g_strdup("Could not decode GIF");
        }
        canvas_update_frame(canvas);
        canvas->zoom = 1.0;
        canvas_fit(canvas, true);
        canvas_schedule_frame(canvas);
        return NULL;
    }
    GError *error = NULL;
    GdkPixbufAnimation *animation = gdk_pixbuf_animation_new_from_file(path, &error);

    if (!animation) {
        canvas_clear_image(canvas);
        gtk_widget_queue_draw(canvas->area);
        char *message = g_strdup(error ? error->message : "failed to load image");
        g_clear_error(&error);
        return message;
    }

    canvas_clear_image(canvas);
    canvas->animation = animation;
    if (!gdk_pixbuf_animation_is_static_image(animation)) {
        canvas->animation_iter = gdk_pixbuf_animation_get_iter(animation, NULL);
        canvas_schedule_frame(canvas);
    }
    canvas_update_frame(canvas);
    canvas->zoom = 1.0;
    canvas_fit(canvas, true);
    return NULL;
}
G_GNUC_END_IGNORE_DEPRECATIONS

static void canvas_actual_size(Canvas *canvas) {
    if (canvas->fullscreen && canvas->player) return;
    if (!canvas->pixbuf) {
        return;
    }

    GtkAllocation alloc;
    gtk_widget_get_allocation(canvas->area, &alloc);
    canvas->zoom = 1.0;
    canvas->ox = (alloc.width - gdk_pixbuf_get_width(canvas->pixbuf)) / 2.0;
    canvas->oy = (alloc.height - gdk_pixbuf_get_height(canvas->pixbuf)) / 2.0;
    gtk_widget_queue_draw(canvas->area);
}

static void canvas_zoom_step(Canvas *canvas, double factor, double cx, double cy) {
    if (canvas->fullscreen && canvas->player) return;
    if (!canvas->pixbuf) {
        return;
    }

    GtkAllocation alloc;
    gtk_widget_get_allocation(canvas->area, &alloc);
    if (cx < 0) {
        cx = alloc.width / 2.0;
    }
    if (cy < 0) {
        cy = alloc.height / 2.0;
    }

    double ix = (cx - canvas->ox) / canvas->zoom;
    double iy = (cy - canvas->oy) / canvas->zoom;
    canvas->zoom = clamp_double(canvas->zoom * factor, ZOOM_MIN, ZOOM_MAX);
    canvas->ox = cx - ix * canvas->zoom;
    canvas->oy = cy - iy * canvas->zoom;
    gtk_widget_queue_draw(canvas->area);
}

static void canvas_rotate(Canvas *canvas, bool clockwise) {
    if (!canvas->pixbuf) {
        return;
    }

    /* A horizontal reflection reverses the direction of rotation. */
    int delta = clockwise != canvas->flipped ? 270 : 90;
    canvas->rotation = (canvas->rotation + delta) % 360;
    canvas_update_frame(canvas);
    canvas_fit(canvas, true);
}

static void canvas_flip_h(Canvas *canvas) {
    if (!canvas->pixbuf) {
        return;
    }

    canvas->flipped = !canvas->flipped;
    canvas_update_frame(canvas);
}

/* Resample still images once per zoom level, then pan with pixel-aligned
 * copies. Bound the extra RAM to 32 MiB; very large zooms use the clipped
 * original surface instead. Animated media never builds this extra copy. */
static void canvas_prepare_scaled(Canvas *canvas, int device_scale) {
    if (canvas->scaled_surface && canvas->scaled_zoom == canvas->zoom &&
        canvas->scaled_device_scale == device_scale) return;
    g_clear_pointer(&canvas->scaled_surface, cairo_surface_destroy);
    if (!canvas->image_surface || canvas->player || canvas->gif_stream ||
        canvas->animation_iter || canvas->zoom * device_scale == 1.0) return;
    double w = ceil(gdk_pixbuf_get_width(canvas->pixbuf) * canvas->zoom * device_scale);
    double h = ceil(gdk_pixbuf_get_height(canvas->pixbuf) * canvas->zoom * device_scale);
    if (w < 1 || h < 1 || w * h > 8 * 1024 * 1024) return;
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int)w, (int)h);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        return;
    }
    cairo_surface_set_device_scale(surface, device_scale, device_scale);
    cairo_t *cr = cairo_create(surface);
    cairo_scale(cr, canvas->zoom, canvas->zoom);
    cairo_set_source_surface(cr, canvas->image_surface, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
    cairo_paint(cr);
    cairo_destroy(cr);
    canvas->scaled_surface = surface;
    canvas->scaled_zoom = canvas->zoom;
    canvas->scaled_device_scale = device_scale;
}

static gboolean canvas_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    Canvas *canvas = data;
    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);

    if (canvas->fullscreen && canvas->player)
        cairo_set_source_rgb(cr, 0, 0, 0);
    else
        cairo_set_source(cr, canvas->checker_pattern);
    cairo_rectangle(cr, 0, 0, alloc.width, alloc.height);
    cairo_fill(cr);

    if (canvas->audio_only && canvas->player) {
        cairo_set_source_rgb(cr, 0.055, 0.055, 0.055);
        cairo_paint(cr);
        return TRUE;
    }
    if (!canvas->pixbuf) {
        const char *msg = canvas->player ? "Loading video..." : "No media";
        cairo_text_extents_t extents;
        cairo_set_source_rgba(cr, 0.45, 0.45, 0.45, 0.7);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 16);
        cairo_text_extents(cr, msg, &extents);
        cairo_move_to(cr, (alloc.width - extents.width) / 2.0, (alloc.height + extents.height) / 2.0);
        cairo_show_text(cr, msg);
        return TRUE;
    }

    int device_scale = gtk_widget_get_scale_factor(widget);
    canvas_prepare_scaled(canvas, device_scale);
    cairo_save(cr);
    cairo_translate(cr, round(canvas->ox * device_scale) / device_scale,
                    round(canvas->oy * device_scale) / device_scale);
    if (canvas->scaled_surface) {
        cairo_set_source_surface(cr, canvas->scaled_surface, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    } else {
        cairo_scale(cr, canvas->zoom, canvas->zoom);
        cairo_set_source_surface(cr, canvas->image_surface, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
    }
    cairo_paint(cr);
    cairo_restore(cr);
    return TRUE;
}

static void video_toggle_pause(Canvas *canvas);

static bool canvas_drag_distance(Canvas *canvas, double x, double y) {
    return fabs(x - canvas->drag_x) > DRAG_THRESHOLD || fabs(y - canvas->drag_y) > DRAG_THRESHOLD;
}

static void canvas_damage_media(Canvas *canvas) {
    if (!canvas->pixbuf) return;
    GtkAllocation a;
    gtk_widget_get_allocation(canvas->area, &a);
    /* Clip in double precision before converting: high zoom can put bounds
     * far outside the window. Include filtering/rounding at the edges. */
    double x = clamp_double(floor(canvas->ox) - 2, 0, a.width);
    double y = clamp_double(floor(canvas->oy) - 2, 0, a.height);
    double right = clamp_double(ceil(canvas->ox + gdk_pixbuf_get_width(canvas->pixbuf) * canvas->zoom) + 2, 0, a.width);
    double bottom = clamp_double(ceil(canvas->oy + gdk_pixbuf_get_height(canvas->pixbuf) * canvas->zoom) + 2, 0, a.height);
    if (right > x && bottom > y)
        gtk_widget_queue_draw_area(canvas->area, (int)x, (int)y, (int)(right - x), (int)(bottom - y));
}

static void canvas_drag_to(Canvas *canvas, double x, double y) {
    if (canvas_drag_distance(canvas, x, y)) canvas->drag_moved = true;
    /* Only playback needs a click/drag dead zone. Images track the pointer
     * immediately, as in the original image-only viewer. */
    if ((!canvas->drag_moved && canvas->player) || !canvas->pixbuf || canvas->audio_only ||
        (canvas->fullscreen && canvas->player)) return;
    double ox = canvas->drag_ox + x - canvas->drag_x;
    double oy = canvas->drag_oy + y - canvas->drag_y;
    if (ox == canvas->ox && oy == canvas->oy) return;
    canvas_damage_media(canvas);
    canvas->ox = ox;
    canvas->oy = oy;
    canvas_damage_media(canvas);
}

static gboolean canvas_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    (void)widget;
    Canvas *canvas = data;
    if (event->button == 1) {
        canvas->dragging = true;
        canvas->drag_moved = false;
        canvas->drag_x = event->x;
        canvas->drag_y = event->y;
        canvas->drag_ox = canvas->ox;
        canvas->drag_oy = canvas->oy;
    }
    return TRUE;
}

static gboolean canvas_button_release(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    (void)widget;
    Canvas *canvas = data;
    if (event->button == 1 && canvas->dragging) {
        canvas_drag_to(canvas, event->x, event->y);
        bool clicked = !canvas->drag_moved && !canvas_drag_distance(canvas, event->x, event->y);
        canvas->dragging = false;
        if (clicked) video_toggle_pause(canvas);
    }
    return TRUE;
}

static gboolean canvas_motion(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
    (void)widget;
    Canvas *canvas = data;
    if (canvas->dragging) {
        canvas_drag_to(canvas, event->x, event->y);
    }
    return TRUE;
}

static void status_update(ImgView *view, const char *path, const char *error) {
    GdkPixbuf *pixbuf = view->canvas.pixbuf;
    bool playback = view->canvas.player != NULL && !error;
    if (view->status.volume_box) gtk_widget_set_visible(view->status.volume_box, playback);
    if (view->status.playback) gtk_widget_set_visible(view->status.playback, playback);

    if (error) {
        char *text = g_strdup_printf("Error: %s", error);
        gtk_label_set_text(GTK_LABEL(view->status.name), text);
        gtk_label_set_text(GTK_LABEL(view->status.dims), "");
        g_free(text);
    } else {
        if (path) {
            char *base = g_path_get_basename(path);
            gtk_label_set_text(GTK_LABEL(view->status.name), base);
            g_free(base);
        }
        if (pixbuf) {
            char *dims = g_strdup_printf("%dx%d", gdk_pixbuf_get_width(pixbuf), gdk_pixbuf_get_height(pixbuf));
            gtk_label_set_text(GTK_LABEL(view->status.dims), dims);
            g_free(dims);
        } else {
            gtk_label_set_text(GTK_LABEL(view->status.dims), view->canvas.audio_only ? "Audio" : "");
        }
    }

    if (view->paths->len > 0) {
        char *index = g_strdup_printf("%u/%u", view->index + 1, view->paths->len);
        gtk_label_set_text(GTK_LABEL(view->status.index), index);
        g_free(index);
    }

    char *zoom = view->canvas.audio_only ? g_strdup("")
        : g_strdup_printf("%.0f%%", view->canvas.zoom * 100.0);
    gtk_label_set_text(GTK_LABEL(view->status.zoom), zoom);
    g_free(zoom);
}

static void volume_update(ImgView *view) {
    if (!view->status.volume_level) return;
    Canvas *canvas = &view->canvas;
    gtk_level_bar_set_value(GTK_LEVEL_BAR(view->status.volume_level), canvas->muted ? 0 : canvas->volume);
    char *label = canvas->muted ? g_strdup("VOL muted") : g_strdup_printf("VOL %.0f%%", canvas->volume * 100);
    gtk_label_set_text(GTK_LABEL(view->status.volume_label), label);
    g_free(label);
}

static gboolean volume_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer data) {
    (void)widget;
    if (!(event->state & GDK_CONTROL_MASK)) return FALSE;
    ImgView *view = data;
    Canvas *canvas = &view->canvas;
    if (!canvas->player) return TRUE;
    double delta = 0;
    if (event->direction == GDK_SCROLL_UP) delta = 1;
    else if (event->direction == GDK_SCROLL_DOWN) delta = -1;
    else if (event->direction == GDK_SCROLL_SMOOTH) {
        double dx, dy;
        if (gdk_event_get_scroll_deltas((GdkEvent *)event, &dx, &dy)) delta = -dy;
    }
    if (delta != 0) {
        canvas->volume = clamp_double(canvas->volume + delta * VOLUME_STEP, 0, VOLUME_MAX);
        canvas->muted = false;
        if (canvas->player)
            g_object_set(canvas->player, "volume", canvas->volume, "mute", FALSE, NULL);
        volume_update(view);
    }
    return TRUE;
}

static gboolean canvas_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer data) {
    if (volume_scroll(widget, event, data)) return TRUE;
    (void)widget;
    ImgView *view = data;
    double factor = 1.0;

    if (event->direction == GDK_SCROLL_SMOOTH) {
        double dx = 0.0;
        double dy = 0.0;
        gdk_event_get_scroll_deltas((GdkEvent *)event, &dx, &dy);
        factor = pow(ZOOM_STEP, -dy);
    } else if (event->direction == GDK_SCROLL_UP) {
        factor = ZOOM_STEP;
    } else if (event->direction == GDK_SCROLL_DOWN) {
        factor = 1.0 / ZOOM_STEP;
    } else {
        return TRUE;
    }

    canvas_zoom_step(&view->canvas, factor, event->x, event->y);
    status_update(view, NULL, NULL);
    return TRUE;
}

static gboolean canvas_configure(GtkWidget *widget, GdkEventConfigure *event, gpointer data) {
    (void)widget;
    (void)event;
    ImgView *view = data;
    canvas_fit(&view->canvas, false);
    status_update(view, NULL, NULL);
    return FALSE;
}

static void canvas_init(Canvas *canvas, ImgView *view) {
    canvas->area = gtk_drawing_area_new();
    canvas->volume = 1.0;
    canvas->zoom = 1.0;
    canvas->checker_pattern = create_checker_pattern();
    gtk_widget_add_events(
        canvas->area,
        GDK_BUTTON_PRESS_MASK |
        GDK_BUTTON_RELEASE_MASK |
        GDK_POINTER_MOTION_MASK |
        GDK_SCROLL_MASK |
        GDK_SMOOTH_SCROLL_MASK
    );

    g_signal_connect(canvas->area, "draw", G_CALLBACK(canvas_draw), canvas);
    g_signal_connect(canvas->area, "button-press-event", G_CALLBACK(canvas_button_press), canvas);
    g_signal_connect(canvas->area, "button-release-event", G_CALLBACK(canvas_button_release), canvas);
    g_signal_connect(canvas->area, "motion-notify-event", G_CALLBACK(canvas_motion), canvas);
    g_signal_connect(canvas->area, "scroll-event", G_CALLBACK(canvas_scroll), view);
    g_signal_connect(canvas->area, "configure-event", G_CALLBACK(canvas_configure), view);
}

static gboolean video_seek_to(Canvas *canvas, gint64 position);

static gboolean progress_change(GtkRange *range, GtkScrollType scroll, double value, gpointer data) {
    (void)range;
    (void)scroll;
    ImgView *view = data;
    if (!view->status.scrubbing)
        video_seek_to(&view->canvas, (gint64)(value * GST_SECOND));
    return FALSE;
}

static void progress_set_pointer(ImgView *view, double x) {
    GtkRange *range = GTK_RANGE(view->status.progress);
    GdkRectangle rect;
    gint slider_start, slider_end;
    gtk_range_get_range_rect(range, &rect);
    gtk_range_get_slider_range(range, &slider_start, &slider_end);
    double handle = slider_end - slider_start;
    double width = rect.width - handle;
    if (width <= 0) return;
    double fraction = clamp_double((x - rect.x - handle / 2.0) / width, 0, 1);
    GtkAdjustment *adjustment = gtk_range_get_adjustment(range);
    double lower = gtk_adjustment_get_lower(adjustment);
    double upper = gtk_adjustment_get_upper(adjustment);
    gtk_range_set_value(range, lower + fraction * (upper - lower));
}

static void progress_drag_begin(GtkGestureDrag *gesture, double x, double y, gpointer data) {
    (void)y;
    ImgView *view = data;
    view->status.scrubbing = true;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    progress_set_pointer(view, x);
}

static void progress_drag_update(GtkGestureDrag *gesture, double dx, double dy, gpointer data) {
    (void)dy;
    double x, y;
    if (gtk_gesture_drag_get_start_point(gesture, &x, &y))
        progress_set_pointer(data, x + dx);
}

static void progress_drag_end(GtkGestureDrag *gesture, double dx, double dy, gpointer data) {
    ImgView *view = data;
    if (view->status.scrubbing) {
        progress_drag_update(gesture, dx, dy, data);
        view->status.scrubbing = false;
        video_seek_to(&view->canvas,
            (gint64)(gtk_range_get_value(GTK_RANGE(view->status.progress)) * GST_SECOND));
    }
}

static void progress_drag_cancel(GtkGesture *gesture, GdkEventSequence *sequence, gpointer data) {
    (void)gesture;
    (void)sequence;
    ImgView *view = data;
    view->status.scrubbing = false;
}

static void progress_reset(StatusBar *status) {
    status->scrubbing = false;
    if (!status->progress) return;
    gtk_range_set_value(GTK_RANGE(status->progress), 0);
    gtk_widget_set_sensitive(status->progress, FALSE);
    gtk_widget_hide(status->progress);
}

static void status_init(ImgView *view) {
    StatusBar *status = &view->status;
    status->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(status->box, "statusbar");
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 9);
    status->progress = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 1, 0.1);
    gtk_widget_set_name(status->progress, "video-progress");
    gtk_scale_set_draw_value(GTK_SCALE(status->progress), FALSE);
    gtk_widget_set_can_focus(status->progress, FALSE);
    gtk_widget_set_no_show_all(status->progress, TRUE);
    g_signal_connect(status->progress, "change-value", G_CALLBACK(progress_change), view);
    g_signal_connect(status->progress, "scroll-event", G_CALLBACK(volume_scroll), view);
    GtkGesture *drag = gtk_gesture_drag_new(status->progress);
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), 1);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(drag), GTK_PHASE_CAPTURE);
    g_signal_connect(drag, "drag-begin", G_CALLBACK(progress_drag_begin), view);
    g_signal_connect(drag, "drag-update", G_CALLBACK(progress_drag_update), view);
    g_signal_connect(drag, "drag-end", G_CALLBACK(progress_drag_end), view);
    g_signal_connect(drag, "cancel", G_CALLBACK(progress_drag_cancel), view);
    g_object_set_data_full(G_OBJECT(status->progress), "progress-drag", drag, g_object_unref);
    gtk_box_pack_start(GTK_BOX(status->box), status->progress, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(status->box), row, FALSE, FALSE, 0);

    status->name = gtk_label_new("");
    status->index = gtk_label_new("");
    status->dims = gtk_label_new("");
    status->zoom = gtk_label_new("");
    status->playback = gtk_label_new("");
    gtk_widget_set_no_show_all(status->playback, TRUE);
    GtkWidget *volume_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    status->volume_box = volume_box;
    status->volume_label = gtk_label_new("");
    gtk_label_set_width_chars(GTK_LABEL(status->volume_label), 9);
    gtk_label_set_xalign(GTK_LABEL(status->volume_label), 1.0);
    status->volume_level = gtk_level_bar_new_for_interval(0, VOLUME_MAX);
    gtk_widget_set_name(status->volume_level, "volume-level");
    gtk_widget_set_size_request(status->volume_level, 52, 3);
    gtk_widget_set_valign(status->volume_level, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(volume_box), status->volume_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(volume_box), status->volume_level, FALSE, FALSE, 0);
    gtk_widget_show_all(volume_box);
    gtk_widget_set_no_show_all(volume_box, TRUE);
    gtk_widget_hide(volume_box);
    volume_update(view);

    gtk_widget_set_name(status->name, "st-name");
    gtk_widget_set_name(status->index, "st-index");
    gtk_widget_set_name(status->dims, "st-dims");
    gtk_widget_set_name(status->zoom, "st-zoom");

    gtk_label_set_xalign(GTK_LABEL(status->name), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(status->name), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_hexpand(status->name, TRUE);

    gtk_box_pack_start(GTK_BOX(row), status->name, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(row), status->playback, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), volume_box, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), status->index, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), status->dims, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), status->zoom, FALSE, FALSE, 0);
}

static void video_update_status(ImgView *view) {
    Canvas *canvas = &view->canvas;
    gint64 position = 0, duration = 0;
    gst_element_query_position(canvas->player, GST_FORMAT_TIME, &position);
    gst_element_query_duration(canvas->player, GST_FORMAT_TIME, &duration);
    if (view->status.progress) {
        GstQuery *query = gst_query_new_seeking(GST_FORMAT_TIME);
        gboolean seekable = FALSE;
        if (gst_element_query(canvas->player, query))
            gst_query_parse_seeking(query, NULL, &seekable, NULL, NULL);
        gst_query_unref(query);
        gtk_widget_show(view->status.progress);
        gtk_widget_set_sensitive(view->status.progress, seekable && duration > 0);
        if (!view->status.scrubbing) {
            gtk_range_set_range(GTK_RANGE(view->status.progress), 0,
                duration > 0 ? (double)duration / GST_SECOND : 1);
            gtk_range_set_value(GTK_RANGE(view->status.progress),
                canvas->ended && duration > 0 ? (double)duration / GST_SECOND :
                (double)MAX(position, 0) / GST_SECOND);
        }
    }
    position = MAX(position, 0) / GST_SECOND;
    duration = MAX(duration, 0) / GST_SECOND;
    char *text = g_strdup_printf("%s %" G_GINT64_FORMAT ":%02" G_GINT64_FORMAT
        " / %" G_GINT64_FORMAT ":%02" G_GINT64_FORMAT "%s",
        canvas->ended ? "Ended" : canvas->paused ? "Paused" : "Playing",
        position / 60, position % 60, duration / 60, duration % 60,
        canvas->muted ? " (muted)" : "");
    if (g_strcmp0(gtk_label_get_text(GTK_LABEL(view->status.playback)), text) != 0)
        gtk_label_set_text(GTK_LABEL(view->status.playback), text);
    g_free(text);
}

static void metadata_update(ImgView *view) {
    if (!view->metadata_label) return;
    Canvas *canvas = &view->canvas;
    GString *text = g_string_new(view->metadata_file_text ? view->metadata_file_text : "No media");
    GdkPixbuf *frame = canvas->video_frame ? canvas->video_frame : canvas->pixbuf;
    if (frame) g_string_append_printf(text, "\nResolution\n%d × %d\n",
        gdk_pixbuf_get_width(frame), gdk_pixbuf_get_height(frame));
    if (canvas->video_fps > 0) g_string_append_printf(text, "\nFrame rate\n%.3g fps\n", canvas->video_fps);
    if (canvas->player) {
        gint64 duration;
        if (gst_element_query_duration(canvas->player, GST_FORMAT_TIME, &duration) && duration > 0) {
            gint64 seconds = duration / GST_SECOND;
            g_string_append_printf(text, "\nDuration\n%" G_GINT64_FORMAT ":%02" G_GINT64_FORMAT "\n",
                seconds / 60, seconds % 60);
        }
    }
    if (canvas->tags) {
        const char *keys[] = {GST_TAG_TITLE, GST_TAG_ARTIST, GST_TAG_ALBUM,
            GST_TAG_VIDEO_CODEC, GST_TAG_AUDIO_CODEC, GST_TAG_CONTAINER_FORMAT};
        const char *labels[] = {"Title", "Artist", "Album", "Video codec", "Audio codec", "Container"};
        for (guint i = 0; i < G_N_ELEMENTS(keys); i++) {
            char *value = NULL;
            if (gst_tag_list_get_string(canvas->tags, keys[i], &value)) {
                g_string_append_printf(text, "\n%s\n%s\n", labels[i], value);
                g_free(value);
            }
        }
        guint bitrate;
        if (gst_tag_list_get_uint(canvas->tags, GST_TAG_BITRATE, &bitrate))
            g_string_append_printf(text, "\nBitrate\n%.0f kb/s\n", bitrate / 1000.0);
    }
    if (g_strcmp0(gtk_label_get_text(GTK_LABEL(view->metadata_label)), text->str))
        gtk_label_set_text(GTK_LABEL(view->metadata_label), text->str);
    g_string_free(text, TRUE);
}

static void metadata_file_load(ImgView *view, const char *path) {
    if (!view->metadata_label) return;
    char *base = g_path_get_basename(path);
    GString *text = g_string_new("File\n");
    g_string_append_printf(text, "%s\n", base);
    const char *extension = strrchr(base, '.');
    if (extension) {
        char *format = g_ascii_strup(extension + 1, -1);
        g_string_append_printf(text, "\nFormat\n%s\n", format);
        g_free(format);
    }
    GFile *file = g_file_new_for_path(path);
    GFileInfo *info = g_file_query_info(file, "standard::size,time::modified", G_FILE_QUERY_INFO_NONE, NULL, NULL);
    if (info) {
        char *size = g_format_size(g_file_info_get_size(info));
        g_string_append_printf(text, "\nFile size\n%s\n", size);
        g_free(size);
        GDateTime *date = g_date_time_new_from_unix_local(g_file_info_get_attribute_uint64(info, "time::modified"));
        if (date) {
            char *modified = g_date_time_format(date, "%Y-%m-%d %H:%M");
            g_string_append_printf(text, "\nModified\n%s\n", modified);
            g_free(modified); g_date_time_unref(date);
        }
        g_object_unref(info);
    }
    g_string_append_printf(text, "\nLocation\n%s\n", path);
    g_free(view->metadata_file_text);
    view->metadata_file_text = g_string_free(text, FALSE);
    g_object_unref(file); g_free(base);
}

typedef struct {
    GstVideoFrame frame;
    GstSample *sample;
} VideoPixels;

static void video_pixels_release(guchar *pixels, gpointer data) {
    (void)pixels;
    VideoPixels *owner = data;
    gst_video_frame_unmap(&owner->frame);
    gst_sample_unref(owner->sample);
    g_free(owner);
}

/* Pull frames on the GTK thread; the bounded sink queue drops stale frames
 * when drawing cannot keep up, while GStreamer maintains audio/video timing. */
static gboolean video_tick(gpointer data) {
    ImgView *view = data;
    Canvas *canvas = &view->canvas;
    GstMessage *message;
    while ((message = gst_bus_pop_filtered(canvas->video_bus, GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_TAG))) {
        if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_TAG) {
            GstTagList *tags;
            gst_message_parse_tag(message, &tags);
            if (!canvas->tags) canvas->tags = gst_tag_list_new_empty();
            gst_tag_list_insert(canvas->tags, tags, GST_TAG_MERGE_REPLACE);
            gst_tag_list_unref(tags);
            gst_message_unref(message);
            metadata_update(view);
            continue;
        }
        if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
            GError *error = NULL;
            gst_message_parse_error(message, &error, NULL);
            status_update(view, NULL, error->message);
            g_clear_error(&error);
            gst_message_unref(message);
            canvas->video_id = 0;
            canvas_clear_image(canvas);
            progress_reset(&view->status);
            gtk_label_set_text(GTK_LABEL(view->status.playback), "");
            gtk_widget_queue_draw(canvas->area);
            return G_SOURCE_REMOVE;
        }
        canvas->ended = true;
        canvas->paused = true;
        gst_element_set_state(canvas->player, GST_STATE_PAUSED);
        gst_message_unref(message);
    }

    if (canvas->audio_only) {
        video_update_status(view);
        return G_SOURCE_CONTINUE;
    }
    GstSample *preroll = gst_app_sink_try_pull_preroll(GST_APP_SINK(canvas->video_sink), 0);
    GstSample *sample = gst_app_sink_try_pull_sample(GST_APP_SINK(canvas->video_sink), 0);
    if (!sample && canvas->paused && !canvas->ended) {
        sample = preroll;
        preroll = NULL;
    }
    if (preroll) gst_sample_unref(preroll);
    if (sample) {
        GstVideoInfo info;
        VideoPixels *owner = g_new0(VideoPixels, 1);
        if (gst_video_info_from_caps(&info, gst_sample_get_caps(sample)) &&
            gst_video_frame_map(&owner->frame, &info, gst_sample_get_buffer(sample), GST_MAP_READ)) {
            int width = GST_VIDEO_INFO_WIDTH(&info), height = GST_VIDEO_INFO_HEIGHT(&info);
            if (GST_VIDEO_INFO_FPS_D(&info) > 0)
                canvas->video_fps = (double)GST_VIDEO_INFO_FPS_N(&info) / GST_VIDEO_INFO_FPS_D(&info);
            bool first = !canvas->video_frame;
            owner->sample = sample;
            GdkPixbuf *pixbuf = gdk_pixbuf_new_from_data(
                GST_VIDEO_FRAME_PLANE_DATA(&owner->frame, 0), GDK_COLORSPACE_RGB, FALSE, 8,
                width, height, GST_VIDEO_FRAME_PLANE_STRIDE(&owner->frame, 0),
                video_pixels_release, owner);
            if (pixbuf) {
                /* The pixbuf owns the sample/map until its last reference is
                 * released. Decoder memory stays valid through paint/rotate. */
                sample = NULL;
                g_clear_object(&canvas->video_frame);
                canvas->video_frame = pixbuf;
                canvas_update_frame(canvas);
                if (first) {
                    canvas_fit(canvas, true);
                    status_update(view, NULL, NULL);
                    metadata_update(view);
                }
            } else {
                gst_video_frame_unmap(&owner->frame);
                g_free(owner);
            }
        } else g_free(owner);
        if (sample) gst_sample_unref(sample);
    }
    video_update_status(view);
    return G_SOURCE_CONTINUE;
}

static char *video_load(ImgView *view, const char *path) {
    Canvas *canvas = &view->canvas;
    progress_reset(&view->status);
    canvas_clear_image(canvas);
    canvas->audio_only = is_audio_ext(path);
    canvas->zoom = 1.0;
    gtk_widget_queue_draw(canvas->area);
    canvas->player = gst_element_factory_make("playbin", NULL);
    canvas->video_sink = gst_element_factory_make("appsink", NULL);
    if (!canvas->player || !canvas->video_sink) {
        canvas_clear_image(canvas);
        return g_strdup("Media playback requires GStreamer base plugins (playbin and appsink)");
    }
    gst_object_ref_sink(canvas->player);
    gst_object_ref_sink(canvas->video_sink);
    GstCaps *caps = gst_caps_from_string("video/x-raw,format=RGB,pixel-aspect-ratio=1/1");
    g_object_set(canvas->video_sink, "caps", caps, "max-buffers", 1u,
        "drop", TRUE, "sync", TRUE, "wait-on-eos", FALSE, NULL);
    gst_caps_unref(caps);
    GError *error = NULL;
    char *uri = gst_filename_to_uri(path, &error);
    if (!uri) {
        char *text = g_strdup(error->message);
        g_clear_error(&error);
        canvas_clear_image(canvas);
        return text;
    }
    /* Video + audio, without subtitle rendering over the image canvas. */
    g_object_set(canvas->player, "uri", uri, "video-sink", canvas->video_sink,
        "flags", canvas->audio_only ? 2u : 3u, "mute", canvas->muted, "volume", canvas->volume, NULL);
    g_free(uri);
    canvas->video_bus = gst_element_get_bus(canvas->player);
    if (gst_element_set_state(canvas->player, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        canvas_clear_image(canvas);
        return g_strdup("Could not start video playback; check GStreamer codecs and audio output");
    }
    canvas->video_id = g_timeout_add(canvas->audio_only ? 100 : 10, video_tick, view);
    video_update_status(view);
    return NULL;
}

static gboolean video_seek_to(Canvas *canvas, gint64 position) {
    gint64 duration;
    if (!canvas->player) return FALSE;
    position = MAX((gint64)0, position);
    if (gst_element_query_duration(canvas->player, GST_FORMAT_TIME, &duration) && duration > 0)
        position = MIN(position, MAX((gint64)0, duration - GST_MSECOND));
    if (gst_element_seek_simple(canvas->player, GST_FORMAT_TIME,
            GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE, position)) {
        canvas->ended = false;
        return TRUE;
    }
    return FALSE;
}

static void video_seek(Canvas *canvas, gint seconds) {
    gint64 position;
    if (!canvas->player || !gst_element_query_position(canvas->player, GST_FORMAT_TIME, &position)) return;
    video_seek_to(canvas, position + seconds * (gint64)GST_SECOND);
}

static void video_toggle_pause(Canvas *canvas) {
    if (!canvas->player) return;
    if (canvas->ended) {
        if (!gst_element_seek_simple(canvas->player, GST_FORMAT_TIME,
                GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE, 0)) return;
        canvas->ended = false;
    }
    canvas->paused = !canvas->paused;
    gst_element_set_state(canvas->player, canvas->paused ? GST_STATE_PAUSED : GST_STATE_PLAYING);
}

static void load_index(ImgView *view, gint index) {
    if (view->paths->len == 0) {
        return;
    }

    gint total = (gint)view->paths->len;
    index %= total;
    if (index < 0) {
        index += total;
    }

    view->index = (guint)index;
    const char *path = g_ptr_array_index(view->paths, view->index);
    metadata_file_load(view, path);
    progress_reset(&view->status);
    gtk_label_set_text(GTK_LABEL(view->status.playback), "");
    char *error = (is_video_ext(path) || is_audio_ext(path))
        ? video_load(view, path) : canvas_load(&view->canvas, path);
    status_update(view, path, error);
    metadata_update(view);

    char *base = g_path_get_basename(path);
    char *title = g_strdup_printf("imgview - %s", base);
    gtk_window_set_title(GTK_WINDOW(view->window), title);
    if (view->header) gtk_header_bar_set_title(GTK_HEADER_BAR(view->header), base);
    g_free(title);
    g_free(base);
    g_free(error);
    if (view->playlist_list) playlist_refresh(view);
}

static gboolean finish_pending_scan(gpointer data) {
    ImgView *view = data;
    view->scan_id = 0;
    if (!view->pending_scan_dir || !view->pending_scan_path) {
        return G_SOURCE_REMOVE;
    }

    GPtrArray *siblings = new_path_array();
    add_dir_images(siblings, view->pending_scan_dir);

    guint index = 0;
    bool found = false;
    for (guint i = 0; i < siblings->len; i++) {
        if (g_strcmp0(g_ptr_array_index(siblings, i), view->pending_scan_path) == 0) {
            index = i;
            found = true;
            break;
        }
    }

    if (found) {
        g_ptr_array_unref(view->paths);
        view->paths = siblings;
        view->index = index;
        status_update(view, g_ptr_array_index(view->paths, view->index), NULL);
    } else {
        g_ptr_array_unref(siblings);
    }

    g_clear_pointer(&view->pending_scan_dir, g_free);
    g_clear_pointer(&view->pending_scan_path, g_free);
    return G_SOURCE_REMOVE;
}

static void go_delta(ImgView *view, gint delta) {
    if (view->paths->len > 0) {
        load_index(view, (gint)view->index + delta);
    }
}

static gboolean slideshow_tick(gpointer data) {
    go_delta(data, 1);
    return G_SOURCE_CONTINUE;
}

static void toggle_slideshow(ImgView *view) {
    if (view->slideshow_id) {
        g_source_remove(view->slideshow_id);
        view->slideshow_id = 0;
    } else {
        view->slideshow_id = g_timeout_add_seconds(4, slideshow_tick, view);
    }
}

static void fullscreen_hover(ImgView *view, double x, double y) {
    int width = gtk_widget_get_allocated_width(view->overlay);
    int height = gtk_widget_get_allocated_height(view->overlay);
    bool inside = x >= 0 && x < width && y >= 0 && y < height;
    if (view->playlist_revealer) {
        bool open = gtk_revealer_get_reveal_child(GTK_REVEALER(view->playlist_revealer));
        bool hover = inside && y >= 32 && y < height - 40 &&
            !view->canvas.dragging && !view->status.scrubbing &&
            x < (open ? MAX(300, gtk_widget_get_allocated_width(view->playlist_revealer)) : 8);
        gtk_revealer_set_reveal_child(GTK_REVEALER(view->playlist_revealer), hover);
        if (hover) {
            playlist_refresh(view);
            playlist_thumbnails(view);
        }
    }
    bool metadata_open = gtk_revealer_get_reveal_child(GTK_REVEALER(view->metadata_revealer));
    int panel_width = MAX(320, gtk_widget_get_allocated_width(view->metadata_revealer));
    bool metadata_hover = inside && y >= 32 && y < height - 40 &&
        !view->canvas.dragging && !view->status.scrubbing &&
        x >= width - (metadata_open ? panel_width : 8);
    gtk_revealer_set_reveal_child(GTK_REVEALER(view->metadata_revealer), metadata_hover);
    gint64 now = g_get_monotonic_time();
    if (metadata_hover && (!metadata_open || now - view->metadata_refresh_time >= G_USEC_PER_SEC)) {
        metadata_update(view);
        view->metadata_refresh_time = now;
    }
    if (!view->fullscreen) return;
    int top = 8, bottom = 8, natural;
    if (gtk_revealer_get_reveal_child(GTK_REVEALER(view->header_revealer))) {
        gtk_widget_get_preferred_height(view->fullscreen_header, NULL, &natural);
        top = MAX(8, natural);
    }
    if (gtk_revealer_get_reveal_child(GTK_REVEALER(view->bottom_revealer))) {
        gtk_widget_get_preferred_height(view->status_host, NULL, &natural);
        bottom = MAX(8, natural);
    }
    gtk_revealer_set_reveal_child(GTK_REVEALER(view->header_revealer), inside && y < top);
    gtk_revealer_set_reveal_child(GTK_REVEALER(view->bottom_revealer),
        view->status.scrubbing || (inside && y >= height - bottom));
}

static gboolean fullscreen_hover_tick(gpointer data) {
    ImgView *view = data;
    playlist_thumbnails(view);
    GdkWindow *window = gtk_widget_get_window(view->window);
    GdkDevice *pointer = gdk_seat_get_pointer(gdk_display_get_default_seat(gtk_widget_get_display(view->window)));
    int x, y, ox = 0, oy = 0;
    gdk_window_get_device_position(window, pointer, &x, &y, NULL);
    gtk_widget_translate_coordinates(view->overlay, view->window, 0, 0, &ox, &oy);
    fullscreen_hover(view, x - ox, y - oy);
    return G_SOURCE_CONTINUE;
}

static void toggle_fullscreen(ImgView *view) {
    if (view->fullscreen) {
        gtk_window_unfullscreen(GTK_WINDOW(view->window));
    } else {
        gtk_window_fullscreen(GTK_WINDOW(view->window));
    }
    view->fullscreen = !view->fullscreen;
    g_object_ref(view->status_host);
    gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(view->status_host)), view->status_host);
    if (view->fullscreen) {
        view->windowed_status_visible = gtk_widget_get_visible(view->status.box);
        gtk_widget_show(view->status.box);
        gtk_widget_set_valign(view->status_host, GTK_ALIGN_END);
        gtk_container_add(GTK_CONTAINER(view->bottom_revealer), view->status_host);
        gtk_revealer_set_reveal_child(GTK_REVEALER(view->bottom_revealer), FALSE);
    } else {
        gtk_revealer_set_reveal_child(GTK_REVEALER(view->header_revealer), FALSE);
        gtk_revealer_set_reveal_child(GTK_REVEALER(view->bottom_revealer), FALSE);
        gtk_widget_set_valign(view->status_host, GTK_ALIGN_FILL);
        gtk_box_pack_start(GTK_BOX(view->content_box), view->status_host, FALSE, FALSE, 0);
        gtk_widget_set_visible(view->status.box, view->windowed_status_visible);
        gtk_widget_show(view->status_host);
    }
    g_object_unref(view->status_host);
    view->canvas.fullscreen = view->fullscreen;
    view->canvas.dragging = false;
    canvas_fit(&view->canvas, true);
    status_update(view, NULL, NULL);
    if (view->header) gtk_widget_set_visible(view->header, !view->fullscreen);
}

static gboolean native_video_resize(gpointer data) {
    ImgView *view = data;
    Canvas *canvas = &view->canvas;
    if (!canvas->player || !canvas->video_frame ||
        g_get_monotonic_time() > view->native_resize_deadline) {
        view->native_resize_id = 0;
        return G_SOURCE_REMOVE;
    }
    GdkWindowState state = gdk_window_get_state(gtk_widget_get_window(view->window));
    if (state & (GDK_WINDOW_STATE_FULLSCREEN | GDK_WINDOW_STATE_MAXIMIZED))
        return G_SOURCE_CONTINUE;
    GtkAllocation area;
    gtk_widget_get_allocation(canvas->area, &area);
    int width, height;
    gtk_window_get_size(GTK_WINDOW(view->window), &width, &height);
    int native_width = gdk_pixbuf_get_width(canvas->pixbuf);
    int native_height = gdk_pixbuf_get_height(canvas->pixbuf);
    /* Fit first: subsequent allocation changes keep a fit ratio of one. The
     * size difference includes the visible controls, not video letterboxing. */
    canvas_fit(canvas, true);
    status_update(view, NULL, NULL);
    if (area.width == native_width && area.height == native_height) {
        view->native_resize_id = 0;
        return G_SOURCE_REMOVE;
    }
    gtk_window_resize(GTK_WINDOW(view->window),
        native_width + MAX(0, width - area.width),
        native_height + MAX(0, height - area.height));
    return G_SOURCE_CONTINUE;
}

static void window_native_video(ImgView *view) {
    if (!view->canvas.player || !view->canvas.video_frame) return;
    if (view->fullscreen) toggle_fullscreen(view);
    gtk_window_unmaximize(GTK_WINDOW(view->window));
    if (view->native_resize_id) g_source_remove(view->native_resize_id);
    view->native_resize_deadline = g_get_monotonic_time() + 2 * G_USEC_PER_SEC;
    view->native_resize_id = g_timeout_add(50, native_video_resize, view);
}

static gboolean key_press(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    (void)widget;
    ImgView *view = data;
    Canvas *canvas = &view->canvas;

    if (!(event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK)) &&
        (event->keyval == GDK_KEY_c || event->keyval == GDK_KEY_C)) {
        window_native_video(view);
        return TRUE;
    }

    if (canvas->player && (event->state & GDK_SHIFT_MASK) &&
        (event->keyval == GDK_KEY_Left || event->keyval == GDK_KEY_Right)) {
        video_seek(canvas, event->keyval == GDK_KEY_Left ? -5 : 5);
        return TRUE;
    }

    switch (event->keyval) {
    case GDK_KEY_space:
        video_toggle_pause(canvas);
        break;
    case GDK_KEY_m:
        if (canvas->player) {
            canvas->muted = !canvas->muted;
            g_object_set(canvas->player, "mute", canvas->muted, NULL);
            volume_update(view);
        }
        break;
    case GDK_KEY_Right:
    case GDK_KEY_n:
    case GDK_KEY_l:
    case GDK_KEY_Page_Down:
        go_delta(view, 1);
        break;
    case GDK_KEY_Left:
    case GDK_KEY_p:
    case GDK_KEY_h:
    case GDK_KEY_Page_Up:
        go_delta(view, -1);
        break;
    case GDK_KEY_End:
        load_index(view, (gint)view->paths->len - 1);
        break;
    case GDK_KEY_Home:
        load_index(view, 0);
        break;
    case GDK_KEY_plus:
    case GDK_KEY_equal:
    case GDK_KEY_KP_Add:
        canvas_zoom_step(canvas, ZOOM_STEP, -1, -1);
        status_update(view, NULL, NULL);
        break;
    case GDK_KEY_minus:
    case GDK_KEY_KP_Subtract:
        canvas_zoom_step(canvas, 1.0 / ZOOM_STEP, -1, -1);
        status_update(view, NULL, NULL);
        break;
    case GDK_KEY_0:
    case GDK_KEY_KP_0:
        canvas_actual_size(canvas);
        status_update(view, NULL, NULL);
        break;
    case GDK_KEY_w:
        canvas_fit(canvas, true);
        status_update(view, NULL, NULL);
        break;
    case GDK_KEY_r:
        canvas_rotate(canvas, true);
        status_update(view, NULL, NULL);
        break;
    case GDK_KEY_R:
        canvas_rotate(canvas, false);
        status_update(view, NULL, NULL);
        break;
    case GDK_KEY_slash:
        canvas_flip_h(canvas);
        break;
    case GDK_KEY_f:
    case GDK_KEY_F:
    case GDK_KEY_F11:
        toggle_fullscreen(view);
        break;
    case GDK_KEY_i:
        if (!view->fullscreen)
            gtk_widget_set_visible(view->status.box, !gtk_widget_get_visible(view->status.box));
        break;
    case GDK_KEY_s:
        toggle_slideshow(view);
        break;
    case GDK_KEY_q:
    case GDK_KEY_Escape:
        if (view->fullscreen) {
            toggle_fullscreen(view);
        } else {
            gtk_widget_destroy(view->window);
        }
        break;
    default:
        break;
    }

    return TRUE;
}

static char *window_settings_path(void) {
    return g_build_filename(g_get_user_config_dir(), "imgview", "window.ini", NULL);
}

static gboolean window_size_save(gpointer data) {
    ImgView *view = data;
    view->window_save_id = 0;
    char *path = window_settings_path();
    char *directory = g_path_get_dirname(path);
    GKeyFile *settings = g_key_file_new();
    g_key_file_set_integer(settings, "Window", "width", view->window_width);
    g_key_file_set_integer(settings, "Window", "height", view->window_height);
    g_key_file_set_boolean(settings, "Window", "maximized", view->window_maximized);
    if (view->has_window_position) {
        g_key_file_set_integer(settings, "Window", "x", view->window_x);
        g_key_file_set_integer(settings, "Window", "y", view->window_y);
    }
    if (g_mkdir_with_parents(directory, 0700) == 0) {
        gsize length;
        char *contents = g_key_file_to_data(settings, &length, NULL);
        g_file_set_contents(path, contents, length, NULL);
        g_free(contents);
    }
    g_key_file_unref(settings);
    g_free(directory); g_free(path);
    return G_SOURCE_REMOVE;
}

static void window_size_schedule(ImgView *view) {
    if (view->window_save_id) g_source_remove(view->window_save_id);
    view->window_save_id = g_timeout_add(400, window_size_save, view);
}

static gboolean window_size_changed(GtkWidget *widget, GdkEventConfigure *event, gpointer data) {
    (void)event;
    ImgView *view = data;
    GdkWindowState state = gdk_window_get_state(gtk_widget_get_window(widget));
    if (view->fullscreen || (state & (GDK_WINDOW_STATE_FULLSCREEN | GDK_WINDOW_STATE_MAXIMIZED | GDK_WINDOW_STATE_ICONIFIED)))
        return FALSE;
    int width, height;
    gtk_window_get_size(GTK_WINDOW(widget), &width, &height);
    bool moved = false;
#ifdef GDK_WINDOWING_X11
    if (GDK_IS_X11_DISPLAY(gtk_widget_get_display(widget))) {
        int x, y;
        gtk_window_get_position(GTK_WINDOW(widget), &x, &y);
        moved = !view->has_window_position || x != view->window_x || y != view->window_y;
        view->window_x = x;
        view->window_y = y;
        view->has_window_position = true;
    }
#endif
    if (width > 0 && height > 0 && (moved || width != view->window_width || height != view->window_height)) {
        view->window_width = width;
        view->window_height = height;
        window_size_schedule(view);
    }
    return FALSE;
}

static gboolean window_state_changed(GtkWidget *widget, GdkEventWindowState *event, gpointer data) {
    (void)widget;
    ImgView *view = data;
    if (!view->fullscreen && !(event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN) &&
        (event->changed_mask & GDK_WINDOW_STATE_MAXIMIZED)) {
        view->window_maximized = (event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED) != 0;
        window_size_schedule(view);
    }
    return FALSE;
}

static void window_size_restore(ImgView *view) {
    view->window_width = 1100;
    view->window_height = 780;
    char *path = window_settings_path();
    GKeyFile *settings = g_key_file_new();
    if (g_key_file_load_from_file(settings, path, G_KEY_FILE_NONE, NULL)) {
        int width = g_key_file_get_integer(settings, "Window", "width", NULL);
        int height = g_key_file_get_integer(settings, "Window", "height", NULL);
        if (width > 0 && width <= 32768 && height > 0 && height <= 32768) {
            view->window_width = width;
            view->window_height = height;
        }
        view->window_maximized = g_key_file_get_boolean(settings, "Window", "maximized", NULL);
        if (g_key_file_has_key(settings, "Window", "x", NULL) &&
            g_key_file_has_key(settings, "Window", "y", NULL)) {
            GError *x_error = NULL, *y_error = NULL;
            view->window_x = g_key_file_get_integer(settings, "Window", "x", &x_error);
            view->window_y = g_key_file_get_integer(settings, "Window", "y", &y_error);
            view->has_window_position = !x_error && !y_error;
            g_clear_error(&x_error); g_clear_error(&y_error);
        }
    }
    g_key_file_unref(settings); g_free(path);
    gtk_window_set_default_size(GTK_WINDOW(view->window), view->window_width, view->window_height);
    if (view->window_maximized) gtk_window_maximize(GTK_WINDOW(view->window));
}

static void view_destroy(gpointer data) {
    ImgView *view = data;
    if (view->native_resize_id) g_source_remove(view->native_resize_id);
    if (view->window_save_id) g_source_remove(view->window_save_id);
    window_size_save(view);
    if (view->hover_id) g_source_remove(view->hover_id);
    if (view->scan_id) g_source_remove(view->scan_id);
    if (view->slideshow_id) {
        g_source_remove(view->slideshow_id);
    }
    canvas_clear_image(&view->canvas);
    g_clear_pointer(&view->canvas.checker_pattern, cairo_pattern_destroy);
    g_clear_pointer(&view->pending_scan_dir, g_free);
    g_clear_pointer(&view->pending_scan_path, g_free);
    g_free(view->metadata_file_text);
    g_clear_pointer(&view->playlist_paths, g_ptr_array_unref);
    g_free(view->playlist_dir);
    g_ptr_array_unref(view->paths);
    g_free(view);
}

static void window_destroy(GtkWidget *widget, gpointer data) {
    (void)widget;
    view_destroy(data);
    gtk_main_quit();
}

static void header_minimize(GtkButton *button, gpointer data) {
    (void)button;
    gtk_window_iconify(GTK_WINDOW(((ImgView *)data)->window));
}

static void header_maximize(GtkButton *button, gpointer data) {
    (void)button;
    ImgView *view = data;
    if (view->fullscreen) {
        toggle_fullscreen(view);
        return;
    }
    GtkWindow *window = GTK_WINDOW(((ImgView *)data)->window);
    if (gtk_window_is_maximized(window)) gtk_window_unmaximize(window);
    else gtk_window_maximize(window);
}

static void header_close(GtkButton *button, gpointer data) {
    (void)button;
    gtk_window_close(GTK_WINDOW(((ImgView *)data)->window));
}

static GtkWidget *create_header(ImgView *view) {
    GtkWidget *header = gtk_header_bar_new();
    gtk_widget_set_name(header, "app-header");
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), FALSE);
    const char *symbols[] = {"×", "□", "−"};
    GCallback actions[] = {G_CALLBACK(header_close), G_CALLBACK(header_maximize), G_CALLBACK(header_minimize)};
    for (int i = 0; i < 3; i++) {
        GtkWidget *button = gtk_button_new_with_label(symbols[i]);
        gtk_widget_set_can_focus(button, FALSE);
        if (i == 0) gtk_style_context_add_class(gtk_widget_get_style_context(button), "close");
        g_signal_connect(button, "clicked", actions[i], view);
        gtk_header_bar_pack_end(GTK_HEADER_BAR(header), button);
    }
    gtk_header_bar_set_has_subtitle(GTK_HEADER_BAR(header), FALSE);
    GtkWidget *brand = gtk_label_new("imgview");
    gtk_widget_set_name(brand, "app-brand");
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), brand);
    return header;
}

static GtkWidget *overlay_revealer(ImgView *view, GtkRevealerTransitionType transition,
                                   GtkAlign horizontal, GtkAlign vertical) {
    GtkWidget *revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(revealer), transition);
    gtk_revealer_set_transition_duration(GTK_REVEALER(revealer), 180);
    gtk_widget_set_halign(revealer, horizontal);
    gtk_widget_set_valign(revealer, vertical);
    gtk_overlay_add_overlay(GTK_OVERLAY(view->overlay), revealer);
    return revealer;
}

/* Thumbnail tasks never retain the player: closing or changing folders is safe. */
static int thumbnail_jobs;

/* Remove only files generated by the former persistent preview cache. */
static void preview_cache_cleanup(void) {
    char *directory = g_build_filename(g_get_user_cache_dir(), "imgview", "previews", NULL);
    if (g_file_test(directory, G_FILE_TEST_IS_SYMLINK)) { g_free(directory); return; }
    GDir *dir = g_dir_open(directory, 0, NULL);
    if (dir) {
        const char *name;
        while ((name = g_dir_read_name(dir))) {
            size_t length = strlen(name);
            if (length != 68 || strcmp(name + 64, ".png")) continue;
            bool generated = true;
            for (int i = 0; i < 64; i++)
                if (!g_ascii_isxdigit(name[i])) { generated = false; break; }
            if (!generated) continue;
            char *path = g_build_filename(directory, name, NULL);
            g_unlink(path);
            g_free(path);
        }
        g_dir_close(dir);
        g_rmdir(directory); /* Only succeeds if empty; preserve unrelated files. */
    }
    g_free(directory);
}

static void thumbnail_worker(GTask *task, gpointer source, gpointer data, GCancellable *cancel) {
    (void)source; (void)cancel;
    const char *path = data;
    GdkPixbuf *thumb = NULL;
    if (!is_video_ext(path)) {
        thumb = gdk_pixbuf_new_from_file_at_scale(path, 72, 44, TRUE, NULL);
    } else {
        GstElement *player = gst_element_factory_make("playbin", NULL);
        GstElement *sink = gst_element_factory_make("appsink", NULL);
        GstElement *audio = gst_element_factory_make("fakesink", NULL);
        if (player && sink && audio) {
            gst_object_ref_sink(player); gst_object_ref_sink(sink); gst_object_ref_sink(audio);
            GstCaps *caps = gst_caps_from_string("video/x-raw,format=RGB,pixel-aspect-ratio=1/1");
            g_object_set(sink, "caps", caps, "max-buffers", 1, "drop", TRUE, NULL);
            gst_caps_unref(caps);
            char *uri = g_filename_to_uri(path, NULL, NULL);
            g_object_set(player, "uri", uri, "video-sink", sink, "audio-sink", audio, "flags", 1, NULL);
            g_free(uri);
            gst_element_set_state(player, GST_STATE_PAUSED);
            GstSample *sample = gst_app_sink_try_pull_preroll(GST_APP_SINK(sink), 2 * GST_SECOND);
            if (sample) {
                GstVideoInfo info;
                GstVideoFrame frame;
                if (gst_video_info_from_caps(&info, gst_sample_get_caps(sample)) &&
                    gst_video_frame_map(&frame, &info, gst_sample_get_buffer(sample), GST_MAP_READ)) {
                    GdkPixbuf *full = gdk_pixbuf_new_from_data(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0),
                        GDK_COLORSPACE_RGB, FALSE, 8, info.width, info.height,
                        GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0), NULL, NULL);
                    double scale = MIN(72.0 / info.width, 44.0 / info.height);
                    thumb = gdk_pixbuf_scale_simple(full, MAX(1, (int)(info.width * scale)),
                        MAX(1, (int)(info.height * scale)), GDK_INTERP_BILINEAR);
                    g_object_unref(full);
                    gst_video_frame_unmap(&frame);
                }
                gst_sample_unref(sample);
            }
            gst_element_set_state(player, GST_STATE_NULL);
        }
        if (player) gst_object_unref(player);
        if (sink) gst_object_unref(sink);
        if (audio) gst_object_unref(audio);
    }
    g_task_return_pointer(task, thumb, thumb ? g_object_unref : NULL);
}

static void thumbnail_done(GObject *image, GAsyncResult *result, gpointer data) {
    (void)data;
    GdkPixbuf *thumb = g_task_propagate_pointer(G_TASK(result), NULL);
    if (thumb) {
        gtk_image_set_from_pixbuf(GTK_IMAGE(image), thumb);
        g_object_unref(thumb);
    }
    thumbnail_jobs--;
}

static void playlist_thumbnails(ImgView *view) {
    if (!view->playlist_paths || thumbnail_jobs >= 2) return;
    while (view->thumbnail_next < view->playlist_paths->len && thumbnail_jobs < 2) {
        GtkWidget *row = GTK_WIDGET(gtk_list_box_get_row_at_index(GTK_LIST_BOX(view->playlist_list),
            view->thumbnail_next++));
        const char *path = g_object_get_data(G_OBJECT(row), "media-path");
        if (is_audio_ext(path) || g_object_get_data(G_OBJECT(row), "thumbnail-started")) continue;
        g_object_set_data(G_OBJECT(row), "thumbnail-started", GINT_TO_POINTER(1));
        GTask *task = g_task_new(g_object_get_data(G_OBJECT(row), "preview"), NULL, thumbnail_done, NULL);
        g_task_set_task_data(task, g_strdup(path), g_free);
        thumbnail_jobs++;
        g_task_run_in_thread(task, thumbnail_worker);
        g_object_unref(task);
    }
}

static void playlist_activate(GtkListBox *list, GtkListBoxRow *row, gpointer data) {
    (void)list;
    ImgView *view = data;
    int index = gtk_list_box_row_get_index(row);
    if (!view->playlist_paths || index < 0 || (guint)index >= view->playlist_paths->len) return;
    if (view->scan_id) { g_source_remove(view->scan_id); view->scan_id = 0; }
    g_clear_pointer(&view->pending_scan_dir, g_free);
    g_clear_pointer(&view->pending_scan_path, g_free);
    GPtrArray *paths = new_path_array();
    for (guint i = 0; i < view->playlist_paths->len; i++)
        g_ptr_array_add(paths, g_strdup(g_ptr_array_index(view->playlist_paths, i)));
    g_ptr_array_unref(view->paths);
    view->paths = paths;
    load_index(view, index);
}

static void playlist_refresh(ImgView *view) {
    if (!view->playlist_list || !view->paths->len) return;
    const char *current = g_ptr_array_index(view->paths, view->index);
    char *dir = g_path_get_dirname(current);
    if (g_strcmp0(dir, view->playlist_dir)) {
        g_free(view->playlist_dir); view->playlist_dir = g_strdup(dir);
        g_clear_pointer(&view->playlist_paths, g_ptr_array_unref);
        view->playlist_paths = new_path_array();
        add_dir_images(view->playlist_paths, dir);
        view->thumbnail_next = 0;
        GList *rows = gtk_container_get_children(GTK_CONTAINER(view->playlist_list));
        for (GList *item = rows; item; item = item->next) gtk_widget_destroy(item->data);
        g_list_free(rows);
        for (guint i = 0; i < view->playlist_paths->len; i++) {
            const char *path = g_ptr_array_index(view->playlist_paths, i);
            GtkWidget *row = gtk_list_box_row_new();
            GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
            GtkWidget *image = gtk_image_new_from_icon_name(is_audio_ext(path) ? "audio-x-generic" :
                is_video_ext(path) ? "video-x-generic" : "image-x-generic", GTK_ICON_SIZE_LARGE_TOOLBAR);
            gtk_widget_set_size_request(image, 72, 44);
            GtkWidget *labels = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
            GtkWidget *name = gtk_label_new(path_basename_ptr(path));
            gtk_label_set_xalign(GTK_LABEL(name), 0);
            gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_MIDDLE);
            gtk_label_set_width_chars(GTK_LABEL(name), 22);
            gtk_label_set_max_width_chars(GTK_LABEL(name), 22);
            const char *ext = strrchr(path_basename_ptr(path), '.');
            char *type = g_ascii_strup(ext ? ext + 1 : "media", -1);
            char *detail = g_strdup_printf("%s · %s", type,
                is_audio_ext(path) ? "audio" : is_video_ext(path) ? "video" : "image");
            GtkWidget *kind = gtk_label_new(detail);
            gtk_widget_set_name(kind, "playlist-type");
            gtk_label_set_xalign(GTK_LABEL(kind), 0);
            gtk_widget_set_valign(labels, GTK_ALIGN_CENTER);
            gtk_box_pack_start(GTK_BOX(labels), name, FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(labels), kind, FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(box), labels, TRUE, TRUE, 0);
            gtk_container_add(GTK_CONTAINER(row), box);
            g_object_set_data_full(G_OBJECT(row), "media-path", g_strdup(path), g_free);
            g_object_set_data(G_OBJECT(row), "preview", image);
            gtk_container_add(GTK_CONTAINER(view->playlist_list), row);
            g_free(type); g_free(detail);
        }
        gtk_widget_show_all(view->playlist_list);
    }
    for (guint i = 0; i < view->playlist_paths->len; i++) {
        if (!g_strcmp0(current, g_ptr_array_index(view->playlist_paths, i))) {
            gtk_list_box_select_row(GTK_LIST_BOX(view->playlist_list),
                gtk_list_box_get_row_at_index(GTK_LIST_BOX(view->playlist_list), i));
            break;
        }
    }
    g_free(dir);
    playlist_thumbnails(view);
}

static void playlist_init(ImgView *view) {
    view->playlist_revealer = overlay_revealer(view, GTK_REVEALER_TRANSITION_TYPE_SLIDE_RIGHT,
        GTK_ALIGN_START, GTK_ALIGN_FILL);
    gtk_widget_set_margin_top(view->playlist_revealer, 32);
    gtk_widget_set_margin_bottom(view->playlist_revealer, 40);
    GtkWidget *panel = gtk_event_box_new();
    gtk_widget_set_name(panel, "playlist-panel");
    gtk_widget_set_size_request(panel, 300, -1);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *title = gtk_label_new("PLAYLIST");
    gtk_label_set_xalign(GTK_LABEL(title), 0);
    gtk_widget_set_margin_start(title, 12);
    gtk_widget_set_margin_top(title, 12);
    gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 0);
    view->playlist_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(view->playlist_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    view->playlist_list = gtk_list_box_new();
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(view->playlist_list), TRUE);
    g_signal_connect(view->playlist_list, "row-activated", G_CALLBACK(playlist_activate), view);
    gtk_container_add(GTK_CONTAINER(view->playlist_scroll), view->playlist_list);
    gtk_box_pack_start(GTK_BOX(box), view->playlist_scroll, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(panel), box);
    gtk_container_add(GTK_CONTAINER(view->playlist_revealer), panel);
}

static void metadata_init(ImgView *view) {
    view->metadata_revealer = overlay_revealer(view, GTK_REVEALER_TRANSITION_TYPE_SLIDE_LEFT,
        GTK_ALIGN_END, GTK_ALIGN_FILL);
    gtk_widget_set_margin_top(view->metadata_revealer, 32);
    gtk_widget_set_margin_bottom(view->metadata_revealer, 40);
    GtkWidget *panel = gtk_event_box_new();
    gtk_widget_set_name(panel, "metadata-panel");
    gtk_widget_set_size_request(panel, 320, -1);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(box), 14);
    GtkWidget *title = gtk_label_new("METADATA");
    gtk_widget_set_name(title, "metadata-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0);
    gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 0);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    view->metadata_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(view->metadata_label), 0);
    gtk_label_set_yalign(GTK_LABEL(view->metadata_label), 0);
    gtk_label_set_line_wrap(GTK_LABEL(view->metadata_label), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(view->metadata_label), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_max_width_chars(GTK_LABEL(view->metadata_label), 36);
    gtk_label_set_selectable(GTK_LABEL(view->metadata_label), TRUE);
    gtk_container_add(GTK_CONTAINER(scroll), view->metadata_label);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(panel), box);
    gtk_container_add(GTK_CONTAINER(view->metadata_revealer), panel);
}

static void create_window(ImgView *view) {
    preview_cache_cleanup();
    view->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(view->window), "imgview");
    window_size_restore(view);
    view->header = create_header(view);
    gtk_window_set_titlebar(GTK_WINDOW(view->window), view->header);

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, CSS, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_object_unref(provider);

    canvas_init(&view->canvas, view);
    status_init(view);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    view->content_box = box;
    view->overlay = gtk_overlay_new();
    view->status_host = gtk_event_box_new();
    gtk_container_add(GTK_CONTAINER(view->status_host), view->status.box);
    gtk_box_pack_start(GTK_BOX(box), view->canvas.area, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), view->status_host, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(view->overlay), box);
    view->fullscreen_header = gtk_event_box_new();
    GtkWidget *fullscreen_bar = create_header(view);
    g_object_bind_property(view->header, "title", fullscreen_bar, "title", G_BINDING_SYNC_CREATE);
    gtk_container_add(GTK_CONTAINER(view->fullscreen_header), fullscreen_bar);
    view->header_revealer = overlay_revealer(view, GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN,
        GTK_ALIGN_FILL, GTK_ALIGN_START);
    gtk_container_add(GTK_CONTAINER(view->header_revealer), view->fullscreen_header);
    view->bottom_revealer = overlay_revealer(view, GTK_REVEALER_TRANSITION_TYPE_SLIDE_UP,
        GTK_ALIGN_FILL, GTK_ALIGN_END);
    metadata_init(view);
    playlist_init(view);
    gtk_container_add(GTK_CONTAINER(view->window), view->overlay);

    g_signal_connect(view->window, "key-press-event", G_CALLBACK(key_press), view);
    gtk_widget_add_events(view->window, GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
    g_signal_connect(view->window, "scroll-event", G_CALLBACK(volume_scroll), view);
    g_signal_connect(view->window, "destroy", G_CALLBACK(window_destroy), view);
    g_signal_connect(view->window, "configure-event", G_CALLBACK(window_size_changed), view);
    g_signal_connect(view->window, "window-state-event", G_CALLBACK(window_state_changed), view);

    gtk_widget_show_all(view->window);
    view->hover_id = g_timeout_add(80, fullscreen_hover_tick, view);
#ifdef GDK_WINDOWING_X11
    /* Move after mapping so GTK knows the client-decoration offsets. */
    if (view->has_window_position && GDK_IS_X11_DISPLAY(gtk_widget_get_display(view->window)))
        gtk_window_move(GTK_WINDOW(view->window), view->window_x, view->window_y);
#endif
    load_index(view, (gint)view->index);
    if (view->pending_scan_dir) {
        view->scan_id = g_idle_add_full(G_PRIORITY_LOW, finish_pending_scan, view, NULL);
    }
}

int main(int argc, char **argv) {
    GPtrArray *paths = NULL;
    guint start = 0;
    char *pending_scan_dir = NULL;
    char *pending_scan_path = NULL;
    resolve_args(argc, argv, &paths, &start, &pending_scan_dir, &pending_scan_path);

    if (!paths || paths->len == 0) {
        g_printerr("imgview: no images, videos or audio files found\n");
        g_printerr("usage: imgview <file|dir> [...]\n");
        if (paths) {
            g_ptr_array_unref(paths);
        }
        return 1;
    }

    ImgView *view = g_new0(ImgView, 1);
    view->paths = paths;
    view->index = start;
    view->pending_scan_dir = pending_scan_dir;
    view->pending_scan_path = pending_scan_path;

    gst_init(NULL, NULL);
    /* Prefer native compositor frame pacing on Wayland. Forcing XWayland
     * for window placement can leave GTK's frame clock at 60 Hz even on a
     * high-refresh display. GDK_BACKEND=x11 remains an explicit opt-in. */
    gdk_set_allowed_backends("wayland,x11,*");
    gtk_init(NULL, NULL);
    create_window(view);
    gtk_main();
    return 0;
}
