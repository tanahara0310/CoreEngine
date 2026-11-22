#include "pch.h"
#include "WaterSeaMapBuilder.h"

#include <DirectXPackedVector.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>

namespace CoreEngine
{
    namespace {
        /// @brief 重力加速度 [m/s²]
        constexpr float kGravity = 9.81f;
        /// @brief 沖とみなす水深 [m]（WaterShoreRunup.hlsli の kShoreWaveMaxDepth と同じ）
        constexpr float kOpenSeaDepth = 12.0f;
        /// @brief 沖が無いときに起点にする海の水深の下限の、海のいちばん深い所の水深に対する割合
        constexpr float kDeepestSeaSeedRatio = 0.9f;
        /// @brief 沖から着くまでの時間の場を進める速さ √(g·水深) の水深の下限と上限 [m]
        constexpr float kTravelMinDepth = 0.5f;
        constexpr float kTravelMaxDepth = 20.0f;
        /// @brief 峰に沿った位置をならす箱の半径 [m]（2 回かける）
        constexpr float kShoreCrestBlurRadius = 4.0f;

        /// @brief 浅い水の波の速さ [m/s]
        float TravelSpeed(float depth)
        {
            return std::sqrt(kGravity * std::clamp(std::abs(depth), kTravelMinDepth, kTravelMaxDepth));
        }

        /// @brief 岸へ寄せる波の、沖から着くまでの時間の場
        struct ShoreWaveField {
            std::vector<float> seconds; ///< 沖から着くまでの時間 [s]
            std::vector<float> crest;   ///< その時間を運んできた沖の点の、峰に沿った位置 [m]
            bool hasOpenSea = false;    ///< 沖（水深 kOpenSeaDepth 以上の海）のテクセルがあるか
        };

        /// @brief 起点のテクセルのうち、起点より浅い海と隣り合うもの（無ければ起点すべて）の並ぶ主な向き（XZ の単位ベクトル）
        /// @details 位置のばらつきの 2×2 の行列の、いちばん大きい固有値の向き。決まらなければ X 軸
        /// @param seedDepth 起点にした海の水深の下限 [m]
        void ComputeCrestAxis(
            const std::vector<uint32_t>& seeds, const std::vector<float>& depth, const std::vector<uint8_t>& isSea,
            uint32_t width, uint32_t height, float seedDepth, float& axisX, float& axisZ)
        {
            auto isEdge = [&](uint32_t index) {
                const uint32_t x = index % width;
                const uint32_t z = index / width;
                auto shallow = [&](size_t n) { return isSea[n] != 0 && depth[n] < seedDepth; };
                return (x > 0 && shallow(index - 1)) || (x + 1 < width && shallow(index + 1))
                    || (z > 0 && shallow(index - width)) || (z + 1 < height && shallow(index + width));
            };
            double count = 0.0;
            double sumX = 0.0;
            double sumZ = 0.0;
            double sumXX = 0.0;
            double sumXZ = 0.0;
            double sumZZ = 0.0;
            auto accumulate = [&](bool edgeOnly) {
                for (const uint32_t index : seeds) {
                    if (edgeOnly && !isEdge(index)) {
                        continue;
                    }
                    const double x = static_cast<double>(index % width);
                    const double z = static_cast<double>(index / width);
                    count += 1.0;
                    sumX += x;
                    sumZ += z;
                    sumXX += x * x;
                    sumXZ += x * z;
                    sumZZ += z * z;
                }
            };
            accumulate(true);
            if (count < 2.0) {
                count = sumX = sumZ = sumXX = sumXZ = sumZZ = 0.0;
                accumulate(false);
            }
            axisX = 1.0f;
            axisZ = 0.0f;
            if (count < 2.0) {
                return;
            }
            const double meanX = sumX / count;
            const double meanZ = sumZ / count;
            const double covXX = sumXX / count - meanX * meanX;
            const double covXZ = sumXZ / count - meanX * meanZ;
            const double covZZ = sumZZ / count - meanZ * meanZ;
            if (covXX + covZZ <= 1.0e-6) {
                return;
            }
            const double angle = 0.5 * std::atan2(2.0 * covXZ, covXX - covZZ);
            axisX = static_cast<float>(std::cos(angle));
            axisZ = static_cast<float>(std::sin(angle));
        }

