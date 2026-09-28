/* Headless integration test: real decoders/timers, stubbed GTK drawing and
 * labels, and a synchronized fake audio output to avoid playing test tones. */
#include <gtk/gtk.h>
#include <gst/gst.h>

static GdkWindow *test_window(GtkWidget *widget) { (void)widget; return NULL; }
static void test_draw(GtkWidget *widget) { (void)widget; }
static int test_width = 640, test_height = 480;
static void test_allocation(GtkWidget *widget, GtkAllocation *a) {
    (void)widget; *a = (GtkAllocation){0, 0, test_width, test_height};
}
static cairo_surface_t *test_surface(const GdkPixbuf *p, int scale, GdkWindow *w) {
    (void)scale; (void)w;
    return cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
        gdk_pixbuf_get_width(p), gdk_pixbuf_get_height(p));
}
static char *last_error;
static void test_label(GtkLabel *label, const char *text) {
    (void)label;
    if (g_str_has_prefix(text, "Error:")) {
        g_free(last_error);
        last_error = g_strdup(text);
    }
}
static const char *test_label_text(GtkLabel *label) { (void)label; return ""; }
static GstElement *test_factory(const char *factory, const char *name) {
    GstElement *element = gst_element_factory_make(factory, name);
    if (element && g_str_equal(factory, "playbin")) {
        GstElement *sink = gst_element_factory_make("fakesink", NULL);
        g_assert_nonnull(sink);
        gst_object_ref_sink(sink);
        g_object_set(sink, "sync", TRUE, NULL);
        g_object_set(element, "audio-sink", sink, NULL);
        gst_object_unref(sink);
    }
    return element;
}
#define gtk_widget_get_window test_window
#define gtk_widget_queue_draw test_draw
#define gtk_widget_get_allocation test_allocation
#define gdk_cairo_surface_create_from_pixbuf test_surface
#define gtk_label_set_text test_label
#define gtk_label_get_text test_label_text
#define gst_element_factory_make test_factory
#define main imgview_main
#include "../src/imgview.c"
#undef main

static void pump(int milliseconds) {
    gint64 end = g_get_monotonic_time() + milliseconds * 1000;
    do {
        while (g_main_context_iteration(NULL, FALSE)) {}
        g_usleep(1000);
    } while (g_get_monotonic_time() < end);
}

static guint64 pixels_hash(GdkPixbuf *p) {
    guint64 hash = 5381;
    for (int y = 0; y < gdk_pixbuf_get_height(p); y++) {
        const guchar *row = gdk_pixbuf_get_pixels(p) + y * gdk_pixbuf_get_rowstride(p);
        for (int x = 0; x < gdk_pixbuf_get_width(p) * gdk_pixbuf_get_n_channels(p); x++)
            hash = hash * 33 + row[x];
    }
    return hash;
}

