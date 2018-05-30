/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2014 Couchbase, Inc
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

/*
 * Connection management and event loop handling.
 */

#ifndef CONNECTIONS_H
#define CONNECTIONS_H

#include "config.h"

#include "memcached.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Destroy all connections and reset connection management */
void destroy_connections(void);

/* Run through all the connections and close them */
void close_all_connections(void);

/* Run the connection event loop; until an event handler returns false. */
void run_event_loop(Connection* c, short which);

/**
 * If the connection doesn't already have read/write buffers, ensure that it
 * does.
 *
 * In the common case, only one read/write buffer is created per worker thread,
 * and this buffer is loaned to the connection the worker is currently
 * handling. As long as the connection doesn't have a partial read/write (i.e.
 * the buffer is totally consumed) when it goes idle, the buffer is simply
 * returned back to the worker thread.
 *
 * If there is a partial read/write, then the buffer is left loaned to that
 * connection and the worker thread will allocate a new one.
 */
void conn_loan_buffers(Connection *c);

/**
 * Return any empty buffers back to the owning worker thread.
 *
 * Converse of conn_loan_buffer(); if any of the read/write buffers are empty
 * (have no partial data) then return the buffer back to the worker thread.
 * If there is partial data, then keep the buffer with the connection.
 */
void conn_return_buffers(Connection *c);

/**
 * Cerate a new client connection
 *
 * @param sfd the socket descriptor
 * @param parent_port the port number the client connected to
 * @param base the event base to bind the client to
 * @param thread the libevent thread object to bind the client to
 * @return a connection object on success, nullptr otherwise
 */
Connection* conn_new(const SOCKET sfd,
                     in_port_t parent_port,
                     struct event_base* base,
                     LIBEVENT_THREAD* thread);

/**
 * Create a new server socket.
 *
 * @param sfd the socket descriptor
 * @param parent_port the port number
 * @param family the address family used for the port
 * @param interf the interface description
 * @param base the event base to use for the socket
 */
ListenConnection* conn_new_server(const SOCKET sfd,
                                  in_port_t parent_port,
                                  sa_family_t family,
                                  const struct interface& interf,
                                  struct event_base* base);

/*
 * Creates a new connection to a pipe, e.g. stdin.
 */
Connection *conn_pipe_new(const int fd,
                          struct event_base *base,
                          LIBEVENT_THREAD* thread);

/*
 * Closes a connection. Afterwards the connection is invalid (can no longer
 * be used), but it's memory is still allocated. See conn_destructor() to
 * actually free it's resources.
 */
void conn_close(McbpConnection *c);

/**
 * Return the TCP or domain socket listening_port structure that
 * has a given port number
 */
ListeningPort *get_listening_port_instance(const in_port_t port);

/* Dump stats for the connection with the given fd number, or all connections
 * if fd is -1.
 * Note: We hold the connections mutex for the duration of this function.
 */
void connection_stats(ADD_STAT add_stats, const void *c, const int64_t fd);

/*
 * Use engine::release to drop any data we may have allocated with engine::allocate
 */
void conn_cleanup_engine_allocations(McbpConnection * c);

/**
 * Signal (set writable) all idle clients bound to either a specific
 * bucket specified by its index, or any bucket (specified as -1).
 * Due to the threading model we're only going to look at the clients
 * connected to the thread represented by me.
 *
 * @param me the thread to inspect
 * @param bucket_idx the bucket we'd like to signal (set to -1 to signal
 *                   all buckets)
 * @return the number of client connections bound to this thread.
 */
int signal_idle_clients(LIBEVENT_THREAD *me, int bucket_idx, bool logging);

/**
 * Assert that none of the connections is assciated with
 * the given bucket (debug function).
 */
void assert_no_associations(int bucket_idx);

#ifndef WIN32
/**
 * Signal handler for SIGUSR1 to dump the connection states
 * for all of the connections.
 *
 * Please note that you <b>should</b> use <code>mcstat connections</code> to
 * get these stats on your node unless you've exhausted the connection limit
 * on the node.
 */
void dump_connection_stat_signal_handler(evutil_socket_t, short, void *);
#endif

/**
 * Apply the requested mask specified by the key. All connection related
 * trace masks for connections is located under: "trace.connection."
 *
 * The full spec for the key is:
 *    "trace.connection.<connectionid>[.field]"
 *
 * (field is currently ignored)
 *
 * @param key the requested key
 * @param mask the mask for the key.
 * @return result of the operation
 */
ENGINE_ERROR_CODE apply_connection_trace_mask(const std::string &key,
                                              const std::string &mask);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* CONNECTIONS_H */
