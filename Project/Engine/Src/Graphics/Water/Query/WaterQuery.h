#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "Math/Vector/Vector3.h"

namespace CoreEngine
{
    class GameObject;

    /// @brief 水面を 1 点で調べた結果
    struct WaterSample {
        bool inWater = false;               ///< 点の真上か真下に水面がある（結果が届いていて、水面の範囲の中）
        float surfaceHeight = 0.0f;         ///< 水面の高さ（ワールド Y）[m]
        Vector3 normal{ 0.0f, 1.0f, 0.0f }; ///< 水面の法線
    };

    /// @brief 水面の高さを聞く点の集まり
    /// @details 点の位置は毎フレーム、シーンの水面ごとに GPU で描かれる水面と同じ式で評価し（WaterHeightQueryManager）、
    ///          結果は 1〜2 フレーム遅れで届く。結果を引くときは、評価した位置から今の位置まで水面の傾きで延ばす。
    ///          ロックは持たない（ゲームの更新と描画の記録と同じスレッドから使う）。
    class WaterQuery {
    public:
        /// @brief 同時に持てる点の数（WaterHeightQuery.CS.hlsl の kWaterHeightQueryMaxPoints と同じ値）
        static constexpr uint32_t kMaxPoints = 1024;
        /// @brief 点ごとに評価する水面の数の上限（主の水とほかの水）
        static constexpr uint32_t kMaxSurfaces = 8;
        /// @brief 点の番号が無いことを表す値
        static constexpr uint32_t kInvalidPoint = UINT32_MAX;
        /// @brief 評価した位置から傾きで延ばす距離の上限 [m]
        static constexpr float kMaxExtrapolationMeters = 2.0f;
        /// @brief 1 回の SetPosition でこれより大きく動いた点は置き直したとみなし、前の位置の結果を捨てる [m]
        static constexpr float kRelocateMeters = 10.0f;

        /// @brief GPU が点 1 つに書く結果（WaterHeightQuery.CS.hlsl の出力と同じ並び）
        struct GpuResult {
            float height = 0.0f; ///< 水面の高さ（ワールド Y）[m]
            float slopeX = 0.0f; ///< X へ 1 m 進んだときの水面の高さの変化
            float slopeZ = 0.0f; ///< Z へ 1 m 進んだときの水面の高さの変化
            uint32_t flags = 0;  ///< kGpuResultInWater
        };
        static_assert(sizeof(GpuResult) == 16, "WaterHeightQuery.CS.hlsl の出力の 1 点分と同じ大きさにする");

        /// @brief GpuResult::flags の印：水面の範囲の中
        static constexpr uint32_t kGpuResultInWater = 1u;

        /// @brief GPU へ投げた点 1 つの控え
        struct Submitted {
            float x = 0.0f;
            float z = 0.0f;
            uint32_t generation = 0; ///< 投げたときの点の世代（番号を使い回した点に古い結果を書かない）
        };

        /// @brief 点を足す
        /// @return 点の番号（満杯なら kInvalidPoint）
        uint32_t AddPoint(const Vector3& worldPosition);

        /// @brief 点を外す
        void RemovePoint(uint32_t point);

        /// @brief 点の位置を変える（次の評価から使う。kRelocateMeters より大きく動いたら前の位置の結果を捨てる）
        void SetPosition(uint32_t point, const Vector3& worldPosition);

        /// @brief worldPosition の真上・真下の水面を返す
        /// @details 点に最後に届いた結果を、評価した位置から worldPosition まで水面の傾きで延ばす。
        ///          水面が重なる所では、範囲の狭い水面（池・プール）を返す
        WaterSample Sample(uint32_t point, const Vector3& worldPosition) const;

        // ===== 描画側（WaterHeightQueryManager）=====

        /// @brief GPU へ投げる点の数（外した番号も含めた、使った番号の上限）
        uint32_t GetPointCount() const { return static_cast<uint32_t>(points_.size()); }

        /// @brief 今の点の位置と世代を out へ控える（番号の順）
        void Snapshot(std::vector<Submitted>& out) const;

        /// @brief GPU の結果を点へ書く（控えの世代が今の点と同じものだけ）
        /// @param results 水面ごと（範囲の狭い水面から）に点の番号の順で並べた結果（submitted.size() 個ずつ surfaceCount 組）
        void ApplyResults(std::span<const Submitted> submitted, std::span<const GpuResult> results, uint32_t surfaceCount);

        /// @brief 全部の点の結果を捨てる（水面の無いフレーム）
        void ClearResults();

    private:
        struct Point {
            bool active = false;
            uint32_t generation = 0;
            float x = 0.0f;
            float z = 0.0f;
            Submitted evaluatedAt{}; ///< 結果を評価した位置
            uint32_t resultCount = 0; ///< 届いた結果の数（水面の数。0 なら結果なし）
            std::array<GpuResult, kMaxSurfaces> results{};
        };

        std::vector<Point> points_;
        std::vector<uint32_t> freePoints_;
    };

    /// @brief object の属するエンジンの、水面の高さの問い合わせ先（無ければ nullptr）
    WaterQuery* FindWaterQuery(const GameObject* object);
}