        /// @brief 決まったテクセルから、通ってよいテクセルへ Fast Marching で到達の値を広げる
        /// @details テクセル n を 1 テクセル進むと到達の値が stepOf(n) 増えるものとして解く。
        ///          carried の各値は、到達の値を決めた隣のテクセルの値を、到達の値の差で重み付けして引き継ぐ
        /// @param arrival  到達の値（known が 1 のテクセルは決まった値。広げたテクセルへ書く）
        /// @param known    決まったテクセルなら 1（広げたテクセルも 1 にする）
        /// @param passable テクセル n へ広げてよいか
        /// @param stepOf   テクセル n を 1 テクセル進むときの、到達の値の増え
        /// @param carried  引き継ぐ値
        template <typename Passable, typename StepOf>
        void MarchFromKnown(
            uint32_t width, uint32_t height, std::vector<float>& arrival, std::vector<uint8_t>& known,
            const Passable& passable, const StepOf& stepOf, std::initializer_list<std::vector<float>*> carried)
        {
            const size_t texelCount = static_cast<size_t>(width) * height;
            constexpr size_t kNone = std::numeric_limits<size_t>::max();
            std::vector<float> tentative(texelCount, std::numeric_limits<float>::infinity());
            using Entry = std::pair<float, uint32_t>;
            std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> trial;

            auto update = [&](uint32_t nx, uint32_t nz) {
                const size_t n = static_cast<size_t>(nz) * width + nx;
                if (known[n] != 0 || !passable(n)) {
                    return;
                }
                // X・Z の向きごとに、決まった隣のうち到達の値の小さい方
                size_t neighborX = kNone;
                size_t neighborZ = kNone;
                auto pick = [&](size_t m, size_t& best) {
                    if (known[m] != 0 && (best == kNone || arrival[m] < arrival[best])) {
                        best = m;
                    }
                };
                if (nx > 0) { pick(n - 1, neighborX); }
                if (nx + 1 < width) { pick(n + 1, neighborX); }
                if (nz > 0) { pick(n - width, neighborZ); }
                if (nz + 1 < height) { pick(n + width, neighborZ); }

                const float step = stepOf(n);
                float value = 0.0f;
                float weightX = 0.0f;
                float weightZ = 0.0f;
                if (neighborX != kNone && neighborZ != kNone && std::abs(arrival[neighborX] - arrival[neighborZ]) < step) {
                    const float difference = arrival[neighborX] - arrival[neighborZ];
                    value = 0.5f * (arrival[neighborX] + arrival[neighborZ] + std::sqrt(2.0f * step * step - difference * difference));
                    weightX = value - arrival[neighborX];
                    weightZ = value - arrival[neighborZ];
                } else if (neighborX != kNone && (neighborZ == kNone || arrival[neighborX] <= arrival[neighborZ])) {
                    value = arrival[neighborX] + step;
                    weightX = 1.0f;
                } else {
                    value = arrival[neighborZ] + step;
                    weightZ = 1.0f;
                }
                if (value >= tentative[n]) {
                    return;
                }
                tentative[n] = value;
                const float inverseWeight = 1.0f / (std::max)(weightX + weightZ, 1.0e-6f);
                for (std::vector<float>* values : carried) {
                    const float fromX = (weightX > 0.0f) ? weightX * (*values)[neighborX] : 0.0f;
                    const float fromZ = (weightZ > 0.0f) ? weightZ * (*values)[neighborZ] : 0.0f;
                    (*values)[n] = (fromX + fromZ) * inverseWeight;
                }
                trial.push({ value, static_cast<uint32_t>(n) });
            };
            auto updateNeighbors = [&](uint32_t index) {
                const uint32_t x = index % width;
                const uint32_t z = index / width;
                if (x > 0) { update(x - 1, z); }
                if (x + 1 < width) { update(x + 1, z); }
                if (z > 0) { update(x, z - 1); }
                if (z + 1 < height) { update(x, z + 1); }
            };

            for (uint32_t index = 0; index < static_cast<uint32_t>(texelCount); ++index) {
                if (known[index] != 0) {
                    updateNeighbors(index);
                }
            }
            // 到達の値の小さい順に決めて、隣の値を求め直す
            while (!trial.empty()) {
                const Entry entry = trial.top();
                trial.pop();
                const uint32_t index = entry.second;
                if (known[index] != 0 || entry.first > tentative[index]) {
                    continue;
                }
                known[index] = 1;
                arrival[index] = entry.first;
                updateNeighbors(index);
            }
        }

