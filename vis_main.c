#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include "vis_window.h"
#include "vis_common.h"

#define MAXLINE 30

void display_windows(struct list * addr_list, int argc, char * argv[])
{
  struct listnode * addr_node;
  struct listnode * local_addr_node;

  LIST_FOREACH(addr_list, addr_node)
  {
    struct addr * cur_addr = (struct addr *)addr_node->data;
    if(strcmp(cur_addr->type, "1") == 0)
    {
      char * host = cur_addr->host;
      struct list * addr_list_procs = calloc(1, sizeof(struct list));

      LIST_FOREACH(addr_list, local_addr_node)
      {
        struct addr * local_cur_addr = (struct addr *)local_addr_node->data;
        if((strcmp(local_cur_addr->type, "1") != 0) && (strcmp(local_cur_addr->host, host) == 0))
        {
          printf("%s\n", local_cur_addr->prefix_str);
          struct listnode * node_to_be_added = calloc(1, sizeof(struct listnode));
          struct addr * addr_to_be_added = calloc(1, sizeof(struct addr));
          node_to_be_added->data = (void *)addr_to_be_added;

          memcpy(addr_to_be_added, local_cur_addr, sizeof(struct addr));
          printf("memcpied %s\n", addr_to_be_added->prefix_str);
          LIST_APPEND(addr_list_procs, node_to_be_added);
        }
      }

      LIST_FOREACH(addr_list_procs, local_addr_node)
      {
        struct addr * local_cur_addr = (struct addr *)local_addr_node->data;
        printf("address copied: %s\n", local_cur_addr->prefix_str);
      }

      // create new child for each window
      pid_t pid = fork();

      if(pid == 0)
      {

        if (!g_thread_supported ()){ g_thread_init(NULL); }
        gdk_threads_init();
        gdk_threads_enter();

        gtk_init(&argc, &argv);
        display_window(cur_addr, addr_list_procs);
        gdk_threads_leave();
        // make sure child doesn't fork again
        _exit(0);
      }
    }
  }
}

int main(int argc, char * argv[])
{
  struct list * addr_list = get_rib_routes();

  display_windows(addr_list, argc, argv);
  wait(NULL);
  exit(0);
}
