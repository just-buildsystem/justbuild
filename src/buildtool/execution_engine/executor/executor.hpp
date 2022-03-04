#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_ENGINE_EXECUTOR_EXECUTOR_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_ENGINE_EXECUTOR_EXECUTOR_HPP

#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <type_traits>
#include <vector>

#include "gsl-lite/gsl-lite.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/common/statistics.hpp"
#include "src/buildtool/common/tree.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/execution_engine/dag/dag.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/logger.hpp"

/// \brief Implementations for executing actions and uploading artifacts.
class ExecutorImpl {
  public:
    /// \brief Execute action and obtain response.
    /// \returns std::nullopt for actions without response (e.g., tree actions).
    /// \returns nullptr on error.
    [[nodiscard]] static auto ExecuteAction(
        Logger const& logger,
        gsl::not_null<DependencyGraph::ActionNode const*> const& action,
        gsl::not_null<IExecutionApi*> const& api,
        std::map<std::string, std::string> const& properties,
        std::chrono::milliseconds const& timeout,
        IExecutionAction::CacheFlag cache_flag)
        -> std::optional<IExecutionResponse::Ptr> {
        auto const& inputs = action->Dependencies();
        auto const root_digest = CreateRootDigest(api, inputs);
        if (not root_digest) {
            Logger::Log(LogLevel::Error,
                        "failed to create root digest for input artifacts.");
            return nullptr;
        }

        if (action->Content().IsTreeAction()) {
            auto const& tree_artifact = action->OutputDirs()[0].node->Content();
            bool failed_inputs = false;
            for (auto const& [local_path, artifact] : inputs) {
                failed_inputs |= artifact->Content().Info()->failed;
            }
            tree_artifact.SetObjectInfo(
                *root_digest, ObjectType::Tree, failed_inputs);
            return std::nullopt;
        }

        Statistics::Instance().IncrementActionsQueuedCounter();

        logger.Emit(LogLevel::Trace, [&inputs]() {
            std::ostringstream oss{};
            oss << "start processing" << std::endl;
            for (auto const& [local_path, artifact] : inputs) {
                auto const& info = artifact->Content().Info();
                oss << fmt::format(
                           " - needs {} {}",
                           local_path,
                           info ? info->ToString() : std::string{"[???]"})
                    << std::endl;
            }
            return oss.str();
        });

        auto remote_action = api->CreateAction(*root_digest,
                                               action->Command(),
                                               action->OutputFilePaths(),
                                               action->OutputDirPaths(),
                                               action->Env(),
                                               properties);

        if (remote_action == nullptr) {
            logger.Emit(LogLevel::Error,
                        "failed to create action for execution.");
            return nullptr;
        }

        // set action options
        remote_action->SetCacheFlag(cache_flag);
        remote_action->SetTimeout(timeout);
        return remote_action->Execute(&logger);
    }

    /// \brief Ensures the artifact is available to the CAS, either checking
    /// that its existing digest corresponds to that of an object already
    /// available or by uploading it if there is no digest in the artifact. In
    /// the later case, the new digest is saved in the artifact
    /// \param[in] artifact The artifact to process.
    /// \returns True if artifact is available at the point of return, false
    /// otherwise
    [[nodiscard]] static auto VerifyOrUploadArtifact(
        gsl::not_null<DependencyGraph::ArtifactNode const*> const& artifact,
        gsl::not_null<IExecutionApi*> const& api) noexcept -> bool {
        auto const object_info_opt = artifact->Content().Info();
        auto const file_path_opt = artifact->Content().FilePath();
        // If there is no object info and no file path, the artifact can not be
        // processed: it means its definition is ill-formed or that it is the
        // output of an action, in which case it shouldn't have reached here
        if (not object_info_opt and not file_path_opt) {
            Logger::Log(LogLevel::Error,
                        "artifact {} can not be processed.",
                        artifact->Content().Id());
            return false;
        }
        // If the artifact has digest, we check that an object with this digest
        // is available to the execution API
        if (object_info_opt) {
            if (not api->IsAvailable(object_info_opt->digest) and
                not UploadGitBlob(api,
                                  artifact->Content().Repository(),
                                  object_info_opt->digest,
                                  /*skip_check=*/true)) {
                Logger::Log(
                    LogLevel::Error,
                    "artifact {} should be present in CAS but is missing.",
                    artifact->Content().Id());
                return false;
            }
            return true;
        }

        // Otherwise, we upload the new file to make it available to the
        // execution API
        // Note that we can be sure now that file_path_opt has a value and
        // that the path stored is relative to the workspace dir, so we need to
        // prepend it
        auto repo = artifact->Content().Repository();
        auto new_info = UploadFile(api, repo, *file_path_opt);
        if (not new_info) {
            Logger::Log(LogLevel::Error,
                        "artifact in {} could not be uploaded to CAS.",
                        file_path_opt->string());
            return false;
        }

        // And we save the digest object type in the artifact
        artifact->Content().SetObjectInfo(*new_info, false);
        return true;
    }

