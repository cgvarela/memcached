/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2015 Couchbase, Inc.
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
#include "config.h"
#include "mcaudit.h"
#include "memcached.h"
#include "runtime.h"
#include "statemachine_mcbp.h"

#include <exception>
#include <utilities/protocol2text.h>
#include <platform/checked_snprintf.h>
#include <platform/strerror.h>

const char* to_string(const Connection::Priority& priority) {
    switch (priority) {
    case Connection::Priority::High:
        return "High";
    case Connection::Priority::Medium:
        return "Medium";
    case Connection::Priority::Low:
        return "Low";
    }
    throw std::invalid_argument("No such priority: " +
                                std::to_string(int(priority)));
}

static cbsasl_conn_t* create_new_cbsasl_server_t() {
    cbsasl_conn_t *conn;
    if (cbsasl_server_new("memcached", // service
                          nullptr, // Server DQDN
                          nullptr, // user realm
                          nullptr, // iplocalport
                          nullptr, // ipremoteport
                          nullptr, // callbacks
                          0, // flags
                          &conn) != CBSASL_OK) {
        throw std::bad_alloc();
    }
    return conn;
}

Connection::Connection(SOCKET sfd, event_base* b)
    : socketDescriptor(sfd),
      base(b),
      sasl_conn(create_new_cbsasl_server_t()),
      internal(false),
      authenticated(false),
      username("unknown"),
      domain(cb::sasl::Domain::Local),
      nodelay(false),
      refcount(0),
      engine_storage(nullptr),
      next(nullptr),
      thread(nullptr),
      parent_port(0),
      bucketEngine(nullptr),
      peername("unknown"),
      sockname("unknown"),
      priority(Priority::Medium),
      clustermap_revno(-2),
      trace_enabled(false),
      xerror_support(false),
      collections_support(false) {
    MEMCACHED_CONN_CREATE(this);
    bucketIndex.store(0);
    updateDescription();
}

Connection::Connection(SOCKET sock,
                       event_base* b,
                       const ListeningPort& interface)
    : Connection(sock, b) {
    parent_port = interface.port;
    resolveConnectionName(false);
    setTcpNoDelay(interface.tcp_nodelay);
    updateDescription();
}

Connection::~Connection() {
    MEMCACHED_CONN_DESTROY(this);
    if (socketDescriptor != INVALID_SOCKET) {
        LOG_INFO(this, "%u - Closing socket descriptor", getId());
        safe_close(socketDescriptor);
    }
}

/**
 * Convert a sockaddr_storage to a textual string (no name lookup).
 *
 * @param addr the sockaddr_storage received from getsockname or
 *             getpeername
 * @param addr_len the current length used by the sockaddr_storage
 * @return a textual string representing the connection. or NULL
 *         if an error occurs (caller takes ownership of the buffer and
 *         must call free)
 */
static std::string sockaddr_to_string(const struct sockaddr_storage* addr,
                                      socklen_t addr_len) {
    char host[50];
    char port[50];

    int err = getnameinfo(reinterpret_cast<const struct sockaddr*>(addr),
                          addr_len,
                          host, sizeof(host),
                          port, sizeof(port),
                          NI_NUMERICHOST | NI_NUMERICSERV);
    if (err != 0) {
        LOG_WARNING(NULL, "getnameinfo failed with error %d", err);
        return NULL;
    }

    if (addr->ss_family == AF_INET6) {
        return "[" + std::string(host) + "]:" + std::string(port);
    } else {
        return std::string(host) + ":" + std::string(port);
    }
}

void Connection::resolveConnectionName(bool listening) {
    int err;
    try {
        if (listening) {
            peername = "*";
        } else {
            struct sockaddr_storage peer;
            socklen_t peer_len = sizeof(peer);
            if ((err = getpeername(socketDescriptor,
                                   reinterpret_cast<struct sockaddr*>(&peer),
                                   &peer_len)) != 0) {
                LOG_WARNING(NULL, "getpeername for socket %d with error %d",
                            socketDescriptor, err);
            } else {
                peername = sockaddr_to_string(&peer, peer_len);
            }
        }

        struct sockaddr_storage sock;
        socklen_t sock_len = sizeof(sock);
        if ((err = getsockname(socketDescriptor,
                               reinterpret_cast<struct sockaddr*>(&sock),
                               &sock_len)) != 0) {
            LOG_WARNING(NULL, "getsockname for socket %d with error %d",
                        socketDescriptor, err);
        } else {
            sockname = sockaddr_to_string(&sock, sock_len);
        }
        updateDescription();
    } catch (std::bad_alloc& e) {
        LOG_WARNING(NULL,
                    "Connection::resolveConnectionName: failed to allocate memory: %s",
                    e.what());
    }
}

