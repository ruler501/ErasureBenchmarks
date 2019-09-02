#undef NDEBUG
#include <algorithm>
#include <cassert>
#include <functional>
#include <type_traits>
#include <utility>

#ifdef MANUAL_BENCH
    #include <chrono>
    #include <iostream>
    using namespace std::chrono;
#endif // MANUAL_BENCH

#ifdef STD_VARIANT
    #include <variant>
#endif
#ifdef DYNO
    #include <dyno.hpp>
#endif
#ifdef FOLLY_POLY
    #include <folly/Poly.h>
#endif
#ifdef MPARK_VARIANT
    #include <mpark/variant.hpp>
#endif
#ifdef GOOGLE_BENCH
    #include<benchmark/benchmark.h>
    #define TEST_ARG benchmark::State& state
#else
    #define TEST_ARG
#endif
#ifdef _MSC_VER
  #define NO_INLINE(...) __declspec(noinline) __VA_ARGS__
#else // _MSC_VER
  #define NO_INLINE(...) __VA_ARGS__ __attribute((noinline))
#endif // _MSC_VER
#ifndef NO_INLINE_CALL_MEMBER
    #define PREVENT_INLINE_CALL_MEMBER(...) constexpr __VA_ARGS__
#else // ALLOW_INLINE
    #define PREVENT_INLINE_CALL_MEMBER(...) NO_INLINE(__VA_ARGS__)
#endif // ALLOW_INLINE
#ifndef NO_INLINE_MEMBERS
    #define PREVENT_MEMBER_INLINE(...) __VA_ARGS__
#else // ALLOW_MEMBER_INLINE
    #define PREVENT_MEMBER_INLINE(...) NO_INLINE(__VA_ARGS__)
#endif // ALLOW_MEMBER_INLINE
#ifdef NO_SELECTOR
#define USE_SELECTOR(Type) Type
#else // NO_SELECTOR
#define USE_SELECTOR(Type) Type<__LINE__>
#endif // !NO_SELECTOR

namespace detail {
    template<typename... Types>
    struct type_list {};

    template<typename T, T val>
    struct value_type_impl {};

    template<auto val>
    using value_type = value_type_impl<decltype(val), val>;

    template<auto val>
    value_type<val> value_type_v;

#ifdef CAPTURING_LAMBDA
    template<typename S>
    struct function_ref;

    template <typename Return, typename... Args>
    struct function_ref<Return(Args...) noexcept> {
        using signature = Return(Args...) noexcept;

        template <typename Functor>
#ifdef __clang__
        constexpr explicit function_ref(Functor& f) : data{reinterpret_cast<void*>(&f)}, callback{[](void* memory, Args... args) noexcept { return static_cast<Return>(static_cast<Functor*>(memory)->operator()(args...)); }}
#else // __clang__
        constexpr explicit function_ref(Functor& f) : data{reinterpret_cast<void*>(&f)}, callback{[](Args... args, void* memory) noexcept { return static_cast<Return>(static_cast<Functor*>(memory)->operator()(args...)); }}
#endif // __clang__
{}

        template <typename Functor, Return(Functor::*fn)(Args...) noexcept>
#ifdef __clang__
        constexpr explicit function_ref(Functor& f, value_type<fn>) : data{reinterpret_cast<void*>(&f)}, callback{[](void* memory, Args... args) noexcept { return static_cast<Return>((static_cast<Functor*>(memory)->*fn)(args...)); }}
#else // __clang__
        constexpr explicit function_ref(Functor& f, value_type<fn>) : data{reinterpret_cast<void*>(&f)}, callback{[](Args... args, void* memory) noexcept { return static_cast<Return>((static_cast<Functor*>(memory)->*fn)(args...)); }}
#endif // __clang__
        {}

        template<typename... Args2>
        constexpr Return operator()(Args2... args) noexcept {
#ifdef __clang__
            return (*callback)(data, std::forward<Args2>(args)...);
#else // __clang__
            return (*callback)(std::forward<Args2>(args)..., data);
#endif // __clang__
        }
#ifdef __clang__
        using callback_type = Return (*)(void*, Args...) noexcept;
#else // __clang__
        using callback_type = Return (*)(Args..., void*) noexcept;
#endif // __clang__
        void* data;
        callback_type callback;
    };
#endif // CAPTURING_LAMBDA

    template<size_t size, size_t align>
    struct initializing_buffer {
        union type
        {
            std::byte __data[size];
#ifdef _MSC_VER
            struct __declspec(align(align)) {} __align;
#else // _MSC_VER
            struct __attribute__((__aligned__((align)))) { } __align;
#endif // _MSC_VER
        };

        type value;
        using deletePtrType = void(*)(void*);
        deletePtrType deletePtr;

        template<typename T, typename=std::enable_if_t<!std::is_same_v<std::decay_t<T>, initializing_buffer>>>
        explicit constexpr initializing_buffer(T&& t) : deletePtr{[](void* memory){ delete static_cast<std::decay_t<T>*>(memory); }} {
            using TR = std::decay_t<T>;
            new (value.__data) TR{std::forward<T>(t)};
            static_assert(sizeof(TR) <= size);
            static_assert(alignof(TR) <= align && align % alignof(TR) == 0);
        }

        template<typename T, typename... Args>
        constexpr initializing_buffer(type_list<T>, Args&&... args) : deletePtr([](void* memory){ delete static_cast<T*>(memory);}) {
            new (value.__data) T{std::forward<Args>(args)...};
            static_assert(sizeof(T) <= size);
            static_assert(alignof(T) <= align && align % alignof(T) == 0);
        }

