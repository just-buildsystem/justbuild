#ifndef INCLUDED_SRC_UTILS_CPP_CONCEPTS_HPP
#define INCLUDED_SRC_UTILS_CPP_CONCEPTS_HPP

#include <string>
#include <type_traits>

// TODO(modernize): remove this once std::derived_from is shipped with libcxx
template <class T, class U>
concept derived_from = std::is_base_of_v<U, T>&&
    std::is_convertible_v<const volatile T*, const volatile U*>;

// TODO(modernize): remove this once std::same_as is shipped with libcxx
template <class T, class U>
concept same_as = std::is_same_v<T, U>and std::is_same_v<U, T>;

template <class T>
concept ContainsString = requires {
    typename T::value_type;
}
and std::is_same_v<typename T::value_type, std::string>;

template <class T>
concept HasSize = requires(T const c) {
    { c.size() }
    ->same_as<std::size_t>;  // TODO(modernize): replace by std::same_as
};

template <typename T>
concept HasToString = requires(T const t) {
    { t.ToString() }
    ->same_as<std::string>;  // TODO(modernize): replace by std::same_as
};

template <class T>
concept InputIterableContainer = requires(T const c) {
    { c.begin() }
    ->same_as<typename T::const_iterator>;  // TODO(modernize): replace by
                                            // std::input_iterator
    { c.end() }
    ->same_as<typename T::const_iterator>;  // TODO(modernize): replace by
                                            // std::input_iterator
};

template <class T>
concept OutputIterableContainer = InputIterableContainer<T>and requires(T c) {
    { std::inserter(c, c.begin()) }
    ->same_as<std::insert_iterator<T>>;  // TODO(modernize): replace by
                                         // std::output_iterator
};

template <class T>
concept InputIterableStringContainer =
    InputIterableContainer<T>and ContainsString<T>;

#endif  // INCLUDED_SRC_UTILS_CPP_CONCEPTS_HPP
