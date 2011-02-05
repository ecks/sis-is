#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include "vis_window.h"
#include "vis_main.h"

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
      struct list * addr_list_procs = malloc(sizeof(struct list));

      LIST_FOREACH(addr_list, local_addr_node)
      {
        struct addr * local_cur_addr = (struct addr *)local_addr_node->data;
        if((strcmp(local_cur_addr->type, "1") != 0) && (strcmp(local_cur_addr->host, host) == 0))
        {
          printf("%s\n", local_cur_addr->prefix_str);
          struct listnode * node_to_be_added = malloc(sizeof(struct listnode));
          struct addr * addr_to_be_added = malloc(sizeof(struct addr));
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

struct addr * parse_ip_address(char prefix_str[])
{
  char * str;
  char * pch;

  struct addr * new_addr = malloc(sizeof(struct addr));

  strcpy(new_addr->prefix_str, prefix_str);
  
  str = prefix_str;
  int i;
  for(i = 0; i < 3; i++)
  {
    pch = strchr(str, '.');
    if (i == 0)
    {
      strncpy(new_addr->pre, str, pch-str+1);
      new_addr->pre[pch-str] = '\0';
    }
    else if(i == 1)
    {
      strncpy(new_addr->type, str, pch-str+1);
      new_addr->type[pch-str] = '\0';
    }
    else 
    {
      strncpy(new_addr->host, str, pch-str+1);
      new_addr->host[pch-str] = '\0';

    }

    str = pch+1;
  }

  // process the end
  strcpy(new_addr->pid, str);

  return new_addr;
}

int main(int argc, char * argv[])
{
  char buf[INET_ADDRSTRLEN];
  ssize_t n;
  struct addr * cur_addr;

  sisis_dump_kernel_routes();
  struct listnode * node;
  struct listnode * appnd_node;
  struct list * addr_list = malloc(sizeof(struct list));
  LIST_FOREACH(ipv4_rib_routes, node)
  {
    struct route_ipv4 * route = (struct route_ipv4 *)node->data;
                        
    // Set up prefix
    char prefix_str[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &(route->p->prefix.s_addr), prefix_str, INET_ADDRSTRLEN) != 1)
    {
      cur_addr = parse_ip_address(prefix_str);
      if(strcmp(cur_addr->pre, "26") == 0)
      {
        appnd_node = malloc(sizeof(struct listnode));
        appnd_node->data = (void *) cur_addr;
        LIST_APPEND(addr_list, appnd_node);
      }
      else
      {
        free(cur_addr);
      }
    }
  }

  display_windows(addr_list, argc, argv);
  wait(NULL);
  exit(0);
}
