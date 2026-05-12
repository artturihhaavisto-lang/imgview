#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <gst/gst.h>
#include <gst/video/videooverlay.h>
#include <stdbool.h>
#include <string.h>

#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif

#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif

#define G_LOG_DOMAIN_COUNT 3
#define SEEK_STEP_NS (5 * GST_SECOND)
#define SEEK_BIG_STEP_NS (60 * GST_SECOND)
#define POS_POLL_MS 200

static const char *CSS =
    "window { background: #0b0b0b; }"
    "#video { background: #050505; }"
    "#controls {"
    "  background: rgba(12,12,12,0.96);"
    "  border-top: 1px solid #202020;"
    "  padding: 5px 9px;"
    "}"
    "#controls label {"
    "  font-family: monospace;"
    "  font-size: 11px;"
    "  color: #777;"
    "}"
    "#name { color: #bbb; }"
    "#time { color: #888; min-width: 118px; }"
    "button {"
    "  min-height: 22px;"
    "  padding: 1px 9px;"
    "  border-radius: 3px;"
    "  color: #ccc;"
    "  background: #191919;"
    "  border: 1px solid #303030;"
    "}"
    "button:hover { background: #232323; }"
    "scale trough { background: #202020; min-height: 5px; border-radius: 2px; }"
    "scale highlight { background: #6f9fd8; border-radius: 2px; }"
    "scale slider { background: #bbb; min-width: 10px; min-height: 10px; border-radius: 5px; }";

typedef struct {
    GtkWidget *window;
    GtkWidget *video;
    GtkWidget *controls;
    GtkWidget *play_button;
    GtkWidget *mute_button;
    GtkWidget *seek;
    GtkWidget *volume;
    GtkWidget *name;
    GtkWidget *time;

    GstElement *playbin;
    GstElement *video_sink;
    guint bus_watch_id;
    guint position_timer_id;
    guintptr window_handle;

    GPtrArray *paths;
    char *pending_scan_dir;
    char *pending_scan_path;
    guint index;

    gint64 duration;
    bool playing;
    bool seeking;
    bool muted;
    bool fullscreen;
} VidView;

static void quiet_log_handler(
    const gchar *log_domain,
    GLogLevelFlags log_level,
    const gchar *message,
    gpointer user_data
) {
    (void)log_domain;
    (void)log_level;
    (void)message;
    (void)user_data;
}

static bool is_video_ext(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) {
        return false;
    }

    static const char *exts[] = {
        ".mp4", ".m4v", ".mkv", ".webm", ".mov", ".avi", ".wmv",
        ".mpg", ".mpeg", ".ts", ".m2ts", ".ogv", ".flv", ".3gp",
        ".3g2", ".vob", NULL
    };

    for (int i = 0; exts[i]; i++) {
        if (g_ascii_strcasecmp(ext, exts[i]) == 0) {
            return true;
        }
    }
    return false;
}

