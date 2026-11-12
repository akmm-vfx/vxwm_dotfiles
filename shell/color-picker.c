#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <string.h>

static unsigned long previous_swatch_pixel;
static int has_previous_swatch;

static unsigned long
color_pixel(Display *display, Colormap colormap, const char *hex)
{
	XColor color;
	if (!XParseColor(display, colormap, hex, &color) ||
		!XAllocColor(display, colormap, &color))
		return BlackPixel(display, DefaultScreen(display));
	return color.pixel;
}

static int
sample_color(Display *display, Window root, int screen, int root_x, int root_y,
	Window *preview, GC gc, XFontStruct *font, Colormap colormap,
	unsigned long background_pixel, unsigned long text_pixel, char value[8])
{
	Window root_return, child, subwindow;
	int window_x, window_y, sample_x = root_x, sample_y = root_y;
	unsigned int mask;
	XWindowAttributes attributes;
	XImage *image;
	XColor color, swatch;
	int width = DisplayWidth(display, screen);
	int height = DisplayHeight(display, screen);
	int preview_width = 134, preview_height = 42;
	int preview_x = root_x + 18, preview_y = root_y + 18;

	XQueryPointer(display, root, &root_return, &child, &window_x, &window_y,
		&sample_x, &sample_y, &mask);
	Window target = child ? child : root;
	if (target != root && !XTranslateCoordinates(display, root, target,
		root_x, root_y, &sample_x, &sample_y, &subwindow))
		target = root, sample_x = root_x, sample_y = root_y;
	if (!XGetWindowAttributes(display, target, &attributes) ||
		attributes.class == InputOnly) {
		target = root;
		sample_x = root_x;
		sample_y = root_y;
		XGetWindowAttributes(display, root, &attributes);
	}
	image = XGetImage(display, target, sample_x, sample_y, 1, 1,
		AllPlanes, ZPixmap);
	if (!image)
		return 0;
	color.pixel = XGetPixel(image, 0, 0);
	XQueryColor(display, attributes.colormap ? attributes.colormap : colormap,
		&color);
	snprintf(value, 8, "#%02X%02X%02X", color.red >> 8, color.green >> 8,
		color.blue >> 8);
	XDestroyImage(image);
	if (!XParseColor(display, colormap, value, &swatch) ||
		!XAllocColor(display, colormap, &swatch))
		return 0;
	if (has_previous_swatch)
		XFreeColors(display, colormap, &previous_swatch_pixel, 1, 0);
	previous_swatch_pixel = swatch.pixel;
	has_previous_swatch = 1;

	if (preview_x + preview_width > width)
		preview_x = root_x - preview_width - 18;
	if (preview_y + preview_height > height)
		preview_y = root_y - preview_height - 18;
	if (preview_x < 0) preview_x = 0;
	if (preview_y < 0) preview_y = 0;
	XMoveWindow(display, *preview, preview_x, preview_y);
	XSetWindowBackground(display, *preview, background_pixel);
	XClearWindow(display, *preview);
	XSetForeground(display, gc, swatch.pixel);
	XFillRectangle(display, *preview, gc, 9, 9, 24, 24);
	XSetForeground(display, gc, text_pixel);
	XDrawRectangle(display, *preview, gc, 8, 8, 25, 25);
	XSetFont(display, gc, font->fid);
	XDrawString(display, *preview, gc, 41, 25, value, (int)strlen(value));
	XFlush(display);
	return 1;
}

