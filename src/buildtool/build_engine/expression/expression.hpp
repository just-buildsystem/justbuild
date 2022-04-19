#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_EXPRESSION_EXPRESSION_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_EXPRESSION_EXPRESSION_HPP

#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include "fmt/core.h"
#include "gsl-lite/gsl-lite.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name_data.hpp"
#include "src/buildtool/build_engine/expression/expression_ptr.hpp"
#include "src/buildtool/build_engine/expression/function_map.hpp"
#include "src/buildtool/build_engine/expression/linked_map.hpp"
#include "src/buildtool/build_engine/expression/target_node.hpp"
#include "src/buildtool/build_engine/expression/target_result.hpp"
#include "src/buildtool/common/artifact_description.hpp"
#include "src/buildtool/crypto/hash_generator.hpp"
#include "src/utils/cpp/atomic.hpp"
#include "src/utils/cpp/hex_string.hpp"
#include "src/utils/cpp/json.hpp"

class Expression {
    friend auto operator+(Expression const& /*lhs*/, Expression const& /*rhs*/)
        -> Expression;

  public:
    using none_t = std::monostate;
    using number_t = double;
    using artifact_t = ArtifactDescription;
    using result_t = TargetResult;
    using node_t = TargetNode;
    using list_t = std::vector<ExpressionPtr>;
    using map_t = LinkedMap<std::string, ExpressionPtr, ExpressionPtr>;
    using name_t = BuildMaps::Base::EntityName;

    template <class T, size_t kIndex = 0>
    static consteval auto IsValidType() -> bool {
        if constexpr (kIndex < std::variant_size_v<decltype(data_)>) {
            return std::is_same_v<
                       T,
                       std::variant_alternative_t<kIndex, decltype(data_)>> or
                   IsValidType<T, kIndex + 1>();
        }
        return false;
    }

    class ExpressionTypeError : public std::exception {
      public:
        explicit ExpressionTypeError(std::string const& msg) noexcept
            : msg_{"ExpressionTypeError: " + msg} {}
        [[nodiscard]] auto what() const noexcept -> char const* final {
            return msg_.c_str();
        }

      private:
        std::string msg_;
    };

    Expression() noexcept = default;
    ~Expression() noexcept = default;
    Expression(Expression const& other) noexcept
        : data_{other.data_}, hash_{other.hash_.load()} {}
    Expression(Expression&& other) noexcept
        : data_{std::move(other.data_)}, hash_{other.hash_.load()} {}
    auto operator=(Expression const& other) noexcept -> Expression& {
        if (this != &other) {
            data_ = other.data_;
        }
        hash_ = other.hash_.load();
        return *this;
    }
    auto operator=(Expression&& other) noexcept -> Expression& {
        data_ = std::move(other.data_);
        hash_ = other.hash_.load();
        return *this;
    }

    template <class T>
    requires(IsValidType<std::remove_cvref_t<T>>())
        // NOLINTNEXTLINE(bugprone-forwarding-reference-overload)
        explicit Expression(T&& data) noexcept
        : data_{std::forward<T>(data)} {}

    [[nodiscard]] auto IsNone() const noexcept -> bool { return IsA<none_t>(); }
    [[nodiscard]] auto IsBool() const noexcept -> bool { return IsA<bool>(); }
    [[nodiscard]] auto IsNumber() const noexcept -> bool {
        return IsA<number_t>();
    }
    [[nodiscard]] auto IsString() const noexcept -> bool {
        return IsA<std::string>();
    }
    [[nodiscard]] auto IsName() const noexcept -> bool { return IsA<name_t>(); }
    [[nodiscard]] auto IsArtifact() const noexcept -> bool {
        return IsA<artifact_t>();
    }
    [[nodiscard]] auto IsResult() const noexcept -> bool {
        return IsA<result_t>();
    }
    [[nodiscard]] auto IsNode() const noexcept -> bool { return IsA<node_t>(); }
    [[nodiscard]] auto IsList() const noexcept -> bool { return IsA<list_t>(); }
    [[nodiscard]] auto IsMap() const noexcept -> bool { return IsA<map_t>(); }