static bool is_video_file(const char *path) {
    return is_video_ext(path) && g_file_test(path, G_FILE_TEST_IS_REGULAR);
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

static void add_dir_videos(GPtrArray *paths, const char *dir) {
    GDir *gdir = g_dir_open(dir, 0, NULL);
    if (!gdir) {
        return;
    }

    const char *name = NULL;
    while ((name = g_dir_read_name(gdir)) != NULL) {
        if (!is_video_ext(name)) {
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
            add_dir_videos(paths, abs);
            g_free(abs);
            *out_paths = paths;
            return;
        }

        if (is_video_file(abs)) {
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
            add_dir_videos(paths, abs);
            g_free(abs);
        } else if (is_video_file(abs)) {
            g_ptr_array_add(paths, abs);
        } else {
            g_free(abs);
        }
    }

    *out_paths = paths;
}

static char *format_time(gint64 ns) {
    if (ns < 0 || (GstClockTime)ns == GST_CLOCK_TIME_NONE) {
        return g_strdup("--:--");
    }

    gint64 total = ns / GST_SECOND;
    gint64 hours = total / 3600;
    gint64 mins = (total / 60) % 60;
    gint64 secs = total % 60;

    if (hours > 0) {
        return g_strdup_printf("%" G_GINT64_FORMAT ":%02" G_GINT64_FORMAT ":%02" G_GINT64_FORMAT, hours, mins, secs);
    }
    return g_strdup_printf("%02" G_GINT64_FORMAT ":%02" G_GINT64_FORMAT, mins, secs);
}

static void update_play_button(VidView *view) {
    gtk_button_set_label(GTK_BUTTON(view->play_button), view->playing ? "Pause" : "Play");
}

static void update_mute_button(VidView *view) {
    gtk_button_set_label(GTK_BUTTON(view->mute_button), view->muted ? "Muted" : "Mute");
}

static void update_time_label(VidView *view, gint64 position) {
    char *pos = format_time(position);
    char *dur = format_time(view->duration);
    char *text = g_strdup_printf("%s / %s", pos, dur);
    gtk_label_set_text(GTK_LABEL(view->time), text);
    g_free(text);
    g_free(dur);
    g_free(pos);
}

static void set_message(VidView *view, const char *message) {
    gtk_label_set_text(GTK_LABEL(view->name), message);
}

static void set_video_overlay_handle(VidView *view, GstVideoOverlay *overlay) {
    if (!overlay || !view->window_handle) {
        return;
    }
    gst_video_overlay_set_window_handle(overlay, view->window_handle);
}

static void realize_video(GtkWidget *widget, gpointer data) {
    VidView *view = data;
    GdkWindow *window = gtk_widget_get_window(widget);
    view->window_handle = 0;

#ifdef GDK_WINDOWING_X11
    if (GDK_IS_X11_WINDOW(window)) {
        view->window_handle = (guintptr)GDK_WINDOW_XID(window);
    }
#endif

#ifdef GDK_WINDOWING_WAYLAND
    if (!view->window_handle && GDK_IS_WAYLAND_WINDOW(window)) {
        view->window_handle = (guintptr)gdk_wayland_window_get_wl_surface(window);
    }
#endif

    if (GST_IS_VIDEO_OVERLAY(view->video_sink)) {
        set_video_overlay_handle(view, GST_VIDEO_OVERLAY(view->video_sink));
    }
}

static GstBusSyncReply bus_sync(GstBus *bus, GstMessage *message, gpointer data) {
    (void)bus;
    VidView *view = data;

    if (gst_is_video_overlay_prepare_window_handle_message(message)) {
        if (view->window_handle && GST_IS_VIDEO_OVERLAY(GST_MESSAGE_SRC(message))) {
            set_video_overlay_handle(view, GST_VIDEO_OVERLAY(GST_MESSAGE_SRC(message)));
            gst_message_unref(message);
            return GST_BUS_DROP;
        }
    }

    return GST_BUS_PASS;
}

static GstElement *make_sink_by_name(const char *name) {
    GstElement *sink = gst_element_factory_make(name, NULL);
    if (!sink) {
        return NULL;
    }

    if (g_object_class_find_property(G_OBJECT_GET_CLASS(sink), "force-aspect-ratio")) {
        g_object_set(sink, "force-aspect-ratio", TRUE, NULL);
    }
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(sink), "enable-last-sample")) {
        g_object_set(sink, "enable-last-sample", FALSE, NULL);
    }
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(sink), "sync")) {
        g_object_set(sink, "sync", TRUE, NULL);
    }

    return sink;
}

static GstElement *make_video_sink(void) {
    const char *override = g_getenv("VIDVIEW_SINK");
    if (override && *override) {
        GstElement *sink = make_sink_by_name(override);
        if (sink) {
            return sink;
        }
    }

    const char *x11_sinks[] = {"xvimagesink", "ximagesink", "glimagesink", "autovideosink", NULL};
    const char *wayland_sinks[] = {"waylandsink", "glimagesink", "autovideosink", NULL};
    const char **names = x11_sinks;

    GdkDisplay *display = gdk_display_get_default();
#ifdef GDK_WINDOWING_WAYLAND
    if (display && GDK_IS_WAYLAND_DISPLAY(display)) {
        names = wayland_sinks;
    }
#endif

    for (int i = 0; names[i]; i++) {
        GstElement *sink = make_sink_by_name(names[i]);
        if (sink) {
            return sink;
        }
    }

    return NULL;
}