int main(int argc, char **argv) {
    g_assert_cmpint(argc, >=, 5);
    gst_init(NULL, NULL);
    ImgView view = {0};
    view.paths = new_path_array();
    Canvas *c = &view.canvas;
    g_assert_true(is_media_file(argv[1]));
    g_assert_true(is_video_ext("UPPER.MP4"));
    g_assert_null(video_load(&view, argv[1]));
    for (int i = 0; !c->pixbuf && i < 500; i++) pump(10);
    if (last_error) g_error("%s", last_error);
    g_assert_nonnull(c->pixbuf);
    g_assert_cmpint(gdk_pixbuf_get_width(c->pixbuf), ==, 160);
    double fitted_zoom = c->zoom;
    canvas_zoom_step(c, 0.75, -1, -1);
    double relative_zoom = c->zoom / c->video_fit_scale;
    test_width = 320; test_height = 240;
    canvas_fit(c, false);
    g_assert_cmpfloat(fabs(c->zoom - fitted_zoom * 0.75 * 0.5), <, 0.00001);
    g_assert_cmpfloat(fabs(c->zoom / c->video_fit_scale - relative_zoom), <, 0.00001);
    test_width = 960; test_height = 720;
    canvas_fit(c, false);
    g_assert_cmpfloat(fabs(c->zoom - fitted_zoom * 0.75 * 1.5), <, 0.00001);
    test_width = 640; test_height = 480;
    canvas_fit(c, false);
    g_assert_cmpfloat(fabs(c->zoom - fitted_zoom * 0.75), <, 0.00001);
    guint64 first = pixels_hash(c->pixbuf);
    pump(250);
    g_assert_true(first != pixels_hash(c->pixbuf));

    video_toggle_pause(c);
    pump(150);
    g_assert_true(c->paused);
    gint64 before, after;
    g_assert_true(gst_element_query_position(c->player, GST_FORMAT_TIME, &before));
    first = pixels_hash(c->pixbuf);
    pump(200);
    g_assert_true(gst_element_query_position(c->player, GST_FORMAT_TIME, &after));
    g_assert_cmpint(llabs(after - before), <, 50 * GST_MSECOND);
    g_assert_true(first == pixels_hash(c->pixbuf));
    video_seek(c, 5);
    pump(300);
    g_assert_true(gst_element_query_position(c->player, GST_FORMAT_TIME, &after));
    g_assert_cmpint(after, >, 4 * GST_SECOND);
    g_assert_true(first != pixels_hash(c->pixbuf));

    canvas_rotate(c, true);
    canvas_flip_h(c);
    c->zoom = 2; c->ox = 17; c->oy = 23;
    video_toggle_pause(c);
    pump(250);
    g_assert_cmpint(gdk_pixbuf_get_width(c->pixbuf), ==, 90);
    g_assert_cmpint(gdk_pixbuf_get_height(c->pixbuf), ==, 160);
    g_assert_true(c->flipped);
    g_assert_cmpfloat(c->zoom, ==, 2);
    g_assert_cmpfloat(c->ox, ==, 17);
    g_assert_cmpfloat(c->oy, ==, 23);
    GdkEventKey mute = {.keyval = GDK_KEY_m};
    key_press(NULL, &mute, &view);
    gboolean muted = FALSE;
    g_object_get(c->player, "mute", &muted, NULL);
    g_assert_true(muted);

    video_seek(c, 100);
    for (int i = 0; !c->ended && i < 300; i++) pump(10);
    g_assert_true(c->ended);
    g_assert_nonnull(c->pixbuf);
    video_toggle_pause(c);
    pump(250);
    g_assert_false(c->paused);
    g_assert_false(c->ended);
    g_assert_true(gst_element_query_position(c->player, GST_FORMAT_TIME, &after));
    g_assert_cmpint(after, <, 2 * GST_SECOND);

    guint timer = c->video_id;
    g_assert_null(canvas_load(c, argv[2]));
    g_assert_null(g_main_context_find_source_by_id(NULL, timer));
    g_assert_null(c->player);
    g_assert_nonnull(c->gif_stream);
    first = pixels_hash(c->pixbuf);
    pump(150);
    g_assert_true(first != pixels_hash(c->pixbuf));
    timer = c->animation_id;
    g_assert_null(video_load(&view, argv[1]));
    g_assert_null(g_main_context_find_source_by_id(NULL, timer));
    timer = c->video_id;
    g_assert_null(canvas_load(c, argv[3]));
    g_assert_null(g_main_context_find_source_by_id(NULL, timer));
    g_assert_null(c->animation_iter);
    g_assert_cmpint(c->rotation, ==, 0);
    g_assert_false(c->flipped);

    char *error = video_load(&view, "/nonexistent/imgview-test.mp4");
    if (!error) {
        for (int i = 0; c->player && i < 300; i++) pump(10);
        g_assert_nonnull(last_error);
    }
    g_free(error);
    g_assert_null(c->player);
    g_assert_cmpuint(c->video_id, ==, 0);
    g_assert_null(video_load(&view, argv[1]));
    timer = c->video_id;
    canvas_clear_image(c);
    g_assert_null(g_main_context_find_source_by_id(NULL, timer));
    pump(50);
    c->volume = 0.5;
    c->muted = false;
    g_assert_true(is_audio_ext("TRACK.MP3"));
    g_assert_true(is_media_file(argv[4]));
    g_assert_null(video_load(&view, argv[4]));
    g_assert_true(c->audio_only);
    pump(400);
    g_assert_nonnull(c->player);
    g_assert_true(gst_element_query_position(c->player, GST_FORMAT_TIME, &after));
    g_assert_cmpint(after, >, 0);
    video_toggle_pause(c);
    pump(100);
    g_assert_true(gst_element_query_position(c->player, GST_FORMAT_TIME, &before));
    pump(150);
    g_assert_true(gst_element_query_position(c->player, GST_FORMAT_TIME, &after));
    g_assert_cmpint(llabs(after - before), <, 50 * GST_MSECOND);
    video_seek(c, 5);
    pump(100);
    g_assert_true(gst_element_query_position(c->player, GST_FORMAT_TIME, &after));
    g_assert_cmpint(after, >, 4 * GST_SECOND);
    video_toggle_pause(c);
    video_seek(c, 100);
    for (int i = 0; !c->ended && i < 300; i++) pump(10);
    g_assert_true(c->ended);
    video_toggle_pause(c);
    pump(150);
    g_assert_false(c->ended);
    g_assert_null(canvas_load(c, argv[2]));
    g_assert_false(c->audio_only);
    canvas_clear_image(c);
    for (int i = 5; i < argc; i++) {
        g_assert_true(is_media_file(argv[i]));
        if (is_video_ext(argv[i])) {
            g_assert_null(video_load(&view, argv[i]));
            for (int j = 0; !c->pixbuf && j < 500; j++) pump(10);
        } else g_assert_null(canvas_load(c, argv[i]));
        g_assert_nonnull(c->pixbuf);
        g_assert_cmpint(gdk_pixbuf_get_width(c->pixbuf), ==, 160);
        canvas_clear_image(c);
    }
    g_ptr_array_unref(view.paths);
    g_free(last_error);
    g_print("PASS: video frames, audio playback, pause, seek, transforms, mute, EOF/replay, GIF/static switching, errors, cleanup\n");
    return 0;
}
