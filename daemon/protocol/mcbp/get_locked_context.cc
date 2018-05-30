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
#include "get_locked_context.h"
#include "engine_wrapper.h"

#include <daemon/debug_helpers.h>
#include <daemon/mcbp.h>
#include <xattr/utils.h>

ENGINE_ERROR_CODE GetLockedCommandContext::getAndLockItem() {
    item* item;
    auto ret = bucket_get_locked(connection, &item, key, vbucket, lock_timeout);
    if (ret == ENGINE_SUCCESS) {
        it.reset(item);
        if (!bucket_get_item_info(&connection, item, &info)) {
            LOG_WARNING(&connection, "%u: GetLockedCommandContext::"
                "getAndLockItem Failed to get item info",
                connection.getId());
            return ENGINE_FAILED;
        }

        payload.buf = static_cast<const char*>(info.value[0].iov_base);
        payload.len = info.value[0].iov_len;

        bool need_inflate = false;
        if (mcbp::datatype::is_snappy(info.datatype)) {
            need_inflate = mcbp::datatype::is_xattr(info.datatype) ||
                           !connection.isSnappyEnabled();
        }

        if (need_inflate) {
            state = State::InflateItem;
        } else {
            state = State::SendResponse;
        }
    } else if (ret == ENGINE_LOCKED) {
        // In order to be backward compatible we should return TMPFAIL
        // instead of the more correct EEXISTS
        ret = ENGINE_LOCKED_TMPFAIL;
    }

    return ret;
}

ENGINE_ERROR_CODE GetLockedCommandContext::inflateItem() {
    try {
        if (!cb::compression::inflate(cb::compression::Algorithm::Snappy,
                                      payload.buf, payload.len, buffer)) {
            LOG_WARNING(&connection, "%u: GetLockedCommandContext::inflateItem:"
                " Failed to inflate item", connection.getId());
            return ENGINE_FAILED;
        }
        payload.buf = buffer.data.get();
        payload.len = buffer.len;
    } catch (const std::bad_alloc&) {
        return ENGINE_ENOMEM;
    }

    state = State::SendResponse;
    return ENGINE_SUCCESS;
}

ENGINE_ERROR_CODE GetLockedCommandContext::sendResponse() {
    protocol_binary_datatype_t datatype = info.datatype;

    if (mcbp::datatype::is_xattr(datatype)) {
        payload = cb::xattr::get_body(payload);
        datatype &= ~PROTOCOL_BINARY_DATATYPE_XATTR;
    }

    datatype = connection.getEnabledDatatypes(datatype);

    auto* rsp = reinterpret_cast<protocol_binary_response_get*>(connection.write.buf);
    const uint32_t bodylength = sizeof(rsp->message.body) + payload.len;

    mcbp_add_header(&connection, PROTOCOL_BINARY_RESPONSE_SUCCESS,
                    sizeof(rsp->message.body), 0 /* keylength */, bodylength,
                    datatype);
    rsp->message.header.response.cas = htonll(info.cas);

    /* add the flags */
    rsp->message.body.flags = info.flags;
    connection.addIov(&rsp->message.body, sizeof(rsp->message.body));
    connection.addIov(payload.buf, payload.len);
    connection.setState(conn_mwrite);

    STATS_INCR(&connection, cmd_lock);
    update_topkeys(key, &connection);

    state = State::Done;
    return ENGINE_SUCCESS;
}

ENGINE_ERROR_CODE GetLockedCommandContext::step() {
    ENGINE_ERROR_CODE ret;
    do {
        switch (state) {
        case State::GetAndLockItem:
            ret = getAndLockItem();
            break;
        case State::InflateItem:
            ret = inflateItem();
            break;
        case State::SendResponse:
            ret = sendResponse();
            break;
        case State::Done:
            return ENGINE_SUCCESS;
        }
    } while (ret == ENGINE_SUCCESS);

    return ret;
}
