/* Live display pacing probe; opens only a temporary canvas, without touching
 * the user's saved window geometry. Run with GDK_BACKEND=wayland or x11. */
#define main imgview_main
#include "../src/imgview.c"
#undef main

typedef struct {
    Canvas canvas;
    guint ticks, draws;
    bool zoom_test;
    int zoom_steps;
    gint64 start, interval;
} Probe;

static gboolean count_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    Probe *p = data;
    p->draws++;
    return canvas_draw(widget, cr, &p->canvas);
}

static gboolean move_frame(GtkWidget *widget, GdkFrameClock *clock, gpointer data) {
    Probe *p = data;
    gint64 now = g_get_monotonic_time();
    if (!p->start) p->start = now;
    gdk_frame_clock_get_refresh_info(clock, gdk_frame_clock_get_frame_time(clock), &p->interval, NULL);
    double t = (now - p->start) / 1000000.0;
    GdkEventMotion event = {.x = 100 + 90 * sin(t * 3), .y = 100 + 60 * cos(t * 3)};
    if (p->zoom_test) {
        int step = (int)(t / 0.3);
        if (step < 10 && step >= p->zoom_steps) {
            canvas_zoom_step(&p->canvas, step % 2 ? 1.0 / 1.2 : 1.2, 400, 300);
            p->zoom_steps = step + 1;
        }
    } else canvas_motion(widget, &event, &p->canvas);
    p->ticks++;
    if (t >= 3) {
        GdkDisplay *display = gtk_widget_get_display(widget);
        GdkMonitor *monitor = gdk_display_get_monitor_at_window(display, gtk_widget_get_window(widget));
        g_print("%s: monitor %.1f Hz, frame-clock %.1f Hz, %.1f updates/s, %.1f draws/s\n",
            G_OBJECT_TYPE_NAME(display), gdk_monitor_get_refresh_rate(monitor) / 1000.0,
            p->interval > 0 ? 1000000.0 / p->interval : 0, p->ticks / t, p->draws / t);
        if (p->zoom_test) {
            g_assert_cmpuint(p->canvas.zoom_id, ==, 0);
            g_assert_cmpfloat_with_epsilon(p->canvas.zoom, 0.5, 1e-12);
            g_print("PASS: ten display-paced zoom transitions completed and released their tick callback\n");
        }
        gtk_main_quit();
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);
    Probe p = {.zoom_test = argc > 1 && g_str_equal(argv[1], "--zoom")};
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "imgview refresh-rate test (3 seconds)");
    gtk_window_set_default_size(GTK_WINDOW(window), 1100, 780);
    p.canvas.area = gtk_drawing_area_new();
    p.canvas.checker_pattern = create_checker_pattern();
    gtk_container_add(GTK_CONTAINER(window), p.canvas.area);
    g_signal_connect(p.canvas.area, "draw", G_CALLBACK(count_draw), &p);
    gtk_widget_show_all(window);
    p.canvas.pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 1920, 1080);
    gdk_pixbuf_fill(p.canvas.pixbuf, 0x668899ff);
    canvas_update_surface(&p.canvas);
    p.canvas.zoom = 0.5;
    p.canvas.dragging = !p.zoom_test;
    gtk_widget_add_tick_callback(p.canvas.area, move_frame, &p, NULL);
    gtk_main();
    canvas_clear_image(&p.canvas);
    cairo_pattern_destroy(p.canvas.checker_pattern);
    gtk_widget_destroy(window);
    return 0;
}
