#ifndef INCLUDED_SRC_COMMON_ARTIFACT_DIGEST_HPP
#define INCLUDED_SRC_COMMON_ARTIFACT_DIGEST_HPP

#include <optional>
#include <string>

#include "gsl-lite/gsl-lite.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/crypto/hash_generator.hpp"
#include "src/utils/cpp/hash_combine.hpp"

// Wrapper for bazel_re::Digest. Can be implicitly cast to bazel_re::Digest.
// Provides getter for size with convenient non-protobuf type.
class ArtifactDigest {
  public:
    ArtifactDigest() noexcept = default;
    explicit ArtifactDigest(bazel_re::Digest digest) noexcept
        : size_{gsl::narrow<std::size_t>(digest.size_bytes())},
          digest_{std::move(digest)} {}
    ArtifactDigest(std::string hash, std::size_t size) noexcept
        : size_{size}, digest_{CreateBazelDigest(std::move(hash), size)} {}

    [[nodiscard]] auto hash() const& noexcept -> std::string const& {
        return digest_.hash();
    }

    [[nodiscard]] auto hash() && noexcept -> std::string {
        return std::move(*digest_.mutable_hash());
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t { return size_; }

    // NOLINTNEXTLINE allow implicit casts
    [[nodiscard]] operator bazel_re::Digest const &() const& { return digest_; }
    // NOLINTNEXTLINE allow implicit casts
    [[nodiscard]] operator bazel_re::Digest() && { return std::move(digest_); }

    [[nodiscard]] auto operator==(ArtifactDigest const& other) const -> bool {
        return std::equal_to<bazel_re::Digest>{}(*this, other);
    }

    [[nodiscard]] static auto Create(std::string const& content) noexcept
        -> ArtifactDigest {
        return ArtifactDigest{ComputeHash(content), content.size()};
    }

  private:
    std::size_t size_{};
    bazel_re::Digest digest_{};

    [[nodiscard]] static auto CreateBazelDigest(std::string&& hash,
                                                std::size_t size)
        -> bazel_re::Digest {
        bazel_re::Digest d;
        d.set_hash(std::move(hash));
        d.set_size_bytes(gsl::narrow<google::protobuf::int64>(size));
        return d;
    }
};

namespace std {
template <>
struct hash<ArtifactDigest> {
    [[nodiscard]] auto operator()(ArtifactDigest const& digest) const noexcept
        -> std::size_t {
        std::size_t seed{};
        hash_combine(&seed, digest.hash());
        hash_combine(&seed, digest.size());
        return seed;
    }
};
}  // namespace std

#endif  // INCLUDED_SRC_COMMON_ARTIFACT_DIGEST_HPP
