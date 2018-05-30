/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2016 Couchbase, Inc.
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
 * This file contains the interface to the SCRAM-SHA1, SCRAM-SHA256 and
 * SCRAM-512 support.
 *
 * SCRAM is defined in https://www.ietf.org/rfc/rfc5802.txt
 *
 * The current implementation does not support channel binding (so we
 * don't advertise the -PLUS)
 */

#include <array>
#include <vector>
#include <iostream>
#include "cbsasl/cbsasl.h"
#include "cbsasl/cbsasl_internal.h"
#include "cbsasl/user.h"

#define MECH_NAME_SCRAM_SHA512 "SCRAM-SHA512"
#define MECH_NAME_SCRAM_SHA256 "SCRAM-SHA256"
#define MECH_NAME_SCRAM_SHA1 "SCRAM-SHA1"

class ScramShaBackend : public MechanismBackend {
protected:
    ScramShaBackend(const std::string& mech_name,
                    cbsasl_conn_t& conn,
                    const Mechanism& mech,
                    const cb::crypto::Algorithm algo)
        : MechanismBackend(mech_name, conn),
          mechanism(mech),
          algorithm(algo)
    {

    }

    /**
     * Add a property to the message list according to
     * https://www.ietf.org/rfc/rfc5802.txt section 5.1
     *
     * The purpose of these conversion functions is that we want to
     * make sure that we enforce the right format on the various attributes
     * and that we detect illegal keys.
     *
     * @param out the destination stream
     * @param key the key to add
     * @param value the string representation of the attribute to add
     * @param more set to true if we should add a trailing comma (more data
     *             follows)
     */
    void addAttribute(std::ostream& out, char key, const std::string& value,
                      bool more);

    /**
     * Add a property to the message list according to
     * https://www.ietf.org/rfc/rfc5802.txt section 5.1
     *
     * The purpose of these conversion functions is that we want to
     * make sure that we enforce the right format on the various attributes
     * and that we detect illegal keys.
     *
     * @param out the destination stream
     * @param key the key to add
     * @param value the integer value of the attribute to add
     * @param more set to true if we should add a trailing comma (more data
     *             follows)
     */
    void addAttribute(std::ostream& out, char key, int value,
                      bool more);

    std::string getServerSignature();

    std::string getClientProof();

    virtual void getSaltedPassword(std::vector<uint8_t>& dest) = 0;

    /**
     * Get the AUTH message (as specified in the RFC)
     */
    std::string getAuthMessage();

    std::string client_first_message;
    std::string client_first_message_bare;
    std::string client_final_message;
    std::string client_final_message_without_proof;
    std::string server_first_message;
    std::string server_final_message;

    std::string username;

    std::string clientNonce;
    std::string serverNonce;
    std::string nonce;

    Mechanism mechanism;
    const cb::crypto::Algorithm algorithm;
};

/**
 * The base class responsible for SCRAM-SHA authentication on the server.
 * To make it easy to add support for multiple SHA versions (1, 256, 512)
 * this is an abstract class with pure virtual functions for all methods
 * needed to get the data who needs the underlying SHA stuff
 */
class ScramShaServerBackend : public ScramShaBackend {
public:
    ScramShaServerBackend(const std::string& mech_name,
                          cbsasl_conn_t& conn,
                          const Mechanism& mech,
                          const cb::crypto::Algorithm algo);

    virtual cbsasl_error_t start(const char* input,
                                 unsigned inputlen, const char** output,
                                 unsigned* outputlen) override;

    virtual cbsasl_error_t step(const char* input,
                                unsigned inputlen, const char** output,
                                unsigned* outputlen) override;

    virtual void getSaltedPassword(std::vector<uint8_t>& dest) override {
        const auto& pw = user.getPassword(mechanism).getPassword();
        std::copy(pw.begin(), pw.end(), std::back_inserter(dest));
    }

    cb::sasl::User user;
};

/**
 * Concrete implementation of the class that provides SCRAM-SHA1
 */
class ScramSha1ServerBackend : public ScramShaServerBackend {
public:
    ScramSha1ServerBackend(cbsasl_conn_t& conn)
        : ScramShaServerBackend(MECH_NAME_SCRAM_SHA1,
                                conn,
                                Mechanism::SCRAM_SHA1,
                                cb::crypto::Algorithm::SHA1) { }
};

/**
 * Concrete implementation of the class that provides SCRAM-SHA256
 */
class ScramSha256ServerBackend : public ScramShaServerBackend {
public:
    ScramSha256ServerBackend(cbsasl_conn_t& conn)
        : ScramShaServerBackend(MECH_NAME_SCRAM_SHA256,
                                conn,
                                Mechanism::SCRAM_SHA256,
                                cb::crypto::Algorithm::SHA256) { }
};

/**
 * Concrete implementation of the class that provides SCRAM-SHA512
 */
class ScramSha512ServerBackend : public ScramShaServerBackend {
public:
    ScramSha512ServerBackend(cbsasl_conn_t& conn)
        : ScramShaServerBackend(MECH_NAME_SCRAM_SHA512,
                                conn,
                                Mechanism::SCRAM_SHA512,
                                cb::crypto::Algorithm::SHA512) { }
};

/**
 * Implementation of the class that provides the client side implementation
 * of the SCRAM-SHA[1,256,512]
 */
class ScramShaClientBackend : public ScramShaBackend {
public:
    ScramShaClientBackend(const std::string& mech_name,
                          cbsasl_conn_t& conn,
                          const Mechanism& mech,
                          const cb::crypto::Algorithm algo);

    virtual cbsasl_error_t start(const char* input,
                                 unsigned inputlen, const char** output,
                                 unsigned* outputlen) override;

    virtual cbsasl_error_t step(const char* input,
                                unsigned inputlen, const char** output,
                                unsigned* outputlen) override;

protected:

    bool generateSaltedPassword(const char *ptr, int len);

    virtual void getSaltedPassword(std::vector<uint8_t>& dest) override {
        if (saltedPassword.empty()) {
            throw std::logic_error("getSaltedPassword called before salted "
                                       "password is initialized");
        }
        std::copy(saltedPassword.begin(), saltedPassword.end(),
                  std::back_inserter(dest));
    }

    std::vector<uint8_t> saltedPassword;
    std::string salt;
    unsigned int iterationCount;
};

class ScramSha1ClientBackend : public ScramShaClientBackend {
public:
    ScramSha1ClientBackend(cbsasl_conn_t& conn)
        : ScramShaClientBackend(MECH_NAME_SCRAM_SHA1,
                                conn,
                                Mechanism::SCRAM_SHA1,
                                cb::crypto::Algorithm::SHA1) { }
};

class ScramSha256ClientBackend : public ScramShaClientBackend {
public:
    ScramSha256ClientBackend(cbsasl_conn_t& conn)
        : ScramShaClientBackend(MECH_NAME_SCRAM_SHA256,
                                conn,
                                Mechanism::SCRAM_SHA256,
                                cb::crypto::Algorithm::SHA256) { }
};

class ScramSha512ClientBackend : public ScramShaClientBackend {
public:
    ScramSha512ClientBackend(cbsasl_conn_t& conn)
        : ScramShaClientBackend(MECH_NAME_SCRAM_SHA512,
                                conn,
                                Mechanism::SCRAM_SHA512,
                                cb::crypto::Algorithm::SHA512) { }
};
