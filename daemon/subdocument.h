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

/**
 * Sub-document API support.
 */

#pragma once

#include "config.h"

class McbpConnection;

/* Subdocument executor functions. */
void subdoc_get_executor(McbpConnection *c, void *packet);
void subdoc_exists_executor(McbpConnection *c, void *packet);
void subdoc_dict_add_executor(McbpConnection *c, void *packet);
void subdoc_dict_upsert_executor(McbpConnection *c, void *packet);
void subdoc_delete_executor(McbpConnection *c, void *packet);
void subdoc_replace_executor(McbpConnection *c, void *packet);
void subdoc_array_push_last_executor(McbpConnection *c, void *packet);
void subdoc_array_push_first_executor(McbpConnection *c, void *packet);
void subdoc_array_insert_executor(McbpConnection * c, void* packet);
void subdoc_array_add_unique_executor(McbpConnection * c, void* packet);
void subdoc_counter_executor(McbpConnection *c, void *packet);
void subdoc_get_count_executor(McbpConnection *c, void *packet);
void subdoc_multi_lookup_executor(McbpConnection *c, void *packet);
void subdoc_multi_mutation_executor(McbpConnection *c, void *packet);