    [[nodiscard]] auto Bool() const -> bool { return Cast<bool>(); }
    [[nodiscard]] auto Number() const -> number_t { return Cast<number_t>(); }
    [[nodiscard]] auto Name() const -> name_t { return Cast<name_t>(); }
    [[nodiscard]] auto String() const& -> std::string const& {
        return Cast<std::string>();
    }
    [[nodiscard]] auto String() && -> std::string {
        return std::move(*this).Cast<std::string>();
    }
    [[nodiscard]] auto Artifact() const& -> artifact_t const& {
        return Cast<artifact_t>();
    }
    [[nodiscard]] auto Artifact() && -> artifact_t {
        return std::move(*this).Cast<artifact_t>();
    }
    [[nodiscard]] auto Result() const& -> result_t const& {
        return Cast<result_t>();
    }
    [[nodiscard]] auto Result() && -> result_t {
        return std::move(*this).Cast<result_t>();
    }
    [[nodiscard]] auto Node() const& -> node_t const& { return Cast<node_t>(); }
    [[nodiscard]] auto Node() && -> node_t {
        return std::move(*this).Cast<node_t>();
    }
    [[nodiscard]] auto List() const& -> list_t const& { return Cast<list_t>(); }
    [[nodiscard]] auto List() && -> list_t {
        return std::move(*this).Cast<list_t>();
    }
    [[nodiscard]] auto Map() const& -> map_t const& { return Cast<map_t>(); }
    [[nodiscard]] auto Map() && -> map_t {
        return std::move(*this).Cast<map_t>();
    }

    [[nodiscard]] auto At(std::string const& key)
        const& -> std::optional<std::reference_wrapper<ExpressionPtr const>> {
        auto value = Map().Find(key);
        if (value) {
            return value;
        }
        return std::nullopt;
    }

    [[nodiscard]] auto At(
        std::string const& key) && -> std::optional<ExpressionPtr> {
        auto value = std::move(*this).Map().Find(key);
        if (value) {
            return std::move(*value);
        }
        return std::nullopt;
    }

    template <class T>
    requires(IsValidType<std::remove_cvref_t<T>>() or
             std::is_same_v<std::remove_cvref_t<T>, ExpressionPtr>)
        [[nodiscard]] auto Get(std::string const& key, T&& default_value) const
        -> ExpressionPtr {
        auto value = At(key);
        if (value) {
            return value->get();
        }
        if constexpr (std::is_same_v<std::remove_cvref_t<T>, ExpressionPtr>) {
            return std::forward<T>(default_value);
        }
        else {
            return ExpressionPtr{std::forward<T>(default_value)};
        }
    }

    template <class T>
    requires(IsValidType<T>()) [[nodiscard]] auto Value() const& noexcept
        -> std::optional<std::reference_wrapper<T const>> {
        if (GetIndexOf<T>() == data_.index()) {
            return std::make_optional(std::ref(std::get<T>(data_)));
        }
        return std::nullopt;
    }

    template <class T>
    requires(IsValidType<T>()) [[nodiscard]] auto Value() && noexcept
        -> std::optional<T> {
        if (GetIndexOf<T>() == data_.index()) {
            return std::make_optional(std::move(std::get<T>(data_)));
        }
        return std::nullopt;
    }

    template <class T>
    [[nodiscard]] auto operator==(T const& other) const noexcept -> bool {
        if constexpr (std::is_same_v<T, Expression>) {
            return (&data_ == &other.data_) or (ToHash() == other.ToHash());
        }
        else {
            return IsValidType<T>() and (GetIndexOf<T>() == data_.index()) and
                   ((static_cast<void const*>(&data_) ==
                     static_cast<void const*>(&other)) or
                    (std::get<T>(data_) == other));
        }
    }

    template <class T>
    [[nodiscard]] auto operator!=(T const& other) const noexcept -> bool {
        return !(*this == other);
    }
    [[nodiscard]] auto operator[](
        std::string const& key) const& -> ExpressionPtr const&;
    [[nodiscard]] auto operator[](std::string const& key) && -> ExpressionPtr;
    [[nodiscard]] auto operator[](
        ExpressionPtr const& key) const& -> ExpressionPtr const&;
    [[nodiscard]] auto operator[](ExpressionPtr const& key) && -> ExpressionPtr;
    [[nodiscard]] auto operator[](size_t pos) const& -> ExpressionPtr const&;
    [[nodiscard]] auto operator[](size_t pos) && -> ExpressionPtr;

    enum class JsonMode { SerializeAll, SerializeAllButNodes, NullForNonJson };

