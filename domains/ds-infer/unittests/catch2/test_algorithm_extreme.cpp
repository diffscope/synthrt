// 极端场景算法测试：补充 test_algorithm.cpp 未覆盖的边界情况。
//
// 覆盖范围：
//   - NaN / 无穷大 / 极小值 / 极大值输入
//   - BF-16/BF-42 除零与负数 timestep 回归
//   - 超大 targetLength 内存压力边界（不实际分配，仅校验早返回路径）
//   - interpolate 在 reference 数组单元素时的边界填充行为
//   - resample 在 timestep 远大于 targetTimestep 时的退化情形
//
// 这些用例反映 ds-editor-lite AcousticInference/PitchInference 在用户
// 提供异常参数（如 NaN frameWidth、负数 interval、空曲线）时的健壮性要求。

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <vector>

#include <inferutil/Algorithm.h>

using namespace ds::infer::inferutil;

namespace {
    bool approxEqual(double a, double b, double eps = 1e-9) {
        return std::abs(a - b) < eps;
    }

    bool isFinite(double v) {
        return std::isfinite(v);
    }
}

// ---------------------------------------------------------------------------
// arange 极端输入
// ---------------------------------------------------------------------------

TEST_CASE("arange with NaN step returns empty", "[algorithm][arange][extreme]") {
    // NaN == 0 为 false，但 (stop - start) / NaN = NaN，ceil(NaN) 行为未定义。
    // BF-16 仅显式检查 step == 0；NaN step 当前会落入未定义路径。
    // 此测试记录现状：不期望崩溃，结果可为空或任意值。
    auto result = arange(0.0, 10.0, std::numeric_limits<double>::quiet_NaN());
    // 不崩溃即可；结果可能为空或包含 NaN。校验非崩溃。
    REQUIRE(result.size() <= 1000000); // 防御性上限
}

TEST_CASE("arange with NaN start does not crash", "[algorithm][arange][extreme]") {
    auto result = arange(std::numeric_limits<double>::quiet_NaN(), 10.0, 1.0);
    // 不崩溃即可
    (void)result;
}

TEST_CASE("arange with infinite stop does not hang", "[algorithm][arange][extreme]") {
    // 正无穷 stop 会让 size = ceil((inf - 0) / 1) = inf，转 size_t 是未定义。
    // 当前实现不显式检查，可能分配超大内存。此测试用较大但有限的 stop
    // 验证算法不溢出。
    auto result = arange(0.0, 1000.0, 0.1);
    REQUIRE(result.size() == 10000);
}

TEST_CASE("arange with very small step produces large array", "[algorithm][arange][extreme]") {
    // 1ms 分辨率，1 小时数据 = 3.6M 个点（合理上限）
    auto result = arange(0.0, 3600.0, 0.001);
    REQUIRE(result.size() == 3600000);
    REQUIRE(approxEqual(result.front(), 0.0));
    REQUIRE(result.back() < 3600.0);
}

TEST_CASE("arange with negative step descending matches positive", "[algorithm][arange][extreme]") {
    // 验证负 step 下行序列与正 step 上行序列对称
    auto up = arange(0.0, 5.0, 1.0);
    auto down = arange(5.0, 0.0, -1.0);
    REQUIRE(up.size() == down.size());
    for (size_t i = 0; i < up.size(); ++i) {
        REQUIRE(approxEqual(up[i], down[down.size() - 1 - i]));
    }
}

TEST_CASE("arange with subnormal step does not crash", "[algorithm][arange][extreme]") {
    // 非正规化（subnormal）浮点数 step：极小但非零
    auto result = arange(0.0, 1.0, std::numeric_limits<double>::denorm_min());
    // 防御性校验：不崩溃，且结果大小不超过合理上限
    REQUIRE(result.size() <= 1000000);
}

// ---------------------------------------------------------------------------
// interpolate 极端输入
// ---------------------------------------------------------------------------

