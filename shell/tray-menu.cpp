#include <gtk/gtk.h>

#include <cstdlib>

struct MenuContext {
    GDBusConnection *bus = nullptr;
    gchar *service = nullptr;
    gchar *path = nullptr;
    GList *windows = nullptr;
};

struct MenuItem {
    MenuContext *context = nullptr;
    gint id = 0;
    GtkWidget *submenu = nullptr;
};

struct MenuLevel {
    MenuContext *context = nullptr;
    GtkWidget *window = nullptr;
    GtkWidget *box = nullptr;
    GtkWidget *parent = nullptr;
};

static void append_children(MenuLevel *level, GVariant *children);

static void activate_item(GtkButton *button, gpointer data)
{
    auto *item = static_cast<MenuItem *>(data);
    if (item->submenu) {
        GtkWidget *parent = gtk_widget_get_toplevel(GTK_WIDGET(button));
        gint parentX = 0;
        gint parentY = 0;
        gint parentWidth = 0;
        gint parentHeight = 0;
        GtkAllocation allocation{};
        gtk_window_get_position(GTK_WINDOW(parent), &parentX, &parentY);
        gtk_widget_get_allocation(GTK_WIDGET(button), &allocation);
        gtk_window_get_size(GTK_WINDOW(parent), &parentWidth, &parentHeight);
        (void)parentHeight;
        gtk_window_move(GTK_WINDOW(item->submenu), parentX + parentWidth - 4,
                        parentY + allocation.y);
        gtk_widget_show_all(item->submenu);
        return;
    }

    g_dbus_connection_call(item->context->bus, item->context->service,
                           item->context->path, "com.canonical.dbusmenu", "Event",
                           g_variant_new("(isvu)", item->id, "clicked",
                                         g_variant_new_int32(0),
                                         gtk_get_current_event_time()),
                           nullptr, G_DBUS_CALL_FLAGS_NONE, 1000, nullptr,
                           nullptr, nullptr);
    gtk_main_quit();
}

static void free_item(gpointer data, GClosure *)
{
    g_free(data);
}

