#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vis_main.h"

void display_windows_for_hosts(struct list * addr_list)
{
  struct listnode * addr_node;
  LIST_FOREACH(addr_list, addr_node)
  {
    struct addr * addr = (struct addr *)addr_node->data;
    if(strcmp(addr->type, "1") == 0)
    {
      printf("%s\n", addr->prefix_str);
      display_window(addr);
    }
  }
}

void parse_ip_address(char prefix_str[], struct list * addr_list )
{
  char * str;
  char * pch;

  struct addr * new_addr = malloc(sizeof(struct addr));
  memset(new_addr, 0, sizeof(struct addr));

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

  if(strcmp(new_addr->pre, "26") == 0)
  {
    struct listnode * addr_node = malloc(sizeof(struct listnode));
    addr_node->data = (void *) new_addr;
    LIST_APPEND(addr_list, addr_node);
  }
  else
  {
    free(new_addr);
  }
}

int main(int argc, char ** argv)
{
  sisis_dump_kernel_routes();
  struct listnode * node;
  struct list * addr_list = malloc(sizeof(struct list));
  LIST_FOREACH(ipv4_rib_routes, node)
  {
    struct route_ipv4 * route = (struct route_ipv4 *)node->data;
                        
    // Set up prefix
    char prefix_str[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &(route->p->prefix.s_addr), prefix_str, INET_ADDRSTRLEN) != 1)
    {
        parse_ip_address(prefix_str, addr_list);
    }
  }
  display_windows_for_hosts(addr_list);

  exit(0);
}
