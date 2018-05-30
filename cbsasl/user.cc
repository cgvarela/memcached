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

#include "config.h"
#include "user.h"
#include "cbsasl_internal.h"

#include <atomic>
#include <iterator>
#include <platform/base64.h>
#include <platform/random.h>
#include <stdexcept>
#include <string>
#include <memory>

std::atomic<int> IterationCount(4096);

/**
 * Generate a salt and store it base64 encoded into the salt
 */
void generateSalt(std::vector<uint8_t>& bytes, std::string& salt) {
    Couchbase::RandomGenerator randomGenerator(true);

    if (!randomGenerator.getBytes(bytes.data(), bytes.size())) {
        throw std::runtime_error("Failed to get random bytes");
    }

    using Couchbase::Base64::encode;

    salt = encode(std::string(reinterpret_cast<char*>(bytes.data()),
                              bytes.size()));
}

cb::sasl::User cb::sasl::UserFactory::create(const std::string& unm,
                                               const std::string& passwd) {
    User ret{unm, false};

    struct {
        cb::crypto::Algorithm algoritm;
        Mechanism mech;
    } algo_info[] = {
        {
            cb::crypto::Algorithm::SHA1,
            Mechanism::SCRAM_SHA1
        }, {
            cb::crypto::Algorithm::SHA256,
            Mechanism::SCRAM_SHA256
        }, {
            cb::crypto::Algorithm::SHA512,
            Mechanism::SCRAM_SHA512
        }
    };

    // The format of the plain password encoding is that we're appending the
    // generated hmac to the salt (which should be 16 bytes). This makes
    // our plain text password generation compatible with ns_server
    std::vector<uint8_t> pwentry(16);
    std::string saltstring;
    generateSalt(pwentry, saltstring);
    std::vector<uint8_t> pw;
    std::copy(passwd.begin(), passwd.end(), std::back_inserter(pw));
    const auto hmac = cb::crypto::HMAC(cb::crypto::Algorithm::SHA1, pwentry, pw);
    std::copy(hmac.begin(), hmac.end(), std::back_inserter(pwentry));
    std::string hash{(const char*)pwentry.data(), pwentry.size()};

    ret.password[Mechanism::PLAIN] = User::PasswordMetaData{hash};

    for (const auto& info : algo_info) {
        if (cb::crypto::isSupported(info.algoritm)) {
            ret.generateSecrets(info.mech, passwd);
        }
    }

    return ret;
}

cb::sasl::User cb::sasl::UserFactory::createDummy(const std::string& unm,
                                                    const Mechanism& mech) {
    User ret{unm};

    std::vector<uint8_t> salt;
    std::string passwd;

    switch (mech) {
    case Mechanism::SCRAM_SHA512:
        salt.resize(cb::crypto::SHA512_DIGEST_SIZE);
        break;
    case Mechanism::SCRAM_SHA256:
        salt.resize(cb::crypto::SHA256_DIGEST_SIZE);
        break;
    case Mechanism::SCRAM_SHA1:
        salt.resize(cb::crypto::SHA1_DIGEST_SIZE);
        break;
    case Mechanism::PLAIN:
    case Mechanism::UNKNOWN:
        throw std::logic_error("cb::cbsasl::UserFactory::createDummy invalid algorithm");
    }

    if (salt.empty()) {
        throw std::logic_error("cb::cbsasl::UserFactory::createDummy invalid algorithm");
    }

    // Generate a random password
    generateSalt(salt, passwd);

    // Generate the secrets by using that random password
    ret.generateSecrets(mech, passwd);

    return ret;
}

cb::sasl::User cb::sasl::UserFactory::create(const cJSON* obj) {
    if (obj == nullptr) {
        throw std::runtime_error("cb::cbsasl::UserFactory::create: obj cannot be null");
    }

    if (obj->type != cJSON_Object) {
        throw std::runtime_error("cb::cbsasl::UserFactory::create: Invalid object type");
    }

    auto* o = cJSON_GetObjectItem(const_cast<cJSON*>(obj), "n");
    if (o == nullptr) {
        throw std::runtime_error("cb::cbsasl::UserFactory::create: missing mandatory label 'n'");
    }
    if (o->type != cJSON_String) {
        throw std::runtime_error("cb::cbsasl::UserFactory::create: 'n' must be a string");
    }

    User ret{o->valuestring, false};

    for (o = obj->child; o != nullptr; o = o->next) {
        std::string label(o->string);
        if (label == "n") {
            // skip. we've already processed this
        } else if (label == "sha512") {
            User::PasswordMetaData pd(o);
            ret.password[Mechanism::SCRAM_SHA512] = pd;
        } else if (label == "sha256") {
            User::PasswordMetaData pd(o);
            ret.password[Mechanism::SCRAM_SHA256] = pd;
        } else if (label == "sha1") {
            User::PasswordMetaData pd(o);
            ret.password[Mechanism::SCRAM_SHA1] = pd;
        } else if (label == "plain") {
            User::PasswordMetaData pd(Couchbase::Base64::decode(o->valuestring));
            ret.password[Mechanism::PLAIN] = pd;
        } else {
            throw std::runtime_error("cb::cbsasl::UserFactory::create: Invalid "
                                         "label \"" + label + "\" specified");
        }
    }

    return ret;
}

void cb::sasl::UserFactory::setDefaultHmacIterationCount(int count) {
    IterationCount.store(count);
}