        /// @brief 沖（水深 kOpenSeaDepth 以上の海）から各テクセルまで、浅い水の波の速さで進む時間と、それを運んできた沖の点の峰に沿った位置
        /// @details 沖のテクセルを時間 0 の起点に、海のテクセルだけを通って Fast Marching で |∇τ| = 1 / √(g·水深) を解く。
        ///          沖のテクセルが無ければ、海のいちばん深い所の水深の kDeepestSeaSeedRatio 倍より深い海のテクセルを起点にする。
        ///          峰に沿った位置は、起点では範囲の中心から ComputeCrestAxis の向きに測った距離 [m]。
        ///          ほかのテクセルは、時間を決めた隣のテクセルの値を、時間の差で重み付けして引き継ぐ。
        ///          海と隣り合う海でないテクセルには、隣の海のテクセルの時間を、そこから向かう向きの傾きで水深 0 の点まで延ばした値を置く。
        ///          残りのテクセル（陸・水たまり・届かなかった海）は、決まったテクセルからの距離で同じように解き、
        ///          いちばん近い決まったテクセルの時間と峰に沿った位置を引き継ぐ
        /// @param isSea テクセルごとに、海とつながった水なら 1
        ShoreWaveField BuildShoreWaveField(
            const std::vector<float>& depth, const std::vector<uint8_t>& isSea, const WaterSeaMapBuilder::Input& input)
        {
            const uint32_t width = input.width;
            const uint32_t height = input.height;
            const size_t texelCount = static_cast<size_t>(width) * height;
            const float texelSize = input.texelSize;

            ShoreWaveField field;
            field.seconds.assign(texelCount, 0.0f);
            field.crest.assign(texelCount, 0.0f);
            if (texelCount == 0) {
                return field;
            }

            // ---- 沖のテクセル（無ければ海のいちばん深い所のテクセル）を起点にする ----
            float deepest = 0.0f;
            for (size_t index = 0; index < texelCount; ++index) {
                if (isSea[index] != 0) {
                    deepest = (std::max)(deepest, depth[index]);
                }
            }
            field.hasOpenSea = deepest >= kOpenSeaDepth;
            const float seedDepth = field.hasOpenSea ? kOpenSeaDepth : kDeepestSeaSeedRatio * deepest;
            std::vector<uint32_t> seeds;
            for (size_t index = 0; index < texelCount; ++index) {
                if (isSea[index] != 0 && depth[index] >= seedDepth) {
                    seeds.push_back(static_cast<uint32_t>(index));
                }
            }
            float axisX = 1.0f;
            float axisZ = 0.0f;
            ComputeCrestAxis(seeds, depth, isSea, width, height, seedDepth, axisX, axisZ);
            const float centerX = 0.5f * static_cast<float>(width);
            const float centerZ = 0.5f * static_cast<float>(height);
            std::vector<uint8_t> known(texelCount, 0);
            for (const uint32_t index : seeds) {
                const float x = static_cast<float>(index % width) + 0.5f - centerX;
                const float z = static_cast<float>(index / width) + 0.5f - centerZ;
                known[index] = 1;
                field.crest[index] = (x * axisX + z * axisZ) * texelSize;
            }

            // ---- 海のテクセルだけを通って、沖から着くまでの時間を解く ----
            MarchFromKnown(width, height, field.seconds, known,
                [&](size_t n) { return isSea[n] != 0; },
                [&](size_t n) { return texelSize / TravelSpeed(depth[n]); },
                { &field.crest });

            // ---- 海と隣り合う海でないテクセルへ、水際（水深 0 の点）での時間を置く ----
            const std::vector<uint8_t> seaKnown = known;
            for (uint32_t z = 0; z < height; ++z) {
                for (uint32_t x = 0; x < width; ++x) {
                    const size_t shore = static_cast<size_t>(z) * width + x;
                    if (seaKnown[shore] != 0 || isSea[shore] != 0) {
                        continue;
                    }
                    float secondsSum = 0.0f;
                    float crestSum = 0.0f;
                    float count = 0.0f;
                    // (dx, dz) 隣の海のテクセルの時間を、そこからこのテクセルへ向かう向きの傾きで水深 0 の点まで延ばす
                    auto addFromSea = [&](int32_t dx, int32_t dz) {
                        const int32_t seaX = static_cast<int32_t>(x) + dx;
                        const int32_t seaZ = static_cast<int32_t>(z) + dz;
                        if (seaX < 0 || seaZ < 0 || seaX >= static_cast<int32_t>(width) || seaZ >= static_cast<int32_t>(height)) {
                            return;
                        }
                        const size_t sea = static_cast<size_t>(seaZ) * width + static_cast<size_t>(seaX);
                        if (seaKnown[sea] == 0) {
                            return;
                        }
                        // 傾きは一つ沖側の海のテクセルとの差（無ければ浅い水の波の速さの逆数）
                        const float maxSlope = 1.0f / TravelSpeed(depth[sea]);
                        float slope = maxSlope;
                        const int32_t outerX = seaX + dx;
                        const int32_t outerZ = seaZ + dz;
                        if (outerX >= 0 && outerZ >= 0 && outerX < static_cast<int32_t>(width) && outerZ < static_cast<int32_t>(height)) {
                            const size_t outer = static_cast<size_t>(outerZ) * width + static_cast<size_t>(outerX);
                            if (seaKnown[outer] != 0) {
                                slope = std::clamp((field.seconds[sea] - field.seconds[outer]) / texelSize, 0.0f, maxSlope);
                            }
                        }
                        // 水深 0 の点は、海のテクセルの中心からこのテクセルの中心までを、水深と陸の高さの比で分けた所
                        const float landHeight = (std::max)(-depth[shore], 0.0f);
                        const float fraction = depth[sea] / (std::max)(depth[sea] + landHeight, 1.0e-6f);
                        secondsSum += field.seconds[sea] + fraction * texelSize * slope;
                        crestSum += field.crest[sea];
                        count += 1.0f;
                    };
                    addFromSea(-1, 0);
                    addFromSea(1, 0);
                    addFromSea(0, -1);
                    addFromSea(0, 1);
                    if (count > 0.0f) {
                        field.seconds[shore] = secondsSum / count;
                        field.crest[shore] = crestSum / count;
                        known[shore] = 1;
                    }
                }
            }

            // ---- 残りのテクセルへ、いちばん近い決まったテクセルの時間と峰に沿った位置を延ばす ----
            std::vector<float> distance(texelCount, 0.0f);
            MarchFromKnown(width, height, distance, known,
                [](size_t) { return true; },
                [&](size_t) { return texelSize; },
                { &field.seconds, &field.crest });
            return field;
        }

