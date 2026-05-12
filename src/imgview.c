#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>

#define ZOOM_STEP 1.25
#define ZOOM_MIN 0.005
#define ZOOM_MAX 32.0

static const char *CSS =
    "window { background: #0e0e0e; }"
    "#statusbar {"
    "  background: rgba(12,12,12,0.95);"
    "  border-top: 1px solid #202020;"
    "  padding: 3px 10px;"
    "}"
    "#statusbar label {"
    "  font-family: monospace;"
    "  font-size: 11px;"
    "  color: #666;"
    "}"
    "#st-name { color: #bbb; }"
    "#st-index { color: #555; }"
    "#st-dims { color: #666; }"
    "#st-zoom { color: #888; min-width: 44px; }";

typedef struct {
    GtkWidget *area;
    GdkPixbuf *pixbuf;
    cairo_surface_t *image_surface;
    cairo_pattern_t *checker_pattern;
    double zoom;
    double ox;
    double oy;
    bool dragging;
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
    GtkWidget *box;
} StatusBar;

typedef struct {
    GtkWidget *window;
    Canvas canvas;
    StatusBar status;
    GPtrArray *paths;
    char *pending_scan_dir;
    char *pending_scan_path;
    guint index;
    guint slideshow_id;
    bool fullscreen;
} ImgView;

static bool is_image_ext(const char *path) {
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
    g_clear_object(&canvas->pixbuf);
    g_clear_pointer(&canvas->image_surface, cairo_surface_destroy);
}

static void canvas_update_surface(Canvas *canvas) {
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
    scale = fmin(scale, 1.0);

    if (force || pw * canvas->zoom > alloc.width || ph * canvas->zoom > alloc.height) {
        canvas->zoom = scale;
    }
    canvas->ox = (alloc.width - pw * canvas->zoom) / 2.0;
    canvas->oy = (alloc.height - ph * canvas->zoom) / 2.0;
    gtk_widget_queue_draw(canvas->area);
}

static char *canvas_load(Canvas *canvas, const char *path) {
    GError *error = NULL;
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(path, &error);

    if (!pixbuf) {
        canvas_clear_image(canvas);
        gtk_widget_queue_draw(canvas->area);
        char *message = g_strdup(error ? error->message : "failed to load image");
        g_clear_error(&error);
        return message;
    }

    canvas_clear_image(canvas);
    canvas->pixbuf = pixbuf;
    canvas_update_surface(canvas);
    canvas->zoom = 1.0;
    canvas_fit(canvas, true);
    return NULL;
}

static void canvas_actual_size(Canvas *canvas) {
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

    GdkPixbufRotation rotation = clockwise
        ? GDK_PIXBUF_ROTATE_CLOCKWISE
        : GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE;
    GdkPixbuf *rotated = gdk_pixbuf_rotate_simple(canvas->pixbuf, rotation);
    if (!rotated) {
        return;
    }
    g_object_unref(canvas->pixbuf);
    canvas->pixbuf = rotated;
    canvas_update_surface(canvas);
    canvas_fit(canvas, true);
}

static void canvas_flip_h(Canvas *canvas) {
    if (!canvas->pixbuf) {
        return;
    }

    GdkPixbuf *flipped = gdk_pixbuf_flip(canvas->pixbuf, TRUE);
    if (!flipped) {
        return;
    }
    g_object_unref(canvas->pixbuf);
    canvas->pixbuf = flipped;
    canvas_update_surface(canvas);
    gtk_widget_queue_draw(canvas->area);
}

static gboolean canvas_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    Canvas *canvas = data;
    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);

    cairo_set_source(cr, canvas->checker_pattern);
    cairo_rectangle(cr, 0, 0, alloc.width, alloc.height);
    cairo_fill(cr);

    if (!canvas->pixbuf) {
        const char *msg = "No image";
        cairo_text_extents_t extents;
        cairo_set_source_rgba(cr, 0.45, 0.45, 0.45, 0.7);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 16);
        cairo_text_extents(cr, msg, &extents);
        cairo_move_to(cr, (alloc.width - extents.width) / 2.0, (alloc.height + extents.height) / 2.0);
        cairo_show_text(cr, msg);
        return TRUE;
    }

    cairo_save(cr);
    cairo_translate(cr, canvas->ox, canvas->oy);
    cairo_scale(cr, canvas->zoom, canvas->zoom);
    cairo_set_source_surface(cr, canvas->image_surface, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
    cairo_paint(cr);
    cairo_restore(cr);
    return TRUE;
}

static gboolean canvas_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    (void)widget;
    Canvas *canvas = data;
    if (event->button == 1) {
        canvas->dragging = true;
        canvas->drag_x = event->x;
        canvas->drag_y = event->y;
        canvas->drag_ox = canvas->ox;
        canvas->drag_oy = canvas->oy;
    }
    return TRUE;
}

static gboolean canvas_button_release(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    (void)widget;
    (void)event;
    Canvas *canvas = data;
    canvas->dragging = false;
    return TRUE;
}

static gboolean canvas_motion(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
    (void)widget;
    Canvas *canvas = data;
    if (canvas->dragging) {
        canvas->ox = canvas->drag_ox + event->x - canvas->drag_x;
        canvas->oy = canvas->drag_oy + event->y - canvas->drag_y;
        gtk_widget_queue_draw(canvas->area);
    }
    return TRUE;
}

