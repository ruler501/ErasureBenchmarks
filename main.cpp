#undef NDEBUG
#include <algorithm>
#include <cassert>
#include <functional>
#include <variant>
#include <type_traits>

// Allow the wrapper around call to be inlined, undefine if you want easier to read assembly
#define ALLOW_INLINE
// Allow the members to be inlined. Undefine to simulate them being implemented in a separate translation unit.
// #define ALLOW_MEMBER_INLINE
// Allow the unsafe tests to run. Only tested to fully work on GCC and Clang
// #define UNSAFE_TEST
// Time the run and report the results for each test
#define TIME_RESULT
#ifdef TIME_RESULT
    #include <chrono>
    #include <iostream>
    using namespace std::chrono;
#endif // TIME_RESULT
#ifdef ALLOW_INLINE
    #define PREVENT_INLINE(...) __VA_ARGS__
#else // ALLOW_INLINE
  #ifdef _MSC_VER
    #define PREVENT_INLINE(...) __declspec(noinline) __VA_ARGS__
  #else // _MSC_VER
    #define PREVENT_INLINE(...) __VA_ARGS__ __attribute((noinline))
  #endif // _MSC_VER
#endif // ALLOW_INLINE
#ifdef ALLOW_MEMBER_INLINE
    #define PREVENT_MEMBER_INLINE(...) __VA_ARGS__
#else // ALLOW_MEMBER_INLINE
  #ifdef _MSC_VER
    #define PREVENT_MEMBER_INLINE(...) __declspec(noinline) __VA_ARGS__
  #else // _MSC_VER
    #define PREVENT_MEMBER_INLINE(...) __VA_ARGS__ __attribute((noinline))
  #endif // _MSC_VER
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

    template<typename S>
    struct const_function_ref;

    template <typename Return, typename... Args>
    struct const_function_ref<Return(Args...) noexcept> {
        using signature = Return(Args...) noexcept;

        template <typename Functor>
#ifdef __clang__
        constexpr explicit const_function_ref(const Functor& f) : data{reinterpret_cast<const void*>(&f)}, callback{[](const void* memory, Args... args) noexcept { return static_cast<Return>(static_cast<const Functor*>(memory)->operator()(args...)); }}
#else // __clang__
        constexpr explicit const_function_ref(const Functor& f) : data{reinterpret_cast<const void*>(&f)}, callback{[](Args... args, const void* memory) noexcept { return static_cast<Return>(static_cast<const Functor*>(memory)->operator()(args...)); }}
#endif // __clang__
{}

        template <typename Functor, Return(Functor::*fn)(Args...) const noexcept>
#ifdef __clang__
        constexpr explicit const_function_ref(const Functor& f, value_type<fn>) : data{reinterpret_cast<const void*>(&f)}, callback{[](const void* memory, Args... args) noexcept { return static_cast<Return>((static_cast<const Functor*>(memory)->*fn)(args...)); }}
#else // __clang__
        constexpr explicit const_function_ref(const Functor& f, value_type<fn>) : data{reinterpret_cast<const void*>(&f)}, callback{[](Args... args, const void* memory) noexcept { return static_cast<Return>((static_cast<const Functor*>(memory)->*fn)(args...)); }}
#endif // __clang__
        {}

        template<typename... Args2>
        constexpr Return operator()(Args2... args) const noexcept {
#ifdef __clang__
            return (*callback)(data, std::forward<Args2>(args)...);
#else // __clang__
            return (*callback)(std::forward<Args2>(args)..., data);
#endif // __clang__
        }
#ifdef __clang__
        using const_callback = Return (*)(const void*, Args...) noexcept;
#else // __clang__
        using const_callback = Return (*)(Args..., const void*) noexcept;
#endif // __clang__
        const void* data;
        const const_callback callback;
    };

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
        const deletePtrType deletePtr;

        template<typename T, typename=std::enable_if_t<!std::is_same_v<std::decay_t<T>, initializing_buffer>>>
        explicit constexpr initializing_buffer(T&& t) : deletePtr{[](void* memory){ delete static_cast<std::decay_t<T>*>(memory); }} {
            using TR = std::decay_t<T>;
            new (value.__data) const TR{std::forward<T>(t)};
            static_assert(sizeof(TR) <= size);
            static_assert(alignof(TR) <= align && align % alignof(TR) == 0);
        }

        template<typename T, typename... Args>
        constexpr initializing_buffer(type_list<T>, Args&&... args) : deletePtr([](void* memory){ delete static_cast<T*>(memory);}) {
            new (value.__data) const T{std::forward<Args>(args)...};
            static_assert(sizeof(T) <= size);
            static_assert(alignof(T) <= align && align % alignof(T) == 0);
        }

        constexpr operator void*() { // NOLINT(google-explicit-constructor,hicpp-explicit-conversions)
            return value.__data;
        }

        constexpr operator const void*() const { // NOLINT(google-explicit-constructor,hicpp-explicit-conversions)
            return value.__data;
        }
    };

