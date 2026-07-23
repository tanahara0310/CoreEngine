#pragma once

#include <cstdint>
#include <vector>

#include "Math/Matrix/Matrix4x4.h"

/// @brief モデル1インスタンス分の可視性評価（LOD選択・Hi-Zオクルージョン判定）
/// @details Model からレンダリング最適化の実装詳細（Hi-Zのスロット管理・
///          画面占有率しきい値・デバッグトグル）を分離するための評価層。
///          Model はこのクラスの問い合わせ結果（LOD段・サブメッシュ可視）だけを使う。

namespace CoreEngine
{
    class ICamera;
    class ModelResource;
    class HiZOcclusionSystem;
    struct BoundingBox;

    class ModelVisibility {
    public:
        ModelVisibility() = default;

        /// @brief デストラクタ（Hi-Z の判定スロットを返却する）
        ~ModelVisibility();

        // Hi-Z 判定スロットの所有権が曖昧になるためコピー・ムーブ禁止
        ModelVisibility(const ModelVisibility&) = delete;
        ModelVisibility& operator=(const ModelVisibility&) = delete;

        /// @brief LOD 段を決定する（デバッグトグル・強制LOD込み）
        /// @details 画面占有率ベース: 距離でなく「画面をどれだけ占めるか」で決めるため、
        ///          カメラを近づければ自然にフル詳細へ、遠ざかるほど低ポリへ切り替わる。
        ///          返り値は SubMeshData::GetLod でクランプされる前提。
        static uint32_t SelectLod(const ModelResource& resource, const Matrix4x4& worldMatrix,
            const ICamera* camera);

        /// @brief オブジェクト単位の視錐台カリング判定（デバッグトグル込み）
        /// @details カリング判定と RenderOptimizationSettings の参照を Culling 層へ
        ///          集約するための入口。呼び出し側（GameObject 層）はトグルを意識しない。
        ///          無効 AABB・カメラ無しは安全側（可視）に倒す。
        /// @return 描画すべきなら true（視錐台の完全外側なら false）
        static bool IsModelInView(const ICamera* camera, const BoundingBox& worldBounds);

        /// @brief フレームのオクルージョン判定を準備する（モデル単位で Draw 毎に呼ぶ）
        /// @param hiZ Hi-Zオクルージョンカリングシステム（nullptr なら判定なし = 常に可視）
        /// @param eligible 判定対象パスか（メイン GameView の GBuffer 構築時のみ true）
        /// @details eligible=false または Hi-Z が収集無効なら、以後 IsSubMeshVisible は常に true。
        ///          サブメッシュ数が変わった場合（リロード等）はスロットを登録し直す。
        void BeginOcclusionQuery(HiZOcclusionSystem* hiZ,
            const ModelResource& resource, const Matrix4x4& worldMatrix,
            const Matrix4x4& viewProjection, bool eligible);

        /// @brief サブメッシュの AABB を判定対象へ登録し、前々フレームの遮蔽結果を返す
        /// @details 遮蔽スキップ中も AABB を毎フレーム登録し続け、再可視化の判定対象から外さない。
        /// @return 描画すべきなら true（遮蔽中なら false）
        bool IsSubMeshVisible(uint32_t subMeshIndex);

    private:
        // スロット登録先の Hi-Z システム（初回 BeginOcclusionQuery で確定。
        // デストラクタのスロット返却に使うため、Model より長寿命であること）
        HiZOcclusionSystem* hiZ_ = nullptr;

        // BeginOcclusionQuery で確定するフレーム内状態
        const ModelResource* resource_ = nullptr;
        Matrix4x4 worldMatrix_{};
        bool active_ = false;

        // Hi-Z の判定スロット ID（サブメッシュ単位。添字はサブメッシュに一致）
        // 要素は HiZOcclusionSystem::kInvalidId = 未登録。初回問い合わせ時に遅延登録する
        std::vector<uint32_t> occlusionIds_;
    };
}
