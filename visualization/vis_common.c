#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vis_common.h"

struct addr * parse_ip_address(char prefix_str[])
{
  char * str;
  char * pch;

  struct addr * new_addr = calloc(1, sizeof(struct addr));

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

void dump_rib(struct list * addr_list)
{
  struct listnode * node;
  LIST_FOREACH(addr_list, node)
  {
    struct addr * addr_to_print = (struct addr *)node->data;
    printf("Addr: %s\n", addr_to_print->prefix_str);
  }
}


// rib_list is freeded
struct list * extract_host_from_rib(struct list * rib_list, struct addr * addr_to_check)
{
  struct listnode * rib_node;
  struct list * rib_filt_list = calloc(1, sizeof(struct list)); 

  LIST_FOREACH(rib_list,rib_node)
  {
    struct addr * rib_addr = (struct addr *)rib_node->data;
    if((strcmp(rib_addr->type, "1") != 0) && (strcmp(rib_addr->host, addr_to_check->host) == 0))
    {
      struct addr * rib_filt_addr = calloc(1, sizeof(struct addr));
      memcpy(rib_filt_addr, rib_addr, sizeof(struct addr));
      struct listnode * rib_filt_node = calloc(1, sizeof(struct listnode));
      rib_filt_node->data = rib_filt_addr;
      LIST_APPEND(rib_filt_list, rib_filt_node);
    }
  }

  FREE_LINKED_LIST( rib_list );

  return rib_filt_list;
}

struct list * get_rib_routes()
{
  sisis_dump_kernel_routes();
  struct listnode * node;
  struct listnode * appnd_node;
  struct list * addr_list = calloc(1, sizeof(struct list));

  LIST_FOREACH(ipv4_rib_routes, node)
  {
    struct route_ipv4 * route = (struct route_ipv4 *)node->data;
    struct addr * cur_addr;
                        
    // Set up prefix
    char prefix_str[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &(route->p->prefix.s_addr), prefix_str, INET_ADDRSTRLEN) != 1)
    {
      cur_addr = parse_ip_address(prefix_str);
      if(strcmp(cur_addr->pre, "26") == 0)
      {
        appnd_node = calloc(1, sizeof(struct listnode));
        appnd_node->data = (void *) cur_addr;
        LIST_APPEND(addr_list, appnd_node);
      }
      else
      {
        free(cur_addr);
        cur_addr = NULL;
      }
    }
  }

  return addr_list;
}
