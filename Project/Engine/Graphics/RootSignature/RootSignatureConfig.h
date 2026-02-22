#pragma once

#include <d3d12.h>
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <functional>

namespace CoreEngine
{
    /// @brief リソースのバインディング戦略
    enum class BindingStrategy {
        RootDescriptor,      ///< 個別のRoot Descriptor (CBV/SRV/UAV) - 高速だがRoot Signatureサイズを消費
        DescriptorTable,     ///< 個別のDescriptor Table - 柔軟だがindirection
        GroupedTable,        ///< 同種リソースをグループ化してTable - バッチ設定に最適
        RootConstants,       ///< 32bit定数として直接埋め込み - 小さなデータに最適
        StaticSampler,       ///< Static Sampler (ルートパラメータを消費しない)
        DynamicSampler       ///< Descriptor Table経由のSampler (動的切り替え可能)
    };

    /// @brief サンプラー設定
    struct SamplerConfig {
        D3D12_FILTER filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        D3D12_TEXTURE_ADDRESS_MODE addressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        D3D12_TEXTURE_ADDRESS_MODE addressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        D3D12_TEXTURE_ADDRESS_MODE addressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        D3D12_COMPARISON_FUNC comparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        D3D12_STATIC_BORDER_COLOR borderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        FLOAT mipLODBias = 0.0f;
        UINT maxAnisotropy = 16;
        FLOAT minLOD = 0.0f;
        FLOAT maxLOD = D3D12_FLOAT32_MAX;

        /// @brief デフォルトのリニアサンプラー
        static SamplerConfig Linear() {
            return SamplerConfig{};
        }

        /// @brief ポイントサンプラー
        static SamplerConfig Point() {
            SamplerConfig config;
            config.filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
            return config;
        }

        /// @brief 異方性フィルタリングサンプラー
        static SamplerConfig Anisotropic(UINT maxAnisotropy = 16) {
            SamplerConfig config;
            config.filter = D3D12_FILTER_ANISOTROPIC;
            config.maxAnisotropy = maxAnisotropy;
            return config;
        }

        /// @brief シャドウマップ用比較サンプラー
        static SamplerConfig Shadow() {
            SamplerConfig config;
            config.filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
            config.comparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            config.addressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            config.addressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            config.addressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            config.borderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
            return config;
        }

        /// @brief クランプモードのリニアサンプラー
        static SamplerConfig LinearClamp() {
            SamplerConfig config;
            config.addressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            config.addressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            config.addressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            return config;
        }
    };

    /// @brief 個別リソースのバインディング設定
    struct ResourceBindingConfig {
        std::string resourceName;                       ///< リソース名（完全一致またはプレフィックスマッチ）
        BindingStrategy strategy;                       ///< バインディング戦略
        std::optional<UINT> tableGroupId;               ///< テーブルグループID（GroupedTable時に使用）
        std::optional<SamplerConfig> samplerConfig;     ///< サンプラー設定（サンプラーの場合）
        std::optional<UINT> rootConstantsCount;         ///< Root Constants の32bit値の数

        ResourceBindingConfig() 
            : strategy(BindingStrategy::RootDescriptor) {}

        ResourceBindingConfig(const std::string& name, BindingStrategy strat)
            : resourceName(name), strategy(strat) {}
    };

    /// @brief テーブルグループ設定
    struct TableGroupConfig {
        UINT groupId;                                   ///< グループID
        std::vector<std::string> resourceNames;         ///< グループに含めるリソース名
        D3D12_SHADER_VISIBILITY visibility;             ///< シェーダー可視性

        TableGroupConfig() 
            : groupId(0), visibility(D3D12_SHADER_VISIBILITY_ALL) {}

        TableGroupConfig(UINT id, const std::vector<std::string>& names, 
                        D3D12_SHADER_VISIBILITY vis = D3D12_SHADER_VISIBILITY_ALL)
            : groupId(id), resourceNames(names), visibility(vis) {}
    };

