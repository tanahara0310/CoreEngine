#pragma once

#include <d3d12.h>
#include <string>
#include <vector>
#include <memory>
#include <map>

namespace CoreEngine
{
    /// @brief シェーダーリソースバインディング情報
    struct ShaderResourceBinding {
        std::string name;                   // リソース名
        D3D_SHADER_INPUT_TYPE type;        // CBV, SRV, UAV, Sampler
        UINT bindPoint;                    // レジスタ番号 (b0, t0, s0など)
        UINT bindCount;                    // 配列サイズ（単体の場合は1）
        UINT space;                        // レジスタ空間
        D3D12_SHADER_VISIBILITY visibility; // VS, PS, ALL
        UINT size;                         // CBVの場合のバイトサイズ

        ShaderResourceBinding()
            : type(D3D_SIT_CBUFFER)
            , bindPoint(0)
            , bindCount(1)
            , space(0)
            , visibility(D3D12_SHADER_VISIBILITY_ALL)
            , size(0)
        {}
    };

    /// @brief 入力レイアウト要素情報
    struct InputElementInfo {
        std::string semanticName;           // セマンティック名 (POSITION, TEXCOORD等)
        UINT semanticIndex;                // セマンティックインデックス
        DXGI_FORMAT format;                // データフォーマット
        UINT inputSlot;                    // 入力スロット番号
        UINT alignedByteOffset;            // オフセット

        InputElementInfo()
            : semanticIndex(0)
            , format(DXGI_FORMAT_UNKNOWN)
            , inputSlot(0)
            , alignedByteOffset(D3D12_APPEND_ALIGNED_ELEMENT)
        {}
    };

    /// @brief シェーダーリフレクション結果を保持するクラス
    class ShaderReflectionData {
    public:
        ShaderReflectionData() = default;
        ~ShaderReflectionData() = default;

        // リソースバインディングの追加（重複チェック付き）
        void AddCBV(const ShaderResourceBinding& binding);
        void AddSRV(const ShaderResourceBinding& binding);
        void AddUAV(const ShaderResourceBinding& binding);
        void AddSampler(const ShaderResourceBinding& binding);
        void AddInputElement(const InputElementInfo& element) { inputElements_.push_back(element); }

        // リソースバインディングの取得
        const std::vector<ShaderResourceBinding>& GetCBVBindings() const { return cbvBindings_; }
        const std::vector<ShaderResourceBinding>& GetSRVBindings() const { return srvBindings_; }
        const std::vector<ShaderResourceBinding>& GetUAVBindings() const { return uavBindings_; }
        const std::vector<ShaderResourceBinding>& GetSamplerBindings() const { return samplerBindings_; }
        const std::vector<InputElementInfo>& GetInputElements() const { return inputElements_; }

        // 全リソースバインディングの取得（ソート済み）
        std::vector<ShaderResourceBinding> GetAllBindingsSorted() const;

        // 特定のリソースを検索
        const ShaderResourceBinding* FindCBV(const std::string& name) const;
        const ShaderResourceBinding* FindSRV(const std::string& name) const;
        const ShaderResourceBinding* FindUAV(const std::string& name) const;
        const ShaderResourceBinding* FindSampler(const std::string& name) const;

        // デバッグ用：リフレクション結果を文字列で出力
        std::string ToString() const;

        // リソース数を取得
        size_t GetCBVCount() const { return cbvBindings_.size(); }
        size_t GetSRVCount() const { return srvBindings_.size(); }
        size_t GetUAVCount() const { return uavBindings_.size(); }
        size_t GetSamplerCount() const { return samplerBindings_.size(); }
        size_t GetInputElementCount() const { return inputElements_.size(); }

        // データをクリア
        void Clear();

        // 2つのリフレクションデータをマージ（VS + PS）
        void Merge(const ShaderReflectionData& other);

        // リソース名からルートパラメータインデックスを取得
        // BuildFromReflection後に使用可能
        int GetRootParameterIndexByName(const std::string& resourceName) const;

        // ルートパラメータインデックスマッピングを設定（RootSignatureManagerから呼ばれる）
        void SetRootParameterMapping(const std::map<std::string, UINT>& mapping);

    private:
        std::vector<ShaderResourceBinding> cbvBindings_;      // 定数バッファ
        std::vector<ShaderResourceBinding> srvBindings_;      // シェーダーリソースビュー
        std::vector<ShaderResourceBinding> uavBindings_;      // アンオーダードアクセスビュー
        std::vector<ShaderResourceBinding> samplerBindings_;  // サンプラー
        std::vector<InputElementInfo> inputElements_;         // 入力レイアウト

        // リソース名 -> ルートパラメータインデックスのマッピング
        std::map<std::string, UINT> rootParameterMapping_;

        // 重複チェック用ヘルパー（visibility考慮版）
        bool HasBinding(const std::vector<ShaderResourceBinding>& bindings, 
                       UINT bindPoint, UINT space, D3D12_SHADER_VISIBILITY visibility) const;

        // デバッグ用ヘルパー関数
        std::string GetShaderVisibilityString(D3D12_SHADER_VISIBILITY visibility) const;
        std::string GetFormatString(DXGI_FORMAT format) const;
    };
}
