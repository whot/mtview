#include <X11/Xlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <grail-touch.h>

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
	float x1 = dev->caps.min_x, y1 = dev->caps.min_y;
	float x2 = dev->caps.max_x, y2 = dev->caps.max_y;
	int x = (t->x - x1) / (x2 - x1) * w->width;
	int y = (t->y - y1) / (y2 - y1) * w->height;
	int d = t->touch_major / (x2 - x1) * w->width * WSCALE;
	if (t->id > w->lastid)
		w->color[t->slot] = new_color();
	XSetForeground(w->dsp, w->gc, w->color[t->slot]);
	XFillArc(w->dsp, w->win, w->gc, x, y, d, d, 0, 360 * 64);
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

	XSelectInput(w->dsp, w->win, ButtonPressMask | ButtonReleaseMask);
	while (1) {
		XNextEvent(w->dsp, &ev);
		clear_screen(w);
		XNextEvent(w->dsp, &ev);
		while (!touch_dev_idle(dev, fd, 1000))
			touch_dev_pull(dev, fd);
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
