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
 *    This translator monitor following metrics and publish to redis chanenl per interval:
 *    a) app_rbw: throughput of read operation - interval - per clien_id 
 *    b) app_wbw: throughput of writev operation - interval - per clien_id 
 *    c) app_r_delay: times of read operation - interval - per clien_id 
 *    d) app_w_delay: times of writev operation - interval - per clien_id 
 *    e) app_diops: times of io operation - interval - per clien_id
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

double time_difference_ms(struct timeval *begin, struct timeval *end)
{
	double duration = 0;
	if (begin == NULL || end == NULL)
		return duration;
	duration = (end->tv_sec - begin->tv_sec) * 1000 + ((end->tv_usec - begin->tv_usec) / 1000);
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
		gf_log("monitor", GF_LOG_INFO,
               "publish %s %s\n", channel_name, message);
        return 1;
    }
}

static void
qos_monitor_data_clear(dict_t *metrics)
{
	ERR_ABORT (metrics);  
	gf_log("monitor", GF_LOG_INFO, "enter qos_monitor_data_clear");
	if (metrics->count > 0)
	{
		dict_destroy(metrics);
		metrics = dict_new();
	}
	gf_log("monitor", GF_LOG_INFO, "qos_monitor_data_clear finished.");
}

void get_server_ip(char *result)
{
	struct ifaddrs *ifAddrStruct = NULL;
    struct ifaddrs *ifa = NULL;
    void *tmpAddrPtr = NULL;
	
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
}

// TODO：待优化发送写法以及指标获取写法：初步想法用数组和下标+枚举类型对应
void func(dict_t *this, char *key, data_t *value, void *data)
{
	gf_log("monitor", GF_LOG_INFO, "enter func");
	char message[120];
	qos_monitor_private_t *priv = NULL;
	struct qos_monitor_data *monitor_data = NULL;
	struct timeval now;
	char client[CLIENTID];
	char server_ip[16];
	double duration = 0;
	
	priv = (qos_monitor_private_t *)data;
	monitor_data = (struct qos_monitor_data *)data_to_ptr(value);
	
	gettimeofday(&now, NULL);
	get_client_id(key, client); 
	get_server_ip(server_ip);
	
	sprintf(message, "%s^^%s^^%ld^^%s^^%lf", server_ip, client, now.tv_sec, "app_wbw", monitor_data->data_written);
	publish(priv->publisher->channel, message, priv->publisher);
	usleep(1);

	sprintf(message, "%s^^%s^^%ld^^%s^^%lf", server_ip, client, now.tv_sec, "app_rbw", monitor_data->data_read);
	publish(priv->publisher->channel, message, priv->publisher);
	usleep(1);

	sprintf(message, "%s^^%s^^%ld^^%s^^%lf", server_ip, client, now.tv_sec, "app_r_delay", monitor_data->read_delay.value);
	publish(priv->publisher->channel, message, priv->publisher);
	usleep(1);

	sprintf(message, "%s^^%s^^%ld^^%s^^%lf", server_ip, client, now.tv_sec, "app_w_delay", monitor_data->write_delay.value);
	publish(priv->publisher->channel, message, priv->publisher);
	usleep(1);

	duration = time_difference(&monitor_data->started_at ,&now);
	if (duration == 0)
		duration = 1;
	sprintf(message, "%s^^%s^^%ld^^%s^^%lf", server_ip, client, now.tv_sec, "app_diops", monitor_data->data_iops / duration);
	publish(priv->publisher->channel, message, priv->publisher);
	usleep(1);
	
	monitor_data->started_at = now;
}