        constexpr operator void*() { // NOLINT(google-explicit-constructor,hicpp-explicit-conversions)
            return value.__data;
        }

        constexpr operator void*() const { // NOLINT(google-explicit-constructor,hicpp-explicit-conversions)
            return value.__data;
        }
    };

#if defined(UNSAFE_MEMBER_PTR_CAST) || defined(UNSAFE_FUNC_PTR_CAST)
    template<typename Class, typename Return, typename... Args>
    using MemFunc = Return(Class::*)(Args...);

    template<typename Class1, typename Class2, typename Return, typename... Args>
    union MemFuncUnion {
        using funcType = Return(*)(void*, Args...) noexcept;
        MemFunc<Class1, Return, Args...> memFunc1;
        funcType fptr;
        MemFunc<Class2, Return, Args...> memFunc2;
    };

    template<typename Class, typename Return, typename... Args>
    using ConstMemFunc = Return(Class::*)(Args...) noexcept;

    template<typename Class1, typename Class2, typename Return, typename... Args>
    union ConstMemFuncUnion {
        using funcType = Return(*)(void*, Args...) noexcept;
        ConstMemFunc<Class1, Return, Args...> memFunc1;
        funcType fptr;
        ConstMemFunc<Class2, Return, Args...> memFunc2;
    };
#endif // UNSAFE_MEMBER_PTR_CAST || UNSAFE_FUNC_PTR_CAST
}

struct IA {
    [[nodiscard]] virtual PREVENT_MEMBER_INLINE(size_t) testMember(size_t, size_t) noexcept = 0;
    [[nodiscard]] virtual PREVENT_MEMBER_INLINE(size_t) testMember2(size_t, size_t) noexcept = 0;
    virtual ~IA() = default;
};

#ifdef MEMBER_PTR
#ifdef NO_SELECTOR
template<std::size_t maxSize, std::size_t alignment>
#else // NO_SELECTOR
template<std::size_t maxSize, std::size_t alignment, size_t selector>
#endif // NO_SELECTOR
struct ErasedA final {
    struct helper_A {
        template<typename T>
        explicit helper_A(detail::type_list<T>)
#ifdef __clang__
                : testMemberPtr([](void* memory, size_t a, size_t b) noexcept { return static_cast<T*>(memory)->testMember(a, b); }),
                  testMember2Ptr([](void* memory, size_t a, size_t b) noexcept { return static_cast<T*>(memory)->testMember2(a, b); })
#else // __clang__
                : testMemberPtr([](size_t a, size_t b, void* memory) noexcept { return static_cast<T*>(memory)->testMember(a, b); }),
                  testMember2Ptr([](size_t a, size_t b, void* memory) noexcept { return static_cast<T*>(memory)->testMember2(a, b); })
#endif // __clang__
        {
            static_assert(sizeof(T) <= maxSize);
            static_assert(alignof(T) <= alignment && alignment % alignof(T) == 0);
        }
#ifdef __clang__
        using testMemberPtrType = size_t(*)(void* memory, size_t a, size_t b) noexcept;
        using testMember2PtrType = size_t(*)(void* memory, size_t a, size_t b) noexcept;
#else // __clang__
        using testMemberPtrType = size_t(*)(size_t a, size_t b, void* memory) noexcept;
        using testMember2PtrType = size_t(*)(size_t a, size_t b, void* memory) noexcept;
#endif // __clang__
        testMemberPtrType testMemberPtr;
        testMember2PtrType testMember2Ptr;
    };

    detail::initializing_buffer<maxSize, alignment> buffer;
    helper_A ptrs;

    [[nodiscard]] constexpr size_t testMember(size_t a, size_t b) noexcept {
#ifdef __clang__
        return ptrs.testMemberPtr(buffer, a, b);
#else // __clang__
        return ptrs.testMemberPtr(a, b, buffer);
#endif // __clang__
    }

    [[nodiscard]] constexpr size_t testMember2(size_t a, size_t b) noexcept {
#ifdef __clang__
        return ptrs.testMember2Ptr(buffer, a, b);
#else // __clang__
        return ptrs.testMember2Ptr(a, b, buffer);
#endif // __clang__
    }

    template<typename T, typename=std::enable_if_t<!std::is_same_v<std::decay_t<T>, ErasedA>>>
    constexpr ErasedA(T&& t)
             : buffer(std::forward<T>(t)), ptrs(detail::type_list<std::decay_t<T>>{})
    { }
};
#endif // MEMBER_PTR

#ifdef INTERFACE_REFERENCE_BUFFER
#ifdef NO_SELECTOR
template<std::size_t maxSize, std::size_t alignment>
#else // NO_SELECTOR
template<std::size_t maxSize, std::size_t alignment, size_t selector>
#endif // NO_SELECTOR
struct polymorphic_valueA final {
    detail::initializing_buffer<maxSize, alignment> buffer;

    [[nodiscard]] constexpr size_t testMember(size_t a, size_t b) noexcept { return static_cast<IA*>(static_cast<void*>(buffer))->testMember(a, b); }
    [[nodiscard]] constexpr size_t testMember2(size_t a, size_t b) noexcept { return static_cast<IA*>(static_cast<void*>(buffer))->testMember2(a, b); }

    template<typename T, typename=std::enable_if_t<std::is_base_of_v<IA, std::decay_t<T>>>>
    explicit constexpr polymorphic_valueA(T&& t) : buffer(std::forward<T>(t))
    { }
};
#endif // INTERFACE_REFERENCE

