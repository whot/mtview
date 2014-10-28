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
#include <libevdev/libevdev.h>
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <cairo.h>
#include <cairo-xlib.h>
#include <getopt.h>
#include <poll.h>

#define DEFAULT_WIDTH 200
#define MIN_WIDTH 5
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
	int has_mt;
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

	/* XI2 axis mapping */
	int x_valuator;
	int y_valuator;
	int pressure_valuator;
	int mt_major_valuator;
	int mt_minor_valuator;
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

static inline float min(float a, float b)
{
	return b < a ? b : a;
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
	XFlush(win->dsp);
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

	if (touch_info->has_touch_major) {
		major = minor = t->data[ABS_MT_TOUCH_MAJOR];
		if (touch_info->has_touch_minor)
			minor = t->data[ABS_MT_TOUCH_MINOR];
		angle = t->data[ABS_MT_ORIENTATION];
	}
	if (major == 0 && minor == 0) {
		major = DEFAULT_WIDTH;
		minor = DEFAULT_WIDTH;
	}

	float ac = fabs(cos(angle));
	float as = fabs(sin(angle));
	float mx = max(MIN_WIDTH, max(minor * ac, major * as) * dx);
	float my = max(MIN_WIDTH, max(major * ac, minor * as) * dy);

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

	memset(w, 0, sizeof(*w));
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

	XSelectInput(w->dsp, w->win, StructureNotifyMask|ExposureMask);
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

static void handle_key_event(struct input_event *ev, struct touch_info *touch_info)
{
	int slot;

	if (touch_info->has_mt)
		return;

	slot = touch_info->current_slot;

	/* Switch of tool is new tracking ID, so we get a new-coloured
	   circle. Exception is BTN_TOUCH, since that just indicates current
	   tool touched surface */
	if (ev->code >= BTN_DIGI && ev->code < BTN_WHEEL && ev->code != BTN_TOUCH)
		touch_info->touches[slot].data[ABS_MT_TRACKING_ID] = ev->code;
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
			slot = ev->value;
			if (ev->value >= DIM_TOUCH) {
				msg("Too many simultaneous touches.\n");
				slot = -1;
			}
			touch_info->current_slot = slot;
			break;
	}
	if (slot == -1)
		return;

	if (!touch_info->has_mt) {
		switch(ev->code) {
			case ABS_X: ev->code = ABS_MT_POSITION_X; break;
			case ABS_Y: ev->code = ABS_MT_POSITION_Y; break;
			case ABS_PRESSURE: ev->code = ABS_MT_PRESSURE; break;
			default:
				break;
		}

	}
	touch_info->touches[slot].data[ev->code] = ev->value;
}

static int handle_event(struct input_event *ev, struct touch_info *touch_info)
{
	if (ev->type == EV_SYN && ev->code == SYN_REPORT)
		return 1;

	if (ev->type == EV_ABS)
		handle_abs_event(ev, touch_info);
	if (ev->type == EV_KEY)
		handle_key_event(ev, touch_info);

	return 0;
}

static void run_window_mtdev(struct touch_info *touch_info,
			     struct mtdev *dev, int fd)
{
	struct input_event iev;
	struct windata w;
	XEvent xev;
	struct pollfd fds[2];

	if (init_window(&w)) {
		error("Failed to open window.\n");
		return;
	}

	clear_screen(touch_info, &w);

	set_screen_size_mtdev(&w, 0);

	fds[0].fd = fd;
	fds[0].events = POLLIN;
	fds[0].revents = 0;
	fds[1].fd = ConnectionNumber(w.dsp);
	fds[1].events = POLLIN;
	fds[1].revents = 0;

