#pragma once

#include "Camera/Camera.h"
#include "Camera/Control/ICameraController.h"
#include "Camera/Control/OrbitFlyController.h"
#include "Camera/Control/FreeLookController.h"
#include <memory>
#include <unordered_map>
#include <string>
#include <utility>

/// @file
/// @brief カメラマネージャー - 複数のカメラを管理して動的に切り替えるクラス

namespace CoreEngine
{

#ifdef USE_IMGUI
    // 前方宣言
    class CameraDebugUI;
    struct CameraEditorViewport;
    class GameObjectManager;
#endif
    class EngineSystem;

    /// @brief 名前付きカメラとそのコントローラを保持し、Scene / Game のどちらを覗くかを決める
    class CameraManager {
    public:
        /// @brief コンストラクタ
        CameraManager();

        /// @brief デストラクタ
        ~CameraManager();

        /// @brief カメラを登録
        /// @param name カメラの名前
        /// @param camera 登録するカメラのユニークポインタ
        void RegisterCamera(const std::string& name, std::unique_ptr<Camera> camera);

        /// @brief カメラを登録解除
        /// @param name カメラの名前
        void UnregisterCamera(const std::string& name);

        /// @brief カメラへコントローラを取り付ける（1 カメラにつき 1 つ・既存があれば置き換え）
        /// @tparam T ICameraController の派生型
        /// @param name 対象カメラ名（未登録なら何もしない）
        /// @return 取り付けたコントローラ（失敗時 nullptr）
        template<typename T, typename... Args>
        T* AttachController(const std::string& name, Args&&... args)
        {
            if (cameras_.find(name) == cameras_.end()) {
                return nullptr;
            }
            auto controller = std::make_unique<T>(std::forward<Args>(args)...);
            T* raw = controller.get();
            controllers_[name] = std::move(controller);
            return raw;
        }

        /// @brief カメラに取り付けられたコントローラを取得
        /// @param name カメラ名
        /// @return コントローラ（未取り付けなら nullptr）
        ICameraController* GetController(const std::string& name) const;

        /// @brief 指定カメラのコントローラを型付きで取得
        /// @return 型が一致しなければ nullptr
        template<typename T>
        T* GetControllerAs(const std::string& name) const
        {
            return dynamic_cast<T*>(GetController(name));
        }

        /// @brief 現在覗いている 3D カメラのコントローラを取得
        ICameraController* GetActiveController() const;

        /// @brief アクティブ 3D カメラのコントローラを型付きで取得
        template<typename T>
        T* GetActiveControllerAs() const
        {
            return dynamic_cast<T*>(GetActiveController());
        }

        /// @brief 現在覗いている 3D カメラの軌道コントローラを取得（未取り付け／別種なら nullptr）
        OrbitFlyController* GetActiveOrbitController() const
        {
            return GetActiveControllerAs<OrbitFlyController>();
        }

        // ===== カメラの役割（Scene / Game）=====
        // Scene カメラ … エディタ視点。シーンの構図とは無関係に自由に動かす
        // Game  カメラ … ゲーム視点。シーンが構図を決める
        // 描画・ギズモ・ピッキングはすべて GetViewCamera() を見るので食い違わない。

        /// @brief エディタ視点カメラの名前を設定
        void SetSceneCameraName(const std::string& name) { sceneCameraName_ = name; }
        const std::string& GetSceneCameraName() const { return sceneCameraName_; }

        /// @brief ゲーム視点カメラの名前を設定
        void SetGameCameraName(const std::string& name) { gameCameraName_ = name; }
        const std::string& GetGameCameraName() const { return gameCameraName_; }

        /// @brief エディタ視点で覗くかどうかを設定（false = ゲーム視点）
        void SetUseSceneCamera(bool useSceneCamera) { useSceneCamera_ = useSceneCamera; }

        /// @brief エディタ視点で覗いているか
        bool IsUsingSceneCamera() const { return useSceneCamera_; }

