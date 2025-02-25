/**
 * Copyright (c) 2015,2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2015,2019 Open Grid Computing, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the BSD-type
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *      Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *      Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 *      Neither the name of Sandia nor the names of any contributors may
 *      be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 *      Neither the name of Open Grid Computing nor the names of any
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *      Modified source versions must be plainly marked as such, and
 *      must not be misrepresented as being the original software.
 *
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file zap_test_reconnect_may.c
 * \brief Zap test for reconnecting to many server scenario
 */

/**
 * \page zap_test_reconnect_many zap_test_reconnect_many
 *
 * \section summary SUMMARY
 *
 * Zap test for the case that the client keeps reconnecting to the many servers.
 *
 * \section synopsis SYNOPSIS
 *
 * \code{.sh}
 * # Server #
 * zap_test_reconnect_many -x XPRT -p PORT -s
 *
 * # Client #
 * zap_test_reconnect_many -x XPRT -p PORT -h HOST -n NUM [-f,--forever] [-m,--message]
 *
 * \endcode
 *
 * \section desc DESCRIPTION
 *
 * This test program, like other zap_test*, can run in two modes: server and
 * client. The test scheme is the following:
 *
 * Server:
 *   After a server starts, it will listen and wait for messages from a client.
 * Upon receiving a message, it will send the client back the messages. The servers
 * runs indefinitely. The servers should be started/stopped repeatedly in order
 * to test the robustness of the reconnect scenario. Run multiple servers in
 * order to test reconnecting to multiple servers. The servers must be
 * started on the same host using consecutive port numbers.
 *
 * Client:
 *   The client will try to connect/re-connect to the multiple servers. In the case that
 * a server does not exist, the client will free the endpoint and create a new
 * one to reconnect to the server. The client will send a message to all servers
 * if --message/-m is given. Otherwise, it will close the connections
 * after they are established. While a connection is connected, the client
 * will send a message to the corresponding server and then sleep for 2 seconds.
 * If the --forever/-f option is not given, the client will close the connection
 * and exit. On the other hand, it will send a message every
 * 2 second interval as long as the connection is connected. If the connection
 * is disconnected, the client will try to reconnect again.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <netinet/ip.h>
#include <sys/time.h>
#include <netdb.h>
#include <errno.h>
#include "zap/zap.h"

char *short_opt = "h:x:p:n:sfm?";
struct option long_opt[] = {
	{"host",     1,  0,  'h'},
	{"xprt",     1,  0,  'x'},
	{"port",     1,  0,  'p'},
	{"server",   0,  0,  's'},
	{"forever",  0,  0,  'f'},
	{"num_servers", 1, 0, 'n'},
	{"message",  0,  0,  'm'},
	{"help",     0,  0,  '?'},
	{0,0,        0,  0}
};

void usage()
{
	printf(
"Usage: test [-x <XPRT>] [-p <PORT>] [-h <HOST>] [-n <NUM> ] [--msg] [--forever] [-s]\n"
"\n"
"OPTIONS:\n"
"	-x XPRT		Transport. The default is sock.\n"
"	-p PORT		Port to listen/connect. The default is 55555\n"
"	-h HOST		(client mode only) Host to connect to. The default is localhost.\n"
"	-n NUM		Number of servers to connect to\n"
"	-s		Indicates server mode.\n"
"	-f,--forever	(client mode only) the client will keep sending messages to\n"
"			the server forever.\n"
"	-m,--message	Send messages to the servers\n"
	);
}

char *host = "localhost";
char *xprt = "sock";
uint16_t port = 10001;
int server_mode = 0;
int forever = 0;
int message = 0;
int num_server = 0;

struct conn {
	struct sockaddr_in sin;
	zap_t zap;
	zap_ep_t ep;
	pthread_mutex_t state_lock;
	enum conn_state {
		INIT = 0,
		CONNECTING,
		CONNECTED,
		DISCONNECTED,
	} state;
};

void handle_args(int argc, char **argv)
{
	char c;
loop:
	c = getopt_long(argc, argv, short_opt, long_opt, NULL);
	switch (c) {
	case 'x':
		xprt = optarg;
		break;
	case 'p':
		port = strtoul(optarg, NULL, 10);
		break;
	case 'h':
		host = optarg;
		break;
	case 'n':
		num_server = atoi(optarg);
		break;
	case 's':
		server_mode = 1;
		break;
	case 'f':
		forever = 1;
		break;
	case 'm':
		message = 1;
		break;
	case -1:
		goto out;
	case '?':
	default:
		usage();
		exit(0);
	}
	goto loop;
out:
	return;
}

struct conn *conn_list = 0;

int exiting = 0;
pthread_mutex_t exiting_mutex = PTHREAD_MUTEX_INITIALIZER;

void zap_log(const char *fmt, ...)
{
	va_list l;
	va_start(l, fmt);
	printf("%d: ", port);
	vprintf(fmt, l);
	va_end(l);
}

zap_mem_info_t zap_mem_info(void)
{
	return NULL;
}

void server_cb(zap_ep_t zep, zap_event_t ev)
{
	char *data;

	switch (ev->type) {
	case ZAP_EVENT_CONNECT_REQUEST:
		zap_accept(zep, server_cb, (void *)ev->data, ev->data_len);
		break;
	case ZAP_EVENT_CONNECTED:
		zap_log("connected\n");
		break;
	case ZAP_EVENT_DISCONNECTED:
		zap_log("disconnected\n");
		zap_free(zep);
		break;
	case ZAP_EVENT_RECV_COMPLETE:
		data = (char *)ev->data;
		zap_log("recv: %s\n", data);
		zap_log("echoing: %s\n", data);
		zap_send(zep, ev->data, ev->data_len);
		break;
	case ZAP_EVENT_REJECTED:
	case ZAP_EVENT_CONNECT_ERROR:
		zap_log("Unexpected Zap event %s\n", zap_event_str(ev->type));
		assert(0);
	default:
		zap_log("Unhandled Zap event %s\n", zap_event_str(ev->type));
		exit(-1);
	}
}

void client_cb(zap_ep_t zep, zap_event_t ev)
{
	struct conn *conn = (struct conn *)zap_get_ucontext(zep);

	struct timeval tv;
	gettimeofday(&tv, NULL);

	pthread_mutex_lock(&conn->state_lock);
	switch(ev->type) {
	case ZAP_EVENT_CONNECTED:
		conn->state = CONNECTED;
		printf("%s to %hu\n", zap_event_str(ev->type),
				ntohs(conn->sin.sin_port));
		break;
	case ZAP_EVENT_CONNECT_ERROR:
		printf("%s from %hu\n", zap_event_str(ev->type),
				ntohs(conn->sin.sin_port));
		conn->state = DISCONNECTED;
		zap_free(zep);
		conn->ep = NULL;
		break;
	case ZAP_EVENT_DISCONNECTED:
		printf("%s from %hu\n", zap_event_str(ev->type),
				ntohs(conn->sin.sin_port));
		conn->state = DISCONNECTED;
		zap_free(zep);
		conn->ep = NULL;
		break;
	case ZAP_EVENT_RECV_COMPLETE:
		printf("%hu: recv: %s\n", ntohs(conn->sin.sin_port),
						(char*)ev->data);
		break;
	case ZAP_EVENT_REJECTED:
		printf("Error: Server never reject the connection request\n");
		pthread_mutex_unlock(&conn->state_lock);
		exit(-1);
		break;
	default:
		printf("Unhandled Zap event %s\n", zap_event_str(ev->type));
		pthread_mutex_unlock(&conn->state_lock);
		exit(-1);
	}
	pthread_mutex_unlock(&conn->state_lock);
}

void do_server(struct sockaddr_in *sin)
{
	zap_t zap;
	zap_ep_t ep;
	int zerr;
	zap = zap_get(xprt, zap_log, zap_mem_info);
	if (!zap) {
		zerr = errno;
		zap_log("zap_get error: %d\n", zerr);
		exit(-1);
	}
	ep = zap_new(zap, server_cb);
	if (!ep) {
		zerr = errno;
		zap_log("zap_new error: %d\n", zerr);
		exit(-1);
	}

	zerr = zap_listen(ep, (void*)sin, sizeof(*sin));
	if (zerr) {
		zap_log("zap_listen error: %d\n", zerr);
		exit(-1);
	}
	zap_log("Listening on port %hu\n", port);
	/* Run forever */
	while (1) {
		/* do nothing */
		sleep(60);
	}
}


