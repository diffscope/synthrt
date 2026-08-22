// T-01: as<>() 类型不匹配极端测试 (BF-50 回归)
//
// 覆盖 BF-50 修复点: as<>() 未判空即解引用 (7 处)。
// BF-50 在每处 as<>() 调用后添加 `if (!typed) return Error;` 防御性判空:
//   - chain/multig2p TaskImpl (G2pInputV1) — plugins/G2P/chain, plugins/G2P/multig2p
//   - ds-dict TaskImpl (DictInputV1)       — plugins/G2P/ds-dict
//   - acoustic/vocoder/duration/pitch/variance (AcousticInitArgs 等) — plugins/diffsinger
//
// as<T>() 模板定义 (NamedObject.h:100-102):
//   template <class U> NO<U> as() const noexcept {
//       return std::static_pointer_cast<U>(*this);
//   }
//
// 关键语义: static_pointer_cast 不做运行时类型检查:
//   - null NO<>            -> as<U>() -> null NO<U>            (安全)
//   - non-null NO<T>       -> as<U>() -> non-null NO<U>        (即使 T/U 无关也返回非空)
//   - 跨兄弟类型 as<> (G2pInputV1 -> DictInputV1) 编译通过, 解引用是 UB
//
// 故 BF-50 的 `if (!typed)` 判空仅能捕获 null 输入 (as<> 对 non-null 总返回 non-null)。
// 类型不匹配的安全保障来自上游 objectName() 校验:
//   - 推理插件: validateStartInput/validateInitArgs (PluginCommon.h, BF-43~49 覆盖)
//   - G2P 插件: 无 objectName 校验 (已知缺口, BF-50 仅补判空)
//
// 测试范围 (L1, 仅链接 srt::g2p):
//   1. 直接测试 NO<T>::as<U>() 模板 (BF-50 核心)
//   2. 用 G2P 类型 (G2pInputV1/DictInputV1) 与自定义 fake 类型模拟类型不匹配
//   3. 复现 BF-50 修复的判空模式, 验证 null 输入返回结构化错误不崩溃
//   4. 推理插件 as<>() 调用使用同一 NO<T>::as<U>() 模板, 行为由本测试覆盖;
//      其 objectName 校验模式 (validateStartInput) 由 tst_plugin_common_extreme.cpp
//      (BF-43~49) 覆盖, 此处用 fake 类型复现同等模式。
//
// 准则核对: ROBUST-03 (防空空指针); ROBUST-05 (错误不丢失)。
// 验收: 类型不匹配/null 返回结构化 Error, 无崩溃。

#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Core/Support/Diagnostic.h>
#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Task/ITask.h>
#include <synthrt/G2P/Support/Error.h>
#include <synthrt/G2P/Task/DictTask.h>
#include <synthrt/G2P/Task/G2pTask.h>

using srt::core::ErrorCode;
using srt::core::Error;
using srt::core::Expected;
using srt::core::NamedObject;
using srt::core::NO;
using srt::core::TaskInitArgs;
using srt::core::TaskResult;
using srt::core::TaskStartInput;
using srt::g2p::DictInputV1;
using srt::g2p::G2pInputV1;
using srt::g2p::G2pResultV1;
using srt::g2p::TaskInput;

// Catch2 needs operator<< to stream ErrorCode values in failed assertions.
// Identical to tst_g2p_edge.cpp; both are inline in srt::core (ODR-safe).
namespace srt::core {
inline std::ostream &operator<<(std::ostream &os, const ErrorCode &code) {
    return os << errorCodeToString(code);
}
} // namespace srt::core

namespace {

    constexpr auto kLogPrefix = "[T-01]";

    // Fake TaskStartInput subclasses with distinct objectNames, used to simulate
    // type-mismatch scenarios (proxy for acoustic/vocoder/duration/pitch/variance
    // StartInput types which are not linkable from srt::g2p). The 5 inference
    // plugins each declare a StartInput subclass whose constructor passes
    // API_NAME to TaskStartInput; these fakes replicate that pattern.
    class FakeStartInputA : public TaskStartInput {
    public:
        FakeStartInputA() : TaskStartInput("fakeA") {
        }
    };
    class FakeStartInputB : public TaskStartInput {
    public:
        FakeStartInputB() : TaskStartInput("fakeB") {
        }
    };