void * _qos_monitor_thread(xlator_t *this)
{
	qos_monitor_private_t *priv = NULL;
	int old_cancel_type;
	//dict_t *metrics = NULL;
	
	priv = this->private;
	gf_log(this->name, GF_LOG_INFO,
           "qos_monitor monitor thread started, "
           "polling IO stats every %d seconds",
           priv->qos_monitor_interval);

	while (1) {
		// gf_log(this->name, GF_LOG_ERROR, "qos_monitor: thread should_die: %d", priv->monitor_thread_should_die);
		if (priv->monitor_thread_should_die)
			break;

		(void)pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &old_cancel_type);
        gf_log(this->name, GF_LOG_INFO, "sleep....");
		sleep(priv->qos_monitor_interval);
        (void)pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, &old_cancel_type);
		
		/*LOCK(&priv->lock);
		{
			gf_log(this->name, GF_LOG_INFO, "copy");
			metrics = dict_copy(priv->metrics, metrics);
			//qos_monitor_data_clear(priv->metrics);
			publish(priv->publisher->channel, "hello", priv->publisher);
		}
		UNLOCK(&priv->lock);*/
		
		/* publish monitor metrics */
		gf_log(this->name, GF_LOG_INFO, "--- qos monitor publisher ---");
		dict_foreach(priv->metrics, func, priv);
		/*if (metrics != NULL) {
			dict_destroy(metrics);
			metrics = NULL;
		}*/
		
	}

	priv->monitor_thread_running = 0;
	gf_log(this->name, GF_LOG_INFO, "QoS_monitor monitor thread terminated");
	/*if (metrics != NULL)
		dict_destroy(metrics);*/
	
    return NULL;
}

int _qos_destroy_monitor_thread(qos_monitor_private_t *priv)
{
	gf_log("sh", GF_LOG_INFO, "qos_destroy_monitor_thread invoked.");
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
	gf_log("q", GF_LOG_INFO, "qos_private_destroy invoked.");
	if (priv->metrics)
	{
		dict_destroy(priv->metrics);
		priv->metrics = NULL;
	}
	
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
	gf_log("q", GF_LOG_INFO, "qos_private_destroy finished.");
}


void  _qos_init_monitor_data(struct qos_monitor_data *monitor_data)
{
	monitor_data->data_written = 0.0;
	monitor_data->data_read = 0.0;
	monitor_data->data_iops = 0.0;
	gettimeofday(&monitor_data->started_at, NULL);
	// TODO: whether it's ok to set this initial.
	monitor_data->write_delay.wind_at = monitor_data->started_at;
	monitor_data->write_delay.unwind_at = monitor_data->started_at;
    monitor_data->write_delay.value = 0.0;
	monitor_data->read_delay.wind_at = monitor_data->started_at;
	monitor_data->read_delay.unwind_at = monitor_data->started_at;
	monitor_data->read_delay.value = 0.0;
	gf_log("sh", GF_LOG_INFO, "qos_monitor_data inited.");
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
        qos_monitor_private_t *priv = NULL;
		client_id_t *client = NULL;
		struct qos_monitor_data *monitor_data = NULL;
		struct timeval begin;
		struct timeval end;
		double duration;
		int ret = 0;

		gf_log("sh", GF_LOG_INFO, "enter qos_monitor_writev_cbk.");
        priv = this->private;
		client = (client_id_t*) frame->root->trans;
		
		LOCK(&priv->lock);
		if (priv->metrics != NULL) {
			dict_ref(priv->metrics);
			ret = dict_get_ptr(priv->metrics, client->id, (void **)&monitor_data);
			gf_log("sh", GF_LOG_INFO, "dict_get_ptr fini.");
			if (ret != 0) {
				gf_log("sh", GF_LOG_ERROR, "dict_get_ptr failed.");
			} else {
				monitor_data = (struct qos_monitor_data *)monitor_data;	
				gettimeofday(&monitor_data->write_delay.unwind_at, NULL);
				begin = monitor_data->write_delay.wind_at;
				end = monitor_data->write_delay.unwind_at;
				duration = (time_difference(&begin, &end) != 0 ? time_difference(&begin, &end) : 1);
				monitor_data->data_written = (monitor_data->data_written + op_ret / KB / duration) / 2;
				monitor_data->write_delay.value = (monitor_data->write_delay.value + time_difference_ms(&begin, &end)) / 2;
				gf_log("sh", GF_LOG_INFO, "value = %lf", monitor_data->write_delay.value);
			}
			data_unref(data_from_ptr((void*)monitor_data));
			dict_unref(priv->metrics);			
			gf_log("sh", GF_LOG_INFO, "qos_monitor_writev_cbk prepared.");
		} else {
			gf_log("sh", GF_LOG_ERROR, "priv->metrics == NULL.");
		}
		UNLOCK(&priv->lock);

		gf_log("sh", GF_LOG_INFO, "qos_monitor_writev_cbk unwind start.");
		STACK_UNWIND (frame, op_ret, op_errno, prebuf, postbuf);
		gf_log("sh", GF_LOG_INFO, "qos_monitor_writev_cbk unwind end.");
        return 0;
}


