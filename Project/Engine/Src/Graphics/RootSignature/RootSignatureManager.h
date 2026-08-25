#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <map>
#include <memory>

#include "Utility/Logger/Logger.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/RootSignature/RootSignatureBuilder.h"

namespace CoreEngine
{
    // 前方宣言
    class ShaderReflectionData;

    /// @brief RootSignatureの管理クラス
    /// シェーダーリフレクションと設定ベースでRootSignatureを構築する
    class RootSignatureManager {
    public:
        RootSignatureManager() = default;
        ~RootSignatureManager() = default;

        /// @brief RootSignatureを取得
        ID3D12RootSignature* GetRootSignature() { return rootSignature_.Get(); }

        /// @brief シェーダーリフレクションと設定からRootSignatureを構築
        /// @param device D3D12デバイス
        /// @param reflectionData シェーダーリフレクションデータ
        /// @param config 構築設定（省略時はSimple設定）
        /// @return 構築結果（成功/失敗、マッピング情報を含む）
        RootSignatureBuildResult Build(
            ID3D12Device* device,
            const ShaderReflectionData& reflectionData,
            const RootSignatureConfig& config = RootSignatureConfig{});

        /// @brief 最後のビルド結果を取得
        const RootSignatureBuildResult& GetLastBuildResult() const { return lastBuildResult_; }

        /// @brief リソース名からルートパラメータ（番号＋差し方）を取得
        /// @param resourceName リソース名
        /// @return ルートパラメータ。見つからない場合は kind == None（IsValid() が false）
        /// @note 新規コードはこちらを使い、結果を ShaderBinder へ渡すこと
        RootSlot GetRootSlot(const std::string& resourceName) const;

        /// @brief リソース名からルートパラメータインデックスを取得
        /// @param resourceName リソース名
        /// @return ルートパラメータインデックス（見つからない場合は-1）
        /// @deprecated 番号だけでは Set* を選べない。GetRootSlot() + ShaderBinder へ移行すること。
        ///             Phase 2 で呼び出し側を置き換え終えたら削除する。
        int GetRootParameterIndex(const std::string& resourceName) const;

        /// @brief 設定をクリア（再構築前に呼び出す）
        void Clear();

    private:
        Logger& logger_ = Logger::GetInstance();

        // RootSignature
        Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_ = nullptr;

        // ビルダー
        std::unique_ptr<RootSignatureBuilder> builder_ = std::make_unique<RootSignatureBuilder>();

        // 最後のビルド結果（マッピング情報保持）
        RootSignatureBuildResult lastBuildResult_;
    };
}
