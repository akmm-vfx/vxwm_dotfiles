#define _POSIX_C_SOURCE 200809L

#include <X11/Xatom.h>
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <glob.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define WORKSPACE_COUNT 5
#define MAX_CLIENTS 4096

typedef struct {
	Window window;
	unsigned long desktop;
	unsigned long opened_order;
	char icon_name[96];
	int visible;
	int pinned;
	int minimized;
	int active;
	int seen;
} AppWindow;

static Display *display;
static Window root;
static Atom current_desktop_atom;
static Atom client_list_atom;
static Atom desktop_atom;
static Atom class_atom;
static Atom active_window_atom;
static Atom client_state_atom;
static Atom xkb_rules_atom;
static int xkb_event_base = -1;
static char state_dir[PATH_MAX];
static AppWindow clients[MAX_CLIENTS];
static size_t client_count;
static unsigned long next_open_order;
static int initial_clients = 1;
static long current_workspace = -1;
static int current_layout = -1;
static char current_layout_name[32];
static int caps_lock_active = -1;
static unsigned int current_ram_usage = UINT_MAX;
static unsigned int current_ram_used_tenths = UINT_MAX;
static unsigned int current_cpu_usage = UINT_MAX;
static unsigned int current_gpu_usage = UINT_MAX;
static unsigned long long previous_cpu_total;
static unsigned long long previous_cpu_idle;
static int gpu_status_unavailable;
static Window notification_window;
static Window launcher_window;
static Window switcher_window;
static Window active_window;

static unsigned long get_cardinal(Window window, Atom property, int *found);
static Window read_active_window(void);
static const char *icon_for_class(const char *class_name);

static void
raise_notification(void)
{
	Window returned_root, parent, *children = NULL;
	unsigned int child_count = 0;
	XWindowAttributes attributes;
	int screenshot_active = 0;
	char launcher_marker[PATH_MAX];
	char switcher_marker[PATH_MAX];
	int launcher_open;
	int switcher_open;
	int marker_length = snprintf(launcher_marker, sizeof(launcher_marker),
		"%s/launcher-open", state_dir);
	launcher_open = marker_length > 0 && (size_t)marker_length < sizeof(launcher_marker)
		&& access(launcher_marker, F_OK) == 0;
	marker_length = snprintf(switcher_marker, sizeof(switcher_marker),
		"%s/window-switcher-open", state_dir);
	switcher_open = marker_length > 0 &&
		(size_t)marker_length < sizeof(switcher_marker) &&
		access(switcher_marker, F_OK) == 0;
	if (!launcher_open)
		launcher_window = None;
	if (!XQueryTree(display, root, &returned_root, &parent, &children,
		&child_count))
		return;
	for (unsigned int i = 0; i < child_count; i++) {
		char *name = NULL;
		if (XFetchName(display, children[i], &name) && name &&
			!strcmp(name, "VXWM Screenshot Overlay"))
			screenshot_active = 1;
		if (name)
			XFree(name);
		if (screenshot_active)
			break;
	}
	if (screenshot_active) {
		if (children)
			XFree(children);
		return;
	}
	if (notification_window &&
		XGetWindowAttributes(display, notification_window, &attributes)) {
		XRaiseWindow(display, notification_window);
	} else {
		notification_window = None;
		for (unsigned int i = 0; i < child_count; i++) {
			char *name = NULL;
			if (XFetchName(display, children[i], &name) && name &&
				!strcmp(name, "VXWM Notification Container")) {
				notification_window = children[i];
				XRaiseWindow(display, notification_window);
			}
			if (name)
				XFree(name);
		}
	}
	if (launcher_open) {
		if (launcher_window &&
			!XGetWindowAttributes(display, launcher_window, &attributes))
			launcher_window = None;
		if (launcher_window) {
			XRaiseWindow(display, launcher_window);
		} else {
			for (unsigned int i = 0; i < child_count; i++) {
				char *name = NULL;
				if (XFetchName(display, children[i], &name) && name &&
					!strcmp(name, "VXWM Application Launcher")) {
					launcher_window = children[i];
					XRaiseWindow(display, launcher_window);
				}
				if (name)
					XFree(name);
			}
		}
	}
	if (switcher_open) {
		if (switcher_window &&
			!XGetWindowAttributes(display, switcher_window, &attributes))
			switcher_window = None;
		if (switcher_window) {
			XRaiseWindow(display, switcher_window);
		} else {
			for (unsigned int i = 0; i < child_count; i++) {
				char *name = NULL;
				if (XFetchName(display, children[i], &name) && name &&
					!strcmp(name, "VXWM Window Switcher")) {
					switcher_window = children[i];
					XRaiseWindow(display, switcher_window);
				}
				if (name)
					XFree(name);
			}
		}
	} else {
		switcher_window = None;
	}
	if (children)
		XFree(children);
	XFlush(display);
}

