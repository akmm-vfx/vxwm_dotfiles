#pragma once

/* See LICENSE file for copyright and license details. */

/* appearance */
static const unsigned int borderpx  = 0;        /* border pixel of windows */
static const unsigned int snap      = 0;       /* snap pixel */
static const int showbar            = 0;        /* 0 means no bar */
static const int topbar             = 1;        /* 0 means bottom bar */
static const char *fonts[]          = { "monospace:size=10" };
static const char dmenufont[]       = "monospace:size=10";
#define COORDINATES_STYLE "[x%d y%d]" /* The style of coordinates displayed in bar, do not remove %d. */

static MAYBE_CONST char normbgcolor[]           = "#222222";
static MAYBE_CONST char normbordercolor[]       = "#444444";
static MAYBE_CONST char normfgcolor[]           = "#bbbbbb";
static MAYBE_CONST char selfgcolor[]            = "#eeeeee";
static MAYBE_CONST char selbordercolor[]        = "#222222";
static MAYBE_CONST char selbgcolor[]            = "#005577";
static MAYBE_CONST char *colors[][3] = {
       /*               fg           bg           border   */
       [SchemeNorm] = { normfgcolor, normbgcolor, normbordercolor },
       [SchemeSel]  = { selfgcolor,  selbgcolor,  selbordercolor  },
};

#define CENTER_NEW_FLOATING_WINDOWS 1 // so, basically, it does what it says. (make 0 to turn off)
#define NEW_FLOATING_WINDOWS_APPEAR_UNDER_CURSOR 0 // so, basically, it does what it says. (make 0 to turn off) 

#if GAPS
static const unsigned int gappx = 5;
#endif

#if BAR_HEIGHT
static const int user_bh = 0;
#endif

#if BAR_PADDING
static const int top_vertpad = 0;          /* top vertical padding of bar */ 
static const int bottom_vertpad = 0;       /* bottom vertical padding of bar */
static const int left_sidepad = 0;         /* left horizontal padding of bar */
static const int right_sidepad = 0;        /* right horizontal padding of bar */
#endif

#define BAR_ALWAYS_ON_TOP 1 /* Makes internal bar on top of other windows. */

#if EXTERNAL_BARS
#define EXTERNAL_BARS_ALWAYS_ON_TOP 1 /* Makes external bars on top of other windows. */
#endif

#if INFINITE_TAGS
#define PINNED_WINDOWS_ALWAYS_ON_TOP 1 /* Makes pinned windows on top of other windows */
#endif

/* tagging */
static const char *tags[] = { "1", "2", "3", "4", "5" };

#if OCCUPIED_TAGS_DECORATION
static const char *occupiedtags[] = { "1+", "2+", "3+", "4+", "5+" };
#endif

#if INFINITE_TAGS
#define MOVE_CANVAS_STEP 120 /* Defines how many pixel will be jumped when using movecanvas function */
#endif

#if INFINITE_TAGS && IT_SHOW_COORDINATES_IN_BAR
#define COORDINATES_DIVISOR 10 /* Defines by what number coordinates on the bar will be divided, can be used for making numbers smaller which makes navigation easier */
#endif

#if MOVE_RESIZE_WITH_KEYBOARD
#define MOVE_WITH_KEYBOARD_STEP 50 /* Defines by how many pixels windows will be resized with keyboard */
#define RESIZE_WITH_KEYBOARD_STEP 50 /* Defines by how many pixels windows will be resized with keyboard */
#endif

#if AUTOSTART
/* vxwm will execute this on startup (can be skipped with -ignoreautostart vxwm flag). */

static const char *const autostart[] = {
	"setxkbmap -layout us,mm -option grp:win_space_toggle",
	"picom --config \"$HOME/vxwm/picom.conf\"",
	"pgrep -x sxhkd >/dev/null || sxhkd -c \"$HOME/vxwm/sxhkdrc\"",
	"\"$HOME/vxwm/shell/wallpaper.sh\" --restore",
	"\"$HOME/vxwm/shell/start-ui.sh\"",
	NULL /* must end with NULL */
};
#endif

static const Rule rules[] = {
	/* xprop(1):
	 *	WM_CLASS(STRING) = instance, class
	 *	WM_NAME(STRING) = title
	 */
	/* class      instance    title       tags mask     isfloating   monitor */
	{ "Gimp",     NULL,       NULL,       0,            1,           -1 },
};

/* layout(s) */
static const float mfact     = 0.55; /* factor of master area size [0.05..0.95] */
static const int nmaster     = 1;    /* number of clients in master area */
static const int resizehints = 1;    /* 1 means respect size hints in tiled resizals */
static const int lockfullscreen = 1; /* 1 will force focus on the fullscreen window */
#if LOCK_MOVE_RESIZE_REFRESH_RATE
static const int refreshrate = 360;  /* refresh rate (per second) for client move/resize, set it to your monitor refresh rate or double of that*/
#endif //LOCK_MOVE_RESIZE_REFRESH_RATE
static const Layout layouts[] = {
	/* Canvas is free-positioned; infinite tags supplies the movable workspace view. */
	{ "Canvas", NULL },
};