    // Fake TaskInitArgs subclasses with distinct objectNames (proxy for
    // AcousticInitArgs / VocoderInitArgs / etc.).
    class FakeInitArgsA : public TaskInitArgs {
    public:
        FakeInitArgsA() : TaskInitArgs("fakeInitA") {
        }
    };
    class FakeInitArgsB : public TaskInitArgs {
    public:
        FakeInitArgsB() : TaskInitArgs("fakeInitB") {
        }
    };

    /// Replicate the G2P chain/multig2p BF-50 fix pattern:
    ///   1. if (!input) return Error(ConfigError/NullPointerError);
    ///   2. auto typed = input.as<G2pInputV1>();
    ///   3. if (!typed) return Error(ValidationError);
    ///   4. dereference typed->g2pInput
    /// Step 3 catches null input (BF-50). For non-null type-mismatched input,
    /// step 3 does NOT catch (static_pointer_cast returns non-null); the caller
    /// must NOT pass a type-mismatched non-null input (no objectName gate in G2P
    /// plugins). This helper is only called with null or correctly-typed input.
    Expected<NO<TaskResult>> chainStyleStart(const NO<TaskInput> &input) {
        if (!input) {
            return Error(ErrorCode::G2pConfigError,
                         std::string(kLogPrefix) + " g2p input is nullptr");
        }
        const auto g2pInput = input.as<G2pInputV1>();
        if (!g2pInput) {
            // BF-50 defensive check: only reachable if as<>() returned null,
            // which only happens when input was null (caught above). Defense-in-depth.
            return Error(ErrorCode::G2pValidationError,
                         std::string(kLogPrefix) + " type mismatch, expected G2pInputV1");
        }
        auto result = NO<G2pResultV1>::create();
        result->g2pResult.reserve(g2pInput->g2pInput.size());
        return result;
    }

    /// Replicate the G2P ds-dict BF-50 fix pattern:
    ///   1. if (!input) return Error(NullPointerError);
    ///   2. auto typed = input.as<DictInputV1>();
    ///   3. if (!typed) return Error(ValidationError);
    /// Same null-safety semantics as chainStyleStart; uses NullPointerError
    /// for null input (matching actual ds-dict TaskImpl.cpp:110).
    Expected<NO<TaskResult>> dsDictStyleStart(const NO<TaskInput> &input) {
        if (!input) {
            return Error(ErrorCode::G2pNullPointerError,
                         std::string(kLogPrefix) + " dict input is nullptr");
        }
        auto dictInput = input.as<DictInputV1>();
        if (!dictInput) {
            return Error(ErrorCode::G2pValidationError,
                         std::string(kLogPrefix) + " Invalid input type, expected DictInputV1");
        }
        // ds-dict would call processQuery(*dictInput); return an empty result here.
        return NO<G2pResultV1>::create();
    }

    /// Replicate the inference plugin BF-50 fix pattern (acoustic/vocoder/etc.):
    ///   1. validateStartInput: if (!input) return InvalidArgument;
    ///                         if (input->objectName() != apiName) return InvalidArgument;
    ///   2. auto typed = input.as<SpecificStartInput>();
    ///   3. if (!typed) return InferenceInputInvalid;
    /// The objectName check at step 1 is the real type-mismatch guard for
    /// non-null input; step 3 catches null input (defense-in-depth, BF-50).
    /// This mirrors validateStartInput in PluginCommon.h (BF-46) which cannot
    /// be included here (srt::g2p does not link dsinfer).
    Expected<void> inferenceStyleValidateStart(const NO<TaskStartInput> &input,
                                                std::string_view apiName) {
        if (!input) {
            return Error(ErrorCode::InvalidArgument,
                         std::string(kLogPrefix) + " start: input is nullptr");
        }
        if (input->objectName() != apiName) {
            return Error(ErrorCode::InvalidArgument,
                         std::string(kLogPrefix) + " start: invalid input name: expected \"" +
                             std::string(apiName) + "\", got \"" + input->objectName() + "\"");
        }
        return Expected<void>();
    }

    /// Replicate validateInitArgs (PluginCommon.h, BF-45) for the inference
    /// plugins' initialize() entry. Same objectName gate pattern.
    Expected<void> inferenceStyleValidateInitArgs(const NO<TaskInitArgs> &args,
                                                   std::string_view apiName) {
        if (!args) {
            return Error(ErrorCode::InvalidArgument,
                         std::string(kLogPrefix) + " task init args is nullptr");
        }
        if (args->objectName() != apiName) {
            return Error(ErrorCode::InvalidArgument,
                         std::string(kLogPrefix) + " invalid task init args name: expected \"" +
                             std::string(apiName) + "\", got \"" + args->objectName() + "\"");
        }
        return Expected<void>();
    }

} // namespace

