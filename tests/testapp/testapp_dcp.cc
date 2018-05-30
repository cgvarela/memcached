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


/**
 * In this file you'll find unit tests related to the DCP subsystem
 */

#include "testapp.h"
#include "testapp_client_test.h"

#include <xattr/blob.h>
#include <xattr/utils.h>

class DcpTest : public TestappClientTest {

};

INSTANTIATE_TEST_CASE_P(TransportProtocols,
                        DcpTest,
                        ::testing::Values(TransportProtocols::McbpPlain,
                                          TransportProtocols::McbpIpv6Plain,
                                          TransportProtocols::McbpSsl,
                                          TransportProtocols::McbpIpv6Ssl
                                         ),
                        ::testing::PrintToStringParamName());

/**
 * Test that if if we specify the DCP_OPEN_PRODUCER flag the stream
 * contains keys and values
 */
TEST_P(DcpTest, TestProducer) {
    auto& conn = dynamic_cast<MemcachedBinprotConnection&>(getConnection());

    conn.sendCommand(BinprotDcpOpenCommand{"ewb_internal:1", 0,
                                           DCP_OPEN_PRODUCER});

    BinprotResponse rsp;
    conn.recvResponse(rsp);
    EXPECT_TRUE(rsp.isSuccess());

    conn.sendCommand(BinprotDcpStreamRequestCommand{});
    conn.recvResponse(rsp);
    EXPECT_TRUE(rsp.isSuccess());

    BinprotDcpMutationCommand mutation;

    // I can use getRawPacket and then create my own class here?
    conn.recvResponse(rsp);
    mutation.reset(rsp.getRawPacket());
    EXPECT_EQ("k", mutation.getKey());
    EXPECT_EQ("this is the value", mutation.getValue());
}

/**
 * Test that if if we specify the DCP_OPEN_NO_VALUE flag the stream
 * contains keys, but the value is stripped off.
 */
TEST_P(DcpTest, TestProducerNoValue) {
    auto& conn = dynamic_cast<MemcachedBinprotConnection&>(getConnection());

    conn.sendCommand(BinprotDcpOpenCommand{"ewb_internal:1", 0,
                                           DCP_OPEN_PRODUCER |
                                           DCP_OPEN_NO_VALUE});

    BinprotResponse rsp;
    conn.recvResponse(rsp);
    EXPECT_TRUE(rsp.isSuccess());

    conn.sendCommand(BinprotDcpStreamRequestCommand{});
    conn.recvResponse(rsp);
    EXPECT_TRUE(rsp.isSuccess());

    BinprotDcpMutationCommand mutation;

    // I can use getRawPacket and then create my own class here?
    conn.recvResponse(rsp);
    mutation.reset(rsp.getRawPacket());
    EXPECT_EQ("k", mutation.getKey());
    EXPECT_EQ("", mutation.getValue());
}

TEST_P(DcpTest, TestProducerIncludeXAttr) {
    auto& conn = dynamic_cast<MemcachedBinprotConnection&>(getConnection());

    conn.sendCommand(BinprotDcpOpenCommand{"ewb_internal:1", 0,
                                           DCP_OPEN_PRODUCER |
                                           DCP_OPEN_INCLUDE_XATTRS});

    BinprotResponse rsp;
    conn.recvResponse(rsp);
    EXPECT_TRUE(rsp.isSuccess());

    conn.sendCommand(BinprotDcpStreamRequestCommand{});
    conn.recvResponse(rsp);
    EXPECT_TRUE(rsp.isSuccess());

    BinprotDcpMutationCommand mutation;

    // I can use getRawPacket and then create my own class here?
    conn.recvResponse(rsp);
    mutation.reset(rsp.getRawPacket());

    // Generate a JSON representation of the xattrs and verify they
    // look the way we want.
    const auto value = mutation.getValue();
    auto blobsize = cb::xattr::get_body_offset({value.data(), value.size()});

    cb::byte_buffer buffer{(uint8_t*)value.data(), blobsize};
    cb::xattr::Blob blob(buffer);
    EXPECT_EQ("{\"_ewb\":{\"internal\":true},\"meta\":{\"author\":\"jack\"}}",
              to_string(blob.to_json(), false));


    EXPECT_EQ("k", mutation.getKey());
    EXPECT_EQ("this is the value", mutation.getValue().substr(blobsize));
}