bool Connection::setTcpNoDelay(bool enable) {
    int flags = enable ? 1 : 0;

#if defined(WIN32)
    char* flags_ptr = reinterpret_cast<char*>(&flags);
#else
    void* flags_ptr = reinterpret_cast<void*>(&flags);
#endif
    int error = setsockopt(socketDescriptor, IPPROTO_TCP, TCP_NODELAY,
                           flags_ptr,
                           sizeof(flags));

    if (error != 0) {
        std::string errmsg = cb_strerror(GetLastNetworkError());
        LOG_WARNING(this, "setsockopt(TCP_NODELAY): %s",
                    errmsg.c_str());
        nodelay = false;
        return false;
    } else {
        nodelay = enable;
    }

    return true;
}

/* cJSON uses double for all numbers, so only has 53 bits of precision.
 * Therefore encode 64bit integers as string.
 */
static cJSON* json_create_uintptr(uintptr_t value) {
    try {
        char buffer[32];
        checked_snprintf(buffer, sizeof(buffer), "0x%" PRIxPTR, value);
        return cJSON_CreateString(buffer);
    } catch (std::exception& e) {
        return cJSON_CreateString("<Failed to convert pointer>");
    }
}

static void json_add_uintptr_to_object(cJSON* obj, const char* name,
                                       uintptr_t value) {
    cJSON_AddItemToObject(obj, name, json_create_uintptr(value));
}

static void json_add_bool_to_object(cJSON* obj, const char* name, bool value) {
    if (value) {
        cJSON_AddTrueToObject(obj, name);
    } else {
        cJSON_AddFalseToObject(obj, name);
    }
}

std::string to_string(Protocol protocol) {
    switch (protocol) {
    case Protocol::Memcached:
        return "memcached";
    }

    return "unknown protocol: " + std::to_string(int(protocol));
}

cJSON* Connection::toJSON() const {
    cJSON* obj = cJSON_CreateObject();
    json_add_uintptr_to_object(obj, "connection", (uintptr_t)this);
    if (socketDescriptor == INVALID_SOCKET) {
        cJSON_AddStringToObject(obj, "socket", "disconnected");
    } else {
        cJSON_AddNumberToObject(obj, "socket", (double)socketDescriptor);
        cJSON_AddStringToObject(
                obj, "protocol", to_string(getProtocol()).c_str());
        cJSON_AddStringToObject(obj, "peername", getPeername().c_str());
        cJSON_AddStringToObject(obj, "sockname", getSockname().c_str());
        cJSON_AddNumberToObject(obj, "parent_port", parent_port);
        cJSON_AddNumberToObject(obj, "bucket_index", getBucketIndex());
        json_add_bool_to_object(obj, "internal", isInternal());
        if (authenticated) {
            cJSON_AddStringToObject(obj, "username", username.c_str());
        }
        if (sasl_conn != NULL) {
            json_add_uintptr_to_object(obj, "sasl_conn",
                                       (uintptr_t)sasl_conn.get());
        }
        json_add_bool_to_object(obj, "nodelay", nodelay);
        cJSON_AddNumberToObject(obj, "refcount", refcount);

        cJSON* features = cJSON_CreateObject();
        json_add_bool_to_object(features, "mutation_extras",
                                isSupportsMutationExtras());
        json_add_bool_to_object(features, "xerror", isXerrorSupport());

        cJSON_AddItemToObject(obj, "features", features);

        json_add_uintptr_to_object(obj, "engine_storage",
                                   (uintptr_t)engine_storage);
        json_add_uintptr_to_object(obj, "next", (uintptr_t)next);
        json_add_uintptr_to_object(obj, "thread", (uintptr_t)thread.load(
            std::memory_order::memory_order_relaxed));
        cJSON_AddStringToObject(obj, "priority", to_string(priority));

        if (clustermap_revno == -2) {
            cJSON_AddStringToObject(obj, "clustermap_revno", "unknown");
        } else {
            cJSON_AddNumberToObject(obj, "clustermap_revno", clustermap_revno);
        }
    }
    return obj;
}

void Connection::restartAuthentication() {
    sasl_conn.reset(create_new_cbsasl_server_t());
    internal = false;
    authenticated = false;
    username = "";
}