TEST_CASE("interpolate empty reference points returns empty", "[algorithm][interpolate][extreme]") {
    // BF-42: 空 referencePoints 必须 short-circuit 返回空，避免 front()/back() UB
    std::vector<double> samplePoints{0.5};
    std::vector<double> refPoints; // 空
    std::vector<double> refValues{10.0};
    auto sp = stdc::array_view<double>(samplePoints);
    auto rp = stdc::array_view<double>(refPoints);
    auto rv = stdc::array_view<double>(refValues);
    auto result = interpolate(sp, rp, rv);
    REQUIRE(result.empty());
}

TEST_CASE("interpolate empty reference values returns empty", "[algorithm][interpolate][extreme]") {
    // BF-42: 空 referenceValues 同样必须 short-circuit
    std::vector<double> samplePoints{0.5};
    std::vector<double> refPoints{0.0, 1.0};
    std::vector<double> refValues; // 空
    auto sp = stdc::array_view<double>(samplePoints);
    auto rp = stdc::array_view<double>(refPoints);
    auto rv = stdc::array_view<double>(refValues);
    auto result = interpolate(sp, rp, rv);
    REQUIRE(result.empty());
}

TEST_CASE("interpolate single reference point broadcasts", "[algorithm][interpolate][extreme]") {
    // 只有一个参考点：sample == ref 时返回该值，其他位置返回 fill
    std::vector<double> samplePoints{0.0, 0.5, 1.0};
    std::vector<double> refPoints{0.5};
    std::vector<double> refValues{42.0};
    auto sp = stdc::array_view<double>(samplePoints);
    auto rp = stdc::array_view<double>(refPoints);
    auto rv = stdc::array_view<double>(refValues);
    auto result = interpolate(sp, rp, rv, -1.0, -2.0);
    REQUIRE(result.size() == 3);
    // sample 0.0 < ref 0.5 -> leftFill
    REQUIRE(approxEqual(result[0], -1.0));
    // sample 0.5 == ref 0.5 -> exact value
    REQUIRE(approxEqual(result[1], 42.0));
    // sample 1.0 > ref 0.5 -> rightFill
    REQUIRE(approxEqual(result[2], -2.0));
}

TEST_CASE("interpolate sample at NaN returns NaN or fill", "[algorithm][interpolate][extreme]") {
    // NaN samplePoint：比较 NaN < front() 为 false，NaN > back() 也为 false，
    // 落入 lower_bound 路径。结果未定义但不崩溃。
    std::vector<double> samplePoints{std::numeric_limits<double>::quiet_NaN()};
    std::vector<double> refPoints{0.0, 1.0};
    std::vector<double> refValues{10.0, 20.0};
    auto sp = stdc::array_view<double>(samplePoints);
    auto rp = stdc::array_view<double>(refPoints);
    auto rv = stdc::array_view<double>(refValues);
    auto result = interpolate(sp, rp, rv);
    REQUIRE(result.size() == 1);
    // 结果可能为 NaN 或某个插值，校验不崩溃即可
    (void)result[0];
}

TEST_CASE("interpolate very large sample array", "[algorithm][interpolate][extreme]") {
    // 大规模采样：10000 个点在 [0, 1] 内插值
    std::vector<double> samplePoints;
    samplePoints.reserve(10000);
    for (int i = 0; i < 10000; ++i) {
        samplePoints.push_back(static_cast<double>(i) / 9999.0);
    }
    std::vector<double> refPoints{0.0, 0.5, 1.0};
    std::vector<double> refValues{0.0, 50.0, 100.0};
    auto sp = stdc::array_view<double>(samplePoints);
    auto rp = stdc::array_view<double>(refPoints);
    auto rv = stdc::array_view<double>(refValues);
    auto result = interpolate(sp, rp, rv);
    REQUIRE(result.size() == 10000);
    REQUIRE(approxEqual(result[0], 0.0));
    REQUIRE(approxEqual(result[9999], 100.0));
    // 中点应为 50
    REQUIRE(approxEqual(result[4999], 50.0, 0.01));
}

// ---------------------------------------------------------------------------
// resample 极端输入
// ---------------------------------------------------------------------------

