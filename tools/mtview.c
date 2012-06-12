/*****************************************************************************
 *
 * mtview - Multitouch Viewer (GPLv3 license)
 *
 * Copyright (C) 2010-2011 Canonical Ltd.
 * Copyright (C) 2010      Henrik Rydberg <rydberg@euromail.se>
 * Copyright © 2012 Red Hat, Inc
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 ****************************************************************************/


#define _GNU_SOURCE
#include "config.h"

#include <linux/input.h>
#include <mtdev.h>
#include <evemu.h>
#include <X11/Xlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <cairo.h>
#include <cairo-xlib.h>

#define DEFAULT_WIDTH 200
#define DEFAULT_WIDTH_MULTIPLIER 5 /* if no major/minor give the actual size */

#define DIM_TOUCH 32

static int opcode;

struct color {
	float r, g, b;
};

struct touch_data {
	int active;
	int data[ABS_CNT];
};

struct touch_info {
	int minx,
	    maxx,
	    miny,
	    maxy;
	int has_pressure;
	int has_touch_major,
	    has_touch_minor;

	int ntouches;
	struct touch_data touches[DIM_TOUCH];
	int current_slot;
};

struct windata {
	Display *dsp;
	Window win;
	GC gc;
	Visual *visual;
	int screen;
	float off_x, off_y;
	int width, height; /* of window */
	unsigned long white, black;
	struct color color[DIM_TOUCH];
	int id[DIM_TOUCH];

	/* buffer */
	cairo_t *cr;
	cairo_surface_t *surface;

	/* window */
	cairo_t *cr_win;
	cairo_surface_t *surface_win;
};

static int error(const char *fmt, ...)
{
	va_list args;
	fprintf(stderr, "error: ");

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	return EXIT_FAILURE;
}

static void msg(const char *fmt, ...)
{
	va_list args;
	printf("info: ");

	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
}

static inline float max(float a, float b)
{
	return b > a ? b : a;
}

static struct color new_color(struct windata *w)
{
	struct color c;

	c.r = 1.0 * rand()/RAND_MAX;
	c.g = 1.0 * rand()/RAND_MAX;
	c.b = 1.0 * rand()/RAND_MAX;
	return c;
}

static void expose(struct windata *win, int x, int y, int w, int h)
{
	cairo_set_source_surface(win->cr_win, win->surface, 0, 0);
	cairo_rectangle(win->cr_win, x, y, w, h);
	cairo_fill(win->cr_win);
}

static void clear_screen(struct touch_info *touch_info, struct windata *w)
{
	int width = touch_info->maxx - touch_info->minx;
	int height = touch_info->maxy - touch_info->miny;

	cairo_set_line_width(w->cr, 1);
	cairo_set_source_rgb(w->cr, 1, 1, 1);
	cairo_rectangle(w->cr, 0, 0, width, height);
	cairo_fill(w->cr);

	expose(w, 0, 0, width, height);
}

static void output_touch(const struct touch_info *touch_info,
			 struct windata *w,
			 const struct touch_data *t)
{
	float dx = 1.0 * w->width/(touch_info->maxx - touch_info->minx);
	float dy = 1.0 * w->height/(touch_info->maxy - touch_info->miny);
	float x = (t->data[ABS_MT_POSITION_X] - touch_info->minx) * dx,
	      y = (t->data[ABS_MT_POSITION_Y] - touch_info->miny) * dy;
	float major = 0, minor = 0, angle = 0;

	if (touch_info->has_pressure) {
		major = DEFAULT_WIDTH_MULTIPLIER * t->data[ABS_MT_PRESSURE] * dy;
		minor = DEFAULT_WIDTH_MULTIPLIER * t->data[ABS_MT_PRESSURE] * dx;
		angle = 0;
	}
	if (touch_info->has_touch_major && touch_info->has_touch_minor) {
		major = t->data[ABS_MT_TOUCH_MAJOR];
		minor = t->data[ABS_MT_TOUCH_MINOR];
		angle = t->data[ABS_MT_ORIENTATION];
	}
	if (major == 0 && minor == 0) {
		major = DEFAULT_WIDTH * dy;
		minor = DEFAULT_WIDTH * dx;
	}

	float ac = fabs(cos(angle));
	float as = fabs(sin(angle));
	float mx = max(minor * ac, major * as);
	float my = max(major * ac, minor * as);

	if (w->id[t->data[ABS_MT_SLOT]] != t->data[ABS_MT_TRACKING_ID]) {
		w->id[t->data[ABS_MT_SLOT]] = t->data[ABS_MT_TRACKING_ID];
		w->color[t->data[ABS_MT_SLOT]] = new_color(w);
	}

	cairo_set_source_rgb(w->cr,
			     w->color[t->data[ABS_MT_SLOT]].r,
			     w->color[t->data[ABS_MT_SLOT]].g,
			     w->color[t->data[ABS_MT_SLOT]].b);
	/* cairo ellipsis */
	cairo_save(w->cr);
	cairo_translate(w->cr, x, y);
	cairo_scale(w->cr, mx/2., my/2.);
	cairo_arc(w->cr, 0, 0, 1, 0, 2 * M_PI);
	cairo_fill(w->cr);
	cairo_restore(w->cr);

	expose(w, x - mx/2, y - my/2, mx, my);
}

