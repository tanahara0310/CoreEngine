#pragma once

// 前方宣言（ModelRenderContext が参照する全依存クラス）
namespace CoreEngine {
    class DirectXCommon;
    class ShadowMapManager;
    class BaseModelRenderer;
    class ShadowMapRenderer;
    class InstanceBatchManager;
    class SkinningComputeDispatcher;
}

namespace CoreEngine
{
    /// @brief WVP 行列バッファの用途スロット（Game / Scene / Shadow パスで切り替え）
    enum class TransformBufferSlot : uint32_t {
        Game = 0, ///< ゲームカメラ用
        Scene = 1, ///< シーンビュー用
        Shadow = 2, ///< シャドウマップ用
    };

    /// @brief Model クラスが描画に必要とする固定依存をまとめたコンテキスト構造体
    /// 起動時に一度だけ設定される依存オブジェクトを集約する。
    struct ModelRenderContext {
        DirectXCommon* dxCommon = nullptr; ///< デバイス・コマンドリスト・DescriptorManager 取得元
        ShadowMapManager* shadowMapManager = nullptr; ///< ライト VP 行列の一元管理
        BaseModelRenderer* modelRenderer = nullptr; ///< 通常モデル描画用レンダラー
        BaseModelRenderer* skinnedRenderer = nullptr; ///< スキニングモデル描画用レンダラー
        ShadowMapRenderer* shadowRenderer = nullptr; ///< シャドウマップ描画用レンダラー
        InstanceBatchManager* instanceBatchManager = nullptr; ///< 通常モデルのインスタンシング集約（ModelManager 内で自動設定）
        SkinningComputeDispatcher* skinningDispatcher = nullptr; ///< GPUスキニング(CS)ディスパッチャー（ModelManager 内で自動設定）

        /// @brief ポインタ依存が全て設定済みか確認
        /// @note instanceBatchManager は ModelManager が内部生成するため検証対象外
        bool IsValid() const {
            return dxCommon != nullptr
                && shadowMapManager != nullptr
                && modelRenderer != nullptr
                && skinnedRenderer != nullptr
                && shadowRenderer != nullptr;
        }
    };
}
