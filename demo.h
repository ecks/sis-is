#ifndef DEMO_H
#define DEMO_H

#define SHIM_PORT 50000
#define SORT_PORT 50000
#define JOIN_PORT 50000
#define VOTER_PORT 50000

#define RECV_BUFFER_SIZE 65536
#define SEND_BUFFER_SIZE 65536

#define GATHER_RESULTS_TIMEOUT_USEC		100000	// 100ms

// From time.h
#ifndef timersub
#define timersub(a, b, result)						      \
  do {									      \
    (result)->tv_sec = (a)->tv_sec - (b)->tv_sec;			      \
    (result)->tv_usec = (a)->tv_usec - (b)->tv_usec;			      \
    if ((result)->tv_usec < 0) {					      \
      --(result)->tv_sec;						      \
      (result)->tv_usec += 1000000;					      \
    }									      \
  } while (0)
#endif

#define MAX_TABLE_SIZE 50

#define REDUNDANCY_PERCENTAGE 20
#define MIN_REDUNDANCY 3

#endif