        /// @brief 半径 radius の箱の平均を X・Z の順に 2 回かける（端は端の値が続くものとして扱う）
        void BoxBlur2D(std::vector<float>& values, uint32_t width, uint32_t height, uint32_t radius)
        {
            if (radius == 0 || width == 0 || height == 0) {
                return;
            }
            const int64_t r = static_cast<int64_t>(radius);
            const float inverseWindow = 1.0f / static_cast<float>(2 * radius + 1);
            std::vector<float> line;
            auto blurLine = [&](float* data, size_t count, size_t stride) {
                line.resize(count);
                for (size_t k = 0; k < count; ++k) {
                    line[k] = data[k * stride];
                }
                const int64_t last = static_cast<int64_t>(count) - 1;
                auto at = [&](int64_t k) { return line[static_cast<size_t>(std::clamp<int64_t>(k, 0, last))]; };
                float sum = 0.0f;
                for (int64_t k = -r; k <= r; ++k) {
                    sum += at(k);
                }
                for (size_t k = 0; k < count; ++k) {
                    data[k * stride] = sum * inverseWindow;
                    const int64_t i = static_cast<int64_t>(k);
                    sum += at(i + r + 1) - at(i - r);
                }
            };
            for (int pass = 0; pass < 2; ++pass) {
                for (uint32_t z = 0; z < height; ++z) {
                    blurLine(values.data() + static_cast<size_t>(z) * width, width, 1);
                }
                for (uint32_t x = 0; x < width; ++x) {
                    blurLine(values.data() + x, height, width);
                }
            }
        }

