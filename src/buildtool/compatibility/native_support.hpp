#ifndef INCLUDED_SRC_BUILDTOOL_COMPATIBILITY_NATIVE_SUPPORT_HPP
#define INCLUDED_SRC_BUILDTOOL_COMPATIBILITY_NATIVE_SUPPORT_HPP

#include <string>

#include <gsl-lite/gsl-lite.hpp>

#include "src/buildtool/compatibility/compatibility.hpp"

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
        gsl_ExpectsAudit(not IsPrefixed(hash));
        return (is_tree ? kTreeTag : kBlobTag) + hash;
    }

    [[nodiscard]] static auto Unprefix(std::string const& hash) noexcept
        -> std::string {
        if (Compatibility::IsCompatible()) {
            return hash;
        }
        gsl_ExpectsAudit(IsPrefixed(hash));
        return hash.substr(kTagLength);
    }

    [[nodiscard]] static auto IsTree(std::string const& hash) noexcept -> bool {
        return IsPrefixed(hash) and hash.starts_with(kTreeTag);
    }
};
#endif  // INCLUDED_SRC_BUILDTOOL_COMPATIBILITY_NATIVE_SUPPORT_HPP