    /// @brief RootSignature構築設定クラス
    /// リフレクションデータと組み合わせて柔軟なRootSignature構築を可能にする
    class RootSignatureConfig {
    public:
        RootSignatureConfig() = default;
        ~RootSignatureConfig() = default;

        // ========== デフォルト戦略設定 ==========

        /// @brief CBVのデフォルト戦略を設定
        RootSignatureConfig& SetDefaultCBVStrategy(BindingStrategy strategy) {
            defaultCBVStrategy_ = strategy;
            return *this;
        }

        /// @brief SRVのデフォルト戦略を設定
        RootSignatureConfig& SetDefaultSRVStrategy(BindingStrategy strategy) {
            defaultSRVStrategy_ = strategy;
            return *this;
        }

        /// @brief UAVのデフォルト戦略を設定
        RootSignatureConfig& SetDefaultUAVStrategy(BindingStrategy strategy) {
            defaultUAVStrategy_ = strategy;
            return *this;
        }

        /// @brief サンプラーのデフォルト戦略を設定
        RootSignatureConfig& SetDefaultSamplerStrategy(BindingStrategy strategy) {
            defaultSamplerStrategy_ = strategy;
            return *this;
        }

        /// @brief デフォルトのサンプラー設定を指定
        RootSignatureConfig& SetDefaultSamplerConfig(const SamplerConfig& config) {
            defaultSamplerConfig_ = config;
            return *this;
        }

        // ========== 個別リソース設定 ==========

        /// @brief 特定リソースのバインディング設定を追加
        RootSignatureConfig& ConfigureResource(const ResourceBindingConfig& config) {
            resourceConfigs_[config.resourceName] = config;
            return *this;
        }

        /// @brief 特定リソースのバインディング戦略を設定
        RootSignatureConfig& ConfigureResource(const std::string& name, BindingStrategy strategy) {
            ResourceBindingConfig config;
            config.resourceName = name;
            config.strategy = strategy;
            resourceConfigs_[name] = config;
            return *this;
        }

        /// @brief サンプラー設定を追加（名前ベース）
        RootSignatureConfig& ConfigureSampler(const std::string& name, const SamplerConfig& samplerConfig,
                                              BindingStrategy strategy = BindingStrategy::StaticSampler) {
            ResourceBindingConfig config;
            config.resourceName = name;
            config.strategy = strategy;
            config.samplerConfig = samplerConfig;
            resourceConfigs_[name] = config;
            return *this;
        }

        // ========== テーブルグルーピング ==========

        /// @brief テーブルグループを作成
        RootSignatureConfig& CreateTableGroup(UINT groupId, const std::vector<std::string>& resourceNames,
                                              D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL) {
            TableGroupConfig group(groupId, resourceNames, visibility);
            tableGroups_[groupId] = group;

            // 各リソースにグループIDを設定
            for (const auto& name : resourceNames) {
                if (resourceConfigs_.find(name) == resourceConfigs_.end()) {
                    ResourceBindingConfig config;
                    config.resourceName = name;
                    config.strategy = BindingStrategy::GroupedTable;
                    config.tableGroupId = groupId;
                    resourceConfigs_[name] = config;
                } else {
                    resourceConfigs_[name].strategy = BindingStrategy::GroupedTable;
                    resourceConfigs_[name].tableGroupId = groupId;
                }
            }
            return *this;
        }

        // ========== フラグ設定 ==========

        /// @brief Root Signatureフラグを設定
        RootSignatureConfig& SetFlags(D3D12_ROOT_SIGNATURE_FLAGS flags) {
            flags_ = flags;
            return *this;
        }

        // ========== 取得メソッド ==========

        BindingStrategy GetDefaultCBVStrategy() const { return defaultCBVStrategy_; }
        BindingStrategy GetDefaultSRVStrategy() const { return defaultSRVStrategy_; }
        BindingStrategy GetDefaultUAVStrategy() const { return defaultUAVStrategy_; }
        BindingStrategy GetDefaultSamplerStrategy() const { return defaultSamplerStrategy_; }
        const SamplerConfig& GetDefaultSamplerConfig() const { return defaultSamplerConfig_; }
        D3D12_ROOT_SIGNATURE_FLAGS GetFlags() const { return flags_; }

