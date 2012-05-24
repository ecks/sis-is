#ifndef _REMOTE_SPAWN_H
#define _REMOTE_SPAWN_H

#define REMOTE_SPAWN_PORT 50000

// Requests
#define REMOTE_SPAWN_REQ_START									0
//#define REMOTE_SPAWN_REQ_STOP										1	// Probably not needed, we won't know which one to stop if multiple processes exist
//#define REMOTE_SPAWN_REQ_RESTART								2 // Probably not needed, we won't know which one to stop if multiple processes exist

// Responses
#define REMOTE_SPAWN_RESP_OK										0
#define REMOTE_SPAWN_RESP_INVALID_REQUEST				1
#define REMOTE_SPAWN_RESP_INVALID_PROCESS_TYPE	2
#define REMOTE_SPAWN_RESP_NOT_SPAWNABLE					3
#define REMOTE_SPAWN_RESP_NOT_IMPLEMENTED				4
#define REMOTE_SPAWN_RESP_SPAWN_FAILED					5

#endif