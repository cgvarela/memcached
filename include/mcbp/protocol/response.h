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

#include "config.h"

#include <mcbp/protocol/magic.h>
#include <platform/sized_buffer.h>

#include <cstdint>

namespace cb {
namespace mcbp {

/**
 * Definition of the header structure for a response packet.
 * See section 2
 */
struct Response {
    uint8_t magic;
    uint8_t opcode;
    uint16_t keylen;
    uint8_t extlen;
    uint8_t datatype;
    uint16_t status;
    uint32_t bodylen;
    uint32_t opaque;
    uint64_t cas;

    // Convenience methods to get/set the various fields in the header (in
    // the correct byteorder)

    uint16_t getKeylen() const {
        return ntohs(keylen);
    }

    void setKeylen(uint16_t value) {
        keylen = htons(value);
    }

    uint16_t getStatus() const {
        return ntohs(status);
    }

    void setStatus(uint16_t value) {
        status = htons(value);
    }

    uint32_t getBodylen() const {
        return ntohl(bodylen);
    }

    void setBodylen(uint32_t value) {
        bodylen = htonl(value);
    }

    uint64_t getCas() const {
        return ntohll(cas);
    }

    void setCas(uint64_t val) {
        cas = htonll(val);
    }

    cb::const_byte_buffer getKey() {
        return {reinterpret_cast<const uint8_t*>(this) + sizeof(*this) + extlen,
                getKeylen()};
    }

    cb::const_byte_buffer getExtdata() {
        return {reinterpret_cast<const uint8_t*>(this) + sizeof(*this), extlen};
    }

    cb::const_byte_buffer getValue() {
        const auto buf = getKey();
        return {buf.data() + buf.size(), getBodylen() - getKeylen() - extlen};
    }

    /**
     * Validate that the header is "sane" (correct magic, and extlen+keylen
     * doesn't exceed the body size)
     */
    bool validate() {
        auto m = Magic(magic);
        if (m != Magic::ClientResponse && m != Magic::ServerResponse) {
            return false;
        }

        return (size_t(extlen) + size_t(getKeylen()) <= size_t(getBodylen()));
    }
};

static_assert(sizeof(Response) == 24, "Incorrect compiler padding");

} // namespace mcbp
} // namespace cb
