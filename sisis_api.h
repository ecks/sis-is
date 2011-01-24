/*
 * SIS-IS Test program.
 * Stephen Sigwart
 * University of Delaware
 */

#ifndef SISIS_API_H
#define SISIS_API_H

#define SISIS_VERSION 1

// SIS-IS Commands
#define SISIS_CMD_REGISTER_ADDRESS				1
#define SISIS_CMD_UNREGISTER_ADDRESS			2

extern int sisis_listener_port;
extern char * sisis_listener_ip_addr;

/**
 * Construct SIS-IS address.
 *
 * sisis_addr String to store resulting SIS-IS/IP address in.
 * 
 * Returns zero on success.
 */
int sisis_create_addr(unsigned int ptype, unsigned int host_num, unsigned int pid, char * sisis_addr);

/**
 * Registers SIS-IS process.
 *
 * sisis_addr String to store resulting SIS-IS/IP address in.
 * 
 * Returns zero on success.
 */
int sisis_register(unsigned int ptype, unsigned int host_num, unsigned int pid, char * sisis_addr);

/**
 * Unregisters SIS-IS process.
 * Returns zero on success.
 */
int sisis_unregister(unsigned int ptype, unsigned int host_num, unsigned int pid);

#endif // SISIS_API_H