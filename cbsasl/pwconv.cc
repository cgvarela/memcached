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
#include "pwconv.h"
#include "user.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <platform/memorymap.h>
#include <sstream>

void cbsasl_pwconv(std::istream& is, std::ostream& os) {
    unique_cJSON_ptr root(cJSON_CreateObject());
    if (root.get() == nullptr) {
        throw std::bad_alloc();
    }
    auto* users = cJSON_CreateArray();
    if (users == nullptr) {
        throw std::bad_alloc();
    }
    cJSON_AddItemToObject(root.get(), "users", users);

    /* File has lines that are newline terminated.
     * File may have comment lines that must being with '#'.
     * Lines should look like...
     *   <NAME><whitespace><PASSWORD>
     */
    std::vector<char> up(1024);

    while (is.getline(up.data(), up.size()).good()) {
        if (up.front() == '#') {
            // comment line
            continue;
        }

        std::string username(up.data());
        // strip off potential carrige returns
        auto index = username.find('\r');
        if (index != username.npos) {
            username.resize(index);
        }

        if (username.empty()) {
            // empty line
            continue;
        }

        std::string password;
        index = username.find(' ');
        if (index != username.npos) {
            password = username.substr(index + 1);
            username.resize(index);
        }

        logging::log(logging::Level::Password,
                     "Create user entry for [" + username +
                             "] with password [" + password + "]");

        auto u = cb::sasl::UserFactory::create(username, password);
        cJSON_AddItemToArray(users, u.to_json().release());
    }

    os << to_string(root, true) << std::endl;
}

void cbsasl_pwconv(const std::string& ifile, const std::string& ofile) {
    std::stringstream inputstream(cbsasl_read_password_file(ifile));
    std::stringstream outputstream;

    cbsasl_pwconv(inputstream, outputstream);

    cbsasl_write_password_file(ofile, outputstream.str());
}

std::string cbsasl_read_password_file(const std::string& filename) {
    if (filename == "-") {
        std::string contents;
        try {
            contents.assign(std::istreambuf_iterator<char>{std::cin},
                            std::istreambuf_iterator<char>());
        } catch (const std::exception& ex) {
            std::cerr << "Failed to read password database from stdin: "
                      << ex.what() << std::endl;
            exit(EXIT_FAILURE);
        }
        return contents;
    }

    cb::MemoryMappedFile map(filename.c_str(),
                             cb::MemoryMappedFile::Mode::RDONLY);
    map.open();
    std::string ret(reinterpret_cast<char*>(map.getRoot()),
                    map.getSize());
    map.close();

    // The password file may be encrypted
    auto* env = getenv("COUCHBASE_CBSASL_SECRETS");
    if (env == nullptr) {
        return ret;
    } else {
        unique_cJSON_ptr json(cJSON_Parse(env));
        if (json.get() == nullptr) {
            throw std::runtime_error("cbsasl_read_password_file: Invalid json"
                                         " specified in COUCHBASE_CBSASL_SECRETS");
        }

        auto decoded = cb::crypto::decrypt(json.get(),
                                           reinterpret_cast<const uint8_t*>(ret.data()),
                                           ret.size());

        return std::string{(const char*)decoded.data(), decoded.size()};
    }
}

void cbsasl_write_password_file(const std::string& filename,
                                const std::string& content) {
    if (filename == "-") {
        std::cout.write(content.data(), content.size());
        std::cout.flush();
        return;
    }

    std::ofstream of(filename, std::ios::binary);

    auto* env = getenv("COUCHBASE_CBSASL_SECRETS");
    if (env == nullptr) {
        of.write(content.data(), content.size());
    } else {
        unique_cJSON_ptr json(cJSON_Parse(env));
        if (json.get() == nullptr) {
            throw std::runtime_error("cbsasl_write_password_file: Invalid json"
                                         " specified in COUCHBASE_CBSASL_SECRETS");
        }
        auto enc = cb::crypto::encrypt(json.get(),
                                       reinterpret_cast<const uint8_t*>(content.data()),
                                       content.size());
        of.write((const char*)enc.data(), enc.size());
    }

    of.flush();
    of.close();
}
