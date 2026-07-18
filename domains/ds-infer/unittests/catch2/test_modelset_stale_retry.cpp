// Stale ModelSet 重建+重试模式回归测试。
//
// 覆盖范围：
//   - markStale() 后 load/start 返回 StaleModelSet
//   - markStale() 后 isStale() 返回 true
//   - markStale() 是单向的：stale 标志无法被清除，必须重建 ModelSet
//   - markStale() 后 stop/unload/reset 仍可执行（清理资源）
//   - markStale() 后 result() 仍可读取（查询历史结果）
//   - markStale() 后 model() 仍可读取（兼容性逃生口）
//   - markStale() 后 unloadAll() 仍可执行
//   - markStale() 后 isLoaded() 仍反映当前加载状态
//   - 重建 ModelSet 后新实例的 isStale()=false，可正常 load/start
//
// 这些用例反映 ds-editor-lite SynthrtEngine::acquireSingerSession 在
// SingerModelSession 析构（unloadAll）+ 重新 acquire 时的实际调用模式。
// 当 voice bank package 重新加载时，旧 ModelSet 必须 markStale，新请求
// 必须创建新 ModelSet 实例（不能复用旧实例）。

#include <catch2/catch_test_macros.hpp>

#include <diffsinger/Infer/ModelSet.h>
#include <diffsinger/Infer/StageKind.h>
#include <synthrt/Core/Support/Diagnostic.h>

using namespace ds::infer;
using srt::core::ErrorCode;

namespace {
    StageSet makeEmptyStageSet() {
        StageSet stages;
        return stages;
    }
}

// ---------------------------------------------------------------------------
// markStale 基本行为
// ---------------------------------------------------------------------------

TEST_CASE("ModelSet markStale sets isStale to true", "[modelset][stale]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    REQUIRE(!modelSet.isStale());
    modelSet.markStale();
    REQUIRE(modelSet.isStale());
}

TEST_CASE("ModelSet markStale is idempotent", "[modelset][stale]") {
    // 多次 markStale 不应有副作用（atomic store 仍为 true）
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    modelSet.markStale();
    modelSet.markStale();
    modelSet.markStale();
    REQUIRE(modelSet.isStale());
}

// ---------------------------------------------------------------------------
// markStale 后 load/start 被拒绝
// ---------------------------------------------------------------------------

TEST_CASE("ModelSet stale rejects load with StaleModelSet", "[modelset][stale]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    modelSet.markStale();
    auto exp = modelSet.load(StageKind::Duration);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::StaleModelSet));
    REQUIRE(exp.error().message().find("stale") != std::string::npos);
}

TEST_CASE("ModelSet stale rejects start with StaleModelSet", "[modelset][stale]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    modelSet.markStale();
    auto exp = modelSet.start(StageKind::Duration, {});
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::StaleModelSet));
}

TEST_CASE("ModelSet stale rejects load for all stages", "[modelset][stale]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    modelSet.markStale();
    for (auto kind : {StageKind::Duration, StageKind::Pitch, StageKind::Variance,
                      StageKind::Acoustic, StageKind::Vocoder}) {
        auto exp = modelSet.load(kind);
        REQUIRE(!exp.hasValue());
        REQUIRE(exp.isError(ErrorCode::StaleModelSet));
    }
}

TEST_CASE("ModelSet stale rejects start for all stages", "[modelset][stale]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    modelSet.markStale();
    for (auto kind : {StageKind::Duration, StageKind::Pitch, StageKind::Variance,
                      StageKind::Acoustic, StageKind::Vocoder}) {
        auto exp = modelSet.start(kind, {});
        REQUIRE(!exp.hasValue());
        REQUIRE(exp.isError(ErrorCode::StaleModelSet));
    }
}

// ---------------------------------------------------------------------------
// markStale 是单向的：stale 标志无法清除
// ---------------------------------------------------------------------------