/* key definitions */
#define MODKEY Mod4Mask
#define ALTERNATE_MODKEY Mod1Mask

#define SCROLL_UP Button4
#define SCROLL_DOWN Button5

#define TAGKEYS(KEY,TAG) \
	{ MODKEY,           KEY, view, {.ui = 1 << TAG} }, \
	{ MODKEY|ShiftMask, KEY, tag,  {.ui = 1 << TAG} },

/* helper for spawning shell commands in the pre dwm-5.0 fashion */
#define SHCMD(cmd) { .v = (const char*[]){ "/bin/sh", "-c", cmd, NULL } }

/* commands */
static char dmenumon[2] = "0"; /* component of dmenucmd, manipulated in spawn() */
static const char *dmenucmd[] = { "dmenu_run", "-fn", dmenufont, "-nb", normbgcolor, "-nf", normfgcolor, "-sb", selbordercolor, "-sf", selfgcolor, NULL };
static const char *blendercmd[] = { "blender", NULL };

static const char *termcmd[]  = { "kitty", NULL };
static const char *dolphincmd[] = { "dolphin", NULL };

#if ZOOM
static const char *zoomin[] = { "vcompmgr", "-Z", "+0.15", NULL }; // zoom in
static const char *zoomout[] = { "vcompmgr", "-Z", "-0.15", NULL }; // zoom out
static const char *zoomreset[] = { "vcompmgr", "-Z", "1", NULL }; // set zoom to 1
#endif

static const Key keys[] = {
	/* Custom defaults. Super is Mod4; Alt is Mod1. */
	{ MODKEY, XK_Return, spawn, {.v = termcmd} },
	/* Super+d is handled by sxhkd for the QML launcher. */
	{ MODKEY, XK_e,      spawn, {.v = dolphincmd} },
	{ MODKEY, XK_b,      spawn, {.v = blendercmd} },
	{ MODKEY, XK_j,      focusstack, {.i = -1} },
	{ MODKEY, XK_k,      focusstack, {.i = +1} },
	{ MODKEY, XK_q,      killclient, {0} },
	{ MODKEY, XK_m,      minimizeclient, {0} },
	{ MODKEY|ShiftMask, XK_m, restoreminimized, {0} },
	{ MODKEY, XK_f,      maximizeorrestore, {0} },
#if INFINITE_TAGS
	{ MODKEY, XK_p,      pinwindow, {0} },
#endif
	TAGKEYS(XK_1, 0)
	TAGKEYS(XK_2, 1)
	TAGKEYS(XK_3, 2)
	TAGKEYS(XK_4, 3)
	TAGKEYS(XK_5, 4)
};

/* button definitions */
/* click can be ClkTagBar, ClkLtSymbol, ClkStatusText, ClkWinTitle, ClkClientWin, or ClkRootWin */
static const Button buttons[] = {
	/* click                event mask      button          function        argument */
#if INFINITE_TAGS
  { ClkRootWin,           MODKEY|ShiftMask,         Button1,        movecanvasmouse,     {.f = 1.5 } }, 
  { ClkClientWin,         MODKEY|ShiftMask,         Button1,        movecanvasmouse,     {.f = 1.5 } },
  /* .f = 1 is moving multiplier, for example if set to 0.5, canvas will move 2 times slower, if set to 2, canvas will move 2 times faster. 
     If you want inverted canvas move then set the value to a negative value. */
#endif
#if ZOOM
  { ClkRootWin,           MODKEY,         SCROLL_UP,      spawn,          {.v = zoomin } },
  { ClkRootWin,           MODKEY,         SCROLL_DOWN,    spawn,          {.v = zoomout } },

  { ClkClientWin,         MODKEY,         SCROLL_UP,      spawn,          {.v = zoomin } },
  { ClkClientWin,         MODKEY,         SCROLL_DOWN,    spawn,          {.v = zoomout } },
#endif
	{ ClkWinTitle,          0,              Button2,        swapmaster,     {0} },
	{ ClkStatusText,        0,              Button2,        spawn,          {.v = termcmd } },
	{ ClkClientWin,         MODKEY,         Button1,        movemouse,      {0} },
	{ ClkClientWin,         MODKEY,         Button2,        togglefloating, {0} },
	{ ClkClientWin,         MODKEY,         Button3,        resizemouse,    {0} },
	{ ClkTagBar,            0,              Button1,        view,           {0} },
	{ ClkTagBar,            0,              Button3,        toggleview,     {0} },
	{ ClkTagBar,            MODKEY,         Button1,        tag,            {0} },
	{ ClkTagBar,            MODKEY,         Button3,        toggletag,      {0} },
};

