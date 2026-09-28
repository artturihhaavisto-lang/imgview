/* Headless rendering benchmark and cache/drag regression checks. */
#include <gtk/gtk.h>
static int damage_count;
static void allocation(GtkWidget *w, GtkAllocation *a) {
    (void)w; *a = (GtkAllocation){0, 0, 1920, 1080};
}
static int scale_factor(GtkWidget *w) { (void)w; return 1; }
static void damage(GtkWidget *w, int x, int y, int width, int height) {
    (void)w;
    g_assert_cmpint(x, >=, 0); g_assert_cmpint(y, >=, 0);
    g_assert_cmpint(x + width, <=, 1920);
    g_assert_cmpint(y + height, <=, 1080);
    damage_count++;
}
#define gtk_widget_get_allocation allocation
#define gtk_widget_get_scale_factor scale_factor
#define gtk_widget_queue_draw_area damage
#define main imgview_main
#include "../src/imgview.c"
#undef main

static double benchmark(Canvas *c, cairo_t *cr) {
    gint64 start = g_get_monotonic_time();
    for (int i = 0; i < 120; i++) {
        c->ox = i; c->oy = i / 2;
        canvas_draw(NULL, cr, c);
    }
    return (g_get_monotonic_time() - start) / 120000.0;
}

int main(void) {
    /* Equal elapsed time gives equal zoom and cursor anchoring regardless
     * of how many display ticks occurred; late ticks finish exactly. */
    const int rates[] = {60, 144, 240};
    for (guint r = 0; r < G_N_ELEMENTS(rates); r++) {
        Canvas z = {.zoom_from = 0.5, .zoom_target = 2.0, .zoom_start = 1000000,
            .zoom_cx = 400, .zoom_cy = 300, .zoom_ix = 120, .zoom_iy = 80};
        for (gint64 elapsed = 0; elapsed < 60000; elapsed += 1000000 / rates[r])
            g_assert_true(canvas_zoom_at(&z, z.zoom_start + elapsed));
        g_assert_true(canvas_zoom_at(&z, z.zoom_start + 60000));
        g_assert_cmpfloat_with_epsilon(z.zoom, exp(log(0.5) + log(4.0) * 0.875), 1e-12);
        g_assert_cmpfloat_with_epsilon((400 - z.ox) / z.zoom, 120, 1e-12);
        g_assert_cmpfloat_with_epsilon((300 - z.oy) / z.zoom, 80, 1e-12);
        g_assert_false(canvas_zoom_at(&z, z.zoom_start + 200000));
        g_assert_cmpfloat(z.zoom, ==, 2.0);
    }
    Canvas c = {0};
    c.pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 3840, 2160);
    gdk_pixbuf_fill(c.pixbuf, 0x789abcff);
    c.image_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 3840, 2160);
    cairo_t *source = cairo_create(c.image_surface);
    cairo_set_source_rgb(source, 0.47, 0.60, 0.74);
    cairo_paint(source);
    cairo_destroy(source);
    c.checker_pattern = create_checker_pattern();
    c.zoom = 0.4;
    canvas_prepare_scaled(&c, 1);
    g_assert_nonnull(c.scaled_surface);
    cairo_surface_t *cached = c.scaled_surface;
    canvas_prepare_scaled(&c, 1);
    g_assert_true(cached == c.scaled_surface);
    c.dragging = true;
    canvas_drag_to(&c, 2, 2);
    g_assert_cmpint(damage_count, ==, 2);
    g_assert_cmpfloat(c.ox, ==, 2);
    g_assert_false(c.drag_moved);
    canvas_drag_to(&c, 100, 50);
    g_assert_cmpint(damage_count, ==, 4);
    g_assert_cmpfloat(c.ox, ==, 100);
    GdkEventButton release = {.button = 1, .x = 120, .y = 60};
    canvas_button_release(NULL, &release, &c);
    g_assert_cmpfloat(c.ox, ==, 120);
    g_assert_false(c.dragging);
    cairo_surface_t *target = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 1920, 1080);
    cairo_t *cr = cairo_create(target);
    double fast = benchmark(&c, cr);
    g_clear_pointer(&c.scaled_surface, cairo_surface_destroy);
    c.zoom_id = 1; /* Intermediate zoom frames deliberately skip caching. */
    double original = benchmark(&c, cr);
    g_assert_null(c.scaled_surface);
    c.zoom_id = 0;
    c.player = (GstElement *)&c; /* Video frames also reuse the scaled surface. */
    canvas_prepare_scaled(&c, 1);
    g_assert_nonnull(c.scaled_surface);
    cached = c.scaled_surface;
    canvas_prepare_scaled(&c, 1);
    g_assert_true(cached == c.scaled_surface);
    c.player = NULL;
    c.zoom = 32;
    canvas_prepare_scaled(&c, 1);
    g_assert_null(c.scaled_surface);
    c.zoom = 0.5;
    canvas_prepare_scaled(&c, 2);
    g_assert_null(c.scaled_surface); /* Native device pixels need no cache. */
    c.zoom = 0.25;
    canvas_prepare_scaled(&c, 2);
    g_assert_nonnull(c.scaled_surface);
    g_assert_cmpint(cairo_image_surface_get_width(c.scaled_surface), ==, 1920);
    canvas_clear_image(&c);
    g_assert_null(c.scaled_surface);
    cairo_pattern_destroy(c.checker_pattern);
    cairo_destroy(cr);
    cairo_surface_destroy(target);
    g_print("PASS: cache reuse/bounds/HiDPI/cleanup, drag damage and release; "
            "4K image panning: %.2f ms/frame cached vs %.2f ms/frame uncached (CPU rendering)\n", fast, original);
    return 0;
}
