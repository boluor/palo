// Modifications copyright (C) 2017, Baidu.com, Inc.
// Copyright 2017 The Apache Software Foundation

// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "exec/schema_scanner/schema_schemata_scanner.h"
#include "runtime/primitive_type.h"
#include "runtime/string_value.h"
#include "exec/schema_scanner/frontend_helper.h"

namespace palo {

SchemaScanner::ColumnDesc SchemaSchemataScanner::_s_columns[] = {
    //   name,       type,          size
    { "CATALOG_NAME",               TYPE_VARCHAR, sizeof(StringValue), true},
    { "SCHEMA_NAME",                TYPE_VARCHAR, sizeof(StringValue), false},
    { "DEFAULT_CHARACTER_SET_NAME", TYPE_VARCHAR, sizeof(StringValue), false},
    { "DEFAULT_COLLATION_NAME",     TYPE_VARCHAR, sizeof(StringValue), false},
    { "SQL_PATH",                   TYPE_VARCHAR, sizeof(StringValue), true},
};

SchemaSchemataScanner::SchemaSchemataScanner() : 
        SchemaScanner(_s_columns, sizeof(_s_columns) / sizeof(SchemaScanner::ColumnDesc)),
        _db_index(0) {
}

SchemaSchemataScanner::~SchemaSchemataScanner() {
}

Status SchemaSchemataScanner::start(RuntimeState *state) {
    if (!_is_init) {
        return Status("used before initial.");
    }
    TGetDbsParams db_params;
    if (NULL != _param->wild) {
        db_params.__set_pattern(*(_param->wild));
    }
    if (NULL != _param->user) {
        db_params.__set_user(*(_param->user));
    }
    if (NULL != _param->ip && 0 != _param->port) {
        RETURN_IF_ERROR(FrontendHelper::get_db_names(*(_param->ip),
                    _param->port, db_params, &_db_result)); 
    } else {
        return Status("IP or port dosn't exists");
    }

    return Status::OK;
}

Status SchemaSchemataScanner::fill_one_row(Tuple *tuple, MemPool *pool) {
    // set all bit to not null
    memset((void *)tuple, 0, _tuple_desc->num_null_bytes());

    // catalog
    {
        tuple->set_null(_tuple_desc->slots()[0]->null_indicator_offset());
    }
    // schema
    {
        void *slot = tuple->get_slot(_tuple_desc->slots()[1]->tuple_offset());
        StringValue* str_slot = reinterpret_cast<StringValue*>(slot);
        str_slot->ptr = (char *)pool->allocate(_db_result.dbs[_db_index].length());
        if (NULL == str_slot->ptr) {
            return Status("Allocate memory failed.");
        }
        str_slot->len = _db_result.dbs[_db_index].length();
        memcpy(str_slot->ptr, _db_result.dbs[_db_index].c_str(), str_slot->len);
    }
    // DEFAULT_CHARACTER_SET_NAME
    {
        void *slot = tuple->get_slot(_tuple_desc->slots()[2]->tuple_offset());
        StringValue* str_slot = reinterpret_cast<StringValue*>(slot);
        str_slot->len = strlen("utf8") + 1;
        str_slot->ptr = (char *)pool->allocate(str_slot->len);
        if (NULL == str_slot->ptr) {
            return Status("Allocate memory failed.");
        }
        memcpy(str_slot->ptr, "utf8", str_slot->len);
    }
    // DEFAULT_COLLATION_NAME
    {
        void *slot = tuple->get_slot(_tuple_desc->slots()[3]->tuple_offset());
        StringValue* str_slot = reinterpret_cast<StringValue*>(slot);
        str_slot->len = strlen("utf8_general_ci") + 1;
        str_slot->ptr = (char *)pool->allocate(str_slot->len);
        if (NULL == str_slot->ptr) {
            return Status("Allocate memory failed.");
        }
        memcpy(str_slot->ptr, "utf8_general_ci", str_slot->len);
    }
    // SQL_PATH
    {
        tuple->set_null(_tuple_desc->slots()[4]->null_indicator_offset());
    }
    _db_index++;
    return Status::OK;
}

Status SchemaSchemataScanner::get_next_row(Tuple *tuple, MemPool *pool, bool *eos) {
    if (!_is_init) {
        return Status("Used before Initialized.");
    }
    if (NULL == tuple || NULL == pool || NULL == eos) {
        return Status("input pointer is NULL.");
    }
    if (_db_index >= _db_result.dbs.size()) {
        *eos = true;
        return Status::OK;
    }
    *eos = false;
    return fill_one_row(tuple, pool);
}

}
