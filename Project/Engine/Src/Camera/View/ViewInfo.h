#pragma once

#include <d3d12.h>
#include <cstdint>

#include "Math/MathCore.h"
#include "Math/Frustum.h"
#include "Graphics/Render/RenderViewType.h"

/// @brief 1 ビュー分の描画パラメータ（フレーム内不変のスナップショット）

namespace CoreEngine
{
    class Camera;

    /// @brief 描画・カリング・RT が参照するビュー情報
    /// @details フレーム先頭で 1 回だけ構築し、以降は誰も書き換えない「値」。
    ///          カメラという可変オブジェクトを描画側が読みに行くと、読み取り時刻によって
    ///          答えが変わる（過去に SSAO の黒斑・ギズモずれを生んだ）。行列を一度確定させて
    ///          配ることで、フレーム内の整合が構造として保証される。
    ///
    ///          ジッタは projection に既に注入済み。viewProjection / invViewProjection /
    ///          frustum はすべてこの projection から導出されるため、モデルの WVP・
    ///          深度復元・カリングが同じ行列に揃う。
    struct ViewInfo {
        /// @brief 移行期の互換用カメラポインタ
        /// @details GameObject::Draw(const Camera*) 系のレガシー描画経路がまだ Camera を
        ///          要求するため保持する。Phase 2（カメラのデータ化）で撤去する。
        ///          新規コードはここではなく下の行列群を参照すること。
        const Camera* camera = nullptr;

        RenderViewType type = RenderViewType::GameView;

        Matrix4x4 viewMatrix{};
        Matrix4x4 projection{};        ///< TAA ジッタ適用後
        Matrix4x4 viewProjection{};
        Matrix4x4 invViewProjection{}; ///< 深度からのワールド座標復元用

        Vector3 position{ 0.0f, 0.0f, 0.0f };
        Frustum frustum{};

        float nearZ = 0.1f;
        float farZ = 1000.0f;
        Vector2 jitterNdc{ 0.0f, 0.0f };

        /// @brief gCamera（CameraForGPU = 視点ワールド座標）の CBV アドレス（0 = 未確保）
        D3D12_GPU_VIRTUAL_ADDRESS cameraCBV = 0;

        bool isValid = false;
    };

    /// @brief 1 フレーム分の全ビュー（ビュー種別で引く）
    /// @details 反射ビュー等を増やす場合はここへ ViewInfo をもう 1 つ入れるだけでよい。
    ///          カメラのビュー行列を一時的に差し替えるハックは不要になる。
    class FrameViews {
    public:
        static constexpr size_t kViewCount = 3; ///< RenderViewType の要素数

        /// @brief ビューを登録する
        void Set(RenderViewType type, const ViewInfo& view)
        {
            const size_t index = ToIndex(type);
            if (index < kViewCount) {
                views_[index] = view;
                views_[index].type = type;
            }
        }

        /// @brief ビューを取得する（未登録なら isValid == false の無効ビュー）
        const ViewInfo& Get(RenderViewType type) const
        {
            const size_t index = ToIndex(type);
            return (index < kViewCount) ? views_[index] : kInvalidView;
        }

        /// @brief ビューが登録されているか
        bool Has(RenderViewType type) const { return Get(type).isValid; }

        /// @brief GameView のビュー（描画の主経路）
        const ViewInfo& GameView() const { return Get(RenderViewType::GameView); }

        /// @brief 2D（正射影）ビュー。Sprite / UI 用で 3D とは独立に持つ
        const ViewInfo& View2D() const { return view2D_; }

        /// @brief 2D ビューを登録する
        void Set2D(const ViewInfo& view) { view2D_ = view; }

    private:
        static size_t ToIndex(RenderViewType type) { return static_cast<size_t>(type); }

        static inline const ViewInfo kInvalidView{};

        ViewInfo views_[kViewCount]{};
        ViewInfo view2D_{};
    };
}