static void set_playing(VidView *view, bool playing) {
    view->playing = playing;
    gst_element_set_state(view->playbin, playing ? GST_STATE_PLAYING : GST_STATE_PAUSED);
    update_play_button(view);
}

static void toggle_playing(VidView *view) {
    set_playing(view, !view->playing);
}

static void play_clicked(GtkButton *button, gpointer data) {
    (void)button;
    toggle_playing(data);
}

static void seek_to(VidView *view, gint64 position) {
    if (position < 0) {
        position = 0;
    }
    if (view->duration > 0 && position > view->duration) {
        position = view->duration;
    }

    gst_element_seek_simple(
        view->playbin,
        GST_FORMAT_TIME,
        GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
        position
    );
}

static void seek_relative(VidView *view, gint64 delta) {
    gint64 pos = 0;
    if (!gst_element_query_position(view->playbin, GST_FORMAT_TIME, &pos)) {
        pos = 0;
    }
    seek_to(view, pos + delta);
}

static gboolean position_tick(gpointer data) {
    VidView *view = data;
    gint64 pos = 0;
    gint64 dur = GST_CLOCK_TIME_NONE;

    if (gst_element_query_duration(view->playbin, GST_FORMAT_TIME, &dur) && dur > 0) {
        view->duration = dur;
    }

    if (!gst_element_query_position(view->playbin, GST_FORMAT_TIME, &pos)) {
        pos = 0;
    }

    if (!view->seeking && view->duration > 0) {
        double value = ((double)pos / (double)view->duration) * 1000.0;
        gtk_range_set_value(GTK_RANGE(view->seek), value);
    }
    update_time_label(view, pos);
    return G_SOURCE_CONTINUE;
}

static gboolean bus_message(GstBus *bus, GstMessage *message, gpointer data) {
    (void)bus;
    VidView *view = data;

    switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_ERROR: {
        GError *error = NULL;
        char *debug = NULL;
        gst_message_parse_error(message, &error, &debug);
        set_message(view, error ? error->message : "Playback error");
        g_clear_error(&error);
        g_free(debug);
        set_playing(view, false);
        break;
    }
    case GST_MESSAGE_EOS:
        set_playing(view, false);
        break;
    case GST_MESSAGE_DURATION_CHANGED:
        view->duration = GST_CLOCK_TIME_NONE;
        break;
    case GST_MESSAGE_STATE_CHANGED:
        if (GST_MESSAGE_SRC(message) == GST_OBJECT(view->playbin)) {
            GstState old_state;
            GstState new_state;
            GstState pending;
            gst_message_parse_state_changed(message, &old_state, &new_state, &pending);
            (void)old_state;
            (void)pending;
            view->playing = new_state == GST_STATE_PLAYING;
            update_play_button(view);
        }
        break;
    default:
        break;
    }

    return G_SOURCE_CONTINUE;
}

static void load_index(VidView *view, gint index) {
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
    char *uri = g_filename_to_uri(path, NULL, NULL);
    if (!uri) {
        set_message(view, "Could not build file URI");
        return;
    }

    gst_element_set_state(view->playbin, GST_STATE_NULL);
    g_object_set(view->playbin, "uri", uri, NULL);
    view->duration = GST_CLOCK_TIME_NONE;
    gtk_range_set_value(GTK_RANGE(view->seek), 0.0);

    char *base = g_path_get_basename(path);
    char *title = g_strdup_printf("vidview - %s", base);
    char *label = g_strdup_printf("%s  %u/%u", base, view->index + 1, view->paths->len);
    gtk_window_set_title(GTK_WINDOW(view->window), title);
    gtk_label_set_text(GTK_LABEL(view->name), label);

    set_playing(view, true);

    g_free(label);
    g_free(title);
    g_free(base);
    g_free(uri);
}

static void go_delta(VidView *view, gint delta) {
    if (view->paths->len > 0) {
        load_index(view, (gint)view->index + delta);
    }
}

