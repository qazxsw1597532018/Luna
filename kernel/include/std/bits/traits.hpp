#pragma once

#include <stddef.h>

namespace std
{
    template<typename...>
    using void_t = void;

    template<typename T, T v>
    struct integral_constant {
        static constexpr T value = v;
        using value_type = T;
        using type = integral_constant; // using injected-class-name
        constexpr operator value_type() const noexcept { return value; }
        constexpr value_type operator()() const noexcept { return value; } //since c++14
    };

    using true_type = integral_constant<bool, true>;
    using false_type = integral_constant<bool, false>;

    template<typename T> struct remove_reference { typedef T type; };
    template<typename T> struct remove_reference<T&> { typedef T type; };
    template<typename T> struct remove_reference<T&&> { typedef T type; };

    template<typename T>
    using remove_reference_t = typename remove_reference<T>::type;

    template<typename T> struct remove_cv { typedef T type; };
    template<typename T> struct remove_cv<const T> { typedef T type; };
    template<typename T> struct remove_cv<volatile T> { typedef T type; };
    template<typename T> struct remove_cv<const volatile T> { typedef T type; };

    template<typename T>
    using remove_cv_t = typename remove_cv<T>::type;

    template<typename T, typename U> struct is_same : std::false_type {};
    template<typename T> struct is_same<T, T> : std::true_type {};

    template<typename T, typename U>
    inline constexpr bool is_same_v = is_same<T, U>::value;

    template<typename T> struct is_lvalue_reference : std::false_type {};
    template<typename T> struct is_lvalue_reference<T&> : std::true_type {};

    template<typename T> struct is_const : std::false_type {};
    template<typename T> struct is_const<const T> : std::true_type {};

    template<typename T> struct is_void : std::is_same<void, typename std::remove_cv<T>::type> {};

    template<typename T> struct is_reference : std::false_type {};
    template<typename T> struct is_reference<T&> : std::true_type {};
    template<typename T> struct is_reference<T&&> : std::true_type {};

    template<class T>
    struct is_array : std::false_type {};
 
    template<class T>
    struct is_array<T[]> : std::true_type {};
 
    template<class T, size_t N>
    struct is_array<T[N]> : std::true_type {};

    template<class T>
    inline constexpr bool is_array_v = is_array<T>::value;

    template<typename T>
    inline constexpr bool is_reference_v = is_reference<T>::value;

    template<typename T>
    struct is_function : std::integral_constant<bool, !std::is_const<const T>::value && !std::is_reference<T>::value> {};

    template<typename T>
    inline constexpr bool is_function_v = is_function<T>::value;

    template<typename T>
    struct type_identity { using type = T; };

    template<typename T>
    using type_identity_t = typename type_identity<T>::type;

    namespace detail
    {
        template<typename T>
        auto try_add_rvalue_reference(int) -> std::type_identity<T&&>;

        template<typename T>
        auto try_add_rvalue_reference(...) -> std::type_identity<T>;

        template <typename T>
        auto try_add_lvalue_reference(int) -> std::type_identity<T&>;

        template <typename T>
        auto try_add_lvalue_reference(...) -> std::type_identity<T>;
    } // namespace detail

    template<typename T>
    struct add_rvalue_reference : decltype(detail::try_add_rvalue_reference<T>(0)) {};

    template<typename T>
    using add_rvalue_reference_t = typename add_rvalue_reference<T>::type;

    template <typename T>
    struct add_lvalue_reference : decltype(detail::try_add_lvalue_reference<T>(0)) {};

    template<typename T>
    using add_lvalue_reference_t = typename add_lvalue_reference<T>::type;

    template<bool B, typename T, typename F>
    struct conditional { typedef T type; };

    template<typename T, typename F>
    struct conditional<false, T, F> { typedef F type; };

    template<bool B, typename T, typename F>
    using conditional_t = typename conditional<B,T,F>::type;

    template<class T>
    struct remove_extent { typedef T type; };
 
    template<class T>
    struct remove_extent<T[]> { typedef T type; };
 
    template<class T, size_t N>
    struct remove_extent<T[N]> { typedef T type; };

    template<class T>
    using remove_extent_t = typename remove_extent<T>::type;

    namespace detail {
        template <class T>
        auto try_add_pointer(int) -> std::type_identity<typename std::remove_reference<T>::type*>;
        template <class T>
        auto try_add_pointer(...) -> std::type_identity<T>;
    } // namespace detail
 
    template <class T>
    struct add_pointer : decltype(detail::try_add_pointer<T>(0)) {};
} // namespace std