#ifdef UNSAFE_TEST
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
    using ConstMemFunc = Return(Class::*)(Args...) const noexcept;

    template<typename Class1, typename Class2, typename Return, typename... Args>
    union ConstMemFuncUnion {
        using funcType = Return(*)(const void*, Args...) noexcept;
        ConstMemFunc<Class1, Return, Args...> memFunc1;
        funcType fptr;
        ConstMemFunc<Class2, Return, Args...> memFunc2;
    };
#endif // UNSAFE_TEST
}

struct IA {
    [[nodiscard]] virtual PREVENT_MEMBER_INLINE(size_t) testMember(size_t, size_t) const noexcept = 0;
    [[nodiscard]] virtual PREVENT_MEMBER_INLINE(size_t) testMember2(size_t, size_t) const noexcept = 0;
    virtual ~IA() = default;
};

template<std::size_t maxSize, std::size_t alignment, size_t selector>
struct ErasedA final {
    struct helper_A {
        template<typename T>
        explicit constexpr helper_A(detail::type_list<T>)
#ifdef __clang__
                : testMemberPtr([](const void* memory, size_t a, size_t b) noexcept { return static_cast<const T*>(memory)->testMember(a, b); }),
                  testMember2Ptr([](const void* memory, size_t a, size_t b) noexcept { return static_cast<const T*>(memory)->testMember2(a, b); })
#else // __clang__
                : testMemberPtr([](size_t a, size_t b, const void* memory) noexcept { return static_cast<const T*>(memory)->testMember(a, b); }),
                  testMember2Ptr([](size_t a, size_t b, const void* memory) noexcept { return static_cast<const T*>(memory)->testMember2(a, b); })
#endif // __clang__
        {
            static_assert(sizeof(T) <= maxSize);
            static_assert(alignof(T) <= alignment && alignment % alignof(T) == 0);
        }
#ifdef __clang__
        using testMemberPtrType = size_t(*)(const void* memory, size_t a, size_t b) noexcept;
        using testMember2PtrType = size_t(*)(const void* memory, size_t a, size_t b) noexcept;
#else // __clang__
        using testMemberPtrType = size_t(*)(size_t a, size_t b, const void* memory) noexcept;
        using testMember2PtrType = size_t(*)(size_t a, size_t b, const void* memory) noexcept;
#endif // __clang__
        const testMemberPtrType testMemberPtr;
        const testMember2PtrType testMember2Ptr;
    };

    const detail::initializing_buffer<maxSize, alignment> buffer;
    const helper_A ptrs;

    [[nodiscard]] constexpr size_t testMember(size_t a, size_t b) const noexcept {
#ifdef __clang__
        return ptrs.testMemberPtr(buffer, a, b);
#else // __clang__
        return ptrs.testMemberPtr(a, b, buffer);
#endif // __clang__
    }

    [[nodiscard]] constexpr size_t testMember2(size_t a, size_t b) const noexcept {
#ifdef __clang__
        return ptrs.testMember2Ptr(buffer, a, b);
#else // __clang__
        return ptrs.testMember2Ptr(a, b, buffer);
#endif // __clang__
    }