TEST_CASE("ModelSet stale flag cannot be cleared via public API", "[modelset][stale]") {
    // markStale 是单向的：一旦标记 stale，必须重建 ModelSet。
    // 公共 API 没有清除 stale 的方法，所以 isStale() 永远返回 true。
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    modelSet.markStale();
    // 尝试通过 unload/reset/stop 等操作"重置"状态
    REQUIRE(modelSet.unload(StageKind::Duration).hasValue());
    REQUIRE(modelSet.stop(StageKind::Duration).hasValue());
    REQUIRE(modelSet.reset(StageKind::Duration).hasValue());
    REQUIRE(modelSet.unloadAll().hasValue());
    // stale 标志仍为 true
    REQUIRE(modelSet.isStale());
    // 仍然拒绝 load
    REQUIRE(modelSet.load(StageKind::Duration).isError(ErrorCode::StaleModelSet));
}

// ---------------------------------------------------------------------------
// markStale 后 stop/unload/reset 仍可执行（清理资源）
// ---------------------------------------------------------------------------

TEST_CASE("ModelSet stale allows stop on never-loaded stage", "[modelset][stale]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    modelSet.markStale();
    // stop on never-loaded stage 应仍成功（no-op）
    REQUIRE(modelSet.stop(StageKind::Duration).hasValue());
    REQUIRE(modelSet.stop(StageKind::Vocoder).hasValue());
}

TEST_CASE("ModelSet stale allows unload on never-loaded stage", "[modelset][stale]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    modelSet.markStale();
    REQUIRE(modelSet.unload(StageKind::Duration).hasValue());
    REQUIRE(modelSet.unload(StageKind::Vocoder).hasValue());
}

TEST_CASE("ModelSet stale allows reset on never-loaded stage", "[modelset][stale]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    modelSet.markStale();
    REQUIRE(modelSet.reset(StageKind::Duration).hasValue());
    REQUIRE(modelSet.reset(StageKind::Vocoder).hasValue());
}

TEST_CASE("ModelSet stale allows unloadAll", "[modelset][stale]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    modelSet.markStale();
    // unloadAll 应仍可执行（清理资源）
    REQUIRE(modelSet.unloadAll().hasValue());
    // 多次 unloadAll 也应成功
    REQUIRE(modelSet.unloadAll().hasValue());
}

// ---------------------------------------------------------------------------
// markStale 后查询 API 仍可读取
// ---------------------------------------------------------------------------

TEST_CASE("ModelSet stale allows result query", "[modelset][stale]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    modelSet.markStale();
    // result() 查询应仍可读取（返回空 NO，因为从未 start 过）
    REQUIRE(!modelSet.result(StageKind::Duration));
    REQUIRE(!modelSet.result(StageKind::Vocoder));
}

TEST_CASE("ModelSet stale allows model query", "[modelset][stale]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    modelSet.markStale();
    // model() 兼容性逃生口应仍可读取（返回空 NO，因为从未 load 过）
    REQUIRE(!modelSet.model(StageKind::Duration));
    REQUIRE(!modelSet.model(StageKind::Vocoder));
}

TEST_CASE("ModelSet stale allows isLoaded query", "[modelset][stale]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    modelSet.markStale();
    REQUIRE(!modelSet.isLoaded(StageKind::Duration));
    REQUIRE(!modelSet.isLoaded(StageKind::Pitch));
    REQUIRE(!modelSet.isLoaded(StageKind::Variance));
    REQUIRE(!modelSet.isLoaded(StageKind::Acoustic));
    REQUIRE(!modelSet.isLoaded(StageKind::Vocoder));
}

TEST_CASE("ModelSet stale allows stages query", "[modelset][stale]") {
    auto stages = makeEmptyStageSet();
    stages.duration.kind = StageKind::Duration;
    ModelSet modelSet(std::move(stages));

    modelSet.markStale();
    const auto &stored = modelSet.stages();
    REQUIRE(stored.duration.kind == StageKind::Duration);
}

// ---------------------------------------------------------------------------
// stale 错误包含 moduleId 上下文
// ---------------------------------------------------------------------------

TEST_CASE("ModelSet stale load error has moduleId context", "[modelset][stale][trace]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    modelSet.markStale();
    auto exp = modelSet.load(StageKind::Duration);
    REQUIRE(!exp.hasValue());
    const auto &diag = exp.error().diagnostic();
    // stale 错误应携带 stage name 作为 moduleId
    REQUIRE(diag.moduleId == "duration");
}

