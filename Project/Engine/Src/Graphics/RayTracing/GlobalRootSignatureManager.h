#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <string>
#include <vector>
#include <map>

namespace CoreEngine
{
    /// @brief DXRグローバルルートシグネチャのエントリ種別
    enum class DXRRootEntryType {
        UAVTable,       ///< UAV デスクリプタテーブル（RWTexture 等）
        SRVTable,       ///< SRV デスクリプタテーブル（TLAS, テクスチャ等）
        CBV,            ///< CBV Root Descriptor（定数バッファ）
        RootConstants,  ///< Root Constants（32bit 値をコマンドストリームに直接埋め込み）
    };

    /// @brief DXR グローバルルートシグネチャ管理クラス
    /// @details エントリを宣言的に追加して Build() で生成する。追加順がルートパラメータ番号になる。
    /// @note 全エントリは DXR 要件により D3D12_SHADER_VISIBILITY_ALL で登録される。
    class GlobalRootSignatureManager {
    public:
        /// @brief UAV デスクリプタテーブルエントリを追加
        /// @param name        バインド名（GetRootParameterIndex の検索キー）
        /// @param shaderRegister  HLSL の u レジスタ番号
        /// @param registerSpace   レジスタスペース（省略時 0）
        GlobalRootSignatureManager& AddUAVTable(
            const std::string& name, UINT shaderRegister, UINT registerSpace = 0);

        /// @brief SRV デスクリプタテーブルエントリを追加
        /// @param name        バインド名（GetRootParameterIndex の検索キー）
        /// @param shaderRegister  HLSL の t レジスタ番号
        /// @param registerSpace   レジスタスペース（省略時 0）
        GlobalRootSignatureManager& AddSRVTable(
            const std::string& name, UINT shaderRegister, UINT registerSpace = 0);

        /// @brief CBV Root Descriptor エントリを追加
        /// @param name        バインド名（GetRootParameterIndex の検索キー）
        /// @param shaderRegister  HLSL の b レジスタ番号
        /// @param registerSpace   レジスタスペース（省略時 0）
        GlobalRootSignatureManager& AddCBV(
            const std::string& name, UINT shaderRegister, UINT registerSpace = 0);

        /// @brief Root Constants エントリを追加（小さい定数データをコマンドストリームに直接埋め込む）
        /// @param name             バインド名（GetRootParameterIndex の検索キー）
        /// @param shaderRegister   HLSL の b レジスタ番号
        /// @param num32BitValues   32bit 値の個数
        /// @param registerSpace    レジスタスペース（省略時 0）
        GlobalRootSignatureManager& AddRootConstants(
            const std::string& name, UINT shaderRegister, UINT num32BitValues, UINT registerSpace = 0);

        /// @brief 登録済みエントリからグローバルルートシグネチャを構築する
        /// @param device  D3D12 デバイス
        /// @return 成功した場合 true
        bool Build(ID3D12Device* device);

        /// @brief 構築済みルートシグネチャを取得
        ID3D12RootSignature* GetRootSignature() const { return rootSignature_.Get(); }

        /// @brief バインド名からルートパラメータインデックスを取得
        /// @return インデックス（未登録の場合 -1）
        int GetRootParameterIndex(const std::string& name) const;

        /// @brief 構築済みか
        bool IsBuilt() const { return rootSignature_ != nullptr; }

        /// @brief 登録エントリと構築結果をリセットする
        void Clear();

    private:
        /// @brief ルートパラメータ 1 個分の宣言（追加順がそのままパラメータ番号になる）
        struct Entry {
            std::string name;
            DXRRootEntryType type;
            UINT shaderRegister;
            UINT registerSpace;
            UINT num32BitValues = 0;  ///< RootConstants 用: 32bit 値の個数
        };

        std::vector<Entry> entries_;
        std::map<std::string, UINT>  nameToIndex_;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    };
}
