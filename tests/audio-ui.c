#include <gtk/gtk.h>
#include <gst/gst.h>

static GstElement *silent_factory(const gchar *factory, const gchar *name) {
    GstElement *element = gst_element_factory_make(factory, name);
    if (element && g_str_equal(factory, "playbin")) {
        GstElement *sink = gst_element_factory_make("fakesink", NULL);
        g_object_set(sink, "sync", TRUE, NULL);
        g_object_set(element, "audio-sink", sink, NULL);
    }
    return element;
}
#define gst_element_factory_make silent_factory
#define main imgview_main
#include "../src/imgview.c"
#undef main

static gboolean check_audio(gpointer data) {
    ImgView *view = data;
    g_assert_true(view->canvas.audio_only);
    g_assert_null(view->canvas.pixbuf);
    g_assert_true(gtk_widget_get_visible(view->status.progress));
    g_assert_true(gtk_widget_get_sensitive(view->status.progress));
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(view->status.dims)), ==, "Audio");
    const char *shot_path = g_getenv("IMGVIEW_TEST_SCREENSHOT");
    if (shot_path) {
        GdkPixbuf *shot = gdk_pixbuf_get_from_window(gtk_widget_get_window(view->window), 0, 0,
            gtk_widget_get_allocated_width(view->window), gtk_widget_get_allocated_height(view->window));
        g_assert_nonnull(shot);
        g_assert_true(gdk_pixbuf_save(shot, shot_path, "png", NULL, NULL));
        g_object_unref(shot);
    }
    gtk_widget_destroy(view->window);
    g_print("PASS: audio seeking controls and status UI\n");
    return G_SOURCE_REMOVE;
}

int main(int argc, char **argv) {
    g_assert_cmpint(argc, ==, 2);
    gst_init(NULL, NULL); gtk_init(NULL, NULL);
    ImgView *view = g_new0(ImgView, 1);
    view->paths = new_path_array();
    g_ptr_array_add(view->paths, g_canonicalize_filename(argv[1], NULL));
    create_window(view);
    g_timeout_add(800, check_audio, view);
    gtk_main();
    return 0;
}
