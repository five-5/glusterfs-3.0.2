/*
  Copyright (c) 2006-2009 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

/**
 * xlators/debug/qos_monitor :
 *    test for publish
 */

#include "qos_monitor.h"
#include <stdio.h>
#include <sys/types.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>

/* return the difference of begin and end by second*/
double time_difference(struct timeval *begin, struct timeval *end)
{
	double duration = 0;
	if (begin == NULL || end == NULL)
		return duration;
	duration = (end->tv_sec - begin->tv_sec) + ((end->tv_usec - begin->tv_usec) / 1000000);
	return duration;
}

/* return the index of n-th pos that f appears in str*/
int find_str_n(char *str, char *f, int n)
{
    int i;
    char *tmp;
    int len = strlen(str);

    // check
    if (str == NULL || f == NULL || n > len || n < 0)
    {
        return -1;
    }

    tmp = strstr(str, f);
    for (i = 1; i < n && tmp != NULL; ++i)
    {
        tmp = strstr(tmp + strlen(f), f);
    }

    if (i != n)
        return -1;
    else
        return len - strlen(tmp);
}

/* according to current client_id_t info to get client_id
 * TODO: maybe should modify with the map relation with client and application
*/
void get_client_id(char *client, char *client_id)
{
    int len = find_str_n(client, DELIMER, TIMES);
	// 未找到
	if (len == -1) {
		strncpy(client_id, client, strlen(client));
	} else {
		strncpy(client_id, client, len);
		client_id[len] = '\0';
	}
}

/* hiredis 相关函数*/
// 事件分发线程函数
void *event_proc(void *pthis)
{
    CRedisPublisher *p = (CRedisPublisher *)pthis;
    sem_wait(&p->_event_sem);

	// 开启事件分发，event_base_dispatch会阻塞
    event_base_dispatch(p->_event_base);

    return NULL;
}

void *event_thread(void *data)
{
    if (NULL == data)
    {
        gf_log("monitor", GF_LOG_ERROR,
               "even_thread Error!\n");
        return NULL;
    }

    return event_proc(data);
}

void pubCallback(redisAsyncContext *c, void *r, void *priv)
{
    redisReply *reply = (redisReply *)r;
    if (reply == NULL) return;
	gf_log("monitor", GF_LOG_ERROR,
               "[pub_cbk] %s: %d\n", (char*)priv, reply->integer);
}

void connectCallback(const redisAsyncContext *c, int status)
{
    if (status != REDIS_OK) {
		gf_log("monitor", GF_LOG_ERROR,
               "Error: %s\n", c->errstr);
        return;
    }
	gf_log("monitor", GF_LOG_ERROR,
               "Connected...\n");
}

void disconnectCallback(const redisAsyncContext *c, int status)
{
    if (status != REDIS_OK) {
		gf_log("monitor", GF_LOG_ERROR,
               "Error: %s\n", c->errstr);
        return;
    }
    gf_log("monitor", GF_LOG_ERROR,
               "Disconnected...\n");
}


/* CRedisPublisher */
int redis_init(void *pthis)
{
    CRedisPublisher *p = (CRedisPublisher *)pthis;
    // initialize the event
    p->_event_base = event_base_new();
    if (NULL == p->_event_base)
    {
		gf_log("monitor", GF_LOG_ERROR,
               "Create redis event failed.\n");
        return 0;
    }

    memset(&p->_event_sem, 0, sizeof(p->_event_sem));
    int ret = sem_init(&p->_event_sem, 0, 0);
    if (ret != 0)
    {
		gf_log("monitor", GF_LOG_ERROR,
               "Init sem failed.\n");
        return 0;
    }

    return 1;
}

int redis_uninit(void *pthis)
{
    CRedisPublisher *p = (CRedisPublisher *)pthis;
	event_base_free(p->_event_base);
    p->_event_base = NULL;

    sem_destroy(&p->_event_sem);

	if (p->redis_host != NULL) {
		FREE(p->redis_host);
		p->redis_host = NULL;
	}
	if (p->channel != NULL) {
		FREE(p->channel);
		p->channel = NULL;
	}

    return 1;
}

int redis_disconnect(void *pthis)
{
    CRedisPublisher *p = (CRedisPublisher *)pthis;
    if (p->_redis_context)
    {
        redisAsyncDisconnect(p->_redis_context);
        p->_redis_context = NULL;
    }

    return 1;
}

