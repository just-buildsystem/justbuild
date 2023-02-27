// Copyright 2023 Huawei Cloud Computing Technology Co., Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "src/buildtool/execution_api/execution_service/operation_cache.hpp"

#include <algorithm>

#include "google/protobuf/timestamp.pb.h"

void OperationCache::GarbageCollection() {
    std::shared_lock slock{mutex_};
    if (cache_.size() > (threshold_ << 1U)) {
        std::vector<std::pair<std::string, ::google::longrunning::Operation>>
            tmp;
        tmp.reserve(cache_.size());
        std::copy(cache_.begin(), cache_.end(), std::back_insert_iterator(tmp));
        slock.release();
        std::sort(tmp.begin(), tmp.end(), [](auto const& x, auto const& y) {
            ::google::protobuf::Timestamp tx;
            ::google::protobuf::Timestamp ty;
            x.second.metadata().UnpackTo(&tx);
            y.second.metadata().UnpackTo(&ty);
            return tx.seconds() < ty.seconds();
        });

        std::size_t deleted = 0;
        std::unique_lock ulock{mutex_};
        for (auto const& [key, op] : tmp) {
            if (op.done()) {
                DropInternal(key);
                ++deleted;
            }
            if (deleted == threshold_) {
                break;
            }
        }
    }
}
