#pragma once

#include "Graphics/RootSignature/RootSlot.h"

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

        /// @brief CBV のバインド情報を登録する
        // リソースバインディングの追加（重複チェック付き）
        void AddCBV(const ShaderResourceBinding& binding);
        /// @brief SRV のバインド情報を登録する
        void AddSRV(const ShaderResourceBinding& binding);
        /// @brief UAV のバインド情報を登録する
        void AddUAV(const ShaderResourceBinding& binding);
        /// @brief サンプラーのバインド情報を登録する
        void AddSampler(const ShaderResourceBinding& binding);
        void AddInputElement(const InputElementInfo& element) { inputElements_.push_back(element); }

        // リソースバインディングの取得
        const std::vector<ShaderResourceBinding>& GetCBVBindings() const { return cbvBindings_; }
        const std::vector<ShaderResourceBinding>& GetSRVBindings() const { return srvBindings_; }
        const std::vector<ShaderResourceBinding>& GetUAVBindings() const { return uavBindings_; }
        const std::vector<ShaderResourceBinding>& GetSamplerBindings() const { return samplerBindings_; }
        const std::vector<InputElementInfo>& GetInputElements() const { return inputElements_; }

        /// @brief 名前で CBV のバインド情報を引く（無ければ nullptr）
        // 特定のリソースを検索
        const ShaderResourceBinding* FindCBV(const std::string& name) const;
        /// @brief 名前で SRV のバインド情報を引く（無ければ nullptr）
        const ShaderResourceBinding* FindSRV(const std::string& name) const;
        /// @brief 名前で UAV のバインド情報を引く（無ければ nullptr）
        const ShaderResourceBinding* FindUAV(const std::string& name) const;

        // デバッグ用：リフレクション結果を文字列で出力
        std::string ToString() const;

        // リソース数を取得
        size_t GetCBVCount() const { return cbvBindings_.size(); }
        size_t GetSRVCount() const { return srvBindings_.size(); }
        size_t GetUAVCount() const { return uavBindings_.size(); }
        size_t GetSamplerCount() const { return samplerBindings_.size(); }
        size_t GetInputElementCount() const { return inputElements_.size(); }

        // シェーダー名の設定・取得
        void SetShaderName(const std::string& name) { shaderName_ = name; }
        const std::string& GetShaderName() const { return shaderName_; }

        // 2つのリフレクションデータをマージ（VS + PS）
        void Merge(const ShaderReflectionData& other);

        /// @brief リソース名からルートパラメータ（番号＋差し方）を取得
        /// @return 見つからない場合は kind == None（IsValid() が false）
        /// @note RootSignature 構築後に使用可能。新規コードはこちらを使い ShaderBinder へ渡すこと。
        RootSlot GetRootSlot(const std::string& resourceName) const;

        // リソース名からルートパラメータインデックスを取得
        // BuildFromReflection後に使用可能
        /// @deprecated 番号だけでは Set* を選べない。GetRootSlot() + ShaderBinder へ移行すること。
        ///             Phase 2 で呼び出し側を置き換え終えたら削除する。
        int GetRootParameterIndexByName(const std::string& resourceName) const;

        // ルートパラメータマッピングを設定（RootSignatureManagerから呼ばれる）
        void SetRootParameterMapping(const std::map<std::string, RootSlot>& mapping);

        /// @brief 定数バッファのサイズを検証
        /// @param cbvName 定数バッファ名
        /// @param cppStructSize C++側の構造体サイズ
        /// @return サイズが一致すればtrue、不一致またはCBVが見つからなければfalse
        bool ValidateCBVSize(const std::string& cbvName, size_t cppStructSize) const;

        /// @brief 全ての定数バッファサイズを検証（複数CBVを一度に検証）
        /// @param validations CBV名とC++構造体サイズのペアのリスト
        /// @return 全て一致すればtrue
        bool ValidateAllCBVSizes(const std::vector<std::pair<std::string, size_t>>& validations) const;

        /// @brief 自動スロット検出済みの入力要素を取得
        /// @return スロット自動検出後の入力要素リスト
        std::vector<InputElementInfo> GetInputElementsWithAutoSlots() const;

    private:
        std::string shaderName_;                              // シェーダー識別名
        std::vector<ShaderResourceBinding> cbvBindings_;      // 定数バッファ
        std::vector<ShaderResourceBinding> srvBindings_;      // シェーダーリソースビュー
        std::vector<ShaderResourceBinding> uavBindings_;      // アンオーダードアクセスビュー
        std::vector<ShaderResourceBinding> samplerBindings_;  // サンプラー
        std::vector<InputElementInfo> inputElements_;         // 入力レイアウト

        // リソース名 -> ルートパラメータインデックスのマッピング
        std::map<std::string, RootSlot> rootParameterMapping_;

        // デバッグ用ヘルパー関数
        std::string GetShaderVisibilityString(D3D12_SHADER_VISIBILITY visibility) const;
        std::string GetFormatString(DXGI_FORMAT format) const;
    };
}