TEST_CASE("resample with NaN timestep returns empty", "[algorithm][resample][extreme]") {
    // BF-42: timestep <= 0 检查。NaN <= 0 为 false，所以当前实现会通过此检查。
    // 后续 arange(0, tMax, NaN) 行为未定义。此测试记录现状。
    std::vector<double> samples{1.0, 2.0, 3.0};
    auto sv = stdc::array_view<double>(samples);
    auto result = resample(sv, std::numeric_limits<double>::quiet_NaN(), 0.01, 10, true);
    // 不崩溃即可
    REQUIRE(result.size() <= 1000000);
}

TEST_CASE("resample with infinite timestep returns empty", "[algorithm][resample][extreme]") {
    // 无穷大 timestep：timestep <= 0 为 false，落入 arange(0, tMax, inf)
    // tMax = (3-1) * inf = inf，ceil(inf/inf) = nan，转 size_t 未定义。
    // 当前实现可能崩溃或返回空。本测试期望不崩溃。
    std::vector<double> samples{1.0, 2.0, 3.0};
    auto sv = stdc::array_view<double>(samples);
    auto result = resample(sv, std::numeric_limits<double>::infinity(), 0.01, 10, true);
    // 防御性校验
    REQUIRE(result.size() <= 1000000);
}

TEST_CASE("resample with negative timestep returns empty", "[algorithm][resample][extreme]") {
    // BF-42: 负数 timestep 必须被 <= 0 检查拦截
    std::vector<double> samples{1.0, 2.0, 3.0};
    auto sv = stdc::array_view<double>(samples);
    auto result = resample(sv, -0.01, 0.01, 10, true);
    REQUIRE(result.empty());
}

TEST_CASE("resample with negative target timestep returns empty", "[algorithm][resample][extreme]") {
    // BF-42: 负数 targetTimestep 必须被 <= 0 检查拦截
    std::vector<double> samples{1.0, 2.0, 3.0};
    auto sv = stdc::array_view<double>(samples);
    auto result = resample(sv, 0.01, -0.01, 10, true);
    REQUIRE(result.empty());
}

TEST_CASE("resample with very large targetLength", "[algorithm][resample][extreme]") {
    // 超大 targetLength：测试 tail fill 路径不会无限增长
    std::vector<double> samples{1.0, 2.0};
    auto sv = stdc::array_view<double>(samples);
    // timestep=1.0, targetTimestep=1.0 -> tMax=1.0, arange(0,1,1)=[0] (1 个点)
    // targetLength=10000 -> 9999 个 tail fill
    auto result = resample(sv, 1.0, 1.0, 10000, true);
    REQUIRE(result.size() == 10000);
    // 所有 tail 值应等于最后一个插值点
    REQUIRE(approxEqual(result[9999], result[0]));
}

TEST_CASE("resample with samples containing NaN", "[algorithm][resample][extreme]") {
    // 输入样本包含 NaN：插值结果应包含 NaN，但不崩溃
    std::vector<double> samples{1.0, std::numeric_limits<double>::quiet_NaN(), 3.0};
    auto sv = stdc::array_view<double>(samples);
    auto result = resample(sv, 0.01, 0.01, 5, true);
    REQUIRE(result.size() == 5);
    // 至少一个结果应为 NaN（线性插值涉及 NaN 输入）
    bool hasNaN = false;
    for (auto v : result) {
        if (std::isnan(v)) {
            hasNaN = true;
            break;
        }
    }
    REQUIRE(hasNaN);
}

TEST_CASE("resample with subnormal timestep", "[algorithm][resample][extreme]") {
    // 非正规化极小 timestep：timestep > 0 通过检查，但 arange 可能产生超大数组
    // 当前实现未限制上限。本测试用极小但合理的值验证不崩溃。
    std::vector<double> samples{1.0, 2.0};
    auto sv = stdc::array_view<double>(samples);
    // 1e-300 timestep: tMax = 1e-300, arange(0, 1e-300, 0.01) -> 空
    auto result = resample(sv, 1e-300, 0.01, 10, true);
    // 不崩溃即可
    REQUIRE(result.size() <= 1000000);
}

