// Copyright 2022 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_BUILDTOOL_COMPATIBILITY_NATIVE_SUPPORT_HPP
#define INCLUDED_SRC_BUILDTOOL_COMPATIBILITY_NATIVE_SUPPORT_HPP

#include <cstddef>
#include <string>

#include "gsl/gsl"
#include "src/buildtool/compatibility/compatibility.hpp"
#include "src/utils/cpp/gsl.hpp"

/// \brief Helper functions to support the native remote-execution protocol.
class NativeSupport {
    static constexpr std::size_t kTagLength = 2;
    static constexpr std::size_t kTaggedLength = 42;
    static constexpr auto kBlobTag = "62";
    static constexpr auto kTreeTag = "74";

  public:
    [[nodiscard]] static auto IsPrefixed(std::string const& hash) noexcept
        -> bool {
        if (Compatibility::IsCompatible()) {
            return false;
        }
        return hash.length() == kTaggedLength;
    }

    /// \brief Returns a prefixed hash in case of native remote-execution
    /// protocol (0x62 in case of a blob, 0x74 in case of a tree).
    [[nodiscard]] static auto Prefix(std::string const& hash,
                                     bool is_tree) noexcept -> std::string {
        if (Compatibility::IsCompatible()) {
            return hash;
        }
        ExpectsAudit(not IsPrefixed(hash));
        return (is_tree ? kTreeTag : kBlobTag) + hash;
    }

    [[nodiscard]] static auto Unprefix(std::string const& hash) noexcept
        -> std::string {
        if (Compatibility::IsCompatible()) {
            return hash;
        }
        ExpectsAudit(IsPrefixed(hash));
        return hash.substr(kTagLength);
    }

    [[nodiscard]] static auto IsTree(std::string const& hash) noexcept -> bool {
        return IsPrefixed(hash) and hash.starts_with(kTreeTag);
    }
};
#endif  // INCLUDED_SRC_BUILDTOOL_COMPATIBILITY_NATIVE_SUPPORT_HPP
