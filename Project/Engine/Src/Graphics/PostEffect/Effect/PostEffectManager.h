#pragma once
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>
#include <d3d12.h>
#include <cassert>

#include "Graphics/PostEffect/Effect/PostEffectBase.h"
#include "Graphics/PostEffect/Effect/PostEffectNames.h"
#include "PostEffectPresetManager.h"

namespace CoreEngine
{
// 前方宣言
class DirectXCommon;
class Render;
class RenderTarget;

/// @brief ポストエフェクト管理クラス
class PostEffectManager {
public:
    /// @brief 初期化
    /// @param dxCommon DirectXCommonのポインタ
    /// @param render Renderクラスのポインタ
    void Initialize(DirectXCommon* dxCommon, Render* render);

    /// @brief テンプレートでエフェクトを登録（型推論による簡潔な登録）
    /// @tparam T エフェクトの型（PostEffectBaseを継承）
    /// @param name エフェクト名
    /// @param enabled 初期有効状態
    template<typename T>
    void RegisterEffect(const std::string& name);

    /// @brief 型安全なエフェクト取得
    /// @tparam T エフェクトの型
    /// @param name エフェクト名
    /// @return キャストされたエフェクトのポインタ（見つからない場合はnullptr）
    template<typename T>
    T* GetEffect(const std::string& name);

    /// @brief 型安全なエフェクト取得（const版）
    /// @tparam T エフェクトの型
    /// @param name エフェクト名
    /// @return キャストされたエフェクトのポインタ（見つからない場合はnullptr）
    template<typename T>
    const T* GetEffect(const std::string& name) const;

    /// @brief エフェクトの有効/無効を設定
    /// @param effectName エフェクト名
    /// @param enabled 有効にするかどうか
    void SetEffectEnabled(const std::string& effectName, bool enabled);

    /// @brief エフェクトが有効かどうかを取得
    /// @param effectName エフェクト名
    /// @return 有効ならtrue
    bool IsEffectEnabled(const std::string& effectName) const;

    /// @brief エフェクトチェーンの順序を設定
    /// @param effectNames エフェクト名のリスト
    void SetEffectChain(const std::vector<std::string>& effectNames);

    /// @brief チェーンの段（PostEffectStage）が規約を満たすか検証する
    /// @details 検証内容は 2 つ。
    ///          ① 段が SceneHDR → Tonemap → PostTonemap の順に単調であること
    ///          ② Tonemap 段がちょうど 1 つであること
    ///          違反は Error ログへ理由付きで出したうえで assert する。
    ///          「並べ間違えても何も起きない」状態を無くすのが目的。
    /// @return 規約を満たしていれば true
    bool ValidateChain() const;

    /// @brief 今フレームの文脈を全エフェクトへ配る（毎フレーム 1 回）
    /// @details 有効なエフェクトはチェーン内外を問わず全て受け取る。
    ///          エフェクト固有の値注入はこの経路だけを使うこと。
    /// @param ctx 今フレームの文脈
    void PrepareFrame(const PostEffectFrameContext& ctx);

    /// @brief ImGuiでポストエフェクトのパラメータを調整（独自ウィンドウ付き）
    void DrawImGui();

    /// @brief ImGuiのコンテンツのみ描画（外部ウィンドウから呼び出し用）
    void DrawImGuiContent();

    /// @brief 現在表示すべき最終テクスチャハンドルを取得
    /// @return 表示すべきテクスチャのSRVハンドル
    D3D12_GPU_DESCRIPTOR_HANDLE GetFinalDisplayTextureHandle() const;

    /// @brief 現在表示すべき最終テクスチャハンドルを設定
    /// @param handle 表示対象のSRVハンドル
    void SetFinalDisplayTextureHandle(D3D12_GPU_DESCRIPTOR_HANDLE handle);

    /// @brief 現在有効なポストエフェクト列を取得する
    /// @return 有効エフェクトの実行順リスト
    const std::vector<PostEffectBase*>& GetEnabledEffects() const { return effectPtrCache_; }