    template<typename T, typename=std::enable_if_t<!std::is_same_v<std::decay_t<T>, ErasedA>>>
    explicit constexpr ErasedA(T&& t)
             : buffer(std::forward<T>(t)), ptrs(detail::type_list<std::decay_t<T>>{})
    { }
};

template<std::size_t maxSize, std::size_t alignment, size_t selector>
struct ErasedAInherit final {
    const detail::initializing_buffer<maxSize, alignment> buffer;

    [[nodiscard]] constexpr size_t testMember(size_t a, size_t b) const noexcept { return static_cast<const IA*>(static_cast<const void*>(buffer))->testMember(a, b); }
    [[nodiscard]] constexpr size_t testMember2(size_t a, size_t b) const noexcept { return static_cast<const IA*>(static_cast<const void*>(buffer))->testMember2(a, b); }

    template<typename T, typename=std::enable_if_t<std::is_base_of_v<IA, std::decay_t<T>>>>
    explicit constexpr ErasedAInherit(T&& t) : buffer(std::forward<T>(t))
    { }
};

template<std::size_t maxSize, std::size_t alignment, size_t selector>
struct ErasedACreateInherit final {
    template<typename T>
    struct helper_A final : public IA {
        const T obj;
        // Can't make this constexpr without C++20
        [[nodiscard]] size_t testMember(size_t a, size_t b) const noexcept final { return obj.testMember(a, b); }
        [[nodiscard]] size_t testMember2(size_t a, size_t b) const noexcept final { return obj.testMember2(a, b); }

        template<typename TF>
        constexpr helper_A(TF&& tf) : obj(std::forward<TF>(tf)) {}
    };

    const detail::initializing_buffer<maxSize + sizeof(void*), alignment + sizeof(void*)> buffer;

    [[nodiscard]] constexpr size_t testMember(size_t a, size_t b) const noexcept { return static_cast<const IA*>(static_cast<const void*>(buffer))->testMember(a, b); }
    [[nodiscard]] constexpr size_t testMember2(size_t a, size_t b) const noexcept { return static_cast<const IA*>(static_cast<const void*>(buffer))->testMember2(a, b); }

    template<typename T, typename=std::enable_if_t<!std::is_same_v<std::decay_t<T>, ErasedACreateInherit>>>
    explicit constexpr ErasedACreateInherit(T&& t) : buffer(detail::type_list<helper_A<std::decay_t<T>>>{}, std::forward<T>(t))
    { }
};

template<std::size_t maxSize, std::size_t alignment, std::size_t testMemberSize, std::size_t testMemberAlign,
        std::size_t testMember2Size, std::size_t testMember2Align>
struct ErasedCapturingLambdaA final {
    struct helper_A {
        template<typename T>
        explicit constexpr helper_A(const void* testMemberLambdaStorage, const void* testMember2LambdaStorage, detail::type_list<T>)
                : testMemberPtr(*static_cast<const testMemberLambdaType<T>*>(testMemberLambdaStorage)),
                  testMember2Ptr(*static_cast<const testMember2LambdaType<T>*>(testMember2LambdaStorage)) {
        }

        using testMemberPtrType = detail::const_function_ref<size_t(size_t, size_t) noexcept>;
        using testMember2PtrType = detail::const_function_ref<size_t(size_t, size_t) noexcept>;

        template<typename T>
        static constexpr auto createTestMemberLambda(const T& t) {
            return [&o=t](size_t a, size_t b) { return o.testMember(a, b); };
        }
        template<typename T>
        static constexpr auto createTestMember2Lambda(const T& t) {
            return [&o=t](size_t a, size_t b) { return o.testMember2(a, b); };
        }
        template<typename T>
        using testMemberLambdaType = std::decay_t<decltype(createTestMemberLambda(std::declval<const T&>()))>;
        template<typename T>
        using testMember2LambdaType = std::decay_t<decltype(createTestMember2Lambda(std::declval<const T&>()))>;
        template<typename T>
        static constexpr size_t testMemberLambdaSize = sizeof(testMemberLambdaType<T>);
        template<typename T>
        static constexpr size_t testMember2LambdaSize = sizeof(testMember2LambdaType<T>);
        template<typename T>
        static constexpr size_t testMemberLambdaAlign = alignof(testMemberLambdaType<T>);
        template<typename T>
        static constexpr size_t testMember2LambdaAlign = alignof(testMember2LambdaType<T>);