#ifdef CREATE_INTERFACE_IMPL
#ifdef NO_SELECTOR
template<std::size_t maxSize, std::size_t alignment>
#else // NO_SELECTOR
template<std::size_t maxSize, std::size_t alignment, size_t selector>
#endif // NO_SELECTOR
struct ErasedACreateInherit final {
    template<typename T>
    struct helper_A final : public IA {
        T obj;
        // Can't make this constexpr without C++20
        [[nodiscard]] size_t testMember(size_t a, size_t b) noexcept final { return obj.testMember(a, b); }
        [[nodiscard]] size_t testMember2(size_t a, size_t b) noexcept final { return obj.testMember2(a, b); }

        template<typename TF>
        constexpr helper_A(TF&& tf) : obj(std::forward<TF>(tf)) {}
    };

    detail::initializing_buffer<maxSize + sizeof(void*), alignment + sizeof(void*)> buffer;

    [[nodiscard]] constexpr size_t testMember(size_t a, size_t b) noexcept { return static_cast<IA*>(static_cast<void*>(buffer))->testMember(a, b); }
    [[nodiscard]] constexpr size_t testMember2(size_t a, size_t b) noexcept { return static_cast<IA*>(static_cast<void*>(buffer))->testMember2(a, b); }

    template<typename T, typename=std::enable_if_t<!std::is_same_v<std::decay_t<T>, ErasedACreateInherit>>>
    explicit constexpr ErasedACreateInherit(T&& t) : buffer(detail::type_list<helper_A<std::decay_t<T>>>{}, std::forward<T>(t))
    { }
};
#endif // CREATE_INTERFACE_IMPL

#ifdef CAPTURING_LAMBDA
// TODO: Inline the implementation of function_ref to avoid duplicating pointers
template<std::size_t maxSize, std::size_t alignment, std::size_t testMemberSize, std::size_t testMemberAlign,
        std::size_t testMember2Size, std::size_t testMember2Align>
struct ErasedCapturingLambdaA final {
    struct helper_A {
        template<typename T>
        explicit constexpr helper_A(void* testMemberLambdaStorage, void* testMember2LambdaStorage, detail::type_list<T>)
                : testMemberPtr(*static_cast<testMemberLambdaType<T>*>(testMemberLambdaStorage)),
                  testMember2Ptr(*static_cast<testMember2LambdaType<T>*>(testMember2LambdaStorage)) {
        }

        using testMemberPtrType = detail::function_ref<size_t(size_t, size_t) noexcept>;
        using testMember2PtrType = detail::function_ref<size_t(size_t, size_t) noexcept>;

        template<typename T>
        static constexpr auto createTestMemberLambda(T& t) {
            return [&o=t](size_t a, size_t b) { return o.testMember(a, b); };
        }
        template<typename T>
        static constexpr auto createTestMember2Lambda(T& t) {
            return [&o=t](size_t a, size_t b) { return o.testMember2(a, b); };
        }
        template<typename T>
        using testMemberLambdaType = std::decay_t<decltype(createTestMemberLambda(std::declval<T&>()))>;
        template<typename T>
        using testMember2LambdaType = std::decay_t<decltype(createTestMember2Lambda(std::declval<T&>()))>;
        template<typename T>
        static constexpr size_t testMemberLambdaSize = sizeof(testMemberLambdaType<T>);
        template<typename T>
        static constexpr size_t testMember2LambdaSize = sizeof(testMember2LambdaType<T>);
        template<typename T>
        static constexpr size_t testMemberLambdaAlign = alignof(testMemberLambdaType<T>);
        template<typename T>
        static constexpr size_t testMember2LambdaAlign = alignof(testMember2LambdaType<T>);

        testMemberPtrType testMemberPtr;
        testMember2PtrType testMember2Ptr;
    };

    detail::initializing_buffer<maxSize, alignment> buffer;
    detail::initializing_buffer<testMemberSize, testMemberAlign> testMemberBuffer;
    detail::initializing_buffer<testMember2Size, testMember2Align> testMember2Buffer;
    helper_A ptrs;

    [[nodiscard]] constexpr size_t testMember(size_t a, size_t b) noexcept {
        return ptrs.testMemberPtr(a, b);
    }

    [[nodiscard]] constexpr size_t testMember2(size_t a, size_t b) noexcept {
        return ptrs.testMember2Ptr(a, b);
    }

    template<typename T>
    constexpr T& bufferAsT() {
        return *static_cast<T*>(static_cast<void*>(buffer));
    }

    template<typename T, typename=std::enable_if_t<!std::is_same_v<std::decay_t<T>, ErasedCapturingLambdaA>>>
    explicit constexpr ErasedCapturingLambdaA(T&& t)
            : buffer(std::forward<T>(t)),
              testMemberBuffer(helper_A::template createTestMemberLambda(bufferAsT<std::decay_t<T>>())),
              testMember2Buffer(helper_A::template createTestMember2Lambda(bufferAsT<std::decay_t<T>>())),
              ptrs(testMemberBuffer, testMember2Buffer, detail::type_list<std::decay_t<T>>{})
    { }
};
#endif // CAPTURING_LAMBDA

#ifdef UNSAFE_MEMBER_PTR_CAST
template<std::size_t maxSize, std::size_t alignment>
struct ErasedUnsafeA final {
    struct C final {};
    struct helper_A {
        template<typename T>
        constexpr explicit helper_A(detail::type_list<T>)
                : testMemberPtr(reinterpret_cast<testMemberPtrType>(&T::testMember)),
                  testMember2Ptr(reinterpret_cast<testMember2PtrType>(&T::testMember2))
        { }

