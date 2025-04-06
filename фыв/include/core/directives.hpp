#pragma once

// Compiler-specific directives
#ifdef __clang__
    #define CLOUD_CLANG
    #define CLOUD_COMPILER "clang"
#elif defined(__GNUC__)
    #define CLOUD_GCC
    #define CLOUD_COMPILER "gcc"
#elif defined(_MSC_VER)
    #define CLOUD_MSVC
    #define CLOUD_COMPILER "msvc"
#else
    #define CLOUD_UNKNOWN_COMPILER
    #define CLOUD_COMPILER "unknown"
#endif

// Platform-specific directives
#ifdef _WIN32
    #define CLOUD_WINDOWS
    #define CLOUD_PLATFORM "windows"
#elif defined(__linux__)
    #define CLOUD_LINUX
    #define CLOUD_PLATFORM "linux"
#elif defined(__APPLE__)
    #define CLOUD_MACOS
    #define CLOUD_PLATFORM "macos"
#else
    #define CLOUD_UNKNOWN_PLATFORM
    #define CLOUD_PLATFORM "unknown"
#endif

// Architecture-specific directives
#ifdef __x86_64__
    #define CLOUD_X86_64
    #define CLOUD_ARCH "x86_64"
#elif defined(__aarch64__)
    #define CLOUD_ARM64
    #define CLOUD_ARCH "arm64"
#elif defined(__arm__)
    #define CLOUD_ARM
    #define CLOUD_ARCH "arm"
#else
    #define CLOUD_UNKNOWN_ARCH
    #define CLOUD_ARCH "unknown"
#endif

// Feature detection
#ifdef __cpp_concepts
    #define CLOUD_HAS_CONCEPTS
#endif

#ifdef __cpp_coroutines
    #define CLOUD_HAS_COROUTINES
#endif

#ifdef __cpp_modules
    #define CLOUD_HAS_MODULES
#endif

#ifdef __cpp_ranges
    #define CLOUD_HAS_RANGES
#endif

// Debug/Release directives
#ifdef NDEBUG
    #define CLOUD_RELEASE
    #define CLOUD_DEBUG 0
#else
    #define CLOUD_DEBUG
    #define CLOUD_DEBUG 1
#endif

// Optimization directives
#ifdef __OPTIMIZE__
    #define CLOUD_OPTIMIZED
#endif

// Inline directives
#ifdef CLOUD_MSVC
    #define CLOUD_INLINE __forceinline
#else
    #define CLOUD_INLINE __attribute__((always_inline))
#endif

// Alignment directives
#define CLOUD_ALIGN(x) alignas(x)
#define CLOUD_CACHE_LINE_ALIGN CLOUD_ALIGN(64)

// Deprecation directives
#define CLOUD_DEPRECATED(msg) [[deprecated(msg)]]
#define CLOUD_DEPRECATED_MSG(msg) [[deprecated(msg)]]

// Nodiscard directives
#define CLOUD_NODISCARD [[nodiscard]]
#define CLOUD_NODISCARD_MSG(msg) [[nodiscard(msg)]]

// Fallthrough directives
#define CLOUD_FALLTHROUGH [[fallthrough]]

// Likely/Unlikely directives
#ifdef __has_builtin(__builtin_expect)
    #define CLOUD_LIKELY(x) __builtin_expect(!!(x), 1)
    #define CLOUD_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
    #define CLOUD_LIKELY(x) (x)
    #define CLOUD_UNLIKELY(x) (x)
#endif

// Branch prediction directives
#ifdef __has_builtin(__builtin_expect_with_probability)
    #define CLOUD_EXPECT(x, p) __builtin_expect_with_probability(!!(x), 1, p)
#else
    #define CLOUD_EXPECT(x, p) (x)
#endif

// Hot/Cold function attributes
#ifdef __has_attribute(hot)
    #define CLOUD_HOT __attribute__((hot))
#else
    #define CLOUD_HOT
#endif

#ifdef __has_attribute(cold)
    #define CLOUD_COLD __attribute__((cold))
#else
    #define CLOUD_COLD