TEST_P(DcpTest, TestProducerIncludeXAttrNoValue) {
    auto& conn = dynamic_cast<MemcachedBinprotConnection&>(getConnection());

    conn.sendCommand(BinprotDcpOpenCommand{"ewb_internal:1", 0,
                                           DCP_OPEN_PRODUCER |
                                           DCP_OPEN_INCLUDE_XATTRS |
                                           DCP_OPEN_NO_VALUE});

    BinprotResponse rsp;
    conn.recvResponse(rsp);
    EXPECT_TRUE(rsp.isSuccess());

    conn.sendCommand(BinprotDcpStreamRequestCommand{});
    conn.recvResponse(rsp);
    EXPECT_TRUE(rsp.isSuccess());

    BinprotDcpMutationCommand mutation;

    // I can use getRawPacket and then create my own class here?
    conn.recvResponse(rsp);
    mutation.reset(rsp.getRawPacket());
    EXPECT_EQ("k", mutation.getKey());

    // Generate a JSON representation of the xattrs and verify they
    // look the way we want.
    const auto value = mutation.getValue();
    cb::byte_buffer buffer{(uint8_t*)value.data(), value.size()};
    cb::xattr::Blob blob(buffer);
    EXPECT_EQ("{\"_ewb\":{\"internal\":true},\"meta\":{\"author\":\"jack\"}}",
             to_string(blob.to_json(), false));
}

TEST_P(DcpTest, TestDcpOpenCantBeProducerAndConsumer) {
    auto& conn = dynamic_cast<MemcachedBinprotConnection&>(getConnection());

    conn.sendCommand(BinprotDcpOpenCommand{"ewb_internal:1", 0,
                                           DCP_OPEN_PRODUCER |
                                           DCP_OPEN_NOTIFIER});

    BinprotResponse rsp;
    conn.recvResponse(rsp);
    EXPECT_FALSE(rsp.isSuccess());
    EXPECT_EQ(PROTOCOL_BINARY_RESPONSE_EINVAL, rsp.getStatus());
}

TEST_P(DcpTest, TestDcpNotfierCantBeNoValue) {
    auto& conn = dynamic_cast<MemcachedBinprotConnection&>(getConnection());

    conn.sendCommand(BinprotDcpOpenCommand{"ewb_internal:1", 0,
                                           DCP_OPEN_NO_VALUE |
                                           DCP_OPEN_NOTIFIER});

    BinprotResponse rsp;
    conn.recvResponse(rsp);
    EXPECT_FALSE(rsp.isSuccess());
    EXPECT_EQ(PROTOCOL_BINARY_RESPONSE_EINVAL, rsp.getStatus());
}

TEST_P(DcpTest, TestDcpNotfierCantIncludeXattrs) {
    auto& conn = dynamic_cast<MemcachedBinprotConnection&>(getConnection());

    conn.sendCommand(BinprotDcpOpenCommand{"ewb_internal:1", 0,
                                           DCP_OPEN_INCLUDE_XATTRS |
                                           DCP_OPEN_NOTIFIER});

    BinprotResponse rsp;
    conn.recvResponse(rsp);
    EXPECT_FALSE(rsp.isSuccess());
    EXPECT_EQ(PROTOCOL_BINARY_RESPONSE_EINVAL, rsp.getStatus());
}

/**
 * Make sure that the rollback sequence number in the response isn't being
 * stripped / replaced with an error object
 */
TEST_P(DcpTest, MB24145_RollbackShouldContainSeqno) {
    auto& conn = dynamic_cast<MemcachedBinprotConnection&>(getConnection());

    conn.sendCommand(BinprotDcpOpenCommand{"ewb_internal:1", 0,
                                           DCP_OPEN_PRODUCER});

    BinprotResponse rsp;
    conn.recvResponse(rsp);
    ASSERT_TRUE(rsp.isSuccess());

    BinprotDcpStreamRequestCommand streamReq;
    streamReq.setDcpStartSeqno(1);
    conn.sendCommand(streamReq);
    conn.recvResponse(rsp);
    ASSERT_FALSE(rsp.isSuccess());

    auto data = rsp.getData();
    ASSERT_EQ(sizeof(uint64_t), data.size());
    auto* value = reinterpret_cast<const uint64_t*>(data.data());
    EXPECT_EQ(0, *value);

}
