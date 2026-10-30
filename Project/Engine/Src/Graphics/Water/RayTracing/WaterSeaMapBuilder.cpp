#include "pch.h"
#include "WaterSeaMapBuilder.h"

#include <DirectXPackedVector.h>

#include <algorithm>

namespace CoreEngine
{
    namespace {
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
            depth[i] = std::clamp(input.measured[2 * i], -kMaxValue, kMaxValue);
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
        for (size_t i = 0; i < texelCount; ++i) {
            const int32_t body = label[i];
            if (body < 0) {
                ++stats.landTexels;
            } else if (body == largestBody || bodyTouchesBorder[static_cast<size_t>(body)] != 0) {
                ++stats.seaTexels;
            } else {
                ++stats.basinTexels;
                depth[i] = -kMaxValue;
                clearance[i] = 0.0f;
            }
        }
        if (outStats) {
            *outStats = stats;
        }

        // ---- 粗い格子用の窓の最小値 ----
        const std::vector<float> conservativeDepth = MinFilter2D(depth, width, height, input.conservativeRadius);
        const std::vector<float> conservativeClearance = MinFilter2D(clearance, width, height, input.conservativeRadius);

        std::vector<uint16_t> texels(texelCount * 4);
        for (size_t i = 0; i < texelCount; ++i) {
            texels[4 * i + 0] = DirectX::PackedVector::XMConvertFloatToHalf(depth[i]);
            texels[4 * i + 1] = DirectX::PackedVector::XMConvertFloatToHalf(clearance[i]);
            texels[4 * i + 2] = DirectX::PackedVector::XMConvertFloatToHalf(conservativeDepth[i]);
            texels[4 * i + 3] = DirectX::PackedVector::XMConvertFloatToHalf(conservativeClearance[i]);
        }
        return texels;
    }
}
