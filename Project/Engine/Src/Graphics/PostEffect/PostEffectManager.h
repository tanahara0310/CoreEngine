#pragma once
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>
#include <d3d12.h>
#include <cassert>

#include "Graphics/PostEffect/PostEffectBase.h"
#include "Graphics/PostEffect/PostEffectNames.h"
#include "Graphics/PostEffect/PingPongBuffer.h"
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
    void RegisterEffect(const std::string& name, bool enabled = false);

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

    /// @brief 特定のポストエフェクトを実行
    /// @note 有効/無効チェックなし。必要な場合はIsEffectEnabled()で事前に確認すること
    /// @param name エフェクト名
    /// @param inputSrvHandle 入力テクスチャのSRVハンドル
    void ExecuteEffect(const std::string& name, D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle);

    /// @brief バックバッファ(_SRGB)への最終描画用にエフェクトを実行
    /// @note 有効/無効チェックなし。必要な場合はIsEffectEnabled()で事前に確認すること
    /// @param name エフェクト名
    /// @param inputSrvHandle 入力テクスチャのSRVハンドル
    void ExecuteEffectToBackBuffer(const std::string& name, D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle);

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

    /// @brief 現在のエフェクトチェーンを取得
    /// @return エフェクト名のリスト
    const std::vector<std::string>& GetEffectChain() const;

    /// @brief 更新処理
    /// @param deltaTime フレーム時間
    void Update(float deltaTime);

    /// @brief ImGuiでポストエフェクトのパラメータを調整（独自ウィンドウ付き）
    void DrawImGui();

    /// @brief ImGuiのコンテンツのみ描画（外部ウィンドウから呼び出し用）
    void DrawImGuiContent();

    /// @brief 現在表示すべき最終テクスチャハンドルを取得
    /// @return 表示すべきテクスチャのSRVハンドル
    D3D12_GPU_DESCRIPTOR_HANDLE GetFinalDisplayTextureHandle() const;

    /// @brief エフェクトチェーンを実行し、結果のテクスチャハンドルを取得
    /// @param inputSrvHandle 入力テクスチャのSRVハンドル
    /// @return 最終出力のSRVハンドル
    D3D12_GPU_DESCRIPTOR_HANDLE ExecuteEffectChain(D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle);

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
    std::vector<PostEffectBase*> effectPtrCache_;

    std::unique_ptr<PostEffectPresetManager> presetManager_;

    D3D12_GPU_DESCRIPTOR_HANDLE finalDisplayHandle_ = {};
};

// =============================================================================
// テンプレート関数の実装
// =============================================================================

template<typename T>
void PostEffectManager::RegisterEffect(const std::string& name, bool enabled)
{
    static_assert(std::is_base_of<PostEffectBase, T>::value, 
        "T must inherit from PostEffectBase");
    
    auto effect = std::make_unique<T>();
    effect->Initialize(directXCommon_);
    effect->SetEnabled(enabled);
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