static gboolean finish_pending_scan(gpointer data) {
    VidView *view = data;
    if (!view->pending_scan_dir || !view->pending_scan_path) {
        return G_SOURCE_REMOVE;
    }

    GPtrArray *siblings = new_path_array();
    add_dir_videos(siblings, view->pending_scan_dir);

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

        const char *path = g_ptr_array_index(view->paths, view->index);
        char *base = g_path_get_basename(path);
        char *label = g_strdup_printf("%s  %u/%u", base, view->index + 1, view->paths->len);
        gtk_label_set_text(GTK_LABEL(view->name), label);
        g_free(label);
        g_free(base);
    } else {
        g_ptr_array_unref(siblings);
    }

    g_clear_pointer(&view->pending_scan_dir, g_free);
    g_clear_pointer(&view->pending_scan_path, g_free);
    return G_SOURCE_REMOVE;
}

static gboolean seek_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    (void)widget;
    (void)event;
    VidView *view = data;
    view->seeking = true;
    return FALSE;
}

static gboolean seek_release(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    (void)widget;
    (void)event;
    VidView *view = data;
    if (view->duration > 0) {
        double value = gtk_range_get_value(GTK_RANGE(view->seek));
        seek_to(view, (gint64)((value / 1000.0) * (double)view->duration));
    }
    view->seeking = false;
    return FALSE;
}

static void seek_value_changed(GtkRange *range, gpointer data) {
    VidView *view = data;
    if (view->seeking && view->duration > 0) {
        double value = gtk_range_get_value(range);
        update_time_label(view, (gint64)((value / 1000.0) * (double)view->duration));
    }
}

static void volume_changed(GtkRange *range, gpointer data) {
    VidView *view = data;
    double value = gtk_range_get_value(range) / 100.0;
    g_object_set(view->playbin, "volume", value, NULL);
    if (value > 0.0 && view->muted) {
        view->muted = false;
        g_object_set(view->playbin, "mute", FALSE, NULL);
        update_mute_button(view);
    }
}

static void toggle_mute(VidView *view) {
    view->muted = !view->muted;
    g_object_set(view->playbin, "mute", view->muted, NULL);
    update_mute_button(view);
}

static void mute_clicked(GtkButton *button, gpointer data) {
    (void)button;
    toggle_mute(data);
}

static void toggle_fullscreen(VidView *view) {
    if (view->fullscreen) {
        gtk_window_unfullscreen(GTK_WINDOW(view->window));
    } else {
        gtk_window_fullscreen(GTK_WINDOW(view->window));
    }
    view->fullscreen = !view->fullscreen;
}

static void toggle_controls(VidView *view) {
    gtk_widget_set_visible(view->controls, !gtk_widget_get_visible(view->controls));
}

