#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vis_window.h"
#include "vis_main.h"

#define MAXLINE 30

void send_addr(struct list * addr_list, int pfds[])
{
  struct listnode * addr_node;
  LIST_FOREACH(addr_list, addr_node)
  {
    struct addr * cur_addr = (struct addr *)addr_node->data;
    if(strcmp(cur_addr->type, "1") == 0)
    {
      // strlen(str)+1 so that we may send the null-terminating char as well
      write(pfds[1], cur_addr->prefix_str, strlen(cur_addr->prefix_str)+1);
      printf("%s\n", cur_addr->prefix_str);

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
  int pfds[2];
  char buf[MAXLINE];
  ssize_t n;
  struct addr * cur_addr;

  pipe(pfds);

  pid_t pid = fork();

  if(pid > 0)
  {
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

  send_addr(addr_list, pfds);
  }

  if(pid == 0)
  {
    int i = 0;
    while(n = read(pfds[0], buf, MAXLINE) > 0)
    {
      // since memory is shared need to allocate new string before sending it
      struct addr * addr_to_send = parse_ip_address(buf);
//      char * str_to_send = malloc(sizeof(char) * MAXLINE);
//      printf("Outside thread: %s\n", buf);
//      strcpy(str_to_send, buf);
      pthread_create(&window_registration_thread, NULL, display_window, (void *)addr_to_send);
    }
  }
}