TEST_CASE("ModelSet stale start error has moduleId context per stage",
          "[modelset][stale][trace]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    modelSet.markStale();
    for (auto kind : {StageKind::Duration, StageKind::Pitch, StageKind::Variance,
                      StageKind::Acoustic, StageKind::Vocoder}) {
        auto exp = modelSet.start(kind, {});
        REQUIRE(!exp.hasValue());
        REQUIRE(!exp.error().diagnostic().moduleId.empty());
    }
}

// ---------------------------------------------------------------------------
// 重建 ModelSet 模式：旧 markStale 后创建新实例
// ---------------------------------------------------------------------------

TEST_CASE("ModelSet rebuild after stale creates fresh instance", "[modelset][stale][rebuild]") {
    // 模拟 ds-editor-lite acquireSingerSession 的重建模式：
    // 1. 旧 ModelSet markStale
    // 2. 析构旧 ModelSet（自动 unloadAll）
    // 3. 创建新 ModelSet，新实例 isStale()=false
    auto oldStages = makeEmptyStageSet();
    ModelSet oldModelSet(std::move(oldStages));
    oldModelSet.markStale();
    REQUIRE(oldModelSet.isStale());

    // 析构旧实例（实际通过 unique_ptr 释放）
    // 这里通过移动语义模拟"释放"
    {
        auto newStages = makeEmptyStageSet();
        ModelSet newModelSet(std::move(newStages));
        // 新实例的 isStale 必须为 false
        REQUIRE(!newModelSet.isStale());
        // 新实例的 load 应返回 null spec 错误（不是 stale 错误）
        auto exp = newModelSet.load(StageKind::Duration);
        REQUIRE(!exp.hasValue());
        REQUIRE(exp.isError(ErrorCode::InferenceInputInvalid));
        REQUIRE(exp.error().message().find("null") != std::string::npos);
    }
}

TEST_CASE("ModelSet rebuild after stale allows normal lifecycle",
          "[modelset][stale][rebuild]") {
    // 重建后的新实例应支持完整的生命周期（在空 stages 范围内）
    auto oldStages = makeEmptyStageSet();
    ModelSet oldModelSet(std::move(oldStages));
    oldModelSet.markStale();

    // 创建新实例
    auto newStages = makeEmptyStageSet();
    ModelSet newModelSet(std::move(newStages));

    REQUIRE(!newModelSet.isStale());
    // 新实例的 stop/unload/reset/unloadAll 应正常执行
    REQUIRE(newModelSet.stop(StageKind::Duration).hasValue());
    REQUIRE(newModelSet.unload(StageKind::Duration).hasValue());
    REQUIRE(newModelSet.reset(StageKind::Duration).hasValue());
    REQUIRE(newModelSet.unloadAll().hasValue());
    // 新实例 markStale 后变为 stale
    newModelSet.markStale();
    REQUIRE(newModelSet.isStale());
}

// ---------------------------------------------------------------------------
// markStale 在 unloadAll 之后调用仍生效
// ---------------------------------------------------------------------------

TEST_CASE("ModelSet markStale after unloadAll still rejects future load",
          "[modelset][stale]") {
    // 即使先 unloadAll，再 markStale，stale 标志仍应拒绝未来 load
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    REQUIRE(modelSet.unloadAll().hasValue());
    modelSet.markStale();
    REQUIRE(modelSet.isStale());
    REQUIRE(modelSet.load(StageKind::Duration).isError(ErrorCode::StaleModelSet));
}

// ---------------------------------------------------------------------------
// 多次 markStale + 多次 load/start 都返回 stale 错误
// ---------------------------------------------------------------------------

TEST_CASE("ModelSet repeated load after stale always returns StaleModelSet",
          "[modelset][stale]") {
    auto stages = makeEmptyStageSet();
    ModelSet modelSet(std::move(stages));

    modelSet.markStale();
    // 第一次 load 返回 stale
    auto exp1 = modelSet.load(StageKind::Duration);
    REQUIRE(exp1.isError(ErrorCode::StaleModelSet));
    // 第二次 load 仍返回 stale（不是 InferenceInputInvalid）
    auto exp2 = modelSet.load(StageKind::Duration);
    REQUIRE(exp2.isError(ErrorCode::StaleModelSet));
    // 第三次 load 仍返回 stale
    auto exp3 = modelSet.load(StageKind::Duration);
    REQUIRE(exp3.isError(ErrorCode::StaleModelSet));
}
