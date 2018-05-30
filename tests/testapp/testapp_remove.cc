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

#include "testapp.h"
#include "testapp_client_test.h"
#include <protocol/connection/client_mcbp_connection.h>

#include <algorithm>
#include <platform/compress.h>

class RemoveTest : public TestappClientTest {
public:

protected:
    void verify_MB_22553(const std::string& config);

    /**
     * Create a document and keep the information about the document in
     * the info member
     */
    void createDocument() {
        Document doc;
        doc.info.cas = mcbp::cas::Wildcard;
        doc.info.datatype = cb::mcbp::Datatype::JSON;
        doc.info.flags = 0xcaffee;
        doc.info.id = name;
        auto content = to_string(memcached_cfg);
        std::copy(content.begin(), content.end(),
                  std::back_inserter(doc.value));
        info = getConnection().mutate(doc, 0, MutationType::Add);
    }

    MutationInfo info;
};

void RemoveTest::verify_MB_22553(const std::string& config) {
    auto& conn = getAdminConnection();
    auto& connection = dynamic_cast<MemcachedBinprotConnection&>(conn);

    std::string name = "bucket-1";
    conn.createBucket(name, config, BucketType::Memcached);
    conn.selectBucket("bucket-1");

    // Create a document
    conn.store(name, 0, std::string{"foobar"});

    // Add an xattr
    {
        BinprotSubdocCommand cmd;
        cmd.setOp(PROTOCOL_BINARY_CMD_SUBDOC_DICT_ADD);
        cmd.setKey(name);
        cmd.setPath("_rbac.attribute");
        cmd.setValue("\"read-only\"");
        cmd.addPathFlags(SUBDOC_FLAG_XATTR_PATH | SUBDOC_FLAG_MKDIR_P);

        connection.sendCommand(cmd);

        BinprotResponse resp;
        connection.recvResponse(resp);
        EXPECT_EQ(PROTOCOL_BINARY_RESPONSE_SUCCESS, resp.getStatus());
    }

    // Delete the document
    conn.remove(name, 0);

    // The document itself should not be accessible MB-22553
    try {
        conn.get(name, 0);
        FAIL() << "Document with XATTRs should not be accessible after remove";
    } catch (const ConnectionError& error) {
        EXPECT_TRUE(error.isNotFound())
                    << "MB-22553: doc with xattr is still accessible";
    }

    {
        // It should not be accessible over subdoc..
        BinprotSubdocCommand cmd;
        cmd.setOp(PROTOCOL_BINARY_CMD_SUBDOC_GET);
        cmd.setKey(name);
        cmd.setPath("verbosity");
        cmd.addPathFlags(SUBDOC_FLAG_NONE);

        connection.sendCommand(cmd);

        BinprotSubdocResponse resp;
        connection.recvResponse(resp);

        EXPECT_EQ(PROTOCOL_BINARY_RESPONSE_KEY_ENOENT, resp.getStatus())
                    << "MB-22553: doc with xattr is still accessible";
    }

    conn.deleteBucket("bucket-1");
    conn.reconnect();
}

INSTANTIATE_TEST_CASE_P(TransportProtocols,
                        RemoveTest,
                        ::testing::Values(TransportProtocols::McbpPlain,
                                          TransportProtocols::McbpIpv6Plain,
                                          TransportProtocols::McbpSsl,
                                          TransportProtocols::McbpIpv6Ssl
                                         ),
                        ::testing::PrintToStringParamName());

/**
 * Verify that remove of an non-existing object work (and return the expected
 * value)
 */
TEST_P(RemoveTest, RemoveNonexisting) {
    auto& conn = getConnection();

    try {
        conn.remove(name, 0);
    } catch (const ConnectionError& error) {
        EXPECT_TRUE(error.isNotFound()) << error.what();
    }
}

/**
 * Verify that remove of an existing document with setting the CAS value
 * to the wildcard works
 */
TEST_P(RemoveTest, RemoveCasWildcard) {
    auto& conn = getConnection();

    createDocument();
    auto deleted = conn.remove(name, 0);
    EXPECT_NE(info.cas, deleted.cas);
}

/**
 * Verify that remove of an existing document with an incorrect value
 * fails with EEXISTS
 */
TEST_P(RemoveTest, RemoveWithInvalidCas) {
    auto& conn = getConnection();

    createDocument();
    try {
        conn.remove(name, 0, info.cas + 1);
        FAIL() << "Invalid cas should return EEXISTS";
    } catch (const ConnectionError& error) {
        EXPECT_TRUE(error.isAlreadyExists()) << error.what();
    }
}

/**
 * Verify that remove of an existing document with the correct CAS
 * value works
 */
TEST_P(RemoveTest, RemoveWithCas) {
    auto& conn = getConnection();

    createDocument();
    auto deleted = conn.remove(name, 0, info.cas);
    EXPECT_NE(info.cas, deleted.cas);
}

/**
 * Verify that you may access system attributes of a deleted
 * document, and that the user attributes will be nuked off
 */
TEST_P(RemoveTest, RemoveWithXattr) {
    createDocument();

    auto& conn = getConnection();
    createXattr("meta.content-type", "\"application/json; charset=utf-8\"");
    createXattr("_rbac.attribute", "\"read-only\"");
    conn.remove(name, 0, 0);

    // The system xattr should have been preserved
    EXPECT_EQ("\"read-only\"", getXattr("_rbac.attribute", true));

    // The user xattr should not be there
    try {
        getXattr("meta.content_type", true);
        FAIL() << "The user xattr should be gone!";
    } catch (const BinprotConnectionError& exp) {
        EXPECT_EQ(PROTOCOL_BINARY_RESPONSE_SUBDOC_PATH_ENOENT,
                  exp.getReason())
                    << memcached_status_2_text(
                        protocol_binary_response_status(exp.getReason()));
    }
}

/**
 * Verify that you cannot get a document (with xattrs) which is deleted
 */
TEST_P(RemoveTest, MB_22553_DeleteDocWithXAttr_keep_deleted) {
    verify_MB_22553("keep_deleted=true");}

/**
 * Verify that you cannot get a document (with xattrs) which is deleted
 * when the memcached bucket isn't using the keep deleted flag
 */
TEST_P(RemoveTest, MB_22553_DeleteDocWithXAttr) {
    verify_MB_22553("");
}
