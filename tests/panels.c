/* Live panel transition pacing and layout regression probe. */
#include <gtk/gtk.h>
static gint64 conversion_us;
static cairo_surface_t *profile_surface(const GdkPixbuf *p, int scale, GdkWindow *w) {
    gint64 start = g_get_monotonic_time();
    cairo_surface_t *s = gdk_cairo_surface_create_from_pixbuf(p, scale, w);
    conversion_us += g_get_monotonic_time() - start;
    return s;
}
#define gdk_cairo_surface_create_from_pixbuf profile_surface
#define main imgview_app_main
#include "../src/imgview.c"
#undef main

typedef struct {
    ImgView *view;
    guint stage, draws, layouts, ticks;
    bool pause, area_finalized;
    gint64 canvas_us, scaling_us;
    guint canvas_draws;
    gint64 start;
} PanelProbe;

static gboolean profile_canvas(GtkWidget *w, cairo_t *cr, gpointer data) {
    PanelProbe *p = data;
    gint64 scale_start = g_get_monotonic_time();
    canvas_prepare_scaled(&p->view->canvas, gtk_widget_get_scale_factor(w));
    p->scaling_us += g_get_monotonic_time() - scale_start;
    gint64 start = g_get_monotonic_time();
    gboolean result = canvas_draw(w, cr, &p->view->canvas);
    p->canvas_us += g_get_monotonic_time() - start;
    p->canvas_draws++;
    return result;
}
static gboolean panel_draw_count(GtkWidget *w, cairo_t *cr, gpointer data) {
    (void)w; (void)cr;
    ((PanelProbe *)data)->draws++;
    return FALSE;
}
static void panel_layout_count(GtkWidget *w, GtkAllocation *a, gpointer data) {
    (void)w; (void)a;
    ((PanelProbe *)data)->layouts++;
}
static void area_finalized(gpointer data, GObject *object) {
    (void)object;
    *(bool *)data = true;
}
static gboolean probe_step(gpointer data) {
    PanelProbe *p = data;
    ImgView *v = p->view;
    if (!p->stage) {
        if (v->hover_id) { g_source_remove(v->hover_id); v->hover_id = 0; }
        toggle_fullscreen(v);
    } else if (p->stage == 1) {
        if (p->pause) video_toggle_pause(&v->canvas);
        p->draws = p->layouts = p->ticks = 0;
        p->canvas_us = p->scaling_us = conversion_us = 0; p->canvas_draws = 0;
        p->start = g_get_monotonic_time();
        g_print("CANVAS: %d x %d, scale %d, zoom %.3f\n", gtk_widget_get_allocated_width(v->canvas.area),
            gtk_widget_get_allocated_height(v->canvas.area), gtk_widget_get_scale_factor(v->canvas.area), v->canvas.zoom);
    }
    if (p->stage >= 1 && p->stage <= 6) {
        gboolean open = p->stage % 2;
        gtk_revealer_set_reveal_child(GTK_REVEALER(v->playlist_revealer), open);
        gtk_revealer_set_reveal_child(GTK_REVEALER(v->metadata_revealer), open);
        gtk_revealer_set_reveal_child(GTK_REVEALER(v->bottom_revealer), open);
    }
    if (p->stage++ == 7) {
        g_print("PANELS: %u display ticks, %u panel draws, %u panel allocations over %.2f seconds\n",
            p->ticks, p->draws, p->layouts, (g_get_monotonic_time() - p->start) / 1e6);
        g_print("CPU: canvas %.1f ms (%u draws), conversion %.1f ms, scaling %.1f ms\n", p->canvas_us / 1000.0, p->canvas_draws, conversion_us / 1000.0, p->scaling_us / 1000.0);
        if (v->canvas.player) {
            g_assert_true(v->canvas.video_frame_clock);
            /* Exercise cancellation while a worker still owns the frame. */
            g_clear_object(&v->canvas.video_scaled_frame);
            canvas_scale_video(&v->canvas, 1, 320, 180);
            g_assert_true(v->canvas.video_scale_busy);
            GCancellable *cancel = g_object_ref(v->canvas.video_scale_cancel);
            canvas_clear_image(&v->canvas);
            g_assert_true(g_cancellable_is_cancelled(cancel));
            g_object_unref(cancel);
        }
        gtk_widget_destroy(v->window);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}
static gboolean probe_tick(GtkWidget *w, GdkFrameClock *c, gpointer data) {
    (void)w; (void)c;
    ((PanelProbe *)data)->ticks++;
    return G_SOURCE_CONTINUE;
}
int main(int argc, char **argv) {
    char *config = g_dir_make_tmp("imgview-panels-XXXXXX", NULL);
    g_assert_nonnull(config);
    g_setenv("XDG_CONFIG_HOME", config, TRUE);
    gtk_init(&argc, &argv); gst_init(NULL, NULL);
    ImgView *v = g_new0(ImgView, 1);
    v->paths = new_path_array();
    g_ptr_array_add(v->paths, g_strdup(argc > 1 ? argv[1] : "build/test.png"));
    create_window(v);
    PanelProbe p = {.view = v, .pause = argc > 2};
    g_object_weak_ref(G_OBJECT(v->canvas.area), area_finalized, &p.area_finalized);
    union { GCallback callback; gpointer pointer; } draw_handler = {.callback = G_CALLBACK(canvas_draw)};
    g_signal_handlers_disconnect_matched(v->canvas.area, G_SIGNAL_MATCH_FUNC | G_SIGNAL_MATCH_DATA,
        0, 0, NULL, draw_handler.pointer, &v->canvas);
    g_signal_connect(v->canvas.area, "draw", G_CALLBACK(profile_canvas), &p);
    GtkWidget *panels[] = {v->playlist_revealer, v->metadata_revealer, v->bottom_revealer};
    for (guint i = 0; i < G_N_ELEMENTS(panels); i++) {
        g_signal_connect(panels[i], "draw", G_CALLBACK(panel_draw_count), &p);
        g_signal_connect(panels[i], "size-allocate", G_CALLBACK(panel_layout_count), &p);
    }
    gtk_widget_add_tick_callback(v->window, probe_tick, &p, NULL);
    g_timeout_add(350, probe_step, &p);
    gtk_main();
    gint64 deadline = g_get_monotonic_time() + G_USEC_PER_SEC;
    while (!p.area_finalized && g_get_monotonic_time() < deadline) {
        while (g_main_context_iteration(NULL, FALSE)) {}
        g_usleep(1000);
    }
    g_assert_true(p.area_finalized);
    char *settings = g_build_filename(config, "imgview", "window.ini", NULL);
    char *directory = g_build_filename(config, "imgview", NULL);
    g_remove(settings); g_rmdir(directory); g_rmdir(config);
    g_free(settings); g_free(directory); g_free(config);
    g_print("PASS: display-paced video panels and safe worker cancellation/cleanup\n");
    return 0;
}
