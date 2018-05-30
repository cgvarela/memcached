/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2017 Couchbase, Inc.
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

/**
 * This file contains the definitions of the privilege system used
 * by the memcached core. For more information see rbac.md in the
 * docs directory.
 */
#include <cJSON.h>
#include <memcached/rbac/privileges.h>

#include <cbsasl/cbsasl.h>
#include <bitset>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace cb {
namespace rbac {

/**
 * An array containing all of the possible privileges we've got. It is
 * tightly coupled with the Privilege enum class, and when entries is
 * added to the Privilege enum class the size of the mask needs to
 * be updated.
 */
using PrivilegeMask = std::bitset<size_t(Privilege::Impersonate) + 1>;

/**
 * The UserEntry object is in an in-memory representation of the per-user
 * privileges.
 */
class RBAC_PUBLIC_API UserEntry {
public:
    /**
     * Create a new UserEntry from the provided JSON
     *
     * @param json A JSON representation of the user.
     * @throws std::invalid_argument if the provided JSON isn't according
     *         to the specification.
     * @throws std::bad_alloc if we run out of memory
     */
    UserEntry(const cJSON& json);

    /**
     * Get a map containing all of the buckets and the privileges in those
     * buckets that the user have access to.
     */
    const std::unordered_map<std::string, PrivilegeMask>& getBuckets() const {
        return buckets;
    }

    /**
     * Get all of the "global" (not related to a bucket) privileges the user
     * have in its effective set.
     */
    const PrivilegeMask& getPrivileges() const {
        return privileges;
    }

    /**
     * Get the domain where the user is defined.
     */
    cb::sasl::Domain getDomain() const {
        return domain;
    }

    /**
     * Is this a system internal user or not? A system internal user is a
     * user one of the system components use.
     */
    bool isInternal() const {
        return internal;
    }

protected:
    /**
     * Parse a JSON array containing a set of privileges.
     *
     * @param priv The JSON array to parse
     * @param buckets Set to true if this is for the bucket list (which
     *                will mask out some of the privileges you can't
     *                specify for a bucket)
     * @return A vector of all of the privileges found in the specified JSON
     */
    PrivilegeMask parsePrivileges(const cJSON* priv, bool buckets);

    std::unordered_map<std::string, PrivilegeMask> buckets;
    PrivilegeMask privileges;
    cb::sasl::Domain domain;
    bool internal;
};

/**
 * The PrivilegeContext is the current context (selected bucket).
 * The reason for this class is to provide a fast lookup for all
 * of the privileges. It is used (possibly multiple times) for every
 * command being executed.
 */
class RBAC_PUBLIC_API PrivilegeContext {
public:
    /**
     * Create a new (empty) instance of the privilege context.
     *
     * The generation is set to "max" which will cause the the access
     * check to return stale if being used. This is the initial
     * context being used.
     */
    PrivilegeContext()
        : generation(std::numeric_limits<uint32_t>::max()), mask() {
    }

    /**
     * Create a new instance of the privilege context from the
     * given generation and assign it the given mask.
     *
     * @param gen the generation of the privilege database
     * @param m the mask to set it to.
     */
    PrivilegeContext(uint32_t gen, const PrivilegeMask& m)
        : generation(gen), mask(m) {
        // empty
    }

    /**
     * Check if the given privilege is part of the context
     *
     * @param privilege the privilege to check
     * @return if access is granted or not.
     */
    PrivilegeAccess check(Privilege privilege) const;

    /**
     * Get the generation of the Privilege Database this context maps
     * to. If there is a mismatch with this number and the current number
     * of the privilege database this context is no longer valid.
     */
    uint32_t getGeneration() const {
        return generation;
    }

    /**
     * Get a textual representation of this object in the format:
     *
     *   [privilege,privilege,privilege]
     *
     * An empty set is written as [none], and a full set is written
     * as [all].
     */
    std::string to_string() const;

    /**
     * Clear all of the privileges in this context which contains
     * bucket privileges.
     */
    void clearBucketPrivileges();

