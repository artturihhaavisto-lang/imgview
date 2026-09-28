#define main imgview_app_main
#include "../src/imgview.c"
#undef main

static int stage, width, height, retries;
static gboolean check(gpointer data) {
    ImgView *view = data;
    int w = gtk_widget_get_allocated_width(view->overlay);
    int h = gtk_widget_get_allocated_height(view->overlay);
    if (view->playlist_paths) playlist_thumbnails(view);
    switch (stage++) {
    case 0:
        /* Previews must be prepared before the first hover. */
        g_assert_false(gtk_revealer_get_reveal_child(GTK_REVEALER(view->playlist_revealer)));
        g_assert_nonnull(view->playlist_paths);
        GtkListBoxRow *initial = gtk_list_box_get_selected_row(GTK_LIST_BOX(view->playlist_list));
        g_assert_nonnull(initial);
        GtkWidget *initial_preview = g_object_get_data(G_OBJECT(initial), "preview");
        if (gtk_image_get_storage_type(GTK_IMAGE(initial_preview)) != GTK_IMAGE_PIXBUF && retries++ < 5) {
            stage = 0; return G_SOURCE_CONTINUE;
        }
        g_assert_cmpint(gtk_image_get_storage_type(GTK_IMAGE(initial_preview)), ==, GTK_IMAGE_PIXBUF);
        g_source_remove(view->hover_id); view->hover_id = 0;
        width = gtk_widget_get_allocated_width(view->canvas.area);
        height = gtk_widget_get_allocated_height(view->canvas.area);
        fullscreen_hover(view, 1, h / 2);
        g_assert_nonnull(view->playlist_paths);
        g_assert_cmpuint(view->playlist_paths->len, >, 2);
        break;
    case 1: {
        g_assert_true(gtk_revealer_get_child_revealed(GTK_REVEALER(view->playlist_revealer)));
        g_assert_cmpint(gtk_widget_get_allocated_width(view->canvas.area), ==, width);
        g_assert_cmpint(gtk_widget_get_allocated_height(view->canvas.area), ==, height);
        GtkListBoxRow *selected = gtk_list_box_get_selected_row(GTK_LIST_BOX(view->playlist_list));
        g_assert_nonnull(selected);
        GtkWidget *preview = g_object_get_data(G_OBJECT(selected), "preview");
        if (gtk_image_get_storage_type(GTK_IMAGE(preview)) != GTK_IMAGE_PIXBUF && retries++ < 5) {
            stage = 1; return G_SOURCE_CONTINUE;
        }
        g_assert_cmpint(gtk_image_get_storage_type(GTK_IMAGE(preview)), ==, GTK_IMAGE_PIXBUF);
        for (guint i = 0; i < view->playlist_paths->len; i++) {
            if (g_str_has_suffix(g_ptr_array_index(view->playlist_paths, i), "test.mp4")) {
                GtkListBoxRow *row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(view->playlist_list), i);
                g_signal_emit_by_name(view->playlist_list, "row-activated", row);
                g_assert_cmpstr(path_basename_ptr(g_ptr_array_index(view->paths, view->index)), ==, "test.mp4");
                break;
            }
        }
        fullscreen_hover(view, 100, h / 2);
        g_assert_true(gtk_revealer_get_reveal_child(GTK_REVEALER(view->playlist_revealer)));
        break;
    }
    case 2: {
        g_assert_nonnull(view->canvas.video_frame);
        GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(view->playlist_list));
        GtkWidget *preview = g_object_get_data(G_OBJECT(row), "preview");
        /* Explicitly enqueue the newly selected video's preview. */
        GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(view->playlist_scroll));
        GtkAllocation allocation; gtk_widget_get_allocation(GTK_WIDGET(row), &allocation);
        gtk_adjustment_set_value(adj, allocation.y);
        playlist_thumbnails(view);
        g_object_set_data(G_OBJECT(view->window), "test-preview", preview);
        break;
    }
    case 3: {
        GtkWidget *preview = g_object_get_data(G_OBJECT(view->window), "test-preview");
        g_assert_cmpint(gtk_image_get_storage_type(GTK_IMAGE(preview)), ==, GTK_IMAGE_PIXBUF);
        fullscreen_hover(view, w / 2, h / 2);
        g_assert_false(gtk_revealer_get_reveal_child(GTK_REVEALER(view->playlist_revealer)));
        toggle_fullscreen(view);
        break;
    }
    case 4:
        fullscreen_hover(view, 1, h / 2);
        g_assert_true(gtk_revealer_get_reveal_child(GTK_REVEALER(view->playlist_revealer)));
        for (guint i = 0; i < view->playlist_paths->len; i++) {
            if (g_str_has_suffix(g_ptr_array_index(view->playlist_paths, i), "test.wav")) {
                g_signal_emit_by_name(view->playlist_list, "row-activated",
                    gtk_list_box_get_row_at_index(GTK_LIST_BOX(view->playlist_list), i));
                g_assert_true(view->canvas.audio_only);
                break;
            }
        }
        gtk_widget_destroy(view->window);
        g_print("PASS: folder playlist, image/video previews, selection, hover retention, fullscreen, unchanged canvas\n");
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}
int main(int argc, char **argv) {
    gtk_init(&argc, &argv); gst_init(NULL, NULL);
    ImgView *view = g_new0(ImgView, 1);
    view->paths = new_path_array();
    g_ptr_array_add(view->paths, g_canonicalize_filename("build/test.png", NULL));
    create_window(view);
    g_timeout_add(1000, check, view);
    gtk_main();
}