static GtkWidget *make_submenu(MenuContext *context, GVariant *children,
                               GtkWidget *parent)
{
    auto *level = g_new0(MenuLevel, 1);
    level->context = context;
    level->parent = parent;
    level->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_wmclass(GTK_WINDOW(level->window), "tray-menu", "Tray-menu");
    gtk_window_set_title(GTK_WINDOW(level->window), "Tray-menu");
    gtk_window_set_type_hint(GTK_WINDOW(level->window), GDK_WINDOW_TYPE_HINT_POPUP_MENU);
    gtk_window_set_decorated(GTK_WINDOW(level->window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(level->window), FALSE);
    gtk_window_set_accept_focus(GTK_WINDOW(level->window), FALSE);
    gtk_window_set_focus_on_map(GTK_WINDOW(level->window), FALSE);
    gtk_window_set_keep_above(GTK_WINDOW(level->window), TRUE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(level->window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(level->window), TRUE);
    if (parent)
        gtk_window_set_transient_for(GTK_WINDOW(level->window), GTK_WINDOW(parent));

    level->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(level->window), level->box);
    g_object_set_data_full(G_OBJECT(level->window), "vxwm-menu-level", level, g_free);
    context->windows = g_list_prepend(context->windows, level->window);
    append_children(level, children);
    return level->window;
}

static GtkWidget *make_item(MenuLevel *level, GVariant *node)
{
    gint id = 0;
    GVariant *properties = nullptr;
    GVariant *children = nullptr;
    g_variant_get(node, "(i@a{sv}@av)", &id, &properties, &children);

    const gchar *label = "";
    const gchar *type = "";
    gboolean visible = TRUE;
    gboolean enabled = TRUE;
    g_variant_lookup(properties, "label", "&s", &label);
    g_variant_lookup(properties, "type", "&s", &type);
    g_variant_lookup(properties, "visible", "b", &visible);
    g_variant_lookup(properties, "enabled", "b", &enabled);

    GtkWidget *widget = nullptr;
    if (g_str_equal(type, "separator")) {
        widget = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_widget_set_margin_start(widget, 8);
        gtk_widget_set_margin_end(widget, 8);
    } else {
        const gboolean hasChildren = g_variant_n_children(children) > 0;
        gchar *text = hasChildren ? g_strdup_printf("%s  ›", label) : g_strdup(label);
        widget = gtk_button_new_with_label(text);
        g_free(text);
        gtk_button_set_relief(GTK_BUTTON(widget), GTK_RELIEF_NONE);
        gtk_widget_set_halign(widget, GTK_ALIGN_FILL);
        gtk_widget_set_hexpand(widget, TRUE);
        gtk_widget_set_can_focus(widget, FALSE);
        gtk_widget_set_sensitive(widget, enabled);

        auto *item = g_new0(MenuItem, 1);
        item->context = level->context;
        item->id = id;
        if (hasChildren)
            item->submenu = make_submenu(level->context, children, level->window);
        g_signal_connect_data(widget, "clicked", G_CALLBACK(activate_item), item,
                              free_item, G_CONNECT_DEFAULT);
    }

    gtk_widget_set_no_show_all(widget, !visible);
    gtk_box_pack_start(GTK_BOX(level->box), widget, FALSE, FALSE, 0);
    g_variant_unref(properties);
    g_variant_unref(children);
    return widget;
}

static void append_children(MenuLevel *level, GVariant *children)
{
    for (gsize i = 0; i < g_variant_n_children(children); ++i) {
        GVariant *wrapped = g_variant_get_child_value(children, i);
        GVariant *node = g_variant_get_variant(wrapped);
        make_item(level, node);
        g_variant_unref(node);
        g_variant_unref(wrapped);
    }
}

static gboolean close_if_pointer_outside(gpointer data)
{
    auto *context = static_cast<MenuContext *>(data);
    GdkDisplay *display = gdk_display_get_default();
    GdkSeat *seat = gdk_display_get_default_seat(display);
    GdkDevice *pointer = gdk_seat_get_pointer(seat);
    gint pointerX = 0;
    gint pointerY = 0;
    gdk_device_get_position(pointer, nullptr, &pointerX, &pointerY);

    for (GList *entry = context->windows; entry; entry = entry->next) {
        GtkWidget *window = GTK_WIDGET(entry->data);
        if (!gtk_widget_get_visible(window))
            continue;
        gint x = 0;
        gint y = 0;
        gint width = 0;
        gint height = 0;
        gtk_window_get_position(GTK_WINDOW(window), &x, &y);
        gtk_window_get_size(GTK_WINDOW(window), &width, &height);
        if (pointerX >= x && pointerX < x + width &&
            pointerY >= y && pointerY < y + height)
            return G_SOURCE_CONTINUE;
    }
    gtk_main_quit();
    return G_SOURCE_REMOVE;
}

int main(int argc, char **argv)
{
    if (argc != 5)
        return 2;
    gtk_init(&argc, &argv);

    GError *error = nullptr;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (!bus) {
        g_clear_error(&error);
        return 1;
    }

    const gchar *properties[] = {"children-display", "enabled", "label", "type", "visible", nullptr};
    GVariant *reply = g_dbus_connection_call_sync(
        bus, argv[1], argv[2], "com.canonical.dbusmenu", "GetLayout",
        g_variant_new("(ii^as)", 0, -1, properties),
        G_VARIANT_TYPE("(u(ia{sv}av))"), G_DBUS_CALL_FLAGS_NONE, 1500,
        nullptr, &error);
    if (!reply) {
        g_clear_error(&error);
        g_object_unref(bus);
        return 1;
    }

    guint revision = 0;
    GVariant *layout = nullptr;
    g_variant_get(reply, "(u@(ia{sv}av))", &revision, &layout);
    GVariant *children = g_variant_get_child_value(layout, 2);
    MenuContext context;
    context.bus = G_DBUS_CONNECTION(g_object_ref(bus));
    context.service = g_strdup(argv[1]);
    context.path = g_strdup(argv[2]);
    GtkWidget *root = make_submenu(&context, children, nullptr);

    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        "window { background-color: @theme_bg_color; color: @theme_fg_color; "
        "border: 1px solid alpha(@theme_fg_color, 0.18); }"
        "button { min-height: 28px; padding: 4px 12px; text-align: left; "
        "background: transparent; border: 0; border-radius: 0; box-shadow: none; }"
        "button:hover { background-color: @theme_selected_bg_color; }",
        -1, nullptr);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    gtk_widget_show_all(root);
    gtk_window_resize(GTK_WINDOW(root), 220, -1);
    const gint x = std::atoi(argv[3]);
    const gint y = std::atoi(argv[4]);
    GdkDisplay *display = gdk_display_get_default();
    GdkMonitor *monitor = gdk_display_get_monitor_at_point(display, x, y);
    GdkRectangle bounds{};
    gdk_monitor_get_geometry(monitor, &bounds);
    gint width = 0;
    gint height = 0;
    gtk_window_get_size(GTK_WINDOW(root), &width, &height);
    const gint menuX = MIN(x, bounds.x + bounds.width - width);
    const gint menuY = MIN(y, bounds.y + bounds.height - height);
    gtk_window_move(GTK_WINDOW(root), MAX(menuX, bounds.x), MAX(menuY, bounds.y));

    g_timeout_add(350, [](gpointer context) {
        g_timeout_add(100, close_if_pointer_outside, context);
        return G_SOURCE_REMOVE;
    }, &context);
    gtk_main();

    g_list_free(context.windows);
    g_free(context.service);
    g_free(context.path);
    g_object_unref(context.bus);
    g_object_unref(bus);
    g_variant_unref(children);
    g_variant_unref(layout);
    g_variant_unref(reply);
    return 0;
}
