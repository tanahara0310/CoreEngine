#pragma once

#include "RootSignatureConfig.h"
#include "Graphics/RootSignature/RootSlot.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include <map>
#include <memory>

namespace CoreEngine
{
    /// @brief RootSignature構築結果
    struct RootSignatureBuildResult {
        Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
        /// @brief リソース名 -> ルートパラメータ（番号＋差し方）
        /// @note 番号だけでなく種別も返すのが要点。Set* の選択を呼び出し側に暗記させない。
        std::map<std::string, RootSlot> resourceToRootSlot;
        std::map<UINT, std::vector<std::string>> tableGroupResources; ///< グループID -> 含まれるリソース名リスト
        bool success = false;
        std::string errorMessage;
    };

    /// @brief シェーダーリフレクションと設定からRootSignatureを構築するクラス
    class RootSignatureBuilder {
    public:
        RootSignatureBuilder() = default;
        ~RootSignatureBuilder() = default;

        /// @brief リフレクションデータと設定からRootSignatureを構築
        /// @param device D3D12デバイス
        /// @param reflectionData シェーダーリフレクションデータ
        /// @param config 構築設定（省略時はデフォルト設定）
        /// @return 構築結果
        RootSignatureBuildResult Build(
            ID3D12Device* device,
            const ShaderReflectionData& reflectionData,
            const RootSignatureConfig& config = RootSignatureConfig());

    private:
        /// @brief 名前→ルートパラメータ（番号＋差し方）を登録する
        /// @details 同名が既にある場合は「VS と PS で同名リソースが別レジスタに置かれた」ケースで、
        ///          後勝ちの上書きにより片方が名前から到達不能になる。無言で壊れるのでログへ出す。
        void AssignMapping(
            std::map<std::string, RootSlot>& mapping,
            const std::string& resourceName,
            UINT rootParamIndex,
            RootSlotKind kind,
            const std::string& shaderName) const;

        /// @brief CBVを処理
        void ProcessCBVs(
            const ShaderReflectionData& reflectionData,
            const RootSignatureConfig& config,
            std::vector<D3D12_ROOT_PARAMETER>& rootParams,
            std::map<std::string, RootSlot>& mapping,
            UINT& currentIndex);

        /// @brief SRVを処理
        void ProcessSRVs(
            const ShaderReflectionData& reflectionData,
            const RootSignatureConfig& config,
            std::vector<D3D12_ROOT_PARAMETER>& rootParams,
            std::vector<std::vector<D3D12_DESCRIPTOR_RANGE>>& rangeStorage,
            std::map<std::string, RootSlot>& mapping,
            std::map<UINT, std::vector<std::string>>& tableGroups,
            UINT& currentIndex);

        /// @brief UAVを処理
        void ProcessUAVs(
            const ShaderReflectionData& reflectionData,
            const RootSignatureConfig& config,
            std::vector<D3D12_ROOT_PARAMETER>& rootParams,
            std::vector<std::vector<D3D12_DESCRIPTOR_RANGE>>& rangeStorage,
            std::map<std::string, RootSlot>& mapping,
            std::map<UINT, std::vector<std::string>>& tableGroups,
            UINT& currentIndex);

        /// @brief サンプラーを処理
        void ProcessSamplers(
            const ShaderReflectionData& reflectionData,
            const RootSignatureConfig& config,
            std::vector<D3D12_STATIC_SAMPLER_DESC>& staticSamplers,
            std::vector<D3D12_ROOT_PARAMETER>& rootParams,
            std::vector<std::vector<D3D12_DESCRIPTOR_RANGE>>& rangeStorage,
            std::map<std::string, RootSlot>& mapping,
            UINT& currentIndex);

        /// @brief リソースの戦略を決定
        BindingStrategy DetermineStrategy(
            const std::string& resourceName,
            D3D_SHADER_INPUT_TYPE resourceType,
            const RootSignatureConfig& config) const;

        /// @brief SamplerConfigをD3D12_STATIC_SAMPLER_DESCに変換
        D3D12_STATIC_SAMPLER_DESC ConvertToStaticSamplerDesc(
            const SamplerConfig& config,
            UINT shaderRegister,
            UINT registerSpace,
            D3D12_SHADER_VISIBILITY visibility) const;

        /// @brief Root Descriptorパラメータを作成
        D3D12_ROOT_PARAMETER CreateRootDescriptorParam(
            D3D12_ROOT_PARAMETER_TYPE type,
            UINT shaderRegister,
            UINT registerSpace,
            D3D12_SHADER_VISIBILITY visibility) const;
    };
}