#endif

// Pure/Const function attributes
#define CLOUD_PURE __attribute__((pure))
#define CLOUD_CONST __attribute__((const))

// Thread safety attributes
#define CLOUD_THREAD_SAFE __attribute__((thread_safe))
#define CLOUD_GUARDED_BY(x) __attribute__((guarded_by(x)))
#define CLOUD_PT_GUARDED_BY(x) __attribute__((pt_guarded_by(x)))
#define CLOUD_ACQUIRED_BEFORE(...) __attribute__((acquired_before(__VA_ARGS__)))
#define CLOUD_ACQUIRED_AFTER(...) __attribute__((acquired_after(__VA_ARGS__)))
#define CLOUD_EXCLUSIVE_LOCK_FUNCTION(...) __attribute__((exclusive_lock_function(__VA_ARGS__)))
#define CLOUD_SHARED_LOCK_FUNCTION(...) __attribute__((shared_lock_function(__VA_ARGS__)))
#define CLOUD_EXCLUSIVE_TRYLOCK_FUNCTION(...) __attribute__((exclusive_trylock_function(__VA_ARGS__)))
#define CLOUD_SHARED_TRYLOCK_FUNCTION(...) __attribute__((shared_trylock_function(__VA_ARGS__)))
#define CLOUD_UNLOCK_FUNCTION(...) __attribute__((unlock_function(__VA_ARGS__)))
#define CLOUD_LOCK_RETURNED(x) __attribute__((lock_returned(x)))
#define CLOUD_LOCKS_EXCLUDED(...) __attribute__((locks_excluded(__VA_ARGS__)))
#define CLOUD_EXCLUSIVE_LOCKS_REQUIRED(...) __attribute__((exclusive_locks_required(__VA_ARGS__)))
#define CLOUD_SHARED_LOCKS_REQUIRED(...) __attribute__((shared_locks_required(__VA_ARGS__)))

// Memory safety attributes
#define CLOUD_NONNULL(...) __attribute__((nonnull(__VA_ARGS__)))
#define CLOUD_NONNULL_ALL __attribute__((nonnull))
#define CLOUD_RETURNS_NONNULL __attribute__((returns_nonnull))
#define CLOUD_ALLOC_SIZE(...) __attribute__((alloc_size(__VA_ARGS__)))
#define CLOUD_ALLOC_ALIGN(x) __attribute__((alloc_align(x)))

// Format string attributes
#define CLOUD_PRINTF(fmt, first) __attribute__((format(printf, fmt, first)))
#define CLOUD_SCANF(fmt, first) __attribute__((format(scanf, fmt, first)))

// Section attributes
#define CLOUD_SECTION(x) __attribute__((section(x)))
#define CLOUD_USED __attribute__((used))
#define CLOUD_UNUSED __attribute__((unused))

// Visibility attributes
#define CLOUD_VISIBILITY(x) __attribute__((visibility(x)))
#define CLOUD_INTERNAL CLOUD_VISIBILITY("internal")
#define CLOUD_HIDDEN CLOUD_VISIBILITY("hidden")
#define CLOUD_PROTECTED CLOUD_VISIBILITY("protected")
#define CLOUD_DEFAULT_VISIBILITY CLOUD_VISIBILITY("default")

// Linkage attributes
#define CLOUD_WEAK __attribute__((weak))
#define CLOUD_ALIAS(x) __attribute__((alias(x)))
#define CLOUD_WEAK_ALIAS(x) __attribute__((weak, alias(x)))

// Optimization hints
#define CLOUD_ASSUME(x) __builtin_assume(x)
#define CLOUD_UNREACHABLE __builtin_unreachable()

// Debugging macros
#ifdef CLOUD_DEBUG
    #define CLOUD_ASSERT(x) assert(x)
    #define CLOUD_VERIFY(x) assert(x)
    #define CLOUD_STATIC_ASSERT(x) static_assert(x)
#else
    #define CLOUD_ASSERT(x) ((void)0)
    #define CLOUD_VERIFY(x) ((void)(x))
    #define CLOUD_STATIC_ASSERT(x) static_assert(x)