        /// @brief 沖から着くまでの時間の場の傾き [s/m]（3×3 の平均をとった時間の中央差分）
        void ComputeTravelGradient(
            const std::vector<float>& seconds, uint32_t width, uint32_t height, float texelSize,
            std::vector<float>& gradientX, std::vector<float>& gradientZ)
        {
            const size_t texelCount = static_cast<size_t>(width) * height;
            auto at = [&](int32_t x, int32_t z) {
                x = std::clamp(x, 0, static_cast<int32_t>(width) - 1);
                z = std::clamp(z, 0, static_cast<int32_t>(height) - 1);
                return seconds[static_cast<size_t>(z) * width + static_cast<size_t>(x)];
            };
            std::vector<float> smoothed(texelCount);
            for (int32_t z = 0; z < static_cast<int32_t>(height); ++z) {
                for (int32_t x = 0; x < static_cast<int32_t>(width); ++x) {
                    float sum = 0.0f;
                    for (int32_t dz = -1; dz <= 1; ++dz) {
                        for (int32_t dx = -1; dx <= 1; ++dx) {
                            sum += at(x + dx, z + dz);
                        }
                    }
                    smoothed[static_cast<size_t>(z) * width + static_cast<size_t>(x)] = sum / 9.0f;
                }
            }
            auto smoothedAt = [&](int32_t x, int32_t z) {
                x = std::clamp(x, 0, static_cast<int32_t>(width) - 1);
                z = std::clamp(z, 0, static_cast<int32_t>(height) - 1);
                return smoothed[static_cast<size_t>(z) * width + static_cast<size_t>(x)];
            };
            gradientX.resize(texelCount);
            gradientZ.resize(texelCount);
            const float inverseSpan = 1.0f / (2.0f * texelSize);
            for (int32_t z = 0; z < static_cast<int32_t>(height); ++z) {
                for (int32_t x = 0; x < static_cast<int32_t>(width); ++x) {
                    const size_t index = static_cast<size_t>(z) * width + static_cast<size_t>(x);
                    gradientX[index] = (smoothedAt(x + 1, z) - smoothedAt(x - 1, z)) * inverseSpan;
                    gradientZ[index] = (smoothedAt(x, z + 1) - smoothedAt(x, z - 1)) * inverseSpan;
                }
            }
        }

        /// @brief 1 列ぶんの、半径 radius の窓の最小値（端は端の値が続くものとして扱う）
        /// @details ブロックごとの前からの最小値と後ろからの最小値を組み合わせ、窓の幅に依らない手間で求める
        void MinFilterLine(
            const float* source, float* destination, size_t count, size_t radius,
            std::vector<float>& padded, std::vector<float>& prefix, std::vector<float>& suffix)
        {
            const size_t window = 2 * radius + 1;
            const size_t paddedCount = count + 2 * radius;
            padded.resize(paddedCount);
            prefix.resize(paddedCount);
            suffix.resize(paddedCount);
            for (size_t k = 0; k < paddedCount; ++k) {
                padded[k] = source[(k > radius) ? (std::min)(k - radius, count - 1) : 0];
            }
            for (size_t k = 0; k < paddedCount; ++k) {
                prefix[k] = (k % window == 0) ? padded[k] : (std::min)(prefix[k - 1], padded[k]);
            }
            for (size_t k = paddedCount; k-- > 0;) {
                const bool blockEnd = (k % window == window - 1) || (k == paddedCount - 1);
                suffix[k] = blockEnd ? padded[k] : (std::min)(suffix[k + 1], padded[k]);
            }
            for (size_t i = 0; i < count; ++i) {
                destination[i] = (std::min)(suffix[i], prefix[i + window - 1]);
            }
        }