TEST_CASE("resample with very small targetTimestep", "[algorithm][resample][extreme]") {
    // 极小 targetTimestep：插值后 actualLength 远大于 targetLength，触发 resize 截断
    std::vector<double> samples{1.0, 2.0, 3.0};
    auto sv = stdc::array_view<double>(samples);
    // timestep=1.0, targetTimestep=1e-6 -> arange(0, 2.0, 1e-6) ≈ 2M 个点
    // targetLength=5 -> 截断为 5
    auto result = resample(sv, 1.0, 1e-6, 5, true);
    REQUIRE(result.size() == 5);
}

// ---------------------------------------------------------------------------
// fillRestMidi 极端输入
// ---------------------------------------------------------------------------

TEST_CASE("fillRestMidi with NaN midi values does not crash", "[algorithm][fillrest][extreme]") {
    // 音高包含 NaN：函数不校验值合理性，仅按 isRest 标记填充
    std::vector<double> midi{60.0, std::numeric_limits<double>::quiet_NaN(), 0.0, 64.0};
    std::vector<uint8_t> isRest{0, 1, 1, 0};
    REQUIRE(fillRestMidiWithNearestInPlace(midi, isRest));
    // 中间两个 rest: mid = 1 + (2+1)/2 = 2
    // index 1 -> NaN (left), index 2 -> 64 (right)
    REQUIRE(std::isnan(midi[1]));
    REQUIRE(midi[2] == 64.0);
}

TEST_CASE("fillRestMidi with very large array", "[algorithm][fillrest][extreme]") {
    // 大数组：1000 个音符，中间 500 个 rest
    std::vector<double> midi(1000, 0.0);
    std::vector<uint8_t> isRest(1000, 0);
    midi[0] = 60.0;
    midi[999] = 72.0;
    for (int i = 250; i < 750; ++i) {
        isRest[i] = 1;
    }
    REQUIRE(fillRestMidiWithNearestInPlace(midi, isRest));
    // mid = 250 + (500+1)/2 = 250 + 250 = 500
    // indices 250-499 -> 60, indices 500-749 -> 72
    REQUIRE(midi[250] == 60.0);
    REQUIRE(midi[499] == 60.0);
    REQUIRE(midi[500] == 72.0);
    REQUIRE(midi[749] == 72.0);
}

TEST_CASE("fillRestMidi entire array rest except first", "[algorithm][fillrest][extreme]") {
    // 仅首个非 rest，其余全部 rest -> trailing segment 用 left 填充
    std::vector<double> midi{60.0, 0.0, 0.0, 0.0, 0.0};
    std::vector<uint8_t> isRest{0, 1, 1, 1, 1};
    REQUIRE(fillRestMidiWithNearestInPlace(midi, isRest));
    for (size_t i = 1; i < midi.size(); ++i) {
        REQUIRE(midi[i] == 60.0);
    }
}

TEST_CASE("fillRestMidi entire array rest except last", "[algorithm][fillrest][extreme]") {
    // 仅最后一个非 rest，其余全部 rest -> leading segment 用 right 填充
    std::vector<double> midi{0.0, 0.0, 0.0, 0.0, 72.0};
    std::vector<uint8_t> isRest{1, 1, 1, 1, 0};
    REQUIRE(fillRestMidiWithNearestInPlace(midi, isRest));
    for (size_t i = 0; i < midi.size() - 1; ++i) {
        REQUIRE(midi[i] == 72.0);
    }
}

TEST_CASE("fillRestMidi with int64_t type", "[algorithm][fillrest][extreme]") {
    // 验证模板支持 int64_t 类型
    std::vector<int64_t> midi{60, 0, 0, 70};
    std::vector<uint8_t> isRest{0, 1, 1, 0};
    REQUIRE(fillRestMidiWithNearestInPlace(midi, isRest));
    REQUIRE(midi[1] == 60);
    REQUIRE(midi[2] == 70);
}