/* Caller must take the state lock */
int __connect(struct conn *conn)
{
	zap_err_t zerr;
	if (conn->state != INIT || conn->state != DISCONNECTED) {
		assert("Invalid conn->state");
	}

	if (!conn->zap) {
		conn->zap = zap_get(xprt, zap_log, zap_mem_info);
		if (!conn->zap) {
			printf("zap_get error\n");
			exit(-1);
		}
	}

	conn->ep = zap_new(conn->zap, client_cb);
	if (!conn->ep) {
		printf("zap_new error\n");
		exit(-1);
	}

	zap_set_ucontext(conn->ep, (void *)conn);

	printf("connecting to %s:%hu\n", host, ntohs(conn->sin.sin_port));
	conn->state = CONNECTING;

	zerr = zap_connect(conn->ep, (void *)&(conn->sin), sizeof(conn->sin), NULL, 0);
	if (zerr) {
		printf("Error %d: zap_connect failed.\n", zerr);
		exit(-1);
	}
	return 0;
}

void send_msg(struct conn *conn)
{
	struct sockaddr_storage lsin = {0};
	struct sockaddr_storage rsin = {0};
	socklen_t slen;
	struct timeval tv;
	char data[512];
	zap_err_t zerr;

	zap_ep_t ep = conn->ep;

	gettimeofday(&tv, NULL);
	slen = sizeof(lsin);
	zap_get_name(ep, (void*)&lsin, (void*)&rsin, &slen);
	printf("%hu: Sending %d.%d\n", ntohs(conn->sin.sin_port),
					(int)tv.tv_sec, (int)tv.tv_usec);
	snprintf(data, 512, "%d.%d", (int)tv.tv_sec, (int)tv.tv_usec);
	zerr = zap_send(ep, data, strlen(data) + 1);
	if (zerr != ZAP_ERR_OK)
		printf("Error %d in zap_send.\n", zerr);
}

