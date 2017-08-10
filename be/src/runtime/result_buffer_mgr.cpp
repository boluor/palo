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

#include "runtime/result_buffer_mgr.h"
#include <boost/bind.hpp>
#include "runtime/buffer_control_block.h"
#include "runtime/raw_value.h"
#include "util/debug_util.h"
#include "gen_cpp/PaloInternalService_types.h"

namespace palo {

std::size_t hash_value(const TUniqueId& fragment_id) {
    uint32_t value = RawValue::get_hash_value(&fragment_id.lo, TypeDescriptor(TYPE_BIGINT), 0);
    value = RawValue::get_hash_value(&fragment_id.hi, TypeDescriptor(TYPE_BIGINT), value);
    return value;
}

ResultBufferMgr::ResultBufferMgr()
    : _is_stop(false) {
}

ResultBufferMgr::~ResultBufferMgr() {
    _is_stop = true;
    _cancel_thread->join();
}

Status ResultBufferMgr::init() {
    _cancel_thread.reset(
            new boost::thread(
                    boost::bind<void>(boost::mem_fn(&ResultBufferMgr::cancel_thread), this)));
    return Status::OK;
}

Status ResultBufferMgr::create_sender(
    const TUniqueId& query_id, int buffer_size,
    boost::shared_ptr<BufferControlBlock>* sender) {
    if (find_control_block(query_id).get() != NULL) {
        LOG(WARNING) << "already have buffer control block for this instance "
                     << query_id;
        *sender = find_control_block(query_id);
        return Status::OK;
    }

    boost::shared_ptr<BufferControlBlock> control_block(
        new BufferControlBlock(query_id, buffer_size));
    boost::lock_guard<boost::mutex> l(_lock);
    _buffer_map.insert(std::make_pair(query_id, control_block));
    *sender = control_block;
    return Status::OK;
}

boost::shared_ptr<BufferControlBlock> ResultBufferMgr::find_control_block(
    const TUniqueId& query_id) {
    // TODO(zhaochun): this lock can be bottleneck?
    boost::lock_guard<boost::mutex> l(_lock);
    BufferMap::iterator iter = _buffer_map.find(query_id);

    if (_buffer_map.end() != iter) {
        return iter->second;
    }

    return boost::shared_ptr<BufferControlBlock>();
}

Status ResultBufferMgr::fetch_data(
    const TUniqueId& query_id, TFetchDataResult* result) {
    boost::shared_ptr<BufferControlBlock> cb = find_control_block(query_id);

    if (NULL == cb) {
        // the sender tear down its buffer block
        return Status("no result for this query.");
    }

    return cb->get_batch(result);
}

Status ResultBufferMgr::cancel(const TUniqueId& query_id) {
    boost::lock_guard<boost::mutex> l(_lock);
    BufferMap::iterator iter = _buffer_map.find(query_id);

    if (_buffer_map.end() != iter) {
        iter->second->cancel();
        _buffer_map.erase(iter);
    }

    return Status::OK;
}

Status ResultBufferMgr::cancel_at_time(time_t cancel_time, const TUniqueId& query_id) {
    boost::lock_guard<boost::mutex> l(_timeout_lock);
    TimeoutMap::iterator iter = _timeout_map.find(cancel_time);

    if (_timeout_map.end() == iter) {
        _timeout_map.insert(std::pair<time_t, std::vector<TUniqueId> >(
                                 cancel_time, std::vector<TUniqueId>()));
        iter = _timeout_map.find(cancel_time);
    }

    iter->second.push_back(query_id);
    return Status::OK;
}

void ResultBufferMgr::cancel_thread() {
    LOG(INFO) << "result buffer manager cancel thread begin.";

    while (!_is_stop) {
        // get query
        std::vector<TUniqueId> query_to_cancel;
        time_t now_time = time(NULL);
        {
            boost::lock_guard<boost::mutex> l(_timeout_lock);
            TimeoutMap::iterator end = _timeout_map.upper_bound(now_time + 1);

            for (TimeoutMap::iterator iter = _timeout_map.begin(); iter != end; ++iter) {
                for (int i = 0; i < iter->second.size(); ++i) {
                    query_to_cancel.push_back(iter->second[i]);
                }
            }

            _timeout_map.erase(_timeout_map.begin(), end);
        }

        // cancel query
        for (int i = 0; i < query_to_cancel.size(); ++i) {
            cancel(query_to_cancel[i]);
        }

        sleep(1);
    }

    LOG(INFO) << "result buffer manager cancel thread finish.";
}

}
/* vim: set ts=4 sw=4 sts=4 tw=100 noet: */