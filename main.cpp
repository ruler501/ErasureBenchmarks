#include <algorithm>
#include <array>
#include <cassert>
#include <fstream>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>
#include <variant>

#include <nanobench.h>
#include "mparkvariant.hpp"

#ifdef STD_VARIANT
    #include <variant>
#endif

#ifdef _MSC_VER
  #define NO_INLINE(...) __declspec(noinline) __VA_ARGS__
#else // _MSC_VER
  #define NO_INLINE(...) __VA_ARGS__ __attribute((noinline))
#endif // _MSC_VER

#ifndef NO_INLINE_MEMBERS
    #define PREVENT_MEMBER_INLINE(...) constexpr __VA_ARGS__
#else // ALLOW_MEMBER_INLINE
    #define PREVENT_MEMBER_INLINE(...) NO_INLINE(__VA_ARGS__)
#endif // ALLOW_MEMBER_INLINE

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
        constexpr explicit function_ref(Functor& f) noexcept : data{reinterpret_cast<void*>(&f)}, callback{[](void* memory, Args... args) noexcept { return static_cast<Return>(static_cast<Functor*>(memory)->operator()(args...)); }}
#else // __clang__
        constexpr explicit function_ref(Functor& f) noexcept : data{reinterpret_cast<void*>(&f)}, callback{[](Args... args, void* memory) noexcept { return static_cast<Return>(static_cast<Functor*>(memory)->operator()(args...)); }}
#endif // __clang__
{}

        template <typename Functor, Return(Functor::*fn)(Args...) noexcept>
#ifdef __clang__
        constexpr explicit function_ref(Functor& f, value_type<fn>) noexcept : data{reinterpret_cast<void*>(&f)}, callback{[](void* memory, Args... args) noexcept { return static_cast<Return>((static_cast<Functor*>(memory)->*fn)(args...)); }}