    /// @brief 現在有効なポストエフェクトの登録名列を取得する（GetEnabledEffects() と同順）
    /// @return 有効エフェクトの登録名リスト
    const std::vector<std::string>& GetEnabledEffectNames() const { return effectNameCache_; }

    private:
    /// @brief 全エフェクトを登録
    void RegisterAllEffects();

    /// @brief ポストエフェクトを登録（内部用）
    /// @param name エフェクト名
    /// @param effect ポストエフェクトのインスタンス
    void RegisterEffectInternal(const std::string& name, std::unique_ptr<PostEffectBase> effect);

    /// @brief エフェクト取得（内部用・型なし）
    /// @param name エフェクト名
    /// @return ポストエフェクトのポインタ（見つからない場合はnullptr）
    PostEffectBase* GetEffectInternal(const std::string& name);

    /// @brief エフェクト取得（内部用・型なし・const版）
    /// @param name エフェクト名
    /// @return ポストエフェクトのポインタ（見つからない場合はnullptr）
    const PostEffectBase* GetEffectInternal(const std::string& name) const;

    /// @brief 有効エフェクトのポインタキャッシュを再構築
    /// @details effectChain_の変更またはSetEffectEnabled呼び出し時に実行する
    void RebuildEffectPtrCache();

    static constexpr int kImGuiSearchBufSize = 128;
    static constexpr float kEffectListPanelWidth = 200.0f;

    DirectXCommon* directXCommon_ = nullptr;
    Render* render_ = nullptr;

    std::unordered_map<std::string, std::unique_ptr<PostEffectBase>> effects_;

    char imguiSearchBuf_[kImGuiSearchBufSize] = {};
    std::string imguiSelectedEffect_;

    std::vector<std::string> effectChain_;

    /// @brief effectChain_中の有効エフェクトのポインタキャッシュ
    /// @details 毎フレームの unordered_map ルックアップを排除するため使用
    /// @brief 最後にキャッシュを再構築した時点の CVar 変更通番
    /// @details 有効/無効は CVar が持つため、SetEffectEnabled を通らない変更
    ///          （設定復元・コンソール・プリセット）も検知する必要がある
    uint32_t lastCVarRevision_ = 0;

    std::vector<PostEffectBase*> effectPtrCache_;
    std::vector<std::string> effectNameCache_; ///< effectPtrCache_ と同順の登録名キャッシュ

    /// @brief PrepareFrame を配る対象（チェーン内の有効エフェクト＋チェーン外の有効エフェクト）
    /// @details 毎フレームの探索を無くすため effectPtrCache_ と同時に作り直す。
    ///          チェーン外（FullScreen 等）も文脈は受け取る必要がある。
    std::vector<PostEffectBase*> prepareCache_;

    std::unique_ptr<PostEffectPresetManager> presetManager_;

    D3D12_GPU_DESCRIPTOR_HANDLE finalDisplayHandle_ = {};
};

// =============================================================================
// テンプレート関数の実装
// =============================================================================

template<typename T>
void PostEffectManager::RegisterEffect(const std::string& name)
{
    static_assert(std::is_base_of<PostEffectBase, T>::value,
        "T must inherit from PostEffectBase");

    auto effect = std::make_unique<T>();
    effect->Initialize(directXCommon_);
    // 有効/無効は各エフェクトの CVar（"r.<Effect>.Enabled"）の既定値、
    // または CVar を持たないエフェクトの enabled_ 初期値が決める
    RegisterEffectInternal(name, std::move(effect));
}

template<typename T>
T* PostEffectManager::GetEffect(const std::string& name)
{
    auto it = effects_.find(name);
    if (it != effects_.end()) {
        return dynamic_cast<T*>(it->second.get());
    }
    return nullptr;
}

template<typename T>
const T* PostEffectManager::GetEffect(const std::string& name) const
{
    auto it = effects_.find(name);
    if (it != effects_.end()) {
        return dynamic_cast<const T*>(it->second.get());
    }
    return nullptr;
}
}
