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

#include <cbsasl/cbsasl_internal.h>
#include <cbsasl/user.h>

namespace cb {
namespace sasl {
namespace plain {

/**
 * Check if the supplied password match what's stored in the
 * provided user object
 *
 * @param conn the connection object trying to perform authentication
 * @param user the user object to check
 * @param password the password to compare
 * @return CBSASL_OK if the provided password match the supplied
 *                   password.
 */
cbsasl_error_t check_password(cbsasl_conn_t* conn,
                              const cb::sasl::User& user,
                              const std::string& password);
}
}
}