        using testMemberPtrType = size_t(C::*)(size_t a, size_t b) noexcept;
        using testMember2PtrType = size_t(C::*)(size_t a, size_t b) noexcept;
        testMemberPtrType testMemberPtr;
        testMember2PtrType testMember2Ptr;
    };

    detail::initializing_buffer<maxSize, alignment> buffer;
    helper_A ptrs;

    [[nodiscard]] constexpr size_t testMember(size_t a, size_t b) noexcept {
        return (static_cast<C*>(static_cast<void*>(buffer))->*ptrs.testMemberPtr)(a, b);
    }

    [[nodiscard]] constexpr size_t testMember2(size_t a, size_t b) noexcept {
        return (static_cast<C*>(static_cast<void*>(buffer))->*ptrs.testMember2Ptr)(a, b);
    }

    template<typename T, typename=std::enable_if_t<!std::is_same_v<std::decay_t<T>, ErasedUnsafeA>>>
    explicit constexpr ErasedUnsafeA(T&& t)
            : buffer(std::forward<T>(t)), ptrs(detail::type_list<std::decay_t<T>>{})
    { }
};
#endif // UNSAFE_MEMBER_PTR_CAST

#ifdef UNSAFE_FUNC_PTR_CAST
template<std::size_t maxSize, std::size_t alignment>
struct ErasedAsmA final {
    struct C final {};
    struct helper_A {
        template<typename T>
        constexpr explicit helper_A(detail::type_list<T>)
                : testMemberPtr(detail::ConstMemFuncUnion<T, C, size_t, size_t, size_t>{&T::testMember}.fptr),
                  testMember2Ptr(detail::ConstMemFuncUnion<T, C, size_t, size_t, size_t>{&T::testMember2}.fptr)
        { }

        using funcType = size_t(*)(void*, size_t, size_t) noexcept;
        funcType testMemberPtr;
        funcType testMember2Ptr;
    };

    detail::initializing_buffer<maxSize, alignment> buffer;
    helper_A ptrs;

    [[nodiscard]] constexpr size_t testMember(size_t a, size_t b) noexcept {
        return ptrs.testMemberPtr(buffer, a, b);
    }

    [[nodiscard]] constexpr size_t testMember2(size_t a, size_t b) noexcept {
        return ptrs.testMember2Ptr(buffer, a, b);
    }

    template<typename T, typename=std::enable_if_t<!std::is_same_v<std::decay_t<T>, ErasedAsmA>>>
    explicit constexpr ErasedAsmA(T&& t)
            : buffer(std::forward<T>(t)), ptrs(detail::type_list<std::decay_t<T>>{})
    { }
};
#endif // UNSAFE_FUNC_PTR_CAST

#ifdef DYNO
// Test with the Dyno framework
DYNO_INTERFACE(ErasedADyno,
    (testMember, std::size_t(std::size_t, std::size_t) const),
    (testMember2, std::size_t(std::size_t, std::size_t) const));
#endif // DYNO

#ifdef FOLLY_POLY
// TODO: Implement
#endif // FOLLY_POLY

#ifdef MPARK_VARIANT
// TODO: Implement
#endif // MPARK_VARIANT

#ifdef STD_VARIANT
template<template<typename...> typename Var, typename... Alts>
struct VariantA {
    Var<Alts...> storage;

    [[nodiscard]] constexpr size_t testMember(size_t a, size_t b) noexcept {
        return visit([a,b](auto o){ return o.testMember(a, b); }, storage);
    }
    [[nodiscard]] constexpr size_t testMember2(size_t a, size_t b) noexcept {
        return visit([a,b](auto o){ return o.testMember2(a, b); }, storage);
    }

    template<typename T>
    explicit constexpr VariantA(T&& t) : storage(std::forward<T>(t)) {}
};
#endif // STD_VARIANT
#ifndef NO_SELECTOR
template<size_t selector>
#endif
struct ACompliant final {
    [[nodiscard]] PREVENT_MEMBER_INLINE(size_t) testMember(size_t a, size_t b) noexcept { return a ^ b; }
    [[nodiscard]] PREVENT_MEMBER_INLINE(size_t) testMember2(size_t a, size_t b) noexcept { return a ^ ~b; }
};

#ifdef NONFINAL_VIRTUAL
struct AImpl : public IA {
    [[nodiscard]] PREVENT_MEMBER_INLINE(size_t) testMember(size_t a, size_t b) noexcept override { return a ^ b; }
    [[nodiscard]] PREVENT_MEMBER_INLINE(size_t) testMember2(size_t a, size_t b) noexcept override { return a ^ ~b; }
};
#endif // NONFINAL_VIRTUAL

#ifdef FINAL_VIRTUAL
struct AImplFinal final : public IA {
    [[nodiscard]] PREVENT_MEMBER_INLINE(size_t) testMember(size_t a, size_t b) noexcept override final { return a ^ b; }
    [[nodiscard]] PREVENT_MEMBER_INLINE(size_t) testMember2(size_t a, size_t b) noexcept override final { return a ^ ~b; }
};
#endif // FINAL_VIRTUAL

#ifdef GOOGLE_BENCH
constexpr std::size_t maxValue = 5llu << 0u;
#else // GOOGLE_BENCH
constexpr std::size_t maxValue = 3llu << 30u;
#endif // !GOOGLE_BENCH