static void status_update(ImgView *view, const char *path, const char *error) {
    GdkPixbuf *pixbuf = view->canvas.pixbuf;

    if (error) {
        char *text = g_strdup_printf("Error: %s", error);
        gtk_label_set_text(GTK_LABEL(view->status.name), text);
        gtk_label_set_text(GTK_LABEL(view->status.dims), "");
        g_free(text);
    } else if (path) {
        char *base = g_path_get_basename(path);
        gtk_label_set_text(GTK_LABEL(view->status.name), base);
        g_free(base);

        if (pixbuf) {
            char *dims = g_strdup_printf("%dx%d", gdk_pixbuf_get_width(pixbuf), gdk_pixbuf_get_height(pixbuf));
            gtk_label_set_text(GTK_LABEL(view->status.dims), dims);
            g_free(dims);
        }
    }

    if (view->paths->len > 0) {
        char *index = g_strdup_printf("%u/%u", view->index + 1, view->paths->len);
        gtk_label_set_text(GTK_LABEL(view->status.index), index);
        g_free(index);
    }

    char *zoom = g_strdup_printf("%.0f%%", view->canvas.zoom * 100.0);
    gtk_label_set_text(GTK_LABEL(view->status.zoom), zoom);
    g_free(zoom);
}

static gboolean canvas_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer data) {
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
    canvas_fit(data, false);
    return FALSE;
}

static void canvas_init(Canvas *canvas, ImgView *view) {
    canvas->area = gtk_drawing_area_new();
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
    g_signal_connect(canvas->area, "configure-event", G_CALLBACK(canvas_configure), canvas);
}

static void status_init(StatusBar *status) {
    status->box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_widget_set_name(status->box, "statusbar");

    status->name = gtk_label_new("");
    status->index = gtk_label_new("");
    status->dims = gtk_label_new("");
    status->zoom = gtk_label_new("");

    gtk_widget_set_name(status->name, "st-name");
    gtk_widget_set_name(status->index, "st-index");
    gtk_widget_set_name(status->dims, "st-dims");
    gtk_widget_set_name(status->zoom, "st-zoom");

    gtk_label_set_xalign(GTK_LABEL(status->name), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(status->name), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_hexpand(status->name, TRUE);

    gtk_box_pack_start(GTK_BOX(status->box), status->name, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(status->box), status->index, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(status->box), status->dims, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(status->box), status->zoom, FALSE, FALSE, 0);
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
    char *error = canvas_load(&view->canvas, path);
    status_update(view, path, error);

    char *base = g_path_get_basename(path);
    char *title = g_strdup_printf("imgview - %s", base);
    gtk_window_set_title(GTK_WINDOW(view->window), title);
    g_free(title);
    g_free(base);
    g_free(error);
}

static gboolean finish_pending_scan(gpointer data) {
    ImgView *view = data;
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

static void toggle_fullscreen(ImgView *view) {
    if (view->fullscreen) {
        gtk_window_unfullscreen(GTK_WINDOW(view->window));
    } else {
        gtk_window_fullscreen(GTK_WINDOW(view->window));
    }
    view->fullscreen = !view->fullscreen;
}

static gboolean key_press(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    (void)widget;
    ImgView *view = data;
    Canvas *canvas = &view->canvas;

    switch (event->keyval) {
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
    case GDK_KEY_f:
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
    case GDK_KEY_F11:
        toggle_fullscreen(view);
        break;
    case GDK_KEY_i:
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

static void view_destroy(gpointer data) {
    ImgView *view = data;
    if (view->slideshow_id) {
        g_source_remove(view->slideshow_id);
    }
    canvas_clear_image(&view->canvas);
    g_clear_pointer(&view->canvas.checker_pattern, cairo_pattern_destroy);
    g_clear_pointer(&view->pending_scan_dir, g_free);
    g_clear_pointer(&view->pending_scan_path, g_free);
    g_ptr_array_unref(view->paths);
    g_free(view);
}

static void window_destroy(GtkWidget *widget, gpointer data) {
    (void)widget;
    view_destroy(data);
    gtk_main_quit();
}

static void create_window(ImgView *view) {
    view->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(view->window), "imgview");
    gtk_window_set_default_size(GTK_WINDOW(view->window), 1100, 780);

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, CSS, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_object_unref(provider);

    canvas_init(&view->canvas, view);
    status_init(&view->status);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(box), view->canvas.area, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), view->status.box, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(view->window), box);

    g_signal_connect(view->window, "key-press-event", G_CALLBACK(key_press), view);
    g_signal_connect(view->window, "destroy", G_CALLBACK(window_destroy), view);

    gtk_widget_show_all(view->window);
    load_index(view, (gint)view->index);
    if (view->pending_scan_dir) {
        g_idle_add_full(G_PRIORITY_LOW, finish_pending_scan, view, NULL);
    }
}

int main(int argc, char **argv) {
    GPtrArray *paths = NULL;
    guint start = 0;
    char *pending_scan_dir = NULL;
    char *pending_scan_path = NULL;
    resolve_args(argc, argv, &paths, &start, &pending_scan_dir, &pending_scan_path);

    if (!paths || paths->len == 0) {
        g_printerr("imgview: no images found\n");
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

    gtk_init(NULL, NULL);
    create_window(view);
    gtk_main();
    return 0;
}
