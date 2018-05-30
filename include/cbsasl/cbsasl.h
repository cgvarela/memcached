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

#pragma once

#include <cbsasl/logging.h>
#include <cbsasl/saslauthd_config.h>
#include <cbsasl/visibility.h>
#include <memory>
#include <string>

typedef enum cbsasl_error {
    CBSASL_OK = 0,
    CBSASL_CONTINUE = 1,
    CBSASL_FAIL = 2,
    CBSASL_NOMEM = 3,
    CBSASL_BADPARAM = 4,
    CBSASL_NOMECH = 5,
    CBSASL_NOUSER = 6,
    CBSASL_PWERR = 7,
    CBSASL_NO_RBAC_PROFILE = 8
} cbsasl_error_t;

typedef struct {
    unsigned long len;
    unsigned char data[1];
} cbsasl_secret_t;

typedef struct {
    unsigned long id;
    int (*proc)(void);
    void* context;
} cbsasl_callback_t;

typedef struct cbsasl_conn_st cbsasl_conn_t;

/**
 * Lists all of the mechanisms this sasl server supports
 *
 * Currently all parameters except result and len is ignored, but provided
 * to maintain compatibility with other SASL implementations.
 *
 * @param conn the connection object that wants to call list mechs. May
 *             be null
 * @param user the user who wants to connect (may restrict the available
 *             mechs). May be null
 * @param prefix the prefix to insert to the resulting string (may be null)
 * @param sep the separator between each mechanism
 * @param suffix the suffix to append to the resulting string (may be null)
 * @param result pointer to where the result is to be stored (allocated
 *               and feed by the library)
 * @param len the length of the resulting string (may be null)
 * @param count the number of mechanisms in the resulting string (may be
 *              null)
 *
 * @return Whether or not an error occured while getting the mechanism list
 */
CBSASL_PUBLIC_API
cbsasl_error_t cbsasl_listmech(cbsasl_conn_t* conn,
                               const char* user,
                               const char* prefix,
                               const char* sep,
                               const char* suffix,
                               const char** result,
                               unsigned* len,
                               int* count);

/**
 * Convert a sasl error code to a textual representation
 *
 * @param conn the connection object who genertated the error code
 * @param error the error value to look up
 * @return pointer to a textual string describing the error. This pointer
 *         is valid as long as the conn object is valid (caller should not
 *         free this pointer)
 */
CBSASL_PUBLIC_API
const char* cbsasl_strerror(cbsasl_conn_t* conn, cbsasl_error_t error);

/**
 * Initializes the sasl server
 *
 * This function initializes the server by loading passwords from the cbsasl
 * password file. This function should only be called once.
 *
 * @param cb the callbacks to use for the server (may be nullptr)
 * @param appname the name of the application (may be nullptr)
 * @return Whether or not the sasl server initialization was successful
 */
CBSASL_PUBLIC_API
cbsasl_error_t cbsasl_server_init(const cbsasl_callback_t* cb,
                                  const char* appname);

/**
 * close and release allocated resources
 *
 * @return SASL_OK upon success
 */
CBSASL_PUBLIC_API
cbsasl_error_t cbsasl_server_term(void);

/**
 * create context for a single SASL connection
 *  @param service registered name of the service using SASL (e.g. "imap")
 *  @param serverFQDN  Fully qualified domain name of server.  NULL means use
 *                    gethostname() or equivalent.
 *                    Useful for multi-homed servers.
 *  @param user_realm permits multiple user realms on server, NULL = default
 *  @param iplocalport server IPv4/IPv6 domain literal string with port
 *                     (if NULL, then mechanisms requiring IPaddr are disabled)
 *  @param ipremoteport client IPv4/IPv6 domain literal string with port
 *                    (if NULL, then mechanisms requiring IPaddr are disabled)
 *  @param callbacks  callbacks (e.g., authorization, lang, new getopt context)
 *  @param flags usage flags (see above)
 *  @param conn where to store the allocated context
 *
 * @returns SASL_OK upon success
 */
CBSASL_PUBLIC_API
cbsasl_error_t cbsasl_server_new(
        const char* service, // may be null
        const char* serverFQDN, // may be null
        const char* user_realm, // may be null
        const char* iplocalport, // may be null
        const char* ipremoteport, // may be null
        const cbsasl_callback_t* callbacks, // may be null
        unsigned int flags,
        cbsasl_conn_t** conn);

/**
 * Creates a sasl connection and begins authentication
 *
 * When a client receives a request for sasl authentication this function is
 * called in order to initialize the sasl connection based on the mechanism
 * specified.
 *
 * @param conn The connection context for this session
 * @param mechanism The mechanism that will be used for authentication
 *
 * @return Whether or not the mecahnism initialization was successful
 */
CBSASL_PUBLIC_API
cbsasl_error_t cbsasl_server_start(cbsasl_conn_t* conn,
                                   const char* mech,
                                   const char* clientin,
                                   unsigned int clientinlen,
                                   const char** serverout,
                                   unsigned int* serveroutlen);