void cbsasl_set_hmac_iteration_count(cbsasl_getopt_fn getopt_fn,
                                     void* context) {

    const char* result = nullptr;
    unsigned int result_len;

    if (getopt_fn(context, nullptr, "hmac iteration count", &result,
                  &result_len) == CBSASL_OK) {
        if (result != nullptr) {
            std::string val(result, result_len);
            try {
                IterationCount.store(std::stoi(val));
            } catch (...) {
                logging::log(logging::Level::Error,
                             "Failed to update HMAC iteration count");
            }
        }
    }
}

void cb::sasl::User::generateSecrets(const Mechanism& mech,
                                      const std::string& passwd) {

    std::vector<uint8_t> salt;
    std::string encodedSalt;
    cb::crypto::Algorithm algorithm;

    switch (mech) {
    case Mechanism::SCRAM_SHA512:
        salt.resize(cb::crypto::SHA512_DIGEST_SIZE);
        algorithm = cb::crypto::Algorithm::SHA512;
        break;
    case Mechanism::SCRAM_SHA256:
        salt.resize(cb::crypto::SHA256_DIGEST_SIZE);
        algorithm = cb::crypto::Algorithm::SHA256;
        break;
    case Mechanism::SCRAM_SHA1:
        salt.resize(cb::crypto::SHA1_DIGEST_SIZE);
        algorithm = cb::crypto::Algorithm::SHA1;
        break;
    case Mechanism::PLAIN:
    case Mechanism::UNKNOWN:
        throw std::logic_error("cb::cbsasl::User::generateSecrets invalid algorithm");
    }

    if (salt.empty()) {
        throw std::logic_error("cb::cbsasl::User::generateSecrets invalid algorithm");
    }

    generateSalt(salt, encodedSalt);
    auto digest = cb::crypto::PBKDF2_HMAC(algorithm, passwd, salt, IterationCount);

    password[mech] =
        PasswordMetaData(std::string((const char*)digest.data(), digest.size()),
                         encodedSalt, IterationCount);
}

cb::sasl::User::PasswordMetaData::PasswordMetaData(cJSON* obj) {
    if (obj->type != cJSON_Object) {
        throw std::runtime_error("cb::cbsasl::User::PasswordMetaData: invalid"
                                     " object type");
    }

    auto* h = cJSON_GetObjectItem(obj, "h");
    auto* s = cJSON_GetObjectItem(obj, "s");
    auto* i = cJSON_GetObjectItem(obj, "i");

    if (h == nullptr || s == nullptr || i == nullptr) {
        throw std::runtime_error("cb::cbsasl::User::PasswordMetaData: missing "
                                     "mandatory attributes");
    }

    if (h->type != cJSON_String) {
        throw std::runtime_error("cb::cbsasl::User::PasswordMetaData: hash"
                                     " should be a string");
    }

    if (s->type != cJSON_String) {
        throw std::runtime_error("cb::cbsasl::User::PasswordMetaData: salt"
                                     " should be a string");
    }

    if (i->type != cJSON_Number) {
        throw std::runtime_error("cb::cbsasl::User::PasswordMetaData: iteration"
                                     " count should be a number");
    }

    if (cJSON_GetArraySize(obj) != 3) {
        throw std::runtime_error("cb::cbsasl::User::PasswordMetaData: invalid "
                                     "number of labels specified");
    }

    salt.assign(s->valuestring);
    // validate that we may decode the salt
    Couchbase::Base64::decode(salt);
    password.assign(Couchbase::Base64::decode(h->valuestring));
    iteration_count = i->valueint;
    if (iteration_count < 0) {
        throw std::runtime_error("cb::cbsasl::User::PasswordMetaData: iteration "
                                     "count must be positive");
    }
}

cJSON* cb::sasl::User::PasswordMetaData::to_json() const {
    auto* ret = cJSON_CreateObject();
    std::string s((char*)password.data(), password.size());
    cJSON_AddStringToObject(ret, "h", Couchbase::Base64::encode(s).c_str());
    cJSON_AddStringToObject(ret, "s", salt.c_str());
    cJSON_AddNumberToObject(ret, "i", iteration_count);

    return ret;
}

unique_cJSON_ptr cb::sasl::User::to_json() const {
    auto* ret = cJSON_CreateObject();

    cJSON_AddStringToObject(ret, "n", username.c_str());
    for (auto& e : password) {
        auto* obj = e.second.to_json();
        switch (e.first) {
        case Mechanism::PLAIN:
            cJSON_AddStringToObject(ret, "plain",
                                    cJSON_GetObjectItem(obj, "h")->valuestring);
            cJSON_Delete(obj);
            break;

        case Mechanism::SCRAM_SHA512:
            cJSON_AddItemToObject(ret, "sha512", obj);
            break;

        case Mechanism::SCRAM_SHA256:
            cJSON_AddItemToObject(ret, "sha256", obj);
            break;
        case Mechanism::SCRAM_SHA1:
            cJSON_AddItemToObject(ret, "sha1", obj);
            break;
        default:
            throw std::runtime_error(
                "cb::cbsasl::User::toJSON(): Unsupported mech");
        }
    }

    return unique_cJSON_ptr(ret);
}

std::string cb::sasl::User::to_string() const {
    return ::to_string(to_json(), false);
}

const cb::sasl::User::PasswordMetaData& cb::sasl::User::getPassword(
    const Mechanism& mech) const {

    const auto iter = password.find(mech);

    if (iter == password.end()) {
        throw std::invalid_argument("cb::cbsasl::User::getPassword: requested "
                                        "mechanism not available");
    } else {
        return iter->second;
    }
}