cb::rbac::PrivilegeAccess Connection::checkPrivilege(
        cb::rbac::Privilege privilege, Cookie& cookie) {
    cb::rbac::PrivilegeAccess ret;

    ret = privilegeContext.check(privilege);
    if (ret == cb::rbac::PrivilegeAccess::Stale) {
        // @todo refactor this so that we may run this through a
        //       greenstack command context!
        std::string command;
        auto* mcbp = dynamic_cast<McbpConnection*>(this);
        if (mcbp != nullptr) {
            command = memcached_opcode_2_text(mcbp->getCmd());
        }

        // The privilege context we had could have been a dummy entry
        // (created when the client connected, and used until the
        // connection authenticates). Let's try to automatically update it,
        // but let the client deal with whatever happens after
        // a single update.
        try {
            privilegeContext = cb::rbac::createContext(getUsername(),
                                                       all_buckets[bucketIndex].name);
        } catch (const cb::rbac::NoSuchBucketException& error) {
            // Remove all access to the bucket
            privilegeContext = cb::rbac::createContext(getUsername(), "");
            LOG_NOTICE(this,
                       "%u: RBAC: Connection::checkPrivilege(%s) %s No access to bucket [%s]. command: [%s] new privilege set: %s",
                       getId(), to_string(privilege).c_str(),
                       getDescription().c_str(),
                       all_buckets[bucketIndex].name,
                       command.c_str(),
                       privilegeContext.to_string().c_str());
        } catch (const cb::rbac::Exception& error) {
            LOG_WARNING(this,
                        "%u: RBAC: Connection::checkPrivilege(%s) %s: An "
                        "exception occurred. command: [%s] bucket: [%s] UUID:"
                        "[%s] message: %s",
                        getId(),
                        to_string(privilege).c_str(),
                        getDescription().c_str(),
                        command.c_str(),
                        all_buckets[bucketIndex].name,
                        cookie.getEventId().c_str(),
                        error.what());
            // Add a textual error as well
            cookie.setErrorContext("An exception occurred. command: [" +
                                   command + "]");
            return cb::rbac::PrivilegeAccess::Fail;
        }

        ret = privilegeContext.check(privilege);
    }

    if (ret == cb::rbac::PrivilegeAccess::Fail) {
        // @todo refactor this so that we may run this through a
        //       greenstack command context!
        std::string command;
        auto* mcbp = dynamic_cast<McbpConnection*>(this);
        if (mcbp != nullptr) {
            command = memcached_opcode_2_text(mcbp->getCmd());
        }

        const std::string privilege_string = cb::rbac::to_string(privilege);
        const std::string context = privilegeContext.to_string();

        if (settings.isPrivilegeDebug()) {
            audit_privilege_debug(this, command, all_buckets[bucketIndex].name,
                                  privilege_string, context);

            LOG_NOTICE(this,
                       "%u: RBAC privilege debug: %s command: [%s] bucket: [%s] privilege: [%s] context: %s",
                       getId(),
                       getDescription().c_str(),
                       command.c_str(),
                       all_buckets[bucketIndex].name,
                       privilege_string.c_str(),
                       context.c_str());

            return cb::rbac::PrivilegeAccess::Ok;
        } else {
            LOG_NOTICE(nullptr,
                       "%u RBAC %s missing privilege %s for %s in bucket:[%s] with context: "
                       "%s UUID:[%s]",
                       getId(),
                       getDescription().c_str(),
                       privilege_string.c_str(),
                       command.c_str(),
                       all_buckets[bucketIndex].name,
                       context.c_str(),
                       cookie.getEventId().c_str());
            // Add a textual error as well
            cookie.setErrorContext("Authorization failure: can't execute " +
                                   command + " operation without the " +
                                   privilege_string + " privilege");
        }
    }

    return ret;
}

Bucket& Connection::getBucket() const {
    return all_buckets[getBucketIndex()];
}