        const testMemberPtrType testMemberPtr;
        const testMember2PtrType testMember2Ptr;
    };

    const detail::initializing_buffer<maxSize, alignment> buffer;
    const detail::initializing_buffer<testMemberSize, testMemberAlign> testMemberBuffer;
    const detail::initializing_buffer<testMember2Size, testMember2Align> testMember2Buffer;
    const helper_A ptrs;

    [[nodiscard]] constexpr size_t testMember(size_t a, size_t b) const noexcept {
        return ptrs.testMemberPtr(a, b);
    }

    [[nodiscard]] constexpr size_t testMember2(size_t a, size_t b) const noexcept {
        return ptrs.testMember2Ptr(a, b);
    }

    template<typename T>
    constexpr const T& bufferAsT() {
        return *static_cast<const T*>(static_cast<const void*>(buffer));
    }

    template<typename T, typename=std::enable_if_t<!std::is_same_v<std::decay_t<T>, ErasedCapturingLambdaA>>>
    explicit constexpr ErasedCapturingLambdaA(T&& t)
            : buffer(std::forward<T>(t)),
              testMemberBuffer(helper_A::template createTestMemberLambda(bufferAsT<std::decay_t<T>>())),
              testMember2Buffer(helper_A::template createTestMember2Lambda(bufferAsT<std::decay_t<T>>())),
              ptrs(testMemberBuffer, testMember2Buffer, detail::type_list<std::decay_t<T>>{})
    { }
};

#ifdef UNSAFE_TEST
template<std::size_t maxSize, std::size_t alignment>
struct ErasedUnsafeA final {
    struct C final {};
    struct helper_A {
        template<typename T>
        constexpr explicit helper_A(detail::type_list<T>)
                : testMemberPtr(reinterpret_cast<testMemberPtrType>(&T::testMember)),
                  testMember2Ptr(reinterpret_cast<testMember2PtrType>(&T::testMember2))
        { }

        using testMemberPtrType = size_t(C::*)(size_t a, size_t b) const noexcept;
        using testMember2PtrType = size_t(C::*)(size_t a, size_t b) const noexcept;
        const testMemberPtrType testMemberPtr;
        const testMember2PtrType testMember2Ptr;
    };

    const detail::initializing_buffer<maxSize, alignment> buffer;
    const helper_A ptrs;

    [[nodiscard]] constexpr size_t testMember(size_t a, size_t b) const noexcept {
        return (static_cast<const C*>(static_cast<const void*>(buffer))->*ptrs.testMemberPtr)(a, b);
    }

    [[nodiscard]] constexpr size_t testMember2(size_t a, size_t b) const noexcept {
        return (static_cast<const C*>(static_cast<const void*>(buffer))->*ptrs.testMember2Ptr)(a, b);
    }

    template<typename T, typename=std::enable_if_t<!std::is_same_v<std::decay_t<T>, ErasedUnsafeA>>>
    explicit constexpr ErasedUnsafeA(T&& t)
            : buffer(std::forward<T>(t)), ptrs(detail::type_list<std::decay_t<T>>{})
    { }
};

template<std::size_t maxSize, std::size_t alignment>
struct ErasedAsmA final {
    struct C final {};
    struct helper_A {
        template<typename T>
        constexpr explicit helper_A(detail::type_list<T>)
                : testMemberPtr(detail::ConstMemFuncUnion<T, C, size_t, size_t, size_t>{&T::testMember}.fptr),
                  testMember2Ptr(detail::ConstMemFuncUnion<T, C, size_t, size_t, size_t>{&T::testMember2}.fptr)
        { }

