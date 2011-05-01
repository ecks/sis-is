#include <zebra.h>

#include "prefix.h"
#include "table.h"
#include "memory.h"
#include "log.h"

#include "rib-comparison/r_lsa.h"
#include "rib-comparison/r_lsdb.h"

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

#define NETSNMP_PARSE_ARGS_SUCCESS       0
#define NETSNMP_PARSE_ARGS_SUCCESS_EXIT  -2
#define NETSNMP_PARSE_ARGS_ERROR_USAGE   -1
#define NETSNMP_PARSE_ARGS_ERROR         -3

#define NETSNMP_DS_WALK_INCLUDE_REQUESTED               1
#define NETSNMP_DS_WALK_PRINT_STATISTICS                2
#define NETSNMP_DS_WALK_DONT_CHECK_LEXICOGRAPHIC        3
#define NETSNMP_DS_WALK_TIME_RESULTS                    4
#define NETSNMP_DS_WALK_DONT_GET_REQUESTED              5

oid objid_id_mib[] =   { 1, 3, 6, 1, 2, 1, 14, 12, 1, 2};
oid objid_mask_mib[] = { 1, 3, 6, 1, 2, 1, 14, 12, 1, 8};

int numprinted = 0;

char * end_name = NULL;
struct thread_master * master;

static void
usage(void)
{
  fprintf(stderr, "USAGE: rc ");
}

static void
optProc(int argc, char *const *argv, int opt)
{

}

void
show_r_routes(struct route_table * rt)
{
  struct route_node * rn;
  
  for(rn = route_top(rt); rn; rn = route_next(rn))
  {
    if(rn->info) 
      printf("Not null: %s/%d\n", inet_ntoa(rn->p.u.prefix4), rn->p.prefixlen);
    else
      printf("Not null: %s/%d\n", inet_ntoa(rn->p.u.prefix4), rn->p.prefixlen);
  }
}

void
snmp_get_and_print(netsnmp_session * ss, oid * theoid, size_t theoid_len){
    netsnmp_pdu *pdu, *response;
    netsnmp_variable_list *vars;    
    int status;
    pdu = snmp_pdu_create(SNMP_MSG_GET);
    snmp_add_null_var(pdu, theoid, theoid_len);

    status = snmp_synch_response(ss, pdu, &response);
    if (status == STAT_SUCCESS && response->errstat == SNMP_ERR_NOERROR) {
        for (vars = response->variables; vars; vars = vars->next_variable) {
            numprinted++;
            print_variable(vars->name, vars->name_length, vars);
        }
    }
    if (response) {
        snmp_free_pdu(response);
    }
}