    /// \brief Lookup blob via digest in local git repositories and upload.
    /// \param api          The endpoint used for uploading
    /// \param repo         The global repository name, the artifact belongs to
    /// \param digest       The digest of the object
    /// \param skip_check   Skip check for existence before upload
    /// \returns true on success
    [[nodiscard]] static auto UploadGitBlob(
        gsl::not_null<IExecutionApi*> const& api,
        std::string const& repo,
        ArtifactDigest const& digest,
        bool skip_check) noexcept -> bool {
        auto const& repo_config = RepositoryConfig::Instance();
        std::optional<std::string> blob{};
        if (auto const* ws_root = repo_config.WorkspaceRoot(repo)) {
            // try to obtain blob from local workspace's Git CAS, if any
            blob = ws_root->ReadBlob(digest.hash());
        }
        if (not blob) {
            // try to obtain blob from global Git CAS, if any
            blob = repo_config.ReadBlobFromGitCAS(digest.hash());
        }
        return blob and
               api->Upload(BlobContainer{{BazelBlob{digest, std::move(*blob)}}},
                           skip_check);
    }

    /// \brief Lookup file via path in local workspace root and upload.
    /// \param api          The endpoint used for uploading
    /// \param repo         The global repository name, the artifact belongs to
    /// \param file_path    The path of the file to be read
    /// \returns The computed object info on success
    [[nodiscard]] static auto UploadFile(
        gsl::not_null<IExecutionApi*> const& api,
        std::string const& repo,
        std::filesystem::path const& file_path) noexcept
        -> std::optional<Artifact::ObjectInfo> {
        auto const* ws_root = RepositoryConfig::Instance().WorkspaceRoot(repo);
        if (ws_root == nullptr) {
            return std::nullopt;
        }
        auto const object_type = ws_root->FileType(file_path);
        if (not object_type) {
            return std::nullopt;
        }
        auto content = ws_root->ReadFile(file_path);
        if (not content.has_value()) {
            return std::nullopt;
        }
        auto digest = ArtifactDigest{ComputeHash(*content), content->size()};
        if (not api->Upload(
                BlobContainer{{BazelBlob{digest, std::move(*content)}}})) {
            return std::nullopt;
        }
        return Artifact::ObjectInfo{std::move(digest), *object_type};
    }

    /// \brief Add digests and object type to artifact nodes for all outputs of
    /// the action that was run
    void static SaveObjectInfo(
        IExecutionResponse::ArtifactInfos const& artifacts,
        gsl::not_null<DependencyGraph::ActionNode const*> const& action,
        bool fail_artifacts) noexcept {
        for (auto const& [name, node] : action->OutputFiles()) {
            node->Content().SetObjectInfo(artifacts.at(name), fail_artifacts);
        }
        for (auto const& [name, node] : action->OutputDirs()) {
            node->Content().SetObjectInfo(artifacts.at(name), fail_artifacts);
        }
    }