static void
touch_file(const char *name)
{
	char path[PATH_MAX];
	int length = snprintf(path, sizeof(path), "%s/%s", state_dir, name);
	if (length > 0 && (size_t)length < sizeof(path)) {
		FILE *file = fopen(path, "a");
		if (file)
			fclose(file);
	}
}

static void
remove_file(const char *name)
{
	char path[PATH_MAX];
	int length = snprintf(path, sizeof(path), "%s/%s", state_dir, name);
	if (length > 0 && (size_t)length < sizeof(path))
		unlink(path);
}

static void
clear_prefix(const char *prefix)
{
	DIR *directory = opendir(state_dir);
	struct dirent *entry;
	size_t prefix_length = strlen(prefix);
	if (!directory)
		return;
	while ((entry = readdir(directory)))
		if (!strncmp(entry->d_name, prefix, prefix_length))
			remove_file(entry->d_name);
	closedir(directory);
}

static unsigned long
get_cardinal(Window window, Atom property, int *found)
{
	Atom actual_type;
	int actual_format;
	unsigned long item_count, bytes_after;
	unsigned char *data = NULL;
	unsigned long value = 0;

	*found = XGetWindowProperty(display, window, property, 0, 1, False,
		XA_CARDINAL, &actual_type, &actual_format, &item_count, &bytes_after,
		&data) == Success && actual_type == XA_CARDINAL && actual_format == 32
		&& item_count > 0 && data;
	if (*found)
		value = *(unsigned long *)data;
	if (data)
		XFree(data);
	return value;
}

static const char *
icon_for_class(const char *class_name)
{
	char lower[256];
	size_t i;
	for (i = 0; i + 1 < sizeof(lower) && class_name[i]; i++)
		lower[i] = (char)tolower((unsigned char)class_name[i]);
	lower[i] = '\0';

	if (strstr(lower, "zen")) return "zen-browser";
	if (strstr(lower, "firefox")) return "firefox";
	if (strstr(lower, "chrom") || strstr(lower, "brave")) return "chromium-browser";
	if (strstr(lower, "xterm")) return "utilities-terminal";
	if (strstr(lower, "kitty") || strstr(lower, "terminal") ||
	    strstr(lower, "konsole") || strstr(lower, "alacritty")) return "kitty";
	if (strstr(lower, "dolphin")) return "org.kde.dolphin";
	if (strstr(lower, "thunar")) return "org.xfce.Thunar";
	if (strstr(lower, "nautilus")) return "org.gnome.Nautilus";
	if (strstr(lower, "blender")) return "blender";
	if (strstr(lower, "chatgpt")) return "chatgpt";
	if (strstr(lower, "codium")) return "vscodium";
	if (strstr(lower, "code")) return "code";
	if (strstr(lower, "spotify")) return "spotify-client";
	if (strstr(lower, "vlc")) return "vlc";
	if (strstr(lower, "mpv")) return "mpv";
	if (strstr(lower, "discord")) return "discord";
	return "application-x-executable";
}

static int
is_shell_window(const char *class_name)
{
	return !strcasecmp(class_name, "qml runtime") ||
	       !strcasecmp(class_name, "qml6") ||
	       !strcasecmp(class_name, "vxwm-shell") ||
	       !strcasecmp(class_name, "vxwm") ||
	       !strcasecmp(class_name, "dwm");
}

