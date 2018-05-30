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

#include <cstring>
#include <stdexcept>
#include <string>
#include <mcbp/mcbp.h>
#include <iomanip>
#include <include/memcached/protocol_binary.h>
#include <utilities/protocol2text.h>

// This file contains the functionality to generate a packet dump
// of a memcached frame

/**
 * The Frame class represents a complete frame as it is being sent on the
 * wire in the Memcached Binary Protocol.
 */
class McbpFrame {
public:
    McbpFrame(const uint8_t* bytes, size_t len)
        : root(bytes),
          length(len) {
        // empty
    }

    virtual ~McbpFrame() {
    }

    void dump(std::ostream& out) const {
        dumpFrame(out);
        dumpPacketInfo(out);
        dumpExtras(out);
        dumpKey(out);
        dumpValue(out);
    }

protected:
    virtual void dumpPacketInfo(std::ostream& out) const = 0;
    virtual void dumpExtras(std::ostream &out) const = 0;
    virtual void dumpKey(std::ostream &out) const = 0;
    virtual void dumpValue(std::ostream&out) const = 0;

    void dumpExtras(const uint8_t* location,
                    uint8_t nbytes,
                    std::ostream& out) const {
        if (nbytes != 0) {
            out << "    Extra               : " << (uint16_t(nbytes) & 0xff)
                << " bytes of binary data" << std::endl;
        }
    }

    void dumpKey(const uint8_t* location, uint16_t nbytes,
                 std::ostream& out) const {
        if (nbytes != 0) {
            const ptrdiff_t first = location - root;
            const ptrdiff_t last = first + nbytes - 1;
            out << "    Key          (" << first << "-" << last << "): ";

            std::string str((const char*)location, nbytes);

            for (const auto&c : str) {
                if (!isprint(c)) {
                    out << nbytes
                        << " bytes of binary data" << std::endl;
                    return;
                }
            }
            out << "The textual string \"" << str
                << "\"" << std::endl;
        }
    }


private:
    void printByte(uint8_t b, bool inBody, std::ostream& out) const {
        uint32_t val = b & 0xff;
        if (b < 0x10) {
            out << " 0x0";
        } else {
            out << " 0x";
        }

        out.flags(std::ios::hex);
        out << val;
        out.flags(std::ios::dec);

        if (inBody && isprint((int)b)) {
            out << " ('" << (char)b << "')";
        } else {
            out << "      ";
        }
        out << "    |";
    }

    void dumpFrame(std::ostream& out) const {
        out << std::endl;
        out <<
        "      Byte/     0       |       1       |       2       |       3       |" <<
        std::endl;
        out <<
        "         /              |               |               |               |" <<
        std::endl;
        out <<
        "        |0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|";

        size_t ii = 0;
        for (; ii < length; ++ii) {
            if (ii % 4 == 0) {
                out << std::endl;
                out <<
                "        +---------------+---------------+---------------+---------------+" <<
                std::endl;
                out.setf(std::ios::right);
                out << std::setw(8) << ii << "|";
                out.setf(std::ios::fixed);
            }
            printByte(root[ii], ii > 23, out);
        }

        out << std::endl;
        if (ii % 4 != 0) {
            out << "        ";
            for (size_t jj = 0; jj < ii % 4; ++jj) {
                out << "+---------------";
            }
            out << "+" << std::endl;
        } else {
            out << "        +---------------+---------------+---------------+---------------+"
            << std::endl;
        }
    }

    const uint8_t *root;
    size_t length;
};

std::ostream& operator<<(std::ostream& out, const McbpFrame& frame) {
    frame.dump(out);
    return out;
}

class Request : public McbpFrame {
public:
    Request(const protocol_binary_request_header& req)
        : McbpFrame(req.bytes, ntohl(req.request.bodylen) + sizeof(req.bytes)),
          request(req) {
        // nothing
    }

protected:

    virtual void dumpPacketInfo(std::ostream& out) const override {
        uint32_t bodylen = ntohl(request.request.bodylen);

        out << "        Total " << sizeof(request) + bodylen << " bytes";
        if (bodylen > 0) {
            out << " (" << sizeof(request) << " bytes header";
            if (request.request.extlen != 0) {
                out << ", " << (unsigned int)request.request.extlen << " byte extras ";
            }
            uint16_t keylen = ntohs(request.request.keylen);
            if (keylen > 0) {
                out << ", " << keylen << " bytes key";
            }
            bodylen -= keylen;
            bodylen -= request.request.extlen;
            if (bodylen > 0) {
                out << " and " << bodylen << " value";
            }
            out << ")";
        }

        out << std::endl << std::endl;
        out.flags(std::ios::hex);
        out << std::setfill('0');
        out << "    Field        (offset) (value)" << std::endl;
        out << "    Magic        (0)    : 0x" << (uint32_t(request.bytes[0]) & 0xff) << std::endl;
        out << "    Opcode       (1)    : 0x" << std::setw(2) << (uint32_t(request.bytes[1]) & 0xff) << " (" << memcached_opcode_2_text(request.bytes[1]) << ")"  << std::endl;
        out << "    Key length   (2,3)  : 0x" << std::setw(4) << (ntohs(request.request.keylen) & 0xffff) << std::endl;
        out << "    Extra length (4)    : 0x" << std::setw(2) << (uint32_t(request.bytes[4]) & 0xff) << std::endl;
        out << "    Data type    (5)    : 0x" << std::setw(2) << (uint32_t(request.bytes[5]) & 0xff) << std::endl;
        out << "    Vbucket      (6,7)  : 0x" << std::setw(4) << (ntohs(request.request.vbucket) & 0xffff) << std::endl;
        out << "    Total body   (8-11) : 0x" << std::setw(8) << (uint64_t(ntohl(request.request.bodylen)) & 0xffff) << std::endl;
        out << "    Opaque       (12-15): 0x" << std::setw(8) << ntohl(request.request.opaque) << std::endl;
        out << "    CAS          (16-23): 0x" << std::setw(16) << ntohll(request.request.cas) << std::endl;
        out << std::setfill(' ');
        out.flags(std::ios::dec);
    }

    virtual void dumpExtras(std::ostream &out) const override {
        McbpFrame::dumpExtras(request.bytes + sizeof(request.bytes),
                          request.request.extlen,
                          out);
    }

    virtual void dumpKey(std::ostream &out) const override {
        McbpFrame::dumpKey(
            request.bytes + sizeof(request.bytes) + request.request.extlen,
            ntohs(request.request.keylen),
            out);
    }

    virtual void dumpValue(std::ostream &out) const override {

    }

    const protocol_binary_request_header& request;
};

class HelloRequest : public Request {
public:
    HelloRequest(const protocol_binary_request_header& req)
        : Request(req) {

    }

protected:
    virtual void dumpExtras(std::ostream& out) const override {
        if (request.request.extlen != 0) {
            throw std::logic_error(
                "HelloRequest::dumpExtras(): extlen must be 0");
        }
    }

    virtual void dumpValue(std::ostream& out) const override {
        uint32_t bodylen = ntohl(request.request.bodylen);
        uint16_t keylen = ntohs(request.request.keylen);
        bodylen -= keylen;

        if ((bodylen % 2) != 0) {
            throw std::logic_error(
                "HelloRequest::dumpValue(): bodylen must be in words");
        }

        if (bodylen == 0) {
            return;
        }
        uint32_t first = sizeof(request.bytes) + keylen;
        out << "    Body                :" << std::endl;

        uint32_t total = bodylen / 2;
        uint32_t offset = sizeof(request.bytes) + keylen;
        for (uint32_t ii = 0; ii < total; ++ii) {
            uint16_t feature;
            memcpy(&feature, request.bytes + offset, 2);
            offset += 2;
            feature = ntohs(feature);

            std::string text;
            try {
                text = mcbp::to_string(mcbp::Feature(feature));
            } catch (...) {
                text = std::to_string(feature);
            }

            out << "                 (" << first << "-"
                << first + 1 <<"): " << text << std::endl;
            first += 2;
        }
    }
};

class SetWithMetaRequest : public Request {
public:
    SetWithMetaRequest(const protocol_binary_request_header& req)
        : Request(req),
          packet(reinterpret_cast<const protocol_binary_request_set_with_meta&>(
                  req)) {
    }

protected:
    const protocol_binary_request_set_with_meta& packet;

    std::string decodeOptions(const uint32_t* ptr) const {
        uint32_t value = ntohl(*ptr);

        std::string options;
        if (value & SKIP_CONFLICT_RESOLUTION_FLAG) {
            options.append("skip conflict resolution, ");
        }

        if (value & FORCE_ACCEPT_WITH_META_OPS) {
            options.append("force accept, ");
        }

        if (value & REGENERATE_CAS) {
            options.append("regenerate cas, ");
        }

        // Should I look for others?

        // Remove trailing ", "
        options.pop_back();
        options.pop_back();

        return options;
    }