ENGINE_ERROR_CODE Connection::remapErrorCode(ENGINE_ERROR_CODE code) const {
    if (xerror_support) {
        return code;
    }

    // Check our whitelist
    switch (code) {
    case ENGINE_SUCCESS: // FALLTHROUGH
    case ENGINE_KEY_ENOENT: // FALLTHROUGH
    case ENGINE_KEY_EEXISTS: // FALLTHROUGH
    case ENGINE_ENOMEM: // FALLTHROUGH
    case ENGINE_NOT_STORED: // FALLTHROUGH
    case ENGINE_EINVAL: // FALLTHROUGH
    case ENGINE_ENOTSUP: // FALLTHROUGH
    case ENGINE_EWOULDBLOCK: // FALLTHROUGH
    case ENGINE_E2BIG: // FALLTHROUGH
    case ENGINE_WANT_MORE: // FALLTHROUGH
    case ENGINE_DISCONNECT: // FALLTHROUGH
    case ENGINE_NOT_MY_VBUCKET: // FALLTHROUGH
    case ENGINE_TMPFAIL: // FALLTHROUGH
    case ENGINE_ERANGE: // FALLTHROUGH
    case ENGINE_ROLLBACK: // FALLTHROUGH
    case ENGINE_EBUSY: // FALLTHROUGH
    case ENGINE_DELTA_BADVAL: // FALLTHROUGH
    case ENGINE_FAILED:
        return code;

    case ENGINE_LOCKED:
        return ENGINE_KEY_EEXISTS;
    case ENGINE_LOCKED_TMPFAIL:
        return ENGINE_TMPFAIL;
    case ENGINE_UNKNOWN_COLLECTION:
        return isCollectionsSupported() ? code : ENGINE_EINVAL;

    case ENGINE_EACCESS:break;
    case ENGINE_NO_BUCKET:break;
    case ENGINE_AUTH_STALE:break;
    }

    // Seems like the rest of the components in our system isn't
    // prepared to receive access denied or authentincation stale.
    // For now we should just disconnect them
    auto errc = cb::make_error_condition(cb::engine_errc(code));
    LOG_NOTICE(nullptr,
               "%u - Client %s not aware of extended error code (%s). Disconnecting",
               getId(), getDescription().c_str(), errc.message().c_str());


    return ENGINE_DISCONNECT;
}

void Connection::resetUsernameCache() {
    static const char unknown[] = "unknown";
    const void* unm = unknown;

    if (cbsasl_getprop(sasl_conn.get(),
                       CBSASL_USERNAME, &unm) != CBSASL_OK) {
        unm = unknown;
    }

    username.assign(reinterpret_cast<const char*>(unm));

    domain = cb::sasl::get_domain(sasl_conn.get());

    updateDescription();
}

void Connection::updateDescription() {
    description.assign("[ " + getPeername() + " - " + getSockname());
    if (authenticated) {
        description += " (";
        if (isInternal()) {
            description += "System, ";
        }
        description += getUsername();

        if (domain == cb::sasl::Domain::External) {
            description += " (LDAP)";
        }
        description += ")";
    } else {
        description += " (not authenticated)";
    }
    description += " ]";
}

void Connection::setBucketIndex(int bucketIndex) {
    Connection::bucketIndex.store(bucketIndex, std::memory_order_relaxed);

    if (bucketIndex < 0) {
        // The connection objects which listens to the ports to accept
        // use a bucketIndex of -1. Those connection objects should
        // don't need an entry
        return;
    }

    // Update the privilege context. If a problem occurs within the RBAC
    // module we'll assign an empty privilege context to the connection.
    try {
        if (authenticated) {
            // The user have logged in, so we should create a context
            // representing the users context in the desired bucket.
            privilegeContext = cb::rbac::createContext(username,
                                                       all_buckets[bucketIndex].name);
        } else if (strcmp("default", all_buckets[bucketIndex].name) == 0) {
            // We've just connected to the _default_ bucket, _AND_ the client
            // is unknown.
            // Personally I think the "default bucket" concept is a really
            // really bad idea, but we need to be backwards compatible for
            // a while... lets look up a profile named "default" and
            // assign that. It should only contain access to the default
            // bucket.
            privilegeContext = cb::rbac::createContext("default",
                                                       all_buckets[bucketIndex].name);
        } else {
            // The user has not authenticated, and this isn't for the
            // "default bucket". Assign an empty profile which won't give
            // you any privileges.
            privilegeContext = cb::rbac::PrivilegeContext{};
        }
    } catch (const cb::rbac::Exception &exception) {
        privilegeContext = cb::rbac::PrivilegeContext{};
    }

    if (bucketIndex == 0) {
        // If we're connected to the no bucket we should return
        // no bucket instead of EACCESS. Lets give the connection all
        // possible bucket privileges
        privilegeContext.setBucketPrivileges();
    }

    LOG_DEBUG(nullptr, "RBAC: %u %s switch privilege context %s",
              getId(), getDescription().c_str(),
              privilegeContext.to_string().c_str());
}
