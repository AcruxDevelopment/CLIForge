#pragma once

// Deduces the return type and argument-type pack of "anything callable":
// plain free-function pointers, non-generic lambdas (captureless or
// capturing), and std::function instances. This is what lets
// Command::action() figure out, at compile time, exactly which C++ types
// it needs to convert parsed CLI tokens into -- without the user ever
// having to spell the types out twice.

#include <cstddef>

#include <functional>
#include <tuple>

namespace cliforge::detail
{

	template <typename T> struct FunctionTraits;

	// Plain free function: void foo(std::string, int)
	template <typename Ret, typename... Args> struct FunctionTraits<Ret (*)(Args...)>
	{
		using ReturnType = Ret;
		static constexpr std::size_t Arity = sizeof...(Args);
		template <std::size_t I> using Arg = std::tuple_element_t<I, std::tuple<Args...>>;
		using ArgsTuple = std::tuple<Args...>;
	};

	// std::function<void(std::string, int)>
	template <typename Ret, typename... Args> struct FunctionTraits<std::function<Ret(Args...)>>
	{
		using ReturnType = Ret;
		static constexpr std::size_t Arity = sizeof...(Args);
		template <std::size_t I> using Arg = std::tuple_element_t<I, std::tuple<Args...>>;
		using ArgsTuple = std::tuple<Args...>;
	};

	// Pointer-to-member-function of operator(), const and non-const, used to
	// peel apart lambdas (which are just anonymous functors under the hood).
	template <typename C, typename Ret, typename... Args>
	struct FunctionTraits<Ret (C::*)(Args...) const>
	{
		using ReturnType = Ret;
		static constexpr std::size_t Arity = sizeof...(Args);
		template <std::size_t I> using Arg = std::tuple_element_t<I, std::tuple<Args...>>;
		using ArgsTuple = std::tuple<Args...>;
	};

	template <typename C, typename Ret, typename... Args> struct FunctionTraits<Ret (C::*)(Args...)>
	{
		using ReturnType = Ret;
		static constexpr std::size_t Arity = sizeof...(Args);
		template <std::size_t I> using Arg = std::tuple_element_t<I, std::tuple<Args...>>;
		using ArgsTuple = std::tuple<Args...>;
	};

	// Fallback: any other callable (lambda, functor) -- peel off operator().
	template <typename F> struct FunctionTraits : FunctionTraits<decltype(&F::operator())>
	{
	};
}