// ===========================================================================
// G2P-AS-01: as<>() on null NO<> returns null NO<> (BF-50 core)
//   The BF-50 fix added `if (!typed)` checks after as<>(). For these checks to
//   be reachable, as<>() must return null when the source is null. Verify
//   static_pointer_cast propagates nullity (null shared_ptr -> null shared_ptr).
// ===========================================================================
TEST_CASE("G2P-AS-01: as<>() on null NO<> returns null NO<> (BF-50 core)",
          "[g2p][extreme][bf-50]") {
    SECTION("null NO<TaskInput> as<G2pInputV1> yields null") {
        NO<TaskInput> nullInput;
        REQUIRE(!nullInput);
        const auto typed = nullInput.as<G2pInputV1>();
        REQUIRE(!typed);
    }

    SECTION("null NO<TaskInput> as<DictInputV1> yields null") {
        NO<TaskInput> nullInput;
        const auto typed = nullInput.as<DictInputV1>();
        REQUIRE(!typed);
    }

    SECTION("null NO<TaskStartInput> as<FakeStartInputA> yields null") {
        NO<TaskStartInput> nullInput;
        const auto typed = nullInput.as<FakeStartInputA>();
        REQUIRE(!typed);
    }

    SECTION("null NO<TaskInitArgs> as<FakeInitArgsA> yields null") {
        NO<TaskInitArgs> nullArgs;
        const auto typed = nullArgs.as<FakeInitArgsA>();
        REQUIRE(!typed);
    }

    SECTION("upcast of null NO<G2pInputV1> as<TaskInput> yields null") {
        NO<G2pInputV1> nullInput;
        REQUIRE(!nullInput);
        const auto up = nullInput.as<TaskInput>();
        REQUIRE(!up);
    }
}

// ===========================================================================
// G2P-AS-02: as<>() on correctly-typed NO<> returns valid non-null
//   The happy path: a correctly-typed non-null NO<T> upcast to NO<Base> then
//   downcast back via as<T>() must roundtrip safely and be dereferenceable.
// ===========================================================================
TEST_CASE("G2P-AS-02: as<>() on correctly-typed NO<> returns valid non-null",
          "[g2p][extreme][bf-50]") {
    SECTION("G2pInputV1 upcast then as<G2pInputV1> roundtrip preserves data") {
        auto g2pInput = NO<G2pInputV1>::create();
        g2pInput->g2pInput = {"hello", "world"};
        NO<TaskInput> base = g2pInput; // upcast to base
        REQUIRE(base);

        const auto typed = base.as<G2pInputV1>();
        REQUIRE(typed);
        REQUIRE(typed->g2pInput.size() == 2);
        REQUIRE(typed->g2pInput[0] == "hello");
        REQUIRE(typed->g2pInput[1] == "world");
        REQUIRE(typed->objectName() == "G2pInputV1");
    }

    SECTION("DictInputV1 upcast then as<DictInputV1> roundtrip preserves data") {
        auto dictInput = NO<DictInputV1>::create();
        dictInput->dictId = "test_dict";
        dictInput->keys = {"k1", "k2"};
        NO<TaskInput> base = dictInput;
        REQUIRE(base);

        const auto typed = base.as<DictInputV1>();
        REQUIRE(typed);
        REQUIRE(typed->dictId == "test_dict");
        REQUIRE(typed->keys.size() == 2);
        REQUIRE(typed->objectName() == "DictInputV1");
    }

    SECTION("FakeStartInputA upcast then as<FakeStartInputA> roundtrip") {
        auto fake = NO<FakeStartInputA>::create();
        NO<TaskStartInput> base = fake;
        const auto typed = base.as<FakeStartInputA>();
        REQUIRE(typed);
        REQUIRE(typed->objectName() == "fakeA");
    }
}

