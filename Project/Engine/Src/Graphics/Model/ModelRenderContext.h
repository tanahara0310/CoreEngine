#pragma once

// 前方宣言（ModelRenderContext が参照する全依存クラス）
namespace CoreEngine {
    class GraphicsCore;
    class BaseModelRenderer;
    class InstanceBatchManager;
    class SkinningComputeDispatcher;
    class HiZOcclusionSystem;
}

namespace CoreEngine
{
    /// @brief Model クラスが描画に必要とする固定依存をまとめたコンテキスト構造体
    /// 起動時に一度だけ設定される依存オブジェクトを集約する。
    struct ModelRenderContext {
        GraphicsCore* dxCommon = nullptr; ///< デバイス・コマンドリスト・DescriptorAllocator 取得元
        BaseModelRenderer* modelRenderer = nullptr; ///< 通常モデル描画用レンダラー
        BaseModelRenderer* skinnedRenderer = nullptr; ///< スキニングモデル描画用レンダラー
        InstanceBatchManager* instanceBatchManager = nullptr; ///< 通常モデルのインスタンシング集約（ModelManager 内で自動設定）
        SkinningComputeDispatcher* skinningDispatcher = nullptr; ///< GPUスキニング(CS)ディスパッチャー（ModelManager 内で自動設定）
        HiZOcclusionSystem* hiZOcclusion = nullptr; ///< Hi-Zオクルージョンカリング（任意依存: nullptr なら遮蔽判定なしで描画する）

        /// @brief 外部から注入される依存が全て設定済みか確認
        /// @note ModelManager::SetRenderContext() の受け入れ検証に使用する。
        ///       instanceBatchManager / skinningDispatcher は ModelManager が内部生成するため対象外。
        bool IsValid() const {
            return dxCommon != nullptr
                && modelRenderer != nullptr
                && skinnedRenderer != nullptr;
        }

        /// @brief 内部生成分を含む全依存が設定済みか確認
        /// @note Model::Initialize() の前提条件。ModelManager 経由でモデルを生成していれば必ず満たされる。
        bool IsComplete() const {
            return IsValid()
                && instanceBatchManager != nullptr
                && skinningDispatcher != nullptr;
        }
    };
}
