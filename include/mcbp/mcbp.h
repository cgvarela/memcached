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

#include <mcbp/protocol/datatype.h>
#include <mcbp/protocol/magic.h>
#include <mcbp/protocol/opcode.h>
#include <mcbp/protocol/response.h>
#include <mcbp/protocol/request.h>
#include <mcbp/protocol/status.h>


// This file contains various methods for the Memcached Binary Protocol
// used for backwards source compatibility
#include <memcached/protocol_binary.h>


#include <ostream>
#include <vector>

namespace cb {
namespace mcbp {

/**
 * Dump a raw packet to the named stream (the packet is expected to contain
 * a valid packet)
 *
 * @param packet pointer to the first byte of a correctly encoded mcbp packet
 * @param out where to dump the bytes
 */
void dump(const uint8_t* packet, std::ostream& out);
}
}