        /// @brief 今このフレームで覗いている 3D カメラ
        /// @details 描画・ギズモ・ピッキングが参照する唯一の 3D カメラ。
        /// @return カメラ（未登録なら nullptr）
        Camera* GetViewCamera() const;

        /// @brief 現在覗いている 3D カメラの名前
        const std::string& GetViewCameraName() const;

        /// @brief アクティブカメラを設定（2D 専用）
        /// @param name カメラの名前
        /// @param type カメラタイプ（Camera2D のみ有効）
        /// @return 設定に成功した場合true
        bool SetActiveCamera(const std::string& name, CameraType type);

        /// @brief アクティブカメラを取得（カメラタイプ別）
        /// @details Camera3D は GetViewCamera() と同じものを返す。
        ///          「編集対象のカメラ」と「描画に使うカメラ」を一致させるための入口。
        /// @param type カメラタイプ（3D or 2D）
        /// @return アクティブカメラのポインタ（存在しない場合nullptr）
        Camera* GetActiveCamera(CameraType type) const;

        /// @brief 名前でカメラを取得
        /// @param name カメラの名前
        /// @return カメラのポインタ（存在しない場合nullptr）
        Camera* GetCamera(const std::string& name) const;

        /// @brief 全カメラを更新（コントローラ → Transform → 行列 の一方向）
        /// @param input 正規化済みのカメラ操作入力
        /// @param deltaTime 前フレームからの経過秒
        void Update(const CameraInputState& input, float deltaTime);

        /// @brief 登録されているカメラの数を取得
        size_t GetCameraCount() const { return cameras_.size(); }

        /// @brief アクティブカメラの名前を取得（タイプ別）
        const std::string& GetActiveCameraName(CameraType type) const;

        /// @brief 全カメラのコンテナを取得（デバッグUI用）
        const std::unordered_map<std::string, std::unique_ptr<Camera>>& GetAllCameras() const { return cameras_; }

        /// @brief デバッグUIが参照するEngineSystemを設定
        void SetEngineSystem(EngineSystem* engine) { engineSystem_ = engine; }

#ifdef USE_IMGUI
        /// @brief ImGuiデバッグウィンドウを描画
        void DrawImGui();

        /// @brief モジュール状態のみ更新（描画なし）
        void UpdateDebugModules();

        /// @brief ゲームビューポート上のカメラ編集の重ね描き（ギズモ・アイコン）
        /// @param viewCamera いま覗いているカメラ
        /// @param viewport ビューポートの位置と大きさ [px]
        void DrawDebugViewportOverlay(const Camera& viewCamera,
            const CameraEditorViewport& viewport);

        /// @brief カメラUIの内容のみ描画（ImGui::Begin/Endなし、Inspectorパネル埋め込み用）
        void DrawImGuiContent();

        /// @brief カメラエディターで参照するGameObjectManagerを設定
        void SetDebugGameObjectManager(GameObjectManager* gameObjectManager);
#endif

    private:
        /// @brief カメラのコンテナ
        std::unordered_map<std::string, std::unique_ptr<Camera>> cameras_;

        /// @brief カメラ名 → コントローラ（1 カメラにつき 1 つ。付いていないカメラもある）
        std::unordered_map<std::string, std::unique_ptr<ICameraController>> controllers_;

        /// @brief 役割ごとのカメラ名
        std::string sceneCameraName_ = CameraNames::Scene;
        std::string gameCameraName_ = CameraNames::Game;

        /// @brief エディタ視点で覗いているか（false = ゲーム視点）
        bool useSceneCamera_ = false;

        /// @brief アクティブな 2D カメラ
        std::string activeCamera2DName_;
        Camera* activeCamera2D_ = nullptr;

        /// @brief 入力・デルタタイム参照用（非所有）
        EngineSystem* engineSystem_ = nullptr;

#ifdef USE_IMGUI
        /// @brief デバッグUI（遅延初期化）
        std::unique_ptr<CameraDebugUI> debugUI_;

        /// @brief デバッグUIへ渡すゲームオブジェクトマネージャー（非所有）
        GameObjectManager* debugGameObjectManager_ = nullptr;
#endif
    };
}