    /// \brief Create root tree digest for input artifacts.
    /// \param api          The endpoint required for uploading
    /// \param artifacts    The artifacts to create the root tree digest from
    [[nodiscard]] static auto CreateRootDigest(
        gsl::not_null<IExecutionApi*> const& api,
        std::vector<DependencyGraph::NamedArtifactNodePtr> const&
            artifacts) noexcept -> std::optional<ArtifactDigest> {
        if (artifacts.size() == 1 and
            (artifacts.at(0).path == "." or artifacts.at(0).path.empty())) {
            auto const& info = artifacts.at(0).node->Content().Info();
            if (info and IsTreeObject(info->type)) {
                // Artifact list contains single tree with path "." or "". Reuse
                // the existing tree artifact by returning its digest.
                return info->digest;
            }
        }
        return api->UploadTree(artifacts);
    }
    /// \brief Check that all outputs expected from the action description
    /// are present in the artifacts map
    [[nodiscard]] static auto CheckOutputsExist(
        IExecutionResponse::ArtifactInfos const& artifacts,
        std::vector<Action::LocalPath> const& outputs) noexcept -> bool {
        for (auto const& output : outputs) {
            if (not artifacts.contains(output)) {
                return false;
            }
        }
        return true;
    }

    /// \brief Parse response and write object info to DAG's artifact nodes.
    /// \returns false on non-zero exit code or if output artifacts are missing
    [[nodiscard]] static auto ParseResponse(
        Logger const& logger,
        IExecutionResponse::Ptr const& response,
        gsl::not_null<DependencyGraph::ActionNode const*> const& action)
        -> bool {
        logger.Emit(LogLevel::Trace, "finished execution");

        if (!response) {
            logger.Emit(LogLevel::Trace, "response is empty");
            return false;
        }

        if (response->IsCached()) {
            logger.Emit(LogLevel::Trace, " - served from cache");
            Statistics::Instance().IncrementActionsCachedCounter();
        }

        PrintInfo(logger, action->Command(), response);
        bool should_fail_outputs = false;
        for (auto const& [local_path, node] : action->Dependencies()) {
            should_fail_outputs |= node->Content().Info()->failed;
        }
        if (response->ExitCode() != 0) {
            if (action->MayFail()) {
                logger.Emit(LogLevel::Warning,
                            "{} (exit code {})",
                            *(action->MayFail()),
                            response->ExitCode());
                should_fail_outputs = true;
            }
            else {
                logger.Emit(LogLevel::Error,
                            "action returned non-zero exit code {}",
                            response->ExitCode());
                PrintError(logger, action->Command());
                return false;
            }
        }

        auto artifacts = response->Artifacts();
        auto output_files = action->OutputFilePaths();
        auto output_dirs = action->OutputDirPaths();

        if (artifacts.empty() or
            not CheckOutputsExist(artifacts, output_files) or
            not CheckOutputsExist(artifacts, output_dirs)) {
            logger.Emit(LogLevel::Error, [&] {
                std::string message{
                    "action executed with missing outputs.\n"
                    " Action outputs should be the following artifacts:"};
                for (auto const& output : output_files) {
                    message += "\n  - " + output;
                }
                return message;
            });
            PrintError(logger, action->Command());
            return false;
        }

        SaveObjectInfo(artifacts, action, should_fail_outputs);

        return true;
    }

    /// \brief Write out if response is empty and otherwise, write out
    /// standard error/output if they are present
    void static PrintInfo(Logger const& logger,
                          std::vector<std::string> const& command,
                          IExecutionResponse::Ptr const& response) noexcept {
        if (!response) {
            logger.Emit(LogLevel::Error, "response is empty");
            return;
        }
        auto has_err = response->HasStdErr();
        auto has_out = response->HasStdOut();
        if (has_err or has_out) {
            logger.Emit(LogLevel::Info, [&] {
                using namespace std::string_literals;
                auto message =
                    (has_err and has_out ? "Stdout and stderr"s
                                         : has_out ? "Stdout"s : "Stderr"s) +
                    " of command: ";
                message += nlohmann::json(command).dump();
                if (response->HasStdOut()) {
                    message += "\n" + response->StdOut();
                }
                if (response->HasStdErr()) {
                    message += "\n" + response->StdErr();
                }
                return message;
            });
        }
    }