static int
read_app(Window window, unsigned long *desktop, char icon_name[96],
         int *visible, int *pinned, int *minimized)
{
	XClassHint hint = {0};
	int has_desktop;
	int has_state;
	unsigned long desktop_value = get_cardinal(window, desktop_atom,
		&has_desktop);
	unsigned long state = get_cardinal(window, client_state_atom, &has_state);
	if (!has_desktop)
		return 0;
	if (!XGetClassHint(display, window, &hint)) {
		hint.res_class = NULL;
		hint.res_name = NULL;
	}
	const char *class_name = hint.res_class ? hint.res_class :
		hint.res_name ? hint.res_name : "unknown";
	*desktop = desktop_value;
	snprintf(icon_name, 96, "%s", icon_for_class(class_name));
	*visible = !is_shell_window(class_name);
	*pinned = has_state && (state & 1);
	*minimized = has_state && (state & 2);
	if (hint.res_class) XFree(hint.res_class);
	if (hint.res_name) XFree(hint.res_name);
	return 1;
}

static void
write_switcher_window(const AppWindow *app)
{
	char name[160];
	if (!app->visible)
		return;
	snprintf(name, sizeof(name), "window-%010lu-%lx-%d-%d-%d-%s",
		app->opened_order, app->window, app->pinned, app->minimized,
		app->active, app->icon_name);
	touch_file(name);
}

static void
remove_switcher_window(const AppWindow *app)
{
	char name[160];
	if (!app->visible)
		return;
	snprintf(name, sizeof(name), "window-%010lu-%lx-%d-%d-%d-%s",
		app->opened_order, app->window, app->pinned, app->minimized,
		app->active, app->icon_name);
	remove_file(name);
}

static void
write_app(const AppWindow *app)
{
	char name[128];
	if (!app->visible)
		return;
	write_switcher_window(app);
	if (app->desktop == 0xFFFFFFFFUL) {
		for (int workspace = 0; workspace < WORKSPACE_COUNT; workspace++) {
			snprintf(name, sizeof(name), "app-%d-%010lu-%lx-%s", workspace,
				app->opened_order, app->window, app->icon_name);
			touch_file(name);
		}
	} else if (app->desktop < WORKSPACE_COUNT) {
		snprintf(name, sizeof(name), "app-%lu-%010lu-%lx-%s", app->desktop,
			app->opened_order, app->window, app->icon_name);
		touch_file(name);
	}
}

static void
remove_app(const AppWindow *app)
{
	char name[128];
	if (!app->visible)
		return;
	remove_switcher_window(app);
	if (app->desktop == 0xFFFFFFFFUL) {
		for (int workspace = 0; workspace < WORKSPACE_COUNT; workspace++) {
			snprintf(name, sizeof(name), "app-%d-%010lu-%lx-%s", workspace,
				app->opened_order, app->window, app->icon_name);
			remove_file(name);
		}
	} else if (app->desktop < WORKSPACE_COUNT) {
		snprintf(name, sizeof(name), "app-%lu-%010lu-%lx-%s", app->desktop,
			app->opened_order, app->window, app->icon_name);
		remove_file(name);
	}
}

static size_t
find_client(Window window)
{
	for (size_t i = 0; i < client_count; i++)
		if (clients[i].window == window)
			return i;
	return client_count;
}

static void
update_client(Window window)
{
	size_t index = find_client(window);
	unsigned long desktop;
	char icon_name[96];
	int visible;
	int pinned, minimized;
	AppWindow updated;
	if (index == client_count || !read_app(window, &desktop, icon_name,
		&visible, &pinned, &minimized))
		return;
	updated = (AppWindow){ .window = window, .desktop = desktop,
		.opened_order = clients[index].opened_order,
		.visible = visible, .pinned = pinned, .minimized = minimized,
		.active = window == active_window, .seen = 1 };
	snprintf(updated.icon_name, sizeof(updated.icon_name), "%s", icon_name);
	if (clients[index].desktop == updated.desktop &&
	    clients[index].opened_order == updated.opened_order &&
	    !strcmp(clients[index].icon_name, updated.icon_name) &&
	    clients[index].visible == updated.visible &&
	    clients[index].pinned == updated.pinned &&
	    clients[index].minimized == updated.minimized &&
	    clients[index].active == updated.active)
		return;
	remove_app(&clients[index]);
	clients[index] = updated;
	write_app(&clients[index]);
}

