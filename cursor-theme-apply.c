#include <X11/Xcursor/Xcursor.h>
#include <X11/Xlib.h>
#include <stdio.h>

int
main(void)
{
	Display *display = XOpenDisplay(NULL);
	Cursor cursor;

	if (!display) {
		fprintf(stderr, "Could not open the X11 display.\n");
		return 1;
	}
	cursor = XcursorLibraryLoadCursor(display, "left_ptr");
	if (!cursor) {
		fprintf(stderr, "Could not load the Bibata left_ptr cursor.\n");
		XCloseDisplay(display);
		return 1;
	}
	XDefineCursor(display, DefaultRootWindow(display), cursor);
	XFlush(display);
	XCloseDisplay(display);
	return 0;
}
