#include "src/buildtool/execution_api/remote/bazel/bazel_network.hpp"

#include "src/buildtool/execution_api/remote/bazel/bazel_client_common.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_response.hpp"
#include "src/buildtool/logging/logger.hpp"

namespace {

[[nodiscard]] auto ReadDirectory(
    gsl::not_null<BazelNetwork const*> const& network,
    bazel_re::Digest const& digest) noexcept
    -> std::optional<bazel_re::Directory> {
    auto blobs = network->ReadBlobs({digest}).Next();
    if (blobs.size() == 1) {
        return BazelMsgFactory::MessageFromString<bazel_re::Directory>(
            blobs.at(0).data);
    }
    Logger::Log(
        LogLevel::Error, "Directory {} not found in CAS", digest.hash());
    return std::nullopt;
}

[[nodiscard]] auto ReadGitTree(
    gsl::not_null<BazelNetwork const*> const& network,
    bazel_re::Digest const& digest) noexcept
    -> std::optional<GitCAS::tree_entries_t> {
    auto blobs = network->ReadBlobs({digest}).Next();
    if (blobs.size() == 1) {
        auto const& content = blobs.at(0).data;
        return GitCAS::ReadTreeData(
            content,
            HashFunction::ComputeTreeHash(content).Bytes(),
            /*is_hex_id=*/false);
    }
    Logger::Log(LogLevel::Error, "Tree {} not found in CAS", digest.hash());
    return std::nullopt;
}

[[nodiscard]] auto TreeToStream(
    gsl::not_null<BazelNetwork const*> const& network,
    bazel_re::Digest const& tree_digest,
    gsl::not_null<FILE*> const& stream) noexcept -> bool {
    if (Compatibility::IsCompatible()) {
        if (auto dir = ReadDirectory(network, tree_digest)) {
            if (auto data = BazelMsgFactory::DirectoryToString(*dir)) {
                auto const& str = *data;
                std::fwrite(str.data(), 1, str.size(), stream);
                return true;
            }
        }
    }
    else {
        if (auto entries = ReadGitTree(network, tree_digest)) {
            if (auto data = BazelMsgFactory::GitTreeToString(*entries)) {
                auto const& str = *data;
                std::fwrite(str.data(), 1, str.size(), stream);
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] auto BlobToStream(
    gsl::not_null<BazelNetwork const*> const& network,
    bazel_re::Digest const& blob_digest,
    gsl::not_null<FILE*> const& stream) noexcept -> bool {
    auto reader = network->IncrementalReadSingleBlob(blob_digest);
    auto data = reader.Next();
    while (data and not data->empty()) {
        auto const& str = *data;
        std::fwrite(str.data(), 1, str.size(), stream);
        data = reader.Next();
    }
    return data.has_value();
}

}  // namespace

BazelNetwork::BazelNetwork(std::string instance_name,
                           std::string const& host,
                           Port port,
                           ExecutionConfiguration const& exec_config) noexcept
    : instance_name_{std::move(instance_name)},
      exec_config_{exec_config},
      cas_{std::make_unique<BazelCasClient>(host, port)},
      ac_{std::make_unique<BazelAcClient>(host, port)},
      exec_{std::make_unique<BazelExecutionClient>(host, port)} {}

auto BazelNetwork::IsAvailable(bazel_re::Digest const& digest) const noexcept
    -> bool {
    return cas_
        ->FindMissingBlobs(instance_name_,
                           std::vector<bazel_re::Digest>{digest})
        .empty();
}

auto BazelNetwork::IsAvailable(std::vector<bazel_re::Digest> const& digests)
    const noexcept -> std::vector<bazel_re::Digest> {
    return cas_->FindMissingBlobs(instance_name_, digests);
}

template <class T_Iter>
auto BazelNetwork::DoUploadBlobs(T_Iter const& first,
                                 T_Iter const& last) noexcept -> bool {
    auto num_blobs = gsl::narrow<std::size_t>(std::distance(first, last));

    std::vector<bazel_re::Digest> digests{};
    digests.reserve(num_blobs);

    auto begin = first;
    auto current = first;
    std::size_t transfer_size{};
    while (current != last) {
        auto const& blob = *current;
        transfer_size += blob.data.size();
        if (transfer_size > kMaxBatchTransferSize) {
            if (begin == current) {
                if (cas_->UpdateSingleBlob(instance_name_, blob)) {
                    digests.emplace_back(blob.digest);
                }
                ++current;
            }
            else {
                for (auto& digest :
                     cas_->BatchUpdateBlobs(instance_name_, begin, current)) {
                    digests.emplace_back(std::move(digest));
                }
            }
            begin = current;
            transfer_size = 0;
        }
        else {
            ++current;
        }
    }
    if (begin != current) {
        for (auto& digest :
             cas_->BatchUpdateBlobs(instance_name_, begin, current)) {
            digests.emplace_back(std::move(digest));
        }
    }

    if (digests.size() != num_blobs) {
        Logger::Log(LogLevel::Warning, "Failed to update all blobs");
        return false;
    }

    return true;
}

auto BazelNetwork::UploadBlobs(BlobContainer const& blobs,
                               bool skip_find_missing) noexcept -> bool {
    if (skip_find_missing) {
        return DoUploadBlobs(blobs.begin(), blobs.end());
    }

    // find digests of blobs missing in CAS
    auto missing_digests =
        cas_->FindMissingBlobs(instance_name_, blobs.Digests());

    if (not missing_digests.empty()) {
        // update missing blobs
        auto missing_blobs = blobs.RelatedBlobs(missing_digests);
        return DoUploadBlobs(missing_blobs.begin(), missing_blobs.end());
    }
    return true;
}

auto BazelNetwork::ExecuteBazelActionSync(
    bazel_re::Digest const& action) noexcept
    -> std::optional<BazelExecutionClient::ExecutionOutput> {
    auto response =
        exec_->Execute(instance_name_, action, exec_config_, true /*wait*/);

    if (response.state !=
            BazelExecutionClient::ExecutionResponse::State::Finished or
        not response.output) {
        Logger::Log(LogLevel::Error,
                    "Failed to execute action with execution id {}.",
                    action.hash());
        return std::nullopt;
    }

    return response.output;
}

auto BazelNetwork::BlobReader::Next() noexcept -> std::vector<BazelBlob> {
    std::size_t size{};
    std::vector<BazelBlob> blobs{};

    while (current_ != ids_.end()) {
        auto blob_size = gsl::narrow<std::size_t>(current_->size_bytes());
        size += blob_size;
        // read if size is 0 (unknown) or exceeds transfer size
        if (blob_size == 0 or size > kMaxBatchTransferSize) {
            // perform read of range [begin_, current_)
            if (begin_ == current_) {
                auto blob = cas_->ReadSingleBlob(instance_name_, *begin_);
                if (blob) {
                    blobs.emplace_back(std::move(*blob));
                }
                ++current_;
            }
            else {
                blobs = cas_->BatchReadBlobs(instance_name_, begin_, current_);
            }
            begin_ = current_;
            break;
        }
        ++current_;
    }

    if (begin_ != current_) {
        blobs = cas_->BatchReadBlobs(instance_name_, begin_, current_);
        begin_ = current_;
    }

    return blobs;
}

auto BazelNetwork::ReadBlobs(std::vector<bazel_re::Digest> ids) const noexcept
    -> BlobReader {
    return BlobReader{instance_name_, cas_.get(), std::move(ids)};
}

auto BazelNetwork::IncrementalReadSingleBlob(bazel_re::Digest const& id)
    const noexcept -> ByteStreamClient::IncrementalReader {
    return cas_->IncrementalReadSingleBlob(instance_name_, id);
}

auto BazelNetwork::GetCachedActionResult(
    bazel_re::Digest const& action,
    std::vector<std::string> const& output_files) const noexcept
    -> std::optional<bazel_re::ActionResult> {
    return ac_->GetActionResult(
        instance_name_, action, false, false, output_files);
}

auto BazelNetwork::ReadTreeInfos(bazel_re::Digest const& tree_digest,
                                 std::filesystem::path const& parent,
                                 bool request_remote_tree) const noexcept
    -> std::optional<std::pair<std::vector<std::filesystem::path>,
                               std::vector<Artifact::ObjectInfo>>> {
    std::optional<DirectoryMap> dir_map{std::nullopt};
    if (Compatibility::IsCompatible() and request_remote_tree) {
        // Query full tree from remote CAS. Note that this is currently not
        // supported by Buildbarn revision c3c06bbe2a.
        auto dirs =
            cas_->GetTree(instance_name_, tree_digest, kMaxBatchTransferSize);

        // Convert to Directory map
        dir_map = DirectoryMap{};
        dir_map->reserve(dirs.size());
        for (auto& dir : dirs) {
            try {
                dir_map->emplace(
                    ArtifactDigest::Create(dir.SerializeAsString()),
                    std::move(dir));
            } catch (...) {
                return std::nullopt;
            }
        }
    }

    std::vector<std::filesystem::path> paths{};
    std::vector<Artifact::ObjectInfo> infos{};

    auto store_info = [&paths, &infos](auto path, auto info) {
        paths.emplace_back(path);
        infos.emplace_back(info);
        return true;
    };

    if (ReadObjectInfosRecursively(dir_map, store_info, parent, tree_digest)) {
        return std::make_pair(std::move(paths), std::move(infos));
    }

    return std::nullopt;
}

// NOLINTNEXTLINE(misc-no-recursion)
auto BazelNetwork::ReadObjectInfosRecursively(
    std::optional<DirectoryMap> const& dir_map,
    BazelMsgFactory::InfoStoreFunc const& store_info,
    std::filesystem::path const& parent,
    bazel_re::Digest const& digest) const noexcept -> bool {
    // read from in-memory tree map
    auto const* tree = tree_map_.GetTree(digest);
    if (tree != nullptr) {
        return std::all_of(
            tree->begin(),
            tree->end(),
            [&store_info, &parent](auto const& entry) {
                try {
                    // LocalTree (from tree_map_) is flat, no recursion needed
                    auto const& [path, info] = entry;
                    return store_info(parent / path, *info);
                } catch (...) {  // satisfy clang-tidy, store_info() could throw
                    return false;
                }
            });
    }
    Logger::Log(
        LogLevel::Debug, "tree {} not found in tree map", digest.hash());

    if (Compatibility::IsCompatible()) {
        // read from in-memory Directory map and cache it in in-memory tree map
        if (dir_map) {
            if (dir_map->contains(digest)) {
                auto tree = tree_map_.CreateTree();
                return BazelMsgFactory::ReadObjectInfosFromDirectory(
                           dir_map->at(digest),
                           [this, &dir_map, &store_info, &parent, &tree](
                               auto path, auto info) {
                               if (IsTreeObject(info.type)) {
                                   // LocalTree (from tree_map_) is flat, so
                                   // recursively traverse subtrees and add
                                   // blobs.
                                   auto tree_store_info = [&store_info,
                                                           &tree,
                                                           &parent](auto path,
                                                                    auto info) {
                                       auto tree_path =
                                           path.lexically_relative(parent);
                                       return tree.AddInfo(tree_path, info) and
                                              store_info(path, info);
                                   };
                                   return ReadObjectInfosRecursively(
                                       dir_map,
                                       tree_store_info,
                                       parent / path,
                                       info.digest);
                               }
                               return tree.AddInfo(path, info) and
                                      store_info(parent / path, info);
                           }) and
                       tree_map_.AddTree(digest, std::move(tree));
            }
        }

        // fallback read from CAS and cache it in in-memory tree map
        if (auto dir = ReadDirectory(this, digest)) {
            auto tree = tree_map_.CreateTree();
            return BazelMsgFactory::ReadObjectInfosFromDirectory(
                       *dir,
                       [this, &dir_map, &store_info, &parent, &tree](
                           auto path, auto info) {
                           if (IsTreeObject(info.type)) {
                               // LocalTree (from tree_map_) is flat, so
                               // recursively traverse subtrees and add blobs.
                               auto tree_store_info =
                                   [&store_info, &tree, &parent](auto path,
                                                                 auto info) {
                                       auto tree_path =
                                           path.lexically_relative(parent);
                                       return tree.AddInfo(tree_path, info) and
                                              store_info(path, info);
                                   };
                               return ReadObjectInfosRecursively(
                                   dir_map,
                                   tree_store_info,
                                   parent / path,
                                   info.digest);
                           }
                           return tree.AddInfo(path, info) and
                                  store_info(parent / path, info);
                       }) and
                   tree_map_.AddTree(digest, std::move(tree));
        }
    }
    else {
        if (auto entries = ReadGitTree(this, digest)) {
            auto tree = tree_map_.CreateTree();
            return BazelMsgFactory::ReadObjectInfosFromGitTree(
                       *entries,
                       [this, &dir_map, &store_info, &parent, &tree](
                           auto path, auto info) {
                           if (IsTreeObject(info.type)) {
                               // LocalTree (from tree_map_) is flat, so
                               // recursively traverse subtrees and add blobs.
                               auto tree_store_info =
                                   [&store_info, &tree, &parent](auto path,
                                                                 auto info) {
                                       auto tree_path =
                                           path.lexically_relative(parent);
                                       return tree.AddInfo(tree_path, info) and
                                              store_info(path, info);
                                   };
                               return ReadObjectInfosRecursively(
                                   dir_map,
                                   tree_store_info,
                                   parent / path,
                                   info.digest);
                           }
                           return tree.AddInfo(path, info) and
                                  store_info(parent / path, info);
                       }) and
                   tree_map_.AddTree(digest, std::move(tree));
        }
    }
    return false;
}

auto BazelNetwork::DumpToStream(
    Artifact::ObjectInfo const& info,
    gsl::not_null<FILE*> const& stream) const noexcept -> bool {
    return IsTreeObject(info.type) ? TreeToStream(this, info.digest, stream)
                                   : BlobToStream(this, info.digest, stream);
}
