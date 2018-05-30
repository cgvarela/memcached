/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2015 Couchbase, Inc
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

/*
 *                "ewouldblock_engine"
 *
 * The "ewouldblock_engine" allows one to test how memcached responds when the
 * engine returns EWOULDBLOCK instead of the correct response.
 */
#pragma once

#include "config.h"

#include <memcached/protocol_binary.h>

using request_ewouldblock_ctl = protocol_binary_request_ewb_ctl;
using response_ewouldblock_ctl = protocol_binary_response_ewb_ctl;

// The mode the engine is currently operating in. Determines when it will
// inject EWOULDBLOCK instead of the real return code.
enum class EWBEngineMode : uint32_t {
    // Make the next_N calls into engine return {inject_error}. N specified
    // by the {value} field.
    Next_N = 0,

    // Randomly return {inject_error}. Chance to return {inject_error} is
    // specified as an integer percentage (1,100) in the {value} field.
    Random = 1,

    // The first call to a given function from each connection will return
    // {inject_error}, with the next (and subsequent) calls to the *same*
    // function operating normally. Calling a different function will reset
    // back to failing again.  In other words, return {inject_error} if the
    // previous function was not this one.
    First = 2,

    // Make the next N calls return a sequence of either their normal value or
    // the injected error code. The sequence can be up to 32 elements long.
    Sequence = 3,

    // Simulate CAS mismatch - make the next N store operations return
    // KEY_EEXISTS. N specified by the {value} field.
    CasMismatch = 4,

    // Increment the cluster map sequence number. Value and inject_error is
    // ignored for this opcode
    IncrementClusterMapRevno = 5,

    // Make a single call into engine and return {inject_error}.  In addition
    // do not add the operation to the processing queue and so a
    // notify_io_complete is never sent.
    No_Notify = 6,

    // Suspend a cookie with the provided id and return ENGINE_EWOULDBLOCK.
    // The connection must be resumed with a call to Resume
    Suspend = 7,

    // Resume a cookie with the provided id
    Resume = 8,

    // Next time the connection invokes a call we'll start monitoring a file
    // for existence, and when the file goes away we'll notify the connection
    // with the {inject_error}.
    // The file to monitor is specified in the key for the packet.
    // This seems like an odd interface to have, but it is needed to be able
    // to test what happens with clients that is working inside the engine
    // while a bucket is deleted. Given that we're not instructing the
    // ewouldblock engine on a special channel there is no way to send
    // commmands to the engine whlie it is being deleted ;-)
    BlockMonitorFile = 9,

    // Set the CAS for an item.
    // Requires the CAS of the item. Bear in mind that we're limited to
    // 32 bits.
    SetItemCas = 10
};