    /**
     * Set all of the privileges in this context which contains
     * bucket privileges.
     */
    void setBucketPrivileges();

protected:
    uint32_t generation;
    PrivilegeMask mask;
};

/**
 * Base class for exceptions thrown by the cb::rbac module in
 * case you want to handle all of them with the same catch block.
 */
class RBAC_PUBLIC_API Exception : public std::runtime_error {
protected:
    Exception(const char* msg) : std::runtime_error(msg) {
    }
};

/**
 * An exception class representing that the user doesn't exist in the
 * PrivilegeDatabase.
 */
class RBAC_PUBLIC_API NoSuchUserException : public Exception {
public:
    NoSuchUserException(const char* msg) : Exception(msg) {
    }
};

/**
 * An exception class representing that the bucket doesn't exist in the
 * PrivilegeDatabase.
 */
class RBAC_PUBLIC_API NoSuchBucketException : public Exception {
public:
    NoSuchBucketException(const char* msg) : Exception(msg) {
    }
};

/**
 * The PrivilegeDatabase is a container for all of the RBAC configuration
 * of the system.
 */
class RBAC_PUBLIC_API PrivilegeDatabase {
public:
    /**
     * Create a new instance of the PrivilegeDatabase and initialize
     * it to the provided JSON
     *
     * @param json A JSON representation of the privilege database as
     *             specified above (or null to create an empty database)
     * @throws std::invalid_argument for invalid syntax
     * @throws std::bad_alloc if we run out of memory
     */
    PrivilegeDatabase(const cJSON* json);

    /**
     * Try to look up a user in the privilege database
     *
     * @param user The name of the user to look up
     * @param domain The domain where the user is defined (not used)
     * @return The user entry for that user
     * @throws cb::rbac::NoSuchUserException if the user doesn't exist
     */
    const UserEntry& lookup(const std::string& user) const {
        auto iter = userdb.find(user);
        if (iter == userdb.cend()) {
            throw NoSuchUserException(user.c_str());
        }

        return iter->second;
    }

    /**
     * Check if the provided context contains the requested privilege
     *
     * @param context The privilege context for the user
     * @param privilege The privilege to check
     * @return PrivilegeAccess::Stale If the context was created by a
     *                                different generation of the database
     *         PrivilegeAccess::Ok If the context contains the privilege
     *         PrivilegeAccess::Fail If the context lacks the privilege
     */
    PrivilegeAccess check(const PrivilegeContext& context,
                          Privilege privilege) {
        if (context.getGeneration() != generation) {
            return PrivilegeAccess::Stale;
        }

        return context.check(privilege);
    }

    /**
     * Create a new PrivilegeContext for the specified user in the specified
     * bucket.
     *
     * @param user The name of the user
     * @param bucket The name of the bucket (may be "" if you're not
     *               connecting to a bucket (aka the no bucket)).
     * @return The privilege context representing the user in that bucket
     * @throws cb::rbac::NoSuchUserException if the user doesn't exist
     * @throws cb::rbac::NoSuchBucketException if the user doesn't have access
     *                                         to that bucket.
     */
    PrivilegeContext createContext(const std::string& user,
                                   const std::string& bucket) const;

    /**
     * Create the initial context for a given user
     *
     * @param user The username to look up
     * @param domain The domain where the user exists
     * @return A pair with a privilege context as the first element, and
     *         a boolean indicating if this is a system user as the second
     *         element.
     * @throws cb::rbac::NoSuchUserException if the user doesn't exist
     */
    std::pair<PrivilegeContext, bool> createInitialContext(
            const std::string& user, cb::sasl::Domain domain);

    /**
     * The generation for this PrivilegeDatabase (a privilege context must
     * match this generation in order to be valid)
     */
    const uint32_t generation;

protected:
    std::unordered_map<std::string, UserEntry> userdb;
};

/**
 * Create a new PrivilegeContext for the specified user in the specified
 * bucket.
 *
 * @todo this might starve the writers?
 *
 * @param user The name of the user
 * @param bucket The name of the bucket (may be "" if you're not
 *               connecting to a bucket (aka the no bucket)).
 * @return The privilege context representing the user in that bucket
 * @throws cb::rbac::NoSuchUserException if the user doesn't exist
 * @throws cb::rbac::NoSuchBucketException if the user doesn't have access
 *                                         to that bucket.
 */
RBAC_PUBLIC_API
PrivilegeContext createContext(const std::string& user,
                               const std::string& bucket);

/**
 * Create the initial context for a given user
 *
 * @param user The username to look up
 * @param domain The domain where the user exists
 * @return A pair with a privilege context as the first element, and
 *         a boolean indicating if this is a system user as the second
 *         element.
 * @throws cb::rbac::NoSuchUserException if the user doesn't exist
 */
RBAC_PUBLIC_API
std::pair<PrivilegeContext, bool> createInitialContext(const std::string& user,
                                                       cb::sasl::Domain domain);

/**
 * Load the named file and install it as the current privilege database
 *
 * @param filename the name of the new file
 * @throws std::runtime_error
 */
RBAC_PUBLIC_API
void loadPrivilegeDatabase(const std::string& filename);

/**
 * Check if the specified user have access to the specified bucket
 */
RBAC_PUBLIC_API
bool mayAccessBucket(const std::string& user, const std::string& bucket);

/**
 * Initialize the RBAC module
 */
RBAC_PUBLIC_API
void initialize();

/**
 * Destroy the RBAC module
 */
RBAC_PUBLIC_API
void destroy();
}
}