static void
sync_clients(void)
{
	Atom actual_type;
	int actual_format;
	unsigned long item_count, bytes_after;
	unsigned char *data = NULL;
	Window *windows;
	int status = XGetWindowProperty(display, root, client_list_atom, 0,
		MAX_CLIENTS, False, XA_WINDOW, &actual_type, &actual_format,
		&item_count, &bytes_after, &data);
	if (status != Success || actual_type != XA_WINDOW || actual_format != 32 || !data) {
		if (data) XFree(data);
		return;
	}
	windows = (Window *)data;
	active_window = read_active_window();
	int initializing = initial_clients;
	for (size_t i = 0; i < client_count; i++)
		clients[i].seen = 0;

	for (unsigned long i = 0; i < item_count && i < MAX_CLIENTS; i++) {
		Window window = windows[i];
		XWindowAttributes attributes;
		if (!XGetWindowAttributes(display, window, &attributes))
			continue;
		size_t index = find_client(window);
		if (index == client_count && client_count < MAX_CLIENTS) {
			index = client_count++;
			clients[index] = (AppWindow){
				.window = window,
				.desktop = ULONG_MAX,
				.opened_order = initializing ? item_count - i : ++next_open_order,
				.visible = 0,
				.seen = 1
			};
			if (clients[index].opened_order > next_open_order)
				next_open_order = clients[index].opened_order;
			XSelectInput(display, window, PropertyChangeMask | StructureNotifyMask);
		}
		if (index < client_count) {
			clients[index].window = window;
			clients[index].seen = 1;
			update_client(window);
		}
	}
	initial_clients = 0;

	for (size_t i = 0; i < client_count;) {
		if (!clients[i].seen) {
			remove_app(&clients[i]);
			clients[i] = clients[--client_count];
		} else {
			i++;
		}
	}
	XFree(data);
}

static void
update_focus(void)
{
	int found;
	unsigned long active = get_cardinal(root, current_desktop_atom, &found);
	if (!found || current_workspace == (long)active)
		return;
	clear_prefix("focus-");
	for (long i = 0; active < WORKSPACE_COUNT && i <= (long)active; i++) {
		char name[32];
		snprintf(name, sizeof(name), "focus-%ld", i);
		touch_file(name);
	}
	current_workspace = (long)active;
}

static Window
read_active_window(void)
{
	Atom actual_type;
	int actual_format;
	unsigned long item_count, bytes_after;
	unsigned char *data = NULL;
	Window active = None;
	if (XGetWindowProperty(display, root, active_window_atom, 0, 1, False,
		XA_WINDOW, &actual_type, &actual_format, &item_count, &bytes_after,
		&data) == Success && actual_type == XA_WINDOW && actual_format == 32 &&
		item_count > 0 && data)
		active = *(Window *)data;
	if (data)
		XFree(data);
	return active;
}