int32_t
qos_monitor_readv_cbk (call_frame_t *frame,
				 void *cookie,
				 xlator_t *this,
				 int32_t op_ret,
				 int32_t op_errno,
				 struct iovec *vector,
				 int32_t count,
				 struct stat *buf,
				 struct iobref *iobref)
{
	qos_monitor_private_t *priv = NULL;
	client_id_t *client = NULL;
	struct qos_monitor_data *monitor_data = NULL;
	struct timeval begin;
	struct timeval end;
	double duration;
	int ret = 0;

	gf_log("sh", GF_LOG_INFO, "enter.");
	priv = this->private;
	client = (client_id_t*) frame->root->trans;

	LOCK(&priv->lock);
	if (priv->metrics != NULL) {
		dict_ref(priv->metrics);
		ret = dict_get_ptr(priv->metrics, client->id, (void **)&monitor_data);
		gf_log("sh", GF_LOG_INFO, "dict_get_ptr fini.");
		if (ret != 0) {
			gf_log("sh", GF_LOG_ERROR, "dict_get_ptr failed.");
		} else {
			monitor_data = (struct qos_monitor_data *)monitor_data;	
			gettimeofday(&monitor_data->read_delay.unwind_at, NULL);
			begin = monitor_data->read_delay.wind_at;
			end = monitor_data->read_delay.unwind_at;
			duration = (time_difference(&begin, &end) != 0 ? time_difference(&begin, &end) : 1);
			monitor_data->data_read = (monitor_data->data_read + op_ret / KB / duration) / 2;
			monitor_data->read_delay.value = (monitor_data->read_delay.value + time_difference_ms(&begin, &end)) / 2;
			gf_log("sh", GF_LOG_INFO, "value = %lf", monitor_data->read_delay.value);
		}
		data_unref(data_from_ptr((void*)monitor_data));
		dict_unref(priv->metrics);			
		gf_log("sh", GF_LOG_INFO, "prepared.");
	} else {
		gf_log("sh", GF_LOG_ERROR, "priv->metrics == NULL.");
	}
	UNLOCK(&priv->lock);


	STACK_UNWIND (frame, op_ret, op_errno, vector, count, buf, iobref);

	return 0;
}


