/* Real GTK integration check; requires a display. */
#define main imgview_main
#include "../src/imgview.c"
#undef main

static int stage;
static void progress_pointer(ImgView *view, GdkEventType type, double fraction) {
    GtkWidget *widget = view->status.progress;
    GdkRectangle rect;
    gint start, end, x, y;
    gtk_range_get_range_rect(GTK_RANGE(widget), &rect);
    gtk_range_get_slider_range(GTK_RANGE(widget), &start, &end);
    double local_x = rect.x + (end - start) / 2.0 + fraction * (rect.width - (end - start));
    gtk_widget_translate_coordinates(widget, view->window, (int)local_x,
        rect.y + rect.height / 2, &x, &y);
    GdkEvent *event = gdk_event_new(type);
    event->any.window = g_object_ref(gtk_widget_get_window(view->window));
    GdkDevice *pointer = gdk_seat_get_pointer(gdk_display_get_default_seat(gtk_widget_get_display(widget)));
    gdk_event_set_device(event, pointer);
    if (type == GDK_MOTION_NOTIFY) {
        event->motion.x = x; event->motion.y = y;
        event->motion.state = GDK_BUTTON1_MASK;
    } else {
        event->button.x = x; event->button.y = y;
        event->button.button = 1;
        event->button.state = type == GDK_BUTTON_RELEASE ? GDK_BUTTON1_MASK : 0;
    }
    GtkEventController *controller = g_object_get_data(G_OBJECT(widget), "progress-drag");
    gtk_event_controller_handle_event(controller, event);
    gdk_event_free(event);
}