static void
update_keyboard_layout(unsigned int group)
{
	char name[32] = "";
	Atom actual_type;
	int actual_format;
	unsigned long item_count, bytes_after;
	unsigned char *data = NULL;
	if (XGetWindowProperty(display, root, xkb_rules_atom, 0, 256, False,
		AnyPropertyType, &actual_type, &actual_format, &item_count, &bytes_after,
		&data) == Success && data && actual_format == 8) {
		char *field = (char *)data;
		unsigned long remaining = item_count;
		for (int i = 0; i < 2 && remaining > 0; i++) {
			size_t length = strnlen(field, remaining);
			if (length >= remaining)
				break;
			field += length + 1;
			remaining -= length + 1;
		}
		if (remaining > 0) {
			char *layout = field;
			for (unsigned int i = 0; i <= group; i++) {
				char *comma = memchr(layout, ',', remaining);
				size_t length = comma ? (size_t)(comma - layout) : strnlen(layout, remaining);
				if (i == group && length > 0) {
					if (length >= sizeof(name))
						length = sizeof(name) - 1;
					memcpy(name, layout, length);
					name[length] = '\0';
					for (size_t j = 0; name[j]; j++)
						name[j] = (char)tolower((unsigned char)name[j]);
					break;
				}
				if (!comma)
					break;
				remaining -= length + 1;
				layout = comma + 1;
			}
		}
	}
	if (data)
		XFree(data);
	if (!name[0])
		snprintf(name, sizeof(name), "%s", group == 0 ? "us" : "mm");
	if (current_layout == (int)group && !strcmp(current_layout_name, name))
		return;
	clear_prefix("layout-");
	char filename[40];
	snprintf(filename, sizeof(filename), "layout-%s", name);
	touch_file(filename);
	current_layout = (int)group;
	snprintf(current_layout_name, sizeof(current_layout_name), "%s", name);
}

static void
update_caps_lock(unsigned int locked_mods)
{
	int active = (locked_mods & LockMask) != 0;
	if (caps_lock_active == active)
		return;
	if (active)
		touch_file("caps-lock");
	else
		remove_file("caps-lock");
	caps_lock_active = active;
}

static void
update_usage_marker(const char *prefix, unsigned int usage, unsigned int *current)
{
	char marker[32];
	if (usage > 100)
		usage = 100;
	if (*current == usage)
		return;
	snprintf(marker, sizeof(marker), "%s%u", prefix, usage);
	clear_prefix(prefix);
	touch_file(marker);
	*current = usage;
}

static void
update_ram_usage(void)
{
	FILE *meminfo = fopen("/proc/meminfo", "r");
	char line[128], name[32];
	unsigned long long total = 0, available = 0, value;
	unsigned long long used, used_tenths;
	unsigned int usage;
	char marker[48];
	if (!meminfo)
		return;
	while (fgets(line, sizeof(line), meminfo)) {
		if (sscanf(line, "%31s %llu", name, &value) != 2)
			continue;
		if (!strcmp(name, "MemTotal:"))
			total = value;
		else if (!strcmp(name, "MemAvailable:"))
			available = value;
		if (total && available)
			break;
	}
	fclose(meminfo);
	if (!total)
		return;
	used = available <= total ? total - available : 0;
	usage = (unsigned int)((used * 100) / total);
	used_tenths = (used * 10 + 524288) / 1048576;
	if (usage == current_ram_usage && used_tenths == current_ram_used_tenths)
		return;
	snprintf(marker, sizeof(marker), "ram-%u-%llu.%llu", usage,
		used_tenths / 10, used_tenths % 10);
	clear_prefix("ram-");
	touch_file(marker);
	current_ram_usage = usage;
	current_ram_used_tenths = (unsigned int)used_tenths;
}

static void
update_cpu_usage(void)
{
	FILE *stat = fopen("/proc/stat", "r");
	unsigned long long counters[8], total = 0, idle;
	unsigned int usage = 0;
	int i;
	if (!stat)
		return;
	if (fscanf(stat, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
	    &counters[0], &counters[1], &counters[2], &counters[3],
	    &counters[4], &counters[5], &counters[6], &counters[7]) != 8) {
		fclose(stat);
		return;
	}
	fclose(stat);
	for (i = 0; i < 8; i++)
		total += counters[i];
	idle = counters[3] + counters[4];
	if (previous_cpu_total && total > previous_cpu_total) {
		unsigned long long total_delta = total - previous_cpu_total;
		unsigned long long idle_delta = idle - previous_cpu_idle;
		usage = (unsigned int)(((total_delta - idle_delta) * 100) / total_delta);
	}
	previous_cpu_total = total;
	previous_cpu_idle = idle;
	update_usage_marker("cpu-", usage, &current_cpu_usage);
}

