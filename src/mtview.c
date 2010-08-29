#include <X11/Xlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <grail-touch.h>
#include <math.h>

#define XMARG 64
#define YMARG 64
#define WSCALE 0.5

struct windata {
	Display *dsp;
	Window root, win;
	GC gc;
	int screen, width, height, lastid;
	unsigned long white, black, color[DIM_TOUCH];
};

static inline float max(float a, float b)
{
	return b > a ? b : a;
}

static unsigned long new_color()
{
	return lrand48();
}

static void clear_screen(struct windata *w)
{
	XSetForeground(w->dsp, w->gc, w->black);
	XFillRectangle(w->dsp, w->win, w->gc, 0, 0, w->width, w->height);
}

static void output_touch(struct touch_dev *dev, struct windata *w,
			 const struct touch *t)
{
	float a = touch_angle(dev, t->orientation);
	float ac = fabs(cos(a));
	float as = fabs(sin(a));
	float mx = max(t->touch_minor * ac, t->touch_major * as);
	float my = max(t->touch_major * ac, t->touch_minor * as);
	float ux = t->x - 0.5 * mx;
	float uy = t->y - 0.5 * my;
	float vx = t->x + 0.5 * mx;
	float vy = t->y + 0.5 * my;

	float x1 = dev->caps.min_x, y1 = dev->caps.min_y;
	float x2 = dev->caps.max_x, y2 = dev->caps.max_y;
	float dx = x2 - x1, dy = y2 - y1;
	float px = (ux - x1) / dx * w->width;
	float py = (uy - y1) / dy * w->height;
	float qx = (vx - x1) / dx * w->width;
	float qy = (vy - y1) / dy * w->height;
	if (t->id > w->lastid)
		w->color[t->slot] = new_color();
	XSetForeground(w->dsp, w->gc, w->color[t->slot]);
	XFillArc(w->dsp, w->win, w->gc, px, py, qx - px, qy - py, 0, 360 * 64);
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
	int i, lastid = 0;
	for (i = 0; i < frame->nactive; i++) {
		const struct touch *t = frame->active[i];
		output_touch(dev, w, t);
		if (t->id > lastid)
			lastid = t->id;
	}
	w->lastid = lastid;
}

static void event_loop(struct touch_dev *dev, int fd, struct windata *w)
{
	XEvent ev;

	dev->event = tp_event;
	dev->sync = tp_sync;
	dev->priv = w;
	w->lastid = -1;

	XSelectInput(w->dsp, w->win, ButtonPress | ButtonRelease);
	clear_screen(w);
	XFlush(w->dsp);

	while (1) {
		if (XEventsQueued(w->dsp, QueuedAlready)) {
			XNextEvent(w->dsp, &ev);
			if (ev.type == ButtonRelease)
				break;
		}
		if(!touch_dev_idle(dev, fd, 100)) {
			touch_dev_pull(dev, fd);
			XFlush(w->dsp);
		}
	}
}

static void run_window(struct touch_dev *dev, int fd)
{
	struct windata w;

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