#else // __clang__
        constexpr explicit function_ref(Functor& f, value_type<fn>) noexcept : data{reinterpret_cast<void*>(&f)}, callback{[](Args... args, void* memory) noexcept { return static_cast<Return>((static_cast<Functor*>(memory)->*fn)(args...)); }}
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

        template<typename T> requires (!std::is_same_v<std::remove_cvref_t<T>, initializing_buffer>)
        explicit constexpr initializing_buffer(T&& t) noexcept : deletePtr{[](void* memory){ delete static_cast<std::remove_cvref_t<T>*>(memory); }} {
            using TR = std::remove_cvref_t<T>;
            new (value.__data) TR{std::forward<T>(t)};
            static_assert(sizeof(TR) <= size);
            static_assert(alignof(TR) <= align && align % alignof(TR) == 0);
        }

        template<typename T, typename... Args> requires (std::is_constructible_v<T, Args...>)
        constexpr initializing_buffer(type_list<T>, Args&&... args) noexcept : deletePtr([](void* memory){ delete static_cast<T*>(memory);}) {
            new (value.__data) T{std::forward<Args>(args)...};
            static_assert(sizeof(T) <= size);
            static_assert(alignof(T) <= align && align % alignof(T) == 0);
        }

        constexpr operator void*() noexcept { // NOLINT(google-explicit-constructor,hicpp-explicit-conversions)
            return value.__data;
        }

        constexpr operator const void*() const noexcept { // NOLINT(google-explicit-constructor,hicpp-explicit-conversions)
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
template<std::size_t maxSize, std::size_t alignment>
struct ErasedA final {
    struct helper_A {
        template<typename T>
        explicit helper_A(detail::type_list<T>) noexcept
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

    template<typename T> requires (!std::is_same_v<std::remove_cvref_t<T>, ErasedA>)
    constexpr ErasedA(T&& t) noexcept
             : buffer(std::forward<T>(t)), ptrs(detail::type_list<std::remove_cvref_t<T>>{})
    { }
};
#endif // MEMBER_PTR

#ifdef INTERFACE_REFERENCE_BUFFER
template<std::size_t maxSize, std::size_t alignment>
struct polymorphic_valueA final {
    detail::initializing_buffer<maxSize, alignment> buffer;

    [[nodiscard]] constexpr size_t testMember(size_t a, size_t b) noexcept { return static_cast<IA*>(static_cast<void*>(buffer))->testMember(a, b); }
    [[nodiscard]] constexpr size_t testMember2(size_t a, size_t b) noexcept { return static_cast<IA*>(static_cast<void*>(buffer))->testMember2(a, b); }

    template<typename T> requires (std::is_base_of_v<IA, std::remove_cvref_t<T>>)
    explicit constexpr polymorphic_valueA(T&& t) noexcept : buffer(std::forward<T>(t))
    { }
};
#endif // INTERFACE_REFERENCE

#ifdef CREATE_INTERFACE_IMPL
template<std::size_t maxSize, std::size_t alignment>
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

    detail::initializing_buffer<maxSize + sizeof(void*), std::max(alignment, alignof(void*))> buffer;

    [[nodiscard]] constexpr size_t testMember(size_t a, size_t b) noexcept { return static_cast<IA*>(static_cast<void*>(buffer))->testMember(a, b); }
    [[nodiscard]] constexpr size_t testMember2(size_t a, size_t b) noexcept { return static_cast<IA*>(static_cast<void*>(buffer))->testMember2(a, b); }

    template<typename T> requires (not std::is_same_v<std::remove_cvref<T>, ErasedACreateInherit>)
    explicit constexpr ErasedACreateInherit(T&& t) noexcept : buffer(detail::type_list<helper_A<std::remove_cvref_t<T>>>{}, std::forward<T>(t))
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
        explicit constexpr helper_A(void* testMemberLambdaStorage, void* testMember2LambdaStorage, detail::type_list<T>) noexcept
                : testMemberPtr(*static_cast<testMemberLambdaType<T>*>(testMemberLambdaStorage)),
                  testMember2Ptr(*static_cast<testMember2LambdaType<T>*>(testMember2LambdaStorage)) {
        }

        using testMemberPtrType = detail::function_ref<size_t(size_t, size_t) noexcept>;
        using testMember2PtrType = detail::function_ref<size_t(size_t, size_t) noexcept>;

        template<typename T>
        static constexpr auto createTestMemberLambda(T& t) noexcept {
            return [&o=t](size_t a, size_t b) { return o.testMember(a, b); };
        }
        template<typename T>
        static constexpr auto createTestMember2Lambda(T& t) noexcept {
            return [&o=t](size_t a, size_t b) { return o.testMember2(a, b); };
        }
        template<typename T>
        using testMemberLambdaType = std::remove_cvref_t<decltype(createTestMemberLambda(std::declval<T&>()))>;
        template<typename T>
        using testMember2LambdaType = std::remove_cvref_t<decltype(createTestMember2Lambda(std::declval<T&>()))>;
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
    constexpr T& bufferAsT() noexcept {
        return *static_cast<T*>(static_cast<void*>(buffer));
    }

    template<typename T> requires (!std::is_same_v<std::remove_cvref_t<T>, ErasedCapturingLambdaA>)
    explicit constexpr ErasedCapturingLambdaA(T&& t) noexcept
            : buffer(std::forward<T>(t)),
              testMemberBuffer(helper_A::template createTestMemberLambda(bufferAsT<std::remove_cvref_t<T>>())),
              testMember2Buffer(helper_A::template createTestMember2Lambda(bufferAsT<std::remove_cvref_t<T>>())),
              ptrs(testMemberBuffer, testMember2Buffer, detail::type_list<std::remove_cvref_t<T>>{})
    { }
};
#endif // CAPTURING_LAMBDA

#ifdef UNSAFE_MEMBER_PTR_CAST
template<std::size_t maxSize, std::size_t alignment>
struct ErasedUnsafeA final {
    struct C final {};
    struct helper_A {
        template<typename T>
        constexpr explicit helper_A(detail::type_list<T>) noexcept
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

    template<typename T> requires (!std::is_same_v<std::remove_cvref_t<T>, ErasedUnsafeA>)
    explicit constexpr ErasedUnsafeA(T&& t) noexcept
            : buffer(std::forward<T>(t)), ptrs(detail::type_list<std::remove_cvref_t<T>>{})
    { }
};
#endif // UNSAFE_MEMBER_PTR_CAST

#ifdef UNSAFE_FUNC_PTR_CAST
template<std::size_t maxSize, std::size_t alignment>
struct ErasedAsmA final {
    struct C final {};
    struct helper_A {
        template<typename T>
        constexpr explicit helper_A(detail::type_list<T>) noexcept
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

    template<typename T> requires (!std::is_same_v<std::remove_cvref_t<T>, ErasedAsmA>)
    explicit constexpr ErasedAsmA(T&& t) noexcept
            : buffer(std::forward<T>(t)), ptrs(detail::type_list<std::remove_cvref_t<T>>{})
    { }
};
#endif // UNSAFE_FUNC_PTR_CAST

#ifdef STD_VARIANT
template<template<typename...> typename Var, typename... Alts>
struct VariantA {
    Var<Alts...> storage;

    [[nodiscard]] size_t testMember(size_t a, size_t b) noexcept {
        return visit([a,b](auto o){ return o.testMember(a, b); }, storage);
    }
    [[nodiscard]] size_t testMember2(size_t a, size_t b) noexcept {
        return visit([a,b](auto o){ return o.testMember2(a, b); }, storage);
    }

    template<typename T>
    explicit constexpr VariantA(T&& t) noexcept : storage(std::forward<T>(t)) {}
};
#endif // STD_VARIANT

struct ACompliant final {
    [[nodiscard]] PREVENT_MEMBER_INLINE(size_t) testMember(size_t a, size_t b) noexcept { return ~(a ^ b); }
    [[nodiscard]] PREVENT_MEMBER_INLINE(size_t) testMember2(size_t a, size_t b) noexcept { return ~(a ^ ~b); }
};

struct BCompliant final {
    [[nodiscard]] PREVENT_MEMBER_INLINE(size_t) testMember(size_t a, size_t b) noexcept { return ~(a | b); }
    [[nodiscard]] PREVENT_MEMBER_INLINE(size_t) testMember2(size_t a, size_t b) noexcept { return ~(a | ~b); }
};

struct CCompliant final {
    [[nodiscard]] PREVENT_MEMBER_INLINE(size_t) testMember(size_t a, size_t b) noexcept { return ~(a & b); }
    [[nodiscard]] PREVENT_MEMBER_INLINE(size_t) testMember2(size_t a, size_t b) noexcept { return ~(a & ~b); }
};

struct AImpl : public IA {
    [[nodiscard]] PREVENT_MEMBER_INLINE(size_t) testMember(size_t a, size_t b) noexcept override { return ~(a ^ b); }
    [[nodiscard]] PREVENT_MEMBER_INLINE(size_t) testMember2(size_t a, size_t b) noexcept override { return ~(a ^ ~b); }
};

struct BImpl : public IA {
    [[nodiscard]] PREVENT_MEMBER_INLINE(size_t) testMember(size_t a, size_t b) noexcept { return ~(a | b); }
    [[nodiscard]] PREVENT_MEMBER_INLINE(size_t) testMember2(size_t a, size_t b) noexcept { return ~(a | ~b); }
};

struct CImpl : public IA {
    [[nodiscard]] PREVENT_MEMBER_INLINE(size_t) testMember(size_t a, size_t b) noexcept { return ~(a & b); }
    [[nodiscard]] PREVENT_MEMBER_INLINE(size_t) testMember2(size_t a, size_t b) noexcept { return ~(a & ~b); }
};

struct AImplFinal final : public IA {
    [[nodiscard]] PREVENT_MEMBER_INLINE(size_t) testMember(size_t a, size_t b) noexcept override final { return ~(a ^ b); }
    [[nodiscard]] PREVENT_MEMBER_INLINE(size_t) testMember2(size_t a, size_t b) noexcept override final { return ~(a ^ ~b); }
};

struct BImplFinal final : public IA {
    [[nodiscard]] PREVENT_MEMBER_INLINE(size_t) testMember(size_t a, size_t b) noexcept { return ~(a | b); }
    [[nodiscard]] PREVENT_MEMBER_INLINE(size_t) testMember2(size_t a, size_t b) noexcept { return ~(a | ~b); }
};

struct CImplFinal final : public IA {
    [[nodiscard]] PREVENT_MEMBER_INLINE(size_t) testMember(size_t a, size_t b) noexcept { return ~(a & b); }
    [[nodiscard]] PREVENT_MEMBER_INLINE(size_t) testMember2(size_t a, size_t b) noexcept { return ~(a & ~b); }
};

constexpr std::size_t maxValue = 1llu;

#ifdef MEMBER_PTR
template<typename Wrapper>
size_t testWrapper(const std::array<unsigned int, maxValue>& choices, ankerl::nanobench::Bench* bench,
                   const char* name) {
    std::vector<Wrapper> sized;
    sized.reserve(maxValue);
    for (auto choice : choices) {
        if (choice == 0) sized.emplace_back(ACompliant{});
        else if (choice == 1) sized.emplace_back(BCompliant{});
        else if (choice == 2) sized.emplace_back(CCompliant{});
    }
    std::size_t start = 53;
    bench->run(name, [&]() {
        for (auto& erased : sized) {
            start += erased.testMember(35, start);
            /* start += erased.testMember2(37, start); */
        }
        ankerl::nanobench::doNotOptimizeAway(start);
    });
    return start;
}
#endif // MEMBER_PTR

#ifdef NONFINAL_VIRTUAL
size_t testVirtualNonFinal(const std::array<unsigned int, maxValue>& choices, ankerl::nanobench::Bench* bench,
                           const char* name) {
    std::vector<std::unique_ptr<IA>> sized;
    sized.reserve(maxValue);
    for (auto choice : choices) {
        if (choice == 0) sized.push_back(std::make_unique<AImpl>());
        else if (choice == 1) sized.push_back(std::make_unique<BImpl>());
        else if (choice == 2) sized.push_back(std::make_unique<CImpl>());
    }
    std::size_t start = 53;
    bench->run(name, [&]() {
        for (auto& erased : sized) {
            start += erased->testMember(35, start);
            /* start += erased->testMember2(37, start); */
        }
        ankerl::nanobench::doNotOptimizeAway(start);
    });
    return start;
}
#endif // NONFINAL_VIRTUAL

#ifdef FINAL_VIRTUAL
size_t testVirtualFinal(const std::array<unsigned int, maxValue>& choices, ankerl::nanobench::Bench* bench,
                        const char* name) {
    std::vector<std::unique_ptr<IA>> sized;
    sized.reserve(maxValue);
    for (auto choice : choices) {
        if (choice == 0) sized.push_back(std::make_unique<AImplFinal>());
        else if (choice == 1) sized.push_back(std::make_unique<BImplFinal>());
        else if (choice == 2) sized.push_back(std::make_unique<CImplFinal>());
    }
    std::size_t start = 53;
    bench->run(name, [&]() {
        for (auto& erased : sized) {
            start += erased->testMember(35, start);
            /* start += erased->testMember2(37, start); */
        }
        ankerl::nanobench::doNotOptimizeAway(start);
    });
    return start;
}
#endif // FINAL_VIRTUAL

#ifdef POLYMORPHIC_VALUE
template <bool final>
size_t testPolymorphicValue(const std::array<unsigned int, maxValue>& choices, ankerl::nanobench::Bench* bench,
                            const char* name) {
    using polymorphic_valueASized = polymorphic_valueA<16, 16>;
    std::vector<polymorphic_valueASized> sized;
    sized.reserve(maxValue);
    for (auto choice : choices) {
        if constexpr(final) {
            if (choice == 0) sized.emplace_back(AImplFinal());
            else if (choice == 1) sized.emplace_back(BImplFinal());
            else if (choice == 2) sized.emplace_back(CImplFinal());
        } else {
            if (choice == 0) sized.emplace_back(AImpl());
            else if (choice == 1) sized.emplace_back(BImpl());
            else if (choice == 2) sized.emplace_back(CImpl());
        }
    }
    std::size_t start = 53;
    bench->run(name, [&]() {
        for (auto& erased : sized) {
            start += erased.testMember(35, start);
            /* start += erased.testMember2(37, start); */
        }
        ankerl::nanobench::doNotOptimizeAway(start);
    });
    return start;
}
#endif // POLYMORPHIC_VALUE

constexpr size_t max_size = std::max({sizeof(ACompliant), sizeof(BCompliant), sizeof(CCompliant)});
constexpr size_t max_align = std::max({alignof(ACompliant), alignof(BCompliant), alignof(CCompliant), alignof(void*)});
constexpr size_t minEpoch = 1 << 28;

int main() {
    ankerl::nanobench::Rng rng;
    std::array<unsigned int, maxValue> choices;
    const std::size_t start = rng();
    for (size_t i=0; i < maxValue; i++) {
        choices[i] = (start + i) % 3;
    }
    ankerl::nanobench::Bench bench;
    bench.title("Type Erasure Method Benchmarks")
        .warmup(minEpoch)
        .relative(true)
        .minEpochIterations(minEpoch);
    bench.performanceCounters(true);

#ifdef STD_VARIANT
    using VariantAAlts = VariantA<std::variant, ACompliant, BCompliant, CCompliant>;
    testWrapper<VariantAAlts>(choices, &bench, "visit(std::variant)");
#endif

#ifdef MPARK_VARIANT
    using MparkVariantAAlts = VariantA<mpark::variant, ACompliant, BCompliant, CCompliant>;
    testWrapper<MparkVariantAAlts>(choices, &bench, "visit(mpark::variant)");
#endif

#ifdef MEMBER_PTR
    using ErasedASized = ErasedA<max_size, max_align>;
    testWrapper<ErasedASized>(choices, &bench, "ErasedA");
#endif

#ifdef CREATE_INTERFACE_IMPL
    using ErasedACreateInheritSized = ErasedACreateInherit<16 + max_size, max_align * 2>;
    testWrapper<ErasedACreateInheritSized>(choices, &bench, "ErasedACreateInherit");
#endif // CREATE_INTERFACE_IMPL

#ifdef CAPTURING_LAMBDA
    using ErasedCapturingLambdaASized = ErasedCapturingLambdaA<max_size, max_align, 8, 8, 8, 8>;
    testWrapper<ErasedCapturingLambdaASized>(choices, &bench, "ErasedCapturingLambdaA");
#endif // CAPTURING_LAMBDA

#ifdef UNSAFE_MEMBER_PTR_CAST
    using ErasedUnsafeASized = ErasedUnsafeA<max_size, max_align>;
    testWrapper<ErasedUnsafeASized>(choices, &bench, "ErasedUnsafeA");
#endif // UNSAFE_MEMBER_PTR_CAST

#ifdef UNSAFE_FUNC_PTR_CAST
    using ErasedUnsafeFuncPtrASized = ErasedAsmA<max_size, max_align>;
    testWrapper<ErasedUnsafeFuncPtrASized>(choices, &bench, "ErasedUnsafeFuncPtrA");
#endif // UNSAFE_FUNC_PTR_CAST

#ifdef FINAL_VIRTUAL
    testVirtualFinal(choices, &bench, "VirtualFinal");
#ifdef POLYMORPHIC_VALUE
    testPolymorphicValue<true>(choices, &bench, "polymorphic_valueAFinal");
#endif // POLYMORPHIC_VALUE
#endif // FINAL_VIRTUAL

#ifdef NONFINAL_VIRTUAL
    testVirtualNonFinal(choices, &bench, "VirtualNonFinal");
#ifdef POLYMORPHIC_VALUE
    testPolymorphicValue<false>(choices, &bench, "polymorphic_valueANonFinal");
#endif // POLYMORPHIC_VALUE
#endif // NONFINAL_VIRTUAL

    std::ofstream renderOut("benchmark.render.html");
    ankerl::nanobench::render(ankerl::nanobench::templates::htmlBoxplot(), bench, renderOut);
};
