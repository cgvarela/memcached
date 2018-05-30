/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#include "config.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "utilities/engine_loader.h"
#include <memcached/types.h>
#include <platform/cb_malloc.h>
#include <platform/platform.h>

static const char * const feature_descriptions[] = {
    "compare and swap",
    "persistent storage",
    "secondary engine",
    "access control",
    "multi tenancy",
    "LRU",
    "vbuckets",
    "datatype",
    "item iovector"
};

struct engine_reference {

    /* Union hack to remove a warning from C99 */
    union my_create_u {
        CREATE_INSTANCE create;
        void* voidptr;
    } my_create_instance;

    union my_destroy_u {
        DESTROY_ENGINE destroy;
        void* voidptr;
    } my_destroy_engine;

    cb_dlhandle_t handle;
};

void unload_engine(engine_reference* engine)
{
    if (engine->handle != NULL) {
        (*engine->my_destroy_engine.destroy)();
        cb_dlclose(engine->handle);
        cb_free(engine);
    }
}

static void* find_symbol(cb_dlhandle_t handle, const char* function, char** errmsg) {
    return cb_dlsym(handle, function, errmsg);
}

engine_reference* load_engine(const char* soname,
                              const char* create_function,
                              const char* destroy_function,
                              EXTENSION_LOGGER_DESCRIPTOR* logger)
{
    void *create_symbol = NULL;
    void *destroy_symbol = NULL;
    char *errmsg = NULL, *create_errmsg = NULL, *destroy_errmsg = NULL;
    const char* const create_functions[] = { "create_instance",
                                             "create_default_engine_instance",
                                             "create_ep_engine_instance",
                                             NULL };
    const char* const destroy_functions[] = { "destroy_engine", NULL };

    cb_dlhandle_t handle = cb_dlopen(soname, &errmsg);

    if (handle == NULL) {
        logger->log(EXTENSION_LOG_WARNING, NULL,
                    "Failed to open library \"%s\": %s\n", soname, errmsg);
        cb_free(errmsg);
        return NULL;
    }

    if (create_function) {
        create_symbol = find_symbol(handle, create_function, &create_errmsg);
    } else {
        int ii = 0;
        while (create_functions[ii] != NULL && create_symbol == NULL) {
            create_symbol = find_symbol(handle, create_functions[ii], &create_errmsg);
        }
    }

    if (destroy_function) {
        destroy_symbol = find_symbol(handle, destroy_function, &destroy_errmsg);
    } else {
        int ii = 0;
        while (destroy_functions[ii] != NULL && destroy_symbol == NULL) {
            destroy_symbol = find_symbol(handle, destroy_functions[ii], &destroy_errmsg);
        }
    }

    if (create_symbol == NULL) {
        logger->log(EXTENSION_LOG_WARNING, NULL,
                "Could not find the function to create an engine instance in %s: %s\n",
                soname, create_errmsg);
        cb_free(create_errmsg);
        return NULL;
    }

    if (destroy_symbol == NULL) {
        logger->log(EXTENSION_LOG_WARNING, NULL,
                "Could not find the function to destroy the engine in %s: %s\n",
                soname, destroy_errmsg);
        cb_free(destroy_errmsg);
        return NULL;
    }

    // dlopen is success and we found all required symbols.
    engine_reference* engine_ref = static_cast<engine_reference*>
        (cb_calloc(1, sizeof(engine_reference)));
    engine_ref->handle = handle;
    engine_ref->my_create_instance.voidptr = create_symbol;
    engine_ref->my_destroy_engine.voidptr = destroy_symbol;
    return engine_ref;
}

bool create_engine_instance(engine_reference* engine_ref,
                            SERVER_HANDLE_V1 *(*get_server_api)(void),
                            EXTENSION_LOGGER_DESCRIPTOR *logger,
                            ENGINE_HANDLE **engine_handle) {
    ENGINE_HANDLE* engine = NULL;

    /* request a instance with protocol version 1 */
    ENGINE_ERROR_CODE error = (*engine_ref->my_create_instance.create)(1, get_server_api, &engine);

    if (error != ENGINE_SUCCESS || engine == NULL) {
        logger->log(EXTENSION_LOG_WARNING, NULL,
                "Failed to create instance. Error code: %d\n", error);
        return false;
    }
    *engine_handle = engine;
    return true;
}

