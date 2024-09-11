// Copyright 2024 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_STREAM_DUMPER_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_STREAM_DUMPER_HPP

#include <cstdio>
#include <optional>
#include <string>
#include <utility>

#include "gsl/gsl"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/execution_api/common/tree_reader_utils.hpp"
#include "src/buildtool/file_system/object_type.hpp"

template <typename TImpl>
class StreamDumper final {
  public:
    template <typename... Args>
    explicit StreamDumper(Args&&... args) noexcept
        : impl_(std::forward<Args>(args)...) {}

    /// \brief Dump artifact to file stream.
    /// Tree artifacts are pretty-printed (i.e., contents are listed) unless
    /// raw_tree is set, then the raw tree will be written to the file stream.
    /// \param info         The object info of the artifact to dump.
    /// \param stream       The file stream to dump to.
    /// \param raw_tree     Dump tree as raw blob.
    /// \returns true on success.
    [[nodiscard]] auto DumpToStream(Artifact::ObjectInfo const& info,
                                    gsl::not_null<FILE*> const& stream,
                                    bool raw_tree) const noexcept -> bool {
        const bool is_tree = IsTreeObject(info.type);
        if (is_tree and raw_tree) {
            return DumpRawTree(info, stream);
        }
        return is_tree ? DumpTree(info, stream) : DumpBlob(info, stream);
    }

  private:
    TImpl impl_;

    [[nodiscard]] auto DumpRawTree(
        Artifact::ObjectInfo const& info,
        gsl::not_null<FILE*> const& stream) const noexcept -> bool {
        auto writer = [this, &stream](std::string const& data) -> bool {
            return DumpString(data, stream);
        };
        return impl_.DumpRawTree(info, writer);
    }

    [[nodiscard]] auto DumpTree(
        Artifact::ObjectInfo const& info,
        gsl::not_null<FILE*> const& stream) const noexcept -> bool {
        if (not impl_.IsNativeProtocol()) {
            auto directory = impl_.ReadDirectory(info.digest);
            auto data = directory
                            ? TreeReaderUtils::DirectoryToString(*directory)
                            : std::nullopt;
            if (data) {
                return DumpString(*data, stream);
            }
        }
        else {
            auto entries = impl_.ReadGitTree(info.digest);
            auto data = entries ? TreeReaderUtils::GitTreeToString(*entries)
                                : std::nullopt;
            if (data) {
                return DumpString(*data, stream);
            }
        }
        return false;
    }

    [[nodiscard]] auto DumpBlob(
        Artifact::ObjectInfo const& info,
        gsl::not_null<FILE*> const& stream) const noexcept -> bool {
        auto writer = [this, &stream](std::string const& data) -> bool {
            return DumpString(data, stream);
        };
        return impl_.DumpBlob(info, writer);
    }

    [[nodiscard]] auto DumpString(
        std::string const& data,
        gsl::not_null<FILE*> const& stream) const noexcept -> bool {
        return std::fwrite(data.data(), 1, data.size(), stream) == data.size();
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_STREAM_DUMPER_HPP