        using funcType = size_t(*)(const void*, size_t, size_t) noexcept;
        const funcType testMemberPtr;
        const funcType testMember2Ptr;
    };

    const detail::initializing_buffer<maxSize, alignment> buffer;
    const helper_A ptrs;

    [[nodiscard]] constexpr size_t testMember(size_t a, size_t b) const noexcept {
        return ptrs.testMemberPtr(buffer, a, b);
    }

    [[nodiscard]] constexpr size_t testMember2(size_t a, size_t b) const noexcept {
        return ptrs.testMember2Ptr(buffer, a, b);
    }

    template<typename T, typename=std::enable_if_t<!std::is_same_v<std::decay_t<T>, ErasedAsmA>>>
    explicit constexpr ErasedAsmA(T&& t)
            : buffer(std::forward<T>(t)), ptrs(detail::type_list<std::decay_t<T>>{})
    { }
};
#endif // UNSAFE_TEST
#ifndef _MSC_VER
#include <dyno.hpp>

// Test with the Dyno framework
DYNO_INTERFACE(ErasedADyno,
    (testMember, std::size_t(std::size_t, std::size_t) const),
    (testMember2, std::size_t(std::size_t, std::size_t) const));
#endif // _MSC_VER

template<typename... Alts>
struct VariantA {
    const std::variant<Alts...> storage;

    [[nodiscard]] constexpr size_t testMember(size_t a, size_t b) const noexcept {
        return std::visit([a,b](auto o){ return o.testMember(a, b); }, storage);
    }
    [[nodiscard]] constexpr size_t testMember2(size_t a, size_t b) const noexcept {
        return std::visit([a,b](auto o){ return o.testMember2(a, b); }, storage);
    }

    template<typename T>
    explicit constexpr VariantA(T&& t) : storage(std::forward<T>(t)) {}
};

struct ACompliant final {
    [[nodiscard]] PREVENT_MEMBER_INLINE(size_t) testMember(size_t a, size_t b) const noexcept { return a ^ b; }
    [[nodiscard]] PREVENT_MEMBER_INLINE(size_t) testMember2(size_t a, size_t b) const noexcept { return a ^ ~b; }
};

struct AImpl : public IA {
    [[nodiscard]] PREVENT_MEMBER_INLINE(size_t) testMember(size_t a, size_t b) const noexcept override { return a ^ b; }
    [[nodiscard]] PREVENT_MEMBER_INLINE(size_t) testMember2(size_t a, size_t b) const noexcept override { return a ^ ~b; }
};

struct AImplFinal final : public IA {
    [[nodiscard]] PREVENT_MEMBER_INLINE(size_t) testMember(size_t a, size_t b) const noexcept override final { return a ^ b; }
    [[nodiscard]] PREVENT_MEMBER_INLINE(size_t) testMember2(size_t a, size_t b) const noexcept override final { return a ^ ~b; }
};

constexpr std::size_t maxValue = 3llu << 30u;

