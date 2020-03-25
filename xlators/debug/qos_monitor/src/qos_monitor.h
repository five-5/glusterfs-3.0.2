#ifndef _QOS_MONITOR_H
#define _QOS_MONITOR_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "xlator.h"
#include "defaults.h"
#include "list.h"
#include "dict.h"

#include <errno.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>

#include <hiredis/hiredis.h> 

// default option value 
#define HOST  "10.10.1.13"
#define PORT 6379
#define CHANNEL "monitor"
#define INTERNAL 10
// get client_id related macros
#define CDELIMER "-"
#define CLIENTID 60
#define TIMES 3
#define KB 1024
#define MB 1024*1024
#define MSG_LENTH 300
#define DELIMER "^^"
/* changed from struct _server_connection
* used for identity client
 */
struct struct_client_id {
	struct list_head    list;
	char               *id;
	int                 ref;
	int                 active_transports;
	pthread_mutex_t     lock;
	char                disconnected;
	fdtable_t          *fdtable; 
	struct _lock_table *ltable;
	xlator_t           *bound_xl;
};

typedef struct struct_client_id client_id_t;

typedef struct CRedisPublisher {
    redisContext *_redis_context; // hiredis 
	char *redis_host;   
	int redis_port;
	char *channel;
}CRedisPublisher;

struct qos_local {
	struct timeval wind_at;
	struct timeval unwind_at;
	double value;
};

struct qos_monitor_data {
	double data_written;
	double data_read;
	struct qos_local write_delay;
	struct qos_local read_delay;
	double data_iops;
	struct timeval started_at;
};

struct qos_monitor_private {
        gf_lock_t lock;
		dict_t *metrics;
		CRedisPublisher *publisher;
		int32_t qos_monitor_interval;
		pthread_t monitor_thread;
		int monitor_thread_should_die;
		int monitor_thread_running;
};
typedef struct qos_monitor_private qos_monitor_private_t;

int redis_disconnect(void *pthis);
int redis_connect(void *pthis);
int publish(const char *channel_name, const char *message, void *pthis);

#endif