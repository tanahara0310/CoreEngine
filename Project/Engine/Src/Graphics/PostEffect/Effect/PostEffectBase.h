#pragma once
#include <d3d12.h>
#include <string>
#include <string_view>
#include <memory>
#include <vector>

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/RootSignature/RootSignatureManager.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/PostEffect/Effect/PostEffectStage.h"
#include "Graphics/PostEffect/Effect/PostEffectFrameContext.h"
#include "Utility/CVar/CVar.h"


namespace CoreEngine {

    /// @brief ポストエフェクトの実行方式
    enum class PostEffectExecutionType {
        Graphics, ///< グラフィクスパイプライン（VS+PS）
        Compute,  ///< コンピュートパイプライン（CS）
    };

    class ShaderReflectionData;
    class PostEffectGraphBuilder;

    /// @brief エフェクトが要求する追加入力 1 件
    /// @details 「このエフェクトは深度が要る」という依存を、パイプライン側の分岐ではなく
    ///          エフェクト自身の宣言として表現するための記述子。
    ///          宣言されたものは PostEffectPass が RenderGraph へ Read として登録するので、
    ///          バリアと実行順が自動で保証される。宣言せずにリソースを直接読むと
    ///          グラフから見えない依存になり、状態遷移が保証されない。
    struct PostEffectInputBinding {
        const char* slot        = nullptr; ///< シェーダー側リソース名（例 "gDepth"）
        const char* logicalName = nullptr; ///< Blackboard 論理名（FrameBlackboard::SceneDepth 等）
        bool        required    = true;    ///< 解決できないときエフェクトごとスキップするか
    };

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

        /// @brief 所属するパイプライン段を返す
        /// @details 既定は演出系を想定した PostTonemap。
        ///          光学現象（ブルーム・色収差・口径食）と露出・グレーディングは SceneHDR、
        ///          トーンマッパ本体は Tonemap を返すこと。
        ///          チェーン順の妥当性は PostEffectManager::ValidateChain() が検証する。
        virtual PostEffectStage GetStage() const { return PostEffectStage::PostTonemap; }

        /// @brief ImGuiでパラメータを調整する関数
        virtual void DrawImGui() {}

        /// @brief フレーム開始時に呼ばれる。行列・時間・シーン状態の取り込みはここで行う
        /// @details エフェクトが「自分に必要な値を自分で取りに行く」ための唯一の入口。
        ///          パイプライン側（PostEffectPass / RenderPipeline）にエフェクト名の分岐を
        ///          書きたくなったら、それはこの関数の使い漏らしである。
        /// @param ctx 今フレームの文脈
        virtual void PrepareFrame(const PostEffectFrameContext& /*ctx*/) {}

        /// @brief 入力色以外に読みたい論理リソースを申告する
        /// @details 申告したものは RenderGraph へ Read として登録され、実行直前に
        ///          SRV が解決されて GetExtraInput() から取れるようになる。
        ///          必須（required=true）が解決できないフレームは、このエフェクトは
        ///          実行されずスキップされる（クラッシュも黒画面も起こさない）。
        /// @param out 追加入力の並び（呼び出し側が空で渡す）
        virtual void DeclareExtraInputs(std::vector<PostEffectInputBinding>& /*out*/) const {}

        /// @brief 解決済みの追加入力を登録する
        /// @note PostEffectPass が実行直前に呼ぶ。エフェクト側からは呼ばない
        void SetResolvedExtraInput(const char* slot, D3D12_GPU_DESCRIPTOR_HANDLE srvHandle);

        /// @brief 解決済み追加入力をクリアする
        /// @note PostEffectPass が解決を始める前に呼ぶ
        void ClearResolvedExtraInputs() { resolvedExtraInputs_.clear(); }

        /// @brief 自分のパス群を RenderGraph へ積む
        /// @details 既定実装は「入力1・出力1の単一パス」を積み、従来どおり Dispatch / Draw を呼ぶ。
        ///          つまり**これを上書きしないエフェクトは無改修のまま動く**。
        ///          ミップチェーンやピラミッド分解のように多段が必要なエフェクトだけが上書きする。
        /// @param builder パスと一時リソースを積むためのファサード
        virtual void BuildPasses(PostEffectGraphBuilder& builder);

        /// @brief エフェクトの有効/無効を設定
        virtual void SetEnabled(bool enabled)
        {
            if (CVar<bool>* cvar = GetEnabledCVar()) {
                cvar->Set(enabled);
                return;
            }
            enabled_ = enabled;
        }

        /// @brief エフェクトが有効かどうかを取得
        bool IsEnabled() const
        {
            if (const CVar<bool>* cvar = GetEnabledCVar()) {
                return cvar->Get();
            }
            return enabled_;
        }

        /// @brief 常時有効なエフェクトかどうかを取得（無効化不可）
        virtual bool IsAlwaysEnabled() const { return false; }

        /// @brief 有効/無効を保持する CVar を返す
        /// @return CVar（"r.<Effect>.Enabled"）。持たないエフェクトは nullptr
        /// @details 派生クラスが自分のファイルスコープ CVar を返すことで、
        ///          有効状態が自動的に UI・CVars.json・コンソールへ載る。
        ///          常時有効なエフェクト（FullScreen / ToneMapping）は nullptr のままでよい。
        ///          設計: Docs/Engine/Editor/CVar_Design.md
        virtual CVar<bool>* GetEnabledCVar() const { return nullptr; }

        /// @brief シェーダーリソース名からルートパラメータインデックスを取得
        int GetRootParamIndex(const std::string& resourceName) const;

    protected:
        virtual std::string GetEffectName() const { return "PostEffect"; }

        /// @brief ルートシグネチャ構築前に呼ばれる設定フック
        virtual void OnConfigureRootSignature(RootSignatureConfig& /*config*/) {}

        /// @brief DeclareExtraInputs で申告したスロットの SRV を取り出す
        /// @param slot DeclareExtraInputs で指定したシェーダーリソース名
        /// @return 解決済み SRV。未解決なら ptr == 0
        D3D12_GPU_DESCRIPTOR_HANDLE GetExtraInput(const char* slot) const;

        DirectXCommon* directXCommon_ = nullptr;
        std::unique_ptr<RootSignatureManager> rootSignatureManager_;
        std::unique_ptr<ShaderReflectionData> reflectionData_; ///< シェーダーリフレクションデータ
        bool enabled_ = true;

    private:
        /// @brief 解決済み追加入力 1 件
        struct ResolvedExtraInput {
            std::string_view            slot; ///< 文字列リテラル（静的記憶域）を指す
            D3D12_GPU_DESCRIPTOR_HANDLE srv{};
        };

        /// @brief 実行直前に PostEffectPass が埋める。件数が少ないため線形探索で十分
        std::vector<ResolvedExtraInput> resolvedExtraInputs_;
    };
}