static gboolean key_press(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    (void)widget;
    VidView *view = data;

    switch (event->keyval) {
    case GDK_KEY_space:
    case GDK_KEY_k:
        toggle_playing(view);
        break;
    case GDK_KEY_Right:
    case GDK_KEY_l:
        seek_relative(view, SEEK_STEP_NS);
        break;
    case GDK_KEY_Left:
    case GDK_KEY_h:
        seek_relative(view, -SEEK_STEP_NS);
        break;
    case GDK_KEY_Page_Down:
        seek_relative(view, SEEK_BIG_STEP_NS);
        break;
    case GDK_KEY_Page_Up:
        seek_relative(view, -SEEK_BIG_STEP_NS);
        break;
    case GDK_KEY_Home:
        seek_to(view, 0);
        break;
    case GDK_KEY_End:
        if (view->duration > 0) {
            seek_to(view, view->duration - GST_SECOND);
        }
        break;
    case GDK_KEY_n:
    case GDK_KEY_Down:
        go_delta(view, 1);
        break;
    case GDK_KEY_p:
    case GDK_KEY_Up:
        go_delta(view, -1);
        break;
    case GDK_KEY_plus:
    case GDK_KEY_equal:
    case GDK_KEY_KP_Add: {
        double value = gtk_range_get_value(GTK_RANGE(view->volume));
        gtk_range_set_value(GTK_RANGE(view->volume), MIN(100.0, value + 5.0));
        break;
    }
    case GDK_KEY_minus:
    case GDK_KEY_KP_Subtract: {
        double value = gtk_range_get_value(GTK_RANGE(view->volume));
        gtk_range_set_value(GTK_RANGE(view->volume), MAX(0.0, value - 5.0));
        break;
    }
    case GDK_KEY_m:
        toggle_mute(view);
        break;
    case GDK_KEY_f:
    case GDK_KEY_F11:
        toggle_fullscreen(view);
        break;
    case GDK_KEY_i:
        toggle_controls(view);
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

static gboolean video_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    (void)widget;
    VidView *view = data;
    if (event->button == 1 && event->type == GDK_2BUTTON_PRESS) {
        toggle_fullscreen(view);
    } else if (event->button == 1) {
        toggle_playing(view);
    }
    return TRUE;
}

static gboolean video_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer data) {
    (void)widget;
    VidView *view = data;
    double value = gtk_range_get_value(GTK_RANGE(view->volume));

    if (event->direction == GDK_SCROLL_UP) {
        value += 5.0;
    } else if (event->direction == GDK_SCROLL_DOWN) {
        value -= 5.0;
    } else if (event->direction == GDK_SCROLL_SMOOTH) {
        double dx = 0.0;
        double dy = 0.0;
        gdk_event_get_scroll_deltas((GdkEvent *)event, &dx, &dy);
        value -= dy * 5.0;
    }

    gtk_range_set_value(GTK_RANGE(view->volume), CLAMP(value, 0.0, 100.0));
    return TRUE;
}

static gboolean video_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    (void)data;
    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);
    cairo_set_source_rgb(cr, 0.02, 0.02, 0.02);
    cairo_rectangle(cr, 0, 0, alloc.width, alloc.height);
    cairo_fill(cr);
    return FALSE;
}

static GtkWidget *make_button(const char *label) {
    GtkWidget *button = gtk_button_new_with_label(label);
    gtk_widget_set_can_focus(button, FALSE);
    return button;
}

static GtkWidget *make_scale(double min, double max, double value) {
    GtkWidget *scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, min, max, 1.0);
    gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
    gtk_range_set_value(GTK_RANGE(scale), value);
    gtk_widget_set_can_focus(scale, FALSE);
    return scale;
}

static void create_ui(VidView *view) {
    view->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(view->window), "vidview");
    gtk_window_set_default_size(GTK_WINDOW(view->window), 1100, 720);

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, CSS, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_object_unref(provider);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    view->video = gtk_drawing_area_new();
    gtk_widget_set_name(view->video, "video");
    gtk_widget_set_hexpand(view->video, TRUE);
    gtk_widget_set_vexpand(view->video, TRUE);
    gtk_widget_add_events(view->video, GDK_BUTTON_PRESS_MASK | GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);

    view->controls = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_name(view->controls, "controls");

    view->seek = make_scale(0.0, 1000.0, 0.0);

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 9);
    view->play_button = make_button("Play");
    view->mute_button = make_button("Mute");
    view->name = gtk_label_new("");
    view->time = gtk_label_new("00:00 / --:--");
    view->volume = make_scale(0.0, 100.0, 80.0);

    gtk_widget_set_name(view->name, "name");
    gtk_widget_set_name(view->time, "time");
    gtk_label_set_xalign(GTK_LABEL(view->name), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(view->name), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_hexpand(view->name, TRUE);
    gtk_widget_set_size_request(view->volume, 92, -1);

    gtk_box_pack_start(GTK_BOX(row), view->play_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), view->name, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(row), view->time, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), view->mute_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), view->volume, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(view->controls), view->seek, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(view->controls), row, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), view->video, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), view->controls, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(view->window), root);

    g_signal_connect(view->window, "key-press-event", G_CALLBACK(key_press), view);
    g_signal_connect(view->video, "realize", G_CALLBACK(realize_video), view);
    g_signal_connect(view->video, "draw", G_CALLBACK(video_draw), view);
    g_signal_connect(view->video, "button-press-event", G_CALLBACK(video_button_press), view);
    g_signal_connect(view->video, "scroll-event", G_CALLBACK(video_scroll), view);
    g_signal_connect(view->play_button, "clicked", G_CALLBACK(play_clicked), view);
    g_signal_connect(view->mute_button, "clicked", G_CALLBACK(mute_clicked), view);
    g_signal_connect(view->seek, "button-press-event", G_CALLBACK(seek_press), view);
    g_signal_connect(view->seek, "button-release-event", G_CALLBACK(seek_release), view);
    g_signal_connect(view->seek, "value-changed", G_CALLBACK(seek_value_changed), view);
    g_signal_connect(view->volume, "value-changed", G_CALLBACK(volume_changed), view);
}