static gboolean check_progress(gpointer data) {
    ImgView *view = data;
    Canvas *canvas = &view->canvas;
    GtkRange *range = GTK_RANGE(view->status.progress);
    gint64 position;
    GdkEventButton event = {.button = 1};
    switch (stage++) {
    case 0:
        g_assert_true(gtk_window_get_titlebar(GTK_WINDOW(view->window)) == view->header);
        g_assert_cmpstr(gtk_header_bar_get_title(GTK_HEADER_BAR(view->header)), ==, "test.mp4");
        g_assert_nonnull(canvas->video_frame);
        fullscreen_hover(view, gtk_widget_get_allocated_width(view->overlay) - 1,
            gtk_widget_get_allocated_height(view->overlay) / 2);
        g_assert_true(gtk_revealer_get_reveal_child(GTK_REVEALER(view->metadata_revealer)));
        g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(view->metadata_label)), "Resolution"));
        g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(view->metadata_label)), "File size"));
        fullscreen_hover(view, 10, 100);
        g_assert_false(gtk_revealer_get_reveal_child(GTK_REVEALER(view->metadata_revealer)));
        g_assert_true(gtk_widget_get_visible(view->status.volume_box));
        g_assert_true(gtk_widget_get_visible(view->status.playback));
        g_assert_true(gtk_widget_get_visible(view->status.progress));
        g_assert_true(gtk_widget_get_sensitive(view->status.progress));
        g_assert_cmpfloat(gtk_range_get_value(range), >, 0);
        event.x = 100; event.y = 100;
        canvas_button_press(canvas->area, &event, canvas);
        GdkEventMotion motion = {.x = 102, .y = 101};
        canvas_motion(canvas->area, &motion, canvas);
        event.x = 102; event.y = 101;
        canvas_button_release(canvas->area, &event, canvas);
        g_assert_true(canvas->paused);
        canvas_button_press(canvas->area, &event, canvas);
        canvas_button_release(canvas->area, &event, canvas);
        g_assert_false(canvas->paused);
        double old_x = canvas->ox;
        canvas_button_press(canvas->area, &event, canvas);
        motion.x = 130; motion.y = 100;
        canvas_motion(canvas->area, &motion, canvas);
        g_assert_cmpfloat(canvas->ox, !=, old_x);
        /* Returning to the press point still counts as a drag. */
        motion.x = event.x; motion.y = event.y;
        canvas_motion(canvas->area, &motion, canvas);
        canvas_button_release(canvas->area, &event, canvas);
        g_assert_false(canvas->paused);
        progress_pointer(view, GDK_BUTTON_PRESS, 0.5);
        g_assert_true(view->status.scrubbing);
        progress_pointer(view, GDK_BUTTON_RELEASE, 0.5);
        g_assert_false(view->status.scrubbing);
        break;
    case 1:
        g_assert_true(gst_element_query_position(canvas->player, GST_FORMAT_TIME, &position));
        g_assert_cmpint(position, >=, 4 * GST_SECOND);
        g_assert_false(canvas->paused);
        video_toggle_pause(canvas);
        event.x = 100; event.y = 100;
        canvas_button_press(canvas->area, &event, canvas);
        GdkEventMotion paused_motion = {.x = 120, .y = 100};
        canvas_motion(canvas->area, &paused_motion, canvas);
        event.x = 120;
        canvas_button_release(canvas->area, &event, canvas);
        g_assert_true(canvas->paused);
        /* Start far from the current thumb, then drag elsewhere. */
        progress_pointer(view, GDK_BUTTON_PRESS, 0.85);
        g_assert_true(view->status.scrubbing);
        g_assert_cmpfloat(gtk_range_get_value(range), >, 6.7);
        progress_pointer(view, GDK_MOTION_NOTIFY, 0.25);
        video_update_status(view);
        g_assert_cmpfloat(gtk_range_get_value(range), >, 1.98);
        g_assert_cmpfloat(gtk_range_get_value(range), <, 2.02);
        progress_pointer(view, GDK_BUTTON_RELEASE, 0.25);
        g_assert_false(view->status.scrubbing);
        break;
    case 2:
        g_assert_true(canvas->paused);
        g_assert_true(gst_element_query_position(canvas->player, GST_FORMAT_TIME, &position));
        g_assert_cmpint(llabs(position - 2 * GST_SECOND), <, 100 * GST_MSECOND);
        g_assert_cmpfloat(gtk_range_get_value(range), >, 1.9);
        g_assert_cmpfloat(gtk_range_get_value(range), <, 2.1);
        double old_zoom = canvas->zoom;
        GdkEventScroll scroll = {.direction = GDK_SCROLL_DOWN, .state = GDK_CONTROL_MASK};
        for (int i = 0; i < 7; i++) canvas_scroll(canvas->area, &scroll, view);
        g_assert_cmpfloat(fabs(canvas->volume - 0.86), <, 0.001);
        g_assert_cmpfloat(canvas->zoom, ==, old_zoom);
        g_assert_false(canvas->muted);
        double player_volume;
        g_object_get(canvas->player, "volume", &player_volume, NULL);
        g_assert_cmpfloat(fabs(player_volume - 0.86), <, 0.001);
        g_assert_cmpfloat(fabs(gtk_level_bar_get_value(GTK_LEVEL_BAR(view->status.volume_level)) - 0.86), <, 0.001);
        GdkEventKey mute = {.keyval = GDK_KEY_m};
        key_press(NULL, &mute, view);
        g_assert_cmpfloat(gtk_level_bar_get_value(GTK_LEVEL_BAR(view->status.volume_level)), ==, 0);
        key_press(NULL, &mute, view);
        break;
    case 3:
        toggle_fullscreen(view);
        g_assert_true(canvas->fullscreen);
        g_assert_false(gtk_revealer_get_reveal_child(GTK_REVEALER(view->header_revealer)));
        g_assert_false(gtk_revealer_get_reveal_child(GTK_REVEALER(view->bottom_revealer)));
        int overlay_height = gtk_widget_get_allocated_height(view->overlay);
        fullscreen_hover(view, 100, 1);
        g_assert_true(gtk_revealer_get_reveal_child(GTK_REVEALER(view->header_revealer)));
        g_assert_false(gtk_revealer_get_reveal_child(GTK_REVEALER(view->bottom_revealer)));
        fullscreen_hover(view, 100, overlay_height - 1);
        g_assert_false(gtk_revealer_get_reveal_child(GTK_REVEALER(view->header_revealer)));
        g_assert_true(gtk_revealer_get_reveal_child(GTK_REVEALER(view->bottom_revealer)));
        view->status.scrubbing = true;
        fullscreen_hover(view, 100, overlay_height / 2);
        g_assert_true(gtk_revealer_get_reveal_child(GTK_REVEALER(view->bottom_revealer)));
        view->status.scrubbing = false;
        fullscreen_hover(view, 100, overlay_height / 2);
        g_assert_false(gtk_revealer_get_reveal_child(GTK_REVEALER(view->header_revealer)));
        g_assert_false(gtk_revealer_get_reveal_child(GTK_REVEALER(view->bottom_revealer)));
        GtkAllocation allocation;
        gtk_widget_get_allocation(canvas->area, &allocation);
        double fitted = fmin((double)allocation.width / gdk_pixbuf_get_width(canvas->pixbuf),
                             (double)allocation.height / gdk_pixbuf_get_height(canvas->pixbuf));
        g_assert_cmpfloat(fabs(canvas->zoom - fitted), <, 0.001);
        g_assert_cmpfloat(canvas->zoom, >, 1);
        canvas_zoom_step(canvas, 2, 50, 50);
        canvas_actual_size(canvas);
        g_assert_cmpfloat(fabs(canvas->zoom - fitted), <, 0.001);
        double fitted_x = canvas->ox, fitted_y = canvas->oy;
        bool was_paused = canvas->paused;
        event.x = 100; event.y = 100;
        canvas_button_press(canvas->area, &event, canvas);
        GdkEventMotion fullscreen_motion = {.x = 200, .y = 200};
        canvas_motion(canvas->area, &fullscreen_motion, canvas);
        canvas_button_release(canvas->area, &event, canvas);
        g_assert_cmpfloat(canvas->ox, ==, fitted_x);
        g_assert_cmpfloat(canvas->oy, ==, fitted_y);
        g_assert_cmpint(canvas->paused, ==, was_paused);
        toggle_fullscreen(view);
        g_assert_false(canvas->fullscreen);
        g_assert_true(gtk_widget_get_visible(view->status_host));
        g_assert_true(gtk_widget_get_parent(view->status_host) == view->content_box);
        g_assert_cmpuint(view->hover_id, >, 0);
        if (g_getenv("IMGVIEW_TEST_SCREENSHOT")) {
            GdkPixbuf *shot = gdk_pixbuf_get_from_window(gtk_widget_get_window(view->window),
                0, 0, gtk_widget_get_allocated_width(view->window), gtk_widget_get_allocated_height(view->window));
            g_assert_nonnull(shot);
            g_assert_true(gdk_pixbuf_save(shot, g_getenv("IMGVIEW_TEST_SCREENSHOT"), "png", NULL, NULL));
            g_object_unref(shot);
        }
        load_index(view, 1);
        char *image_name = g_path_get_basename(g_ptr_array_index(view->paths, 1));
        g_assert_cmpstr(gtk_header_bar_get_title(GTK_HEADER_BAR(view->header)), ==, image_name);
        g_free(image_name);
        g_assert_nonnull(canvas->pixbuf);
        g_assert_false(gtk_widget_get_visible(view->status.volume_box));
        g_assert_false(gtk_widget_get_visible(view->status.playback));
        double image_volume = canvas->volume;
        GdkEventScroll image_scroll = {.direction = GDK_SCROLL_UP, .state = GDK_CONTROL_MASK};
        canvas_scroll(canvas->area, &image_scroll, view);
        g_assert_cmpfloat(canvas->volume, ==, image_volume);
        g_assert_cmpfloat(fabs(canvas->volume - 0.86), <, 0.001);
        g_assert_false(gtk_widget_get_visible(view->status.progress));
        g_assert_cmpfloat(gtk_range_get_value(range), ==, 0);
        GdkEventKey fullscreen = {.keyval = GDK_KEY_f};
        key_press(NULL, &fullscreen, view);
        g_assert_true(view->fullscreen);
        g_assert_false(gtk_widget_get_visible(view->header));
        key_press(NULL, &fullscreen, view);
        g_assert_false(view->fullscreen);
        g_assert_true(gtk_widget_get_visible(view->header));
        fullscreen.keyval = GDK_KEY_F;
        key_press(NULL, &fullscreen, view);
        g_assert_true(view->fullscreen);
        fullscreen.keyval = GDK_KEY_Escape;
        key_press(NULL, &fullscreen, view);
        g_assert_false(view->fullscreen);
        g_assert_true(gtk_widget_get_visible(view->header));
        gtk_widget_destroy(view->window);
        g_print("PASS: click pause/resume, drag without toggling, progress tracking, seeking, pause preservation, image reset\n");
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

int main(int argc, char **argv) {
    g_assert_cmpint(argc, ==, 3);
    gst_init(NULL, NULL);
    gtk_init(NULL, NULL);
    ImgView *view = g_new0(ImgView, 1);
    view->paths = new_path_array();
    view->canvas.muted = true;
    g_ptr_array_add(view->paths, g_canonicalize_filename(argv[1], NULL));
    g_ptr_array_add(view->paths, g_canonicalize_filename(argv[2], NULL));
    create_window(view);
    g_timeout_add(1000, check_progress, view);
    gtk_main();
    return 0;
}
