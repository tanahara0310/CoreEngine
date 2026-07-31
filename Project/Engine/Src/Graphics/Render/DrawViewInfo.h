#pragma once

#include "Graphics/Render/RenderViewType.h"

namespace CoreEngine
{
    class Camera;
    struct ViewInfo;

    /// @brief 1 回の描画呼び出しに対するビュー/パス情報
    /// @details RenderManager が描画キュー実行時に組み立てて GameObject::Draw へ明示的に渡す。
    ///          以前はレンダラー常駐フラグ（BaseModelRenderer::IsInGBufferPass）を Model が
    ///          読み取る暗黙状態だったものを、引数として流すための値オブジェクト。
    ///          ビュー依存の判定（Hi-Z 適用可否・モーションベクター履歴の更新可否など）は
    ///          この情報だけから決められること（呼び出し順への依存を作らない）。
    struct DrawViewInfo {
        /// @brief このビューの行列一式（フレーム先頭で確定した不変スナップショット）
        /// @details 描画側は必ずここから view/proj/frustum を取ること。カメラを直接
        ///          読み直すとパスごとに違う行列を使う事故が起きる。
        const ViewInfo* view = nullptr;
        RenderViewType viewType = RenderViewType::GameView; ///< 実行中のビュー種別
        bool isGBufferPass = false;                    ///< true: GBuffer 蓄積パス / false: Forward 系パス

        /// @brief 移行期の互換用カメラ取得（レガシーな Draw(const Camera*) 経路向け）
        /// @return ビュー未設定なら nullptr
        const Camera* GetCamera() const;
    };
}