int
main(void)
{
	Display *display = XOpenDisplay(NULL);
	Window root;
	Cursor cursor;
	Window preview = None;
	GC gc = NULL;
	XFontStruct *font = NULL;
	XEvent event;
	int screen;
	int pointer_grabbed;
	int escape_grabbed = 0;
	KeyCode escape_keycode;
	unsigned int numlock_mask = 0;
	unsigned int escape_modifiers[4] = { 0, LockMask, 0, LockMask };
	int result = 1;
	char value[8] = "#000000";
	unsigned long background_pixel, text_pixel;

	if (!display)
		return 1;
	screen = DefaultScreen(display);
	root = RootWindow(display, screen);
	Colormap colormap = DefaultColormap(display, screen);
	cursor = XCreateFontCursor(display, XC_crosshair);
	escape_keycode = XKeysymToKeycode(display, XK_Escape);
	XModifierKeymap *modifier_map = XGetModifierMapping(display);
	if (modifier_map) {
		KeyCode numlock_keycode = XKeysymToKeycode(display, XK_Num_Lock);
		for (int modifier = 0; modifier < 8; modifier++)
			for (int slot = 0; slot < modifier_map->max_keypermod; slot++)
				if (modifier_map->modifiermap[
					modifier * modifier_map->max_keypermod + slot] == numlock_keycode)
					numlock_mask = 1U << modifier;
		XFreeModifiermap(modifier_map);
	}
	escape_modifiers[2] = numlock_mask;
	escape_modifiers[3] = LockMask | numlock_mask;
	pointer_grabbed = XGrabPointer(display, root, False,
		ButtonPressMask | PointerMotionMask,
		GrabModeAsync, GrabModeAsync, None, cursor, CurrentTime) == GrabSuccess;
	if (!pointer_grabbed)
		goto cleanup;
	if (escape_keycode) {
		for (int i = 0; i < 4; i++) {
			int duplicate = 0;
			for (int j = 0; j < i; j++)
				if (escape_modifiers[i] == escape_modifiers[j])
					duplicate = 1;
			if (!duplicate)
				XGrabKey(display, escape_keycode, escape_modifiers[i], root,
					False, GrabModeAsync, GrabModeAsync);
		}
		escape_grabbed = 1;
	}
	background_pixel = color_pixel(display, colormap, "#1B1820");
	text_pixel = color_pixel(display, colormap, "#D8BAFB");
	XSetWindowAttributes attributes = {
		.override_redirect = True,
		.background_pixel = color_pixel(display, colormap, "#1B1820")
	};
	preview = XCreateWindow(display, root, 0, 0, 134, 42, 0,
		CopyFromParent, InputOutput, CopyFromParent,
		CWOverrideRedirect | CWBackPixel, &attributes);
	gc = XCreateGC(display, preview, 0, NULL);
	font = XLoadQueryFont(display, "fixed");
	if (!font)
		font = XQueryFont(display, XGContextFromGC(DefaultGC(display, screen)));
	if (!font)
		goto cleanup;
	XMapRaised(display, preview);

	int pointer_x, pointer_y, root_x, root_y;
	Window root_return, child_return;
	unsigned int mask_return;
	XQueryPointer(display, root, &root_return, &child_return, &root_x, &root_y,
		&pointer_x, &pointer_y, &mask_return);
	if (!sample_color(display, root, screen, root_x, root_y, &preview, gc, font,
		colormap, background_pixel, text_pixel, value))
		goto cleanup;
	XFlush(display);
	for (;;) {
		XNextEvent(display, &event);
		if (event.type == KeyPress && event.xkey.keycode == escape_keycode)
			break;
		if (event.type == MotionNotify) {
			XEvent next;
			while (XCheckTypedEvent(display, MotionNotify, &next))
				event = next;
			root_x = event.xmotion.x_root;
			root_y = event.xmotion.y_root;
			if (!sample_color(display, root, screen, root_x, root_y, &preview,
				gc, font, colormap, background_pixel, text_pixel, value))
				break;
			continue;
		}
		if (event.type != ButtonPress)
			continue;
		if (event.xbutton.button != Button1)
			break;
		root_x = event.xbutton.x_root;
		root_y = event.xbutton.y_root;
		if (!sample_color(display, root, screen, root_x, root_y, &preview,
			gc, font, colormap, background_pixel, text_pixel, value))
			break;
		printf("%s\n", value);
		result = 0;
		break;
	}

cleanup:
	if (has_previous_swatch)
		XFreeColors(display, colormap, &previous_swatch_pixel, 1, 0);
	if (escape_grabbed) {
		for (int i = 0; i < 4; i++) {
			int duplicate = 0;
			for (int j = 0; j < i; j++)
				if (escape_modifiers[i] == escape_modifiers[j])
					duplicate = 1;
			if (!duplicate)
				XUngrabKey(display, escape_keycode, escape_modifiers[i], root);
		}
	}
	if (gc)
		XFreeGC(display, gc);
	if (font)
		XFreeFont(display, font);
	if (pointer_grabbed)
		XUngrabPointer(display, CurrentTime);
	if (preview)
		XDestroyWindow(display, preview);
	XFreeCursor(display, cursor);
	XCloseDisplay(display);
	return result;
}