int32_t
qos_monitor_readv (call_frame_t *frame,
			 xlator_t *this,
			 fd_t *fd,
			 size_t size,
			 off_t offset)
{
	 
	 qos_monitor_private_t *priv = NULL;
	 client_id_t *client = NULL;
	 struct qos_monitor_data *monitor_data = NULL;
	 int ret = 0;
	 int first = 0;
	 
	 gf_log("sh", GF_LOG_INFO, "enter.");
	 priv = this->private;
	 client = (client_id_t*) frame->root->trans;

	 LOCK(&priv->lock);
	 gf_log("sh", GF_LOG_INFO, "lock");
	 if (priv->metrics != NULL) {
		 gf_log("sh", GF_LOG_INFO, "priv->metrics != NULL.");
		 dict_ref(priv->metrics);

		 gf_log("sh", GF_LOG_INFO, "dict_get_ptr.");
		 ret = dict_get_ptr(priv->metrics, client->id, (void **)&monitor_data);
		 gf_log("sh", GF_LOG_INFO, "dict_get_ptr fini.");

		 if (ret != 0) {
			 first = 1;
			 gf_log("sh", GF_LOG_INFO, "monitor_data doesn't exist.");
			 monitor_data = CALLOC (1, sizeof(*monitor_data));
			 ERR_ABORT (monitor_data);	
			 _qos_init_monitor_data(monitor_data);
			 ret = dict_set_ptr(priv->metrics, client->id, (void *)monitor_data);
			 if (ret != 0)
				 gf_log("sh", GF_LOG_ERROR, "dict set failed.");
		 } else {
			 first = 0;
			 gf_log("sh", GF_LOG_INFO, "monitor_data exist.");
			 monitor_data = (struct qos_monitor_data *)monitor_data; 
		 } /* end if monitor_data == NULL */

		 gf_log("sh", GF_LOG_INFO, "get write_delay.wind_at.");
		 gettimeofday(&monitor_data->read_delay.wind_at, NULL);
		 monitor_data->data_iops++;

		 if (first)
			 data_unref(data_from_ptr((void *)monitor_data));
		 dict_unref(priv->metrics);
		 gf_log("sh", GF_LOG_INFO, "qos_monitor_writev prepared.");
	 } else {
		 gf_log("sh", GF_LOG_ERROR, "priv->metrics == NULL.");
	 }
	 UNLOCK(&priv->lock);
	 gf_log("sh", GF_LOG_INFO, "unlock");
	 
	 gf_log("sh", GF_LOG_INFO, "start wind.");

	 STACK_WIND (frame,
				 qos_monitor_readv_cbk,
				 FIRST_CHILD(this),
				 FIRST_CHILD(this)->fops->readv,
				 fd,
				 size,
				 offset);
	 
	 gf_log("sh", GF_LOG_INFO, "end wind.");
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

		qos_monitor_private_t *priv = NULL;
		client_id_t *client = NULL;
		struct qos_monitor_data *monitor_data = NULL;
		int ret = 0;
		int first = 0;
		
		gf_log("sh", GF_LOG_INFO, "enter qos_monitor_writev.");
        priv = this->private;
		client = (client_id_t*) frame->root->trans;

		LOCK(&priv->lock);
		gf_log("sh", GF_LOG_INFO, "lock");
		if (priv->metrics != NULL) {
			gf_log("sh", GF_LOG_INFO, "priv->metrics != NULL.");
			dict_ref(priv->metrics);

			gf_log("sh", GF_LOG_INFO, "dict_get_ptr.");
			ret = dict_get_ptr(priv->metrics, client->id, (void **)&monitor_data);
			gf_log("sh", GF_LOG_INFO, "dict_get_ptr fini.");

			if (ret != 0) {
				first = 1;
				gf_log("sh", GF_LOG_INFO, "monitor_data doesn't exist.");
				monitor_data = CALLOC (1, sizeof(*monitor_data));
				ERR_ABORT (monitor_data);  
				_qos_init_monitor_data(monitor_data);
				ret = dict_set_ptr(priv->metrics, client->id, (void *)monitor_data);
				if (ret != 0)
					gf_log("sh", GF_LOG_ERROR, "dict set failed.");
			} else {
				first = 0;
				gf_log("sh", GF_LOG_INFO, "monitor_data exist.");
				monitor_data = (struct qos_monitor_data *)monitor_data;	
			} /* end if monitor_data == NULL */

			gf_log("sh", GF_LOG_INFO, "get write_delay.wind_at.");
			gettimeofday(&monitor_data->write_delay.wind_at, NULL);
			monitor_data->data_iops++;

			if (first)
				data_unref(data_from_ptr((void *)monitor_data));
			dict_unref(priv->metrics);
			gf_log("sh", GF_LOG_INFO, "qos_monitor_writev prepared.");
		} else {
			gf_log("sh", GF_LOG_ERROR, "priv->metrics == NULL.");
		}
		UNLOCK(&priv->lock);
		gf_log("sh", GF_LOG_INFO, "unlock");
		
        gf_log("sh", GF_LOG_INFO, "start wind.");
        STACK_WIND (frame,
                    qos_monitor_writev_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->writev,
                    fd,
                    vector,
                    count,
                    offset,
                    iobref);
		gf_log("sh", GF_LOG_INFO, "end wind.");
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

		priv->metrics = dict_new();
		ERR_ABORT (priv->metrics);  

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
		
		gf_log (this->name, GF_LOG_INFO,
                        "interval = %d, redis-host: %s, publish-channel: %s, redis-port: %d", 
						priv->qos_monitor_interval, priv->publisher->redis_host, priv->publisher->channel, priv->publisher->redis_port);
		
		ret = redis_init(priv->publisher);
		if (!ret)
		{
			gf_log(this->name, GF_LOG_ERROR,
				   "Redis publisher init failed.");
		} else {
			gf_log(this->name, GF_LOG_INFO,
				   "Redis publisher inited.");
		}
		
		ret = redis_connect(priv->publisher);
		if (!ret)
		{
			gf_log(this->name, GF_LOG_ERROR,
				   "Redis connect failed.");
		} else {
			gf_log(this->name, GF_LOG_INFO,
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
		gf_log(this->name, GF_LOG_INFO,
           "qos_monitor: thread should_die: %d", ((qos_monitor_private_t *)this->private)->monitor_thread_should_die);
		gf_log (this->name, GF_LOG_INFO,
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
        .writev      = qos_monitor_writev,
        .readv       = qos_monitor_readv,
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