#endif

// Logging macros
#ifdef CLOUD_DEBUG
    #define CLOUD_LOG(level, msg, ...) \
        cloud::logging::log(level, __FILE__, __LINE__, msg, ##__VA_ARGS__)
    #define CLOUD_TRACE(msg, ...) CLOUD_LOG(cloud::logging::Level::Trace, msg, ##__VA_ARGS__)
    #define CLOUD_DEBUG(msg, ...) CLOUD_LOG(cloud::logging::Level::Debug, msg, ##__VA_ARGS__)
    #define CLOUD_INFO(msg, ...) CLOUD_LOG(cloud::logging::Level::Info, msg, ##__VA_ARGS__)
    #define CLOUD_WARN(msg, ...) CLOUD_LOG(cloud::logging::Level::Warn, msg, ##__VA_ARGS__)
    #define CLOUD_ERROR(msg, ...) CLOUD_LOG(cloud::logging::Level::Error, msg, ##__VA_ARGS__)
#else
    #define CLOUD_LOG(level, msg, ...) ((void)0)
    #define CLOUD_TRACE(msg, ...) ((void)0)
    #define CLOUD_DEBUG(msg, ...) ((void)0)
    #define CLOUD_INFO(msg, ...) ((void)0)
    #define CLOUD_WARN(msg, ...) ((void)0)
    #define CLOUD_ERROR(msg, ...) ((void)0)
#endif

// Performance measurement macros
#ifdef CLOUD_DEBUG
    #define CLOUD_PROFILE_SCOPE(name) \
        cloud::profiling::ScopeTimer CLOUD_CONCAT(profile_scope_, __LINE__)(name)
    #define CLOUD_PROFILE_FUNCTION() CLOUD_PROFILE_SCOPE(__FUNCTION__)
#else
    #define CLOUD_PROFILE_SCOPE(name) ((void)0)
    #define CLOUD_PROFILE_FUNCTION() ((void)0)
#endif

// Memory tracking macros
#ifdef CLOUD_DEBUG
    #define CLOUD_TRACK_MEMORY(x) cloud::memory::track(x)
    #define CLOUD_UNTRACK_MEMORY(x) cloud::memory::untrack(x)
#else
    #define CLOUD_TRACK_MEMORY(x) ((void)0)
    #define CLOUD_UNTRACK_MEMORY(x) ((void)0)
#endif

// Utility macros
#define CLOUD_CONCAT(x, y) x##y
#define CLOUD_STRINGIFY(x) #x
#define CLOUD_STRINGIFY_EXPAND(x) CLOUD_STRINGIFY(x)
#define CLOUD_COUNT_ARGS(...) CLOUD_COUNT_ARGS_IMPL(__VA_ARGS__, 5,4,3,2,1,0)
#define CLOUD_COUNT_ARGS_IMPL(_1,_2,_3,_4,_5,N,...) N

// Version macros
#define CLOUD_VERSION_MAJOR 1
#define CLOUD_VERSION_MINOR 0
#define CLOUD_VERSION_PATCH 0
#define CLOUD_VERSION_STRING CLOUD_STRINGIFY_EXPAND(CLOUD_VERSION_MAJOR.CLOUD_VERSION_MINOR.CLOUD_VERSION_PATCH)

// Feature flags
#define CLOUD_FEATURE_THREADING 1
#define CLOUD_FEATURE_NETWORKING 1
#define CLOUD_FEATURE_CRYPTOGRAPHY 1
#define CLOUD_FEATURE_STORAGE 1
#define CLOUD_FEATURE_MONITORING 1
#define CLOUD_FEATURE_LOGGING 1
#define CLOUD_FEATURE_PROFILING 1
#define CLOUD_FEATURE_METRICS 1
#define CLOUD_FEATURE_TRACING 1
#define CLOUD_FEATURE_DIAGNOSTICS 1

// Compile-time checks
static_assert(sizeof(void*) == 8, "Cloud service requires 64-bit platform");
static_assert(__cplusplus >= 201703L, "Cloud service requires C++17 or later"); 