// ===========================================================================
// G2P-AS-03: as<>() on type-mismatched non-null NO<> returns non-null
//   (static_pointer_cast semantics)
//   Documents that as<T>() does NOT perform runtime type checking. A non-null
//   NO<> holding type T, when as<U>() is called with an unrelated sibling type
//   U (both derive from the same base), returns a non-null NO<U> pointing to
//   the wrong object. The as<>() call itself does not crash; dereferencing the
//   miscast pointer WOULD be undefined behavior and is intentionally NOT done.
//   This is why the type-safety gate must be an objectName() check BEFORE as<>()
//   (as the inference plugins do in validateStartInput), not the BF-50 null
//   check after as<>().
// ===========================================================================
TEST_CASE("G2P-AS-03: as<>() on type-mismatched non-null NO<> returns non-null",
          "[g2p][extreme][bf-50]") {
    SECTION("G2pInputV1 as<DictInputV1> returns non-null (miscast, no deref)") {
        auto g2pInput = NO<G2pInputV1>::create();
        NO<TaskInput> base = g2pInput;
        const auto miscast = base.as<DictInputV1>();
        // static_pointer_cast returns non-null even though the object is
        // G2pInputV1, not DictInputV1. Dereferencing `miscast` is UB and
        // is intentionally avoided.
        REQUIRE(miscast);
    }

    SECTION("DictInputV1 as<G2pInputV1> returns non-null (miscast, no deref)") {
        auto dictInput = NO<DictInputV1>::create();
        NO<TaskInput> base = dictInput;
        const auto miscast = base.as<G2pInputV1>();
        REQUIRE(miscast);
        // No dereference: would be UB.
    }

    SECTION("FakeStartInputA as<FakeStartInputB> returns non-null (miscast)") {
        auto fakeA = NO<FakeStartInputA>::create();
        NO<TaskStartInput> base = fakeA;
        const auto miscast = base.as<FakeStartInputB>();
        REQUIRE(miscast);
        // No dereference: would be UB.
    }

    SECTION("as<>() call on type-mismatched input does not crash (no deref)") {
        // Repeated calls to as<>() with various mismatched types must not crash
        // at the as<>() step itself. This is the BF-50 regression surface: the
        // null check after as<>() runs without crashing even for mismatched types.
        auto g2pInput = NO<G2pInputV1>::create();
        NO<TaskInput> base = g2pInput;
        const auto a = base.as<DictInputV1>();
        const auto b = base.as<G2pInputV1>(); // correct
        const auto c = base.as<TaskInput>();  // upcast
        REQUIRE(a);
        REQUIRE(b);
        REQUIRE(c);
    }
}

// ===========================================================================
// G2P-AS-04: G2P chain/multig2p BF-50 pattern — null input returns structured error
//   Replicates the exact BF-50 fix pattern from chain/multig2p TaskImpl::start().
//   Null input must be caught by `if (!input)` and return a structured Error
//   (G2pConfigError for chain), not crash. The post-as<>() `if (!typed)` check
//   is defense-in-depth and also catches the null case.
// ===========================================================================
TEST_CASE("G2P-AS-04: G2P chain/multig2p BF-50 pattern - null input returns error",
          "[g2p][extreme][bf-50]") {
    SECTION("null TaskInput -> G2pConfigError (chain pattern)") {
        NO<TaskInput> nullInput;
        auto res = chainStyleStart(nullInput);
        REQUIRE(!res.hasValue());
        REQUIRE(res.errorCode() == ErrorCode::G2pConfigError);
        REQUIRE(res.error().message().find("nullptr") != std::string::npos);
    }

    SECTION("default-constructed NO<TaskInput> is null and rejected") {
        auto res = chainStyleStart(NO<TaskInput>{});
        REQUIRE(!res.hasValue());
        REQUIRE(res.errorCode() == ErrorCode::G2pConfigError);
    }

    SECTION("explicit nullptr NO<TaskInput> is null and rejected") {
        auto res = chainStyleStart(NO<TaskInput>{nullptr});
        REQUIRE(!res.hasValue());
        REQUIRE(res.errorCode() == ErrorCode::G2pConfigError);
    }
}

// ===========================================================================
// G2P-AS-05: G2P chain/multig2p BF-50 pattern — correct type succeeds
//   A correctly-typed G2pInputV1 input passes both the null check and the
//   as<G2pInputV1>() null check, and is safely dereferenceable.
// ===========================================================================
TEST_CASE("G2P-AS-05: G2P chain/multig2p BF-50 pattern - correct type succeeds",
          "[g2p][extreme][bf-50]") {
    SECTION("G2pInputV1 input -> success, valid G2pResultV1") {
        auto g2pInput = NO<G2pInputV1>::create();
        g2pInput->g2pInput = {"a", "b", "c"};
        auto res = chainStyleStart(g2pInput);
        REQUIRE(res.hasValue());
        REQUIRE(res.value());
        // Result is a G2pResultV1 with reserved capacity (no crash on deref).
        const auto result = res.value().as<G2pResultV1>();
        REQUIRE(result);
    }

    SECTION("empty G2pInputV1 input -> success (empty is not null)") {
        auto g2pInput = NO<G2pInputV1>::create(); // empty g2pInput vector
        auto res = chainStyleStart(g2pInput);
        REQUIRE(res.hasValue());
    }
}

