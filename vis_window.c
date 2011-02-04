#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <sys/socket.h>
#include <sys/types.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <cairo.h>
#include <cairo-xlib.h>
#include <unistd.h>

#include <gtk/gtk.h>

#define PI 3.1415926535

#include "vis_window.h"
#include "vis_main.h"

#define BUFLEN 16384

struct modal_stack
{
    GtkWindowGroup * group;
    GtkWidget * window;
    struct addr * cur_addr;
};

//the global pixmap that will serve as our buffer
static GdkPixmap *pixmap = NULL;


void get_machine_info(char * prefix_str, char * memory_usage)
{
  char buf_send[] = "ls\n";
  char buf_recv[BUFLEN];

  int sock_client; 
  struct sockaddr_in client_addr;
  int client_addr_len = sizeof(client_addr);
  int client_port = 50000;

  if ((sock_client = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
      perror("Client: can't open datagram socket\n");
      exit(1);
  }

  memset((char *) &client_addr, 0, sizeof(client_addr));
  client_addr.sin_family = AF_INET;
  client_addr.sin_port = htons((in_port_t) client_port);

  if (inet_aton(prefix_str, &client_addr.sin_addr)==0) {
        fprintf(stderr, "inet_aton() failed\n");
        exit(1);
  }

  if (sendto(sock_client, buf_send, sizeof(buf_send), 0, (struct sockaddr *)&client_addr, client_addr_len) < 0)
    perror("sendto()"); 
  
  if (recvfrom(sock_client, buf_recv, sizeof(buf_recv), 0, (struct sockaddr *)&client_addr, &client_addr_len) < 0)
    perror("recvfrom()");

  close(sock_client);

  char * m_ptr = strstr(buf_recv, "FreeMemory");
  m_ptr  = m_ptr + 12;
  strncpy(memory_usage, m_ptr, 5);
  memory_usage[5] = '\0';

  printf("Memory usage: %s\n", memory_usage);
}

static int currently_drawing = 0;
//do_draw will be executed in a separate thread whenever we would like to update
//our animation
void *do_draw(void *ptr)
{
  struct addr * addr_to_draw = (struct addr *)ptr;
 //prepare to trap our SIGALRM so we can draw when we recieve it!
  siginfo_t info;
  sigset_t sigset;

  sigemptyset(&sigset);
  sigaddset(&sigset, SIGALRM);

  while(1){
      //wait for our SIGALRM.  Upon receipt, draw our stuff.  Then, do it again!
      while (sigwaitinfo(&sigset, &info) > 0) {
        currently_drawing = 1;

    char * memory_usage = calloc(5, sizeof(char));
    get_machine_info(addr_to_draw->prefix_str, memory_usage);

    int width, height;
    gdk_threads_enter();
    gdk_drawable_get_size(pixmap, &width, &height);
    gdk_threads_leave();

    //create a gtk-independant surface to draw on
    cairo_surface_t *cst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t *cr = cairo_create(cst);

    cairo_paint(cr);
    cairo_set_source_rgb (cr, 1, 1, 1);
    cairo_save(cr);
    cairo_set_font_size (cr, 20);
    cairo_move_to (cr, 10, 20);
    cairo_show_text (cr, addr_to_draw->host);
    cairo_move_to (cr, 10, 40);
    cairo_show_text (cr, addr_to_draw->pid);
    cairo_move_to (cr, 50, 20);
    cairo_show_text (cr, memory_usage);
    cairo_restore(cr);

    cairo_destroy(cr);


    //When dealing with gdkPixmap's, we need to make sure not to
    //access them from outside gtk_main().
    gdk_threads_enter();

    cairo_t *cr_pixmap = gdk_cairo_create(pixmap);
    cairo_set_source_surface (cr_pixmap, cst, 0, 0);
    cairo_paint(cr_pixmap);
    cairo_destroy(cr_pixmap);

    gdk_threads_leave();

    cairo_surface_destroy(cst);

    currently_drawing = 0;
    }

  }
    return NULL;
}

gboolean timer_exe(gpointer s)
{
    struct modal_stack * wnd = (struct modal_stack *)s;

    static int first_time = 1;

    //use a safe function to get the value of currently_drawing so
    //we don't run into the usual multithreading issues
    int drawing_status = g_atomic_int_get(&currently_drawing);

    //if this is the first time, create the drawing thread
    static pthread_t thread_info;

    if(first_time == 1){
        int  iret;
        iret = pthread_create( &thread_info, NULL, do_draw, wnd->cur_addr);
    }

    //if we are not currently drawing anything, send a SIGALRM signal
    //to our thread and tell it to update our pixmap
    if(drawing_status == 0){
        pthread_kill(thread_info, SIGALRM);
    }

    //tell our window it is time to draw our animation.
    int width, height;
    gdk_drawable_get_size(pixmap, &width, &height);
    gtk_widget_queue_draw_area(wnd->window, 0, 0, width, height);

    first_time = 0;


    return TRUE;

}

gboolean on_window_configure_event(GtkWidget * da, GdkEventConfigure * event, gpointer user_data)
{
    static int oldw = 0;
    static int oldh = 0;
    //make our selves a properly sized pixmap if our window has been resized
    if (oldw != event->width || oldh != event->height){
        //create our new pixmap with the correct size.
        GdkPixmap *tmppixmap = gdk_pixmap_new(da->window, event->width,  event->height, -1);
        //copy the contents of the old pixmap to the new pixmap.  This keeps ugly uninitialized
        //pixmaps from being painted upon resize
        int minw = oldw, minh = oldh;
        if( event->width < minw ){ minw =  event->width; }
        if( event->height < minh ){ minh =  event->height; }
        gdk_draw_drawable(tmppixmap, da->style->fg_gc[GTK_WIDGET_STATE(da)], pixmap, 0, 0, 0, 0, minw, minh);
        //we're done with our old pixmap, so we can get rid of it and replace it with our properly-sized one.
        g_object_unref(pixmap); 
        pixmap = tmppixmap;
    }
    oldw = event->width;
    oldh = event->height;
    return TRUE;
}

gboolean on_window_expose_event(GtkWidget * da, GdkEventExpose * event, gpointer user_data)
{
    gdk_draw_drawable(da->window,
        da->style->fg_gc[GTK_WIDGET_STATE(da)], pixmap,
        // Only copy the area that was exposed.
        event->area.x, event->area.y,
        event->area.x, event->area.y,
        event->area.width, event->area.height);
    return TRUE;
}

void create_window(struct modal_stack s)
{
    //Block SIGALRM in the main thread
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGALRM);
    pthread_sigmask(SIG_BLOCK, &sigset, NULL);

    s.group = gtk_window_group_new();
    s.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    gtk_widget_set_usize(s.window, 200, 200);
    g_signal_connect(G_OBJECT (s.window), "destroy",
        G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(G_OBJECT (s.window), "delete_event",
        G_CALLBACK(gtk_main_quit), NULL);

    g_signal_connect(G_OBJECT(s.window), "expose_event", G_CALLBACK(on_window_expose_event), NULL);
    g_signal_connect(G_OBJECT(s.window), "configure_event", G_CALLBACK(on_window_configure_event), NULL);

    gtk_widget_show(s.window);

    gtk_window_group_add_window(s.group, GTK_WINDOW(s.window));

    //set up our pixmap so it is ready for drawing
    pixmap = gdk_pixmap_new(s.window->window,500,500,-1);
    //because we will be painting our pixmap manually during expose events
    //we can turn off gtk's automatic painting and double buffering routines.
    gtk_widget_set_app_paintable(s.window, TRUE);
    gtk_widget_set_double_buffered(s.window, FALSE);

    (void)g_timeout_add(3333, (GSourceFunc)timer_exe, &s);
}

void * 
display_window(void * addr)
{
    struct addr * local_addr_list = (struct addr *) addr;
    struct modal_stack wnd;
    wnd.cur_addr = addr;
    create_window(wnd);
    gtk_main();
}