static void logit(EXTENSION_LOGGER_DESCRIPTOR *logger, const char* field) {
    logger->log(EXTENSION_LOG_WARNING, nullptr,
                "Failed to initialize engine, missing implementation for %s",
                field);
}

static bool validate_engine_interface(const ENGINE_HANDLE_V1* v1,
                                      EXTENSION_LOGGER_DESCRIPTOR *logger) {
    bool ret = true;
#define check(a) if (v1->a == nullptr) { logit(logger, #a); ret = false; }

    check(get_info);
    check(initialize);
    check(destroy);
    check(allocate);
    check(allocate_ex);
    check(remove);
    check(release);
    check(get);
#if 0
    // We can't enforce this until I've merged the ep-engine patch
    check(get_if);
#endif
    check(get_locked);
    check(unlock);
    check(store);
    check(flush);
    check(get_stats);
    check(reset_stats);
    check(item_set_cas);
    check(get_item_info);
    check(set_item_info);
#undef check

    return ret;
}


bool init_engine_instance(ENGINE_HANDLE *engine,
                          const char *config_str,
                          EXTENSION_LOGGER_DESCRIPTOR *logger)
{
    ENGINE_ERROR_CODE error;

    if (engine->interface == 1) {
        auto engine_v1 = reinterpret_cast<ENGINE_HANDLE_V1*>(engine);
        if (!validate_engine_interface(engine_v1, logger)) {
            // error already logged
            return false;
        }

        error = engine_v1->initialize(engine, config_str);
        if (error != ENGINE_SUCCESS) {
            engine_v1->destroy(engine, false);
            cb::engine_error err{cb::engine_errc(error),
                                 "Failed to initialize instance"};
            logger->log(EXTENSION_LOG_WARNING, nullptr, "%s", err.what());
            return false;
        }
    } else {
        logger->log(EXTENSION_LOG_WARNING, nullptr,
                    "Unsupported interface level %" PRIu64, engine->interface);
        return false;
    }
    return true;
}

void log_engine_details(ENGINE_HANDLE* engine,
                        EXTENSION_LOGGER_DESCRIPTOR* logger)
{
    ENGINE_HANDLE_V1 *engine_v1 = (ENGINE_HANDLE_V1*)engine;
    const engine_info *info;
    info = engine_v1->get_info(engine);
    if (info) {
        ssize_t offset;
        bool comma;
        char message[4096];
        ssize_t nw = snprintf(message, sizeof(message),
                              "Create bucket with engine: %s.",
                              info->description ?
                              info->description : "Unknown");
        if (nw < 0 || nw >= ssize_t(sizeof(message))) {
            return;
        }
        offset = nw;
        comma = false;

        if (info->num_features > 0) {
            unsigned int ii;
            nw = snprintf(message + offset, sizeof(message) - offset,
                          " Supplying the following features: ");
            if (nw < 0 || nw >= ssize_t(sizeof(message) - offset)) {
                return;
            }
            offset += nw;
            for (ii = 0; ii < info->num_features; ++ii) {
                if (info->features[ii].description != NULL) {
                    nw = snprintf(message + offset, sizeof(message) - offset,
                                  "%s%s", comma ? ", " : "",
                                  info->features[ii].description);
                } else {
                    if (info->features[ii].feature <= LAST_REGISTERED_ENGINE_FEATURE) {
                        nw = snprintf(message + offset, sizeof(message) - offset,
                                      "%s%s", comma ? ", " : "",
                                      feature_descriptions[info->features[ii].feature]);
                    } else {
                        nw = snprintf(message + offset, sizeof(message) - offset,
                                      "%sUnknown feature: %d", comma ? ", " : "",
                                      info->features[ii].feature);
                    }
                }
                comma = true;
                if (nw < 0 || nw >= ssize_t(sizeof(message) - offset)) {
                    return;
                }
                offset += nw;
            }
        }
        logger->log(EXTENSION_LOG_NOTICE, NULL, "%s", message);
    } else {
        logger->log(EXTENSION_LOG_NOTICE, NULL, "Create bucket of unknown type");
    }
}