// ===========================================================================
// G2P-AS-06: G2P ds-dict BF-50 pattern — null input returns NullPointerError
//   (regression)
//   Replicates the ds-dict TaskImpl::start() pattern. Null input returns
//   G2pNullPointerError (ds-dict uses NullPointerError, unlike chain's
//   ConfigError). Verifies the existing null check does not regress.
// ===========================================================================
TEST_CASE("G2P-AS-06: G2P ds-dict BF-50 pattern - null input returns NullPointerError",
          "[g2p][extreme][bf-50]") {
    SECTION("null TaskInput -> G2pNullPointerError (ds-dict pattern)") {
        NO<TaskInput> nullInput;
        auto res = dsDictStyleStart(nullInput);
        REQUIRE(!res.hasValue());
        REQUIRE(res.errorCode() == ErrorCode::G2pNullPointerError);
        REQUIRE(res.error().message().find("nullptr") != std::string::npos);
    }

    SECTION("ds-dict null check does not regress (defensive as<> also null)") {
        // The BF-50 defensive `if (!dictInput)` after as<>() is only reachable
        // when input is null (caught above). Verify as<>() on null yields null,
        // so the defensive check WOULD trigger if the first check were removed.
        NO<TaskInput> nullInput;
        const auto dictInput = nullInput.as<DictInputV1>();
        REQUIRE(!dictInput);
    }
}

// ===========================================================================
// G2P-AS-07: ds-dict BF-50 pattern — correct DictInputV1 succeeds
//   Verifies ds-dict accepts a correctly-typed DictInputV1 and returns a
//   result without crashing.
// ===========================================================================
TEST_CASE("G2P-AS-07: G2P ds-dict BF-50 pattern - correct DictInputV1 succeeds",
          "[g2p][extreme][bf-50]") {
    auto dictInput = NO<DictInputV1>::create();
    dictInput->dictId = "test";
    dictInput->keys = {"hello"};
    auto res = dsDictStyleStart(dictInput);
    REQUIRE(res.hasValue());
    REQUIRE(res.value());
}

// ===========================================================================
// G2P-AS-08: Inference plugin BF-50 pattern — null input returns InvalidArgument
//   Replicates validateStartInput (PluginCommon.h, BF-46) which cannot be
//   included here. Covers acoustic/vocoder/duration/pitch/variance start()
//   entry: null input -> InvalidArgument, no crash.
// ===========================================================================
TEST_CASE("G2P-AS-08: Inference BF-50 pattern - null input returns InvalidArgument",
          "[g2p][extreme][bf-50]") {
    SECTION("null TaskStartInput -> InvalidArgument (acoustic proxy)") {
        NO<TaskStartInput> nullInput;
        auto res = inferenceStyleValidateStart(nullInput, "acoustic");
        REQUIRE(!res.hasValue());
        REQUIRE(res.errorCode() == ErrorCode::InvalidArgument);
        REQUIRE(res.error().message().find("nullptr") != std::string::npos);
    }

    SECTION("null TaskStartInput -> InvalidArgument (vocoder proxy)") {
        auto res = inferenceStyleValidateStart(NO<TaskStartInput>{}, "vocoder");
        REQUIRE(!res.hasValue());
        REQUIRE(res.errorCode() == ErrorCode::InvalidArgument);
    }

    SECTION("null TaskInitArgs -> InvalidArgument (initialize entry)") {
        NO<TaskInitArgs> nullArgs;
        auto res = inferenceStyleValidateInitArgs(nullArgs, "acoustic");
        REQUIRE(!res.hasValue());
        REQUIRE(res.errorCode() == ErrorCode::InvalidArgument);
    }

    SECTION("null TaskInitArgs -> InvalidArgument (duration proxy)") {
        auto res = inferenceStyleValidateInitArgs(NO<TaskInitArgs>{}, "duration");
        REQUIRE(!res.hasValue());
        REQUIRE(res.errorCode() == ErrorCode::InvalidArgument);
    }
}