int redis_connect(void *pthis)
{
    CRedisPublisher *p = (CRedisPublisher *)pthis;
    // connect redis
    p->_redis_context = redisAsyncConnect(p->redis_host, p->redis_port);    // 异步连接到redis服务器上，使用默认端口
    if (NULL == p->_redis_context)
    {
		gf_log("monitor", GF_LOG_ERROR,
               "Connect redis failed.\n");
        return 0;
    }

    if (p->_redis_context->err)
    {
		gf_log("monitor", GF_LOG_ERROR,
               "Connect redis error: %d, %s\n",
            p->_redis_context->err, p->_redis_context->errstr);    // 输出错误信息
        return 0;
    }

    // attach the event
    redisLibeventAttach(p->_redis_context, p->_event_base);    // 将事件绑定到redis context上，使设置给redis的回调跟事件关联

    // 创建事件处理线程
    int ret = pthread_create(&p->_event_thread, NULL, event_thread, (void *)p);
    if (ret != 0)
    {
		gf_log("monitor", GF_LOG_ERROR,
               "create event thread failed.\n");
        redis_disconnect(p);
        return 0;
    }

	// 设置连接回调，当异步调用连接后，服务器处理连接请求结束后调用，通知调用者连接的状态
    redisAsyncSetConnectCallback(p->_redis_context, &connectCallback);

	// 设置断开连接回调，当服务器断开连接后，通知调用者连接断开，调用者可以利用这个函数实现重连
    redisAsyncSetDisconnectCallback(p->_redis_context, &disconnectCallback);

	// 启动事件线程
    sem_post(&p->_event_sem);
    return 1;
}

int publish(const char *channel_name, const char *message, void *pthis)
{
    CRedisPublisher *p = (CRedisPublisher *)pthis;
    int ret = redisAsyncCommand(p->_redis_context,
        &pubCallback, p, "PUBLISH %s %s",
        channel_name, message);
    if (REDIS_ERR == ret)
    {
        gf_log("monitor", GF_LOG_ERROR,
               "Publish command failed: %d\n", ret);
        return 0;
    } else {
		gf_log("monitor", GF_LOG_ERROR,
               "publish %s %s\n", channel_name, message);
        return 1;
    }
}