template<typename T, size_t selector>
#ifdef ALLOW_INLINE
constexpr
#endif // ALLOW_INLINE
#ifdef __clang__
PREVENT_INLINE(std::size_t) callMember(size_t a, size_t b, const T& o) {
#else // __clang__
PREVENT_INLINE(std::size_t) callMember(const T& o, size_t a, size_t b) {
#endif // __clang__
    return o.testMember(a, b);
}

template<typename T, size_t selector>
#ifndef PREVENT_INLINE
constexpr
#endif // !PREVENT_INLINE
#ifdef __clang__
PREVENT_INLINE(std::size_t) callMember2(size_t a, size_t b, const T& o) {
#else // __clang__
PREVENT_INLINE(std::size_t) callMember2(const T& o, size_t a, size_t b) {
#endif // __clang__
    return o.testMember2(a, b);
}

template<typename T, size_t selector>
#ifdef _MSC_VER
__declspec(noinline) std::size_t runTest(std::size_t start, const T& b) {
#else
std::size_t __attribute((noinline)) runTest(std::size_t start, const T& b) {
#endif
    for(std::size_t i=0; i < maxValue; i += 2) {
#ifdef __clang__
        start += callMember<T, selector>(i, start, b);
        start += callMember2<T, selector>(i, start, b);
#else // __clang__
        start += callMember<T, selector>(b, i, start);
        start += callMember2<T, selector>(b, i, start);
#endif // __clang__
    }
    return start;
}

template<size_t selector>
using ErasedASized = ErasedA<std::max({sizeof(AImpl), sizeof(AImplFinal), sizeof(ACompliant)}), std::max({alignof(AImpl), alignof(AImplFinal), alignof(ACompliant)}), selector>;
template<size_t selector>
using ErasedAInheritSized = ErasedA<std::max({sizeof(AImpl), sizeof(AImplFinal)}), std::max({alignof(AImpl), alignof(AImplFinal)}), selector>;
template<size_t selector>
using ErasedACreateInheritSized = ErasedACreateInherit<std::max({sizeof(AImpl), sizeof(AImplFinal), sizeof(ACompliant)}), std::max({alignof(AImpl), alignof(AImplFinal), alignof(ACompliant)}), selector>;
using ErasedCapturingLambdaACompliant = ErasedCapturingLambdaA<sizeof(ACompliant), alignof(ACompliant), 8, 8, 8, 8>;
#ifdef UNSAFE_TEST
using ErasedUnsafeACompliant = ErasedUnsafeA<sizeof(ACompliant), alignof(ACompliant)>;
using ErasedAsmACompliant = ErasedAsmA<sizeof(ACompliant), alignof(ACompliant)>;
#endif // UNSAFE_TEST
using VariantAAlts = VariantA<ACompliant, AImpl, AImplFinal>;

int main(int argc, char* argv[]) {
    const ACompliant b;
    const AImpl bI1;
    const AImplFinal bI2;
    const ErasedASized<1> bEI1{bI1};
    const ErasedAInheritSized<1> bEI2{bI2};
    const ErasedACreateInheritSized<1> bECI{bI2};
    const ErasedCapturingLambdaACompliant bEL{b};
    const ErasedASized<2> bE{b};
#ifdef UNSAFE_TEST
    const ErasedUnsafeACompliant bEU{b};
    const ErasedAsmACompliant bEA{b};
#endif // UNSAFE_TEST
#ifndef _MSC_VER
    const ErasedADyno bD{b};
#endif // _MSC_VER
    const VariantAAlts bV1{b};
    const VariantAAlts bV2{bI2};
#ifdef TIME_RESULT
    auto startTime = high_resolution_clock::now();
    std::size_t res = startTime.time_since_epoch().count();
#else // TIME_RESULT
    std::size_t res = argc;
#endif // TIME_RESULT
    size_t res1 = runTest<ACompliant, 1>(res, b);
#ifdef TIME_RESULT
    auto mid1Time = high_resolution_clock::now();
#endif // TIME_RESULT
    size_t res2 = runTest<IA, 2>(res, bI1);
#ifdef TIME_RESULT
    auto mid2Time = high_resolution_clock::now();
#endif // TIME_RESULT
    size_t res3 = runTest<IA, 3>(res, bI2);
#ifdef TIME_RESULT
    auto mid3Time = high_resolution_clock::now();
#endif // TIME_RESULT
    size_t res4 = runTest<ErasedASized<1>, 4>(res, bEI1);
#ifdef TIME_RESULT
    auto mid4Time = high_resolution_clock::now();
#endif // TIME_RESULT
    size_t res5 = runTest<ErasedAInheritSized<1>, 5>(res, bEI2);
#ifdef TIME_RESULT
    auto mid5Time = high_resolution_clock::now();
#endif // TIME_RESULT
    size_t res6 = runTest<ErasedACreateInheritSized<1>, 6>(res, bECI);
#ifdef TIME_RESULT
    auto mid6Time = high_resolution_clock::now();
#endif // TIME_RESULT
    size_t res7 = runTest<ErasedCapturingLambdaACompliant, 7>(res, bEL);
#ifdef TIME_RESULT
    auto mid7Time = high_resolution_clock::now();
#endif // TIME_RESULT
    size_t res8 = runTest<ErasedASized<2>, 8>(res, bE);
#ifdef TIME_RESULT
    auto mid8Time = high_resolution_clock::now();
#endif // TIME_RESULT
#ifdef UNSAFE_TEST
    size_t res9 = runTest<ErasedUnsafeACompliant, 9>(res, bEU);
  #ifdef TIME_RESULT
    auto mid9Time = high_resolution_clock::now();
  #endif // TIME_RESULT
    size_t res10 = runTest<ErasedAsmACompliant, 10>(res, bEA);
#endif // UNSAFE_TEST
#ifdef TIME_RESULT
    auto mid10Time = high_resolution_clock::now();
#endif // TIME_RESULT
#ifndef _MSC_VER
    size_t res11 = runTest<ErasedADyno, 11>(res, bD);
#endif // _MSC_VER
#ifdef TIME_RESULT
    auto mid11Time = high_resolution_clock::now();
#endif // TIME_RESULT
    size_t res12 = runTest<VariantAAlts, 12>(res, bV1);
#ifdef TIME_RESULT
    auto mid12Time = high_resolution_clock::now();
#endif // TIME_RESULT
    size_t res13 = runTest<VariantAAlts, 13>(res, bV2);
#ifdef TIME_RESULT
    auto mid13Time = high_resolution_clock::now();
#endif // TIME_RESULT
#ifdef TIME_RESULT
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
              << "Type Erased Inheritance: " << dur4 << " (" << (dur4/ 10'000)/100.0 << "s)(" << ((int)(dur4/fdur1*10000))/100.0 << "%) res: " << res4 << "\n"
              << "Type Erased Final Inheritance: " << dur5 << " (" << (dur5/ 10'000)/100.0 << "s)(" << ((int)(dur5/fdur1*10000))/100.0 << "%) res: " << res5 << "\n"
              << "Type Erased Created Inheritance: " << dur6 << " (" << (dur6/ 10'000)/100.0 << "s)(" << ((int)(dur6/fdur1*10000))/100.0 << "%) res: " << res6 << "\n"
              << "Type Erased Capturing Lambda: " << dur7 << " (" << (dur7/ 10'000)/100.0 << "s)(" << ((int)(dur7/fdur1*10000))/100.0 << "%) res: " << res7 << "\n"
              << "Type Erased Member Function Pointer: " << dur8 << " (" << (dur8/ 10'000)/100.0 << "s)(" << ((int)(dur8/fdur1*10000))/100.0 << "%) res: " << res8 << "\n"
  #ifdef UNSAFE_TEST
              << "Type Erased Unsafe Member Function Pointer Cast: " << dur9 << " (" << (dur9/ 10'000)/100.0 << "s)(" << ((int)(dur9/fdur1*10000))/100.0 << "%) res: " << res9 << "\n"
              << "Type Erased Unsafe Sketchy Function Cast:  " << dur10 << " (" << (dur10/ 10'000)/100.0 << "s)(" << ((int)(dur10/fdur1*10000))/100.0 << "%) res: " << res10 << "\n"
  #endif // UNSAFE_TEST
  #ifndef _MSC_VER
              << "Dyno Default:  " << dur11 << " (" << (dur11/ 10'000)/100.0 << "s)(" << ((int)(dur11/fdur1*10000))/100.0 << "%) res: " << res11 << "\n"
  #endif // _MSC_VER
              << "Variant Direct:  " << dur12 << " (" << (dur12/ 10'000)/100.0 << "s)(" << ((int)(dur12/fdur1*10000))/100.0 << "%) res: " << res12 << "\n"
              << "Variant Final Inheritance:  " << dur13 << " (" << (dur13/ 10'000)/100.0 << "s)(" << ((int)(dur13/fdur1*10000))/100.0 << "%) res: " << res13 << "\n"

            ;
#endif // TIME_RESULT
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