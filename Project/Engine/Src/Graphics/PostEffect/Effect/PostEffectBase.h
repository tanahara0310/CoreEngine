#pragma once
#include <d3d12.h>
#include <string>
#include <memory>

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/RootSignature/RootSignatureManager.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"


namespace CoreEngine {

    /// @brief ポストエフェクトの実行方式
    enum class PostEffectExecutionType {
        Graphics, ///< グラフィクスパイプライン（VS+PS）
        Compute,  ///< コンピュートパイプライン（CS）
    };

    class ShaderReflectionData;

    /// @brief ポストエフェクト共通基底クラス
    /// @details PS/CS 両方に共通する状態・メソッドのみを管理する
    class PostEffectBase {
    public:
        virtual ~PostEffectBase() = default;

        /// @brief 初期化
        virtual void Initialize(DirectXCommon* dxCommon) = 0;

        /// @brief グラフィクスパイプラインによる描画（PS派生クラスで実装）
        virtual void Draw(D3D12_GPU_DESCRIPTOR_HANDLE /*inputSrvHandle*/) {}

        /// @brief バックバッファ(_SRGB)への最終描画（PS派生クラスで実装）
        virtual void DrawToBackBuffer(D3D12_GPU_DESCRIPTOR_HANDLE /*inputSrvHandle*/) {}

        /// @brief コンピュートシェーダーによるエフェクト実行（CS派生クラスで実装）
        /// @param inputSrvHandle  入力テクスチャのSRVハンドル
        /// @param outputUavHandle 出力テクスチャのUAVハンドル
        /// @param width           出力幅
        /// @param height          出力高さ
        virtual void Dispatch(
            D3D12_GPU_DESCRIPTOR_HANDLE /*inputSrvHandle*/,
            D3D12_GPU_DESCRIPTOR_HANDLE /*outputUavHandle*/,
            uint32_t /*width*/,
            uint32_t /*height*/) {}

        /// @brief エフェクトの実行方式を返す
        virtual PostEffectExecutionType GetExecutionType() const = 0;

        /// @brief ImGuiでパラメータを調整する関数
        virtual void DrawImGui() {}

        /// @brief 更新処理（デフォルトは空実装）
        /// @param deltaTime フレーム時間
        virtual void Update(float /*deltaTime*/) {}

        /// @brief エフェクトの有効/無効を設定
        virtual void SetEnabled(bool enabled) { enabled_ = enabled; }

        /// @brief エフェクトが有効かどうかを取得
        bool IsEnabled() const { return enabled_; }

        /// @brief 常時有効なエフェクトかどうかを取得（無効化不可）
        virtual bool IsAlwaysEnabled() const { return false; }

        /// @brief シェーダーリソース名からルートパラメータインデックスを取得
        int GetRootParamIndex(const std::string& resourceName) const;

    protected:
        virtual std::string GetEffectName() const { return "PostEffect"; }

        /// @brief ルートシグネチャ構築前に呼ばれる設定フック
        virtual void OnConfigureRootSignature(RootSignatureConfig& /*config*/) {}

        DirectXCommon* directXCommon_ = nullptr;
        std::unique_ptr<RootSignatureManager> rootSignatureManager_;
        std::unique_ptr<ShaderReflectionData> reflectionData_; ///< シェーダーリフレクションデータ
        bool enabled_ = true;
    };
}