char * get_server_ip()
{
	struct ifaddrs *ifAddrStruct = NULL;
    struct ifaddrs *ifa = NULL;
    void *tmpAddrPtr = NULL;
	char result[16] = "";

    getifaddrs(&ifAddrStruct);

    for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next)
	{
        if (!ifa->ifa_addr)
		{
            continue;
        }
        if (ifa->ifa_addr->sa_family == AF_INET) // check it is IP4
		{
            tmpAddrPtr=&((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
            char addressBuffer[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, tmpAddrPtr, addressBuffer, INET_ADDRSTRLEN);
			if (strcmp(addressBuffer, "127.0.0.1") && !strstr(addressBuffer, "10.")
				  &&  !strstr(addressBuffer, "172.") && !strstr(addressBuffer, "192.")) {
					  strcpy(result, addressBuffer);
					  break;
				  }

        }
		else if (ifa->ifa_addr->sa_family == AF_INET6) // check it is IP6
		{
            tmpAddrPtr=&((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr;
            char addressBuffer[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, tmpAddrPtr, addressBuffer, INET6_ADDRSTRLEN);
        }
    }
    if (ifAddrStruct!=NULL)
	{
		freeifaddrs(ifAddrStruct);
	}

	return result;
}


void *_qos_monitor_thread(xlator_t *this)
{
	qos_monitor_private_t *priv = NULL;
	int old_cancel_type;
	char message[120];
	struct timeval now;
    char *server_ip;
    int times = 1;
	int i = 0;

	priv = this->private;
	gf_log(this->name, GF_LOG_ERROR,
           "qos_monitor monitor thread started, "
           "polling IO stats every %d seconds",
           priv->qos_monitor_interval);

	while (1) {
		// gf_log(this->name, GF_LOG_ERROR, "qos_monitor: thread should_die: %d", priv->monitor_thread_should_die);
		if (priv->monitor_thread_should_die)
			break;

		(void)pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &old_cancel_type);
        gf_log(this->name, GF_LOG_ERROR, "sleep....");
		sleep(priv->qos_monitor_interval);
        (void)pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, &old_cancel_type);

		/* publish monitor metrics */
 
        gettimeofday(&now, NULL);
        server_ip = get_server_ip();

		for (i = 0; i < 5; ++i) {
			sprintf(message, "%s^^%ld^^%s^^%d", server_ip, now.tv_sec, "invoke_times", times);
			gf_log("monitor", GF_LOG_ERROR, "[%s]: %s", priv->publisher->channel, message);
			publish(priv->publisher->channel, message, priv->publisher);
			
			++times;
		}
        
	}

	priv->monitor_thread_running = 0;
	gf_log(this->name, GF_LOG_ERROR, "QoS_monitor monitor thread terminated");
    return NULL;
}

int _qos_destroy_monitor_thread(qos_monitor_private_t *priv)
{
	gf_log("q", GF_LOG_ERROR, "qos_destroy_monitor_thread invoked.");
    priv->monitor_thread_should_die = 1;
    if (priv->monitor_thread_running) {
        (void)pthread_cancel(priv->monitor_thread);
        (void)pthread_join(priv->monitor_thread, NULL);
    }
    return 0;
}

void qos_private_destroy(qos_monitor_private_t *priv)
{
	if (!priv)
		return;
	gf_log("sh", GF_LOG_ERROR, "qos_private_destroy invoked.");

	_qos_destroy_monitor_thread(priv);
	redis_disconnect(priv->publisher);
	redis_uninit(priv->publisher);

	LOCK_DESTROY (&priv->lock);
	if (priv->publisher)
	{
		if (priv->publisher->redis_host)
		{
			FREE(priv->publisher->redis_host);
			priv->publisher->redis_host = NULL;
		}
		if (priv->publisher->channel)
		{
			FREE(priv->publisher->channel);
			priv->publisher->channel = NULL;
		}
		FREE(priv->publisher);
		priv->publisher = NULL;
	}

	FREE (priv);
	gf_log("sh", GF_LOG_ERROR, "qos_private_destroy finished.");
}

int32_t
qos_monitor_writev_cbk (call_frame_t *frame,
                     void *cookie,
                     xlator_t *this,
                     int32_t op_ret,
                     int32_t op_errno,
                     struct stat *prebuf,
                     struct stat *postbuf)
{
		gf_log(this->name, GF_LOG_ERROR,
						   "start unwind.");

        STACK_UNWIND (frame, op_ret, op_errno, prebuf, postbuf);
		gf_log(this->name, GF_LOG_ERROR,
                   "end unwind.");
        return 0;
}

int32_t
qos_monitor_writev (call_frame_t *frame,
                 xlator_t *this,
                 fd_t *fd,
                 struct iovec *vector,
                 int32_t count,
                 off_t offset,
                 struct iobref *iobref)
{
		client_id_t *client = NULL;
        
		client = (client_id_t*) frame->root->trans;
        gf_log(this->name, GF_LOG_ERROR,
                   "client_id = %s.", client->id);
		gf_log(this->name, GF_LOG_ERROR,
                   "start wind.");
        STACK_WIND (frame,
                    qos_monitor_writev_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->writev,
                    fd,
                    vector,
                    count,
                    offset,
                    iobref);
		gf_log(this->name, GF_LOG_ERROR,
                   "end wind.");
        return 0;
}



int32_t
init (xlator_t *this)
{
        dict_t *options = NULL;
        char *includes = NULL, *excludes = NULL;
        qos_monitor_private_t *priv = NULL;
		int32_t interval;
		int ret = -1;
		char *redis_host;
		char *publish_channel;
		int32_t redis_port;

        if (!this)
                return -1;

        if (!this->children || this->children->next) {
                gf_log (this->name, GF_LOG_ERROR,
                        "qos_monitor translator requires one subvolume");
                return -1;
        }
        if (!this->parents) {
                gf_log (this->name, GF_LOG_WARNING,
                        "dangling volume. check volfile ");
        }

        priv = CALLOC (1, sizeof(*priv));
        ERR_ABORT (priv);

		priv->publisher = CALLOC (1, sizeof(*(priv->publisher)));
        ERR_ABORT (priv->publisher);

        options = this->options;
		interval = data_to_int32 (dict_get (options, "monitor-interval"));
		if (interval == -1)
			interval = INTERNAL;
		redis_host = data_to_str (dict_get (options, "redis-host"));
		publish_channel = data_to_str (dict_get (options, "publish-channel"));
		redis_port = data_to_int32 (dict_get (options, "redis-port"));
		if (redis_port == -1)
			redis_port = PORT;

        LOCK_INIT (&priv->lock);

		if (interval != 0)
			priv->qos_monitor_interval = interval;
		else
			priv->qos_monitor_interval = 0;

		// redis相关数据结构初始化
		if (redis_host) {
			priv->publisher->redis_host = CALLOC (1, sizeof(redis_host));
			ERR_ABORT(priv->publisher->redis_host);
			strcpy(priv->publisher->redis_host, redis_host);
		} else {
			priv->publisher->redis_host = CALLOC (1, sizeof(HOST));
			ERR_ABORT(priv->publisher->redis_host);
			strcpy(priv->publisher->redis_host, HOST);
		}

		if (publish_channel) {
			priv->publisher->channel = CALLOC (1, sizeof(publish_channel));
			ERR_ABORT(priv->publisher->channel);
			strcpy(priv->publisher->channel, publish_channel);
		} else {
			priv->publisher->channel = CALLOC (1, sizeof(CHANNEL));
			ERR_ABORT(priv->publisher->channel);
			strcpy(priv->publisher->channel, CHANNEL);
		}

		priv->publisher->redis_port = redis_port;

		gf_log (this->name, GF_LOG_WARNING,
                        "interval = %d, redis-host: %s, publish-channel: %s, redis-port: %d",
						priv->qos_monitor_interval, priv->publisher->redis_host, priv->publisher->channel, priv->publisher->redis_port);

		ret = redis_init(priv->publisher);
		if (!ret)
		{
			gf_log(this->name, GF_LOG_ERROR,
				   "Redis publisher init failed.");
		} else {
			gf_log(this->name, GF_LOG_ERROR,
				   "Redis publisher inited.");
		}

		ret = redis_connect(priv->publisher);
		if (!ret)
		{
			gf_log(this->name, GF_LOG_ERROR,
				   "Redis connect failed.");
		} else {
			gf_log(this->name, GF_LOG_ERROR,
					"Redis connected.");
		}

        /* Set this translator's inode table pointer to child node's pointer. */
        this->itable = FIRST_CHILD (this)->itable;

		this->private = priv;
		if (priv->qos_monitor_interval > 0) {
			priv->monitor_thread_running = 1;
			priv->monitor_thread_should_die = 0;
			ret = pthread_create(&priv->monitor_thread, NULL,
								   (void *)&_qos_monitor_thread, this);
			if (ret) {
				priv->monitor_thread_running = 0;
				gf_log(this ? this->name : "qos-monitor", GF_LOG_ERROR,
					   "Failed to start thread"
					   "in init. Returning %d",
					   ret);
				goto out;
			}
		}
		this->private = priv;
		gf_log(this->name, GF_LOG_ERROR,
           "qos_monitor: thread should_die: %d", ((qos_monitor_private_t *)this->private)->monitor_thread_should_die);
		gf_log (this->name, GF_LOG_WARNING,
                        "hello from qos_monitor.");
		return ret;

out:
	qos_private_destroy(priv);
    return ret;
}

void
fini (xlator_t *this)
{
        qos_monitor_private_t *priv = NULL;

        if (!this)
                return;

        priv = this->private;

		qos_private_destroy(priv);

		this->private = NULL;
        gf_log (this->name, GF_LOG_NORMAL,
                "io-stats translator unloaded");
        return;
}


struct xlator_fops fops = {
	.writev = qos_monitor_writev,
};

struct xlator_mops mops = {
};

struct xlator_cbks cbks = {
};

struct volume_options options[] = {
		{ .key  = {"monitor-interval", "interval"},
          .type = GF_OPTION_TYPE_INT,
        },
		{ .key  = {"redis-host", "host"},
          .type = GF_OPTION_TYPE_STR,
        },
		{ .key  = {"publish-channel", "channel"},
          .type = GF_OPTION_TYPE_STR,
        },
		{ .key  = {"redis-port", "port"},
          .type = GF_OPTION_TYPE_INT,
        },
        { .key  = {NULL} },
};