    [[nodiscard]] auto ToJson(JsonMode mode = JsonMode::SerializeAll) const
        -> nlohmann::json;
    [[nodiscard]] auto IsCacheable() const -> bool;
    [[nodiscard]] auto ToString() const -> std::string;
    [[nodiscard]] auto ToHash() const noexcept -> std::string;
    [[nodiscard]] auto ToIdentifier() const noexcept -> std::string {
        return ToHexString(ToHash());
    }

    [[nodiscard]] static auto FromJson(nlohmann::json const& json) noexcept
        -> ExpressionPtr;

    inline static ExpressionPtr const kNone = Expression::FromJson("null"_json);
    inline static ExpressionPtr const kEmptyMap =
        Expression::FromJson("{}"_json);
    inline static ExpressionPtr const kEmptyList =
        Expression::FromJson("[]"_json);
    inline static ExpressionPtr const kEmptyMapExpr =
        Expression::FromJson(R"({"type": "empty_map"})"_json);

  private:
    inline static HashGenerator const hash_gen_{
        HashGenerator::HashType::SHA256};

    std::variant<none_t,
                 bool,
                 number_t,
                 std::string,
                 name_t,
                 artifact_t,
                 result_t,
                 node_t,
                 list_t,
                 map_t>
        data_{none_t{}};

    mutable atomic_shared_ptr<std::string> hash_{};
    mutable std::atomic<bool> hash_loading_{};

    template <class T, std::size_t kIndex = 0>
    requires(IsValidType<T>()) [[nodiscard]] static consteval auto GetIndexOf()
        -> std::size_t {
        static_assert(kIndex < std::variant_size_v<decltype(data_)>,
                      "kIndex out of range");
        if constexpr (std::is_same_v<
                          T,
                          std::variant_alternative_t<kIndex,
                                                     decltype(data_)>>) {
            return kIndex;
        }
        else {
            return GetIndexOf<T, kIndex + 1>();
        }
    }

    template <class T>
    [[nodiscard]] auto IsA() const noexcept -> bool {
        return std::holds_alternative<T>(data_);
    }

    template <class T>
    [[nodiscard]] auto Cast() const& -> T const& {
        if (GetIndexOf<T>() == data_.index()) {
            return std::get<T>(data_);
        }
        // throw descriptive ExpressionTypeError
        throw ExpressionTypeError{
            fmt::format("Expression is not of type '{}' but '{}'.",
                        TypeToString<T>(),
                        TypeString())};
    }

    template <class T>
    [[nodiscard]] auto Cast() && -> T {
        if (GetIndexOf<T>() == data_.index()) {
            return std::move(std::get<T>(data_));
        }
        // throw descriptive ExpressionTypeError
        throw ExpressionTypeError{
            fmt::format("Expression is not of type '{}' but '{}'.",
                        TypeToString<T>(),
                        TypeString())};
    }

    template <class T>
    requires(Expression::IsValidType<T>())
        [[nodiscard]] static auto TypeToString() noexcept -> std::string {
        if constexpr (std::is_same_v<T, bool>) {
            return "bool";
        }
        else if constexpr (std::is_same_v<T, Expression::number_t>) {
            return "number";
        }
        else if constexpr (std::is_same_v<T, Expression::name_t>) {
            return "name";
        }
        else if constexpr (std::is_same_v<T, std::string>) {
            return "string";
        }
        else if constexpr (std::is_same_v<T, Expression::artifact_t>) {
            return "artifact";
        }
        else if constexpr (std::is_same_v<T, Expression::result_t>) {
            return "result";
        }
        else if constexpr (std::is_same_v<T, Expression::node_t>) {
            return "node";
        }
        else if constexpr (std::is_same_v<T, Expression::list_t>) {
            return "list";
        }
        else if constexpr (std::is_same_v<T, Expression::map_t>) {
            return "map";
        }
        return "none";
    }

    template <size_t kIndex = 0>
    [[nodiscard]] auto TypeStringForIndex() const noexcept -> std::string;
    [[nodiscard]] auto TypeString() const noexcept -> std::string;
    [[nodiscard]] auto ComputeHash() const noexcept -> std::string;
};

namespace std {
template <>
struct hash<Expression> {
    [[nodiscard]] auto operator()(Expression const& e) const noexcept
        -> std::size_t {
        auto hash = std::size_t{};
        auto bytes = e.ToHash();
        std::memcpy(&hash, bytes.data(), std::min(sizeof(hash), bytes.size()));
        return hash;
    }
};
}  // namespace std

#endif  // INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_EXPRESSION_EXPRESSION_HPP