// ===========================================================================
// G2P-AS-09: Inference plugin BF-50 pattern — type-mismatch via objectName gate
//   returns InvalidArgument
//   The inference plugins' type-safety gate is the objectName() check in
//   validateStartInput/validateInitArgs (PluginCommon.h), which runs BEFORE
//   as<>(). A type-mismatched non-null input (wrong objectName) is rejected
//   with InvalidArgument, preventing the miscast as<>() dereference.
//   This is the safe pattern that the 5 inference plugins follow.
// ===========================================================================
TEST_CASE("G2P-AS-09: Inference BF-50 pattern - type-mismatch via objectName gate",
          "[g2p][extreme][bf-50]") {
    SECTION("FakeStartInputA with apiName 'acoustic' -> InvalidArgument") {
        auto fake = NO<FakeStartInputA>::create(); // objectName == "fakeA"
        auto res = inferenceStyleValidateStart(fake, "acoustic");
        REQUIRE(!res.hasValue());
        REQUIRE(res.errorCode() == ErrorCode::InvalidArgument);
        REQUIRE(res.error().message().find("invalid input name") != std::string::npos);
    }

    SECTION("FakeStartInputB with apiName 'vocoder' -> InvalidArgument") {
        auto fake = NO<FakeStartInputB>::create();
        auto res = inferenceStyleValidateStart(fake, "vocoder");
        REQUIRE(!res.hasValue());
        REQUIRE(res.errorCode() == ErrorCode::InvalidArgument);
    }

    SECTION("FakeStartInputA with apiName 'fakeB' -> InvalidArgument") {
        auto fake = NO<FakeStartInputA>::create();
        auto res = inferenceStyleValidateStart(fake, "fakeB");
        REQUIRE(!res.hasValue());
        REQUIRE(res.errorCode() == ErrorCode::InvalidArgument);
    }

    SECTION("FakeInitArgsA with apiName 'fakeInitB' -> InvalidArgument") {
        auto fake = NO<FakeInitArgsA>::create();
        auto res = inferenceStyleValidateInitArgs(fake, "fakeInitB");
        REQUIRE(!res.hasValue());
        REQUIRE(res.errorCode() == ErrorCode::InvalidArgument);
    }

    SECTION("G2pInputV1 passed as TaskStartInput with apiName 'acoustic' rejected") {
        // Cross-module type mismatch: a G2P input type would be rejected by an
        // inference plugin's objectName gate. (G2pInputV1 objectName is
        // "G2pInputV1", not "acoustic".)
        auto g2pInput = NO<G2pInputV1>::create();
        auto res = inferenceStyleValidateStart(g2pInput, "acoustic");
        REQUIRE(!res.hasValue());
        REQUIRE(res.errorCode() == ErrorCode::InvalidArgument);
    }
}

// ===========================================================================
// G2P-AS-10: Inference plugin BF-50 pattern — correct type passes validation
//   A correctly-typed input (objectName matches apiName) passes the
//   validateStartInput/validateInitArgs gate. Uses fake types as proxies for
//   the 5 inference plugins' StartInput/InitArgs (not linkable from srt::g2p).
// ===========================================================================
TEST_CASE("G2P-AS-10: Inference BF-50 pattern - correct type passes validation",
          "[g2p][extreme][bf-50]") {
    SECTION("FakeStartInputA with apiName 'fakeA' -> success") {
        auto fake = NO<FakeStartInputA>::create();
        auto res = inferenceStyleValidateStart(fake, "fakeA");
        REQUIRE(res.hasValue());
    }

    SECTION("FakeStartInputB with apiName 'fakeB' -> success") {
        auto fake = NO<FakeStartInputB>::create();
        auto res = inferenceStyleValidateStart(fake, "fakeB");
        REQUIRE(res.hasValue());
    }

    SECTION("FakeInitArgsA with apiName 'fakeInitA' -> success") {
        auto fake = NO<FakeInitArgsA>::create();
        auto res = inferenceStyleValidateInitArgs(fake, "fakeInitA");
        REQUIRE(res.hasValue());
    }

    SECTION("FakeInitArgsB with apiName 'fakeInitB' -> success") {
        auto fake = NO<FakeInitArgsB>::create();
        auto res = inferenceStyleValidateInitArgs(fake, "fakeInitB");
        REQUIRE(res.hasValue());
    }
}