static int
read_gpu_busy_percent(unsigned int *usage)
{
	glob_t paths;
	size_t i;
	int found = 0;
	memset(&paths, 0, sizeof(paths));
	if (glob("/sys/class/drm/card[0-9]*/device/gpu_busy_percent", 0,
	    NULL, &paths) == 0) {
		for (i = 0; i < paths.gl_pathc; i++) {
			FILE *file = fopen(paths.gl_pathv[i], "r");
			unsigned int value;
			if (file) {
				if (fscanf(file, "%u", &value) == 1 && value <= 100) {
					*usage = value;
					found = 1;
					fclose(file);
					break;
				}
				fclose(file);
			}
		}
	}
	globfree(&paths);
	return found;
}

static void
update_gpu_usage(void)
{
	unsigned int usage = 0;
	FILE *query;
	char output[32];
	int status;
	if (gpu_status_unavailable)
		return;
	if (read_gpu_busy_percent(&usage)) {
		update_usage_marker("gpu-", usage, &current_gpu_usage);
		return;
	}
	if (access("/usr/bin/nvidia-smi", X_OK) == 0)
		query = popen("/usr/bin/nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits 2>/dev/null", "r");
	else if (access("/usr/sbin/nvidia-smi", X_OK) == 0)
		query = popen("/usr/sbin/nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits 2>/dev/null", "r");
	else {
		gpu_status_unavailable = 1;
		update_usage_marker("gpu-", 0, &current_gpu_usage);
		return;
	}
	if (!query) {
		gpu_status_unavailable = 1;
		update_usage_marker("gpu-", 0, &current_gpu_usage);
		return;
	}
	if (!fgets(output, sizeof(output), query) || sscanf(output, "%u", &usage) != 1 || usage > 100) {
		gpu_status_unavailable = 1;
		usage = 0;
	}
	status = pclose(query);
	if (status != 0) {
		gpu_status_unavailable = 1;
		usage = 0;
	}
	update_usage_marker("gpu-", usage, &current_gpu_usage);
}

static int
xerror(Display *dpy, XErrorEvent *event)
{
	(void)dpy;
	(void)event;
	return 0;
}

