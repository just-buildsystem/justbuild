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

#ifndef INCLUDED_SRC_UTILS_CPP_BACK_MAP_HPP
#define INCLUDED_SRC_UTILS_CPP_BACK_MAP_HPP

#include <cstddef>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>  // IWYU pragma: keep

#include "gsl/gsl"

/// \brief Backmap a container of Values to Keys using a given Converter, and
/// provide an interface for quick lookup of values by keys.
/// \tparam TKey        Result of conversion. Stored by value, std::hash<TKey>
/// MUST be available.
/// \tparam TValue      Source data for conversion. Stored by pointer, the
/// source container MUST stay alive.
template <typename TKey, typename TValue>
class BackMap final {
  public:
    template <typename T>
    using IsKeyWithError = std::bool_constant<std::is_convertible_v<TKey, T> and
                                              std::is_convertible_v<T, bool>>;

    /// \brief Converter from TValue to TKey. The return type may be different
    /// from TKey, in such a case Converter is considered a function that DOES
    /// return TKey, but may fail and return std::nullopt or an unexpected<T>.
    /// TResult must be either the same type as TKey, or support conversions
    /// from TKey and to bool.
    template <typename TResult>
        requires(std::is_same_v<TResult, TKey> or
                 IsKeyWithError<TResult>::value)
    using Converter = std::function<TResult(TValue const&)>;

    explicit BackMap() = default;
    BackMap(BackMap const&) = delete;
    BackMap(BackMap&&) = delete;
    auto operator=(BackMap const&) -> BackMap& = delete;
    auto operator=(BackMap&&) -> BackMap& = delete;
    ~BackMap() = default;

    /// \brief Create a BackMap by iterating over container and applying
    /// Converter.
    /// \param container Container to iterate over. begin() and end() methods
    /// must be available.
    /// \param converter Converter to apply to elements of the container.
    /// \return BackMap on success or std::nullopt on failure. In particular, if
    /// any exception is thrown from internal containers or converter returns an
    /// error, std::nullopt is returned.
    template <typename TContainer, typename TResult>
    [[nodiscard]] static auto Make(TContainer const* const container,
                                   Converter<TResult> const& converter) noexcept
        -> std::unique_ptr<BackMap> {
        if (container == nullptr or converter == nullptr) {
            return nullptr;
        }
        auto const hasher = std::hash<TKey>{};
        auto const size = std::distance(container->begin(), container->end());

        try {
            auto back_map = std::make_unique<BackMap>();
            back_map->keys_.reserve(size);
            back_map->mapping_.reserve(size);

            for (auto const& value : *container) {
                std::optional<TKey> key = BackMap::Convert(converter, value);
                if (not key.has_value()) {
                    return nullptr;
                }
                std::size_t const hash = std::invoke(hasher, *key);
                if (not back_map->mapping_.contains(hash)) {
                    back_map->keys_.emplace(*std::move(key));
                    back_map->mapping_.insert_or_assign(hash, &value);
                }
            }
            return back_map;
        } catch (...) {
            return nullptr;
        }
    }

    template <typename TContainer, typename TConverter>
        requires(std::is_invocable_v<TConverter, TValue const&>)
    [[nodiscard]] static auto Make(TContainer const* const container,
                                   TConverter const& converter) noexcept
        -> std::unique_ptr<BackMap> {
        using TResult = std::invoke_result_t<TConverter, TValue const&>;
        return Make<TContainer, TResult>(container, converter);
    }

    /// \brief Obtain all available keys.
    [[nodiscard]] auto GetKeys() const noexcept
        -> std::unordered_set<TKey> const& {
        return keys_;
    }

    /// \brief Obtain a pointer to the value corresponding to the given key.
    /// Copy free.
    /// \return If the key is known, a pointer to the value is returned,
    /// otherwise std::nullopt.
    [[nodiscard]] auto GetReference(TKey const& key) const noexcept
        -> std::optional<gsl::not_null<TValue const*>> {
        auto const hash = std::invoke(std::hash<TKey>{}, key);
        if (auto it = mapping_.find(hash); it != mapping_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    /// \brief Obtain the set of values corresponding to given keys. If a key
    /// isn't known to the container, it is ignored. Perform deep copy of
    /// referenced values.
    [[nodiscard]] auto GetValues(std::unordered_set<TKey> const& keys)
        const noexcept -> std::unordered_set<TValue> {
        std::unordered_set<TValue> result;
        result.reserve(keys.size());
        for (auto const& key : keys) {
            if (auto value = GetReference(key)) {
                result.emplace(*value.value());
            }
        }
        return result;
    }

    using Iterable = std::unordered_map<gsl::not_null<TKey const*>,
                                        gsl::not_null<TValue const*>>;

    /// \brief Obtain an iterable key-value set where values correspond to the
    /// given keys. If a key isn't known to the container, it is ignored. Copy
    /// free.
    [[nodiscard]] auto IterateReferences(
        std::unordered_set<TKey> const* keys) const noexcept -> Iterable {
        Iterable result;
        result.reserve(keys->size());

        for (auto const& key : *keys) {
            if (auto value = GetReference(key)) {
                result.insert_or_assign(&key, value.value());
            }
        }
        return result;
    }

  private:
    std::unordered_set<TKey> keys_;
    std::unordered_map<std::size_t, gsl::not_null<TValue const*>> mapping_;

    template <typename TResult>
    [[nodiscard]] static auto Convert(Converter<TResult> const& converter,
                                      TValue const& value) noexcept
        -> std::optional<TKey> {
        if constexpr (not std::is_same_v<TKey, TResult>) {
            TResult converted = std::invoke(converter, value);
            if (not static_cast<bool>(converted)) {
                return std::nullopt;
            }
            return *std::move(converted);
        }
        else {
            return std::invoke(converter, value);
        }
    }
};

#endif  // INCLUDED_SRC_UTILS_CPP_BACK_MAP_HPP
