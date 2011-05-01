#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

 /* change the word "define" to "undef" to try the (insecure) SNMPv1 version */
 #undef DEMO_USE_SNMP_VERSION_3
 
 #ifdef DEMO_USE_SNMP_VERSION_3
 #include "net-snmp/transform_oids.h"
 const char *our_v3_passphrase = "The Net-SNMP Demo Password";
 #endif

main()
{
  struct snmp_session session, *ss;
  struct snmp_pdu *pdu;
  struct snmp_pdu *response;
           
  oid anOID[MAX_OID_LEN];
  size_t anOID_len = MAX_OID_LEN;
   
  struct variable_list *vars;
  int status;

  /*
   * Initialize the SNMP library
   */
  init_snmp("snmpapp");

  /*
   * Initialize a "session" that defines who we're going to talk to
   */
  snmp_sess_init( &session );                   /* set up defaults */
  session.peername = "10.100.1.21";

  /* set the SNMP version number */
  session.version = SNMP_VERSION_1;
   
  /* set the SNMPv1 community name used for authentication */
  session.community = "public";
  session.community_len = strlen(session.community);

  /*
   * Open the session
   */
  ss = snmp_open(&session);                     /* establish the session */
  if (!ss) {
      snmp_perror("ack");
      snmp_log(LOG_ERR, "something horrible happened!!!\n");
      exit(2);
  }

  /*
   * Create the PDU for the data for our request.
   *   1) We're going to GET the system.sysDescr.0 node.
   */
  pdu = snmp_pdu_create(SNMP_MSG_GET);

  anOID_len = MAX_OID_LEN;
  if (!snmp_parse_oid(".1.3.6.1.2.1.14.1.1.0", anOID, &anOID_len)) {
      snmp_perror(".1.3.6.1.2.1.14.1.1.0");
      SOCK_CLEANUP;
      exit(1);
    } 
 
  snmp_add_null_var(pdu, anOID, anOID_len);

  /*
   * Send the Request out.
   */
  status = snmp_synch_response(ss, pdu, &response);

  /*
   * Process the response.
   */
  if (status == STAT_SUCCESS && response->errstat == SNMP_ERR_NOERROR) {
    /*
     * SUCCESS: Print the result variables
     */
  
    for(vars = response->variables; vars; vars = vars->next_variable)
       print_variable(vars->name, vars->name_length, vars);
  }

  /*
   * Clean up:
   *  1) free the response.
   *  2) close the session.
   */
  if (response)
    snmp_free_pdu(response);
  snmp_close(ss);
    
 } /* main() */  