int
main(int argc, char **argv)
{
	XEvent event;
	DIR *directory;
	time_t next_usage_update;
	time_t next_client_sync;
	struct dirent *entry;
	if (argc != 2 || strlen(argv[1]) >= sizeof(state_dir))
		return 2;
	strcpy(state_dir, argv[1]);
	if (mkdir(state_dir, 0755) < 0 && errno != EEXIST)
		return 1;
	directory = opendir(state_dir);
	if (directory) {
		while ((entry = readdir(directory)))
			if (!strncmp(entry->d_name, "app-", 4) ||
			    !strncmp(entry->d_name, "window-", 7) ||
			    !strncmp(entry->d_name, "window-switcher-", 16) ||
			    !strncmp(entry->d_name, "focus-", 6) ||
			    !strncmp(entry->d_name, "layout-", 7) ||
			    !strcmp(entry->d_name, "caps-lock") ||
			    !strncmp(entry->d_name, "ram-", 4) ||
			    !strncmp(entry->d_name, "cpu-", 4) ||
			    !strncmp(entry->d_name, "gpu-", 4) ||
			    !strcmp(entry->d_name, ".ready"))
				remove_file(entry->d_name);
		closedir(directory);
	}

	display = XOpenDisplay(NULL);
	if (!display)
		return 1;
	XSetErrorHandler(xerror);
	root = DefaultRootWindow(display);
	current_desktop_atom = XInternAtom(display, "_NET_CURRENT_DESKTOP", False);
	client_list_atom = XInternAtom(display, "_NET_CLIENT_LIST", False);
	desktop_atom = XInternAtom(display, "_NET_WM_DESKTOP", False);
	class_atom = XInternAtom(display, "WM_CLASS", False);
	active_window_atom = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);
	client_state_atom = XInternAtom(display, "_VXWM_CLIENT_STATE", False);
	xkb_rules_atom = XInternAtom(display, "_XKB_RULES_NAMES", False);
	XSelectInput(display, root, PropertyChangeMask | SubstructureNotifyMask);
	int xkb_opcode, xkb_error, xkb_major = XkbMajorVersion, xkb_minor = XkbMinorVersion;
	if (XkbQueryExtension(display, &xkb_opcode, &xkb_event_base, &xkb_error,
		&xkb_major, &xkb_minor)) {
		XkbStateRec state;
		XkbSelectEvents(display, XkbUseCoreKbd, XkbStateNotifyMask,
			XkbStateNotifyMask);
		XkbSelectEventDetails(display, XkbUseCoreKbd, XkbStateNotify,
			XkbAllStateComponentsMask, XkbAllStateComponentsMask);
		if (XkbGetState(display, XkbUseCoreKbd, &state) == Success) {
			update_keyboard_layout(state.group);
			update_caps_lock(state.locked_mods);
		}
	}
	update_focus();
	sync_clients();
	update_ram_usage();
	update_cpu_usage();
	update_gpu_usage();
	next_usage_update = time(NULL) + 2;
	next_client_sync = time(NULL) + 2;
	touch_file(".ready");
	XFlush(display);

	struct pollfd xfd = { .fd = ConnectionNumber(display), .events = POLLIN };
	while (1) {
		if (!XPending(display)) {
			int ready = poll(&xfd, 1, 2000);
			if (ready == 0) {
				if (time(NULL) >= next_client_sync) {
					sync_clients();
					next_client_sync = time(NULL) + 2;
				}
				update_ram_usage();
				update_cpu_usage();
				update_gpu_usage();
				next_usage_update = time(NULL) + 2;
				continue;
			}
			if (ready < 0) {
				if (errno == EINTR)
					continue;
				return 1;
			}
		}
		if (!XPending(display))
			continue;
		XNextEvent(display, &event);
		if (time(NULL) >= next_usage_update) {
			update_ram_usage();
			update_cpu_usage();
			update_gpu_usage();
			next_usage_update = time(NULL) + 2;
		}
		if (time(NULL) >= next_client_sync) {
			sync_clients();
			next_client_sync = time(NULL) + 2;
		}
		if (event.type == PropertyNotify) {
			XPropertyEvent *property = &event.xproperty;
			if (property->window == root) {
				if (property->atom == current_desktop_atom) {
					update_focus();
					raise_notification();
				} else if (property->atom == client_list_atom) {
					sync_clients();
					raise_notification();
				} else if (property->atom == active_window_atom) {
					active_window = read_active_window();
					for (size_t i = 0; i < client_count; i++)
						update_client(clients[i].window);
				} else if (property->atom == xkb_rules_atom) {
					XkbStateRec state;
					if (XkbGetState(display, XkbUseCoreKbd, &state) == Success)
						update_keyboard_layout(state.group);
				}
			} else if (property->atom == desktop_atom || property->atom == class_atom ||
				property->atom == client_state_atom) {
				update_client(property->window);
			}
		} else if (event.type == MapNotify && event.xmap.event == root &&
			event.xmap.window != root) {
			raise_notification();
		} else if (event.type == ConfigureNotify &&
			event.xconfigure.event == root && event.xconfigure.window != root) {
			raise_notification();
		} else if (event.type == CirculateNotify &&
			event.xcirculate.event == root && event.xcirculate.window != root) {
			raise_notification();
		} else if (event.type == DestroyNotify && event.xdestroywindow.event == root) {
			Window window = event.xdestroywindow.window;
			size_t index = find_client(window);
			if (index < client_count) {
				remove_app(&clients[index]);
				clients[index] = clients[--client_count];
			}
		} else if (xkb_event_base >= 0 && event.type == xkb_event_base) {
			XkbEvent *xkb = (XkbEvent *)&event;
			if (xkb->any.xkb_type == XkbStateNotify) {
				update_keyboard_layout(xkb->state.group);
				update_caps_lock(xkb->state.locked_mods);
			}
		}
	}
}