int
main(int argc, char * argv[])
{
  int arg;

  netsnmp_session session, *ss;
  netsnmp_pdu *id_pdu, *id_response;
  netsnmp_pdu *mask_pdu, *mask_response;
  netsnmp_variable_list *id_vars;
  netsnmp_variable_list *mask_vars;
  oid id_root[MAX_OID_LEN];
  oid mask_root[MAX_OID_LEN];
  oid end_id_oid[MAX_OID_LEN];
  oid end_mask_oid[MAX_OID_LEN];
  size_t end_id_len = 0;
  size_t end_mask_len = 0;
  size_t id_rootlen;
  size_t mask_rootlen;
  int exitval = 0;
  oid id_name[MAX_OID_LEN];
  oid mask_name[MAX_OID_LEN];
  size_t id_name_length;
  size_t mask_name_length;
  int count;
  int id_status;
  int mask_status;
  int running;
  int check;
  int id_buf_len = 256;
  int mask_buf_len = 256;
  char id_buf[id_buf_len];
  char mask_buf[mask_buf_len];
  struct r_lsdb * table;
  struct r_lsa * lsa;
  struct r_as_external_lsa * lsa_ex;
  /*
   * get the common command line arguments 
   */
  switch (arg = snmp_parse_args(argc, argv, &session, "C:", optProc)) {
  case NETSNMP_PARSE_ARGS_ERROR:
        exit(1);
  case NETSNMP_PARSE_ARGS_SUCCESS_EXIT:
        exit(0);
  case NETSNMP_PARSE_ARGS_ERROR_USAGE:
    usage();
    exit(1);
  default:
    break;
  }

  /*
   * get the initial object and subtree 
   */
  if (arg < argc) {
    /*
     * specified on the command line 
     */
    id_rootlen = MAX_OID_LEN;
    if (snmp_parse_oid(argv[arg], id_root, &id_rootlen) == NULL) {
      snmp_perror(argv[arg]);
      exit(1);
    }
  } 
  else {
        /*
         * use default value 
         */
        memmove(id_root, objid_id_mib, sizeof(objid_id_mib));
        id_rootlen = sizeof(objid_id_mib) / sizeof(oid);
        memmove(mask_root, objid_mask_mib, sizeof(objid_id_mib));
        mask_rootlen = sizeof(objid_mask_mib) / sizeof(oid);
  }

  /*
   * If we've been given an explicit end point,
   *  then convert this to an OID, otherwise
   *  move to the next sibling of the start.
   */
//  if ( end_name ) {
//    end_len = MAX_OID_LEN;
//    if (snmp_parse_oid(end_name, end_oid, &end_len) == NULL) {
//      snmp_perror(end_name);
//      exit(1);
//    }
//  } 
//  else {
    memmove(end_id_oid, id_root, id_rootlen*sizeof(oid));
    end_id_len = id_rootlen;        end_id_oid[end_id_len-1]++;
    memmove(end_mask_oid, mask_root, mask_rootlen*sizeof(oid));
    end_mask_len = mask_rootlen;        end_mask_oid[end_mask_len-1]++;
//  }

  SOCK_STARTUP;

  /*
   * open an SNMP session 
   */
  ss = snmp_open(&session);
  if (ss == NULL) {
    /*
     * diagnose snmp_open errors with the input netsnmp_session pointer 
     */
    snmp_sess_perror("snmpwalk", &session);
    SOCK_CLEANUP;
    exit(1);
  }

  /*
   * get first object to start walk 
   */
  memmove(id_name, id_root, id_rootlen * sizeof(oid));
  id_name_length = id_rootlen;
  memmove(mask_name, mask_root, mask_rootlen * sizeof(oid));
  mask_name_length = mask_rootlen;

  running = 1;

  check =
        !netsnmp_ds_get_boolean(NETSNMP_DS_APPLICATION_ID,
                        NETSNMP_DS_WALK_DONT_CHECK_LEXICOGRAPHIC);

//  if (netsnmp_ds_get_boolean(NETSNMP_DS_APPLICATION_ID, NETSNMP_DS_WALK_INCLUDE_REQUESTED)) {
//    snmp_get_and_print(ss, root, rootlen);
//  }

  table = r_lsdb_new();

  while(running)
  {
    /*
     * create PDU for GETNEXT request and add object name to request 
     */
    id_pdu = snmp_pdu_create(SNMP_MSG_GETNEXT);
    snmp_add_null_var(id_pdu, id_name, id_name_length);

    mask_pdu = snmp_pdu_create(SNMP_MSG_GETNEXT);
    snmp_add_null_var(mask_pdu, mask_name, mask_name_length);

    /*
     * do the request 
     */
    mask_status = snmp_synch_response(ss, mask_pdu, &mask_response);
    id_status = snmp_synch_response(ss, id_pdu, &id_response);

    if (id_status == STAT_SUCCESS && mask_status == STAT_SUCCESS) {
//    if (mask_status == STAT_SUCCESS) {
      if (id_response->errstat == SNMP_ERR_NOERROR && mask_response->errstat == SNMP_ERR_NOERROR) {
//      if (mask_response->errstat == SNMP_ERR_NOERROR) {
        /*
         * check resulting variables 
         */
//        for (id_vars = id_response->variables, mask_vars = mask_response->variables; id_vars && mask_vars; id_vars = id_vars->next_variable, mask_vars = mask_vars->next_variable) {
        mask_vars = mask_response->variables;
        id_vars = id_response->variables;
        while(id_vars && mask_vars) {
          if ((snmp_oid_compare(end_id_oid, end_id_len, id_vars->name, id_vars->name_length) <= 0) ||
              (snmp_oid_compare(end_mask_oid, end_mask_len, mask_vars->name, mask_vars->name_length) <= 0)) {
//          if (snmp_oid_compare(end_id_oid, end_id_len, id_vars->name, id_vars->name_length) <= 0) {
            /*
             * not part of this subtree 
             */
            running = 0;
            id_vars = id_vars->next_variable;
            mask_vars = mask_vars->next_variable;
            continue;
          }
          numprinted++;
          snprint_variable(id_buf, id_buf_len,
                           id_vars->name, id_vars->name_length, id_vars);
          snprint_variable(mask_buf, mask_buf_len,
                           mask_vars->name, mask_vars->name_length, mask_vars);

//          printf("ID: %s\n", id_buf); 
//          printf("MASK: %s\n", mask_buf); 

          lsa = r_lsa_new();
          lsa->data = r_lsa_data_new(1);
          lsa->lsdb = table;
          inet_pton(AF_INET, id_buf, &(lsa->data->id));   
          lsa_ex = (struct r_as_external_lsa *)lsa->data;
          inet_pton(AF_INET, mask_buf, &(lsa_ex->mask));
          r_lsdb_add(table, lsa);

          printf("Being lsdb\n");
          show_r_routes(table->db);
          printf("End lsdb\n");

          if ((id_vars->type != SNMP_ENDOFMIBVIEW) &&
              (id_vars->type != SNMP_NOSUCHOBJECT) &&
              (id_vars->type != SNMP_NOSUCHINSTANCE)) {
            /*
             * not an exception value 
             */
            if (check &&
                snmp_oid_compare(id_name, id_name_length,
                                 id_vars->name,
                                 id_vars->name_length) >= 0) {
              fprintf(stderr, "Error: OID not increasing: ");
              fprint_objid(stderr, id_name, id_name_length);
              fprintf(stderr, " >= ");
              fprint_objid(stderr, id_vars->name,
                                   id_vars->name_length);
              fprintf(stderr, "\n");
              running = 0;
              exitval = 1;
            }
            memmove((char *) id_name, (char *) id_vars->name,
                     id_vars->name_length * sizeof(oid));
            id_name_length = id_vars->name_length;
          }  
          else
            /*
             * an exception value, so stop 
             */
            running = 0;
          
          id_vars = id_vars->next_variable;
          mask_vars = mask_vars->next_variable;
        }
      }
      else {
        /*
         * error in response, print it 
         */
        running = 0;
        if (id_response->errstat == SNMP_ERR_NOSUCHNAME) {
          printf("End of MIB\n");
        } 
        else {
          fprintf(stderr, "Error in packet.\nReason: %s\n",
                          snmp_errstring(id_response->errstat));
          if (id_response->errindex != 0) {
            fprintf(stderr, "Failed object: ");
            for (count = 1, id_vars = id_response->variables;
                 id_vars && count != id_response->errindex;
                 id_vars = id_vars->next_variable, count++)
              /*EMPTY*/;
            if (id_vars)
              fprint_objid(stderr, id_vars->name,
                                   id_vars->name_length);
            fprintf(stderr, "\n");
          }
          exitval = 2;
        }
      }
    }
    else if (id_status == STAT_TIMEOUT) {
      fprintf(stderr, "Timeout: No Response from %s\n",
                      session.peername);
      running = 0;
      exitval = 1;
    }
    else {                /* status == STAT_ERROR */
      snmp_sess_perror("snmpwalk", ss);
      running = 0;
      exitval = 1;
    }
    if (id_response)
      snmp_free_pdu(id_response);
  }

//  show_r_routes(table->db);

  if (numprinted == 0 && id_status == STAT_SUCCESS) {
    /*
     * no printed successful results, which may mean we were
     * pointed at an only existing instance.  Attempt a GET, just
     * for get measure. 
     */
//    if (!netsnmp_ds_get_boolean(NETSNMP_DS_APPLICATION_ID, NETSNMP_DS_WALK_DONT_GET_REQUESTED)) {
//      snmp_get_and_print(ss, root, rootlen);
//    }
  }
  snmp_close(ss);

  SOCK_CLEANUP;
  return exitval;
}