/**
 * Does username/password authentication
 *
 * After the sasl connection is initialized the step function is called to
 * check credentials.
 *
 * @return Whether or not the sasl step was successful
 */
CBSASL_PUBLIC_API
cbsasl_error_t cbsasl_server_step(cbsasl_conn_t* conn,
                                  const char* input,
                                  unsigned inputlen,
                                  const char** output,
                                  unsigned* outputlen);

/**
 * Frees up funushed sasl connections
 *
 * @param conn The sasl connection to free
 */
CBSASL_PUBLIC_API
void cbsasl_dispose(cbsasl_conn_t** pconn);

/**
 * Refresh the internal data (this may result in loading password
 * databases etc)
 *
 * @return Whether or not the operation was successful
 */
CBSASL_PUBLIC_API
cbsasl_error_t cbsasl_server_refresh(void);

typedef enum { CBSASL_USERNAME = 0 } cbsasl_prop_t;

CBSASL_PUBLIC_API
cbsasl_error_t cbsasl_getprop(cbsasl_conn_t* conn,
                              cbsasl_prop_t propnum,
                              const void** pvalue);

/* Client API */

CBSASL_PUBLIC_API
cbsasl_error_t cbsasl_client_new(const char* service,
                                 const char* serverFQDN,
                                 const char* iplocalport,
                                 const char* ipremoteport,
                                 const cbsasl_callback_t* prompt_supp,
                                 unsigned int flags,
                                 cbsasl_conn_t** pconn);

CBSASL_PUBLIC_API
cbsasl_error_t cbsasl_client_start(cbsasl_conn_t* conn,
                                   const char* mechlist,
                                   void** prompt_need,
                                   const char** clientout,
                                   unsigned int* clientoutlen,
                                   const char** mech);

CBSASL_PUBLIC_API
cbsasl_error_t cbsasl_client_step(cbsasl_conn_t* conn,
                                  const char* serverin,
                                  unsigned int serverinlen,
                                  void** not_used,
                                  const char** clientout,
                                  unsigned int* clientoutlen);

/* Callback API supported by cbsasl */
#define CBSASL_CB_LIST_END 0

/**
 * Get the username
 */
typedef int (*cbsasl_get_username_fn)(void* context,
                                      int id,
                                      const char** result,
                                      unsigned int* len);
#define CBSASL_CB_USER 1
/**
 * Get the name to use for authentication
 */
typedef int (*cbsasl_get_authname_fn)(void* context,
                                      int id,
                                      const char** result,
                                      unsigned int* len);
#define CBSASL_CB_AUTHNAME 2
/**
 * Get the password
 */
typedef int (*cbsasl_get_password_fn)(cbsasl_conn_t* conn,
                                      void* context,
                                      int id,
                                      cbsasl_secret_t** psecret);
#define CBSASL_CB_PASS 3

/**
 * Get client nonce (used for testing)
 */
typedef int (*cbsasl_get_cnonce_fn)(void* context,
                                    int id,
                                    const char** result,
                                    unsigned int* len);
#define CBSASL_CB_CNONCE 6

/**
 * Get option
 *
 * @param context specified when callback was registered
 * @param plugin_name the name of the plugin needing the value (may be null)
 * @param option name of the option requested
 * @param result where store the result (must be valid until the next call
 *               to the function, or the sasl function return)
 * @param len where to store the number of bytes in the result
 * @return CBSASL_OK for success, or another cbsasl error code
 */
typedef int (*cbsasl_getopt_fn)(void* context,
                                const char* plugin_name,
                                const char* option,
                                const char** result,
                                unsigned* len);
#define CBSASL_CB_GETOPT 7

struct CbSaslDeleter {
    void operator()(cbsasl_conn_t* conn) {
        if (conn != nullptr) {
            cbsasl_dispose(&conn);
        }
    }
};

typedef std::unique_ptr<cbsasl_conn_t, CbSaslDeleter> unique_cbsasl_conn_t;

namespace cb {
namespace sasl {

/**
 * The Domain enum defines all of the legal states where the users may
 * be defined.
 */
enum class Domain : uint8_t {
    /**
     * The user is defined locally on the node and authenticated
     * through `cbsasl` (or by using SSL certificates)
     */
    Local,
    /**
     * The user is defined somewhere else but authenticated through
     * `saslauthd`
     */
    External
};

CBSASL_PUBLIC_API
Domain to_domain(const std::string& domain);

CBSASL_PUBLIC_API
std::string to_string(Domain domain);

/**
 * Get the domain where the user in the connection object is defined
 */
CBSASL_PUBLIC_API
Domain get_domain(cbsasl_conn_t* conn);

/**
 * Get the uuid used by this connection structure in the logs.
 *
 * If not used, an empty string is returned.
 *
 * The string object returned is part of the conn structure and invalidated
 * when the conn structure is deleted.
 */
CBSASL_PUBLIC_API
std::string& get_uuid(cbsasl_conn_t* conn);

} // namespace sasl
} // namespace cb