void *client_routine(void *arg)
{
	struct conn *conn_list = (struct conn *)arg;
	int i;
	zap_err_t zerr;

	for (i = 0; i < num_server; i++) {
		__connect(&conn_list[i]);
	}
	sleep(1);
	struct conn *conn;
	int connected = 0;
	while (1) {
		for (i = 0; i < num_server; i++) {
			conn = &conn_list[i];
			pthread_mutex_lock(&conn->state_lock);
			if (conn->state == CONNECTED) {
				connected++;
				if (message)
					send_msg(conn);
				if (!forever) {
					zerr = zap_close(conn->ep);
					if (zerr)
						printf("Error %d in zap_close\n",
								zerr);
				}
			} else if (conn->state == DISCONNECTED) {
				__connect(conn);
			}
			pthread_mutex_unlock(&conn->state_lock);
		}
		sleep(2);
		if (!forever && (!message || (connected == num_server))) {
			pthread_mutex_lock(&exiting_mutex);
			exiting = 1;
			pthread_mutex_unlock(&exiting_mutex);
			break;
		}
	}
	return NULL;
}

void do_client(struct sockaddr_in *sin)
{
	struct addrinfo *ai;
	int rc;

	conn_list = calloc(num_server, sizeof(struct conn));
	if (!conn_list) {
		printf("Out of memory\n");
		exit(-1);
	}

	if (!host) {
		printf("Please give the host name of the server\n");
		exit(-1);
	}

	rc = getaddrinfo(host, NULL, NULL, &ai);
	if (rc) {
		printf("getaddrinfo error: %d\n", rc);
		exit(-1);
	}

	int i;
	for (i = 0; i < num_server; i++) {
		conn_list[i].sin = *(struct sockaddr_in *)ai[0].ai_addr;
		conn_list[i].sin.sin_port = htons(port + i);
		pthread_mutex_init(&conn_list[i].state_lock, NULL);
	}

	freeaddrinfo(ai);

	pthread_t t;
	rc = pthread_create(&t, NULL, client_routine, conn_list);
	if (rc) {
		printf("pthread_create error %d\n", rc);
		exit(-1);
	}
	pthread_join(t, NULL);
}

int main(int argc, char **argv)
{
	handle_args(argc, argv);

	struct sockaddr_in sin = {0};
	sin.sin_port = htons(port);
	sin.sin_family = AF_INET;

	if (server_mode)
		do_server(&sin);
	else
		do_client(&sin);
	sleep(1);
	return 0;
}
