#pragma once
#include <wrl.h>
#include <functional>
#include <memory>
#include <vector>
#include <string>

#include "ComponentManager.h"

#ifdef _DEBUG
#include "Utility/Debug/ImGui/ImGuiManager.h"
#include "Utility/Debug/GameDebugUI.h"
#endif

class WinApp;

#ifdef _DEBUG
class DockingUI; // 前方宣言（デバッグビルドのみ）
class ConsoleUI; // 前方宣言（デバッグビルドのみ）
#endif



// グラフィックス関連
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Render/Sprite/SpriteRenderer.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/TextureManager.h"
#include "Graphics/IBL/IBLGenerator.h"
#include "Particle/ParticleSystem.h"

// 入力関連
#include "Input/InputManager.h"
#include "Input/KeyboardInput.h"
#include "Input/MouseInput.h"
#include "Input/GamePadInput.h"

// オーディオ関連
#include "Audio/SoundManager.h"

// カメラ関連
#include "Camera/Release/Camera.h"

// 数学ライブラリ
#include "Math/MathCore.h"

// ユーティリティ
#include "Utility/Collision/CollisionUtils.h"

// レンダーパイプライン
#include "Graphics/Render/Pass/RenderPipeline.h"


/// @brief エンジンシステム中核システム管理クラス

namespace CoreEngine
{
class EngineSystem {
public:
    /// @brief エンジンシステムの初期化
    /// @param winApp ウィンドウアプリケーション
    void Initialize(WinApp* winApp);

    /// @brief エンジンシステムの終了処理
    void Finalize();

    /// @brief フレーム開始処理
    void BeginFrame();

    /// @brief フレーム終了処理
    void EndFrame();

    /// @brief 共通描画パイプライン - オフスクリーンレンダリングとポストエフェクトを自動処理
    /// @param renderCallback シーン固有の描画処理を記述するコールバック
    void ExecuteRenderPipeline(std::function<void()> renderCallback);

    // ──────────────────────────────────────────────────────────
    // コンポーネントアクセッサ
    // ──────────────────────────────────────────────────────────

    /// @brief コンポーネントを取得（型安全）
    /// @tparam T コンポーネントの型
    /// 
    /// 代表的なコンポーネント例:
    /// 
    /// - DirectXCommon: DirectX12の基本機能
    /// 
    /// - TextureManager: テクスチャ管理
    /// 
    /// - ModelManager: 3Dモデル管理
    /// 
    /// - Input 系: Keyboard / Mouse / Gamepad
    /// 
    /// - Audio / Light / FrameRate など
    /// 
    /// @return コンポーネントへのポインタ（登録されていない場合nullptr）
    template<typename T>
    T* GetComponent() {
        return componentManager_.Get<T>();
    }

    /// @brief コンポーネントが登録されているか確認
    /// @tparam T コンポーネントの型（GetComponentと同じ型を指定可能）
    /// @return 登録されている場合true
    template<typename T>
    bool HasComponent() const {
        return componentManager_.Has<T>();
    }

#ifdef _DEBUG
    // ──────────────────────────────────────────────────────────
    // デバッグ機能アクセッサ（デバッグビルドのみ）
    // ──────────────────────────────────────────────────────────

    /// @brief ImGuiマネージャーへのアクセッサ
    ImGuiManager* GetImGuiManager() { return imGui_.get(); }

    /// @brief ゲームデバッグUIへのアクセッサ
    GameDebugUI* GetGameDebugUI() { return gameDebugUI_.get(); }

    /// @brief ドッキングUIへのアクセッサ
    /// @return ドッキングUIへのポインタ
    DockingUI* GetDockingUI() { return imGui_ ? imGui_->GetDockingUI() : nullptr; }

    /// @brief コンソールUIへのアクセッサ
    /// @return コンソールUIへのポインタ
    ConsoleUI* GetConsole() { return gameDebugUI_ ? gameDebugUI_->GetConsole() : nullptr; }
#endif

private:
    // ──────────────────────────────────────────────────────────
    // コンポーネント登録ヘルパー
    // ──────────────────────────────────────────────────────────

    /// @brief コンポーネントを登録
    /// @tparam T コンポーネントの型
    /// @param component コンポーネントのunique_ptr
    template<typename T>
    void RegisterComponent(std::unique_ptr<T> component) {
        T* ptr = component.get();
        componentOwners_.emplace_back(
            component.release(),
            [](void* p) { delete static_cast<T*>(p); }
        );
        componentManager_.Register(ptr);
    }

    // ──────────────────────────────────────────────────────────
    // コンポーネント作成ヘルパーメソッド
    // ──────────────────────────────────────────────────────────
    void CreateGraphicsComponents();
    void CreateInputComponents();
    void CreateAudioComponents();
    void CreateLightComponents();
    void CreateFrameRateController();

    /// @brief デフォルトのレンダーパイプラインを構築
    void BuildDefaultRenderPipeline();

    // ──────────────────────────────────────────────────────────
    // コアメンバ変数
    // ──────────────────────────────────────────────────────────

    WinApp* winApp_ = nullptr;

    // コンポーネント管理
    ComponentManager componentManager_;

    // コンポーネントの所有権管理（型消去されたポインタのコンテナ）
    std::vector<std::unique_ptr<void, void(*)(void*)>> componentOwners_;

    // レンダーパイプライン
    std::unique_ptr<RenderPipeline> renderPipeline_;

#ifdef _DEBUG
    // デバッグ機能（デバッグビルドのみ）
    std::unique_ptr<ImGuiManager> imGui_ = std::make_unique<ImGuiManager>(); // ImGuiマネージャークラスのインスタンス
    std::unique_ptr<GameDebugUI> gameDebugUI_ = std::make_unique<GameDebugUI>(); // ゲームデバッグUIのインスタンス
#endif // _DEBUG
};
}