// ===========================================================================
// G2P-AS-11: Cross-level as<>() roundtrip is safe
//   Multi-level upcast/downcast roundtrips must preserve the object identity.
//   NO<Derived> -> as<Base> -> as<NamedObject> -> as<Base> -> as<Derived>
//   must yield a non-null pointer to the original object.
// ===========================================================================
TEST_CASE("G2P-AS-11: Cross-level as<>() roundtrip is safe",
          "[g2p][extreme][bf-50]") {
    SECTION("G2pInputV1 roundtrip through TaskInput and back") {
        auto original = NO<G2pInputV1>::create();
        original->g2pInput = {"x"};
        NO<TaskInput> base = original;
        const auto back = base.as<G2pInputV1>();
        REQUIRE(back);
        REQUIRE(back->g2pInput == std::vector<std::string>{"x"});
    }

    SECTION("G2pInputV1 multi-level roundtrip through NamedObject") {
        auto original = NO<G2pInputV1>::create();
        original->g2pInput = {"y"};
        // Upcast to the root base NamedObject, then back down.
        NO<NamedObject> root = original;
        REQUIRE(root);
        NO<TaskInput> mid = root.as<TaskInput>();
        const auto back = mid.as<G2pInputV1>();
        REQUIRE(back);
        REQUIRE(back->g2pInput == std::vector<std::string>{"y"});
        REQUIRE(back->objectName() == "G2pInputV1");
    }

    SECTION("DictInputV1 roundtrip through NamedObject") {
        auto original = NO<DictInputV1>::create();
        original->dictId = "rid";
        NO<NamedObject> root = original;
        const auto back = root.as<DictInputV1>();
        REQUIRE(back);
        REQUIRE(back->dictId == "rid");
    }

    SECTION("roundtrip of null through cross-level yields null at each step") {
        NO<G2pInputV1> nullInput;
        NO<NamedObject> root = nullInput; // null upcast
        REQUIRE(!root);
        NO<TaskInput> mid = root.as<TaskInput>();
        REQUIRE(!mid);
        const auto back = mid.as<G2pInputV1>();
        REQUIRE(!back);
    }
}

// ===========================================================================
// G2P-AS-12: objectName-based type discrimination is the safe gate before as<>()
//   Documents and verifies the type-safety pattern: check objectName() BEFORE
//   calling as<>() to avoid miscast dereferences. This is the pattern the
//   inference plugins follow (validateStartInput) and the pattern the G2P
//   plugins lack (a known gap; BF-50 only added null checks).
// ===========================================================================
TEST_CASE("G2P-AS-12: objectName-based type discrimination before as<>()",
          "[g2p][extreme][bf-50]") {
    SECTION("G2pInputV1 objectName is 'G2pInputV1'") {
        auto g2pInput = NO<G2pInputV1>::create();
        NO<TaskInput> base = g2pInput;
        REQUIRE(base->objectName() == "G2pInputV1");
        REQUIRE(base->objectName() != "DictInputV1");
    }

    SECTION("DictInputV1 objectName is 'DictInputV1'") {
        auto dictInput = NO<DictInputV1>::create();
        NO<TaskInput> base = dictInput;
        REQUIRE(base->objectName() == "DictInputV1");
        REQUIRE(base->objectName() != "G2pInputV1");
    }

    SECTION("objectName gate rejects type mismatch before as<>() (safe pattern)") {
        // Safe pattern: check objectName first, only call as<>() on match.
        auto g2pInput = NO<G2pInputV1>::create();
        NO<TaskInput> base = g2pInput;
        bool wouldCallAsG2p = (base->objectName() == "G2pInputV1");
        bool wouldCallAsDict = (base->objectName() == "DictInputV1");
        REQUIRE(wouldCallAsG2p);
        REQUIRE(!wouldCallAsDict);
        // Only the matching as<>() is safe to dereference.
        if (wouldCallAsG2p) {
            const auto typed = base.as<G2pInputV1>();
            REQUIRE(typed);
            REQUIRE(typed->g2pInput.empty());
        }
    }

    SECTION("G2P plugins lack objectName gate (documented gap)") {
        // chain/multig2p/ds-dict TaskImpl::start() do NOT check objectName
        // before as<>(); they rely on the caller passing the correct type.
        // BF-50's `if (!typed)` only catches null input, not type mismatch.
        // A type-mismatched non-null input would pass the null check and
        // as<>() (returns non-null), then dereference as the wrong type (UB).
        // This case documents that gap; the safe fix would be to add an
        // objectName check before as<>() (matching the inference plugins).
        auto dictInput = NO<DictInputV1>::create();
        NO<TaskInput> base = dictInput;
        // G2P plugin would call base.as<G2pInputV1>() here without checking
        // objectName first. as<>() returns non-null (static_pointer_cast):
        const auto miscast = base.as<G2pInputV1>();
        REQUIRE(miscast); // non-null but WRONG type — dereferencing is UB.
        // The objectName gate that SHOULD prevent this:
        REQUIRE(base->objectName() == "DictInputV1"); // not "G2pInputV1"
    }
}

