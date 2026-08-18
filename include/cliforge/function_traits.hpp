#pragma once
//
// function_traits.hpp
//
// Deduces the return type and argument-type pack of "anything callable":
// plain free-function pointers, non-generic lambdas (captureless or
// capturing), and std::function instances. This is what lets
// Command::action() figure out, at compile time, exactly which C++ types
// it needs to convert parsed CLI tokens into -- without the user ever
// having to spell the types out twice.
//
#include <cstddef>
#include <functional>
#include <tuple>

namespace cliforge::detail {

template <typename T>
struct function_traits;

// Plain free function: void foo(std::string, int)
template <typename Ret, typename... Args>
struct function_traits<Ret (*)(Args...)> {
    using return_type = Ret;
    static constexpr std::size_t arity = sizeof...(Args);
    template <std::size_t I>
    using arg = std::tuple_element_t<I, std::tuple<Args...>>;
    using args_tuple = std::tuple<Args...>;
};

// std::function<void(std::string, int)>
template <typename Ret, typename... Args>
struct function_traits<std::function<Ret(Args...)>> {
    using return_type = Ret;
    static constexpr std::size_t arity = sizeof...(Args);
    template <std::size_t I>
    using arg = std::tuple_element_t<I, std::tuple<Args...>>;
    using args_tuple = std::tuple<Args...>;
};

// Pointer-to-member-function of operator(), const and non-const, used to
// peel apart lambdas (which are just anonymous functors under the hood).
template <typename C, typename Ret, typename... Args>
struct function_traits<Ret (C::*)(Args...) const> {
    using return_type = Ret;
    static constexpr std::size_t arity = sizeof...(Args);
    template <std::size_t I>
    using arg = std::tuple_element_t<I, std::tuple<Args...>>;
    using args_tuple = std::tuple<Args...>;
};

template <typename C, typename Ret, typename... Args>
struct function_traits<Ret (C::*)(Args...)> {
    using return_type = Ret;
    static constexpr std::size_t arity = sizeof...(Args);
    template <std::size_t I>
    using arg = std::tuple_element_t<I, std::tuple<Args...>>;
    using args_tuple = std::tuple<Args...>;
};

// Fallback: any other callable (lambda, functor) -- peel off operator().
template <typename F>
struct function_traits : function_traits<decltype(&F::operator())> {};

}  // namespace cliforge::detail