    virtual void dumpExtras(std::ostream& out) const override {
        if (request.request.extlen < 24) {
            throw std::logic_error(
                    "SetWithMetaRequest::dumpExtras(): extlen must be at lest "
                    "24 bytes");
        }

        out << "    Extras" << std::endl;
        out << std::setfill('0');
        out << "        flags    (24-27): 0x" << std::setw(8)
            << packet.message.body.flags << std::endl;
        out << "        exptime  (28-31): 0x" << std::setw(8)
            << ntohl(packet.message.body.expiration) << std::endl;
        out << "        seqno    (32-39): 0x" << std::setw(16)
            << ntohll(packet.message.body.seqno) << std::endl;
        out << "        cas      (40-47): 0x" << std::setw(16)
            << ntohll(packet.message.body.cas) << std::endl;

        const uint16_t* nmeta_ptr;
        const uint32_t* options_ptr;

        switch (request.request.extlen) {
        case 24: // no nmeta and no options
            break;
        case 26: // nmeta
            nmeta_ptr = reinterpret_cast<const uint16_t*>(packet.bytes + 48);
            out << "        nmeta     (48-49): 0x" << std::setw(4)
                << uint16_t(ntohs(*nmeta_ptr)) << std::endl;
            break;
        case 28: // options (4-byte field)
        case 30: // options and nmeta (options followed by nmeta)
            options_ptr = reinterpret_cast<const uint32_t*>(packet.bytes + 48);
            out << "        options  (48-51): 0x" << std::setw(8)
                << uint32_t(ntohl(*options_ptr));
            if (*options_ptr != 0) {
                out << " (" << decodeOptions(options_ptr) << ")";
            }
            out << std::endl;
            if (request.request.extlen == 28) {
                break;
            }
            nmeta_ptr = reinterpret_cast<const uint16_t*>(packet.bytes + 52);
            out << "        nmeta     (52-53): 0x" << std::setw(4)
                << uint16_t(ntohs(*nmeta_ptr)) << std::endl;
            break;
        default:
            throw std::logic_error(
                    "SetWithMetaRequest::dumpExtras(): Invalid extlen");
        }

        out << std::setfill(' ');
    }
};

class Response : public McbpFrame {
public:
    Response(const protocol_binary_response_header& res)
        : McbpFrame(res.bytes, ntohl(res.response.bodylen) + sizeof(res.bytes)),
          response(res) {
        // nothing
    }

protected:

    virtual void dumpPacketInfo(std::ostream& out) const override {
        uint32_t bodylen = ntohl(response.response.bodylen);
        out << "        Total " << sizeof(response) + bodylen << " bytes";
        if (bodylen > 0) {
            out << " (" << sizeof(response) << " bytes header";
            if (response.response.extlen != 0) {
                out << ", " << (unsigned int)response.response.extlen << " byte extras ";
            }
            uint16_t keylen = ntohs(response.response.keylen);
            if (keylen > 0) {
                out << ", " << keylen << " bytes key";
            }
            bodylen -= keylen;
            bodylen -= response.response.extlen;
            if (bodylen > 0) {
                out << " and " << bodylen << " value";
            }
            out << ")";
        }
        out << std::endl << std::endl;
        out.flags(std::ios::hex);
        out << std::setfill('0');
        auto status = (protocol_binary_response_status)ntohs(response.response.status);

        out << "    Field        (offset) (value)" << std::endl;
        out << "    Magic        (0)    : 0x" << (uint32_t(response.bytes[0]) & 0xff) << std::endl;
        out << "    Opcode       (1)    : 0x" << std::setw(2) << (uint32_t(response.bytes[1]) & 0xff) << " (" << memcached_opcode_2_text(response.bytes[1]) << ")" << std::endl;
        out << "    Key length   (2,3)  : 0x" << std::setw(4) << (ntohs(response.response.keylen) & 0xffff) << std::endl;
        out << "    Extra length (4)    : 0x" << std::setw(2) << (uint32_t(response.bytes[4]) & 0xff) << std::endl;
        out << "    Data type    (5)    : 0x" << std::setw(2) << (uint32_t(response.bytes[5]) & 0xff) << std::endl;
        out << "    Status       (6,7)  : 0x" << std::setw(4) << (ntohs(response.response.status) & 0xffff) << " (" << memcached_status_2_text(status) << ")" << std::endl;
        out << "    Total body   (8-11) : 0x" << std::setw(8) << (uint64_t(ntohl(response.response.bodylen)) & 0xffff) << std::endl;
        out << "    Opaque       (12-15): 0x" << std::setw(8) << response.response.opaque << std::endl;
        out << "    CAS          (16-23): 0x" << std::setw(16) << response.response.cas << std::endl;
        out << std::setfill(' ');
        out.flags(std::ios::dec);
    }

    virtual void dumpExtras(std::ostream &out) const override {
        McbpFrame::dumpExtras(response.bytes + sizeof(response.bytes),
                          response.response.extlen,
                          out);
    }

