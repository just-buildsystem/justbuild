// Copyright 2025 Huawei Cloud Computing Technology Co., Ltd.
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

#include "src/buildtool/common/artifact_blob.hpp"

#include "src/utils/cpp/hash_combine.hpp"

auto ArtifactBlob::ReadContent() const noexcept
    -> std::shared_ptr<std::string const> {
    return content_;
}

namespace std {
auto hash<ArtifactBlob>::operator()(ArtifactBlob const& blob) const noexcept
    -> std::size_t {
    std::size_t seed{};
    hash_combine(&seed, blob.GetDigest());
    hash_combine(&seed, blob.IsExecutable());
    return seed;
}
}  // namespace std