    void static PrintError(Logger const& logger,
                           std::vector<std::string> const& command) noexcept {
        logger.Emit(LogLevel::Error,
                    "Failed to execute command {}",
                    nlohmann::json(command).dump());
    }
};

/// \brief Executor for using concrete Execution API.
class Executor {
    using Impl = ExecutorImpl;
    using CF = IExecutionAction::CacheFlag;

  public:
    explicit Executor(
        IExecutionApi* api,
        std::map<std::string, std::string> properties,
        std::chrono::milliseconds timeout = IExecutionAction::kDefaultTimeout)
        : api_{api}, properties_{std::move(properties)}, timeout_{timeout} {}

    /// \brief Run an action in a blocking manner
    /// This method must be thread-safe as it could be called in parallel
    /// \param[in] action The action to execute.
    /// \returns True if execution was successful, false otherwise
    [[nodiscard]] auto Process(
        gsl::not_null<DependencyGraph::ActionNode const*> const& action)
        const noexcept -> bool {
        Logger logger("action:" + action->Content().Id());

        auto const response = Impl::ExecuteAction(
            logger,
            action,
            api_,
            properties_,
            timeout_,
            action->NoCache() ? CF::DoNotCacheOutput : CF::CacheOutput);

        // check response and save digests of results
        return not response or Impl::ParseResponse(logger, *response, action);
    }

    /// \brief Check artifact is available to the CAS or upload it.
    /// \param[in] artifact The artifact to process.
    /// \returns True if artifact is available or uploaded, false otherwise
    [[nodiscard]] auto Process(
        gsl::not_null<DependencyGraph::ArtifactNode const*> const& artifact)
        const noexcept -> bool {
        return Impl::VerifyOrUploadArtifact(artifact, api_);
    }

  private:
    gsl::not_null<IExecutionApi*> api_;
    std::map<std::string, std::string> properties_;
    std::chrono::milliseconds timeout_;
};

/// \brief Rebuilder for running and comparing actions of two API endpoints.
class Rebuilder {
    using Impl = ExecutorImpl;
    using CF = IExecutionAction::CacheFlag;

  public:
    /// \brief Create rebuilder for action comparision of two endpoints.
    /// \param api          Rebuild endpoint, executes without action cache.
    /// \param api_cached   Reference endpoint, serves everything from cache.
    /// \param properties   Platform properties for execution.
    /// \param timeout      Timeout for action execution.
    Rebuilder(
        IExecutionApi* api,
        IExecutionApi* api_cached,
        std::map<std::string, std::string> properties,
        std::chrono::milliseconds timeout = IExecutionAction::kDefaultTimeout)
        : api_{api},
          api_cached_{api_cached},
          properties_{std::move(properties)},
          timeout_{timeout} {}

    [[nodiscard]] auto Process(
        gsl::not_null<DependencyGraph::ActionNode const*> const& action)
        const noexcept -> bool {
        auto const& action_id = action->Content().Id();
        Logger logger("rebuild:" + action_id);
        auto response = Impl::ExecuteAction(
            logger, action, api_, properties_, timeout_, CF::PretendCached);

        if (not response) {
            return true;  // action without response (e.g., tree action)
        }

        Logger logger_cached("cached:" + action_id);
        auto response_cached = Impl::ExecuteAction(logger_cached,
                                                   action,
                                                   api_cached_,
                                                   properties_,
                                                   timeout_,
                                                   CF::FromCacheOnly);

        if (not response_cached) {
            logger_cached.Emit(LogLevel::Error,
                               "expected regular action with response");
            return false;
        }

        DetectFlakyAction(*response, *response_cached, action->Content());
        return Impl::ParseResponse(logger, *response, action);
    }

    [[nodiscard]] auto Process(
        gsl::not_null<DependencyGraph::ArtifactNode const*> const& artifact)
        const noexcept -> bool {
        return Impl::VerifyOrUploadArtifact(artifact, api_);
    }