static void cleanup(VidView *view) {
    if (view->position_timer_id) {
        g_source_remove(view->position_timer_id);
    }
    if (view->bus_watch_id) {
        g_source_remove(view->bus_watch_id);
    }
    if (view->playbin) {
        gst_element_set_state(view->playbin, GST_STATE_NULL);
        gst_object_unref(view->playbin);
    }
    if (view->video_sink) {
        gst_object_unref(view->video_sink);
    }
    if (view->paths) {
        g_ptr_array_unref(view->paths);
    }
    g_free(view->pending_scan_dir);
    g_free(view->pending_scan_path);
    g_free(view);
}

static void window_destroy(GtkWidget *widget, gpointer data) {
    (void)widget;
    cleanup(data);
    gtk_main_quit();
}

static bool init_player(VidView *view) {
    view->playbin = gst_element_factory_make("playbin", NULL);
    if (!view->playbin) {
        return false;
    }

    view->video_sink = make_video_sink();
    if (view->video_sink) {
        g_object_ref(view->video_sink);
        g_object_set(view->playbin, "video-sink", view->video_sink, NULL);
    }
    g_object_set(view->playbin, "volume", 0.8, NULL);

    GstBus *bus = gst_element_get_bus(view->playbin);
    gst_bus_set_sync_handler(bus, bus_sync, view, NULL);
    view->bus_watch_id = gst_bus_add_watch(bus, bus_message, view);
    gst_object_unref(bus);

    return true;
}

int main(int argc, char **argv) {
    const char *quiet_domains[G_LOG_DOMAIN_COUNT] = {
        "GStreamer-GL",
        "GStreamer-Wayland",
        "GStreamer"
    };
    for (int i = 0; i < G_LOG_DOMAIN_COUNT; i++) {
        g_log_set_handler(
            quiet_domains[i],
            G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING,
            quiet_log_handler,
            NULL
        );
    }

    GPtrArray *paths = NULL;
    guint start = 0;
    char *pending_scan_dir = NULL;
    char *pending_scan_path = NULL;

    resolve_args(argc, argv, &paths, &start, &pending_scan_dir, &pending_scan_path);
    if (!paths || paths->len == 0) {
        g_printerr("vidview: no videos found\n");
        g_printerr("usage: vidview <file|dir> [...]\n");
        if (paths) {
            g_ptr_array_unref(paths);
        }
        g_free(pending_scan_dir);
        g_free(pending_scan_path);
        return 1;
    }

    gtk_init(NULL, NULL);
    gst_init(NULL, NULL);

    VidView *view = g_new0(VidView, 1);
    view->paths = paths;
    view->index = start;
    view->pending_scan_dir = pending_scan_dir;
    view->pending_scan_path = pending_scan_path;
    view->duration = GST_CLOCK_TIME_NONE;

    if (!init_player(view)) {
        g_printerr("vidview: failed to initialize GStreamer playbin\n");
        cleanup(view);
        return 1;
    }

    create_ui(view);
    g_signal_connect(view->window, "destroy", G_CALLBACK(window_destroy), view);
    gtk_widget_show_all(view->window);

    load_index(view, (gint)view->index);
    view->position_timer_id = g_timeout_add(POS_POLL_MS, position_tick, view);
    if (view->pending_scan_dir) {
        g_idle_add_full(G_PRIORITY_LOW, finish_pending_scan, view, NULL);
    }

    gtk_main();
    return 0;
}