        /// @brief 行が Z・列が X の 2 次元の値の、半径 radius の正方形の窓の最小値
        std::vector<float> MinFilter2D(const std::vector<float>& values, uint32_t width, uint32_t height, uint32_t radius)
        {
            std::vector<float> result(values.size());
            if (radius == 0) {
                result = values;
                return result;
            }
            std::vector<float> rows(values.size());
            std::vector<float> padded;
            std::vector<float> prefix;
            std::vector<float> suffix;
            for (uint32_t z = 0; z < height; ++z) {
                const size_t offset = static_cast<size_t>(z) * width;
                MinFilterLine(values.data() + offset, rows.data() + offset, width, radius, padded, prefix, suffix);
            }
            std::vector<float> column(height);
            std::vector<float> filtered(height);
            for (uint32_t x = 0; x < width; ++x) {
                for (uint32_t z = 0; z < height; ++z) {
                    column[z] = rows[static_cast<size_t>(z) * width + x];
                }
                MinFilterLine(column.data(), filtered.data(), height, radius, padded, prefix, suffix);
                for (uint32_t z = 0; z < height; ++z) {
                    result[static_cast<size_t>(z) * width + x] = filtered[z];
                }
            }
            return result;
        }
    }

    std::vector<uint16_t> WaterSeaMapBuilder::Build(const Input& input, Stats* outStats)
    {
        const uint32_t width = input.width;
        const uint32_t height = input.height;
        const size_t texelCount = static_cast<size_t>(width) * height;
        if (!input.measured || texelCount == 0) {
            return {};
        }

        std::vector<float> depth(texelCount);
        std::vector<float> clearance(texelCount);
        for (size_t i = 0; i < texelCount; ++i) {
            depth[i] = std::clamp(input.measured[2 * i], -kMaxLandHeight, kMaxValue);
            clearance[i] = std::clamp(input.measured[2 * i + 1], 0.0f, kMaxValue);
        }

        // ---- 水のテクセルを上下左右のつながりで塊に分ける ----
        std::vector<int32_t> label(texelCount, -1);
        std::vector<uint32_t> bodySizes;
        std::vector<uint8_t> bodyTouchesBorder;
        std::vector<uint32_t> queue;
        queue.reserve(texelCount / 4);
        for (size_t start = 0; start < texelCount; ++start) {
            if (label[start] >= 0 || depth[start] <= 0.0f) {
                continue;
            }
            const int32_t body = static_cast<int32_t>(bodySizes.size());
            uint32_t size = 0;
            bool touchesBorder = false;
            queue.clear();
            queue.push_back(static_cast<uint32_t>(start));
            label[start] = body;
            for (size_t head = 0; head < queue.size(); ++head) {
                const uint32_t index = queue[head];
                ++size;
                const uint32_t x = index % width;
                const uint32_t z = index / width;
                if (x == 0 || z == 0 || x == width - 1 || z == height - 1) {
                    touchesBorder = true;
                }
                auto visit = [&](uint32_t neighbor) {
                    if (label[neighbor] < 0 && depth[neighbor] > 0.0f) {
                        label[neighbor] = body;
                        queue.push_back(neighbor);
                    }
                };
                if (x > 0) { visit(index - 1); }
                if (x + 1 < width) { visit(index + 1); }
                if (z > 0) { visit(index - width); }
                if (z + 1 < height) { visit(index + width); }
            }
            bodySizes.push_back(size);
            bodyTouchesBorder.push_back(touchesBorder ? 1 : 0);
        }

        // 範囲の端に触れる塊と一番大きい塊を海とし、残りの水たまりは陸と同じ扱いにする
        const auto largest = std::max_element(bodySizes.begin(), bodySizes.end());
        const int32_t largestBody = (largest != bodySizes.end())
            ? static_cast<int32_t>(std::distance(bodySizes.begin(), largest)) : -1;
        Stats stats{};
        stats.waterBodies = static_cast<uint32_t>(bodySizes.size());
        std::vector<uint8_t> isSea(texelCount, 0);
        for (size_t i = 0; i < texelCount; ++i) {
            const int32_t body = label[i];
            if (body < 0) {
                ++stats.landTexels;
            } else if (body == largestBody || bodyTouchesBorder[static_cast<size_t>(body)] != 0) {
                ++stats.seaTexels;
                isSea[i] = 1;
            } else {
                ++stats.basinTexels;
                depth[i] = kBasinDepth;
                clearance[i] = 0.0f;
            }
        }
        // ---- 粗い格子用の窓の最小値（上の物までの高さは、陸の下を 0 として取る） ----
        const std::vector<float> conservativeDepth = MinFilter2D(depth, width, height, input.conservativeRadius);
        std::vector<float> clearanceOverWater = clearance;
        for (size_t i = 0; i < texelCount; ++i) {
            if (depth[i] < 0.0f) {
                clearanceOverWater[i] = 0.0f;
            }
        }
        const std::vector<float> conservativeClearance =
            MinFilter2D(clearanceOverWater, width, height, input.conservativeRadius);

        // ---- 岸へ寄せる波の、沖から着くまでの時間の場 ----
        ShoreWaveField shoreWaves = BuildShoreWaveField(depth, isSea, input);
        stats.hasOpenSea = shoreWaves.hasOpenSea;
        if (outStats) {
            *outStats = stats;
        }
        // 峰に沿った位置を箱の平均でならす
        BoxBlur2D(shoreWaves.crest, width, height, (std::max)(1u,
            static_cast<uint32_t>(std::lround(kShoreCrestBlurRadius / (std::max)(input.texelSize, 1.0e-3f)))));
        std::vector<float> travelGradientX;
        std::vector<float> travelGradientZ;
        ComputeTravelGradient(shoreWaves.seconds, width, height, input.texelSize, travelGradientX, travelGradientZ);

        std::vector<uint16_t> texels(texelCount * 8);
        for (size_t i = 0; i < texelCount; ++i) {
            texels[4 * i + 0] = DirectX::PackedVector::XMConvertFloatToHalf(depth[i]);
            texels[4 * i + 1] = DirectX::PackedVector::XMConvertFloatToHalf(clearance[i]);
            texels[4 * i + 2] = DirectX::PackedVector::XMConvertFloatToHalf(conservativeDepth[i]);
            texels[4 * i + 3] = DirectX::PackedVector::XMConvertFloatToHalf(conservativeClearance[i]);
        }
        uint16_t* const fieldTexels = texels.data() + texelCount * 4;
        for (size_t i = 0; i < texelCount; ++i) {
            // 割った余りから周期の半分を引き、[-周期/2, 周期/2) で書く
            fieldTexels[4 * i + 0] = DirectX::PackedVector::XMConvertFloatToHalf(
                std::fmod(shoreWaves.seconds[i], kShoreWaveCycleSeconds) - 0.5f * kShoreWaveCycleSeconds);
            fieldTexels[4 * i + 1] = DirectX::PackedVector::XMConvertFloatToHalf(travelGradientX[i]);
            fieldTexels[4 * i + 2] = DirectX::PackedVector::XMConvertFloatToHalf(travelGradientZ[i]);
            fieldTexels[4 * i + 3] = DirectX::PackedVector::XMConvertFloatToHalf(shoreWaves.crest[i]);
        }
        return texels;
    }
}