static void report_frame(const struct touch_info *touch_info,
			 struct windata *w)
{
	int i;

	for (i = 0; i < touch_info->ntouches; i++)
		if (touch_info->touches[i].active)
			output_touch(touch_info, w, &touch_info->touches[i]);
}

static int init_window(struct windata *w)
{
	int event, err;
	int i;

	memset(w, 0, sizeof(w));
	for (i = 0; i < DIM_TOUCH; i++)
		w->id[i] = -1;

	w->dsp = XOpenDisplay(NULL);
	if (!w->dsp)
		return -1;
	if (!XQueryExtension(w->dsp, "XInputExtension", &opcode, &event, &err))
		return -1;

	w->screen = DefaultScreen(w->dsp);
	w->white = WhitePixel(w->dsp, w->screen);
	w->black = BlackPixel(w->dsp, w->screen);

	w->width = DisplayWidth(w->dsp, w->screen);
	w->height = DisplayHeight(w->dsp, w->screen);
	w->off_x = 0;
	w->off_y = 0;
	w->win = XCreateSimpleWindow(w->dsp, XDefaultRootWindow(w->dsp),
				     0, 0, w->width, w->height,
				     0, w->black, w->white);
	w->gc = DefaultGC(w->dsp, w->screen);
	w->visual = DefaultVisual(w->dsp, w->screen);


	w->surface_win = cairo_xlib_surface_create(w->dsp, w->win, w->visual,
						   w->width, w->height);
	w->cr_win = cairo_create(w->surface_win);

	w->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
						w->width, w->height);
	w->cr = cairo_create(w->surface);

	cairo_set_line_width(w->cr, 1);
	cairo_set_source_rgb(w->cr, 1, 1, 1);
	cairo_rectangle(w->cr, 0, 0, w->width, w->height);
	cairo_fill(w->cr);

	expose(w, 0, 0, w->width, w->height);

	XSelectInput(w->dsp, w->win, StructureNotifyMask);
	XMapWindow(w->dsp, w->win);
	XFlush(w->dsp);

	return 0;
}

static void term_window(struct windata *w)
{
	cairo_destroy(w->cr);
	cairo_destroy(w->cr_win);
	cairo_surface_destroy(w->surface);
	cairo_surface_destroy(w->surface_win);

	XDestroyWindow(w->dsp, w->win);
	XCloseDisplay(w->dsp);
}

static void set_screen_size_mtdev(struct windata *w,
				  XEvent *xev)
{
	XConfigureEvent *cev = (XConfigureEvent *)xev;

	if (cev && cev->width && cev->height) {
		if (cev->width != w->width || cev->height != w->height)
		{
			cairo_destroy(w->cr_win);
			cairo_surface_destroy(w->surface_win);

			w->width = cev->width;
			w->height = cev->height;
			w->surface_win = cairo_xlib_surface_create(w->dsp, w->win,
								   w->visual,
								   w->width, w->height);
			w->cr_win = cairo_create(w->surface_win);
			expose(w, 0, 0, w->width, w->height);
		}
	}
}

static void handle_abs_event(struct input_event *ev, struct touch_info *touch_info)
{
	int slot;

	slot = touch_info->current_slot;
	switch(ev->code) {
		case ABS_MT_TRACKING_ID:
			if (slot == -1)
				break;
			touch_info->touches[slot].active = (ev->value != -1);
			break;
		case ABS_MT_SLOT:
			touch_info->current_slot = ev->value;
			slot = touch_info->current_slot;
			break;
	}
	if (slot == -1)
		return;

	touch_info->touches[slot].data[ev->code] = ev->value;
}

static int handle_event(struct input_event *ev, struct touch_info *touch_info)
{
	if (ev->type == EV_SYN && ev->code == SYN_REPORT)
		return 1;

	if (ev->type == EV_ABS)
		handle_abs_event(ev, touch_info);
	return 0;
}

static void run_window_mtdev(struct touch_info *touch_info,
			     struct mtdev *dev, int fd)
{
	struct input_event iev;
	struct windata w;
	XEvent xev;

	if (init_window(&w))
		return;

	clear_screen(touch_info, &w);

	set_screen_size_mtdev(&w, 0);

	while (1) {
		while (!mtdev_idle(dev, fd, 100)) {
			while (mtdev_get(dev, fd, &iev, 1) > 0) {
				if (handle_event(&iev, touch_info))
					report_frame(touch_info, &w);
			}
		}
		while (XPending(w.dsp)) {
			XNextEvent(w.dsp, &xev);
			if (xev.type == ConfigureNotify)
				set_screen_size_mtdev(&w, &xev);
		}
	}