	while (poll(fds, 2, -1) != -1) {
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

static int is_mt_device(const struct libevdev *dev)
{
	return libevdev_has_event_code(dev, EV_ABS, ABS_MT_POSITION_X) &&
	       libevdev_has_event_code(dev, EV_ABS, ABS_MT_POSITION_Y);
}

static void init_single_touch(const struct libevdev *dev,
			      struct touch_info *t)
{
	t->has_mt = 0;
	t->ntouches = 1;
	t->current_slot = 0;
	t->minx = libevdev_get_abs_minimum(dev, ABS_X);
	t->maxx = libevdev_get_abs_maximum(dev, ABS_X);
	t->miny = libevdev_get_abs_minimum(dev, ABS_Y);
	t->maxy = libevdev_get_abs_maximum(dev, ABS_Y);

	t->has_pressure = libevdev_has_event_code(dev, EV_ABS, ABS_PRESSURE);
	t->has_touch_major = 0;
	t->has_touch_minor = 0;

	t->touches[0].active = 1;
	memset(t->touches[0].data, 0, sizeof(t->touches[0].data));
}

static void init_touches(const struct libevdev *dev,
			 struct touch_info *t, int ntouches)
{
	int i;

	t->has_mt = 1;
	t->ntouches = ntouches;
	t->current_slot = libevdev_get_current_slot(dev);

	t->minx = libevdev_get_abs_minimum(dev, ABS_MT_POSITION_X);
	t->maxx = libevdev_get_abs_maximum(dev, ABS_MT_POSITION_X);
	t->miny = libevdev_get_abs_minimum(dev, ABS_MT_POSITION_Y);
	t->maxy = libevdev_get_abs_maximum(dev, ABS_MT_POSITION_Y);

	t->has_pressure = libevdev_has_event_code(dev, EV_ABS, ABS_MT_PRESSURE);
	t->has_touch_major = libevdev_has_event_code(dev, EV_ABS, ABS_MT_TOUCH_MAJOR);
	t->has_touch_minor = libevdev_has_event_code(dev, EV_ABS, ABS_MT_TOUCH_MINOR);

	for (i = 0; i < t->ntouches; i++) {
		t->touches[i].active = 0;
		memset(t->touches[i].data, 0, sizeof(t->touches[i].data));
		t->touches[i].data[ABS_MT_TRACKING_ID] = -1;
		t->touches[i].data[ABS_MT_SLOT] = -1;
	}
}

static int run_mtdev(const char *name)
{
	struct libevdev *evdev;
	struct mtdev *mtdev;
	struct touch_info t;
	int fd, rc;

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

	rc = libevdev_new_from_fd(fd, &evdev);
	if (rc != 0) {
		error("could not describe device: %s\n",
		      strerror(-rc));
		return -1;
	}

	mtdev = mtdev_new_open(fd);
	if (!mtdev) {
		error("could not open mtdev\n");
		return -1;
	}


	if (is_mt_device(evdev))
		init_touches(evdev, &t, DIM_TOUCH);
	else {
		msg("This a not a multitouch device\n");
		init_single_touch(evdev, &t);
	}

	libevdev_free(evdev);
	evdev = NULL;

	run_window_mtdev(&t, mtdev, fd);

	mtdev_close_delete(mtdev);

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

static int scan_devices_xi2(void)
{
	int major = 2, minor = 2;
	Display *dpy = XOpenDisplay(NULL);
	XIDeviceInfo *info;
	int ndevices, i;
	int deviceid = 0;

	XIQueryVersion(dpy, &major, &minor);
	if (major != 2 && minor < 2) {
		printf("Unsupported XI2 version. Need 2.2 or newer, have %d.%d\n", major, minor);
		goto out;
	}

	info = XIQueryDevice(dpy, XIAllDevices, &ndevices);
	for (i = 0; i < ndevices; i++) {
		XIDeviceInfo *dev = &info[i];
		fprintf(stderr, "%d:	%s\n", dev->deviceid, dev->name);
	}

	fprintf(stderr, "Select the device id [2-%d]: ", ndevices + 2); /* VCP offset */
	scanf("%d", &deviceid);

	for (i = 0; i < ndevices; i++) {
		if (info[i].deviceid == deviceid) {
			break;
		}
	}

	if (i == ndevices)
		deviceid = 0;

	XIFreeDeviceInfo(info);
out:
	XCloseDisplay(dpy);
	return deviceid;
}


static int init_device(Display *dpy, int deviceid, struct touch_info *ti) {
	XIDeviceInfo *info;
	int ndevices, i;
	Atom pressure, mt_major, mt_minor;

	info = XIQueryDevice(dpy, deviceid, &ndevices);
	if (!info || ndevices == 0) {
		error("Failed to open device\n");
		return -1;
	}
	pressure = XInternAtom(dpy, "Abs Pressure", True);
	mt_major = XInternAtom(dpy, "Abs MT Touch Major", True);
	mt_minor = XInternAtom(dpy, "Abs MT Touch Minor", True);

	for (i = 0; i < info->num_classes; i++) {
		switch(info->classes[i]->type) {
			case XITouchClass:
				ti->has_mt = 1;
				ti->has_pressure = 0;
				ti->has_touch_major = 0;
				ti->has_touch_minor = 0;
				ti->ntouches = ((XITouchClassInfo*)info->classes[i])->num_touches;
				break;
			case XIValuatorClass:
				{
					XIValuatorClassInfo *vi = (XIValuatorClassInfo*)info->classes[i];
					if (vi->number == 0) {
						ti->minx = vi->min;
						ti->maxx = vi->max;
						ti->x_valuator = vi->number;
					} else if (vi->number == 1) {
						ti->miny = vi->min;
						ti->maxy = vi->max;
						ti->y_valuator = vi->number;
					}
					if (vi->label == pressure) {
						ti->has_pressure = 1;
						ti->pressure_valuator = vi->number;
					} else if (vi->label == mt_major) {
						ti->has_touch_major = 1;
						ti->mt_major_valuator = vi->number;
					} else if (vi->label == mt_minor) {
						ti->has_touch_minor = 1;
						ti->mt_minor_valuator = vi->number;
					}
				}
				break;
		}
	}

	if (ti->ntouches > DIM_TOUCH) {
		msg("Device claims to support %d touches. Clamping to %d.\n", ti->ntouches, DIM_TOUCH);
		ti->ntouches = DIM_TOUCH;
	}

	for (i = 0; i < ti->ntouches; i++) {
		ti->touches[i].active = 0;
		memset(ti->touches[i].data, 0, sizeof(ti->touches[i].data));
		ti->touches[i].data[ABS_MT_TRACKING_ID] = -1;
		ti->touches[i].data[ABS_MT_SLOT] = -1;
	}

	return 0;
}

static void handle_xi2_event(Display *dpy, XEvent *e, struct touch_info *ti)
{
	int i;
	double *v;
	struct touch_data *touch = NULL;
	XIDeviceEvent *ev;
	XGetEventData(dpy, &e->xcookie);

	ev = e->xcookie.data;
	if (ev->evtype != XI_TouchBegin &&
	    ev->evtype != XI_TouchUpdate &&
	    ev->evtype != XI_TouchEnd)
		return;

	for (i = 0; i < ti->ntouches && touch == NULL; i++) {
		if (!ti->touches[i].active)
			continue;

		if (ti->touches[i].data[ABS_MT_TRACKING_ID] == ev->detail)
			touch = &ti->touches[i];
	}

	if (touch == NULL) {
		if (ev->evtype != XI_TouchBegin)
			return;

		for (i = 0; i < ti->ntouches && touch == NULL; i++) {
			if (!ti->touches[i].active) {
				touch = &ti->touches[i];
				touch->data[ABS_MT_SLOT] = i;
			}
		}
	}

	if (touch == NULL) {
		msg("Too many simultaneous touches. Ignoring most-recent new contact.\n");
		return;
	}

	/* store tracking ID in active */
	touch->active = (ev->evtype != XI_TouchEnd);
	touch->data[ABS_MT_POSITION_X] = ev->root_x;
	touch->data[ABS_MT_POSITION_Y] = ev->root_y;
	touch->data[ABS_MT_TRACKING_ID] = ev->detail;

	v = ev->valuators.values;
	for (i = 0; i <= ev->valuators.mask_len; i++) {
		if (!XIMaskIsSet(ev->valuators.mask, i))
			continue;
		if (i == ti->x_valuator)
			touch->data[ABS_MT_POSITION_X] = (int)*v;
		else if (i == ti->y_valuator)
			touch->data[ABS_MT_POSITION_Y] = (int)*v;
		else if (i == ti->pressure_valuator)
			touch->data[ABS_MT_PRESSURE] = (int)*v;
		else if (i == ti->mt_major_valuator)
			touch->data[ABS_MT_TOUCH_MAJOR] = (int)*v;
		else if (i == ti->mt_minor_valuator)
			touch->data[ABS_MT_TOUCH_MINOR] = (int)*v;

		v++;
	}

	XFreeEventData(dpy, &e->xcookie);
}

static int run_mtdev_xi2(int deviceid)
{
	int major = 2, minor = 2;
	struct windata w;
	struct touch_info touch_info = {0};
	XIEventMask mask;
	unsigned char m[XIMaskLen(XI_LASTEVENT)] = {0};

	if (init_window(&w)) {
		error("Failed to open window.\n");
		return 1;
	}

	XIQueryVersion(w.dsp, &major, &minor);

	if (init_device(w.dsp, deviceid, &touch_info))
		return 1;

	clear_screen(&touch_info, &w);

	set_screen_size_mtdev(&w, 0);

	mask.mask = m;
	mask.deviceid = deviceid;
	mask.mask_len = sizeof(m);
	XISetMask(mask.mask, XI_TouchBegin);
	XISetMask(mask.mask, XI_TouchUpdate);
	XISetMask(mask.mask, XI_TouchEnd);

	while(1) {
		XEvent xev;
		XNextEvent(w.dsp, &xev);
		if (xev.type == ConfigureNotify) {
			set_screen_size_mtdev(&w, &xev);
		} else if (xev.type == Expose) {
			static int grabbed = 0;
			if (!grabbed &&
			    XIGrabDevice(w.dsp, deviceid, w.win, CurrentTime, None,
						GrabModeAsync, GrabModeAsync,
						False, &mask) != Success) {
				error("Failed to grab device\n");
				return 1;
			} else
				grabbed = 1;
		}
		else if (xev.type == GenericEvent) {
			handle_xi2_event(w.dsp, &xev, &touch_info);
			report_frame(&touch_info, &w);
		}
	}

	term_window(&w);

	return 0;
}

enum mode {
	MODE_EVDEV,
	MODE_XI2,
};

static void usage(void) {
	printf("%s [--mode=evdev|xi2] [device]\n", program_invocation_short_name);
}

int main(int argc, char *argv[])
{
	int ret;
	char *device = NULL;
	int deviceid = 0;
	enum mode mode = MODE_EVDEV;

	while (1) {
		static struct option long_options[] = {
			{ "mode", required_argument, 0, 0 },
			{ "help", no_argument, 0, 'h' },
		};

		int option_index = 0;
		int c;

		c = getopt_long(argc, argv, "h", long_options,
				&option_index);
		if (c == -1)
			break;

		switch(c) {
			case 0:
				if (strcmp(long_options[option_index].name, "mode") == 0 &&
				    optarg && strcmp(optarg, "xi2") == 0)
					mode = MODE_XI2;
				break;
			case 'h':
				usage();
				return 0;
			default:
				break;
		}
	}


	if (mode == MODE_EVDEV) {
		if (optind < argc)
			device = strdup(argv[optind]);
		else
			device = scan_devices();

		if (!device) {
		    error("Failed to find a device.\n");
		    return 1;
		}

		ret = run_mtdev(device);
		free(device);
	} else if (mode == MODE_XI2) {
		if (optind < argc)
			deviceid = atoi(argv[optind]);
		else
			deviceid = scan_devices_xi2();

		if (deviceid == 0) {
		    error("Failed to find a device.\n");
		    return 1;
		}
		ret = run_mtdev_xi2(deviceid);
	}

	return ret;
}