    [[nodiscard]] auto DumpFlakyActions() const noexcept -> nlohmann::json {
        std::unique_lock lock{m_};
        auto actions = nlohmann::json::object();
        for (auto const& [action_id, outputs] : flaky_actions_) {
            for (auto const& [path, infos] : outputs) {
                actions[action_id][path]["rebuilt"] = infos.first.ToJson();
                actions[action_id][path]["cached"] = infos.second.ToJson();
            }
        }
        return {{"flaky actions", actions}, {"cache misses", cache_misses_}};
    }

  private:
    gsl::not_null<IExecutionApi*> api_;
    gsl::not_null<IExecutionApi*> api_cached_;
    std::map<std::string, std::string> properties_;
    std::chrono::milliseconds timeout_;
    mutable std::mutex m_;
    mutable std::vector<std::string> cache_misses_{};
    mutable std::unordered_map<
        std::string,
        std::unordered_map<
            std::string,
            std::pair<Artifact::ObjectInfo, Artifact::ObjectInfo>>>
        flaky_actions_{};

    void DetectFlakyAction(IExecutionResponse::Ptr const& response,
                           IExecutionResponse::Ptr const& response_cached,
                           Action const& action) const noexcept {
        if (response and response_cached and
            response_cached->ActionDigest() == response->ActionDigest()) {
            Statistics::Instance().IncrementRebuiltActionComparedCounter();
            auto artifacts = response->Artifacts();
            auto artifacts_cached = response_cached->Artifacts();
            std::ostringstream msg{};
            for (auto const& [path, info] : artifacts) {
                auto const& info_cached = artifacts_cached[path];
                if (info != info_cached) {
                    RecordFlakyAction(&msg, action, path, info, info_cached);
                }
            }
            if (msg.tellp() > 0) {
                Statistics::Instance().IncrementActionsFlakyCounter();
                bool tainted = action.MayFail() or action.NoCache();
                if (tainted) {
                    Statistics::Instance()
                        .IncrementActionsFlakyTaintedCounter();
                }
                Logger::Log(tainted ? LogLevel::Debug : LogLevel::Warning,
                            "{}",
                            msg.str());
            }
        }
        else {
            Statistics::Instance().IncrementRebuiltActionMissingCounter();
            std::unique_lock lock{m_};
            cache_misses_.emplace_back(action.Id());
        }
    }

    void RecordFlakyAction(gsl::not_null<std::ostringstream*> const& msg,
                           Action const& action,
                           std::string const& path,
                           Artifact::ObjectInfo const& rebuilt,
                           Artifact::ObjectInfo const& cached) const noexcept {
        auto const& action_id = action.Id();
        if (msg->tellp() <= 0) {
            bool tainted = action.MayFail() or action.NoCache();
            static constexpr auto kMaxCmdChars = 69;  // 80 - (prefix + suffix)
            auto cmd = GetCmdString(action);
            (*msg) << "Found flaky " << (tainted ? "tainted " : "")
                   << "action:" << std::endl
                   << " - id: " << action_id << std::endl
                   << " - cmd: " << cmd.substr(0, kMaxCmdChars)
                   << (cmd.length() > kMaxCmdChars ? "..." : "") << std::endl;
        }
        (*msg) << " - output '" << path << "' differs:" << std::endl
               << "   - " << rebuilt.ToString() << " (rebuilt)" << std::endl
               << "   - " << cached.ToString() << " (cached)" << std::endl;

        std::unique_lock lock{m_};
        auto& object_map = flaky_actions_[action_id];
        try {
            object_map.emplace(path, std::make_pair(rebuilt, cached));
        } catch (std::exception const& ex) {
            Logger::Log(LogLevel::Error,
                        "recoding flaky action failed with: {}",
                        ex.what());
        }
    }

    static auto GetCmdString(Action const& action) noexcept -> std::string {
        try {
            return nlohmann::json(action.Command()).dump();
        } catch (std::exception const& ex) {
            return fmt::format("<exception: {}>", ex.what());
        }
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_ENGINE_EXECUTOR_EXECUTOR_HPP