	term_window(&w);
}

static int is_mt_device(const struct evemu_device *dev)
{
	return evemu_has_event(dev, EV_ABS, ABS_MT_POSITION_X) &&
	       evemu_has_event(dev, EV_ABS, ABS_MT_POSITION_Y);
}

static void init_touches(const struct evemu_device *dev,
			 struct touch_info *t, int ntouches)
{
	int i;

	t->ntouches = ntouches;
#if EVEMU_HAVE_GET_ABS_CURRENT_VALUE
	t->current_slot = evemu_get_abs_current_value(dev, ABS_MT_SLOT);
#else
	t->current_slot = -1;
	msg("Cannot get current slot value from evemu.\n"
	    "You may not see touchpoints until two or more touchpoints are triggered\n");
#endif

	t->minx = evemu_get_abs_minimum(dev, ABS_MT_POSITION_X);
	t->maxx = evemu_get_abs_maximum(dev, ABS_MT_POSITION_X);
	t->miny = evemu_get_abs_minimum(dev, ABS_MT_POSITION_Y);
	t->maxy = evemu_get_abs_maximum(dev, ABS_MT_POSITION_Y);

	t->has_pressure = evemu_has_event(dev, EV_ABS, ABS_MT_PRESSURE);
	t->has_touch_major = evemu_has_event(dev, EV_ABS, ABS_MT_TOUCH_MAJOR);
	t->has_touch_minor = evemu_has_event(dev, EV_ABS, ABS_MT_TOUCH_MINOR);

	for (i = 0; i < t->ntouches; i++) {
		t->touches[i].active = 0;
		memset(t->touches[i].data, 0, sizeof(t->touches[i].data));
		t->touches[i].data[ABS_MT_TRACKING_ID] = -1;
		t->touches[i].data[ABS_MT_SLOT] = -1;
	}
}

static int run_mtdev(const char *name)
{
	struct evemu_device *evemu;
	struct mtdev *mtdev;
	struct touch_info t;
	int fd;

	fd = open(name, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		error("could not open device (%s)\n", strerror(errno));
		return -1;
	}
	if (ioctl(fd, EVIOCGRAB, 1)) {
		error("could not grab the device.\n");
		error("This device may already be grabbed by "
		      "another process (e.g. the synaptics or the wacom "
		      "X driver)\n");
		return -1;
	}

	evemu = evemu_new(0);
	if (!evemu || evemu_extract(evemu, fd)) {
		error("could not describe device\n");
		return -1;
	}

	if (!is_mt_device(evemu)) {
		error("unsupported device\n");
		error("Is this a multitouch device?\n");
		return -1;
	}
	mtdev = mtdev_new_open(fd);
	if (!mtdev) {
		error("could not open mtdev\n");
		return -1;
	}

	init_touches(evemu, &t, DIM_TOUCH);

	run_window_mtdev(&t, mtdev, fd);

	mtdev_close_delete(mtdev);
	evemu_delete(evemu);

	ioctl(fd, EVIOCGRAB, 0);
	close(fd);

	return 0;
}

#define DEV_INPUT_EVENT "/dev/input"
#define EVENT_DEV_NAME "event"

static int is_event_device(const struct dirent *dir) {
	return strncmp(EVENT_DEV_NAME, dir->d_name, 5) == 0;
}

static char* scan_devices(void)
{
	struct dirent **namelist;
	int i, ndev, devnum;
	char *filename;

	ndev = scandir(DEV_INPUT_EVENT, &namelist, is_event_device, alphasort);
	if (ndev <= 0)
		return NULL;

	fprintf(stderr, "Available devices:\n");

	for (i = 0; i < ndev; i++)
	{
		char fname[64];
		int fd = -1;
		char name[256] = "???";

		snprintf(fname, sizeof(fname),
			 "%s/%s", DEV_INPUT_EVENT, namelist[i]->d_name);
		fd = open(fname, O_RDONLY);
		if (fd < 0)
			continue;
		ioctl(fd, EVIOCGNAME(sizeof(name)), name);

		fprintf(stderr, "%s:	%s\n", fname, name);
		close(fd);
		free(namelist[i]);
	}

	fprintf(stderr, "Select the device event number [0-%d]: ", ndev - 1);
	scanf("%d", &devnum);

	if (devnum >= ndev || devnum < 0)
		return NULL;

	asprintf(&filename, "%s/%s%d",
		 DEV_INPUT_EVENT, EVENT_DEV_NAME,
		 devnum);

	return filename;
}

int main(int argc, char *argv[])
{
	int ret;
	char *device = NULL;

	if (argc < 2) {
		device = scan_devices();
		if (!device)
		{
		    error("Failed to find a device.\n");
		    return 1;
		}
	} else
	    device = strdup(argv[1]);

	ret = run_mtdev(device);

	free(device);

	return ret;
}
