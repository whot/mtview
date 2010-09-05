#include <X11/Xlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <grail-touch.h>
#include <string.h>
#include <math.h>

#define XMARG 16
#define YMARG 16
#define WSCALE 0.5
#define FLUSH_MS 10
#define DEF_FRAC 0.15

struct windata {
	Display *dsp;
	Window root, win;
	GC gc;
	int screen, width, height;
	unsigned long white, black;
	unsigned long color[DIM_TOUCH];
	int id[DIM_TOUCH];
	touch_time_t last_flush;
};

static inline float max(float a, float b)
{
	return b > a ? b : a;
}

static unsigned long new_color(struct windata *w)
{
	return lrand48() & 0xffffff;
}

static void clear_screen(struct windata *w)
{
	XSetForeground(w->dsp, w->gc, w->black);
	XFillRectangle(w->dsp, w->win, w->gc, 0, 0, w->width, w->height);
}

static void output_touch(struct touch_dev *dev, struct windata *w,
			 const struct touch *t)
{
	float x1 = dev->caps.min_x, y1 = dev->caps.min_y;
	float x2 = dev->caps.max_x, y2 = dev->caps.max_y;
	float dx = x2 - x1, dy = y2 - y1;
	float major = 0, minor = 0, angle = 0;

	if (t->pressure > 0) {
		float p = DEF_FRAC / dev->caps.max_press;
		major = t->pressure * p * dx;
		minor = t->pressure * p * dx;
		angle = 0;
	}
	if (t->touch_major > 0 || t->touch_minor > 0) {
		major = t->touch_major;
		minor = t->touch_minor;
		if (major && !minor)
			minor = major;
		angle = touch_angle(dev, t->orientation);
	}

	float ac = fabs(cos(angle));
	float as = fabs(sin(angle));
	float mx = max(minor * ac, major * as);
	float my = max(major * ac, minor * as);
	float ux = t->x - 0.5 * mx;
	float uy = t->y - 0.5 * my;
	float vx = t->x + 0.5 * mx;
	float vy = t->y + 0.5 * my;

	float px = (ux - x1) / dx * w->width;
	float py = (uy - y1) / dy * w->height;
	float qx = (vx - x1) / dx * w->width;
	float qy = (vy - y1) / dy * w->height;

	if (w->id[t->slot] != t->id) {
		w->id[t->slot] = t->id;
		w->color[t->slot] = new_color(w);
	}

	XSetForeground(w->dsp, w->gc, w->color[t->slot]);
	XFillArc(w->dsp, w->win, w->gc, px, py, qx - px, qy - py, 0, 360 * 64);

	if (dev->frame.time - w->last_flush > FLUSH_MS) {
		XFlush(w->dsp);
		w->last_flush = dev->frame.time;
	}
}

static void tp_event(struct touch_dev *dev,
		     const struct input_event *ev)
{
}

static void tp_sync(struct touch_dev *dev,
		    const struct input_event *syn)
{
	struct windata *w = dev->priv;
	struct touch_frame *frame = &dev->frame;
	int i;
	for (i = 0; i < frame->nactive; i++)
		output_touch(dev, w, frame->active[i]);
}

static void event_loop(struct touch_dev *dev, int fd, struct windata *w)
{
	XEvent ev;

	dev->event = tp_event;
	dev->sync = tp_sync;
	dev->priv = w;

	XSelectInput(w->dsp, w->win,
		     ButtonPressMask | ButtonReleaseMask |
		     ExposureMask | StructureNotifyMask);

	clear_screen(w);
	while (1) {
		if(!touch_dev_idle(dev, fd, 100))
			touch_dev_pull(dev, fd);
		while (XPending(w->dsp)) {
			XNextEvent(w->dsp, &ev);
		}
	}
}

static void run_window(struct touch_dev *dev, int fd)
{
	struct windata w;
	int i;
	memset(&w, 0, sizeof(w));
	for (i = 0; i < DIM_TOUCH; i++)
		w.id[i] = -1;

	w.dsp = XOpenDisplay(NULL);
	if (!w.dsp)
		return;

	w.screen = DefaultScreen(w.dsp);
	w.white = WhitePixel(w.dsp, w.screen);
	w.black = BlackPixel(w.dsp, w.screen);
	w.width = DisplayWidth(w.dsp, w.screen) - XMARG;
	w.height = DisplayHeight(w.dsp, w.screen) - YMARG;

	w.root = DefaultRootWindow(w.dsp);
	w.win = XCreateSimpleWindow(w.dsp, w.root,
				    0, 0, w.width, w.height,
				    0, w.black, w.white);

	XMapWindow(w.dsp, w.win);

	long eventMask = StructureNotifyMask;
	XSelectInput(w.dsp, w.win, eventMask);

	XEvent ev;
	do {
		XNextEvent(w.dsp, &ev);
	} while (ev.type != MapNotify);


	w.gc = XCreateGC(w.dsp, w.win, 0, NULL);

	event_loop(dev, fd, &w);

	XDestroyWindow(w.dsp, w.win);
	XCloseDisplay(w.dsp);
}

int main(int argc, char *argv[])
{
	struct touch_dev dev;
	int fd;
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <device>\n", argv[0]);
		return -1;
	}
	fd = open(argv[1], O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		fprintf(stderr, "error: could not open device\n");
		return -1;
	}
	if (ioctl(fd, EVIOCGRAB, 1)) {
		fprintf(stderr, "error: could not grab the device\n");
		return -1;
	}
	if (touch_dev_open(&dev, fd)) {
		fprintf(stderr, "error: could not open touch device\n");
		return -1;
	}
	run_window(&dev, fd);
	touch_dev_close(&dev, fd);
	ioctl(fd, EVIOCGRAB, 0);
	close(fd);
	return 0;
}