    virtual void dumpKey(std::ostream &out) const override {
        McbpFrame::dumpKey(
            response.bytes + sizeof(response.bytes) + response.response.extlen,
            ntohs(response.response.keylen),
            out);
    }

    virtual void dumpValue(std::ostream &out) const override {

    }

    const protocol_binary_response_header& response;
};

class HelloResponse : public Response {
public:
    HelloResponse(const protocol_binary_response_header& res)
        : Response(res) {

    }

protected:
    virtual void dumpExtras(std::ostream& out) const override {
        if (response.response.extlen != 0) {
            throw std::logic_error(
                "HelloResponse::dumpExtras(): extlen must be 0");
        }
    }

    virtual void dumpKey(std::ostream& out) const override {
        if (response.response.keylen != 0) {
            throw std::logic_error(
                "HelloResponse::dumpKey(): keylen must be 0");
        }
    }

    virtual void dumpValue(std::ostream& out) const override {
        uint32_t bodylen = ntohl(response.response.bodylen);

        if ((bodylen % 2) != 0) {
            throw std::logic_error(
                "HelloRequest::dumpValue(): bodylen must be in words");
        }

        if (bodylen == 0) {
            return;
        }
        uint32_t first = sizeof(response.bytes);
        out << "    Body                :" << std::endl;

        uint32_t total = bodylen / 2;
        uint32_t offset = sizeof(response.bytes);
        for (uint32_t ii = 0; ii < total; ++ii) {
            uint16_t feature;
            memcpy(&feature, response.bytes + offset, 2);
            offset += 2;
            feature = ntohs(feature);
            std::string text;
            try {
                text = mcbp::to_string(mcbp::Feature(feature));
            } catch (...) {
                text = std::to_string(feature);
            }
            out << "                 (" << first << "-"
            << first + 1 <<"): " << text << std::endl;
            first += 2;
        }
    }
};

class ListBucketsResponse : public Response {
public:
    ListBucketsResponse(const protocol_binary_response_header& res)
        : Response(res) {

    }

protected:
    virtual void dumpExtras(std::ostream& out) const override {
        if (response.response.extlen != 0) {
            throw std::logic_error(
                "ListBucketsResponse::dumpExtras(): extlen must be 0");
        }
    }

    virtual void dumpKey(std::ostream& out) const override {
        if (response.response.keylen != 0) {
            throw std::logic_error(
                "ListBucketsResponse::dumpKey(): keylen must be 0");
        }
    }

    virtual void dumpValue(std::ostream& out) const override {
        const char* payload = reinterpret_cast<const char*>(response.bytes) +
                                                sizeof(response.bytes);
        std::string buckets(payload, ntohl(response.response.bodylen));
        out << "    Body                :" << std::endl;

        std::string::size_type start = 0;
        std::string::size_type end;

        while ((end = buckets.find(" ", start)) != std::string::npos) {
            const auto b = buckets.substr(start, end - start);
            out << "                        : " << b << std::endl;
            start = end + 1;
        }
        if (start < buckets.size()) {
            out << "                        : " << buckets.substr(start)
                << std::endl;
        }
    }
};


static void dump_request(const protocol_binary_request_header* req,
                         std::ostream& out) {
    switch (req->request.opcode) {
    case PROTOCOL_BINARY_CMD_HELLO:
        out << HelloRequest(*req) << std::endl;
        break;
    case PROTOCOL_BINARY_CMD_SET_WITH_META:
    case PROTOCOL_BINARY_CMD_SETQ_WITH_META:
        out << SetWithMetaRequest(*req) << std::endl;
        break;
    default:
        out << Request(*req) << std::endl;
    }
}

static void dump_response(const protocol_binary_response_header* res,
                          std::ostream& out) {
    switch (res->response.opcode) {
    case PROTOCOL_BINARY_CMD_HELLO:
        out << HelloResponse(*res) << std::endl;
        break;
    case PROTOCOL_BINARY_CMD_LIST_BUCKETS:
        out << ListBucketsResponse(*res) << std::endl;
        break;
    default:
        out << Response(*res) << std::endl;
    }
}

void cb::mcbp::dump(const uint8_t* packet, std::ostream& out) {
    auto* req = reinterpret_cast<const protocol_binary_request_header*>(packet);
    auto* res = reinterpret_cast<const protocol_binary_response_header*>(packet);

    switch (*packet) {
    case PROTOCOL_BINARY_REQ:
        dump_request(req, out);
        break;
    case PROTOCOL_BINARY_RES:
        dump_response(res, out);
        break;
    default:
        throw std::invalid_argument("Couchbase::MCBP::dump: Invalid magic");
    }
}