template<typename T>
#ifdef __clang__
PREVENT_INLINE_CALL_MEMBER(std::size_t) callMember(size_t a, size_t b, T& o) {
#else // __clang__
PREVENT_INLINE_CALL_MEMBER(std::size_t) callMember(T& o, size_t a, size_t b) {
#endif // __clang__
    return o.testMember(a, b);
}

template<typename T>
#ifdef __clang__
PREVENT_INLINE_CALL_MEMBER(std::size_t) callMember2(size_t a, size_t b, T& o) {
#else // __clang__
PREVENT_INLINE_CALL_MEMBER(std::size_t) callMember2(T& o, size_t a, size_t b) {
#endif // __clang__
    return o.testMember2(a, b);
}

template<typename T>
#ifdef GOOGLE_BENCH
size_t
#else // GOOGLE_BENCH
NO_INLINE(size_t)
#endif // !GOOGLE_BENCH
        runTest(std::size_t start, T& b) {
    for(std::size_t i=0; i < maxValue; i += 2) {
#ifdef __clang__
        start += 13*callMember<T>(i, start, b);
        start += 7*callMember2<T>(i, start, b);
#else // __clang__
        start += callMember<T>(b, i, start);
        start += callMember2<T>(b, i, start);
#endif // __clang__
    }
    return start;
}

#ifdef NO_SELECTOR
#ifdef MEMBER_PTR
using ErasedASized = ErasedA<std::max({sizeof(AImpl), sizeof(AImplFinal), sizeof(ACompliant)}), std::max({alignof(AImpl), alignof(AImplFinal), alignof(ACompliant)})>;
#endif // MEMBER_PTR
#ifdef POLYMORPHIC_VALUE
using polymorphic_valueASized = polymorphic_valueA<std::max({sizeof(AImpl), sizeof(AImplFinal)}), std::max({alignof(AImpl), alignof(AImplFinal)})>;
#endif // POLYMORPHIC_VALUE
#ifdef CREATE_INTERFACE_IMPL
using ErasedACreateInheritSized = ErasedACreateInherit<std::max({sizeof(AImpl), sizeof(AImplFinal), sizeof(ACompliant)}), std::max({alignof(AImpl), alignof(AImplFinal), alignof(ACompliant)})>;
#endif // CREATE_INTERFACE_IMPL
#else
#ifdef MEMBER_PTR
template<size_t selector>
using ErasedASized = ErasedA<std::max({sizeof(AImpl), sizeof(AImplFinal), sizeof(USE_SELECTOR(ACompliant))}), std::max({alignof(AImpl), alignof(AImplFinal), alignof(ACompliant)}), selector>;
#endif // MEMBER_PTR
#ifdef POLYMORPHIC_VALUE
template<size_t selector>
using polymorphic_valueASized = polymorphic_valueA<std::max({sizeof(AImpl), sizeof(AImplFinal)}), std::max({alignof(AImpl), alignof(AImplFinal)}), selector>;
#endif // POLYMORPHIC_VALUE
#ifdef CREATE_INTERFACE_IMPL
template<size_t selector>
using ErasedACreateInheritSized = ErasedACreateInherit<std::max({sizeof(AImpl), sizeof(AImplFinal), sizeof(ACompliant)}), std::max({alignof(AImpl), alignof(AImplFinal), alignof(ACompliant)}), selector>;
#endif // CREATE_INTERFACE_IMPL
#endif // !NO_SELECTOR
#ifdef CAPTURING_LAMBDA
using ErasedCapturingLambdaACompliant = ErasedCapturingLambdaA<sizeof(ACompliant), alignof(ACompliant), 8, 8, 8, 8>;
#endif // CAPTURING_LAMBDA
#ifdef UNSAFE_MEMBER_PTR_CAST
using ErasedUnsafeACompliant = ErasedUnsafeA<sizeof(ACompliant), alignof(ACompliant)>;
#endif // UNSAFE_MEMBER_PTR_CAST
#ifdef UNSAFE_FUNC_PTR_CAST
using ErasedUnsafeFuncPtrACompliant = ErasedAsmA<sizeof(ACompliant), alignof(ACompliant)>;
#endif // UNSAFE_FUNC_PTR_CAST
#ifdef STD_VARIANT
using VariantAAlts = VariantA<std::variant, ACompliant, AImpl, AImplFinal>;
#endif
#ifdef MPARK_VARIANT
using MparkVariantAAlts = VariantA<mpark::variant, ACompliant, AImpl, AImplFinal>;
#endif // MPARK_VARIANT

#ifdef GOOGLE_BENCH
template<typename T>
static NO_INLINE(void) RunTest(benchmark::State& state, size_t seed, T& t) {
    for (auto _ : state) {
        seed ^= 13 + runTest<T>(seed, t);
    }
}

#define BENCHMARK_TEMPLATE1_CAPTURE(func, TypeArg, test_case_name, toTest)    \
    BENCHMARK_PRIVATE_DECLARE(##func_Of_##TypeArg) =         \
        (::benchmark::internal::RegisterBenchmarkInternal( \
            new ::benchmark::internal::FunctionBenchmark(  \
                #func "_Of_" #TypeArg "/" test_case_name, \
                [](benchmark::State& st) { decltype(toTest) holder = toTest; func<USE_SELECTOR(TypeArg)>(st, 137, holder); }))) \
                ->ComputeStatistics("max", [](const std::vector<double>& v) -> double { \
return *(std::max_element(std::begin(v), std::end(v))); })

#define ERASURE_BENCHMARK(test_case_name, TypeArg, toTest) BENCHMARK_TEMPLATE1_CAPTURE(RunTest, TypeArg, test_case_name, toTest)
#define CONSTRUCT_FROM(FinalType, InitializerType) USE_SELECTOR(FinalType){USE_SELECTOR(InitializerType){}}

ERASURE_BENCHMARK("DirectCall", ACompliant, ACompliant{});
#ifdef MEMBER_PTR
ERASURE_BENCHMARK("TrueErasedMemberPtr", ErasedASized, CONSTRUCT_FROM(ErasedASized, ACompliant));
#ifdef FINAL_VIRTUAL
ERASURE_BENCHMARK("VirtualFinalMemberPtr", ErasedASized, CONSTRUCT_FROM(ErasedASized, AImplFinal));
#endif
#ifdef NONFINAL_VIRTUAL
ERASURE_BENCHMARK("VirtualNonFinalMemberPtr", ErasedASized, CONSTRUCT_FROM(ErasedASized, AImpl));
#endif
#endif
#ifdef INTERFACE_REFERENCE
#ifdef FINAL_VIRTUAL
ERASURE_BENCHMARK("VirtualFinalReference", IA, AImplFinal{});
#endif
#ifdef NONFINAL_VIRTUAL
ERASURE_BENCHMARK("VirtualNonFinalReference", IA, AImpl{});
#endif // NONFINAL_VIRTUAL
#endif // INTERFACE_REFERENCE
#ifdef STD_VARIANT
ERASURE_BENCHMARK("ACompliantVariant", VariantAAlts, CONSTRUCT_FROM(VariantAAlts, ACompliant));
#ifdef FINAL_VIRTUAL
ERASURE_BENCHMARK("FinalInheritanceVariant", VariantAAlts, CONSTRUCT_FROM(VariantAAlts, AImplFinal));
#endif
#ifdef NONFINAL_VIRTUAL
ERASURE_BENCHMARK("NonFinalInheritanceVariant", VariantAAlts, CONSTRUCT_FROM(VariantAAlts, AImpl));
#endif
#endif
#ifdef MPARK_VARIANT
ERASURE_BENCHMARK("ACompliantMparkVariant", MparkVariantAAlts, CONSTRUCT_FROM(MparkVariantAAlts, ACompliant));
#ifdef FINAL_VIRTUAL
ERASURE_BENCHMARK("FinalInheritanceMparkVariant", MparkVariantAAlts, CONSTRUCT_FROM(MparkVariantAAlts, AImplFinal));
#endif
#ifdef NONFINAL_VIRTUAL
ERASURE_BENCHMARK("NonFinalInheritanceMparkVariant", MparkVariantAAlts, CONSTRUCT_FROM(MparkVariantAAlts, AImpl));
#endif
#endif
#ifdef POLYMORPHIC_VALUE
#ifdef FINAL_VIRTUAL
ERASURE_BENCHMARK("PolymorphicValueFinalInheritance", polymorphic_valueASized, CONSTRUCT_FROM(polymorphic_valueASized, AImplFinal));
#endif // FINAL_VIRTUAL
#ifdef NONFINAL_VIRTUAL
ERASURE_BENCHMARK("PolymorphicValueNonFinalInheritance", polymorphic_valueASized, CONSTRUCT_FROM(polymorphic_valueASized, AImpl));
#endif // NONFINAL_VIRTUAL
#endif // POLYMORPHIC_VALUE
#ifdef CREATE_INTERFACE_IMPL
ERASURE_BENCHMARK("CreateInterfaceTrueErasure", ErasedACreateInheritSized, CONSTRUCT_FROM(ErasedACreateInheritSized, ACompliant));
#endif // CREATE_INTERFACE_IMPL
#ifdef CAPTURING_LAMBDA
ERASURE_BENCHMARK("CapturingLambdaTrueErasure", ErasedCapturingLambdaACompliant, CONSTRUCT_FROM(ErasedCapturingLambdaACompliant, ACompliant));
#endif // CAPTURING_LAMBDA
#ifdef UNSAFE_MEMBER_PTR_CAST
ERASURE_BENCHMARK("UnsafeMemberPtrCastTrueErasure", ErasedUnsafeACompliant, CONSTRUCT_FROM(ErasedUnsafeACompliant, ACompliant));
#endif // UNSAFE_MEMBER_PTR_CAST
#ifdef UNSAFE_FUNC_PTR_CAST
ERASURE_BENCHMARK("UnsafeFuncPtrCast", ErasedUnsafeFuncPtrACompliant, CONSTRUCT_FROM(ErasedUnsafeFuncPtrACompliant, ACompliant));
#endif // UNSAFE_FUNC_PTR_CAST
#ifdef DYNO
ERASURE_BENCHMARK("DynoDefault", ErasedADyno, CONSTRUCT_FROM(ErasedADyno, ACompliant));
#endif // DYNO

BENCHMARK_MAIN();
#else // GOOGLE_BENCH
int main(int argc, char* argv[]) {
    ACompliant b;
#ifdef NONFINAL_VIRTUAL
    AImpl bI1;
#endif // NONFINAL_VIRTUAL
#ifdef FINAL_VIRTUAL
    AImplFinal bI2;
#endif // FINAL_VIRTUAL
#ifdef NO_SELECTOR
#ifdef MEMBER_PTR
#ifdef NONFINAL_VIRTUAL
    ErasedASized bEI1{bI1};
#endif // NONFINAL_VIRTUAL
#ifdef FINAL_VIRTUAL
    ErasedASized bEI2{bI2};
#endif // FINAL_VIRTUAL
    ErasedASized bE{b};
#endif // MEMBER_PTR
#ifdef POLYMORPHIC_VALUE
    polymorphic_valueASized bEI2{bI2};
#endif // POLYMORPHIC_VALUE
#ifdef CREATE_INTERFACE_IMPL
    ErasedACreateInheritSized bECI{bI2};
#endif
#ifdef CAPTURING_LAMBDA
    ErasedCapturingLambdaACompliant bEL{b};
#endif // CAPTURING_LAMBDA
#ifdef UNSAFE_MEMBER_PTR_CAST
    ErasedUnsafeACompliant bEU{b};
#endif // UNSAFE_MEMBER_PTR_CAST
#ifdef UNSAFE_FUNC_PTR_CAST
    ErasedUnsafeFuncPtrACompliant bEA{b};
#endif // UNSAFE_FUNC_PTR_CAST
#ifdef DYNO
    ErasedADyno bD{b};
#endif // DYNO
#ifdef STD_VARIANT
#ifdef NONFINAL_VIRTUAL
    VariantAAlts bV1{bI1};
#endif // NONFINAL_VIRTUAL
#ifdef FINAL_VIRTUAL
    VariantAAlts bV2{bI2};
#endif // FINAL_VIRTUAL
    VariantAAlts bV3{b};
#endif // STD_VARIANT
#else // NO_SELECTOR
#ifdef MEMBER_PTR
  #ifdef NONFINAL_VIRTUAL
    ErasedASized<1> bEI1{bI1};
  #endif // NONFINAL_VIRTUAL
  #ifdef FINAL_VIRTUAL
    ErasedASized<2> bEI2{bI2};
  #endif // FINAL_VIRTUAL
    ErasedASized<3> bE{b};
#endif // MEMBER_PTR
#ifdef POLYMORPHIC_VALUE
    polymorphic_valueASized<1> bEI2{bI2};
#endif // POLYMORPHIC_VALUE
#ifdef CREATE_INTERFACE_IMPL
    ErasedACreateInheritSized<1> bECI{bI2};
#endif
#ifdef CAPTURING_LAMBDA
    ErasedCapturingLambdaACompliant bEL{b};
#endif // CAPTURING_LAMBDA
#ifdef UNSAFE_MEMBER_PTR_CAST
    ErasedUnsafeACompliant bEU{b};
#endif // UNSAFE_MEMBER_PTR_CAST
#ifdef UNSAFE_FUNC_PTR_CAST
    ErasedUnsafeFuncPtrACompliant bEA{b};
#endif // UNSAFE_FUNC_PTR_CAST
#ifdef DYNO
    ErasedADyno bD{b};
#endif // DYNO
#ifdef STD_VARIANT
#ifdef NONFINAL_VIRTUAL
    VariantAAlts bV1{bI1};
#endif // NONFINAL_VIRTUAL
#ifdef FINAL_VIRTUAL
    VariantAAlts bV2{bI2};
#endif // FINAL_VIRTUAL
    VariantAAlts bV3{b};
#endif // STD_VARIANT
#endif // !NO_SELECTOR
#ifdef MANUAL_BENCH
    auto startTime = high_resolution_clock::now();
    std::size_t res = startTime.time_since_epoch().count();
#else // MANUAL_BENCH
    std::size_t res = argc;
#endif // MANUAL_BENCH
    size_t res1 = runTest<ACompliant, 1>(res, b);
#ifdef MANUAL_BENCH
    auto mid1Time = high_resolution_clock::now();
#endif // MANUAL_BENCH
    size_t res2 = runTest<IA, 2>(res, bI1);
#ifdef MANUAL_BENCH
    auto mid2Time = high_resolution_clock::now();
#endif // MANUAL_BENCH
    size_t res3 = runTest<IA, 3>(res, bI2);
#ifdef MANUAL_BENCH
    auto mid3Time = high_resolution_clock::now();
#endif // MANUAL_BENCH
    size_t res4 = runTest<ErasedASized<1>, 4>(res, bEI1);
#ifdef MANUAL_BENCH
    auto mid4Time = high_resolution_clock::now();
#endif // MANUAL_BENCH
    size_t res5 = runTest<polymorphic_valueASized<1>, 5>(res, bEI2);
#ifdef MANUAL_BENCH
    auto mid5Time = high_resolution_clock::now();
#endif // MANUAL_BENCH
    size_t res6 = runTest<ErasedACreateInheritSized<1>, 6>(res, bECI);
#ifdef MANUAL_BENCH
    auto mid6Time = high_resolution_clock::now();
#endif // MANUAL_BENCH
    size_t res7 = runTest<ErasedCapturingLambdaACompliant, 7>(res, bEL);
#ifdef MANUAL_BENCH
    auto mid7Time = high_resolution_clock::now();
#endif // MANUAL_BENCH
    size_t res8 = runTest<ErasedASized<2>, 8>(res, bE);
#ifdef MANUAL_BENCH
    auto mid8Time = high_resolution_clock::now();
#endif // MANUAL_BENCH
#ifdef UNSAFE_TEST
    size_t res9 = runTest<ErasedUnsafeACompliant, 9>(res, bEU);
  #ifdef MANUAL_BENCH
    auto mid9Time = high_resolution_clock::now();
  #endif // MANUAL_BENCH
    size_t res10 = runTest<ErasedUnsafeFuncPtrACompliant, 10>(res, bEA);
#endif // UNSAFE_TEST
#ifdef MANUAL_BENCH
    auto mid10Time = high_resolution_clock::now();
#endif // MANUAL_BENCH
#ifndef _MSC_VER
    size_t res11 = runTest<ErasedADyno, 11>(res, bD);
#endif // _MSC_VER
#ifdef MANUAL_BENCH
    auto mid11Time = high_resolution_clock::now();
#endif // MANUAL_BENCH
    size_t res12 = runTest<VariantAAlts, 12>(res, bV1);
#ifdef MANUAL_BENCH
    auto mid12Time = high_resolution_clock::now();
#endif // MANUAL_BENCH
    size_t res13 = runTest<VariantAAlts, 13>(res, bV2);
#ifdef MANUAL_BENCH
    auto mid13Time = high_resolution_clock::now();
#endif // MANUAL_BENCH
#ifdef MANUAL_BENCH
    auto dur1 = duration_cast<microseconds>(mid1Time - startTime).count();
    auto dur2 = duration_cast<microseconds>(mid2Time - mid1Time).count();
    auto dur3 = duration_cast<microseconds>(mid3Time - mid2Time).count();
    auto dur4 = duration_cast<microseconds>(mid4Time - mid3Time).count();
    auto dur5 = duration_cast<microseconds>(mid5Time - mid4Time).count();
    auto dur6 = duration_cast<microseconds>(mid6Time - mid5Time).count();
    auto dur7 = duration_cast<microseconds>(mid7Time - mid6Time).count();
    auto dur8 = duration_cast<microseconds>(mid8Time - mid7Time).count();
  #ifdef UNSAFE_TEST
    auto dur9 = duration_cast<microseconds>(mid9Time - mid8Time).count();
    auto dur10 = duration_cast<microseconds>(mid10Time - mid9Time).count();
  #endif //UNSAFE_TEST
#ifndef _MSC_VER
    auto dur11 = duration_cast<microseconds>(mid11Time - mid10Time).count();
#endif // _MSC_VER
    auto dur12 = duration_cast<microseconds>(mid12Time - mid11Time).count();
    auto dur13 = duration_cast<microseconds>(mid13Time - mid12Time).count();
    double fdur1 = static_cast<double>(dur1);
    std::cout << "Direct call: " << dur1 << " (" << (dur1/ 10'000)/100.0 << "s)(100.00%) res: " << res1 << "\n"
              << "Inheritance: " << dur2 << " (" << (dur2/ 10'000)/100.0 << "s)(" << ((int)(dur2/fdur1*10000))/100.0 << "%) res: " << res2 << "\n"
              << "Final Inheritance: " << dur3 << " (" << (dur3/ 10'000)/100.0 << "s)(" << ((int)(dur3/fdur1*10000))/100.0 << "%) res: " << res3 << "\n"
              << "Erased Inherited Member Function Pointer: " << dur4 << " (" << (dur4/ 10'000)/100.0 << "s)(" << ((int)(dur4/fdur1*10000))/100.0 << "%) res: " << res4 << "\n"
              << "Final Polymorphic Value: " << dur5 << " (" << (dur5/ 10'000)/100.0 << "s)(" << ((int)(dur5/fdur1*10000))/100.0 << "%) res: " << res5 << "\n"
              << "Erased Created Inheritance: " << dur6 << " (" << (dur6/ 10'000)/100.0 << "s)(" << ((int)(dur6/fdur1*10000))/100.0 << "%) res: " << res6 << "\n"
              << "Erased Capturing Lambda: " << dur7 << " (" << (dur7/ 10'000)/100.0 << "s)(" << ((int)(dur7/fdur1*10000))/100.0 << "%) res: " << res7 << "\n"
              << "Erased Member Function Pointer: " << dur8 << " (" << (dur8/ 10'000)/100.0 << "s)(" << ((int)(dur8/fdur1*10000))/100.0 << "%) res: " << res8 << "\n"
  #ifdef UNSAFE_TEST
              << "Erased Unsafe Member Function Pointer Cast: " << dur9 << " (" << (dur9/ 10'000)/100.0 << "s)(" << ((int)(dur9/fdur1*10000))/100.0 << "%) res: " << res9 << "\n"
              << "Erased Unsafe Function Ptr Cast:  " << dur10 << " (" << (dur10/ 10'000)/100.0 << "s)(" << ((int)(dur10/fdur1*10000))/100.0 << "%) res: " << res10 << "\n"
  #endif // UNSAFE_TEST
  #ifndef _MSC_VER
              << "Dyno Default:  " << dur11 << " (" << (dur11/ 10'000)/100.0 << "s)(" << ((int)(dur11/fdur1*10000))/100.0 << "%) res: " << res11 << "\n"
  #endif // _MSC_VER
              << "Variant Direct:  " << dur12 << " (" << (dur12/ 10'000)/100.0 << "s)(" << ((int)(dur12/fdur1*10000))/100.0 << "%) res: " << res12 << "\n"
              << "Variant Final Inheritance:  " << dur13 << " (" << (dur13/ 10'000)/100.0 << "s)(" << ((int)(dur13/fdur1*10000))/100.0 << "%) res: " << res13 << "\n"

            ;
#endif // MANUAL_BENCH
    assert(res1 == res2);
    assert(res2 == res3);
    assert(res3 == res4);
    assert(res4 == res5);
    assert(res5 == res6);
    assert(res6 == res7);
    assert(res7 == res8);
#ifdef UNSAFE_TEST
    assert(res8 == res9);
    assert(res9 == res10);
#endif // UNSAFE_TEST
#ifndef _MSC_VER
    assert(res1 == res11);
#endif
    assert(res1 == res12);
    assert(res12 == res13);
    return 0;
}
#endif // !GOOGLE_BENCH