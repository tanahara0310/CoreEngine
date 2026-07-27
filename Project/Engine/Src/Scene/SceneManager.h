#pragma once

#include "IScene.h"
#include "SceneTransition.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <functional>
#include <vector>

namespace CoreEngine
{
// 前方宣言。実体は CoreEngine::EngineSystem なので、必ず名前空間の中で宣言すること。
// 以前はグローバルスコープに書かれており、EngineSystem.h と本ヘッダを両方 include した
// 翻訳単位で `using namespace CoreEngine;` すると ::EngineSystem と衝突して
// 「あいまいなシンボル」エラーになっていた。
class EngineSystem;
class ICamera;
class GameObjectManager;

class SceneManager {
public:

    void Initialize(EngineSystem* engine);

    /// @brief シーンを登録する
    /// @tparam T シーンクラス（ISceneを継承している必要がある）
    /// @param name シーン名
    template<typename T>
    void RegisterScene(const std::string& name) {
        static_assert(std::is_base_of<IScene, T>::value);
        sceneFactories_[name] = []() { return std::make_unique<T>(); };
    }

    /// @brief 初期シーンを設定（トランジション無し）
    /// @param name 初期シーン名
    void SetInitialScene(const std::string& name);

    /// @brief シーンを変える関数（デフォルトトランジション）
    /// @param name 変更先のシーン名
    void ChangeScene(std::string name);

    /// @brief シーンを変える関数（トランジション指定版）
    /// @param name 変更先のシーン名
    /// @param transitionType トランジションタイプ
    /// @param duration トランジション時間（秒）
    void ChangeScene(std::string name, SceneTransition::TransitionType transitionType, float duration = 1.0f);

    /// @brief 更新処理
    void Update();

    /// @brief 描画処理
    void Draw();

    /// @brief 描画前準備（描画キュー構築など）
    void PrepareRender();

    /// @brief フレーム描画終了時の後処理（描画キュー破棄・削除オブジェクト cleanup）
    void FinalizeRenderFrame();

    /// @brief シーンの終了処理
    void Finalize();

    /// @brief 指定した名前のシーンが存在するか確認する関数
    /// @param name シーン名
    /// @return true: 存在する, false: 存在しない
    bool HasScene(const std::string& name) const;

    /// @brief 現在のシーン名を取得
    /// @return 現在のシーン名（シーンが無い場合は"None"）
    std::string GetCurrentSceneName() const;

    /// @brief 登録されているすべてのシーン名を取得
    /// @return シーン名のリスト
    std::vector<std::string> GetAllSceneNames() const;

    /// @brief トランジション中か確認
    /// @return true: トランジション中, false: 待機中
    bool IsTransitioning() const;

    /// @brief トランジションをスキップ（デバッグ用）
    void SkipTransition();

    /// @brief 現在のシーンのBGM音量コールバックを登録
    /// @param callback 音量倍率(0.0～1.0)を受け取るコールバック関数
    void RegisterSceneBGMCallback(std::function<void(float)> callback);

    /// @brief Gameビュー用3Dカメラを取得
    ICamera* GetGameViewCamera3D() const;

    /// @brief 既定の Gameビュー用3Dカメラを取得
    ICamera* GetDefaultGameViewCamera3D() const;

    /// @brief Gameビュー用2Dカメラを取得
    ICamera* GetGameViewCamera2D() const;

    /// @brief 現在シーンのオブジェクトマネージャーを取得
    GameObjectManager* GetCurrentGameObjectManager() const;

    /// @brief 現在シーンの DXR 水面屈折用波面データを取得
    const WaterSurfaceData* GetWaterRefractionSurfaceData() const;

    /// @brief 現在シーンが要求する補助 RenderView 一覧を構築する
    /// @return 実行要求一覧
    std::vector<RenderViewRequest> BuildRenderViewRequests();

private:
    std::unordered_map<std::string, std::function<std::unique_ptr<IScene>()>> sceneFactories_;

    std::unique_ptr<IScene> currentScene_ = nullptr;
    std::string currentSceneName_ = "None"; // 現在のシーン名を保持

    //エンジンクラスのポインタ
    EngineSystem* engine_ = nullptr; // エンジンシステムへのポインタ

    // ──────────────────────────────────────────────────────────
    // 遅延シーン切り替え用
    // ──────────────────────────────────────────────────────────
    /// @brief 次のシーン名（遅延切り替え用）
    std::string nextSceneName_;

    /// @brief シーン切り替えリクエストフラグ
    bool isSceneChangeRequested_ = false;

    /// @brief 実際のシーン切り替えを実行（内部関数）
    /// @param name 変更先のシーン名
    void DoChangeScene(const std::string& name);

    // ──────────────────────────────────────────────────────────
    // トランジション管理
    // ──────────────────────────────────────────────────────────
    /// @brief シーントランジション管理
    std::unique_ptr<SceneTransition> sceneTransition_;

    /// @brief 次のトランジションタイプ
    SceneTransition::TransitionType nextTransitionType_ = SceneTransition::TransitionType::Fade;

    /// @brief 次のトランジション時間
    float nextTransitionDuration_ = 1.0f;
};
}
