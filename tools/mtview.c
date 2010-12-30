#include <X11/Xlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <utouch/frame-mtdev.h>
#include <string.h>
#include <math.h>

#define XMARG 16
#define YMARG 16
#define DEF_FRAC 0.15
#define DEF_WIDTH 0.05

#define DIM_TOUCH 32

struct windata {
	Display *dsp;
	Window root, win;
	GC gc;
	int screen, width, height;
	unsigned long white, black;
	unsigned long color[DIM_TOUCH];
	int id[DIM_TOUCH];
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

static void output_touch(utouch_frame_handle fh, struct windata *w,
			 const struct utouch_contact *t)
{
	const struct utouch_surface *s = utouch_frame_get_surface(fh);

	float x1 = s->min_x, y1 = s->min_y;
	float x2 = s->max_x, y2 = s->max_y;
	float dx = x2 - x1, dy = y2 - y1;
	float major = 0, minor = 0, angle = 0;

	if (s->use_pressure) {
		float p = DEF_FRAC / s->max_pressure;
		major = t->pressure * p * dx;
		minor = t->pressure * p * dx;
		angle = 0;
	}
	if (s->use_touch_major) {
		major = t->touch_major;
		minor = t->touch_minor;
		angle = t->orientation;
	}
	if (major == 0 && minor == 0) {
		major = DEF_WIDTH * dy;
		minor = DEF_WIDTH * dx;
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
	XFlush(w->dsp);
}

static void report_frame(utouch_frame_handle fh,
			 const struct utouch_frame *frame,
			 struct windata *w)
{
	int i;

	for (i = 0; i < frame->num_active; i++)
		output_touch(fh, w, frame->active[i]);
}

static void event_loop(utouch_frame_handle fh,
		       struct mtdev *dev, int fd,
		       struct windata *w)
{
	const struct utouch_frame *frame;
	struct input_event iev;
	XEvent xev;

	XSelectInput(w->dsp, w->win,
		     ButtonPressMask | ButtonReleaseMask |
		     ExposureMask | StructureNotifyMask);

	clear_screen(w);
	while (1) {
		while (!mtdev_idle(dev, fd, 100)) {
			while (mtdev_get(dev, fd, &iev, 1) > 0) {
				frame = utouch_frame_pump_mtdev(fh, &iev);
				if (frame)
					report_frame(fh, frame, w);
			}
		}
		while (XPending(w->dsp)) {
			XNextEvent(w->dsp, &xev);
		}
	}
}

static void run_window(utouch_frame_handle fh, struct mtdev *dev, int fd)
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

	event_loop(fh, dev, fd, &w);

	XDestroyWindow(w.dsp, w.win);
	XCloseDisplay(w.dsp);
}

int main(int argc, char *argv[])
{
	struct evemu_device *evemu;
	struct mtdev *mtdev;
	utouch_frame_handle fh;
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

	evemu = evemu_new(0);
	if (!evemu || evemu_extract(evemu, fd)) {
		fprintf(stderr, "error: could not describe device\n");
		return -1;
	}
	if (!utouch_frame_is_supported_mtdev(evemu)) {
		fprintf(stderr, "error: unsupported device\n");
		return -1;
	}
	mtdev = mtdev_new_open(fd);
	if (!mtdev) {
		fprintf(stderr, "error: could not open mtdev\n");
		return -1;
	}
	fh = utouch_frame_new_engine(100, 32, 100);
	if (!fh || utouch_frame_init_mtdev(fh, evemu)) {
		fprintf(stderr, "error: could not init frame\n");
		return -1;
	}

	run_window(fh, mtdev, fd);

	utouch_frame_delete_engine(fh);
	mtdev_close_delete(mtdev);
	evemu_delete(evemu);

	ioctl(fd, EVIOCGRAB, 0);
	close(fd);

	return 0;
}
