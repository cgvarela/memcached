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
#pragma once

#include  <algorithm>

#include "testapp.h"

enum class TransportProtocols {
    McbpPlain,
    McbpSsl,
    McbpIpv6Plain,
    McbpIpv6Ssl
};

std::ostream& operator << (std::ostream& os, const TransportProtocols& t);
std::string to_string(const TransportProtocols& transport);

class TestappClientTest
    : public TestappTest,
      public ::testing::WithParamInterface<TransportProtocols> {

public:
    TestappClientTest() {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        name.assign(info->test_case_name());
        name.append("_");
        name.append(info->name());
        std::replace(name.begin(), name.end(), '/', '_');
    }

    /**
     * Create an extended attribute
     *
     * This method doesn't really belong in this class (as it is supposed
     * to work for greenstack as well, but we're going to need it from
     * multiple tests so it can might as well live here..
     *
     * @param path the full path to the attribute (including the key)
     * @param value The value to store
     * @param macro is this a macro for expansion or not
     */
    void createXattr(const std::string& path, const std::string& value,
                     bool macro = false) {
        auto& conn = getConnection();
        ASSERT_EQ(Protocol::Memcached, conn.getProtocol());
        auto& connection = dynamic_cast<MemcachedBinprotConnection&>(conn);

        BinprotSubdocCommand cmd;
        cmd.setOp(PROTOCOL_BINARY_CMD_SUBDOC_DICT_ADD);
        cmd.setKey(name);
        cmd.setPath(path);
        cmd.setValue(value);
        if (macro) {
            cmd.addPathFlags(SUBDOC_FLAG_XATTR_PATH | SUBDOC_FLAG_EXPAND_MACROS | SUBDOC_FLAG_MKDIR_P);
        } else {
            cmd.addPathFlags(SUBDOC_FLAG_XATTR_PATH | SUBDOC_FLAG_MKDIR_P);
        }

        connection.sendCommand(cmd);

        BinprotResponse resp;
        connection.recvResponse(resp);
        EXPECT_EQ(PROTOCOL_BINARY_RESPONSE_SUCCESS, resp.getStatus());
    }

    /**
     * Get an extended attribute
     *
     * @param path the full path to the attribute to fetch
     * @return the value stored for the key (it is expected to be there!)
     */
    std::string getXattr(const std::string& path, bool deleted = false) {
        auto& conn = getConnection();
        auto& connection = dynamic_cast<MemcachedBinprotConnection&>(conn);

        BinprotSubdocCommand cmd;
        cmd.setOp(PROTOCOL_BINARY_CMD_SUBDOC_GET);
        cmd.setKey(name);
        cmd.setPath(path);
        if (deleted) {
            cmd.addPathFlags(SUBDOC_FLAG_XATTR_PATH);
            cmd.addDocFlags(mcbp::subdoc::doc_flag::AccessDeleted);
        } else {
            cmd.addPathFlags(SUBDOC_FLAG_XATTR_PATH);
        }
        connection.sendCommand(cmd);

        BinprotSubdocResponse resp;
        connection.recvResponse(resp);
        auto status = resp.getStatus();

        if (deleted && status == PROTOCOL_BINARY_RESPONSE_SUBDOC_SUCCESS_DELETED) {
            status = PROTOCOL_BINARY_RESPONSE_SUCCESS;
        }

        if (status!= PROTOCOL_BINARY_RESPONSE_SUCCESS) {
            throw BinprotConnectionError("getXattr() failed: ", resp);
        }
        return resp.getValue();
    }

    int getResponseCount(protocol_binary_response_status statusCode) {
        unique_cJSON_ptr stats(cJSON_Parse(
                cJSON_GetObjectItem(
                        getConnection().stats("responses detailed").get(),
                        "responses")
                        ->valuestring));
        std::stringstream stream;
        stream << std::hex << statusCode;
        return cJSON_GetObjectItem(stats.get(), stream.str().c_str())->valueint;
    }
    static int statResps() {
        // Each stats call gets a new connection prepared for it, resulting in
        // a HELLO. This means we expect 1 success from the stats call and
        // the number of successes a HELLO takes.
        return 1 + helloResps();
    }

    static int helloResps() {
        // We do a HELLO for each feature that we enable
        // DatatypeJSON, Compression, MutationSeqNo, Xattr, Xerror. Therefore
        // we expect a success for each of the responses.
        return 5;
    }

    static int saslResps() {
        // 2 successes expected due to the initial response and then the
        // continue step.
        return 2;
    }

protected:
    std::string name;

    MemcachedConnection& getConnection() override;
};
