#include <stdio.h>
#include <stdlib.h>

#include <cairo.h>
#include <cairo-xlib.h>

#include "vis_main.h"

typedef struct win {
    Display *dpy;
    int scr;

    Window win;
    GC gc;

    int width, height;
    KeyCode quit_code;
} win_t;

static void
win_draw(win_t *win)
{
  cairo_surface_t *surface;
  cairo_t *cr;
  Visual *visual = DefaultVisual(win->dpy, DefaultScreen (win->dpy));

  XClearWindow(win->dpy, win->win);

  surface = cairo_xlib_surface_create (win->dpy, win->win, visual,
                                         win->width, win->height);
  cr = cairo_create(surface);

  cairo_set_source_rgb(cr, 1, 1, 1);

  cairo_save(cr);
//  cairo_set_font_size (cr, 20);
//  cairo_move_to (cr, 10, 10);
//  cairo_show_text (cr, "Hello World.");
  cairo_restore(cr);
  if (cairo_status (cr)) {
        printf("Cairo is unhappy: %s\n",
               cairo_status_to_string (cairo_status (cr)));
        exit(0);
  }

  cairo_destroy(cr);
  cairo_surface_destroy (surface);
}

static void
win_init(win_t *win)
{
    Window root;

    win->width = 400;
    win->height = 400;

    root = DefaultRootWindow(win->dpy);
    win->scr = DefaultScreen(win->dpy);

    win->win = XCreateSimpleWindow(win->dpy, root, 0, 0,
                                   win->width, win->height, 0,
                                   BlackPixel(win->dpy, win->scr), BlackPixel(win->dpy, win->scr));

    win->quit_code = XKeysymToKeycode(win->dpy, XStringToKeysym("Q"));

    XSelectInput(win->dpy, win->win,
                 KeyPressMask
                 |StructureNotifyMask
                 |ExposureMask);

    XMapWindow(win->dpy, win->win);
}

static void
win_deinit(win_t *win)
{
    XDestroyWindow(win->dpy, win->win);
}

static void
win_handle_events(win_t *win)
{
    XEvent xev;

    while (1) {
        XNextEvent(win->dpy, &xev);
        switch(xev.type) {
        case KeyPress:
        {
            XKeyEvent *kev = &xev.xkey;

            if (kev->keycode == win->quit_code) {
                return;
            }
        }
        break;
        case ConfigureNotify:
        {
            XConfigureEvent *cev = &xev.xconfigure;

            win->width = cev->width;
            win->height = cev->height;
        }
        break;
        case Expose:
        {
            XExposeEvent *eev = &xev.xexpose;

            if (eev->count == 0)
                win_draw(win);
        }
        break;
        }
    }
}

int display_window(struct addr * addr)
{
    win_t win;

    win.dpy = XOpenDisplay(0);

    if (win.dpy == NULL) {
        fprintf(stderr, "Failed to open display\n");
        return 1;
    }

    win_init(&win);

    win_draw(&win);

    win_handle_events(&win);

    win_deinit(&win);

    XCloseDisplay(win.dpy);
}