        /// @brief 特定リソースの設定を取得（存在しない場合はnullopt）
        std::optional<ResourceBindingConfig> GetResourceConfig(const std::string& name) const {
            auto it = resourceConfigs_.find(name);
            if (it != resourceConfigs_.end()) {
                return it->second;
            }
            return std::nullopt;
        }

        /// @brief テーブルグループを取得
        const std::map<UINT, TableGroupConfig>& GetTableGroups() const {
            return tableGroups_;
        }

        /// @brief すべてのリソース設定を取得
        const std::map<std::string, ResourceBindingConfig>& GetAllResourceConfigs() const {
            return resourceConfigs_;
        }

        /// @brief リソースがグループに属しているかを確認
        bool IsInTableGroup(const std::string& resourceName) const {
            auto it = resourceConfigs_.find(resourceName);
            if (it != resourceConfigs_.end()) {
                return it->second.tableGroupId.has_value();
            }
            return false;
        }

        /// @brief リソースのグループIDを取得
        std::optional<UINT> GetTableGroupId(const std::string& resourceName) const {
            auto it = resourceConfigs_.find(resourceName);
            if (it != resourceConfigs_.end()) {
                return it->second.tableGroupId;
            }
            return std::nullopt;
        }

        // ========== プリセット設定 ==========

        /// @brief パフォーマンス重視の設定（CBVはRoot Descriptor、SRVは個別Table）
        static RootSignatureConfig PerformanceOptimized() {
            RootSignatureConfig config;
            config.SetDefaultCBVStrategy(BindingStrategy::RootDescriptor);
            config.SetDefaultSRVStrategy(BindingStrategy::DescriptorTable);
            config.SetDefaultUAVStrategy(BindingStrategy::DescriptorTable);
            config.SetDefaultSamplerStrategy(BindingStrategy::StaticSampler);
            return config;
        }

        /// @brief メモリ効率重視の設定（グループ化されたTable）
        static RootSignatureConfig MemoryOptimized() {
            RootSignatureConfig config;
            config.SetDefaultCBVStrategy(BindingStrategy::DescriptorTable);
            config.SetDefaultSRVStrategy(BindingStrategy::GroupedTable);
            config.SetDefaultUAVStrategy(BindingStrategy::GroupedTable);
            config.SetDefaultSamplerStrategy(BindingStrategy::StaticSampler);
            return config;
        }

        /// @brief シンプルな設定（すべてRoot Descriptor / 個別Table）
        static RootSignatureConfig Simple() {
            RootSignatureConfig config;
            config.SetDefaultCBVStrategy(BindingStrategy::RootDescriptor);
            config.SetDefaultSRVStrategy(BindingStrategy::DescriptorTable);
            config.SetDefaultUAVStrategy(BindingStrategy::RootDescriptor);
            config.SetDefaultSamplerStrategy(BindingStrategy::StaticSampler);
            return config;
        }

    private:
        // デフォルト戦略
        BindingStrategy defaultCBVStrategy_ = BindingStrategy::RootDescriptor;
        BindingStrategy defaultSRVStrategy_ = BindingStrategy::DescriptorTable;
        BindingStrategy defaultUAVStrategy_ = BindingStrategy::DescriptorTable;
        BindingStrategy defaultSamplerStrategy_ = BindingStrategy::StaticSampler;
        SamplerConfig defaultSamplerConfig_;

        // 個別リソース設定（リソース名 -> 設定）
        std::map<std::string, ResourceBindingConfig> resourceConfigs_;

        // テーブルグループ設定（グループID -> グループ設定）
        std::map<UINT, TableGroupConfig> tableGroups_;

        // Root Signatureフラグ
        D3D12_ROOT_SIGNATURE_FLAGS flags_ = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    };
}