// ===========================================================================
// G2P-AS-13: default-constructed NO<> and explicit nullptr NO<> are both null
//   Verifies the two ways to construct a null NO<> behave identically under
//   as<>(). BF-50's null check must catch both forms.
// ===========================================================================
TEST_CASE("G2P-AS-13: default-constructed and explicit nullptr NO<> both null",
          "[g2p][extreme][bf-50]") {
    SECTION("default NO<TaskInput> equals explicit nullptr NO<TaskInput>") {
        NO<TaskInput> defaultConstructed;
        NO<TaskInput> explicitNull{nullptr};
        REQUIRE(!defaultConstructed);
        REQUIRE(!explicitNull);
    }

    SECTION("both null forms yield null as<G2pInputV1>()") {
        NO<TaskInput> defaultConstructed;
        NO<TaskInput> explicitNull{nullptr};
        REQUIRE(!defaultConstructed.as<G2pInputV1>());
        REQUIRE(!explicitNull.as<G2pInputV1>());
    }

    SECTION("both null forms yield null as<DictInputV1>()") {
        NO<TaskInput> defaultConstructed;
        NO<TaskInput> explicitNull{nullptr};
        REQUIRE(!defaultConstructed.as<DictInputV1>());
        REQUIRE(!explicitNull.as<DictInputV1>());
    }

    SECTION("BF-50 null check catches both null forms (chain pattern)") {
        NO<TaskInput> defaultConstructed;
        NO<TaskInput> explicitNull{nullptr};
        auto res1 = chainStyleStart(defaultConstructed);
        auto res2 = chainStyleStart(explicitNull);
        REQUIRE(!res1.hasValue());
        REQUIRE(!res2.hasValue());
        REQUIRE(res1.errorCode() == ErrorCode::G2pConfigError);
        REQUIRE(res2.errorCode() == ErrorCode::G2pConfigError);
    }
}

// ===========================================================================
// G2P-AS-14: reinterpret_cast to NO<> is NOT a supported pattern (documented)
//   Random-memory reinterpret_cast to NO<> is undefined behavior and is NOT
//   tested with an actual cast (would risk crashing the test process). This
//   case documents that the only supported way to obtain a NO<> is via
//   NO<T>::create() or shared_ptr conversion; the safe type-navigation pattern
//   is validate-then-as (objectName check before as<>()).
// ===========================================================================
TEST_CASE("G2P-AS-14: reinterpret_cast to NO<> is unsupported (documented)",
          "[g2p][extreme][bf-50]") {
    // reinterpret_cast of random memory to NO<> would violate the shared_ptr
    // invariants (no control block, wrong layout) and is UB. The supported
    // patterns are:
    //   1. NO<T>::create()           — construct a new owned object
    //   2. shared_ptr conversion     — upcast NO<Derived> to NO<Base>
    //   3. as<T>()                   — static_pointer_cast on an existing NO<>
    // Type navigation MUST be gated by objectName() check to avoid miscast.
    SECTION("supported: NO<T>::create() yields a valid non-null owned object") {
        auto obj = NO<G2pInputV1>::create();
        REQUIRE(obj);
        REQUIRE(obj->objectName() == "G2pInputV1");
    }

    SECTION("supported: shared_ptr upcast NO<Derived> -> NO<Base>") {
        NO<G2pInputV1> derived = NO<G2pInputV1>::create();
        NO<TaskInput> base = derived; // implicit upcast
        REQUIRE(base);
        REQUIRE(base->objectName() == "G2pInputV1");
    }

    SECTION("supported: as<T>() static_pointer_cast on existing NO<>") {
        NO<G2pInputV1> derived = NO<G2pInputV1>::create();
        NO<TaskInput> base = derived;
        const auto back = base.as<G2pInputV1>();
        REQUIRE(back);
    }

    SECTION("unsupported: reinterpret_cast would be UB (not executed)") {
        // The following pattern is FORBIDDEN and is NOT executed here:
        //   char buf[64] = {};
        //   auto bad = *reinterpret_cast<NO<G2pInputV1>*>(buf);
        //   bad->g2pInput; // UB: no valid control block, no valid object
        // The safe alternative is to construct a real object and gate as<>()
        // with an objectName() check.
        char buf[64] = {0};
        (void)buf; // mark as intentionally unused
        // Verify the safe pattern works instead:
        auto obj = NO<G2pInputV1>::create();
        REQUIRE(obj->objectName() == "G2pInputV1");
    }
}
