#pragma once

#include "Camera/Camera.h"
#include "Camera/Control/ICameraController.h"
#include "Camera/Control/OrbitFlyController.h"
#include "Camera/Control/FreeLookController.h"
#include <memory>
#include <unordered_map>
#include <string>
#include <utility>

/// @brief カメラマネージャー - 複数のカメラを管理して動的に切り替えるクラス

namespace CoreEngine
{

#ifdef USE_IMGUI
    // 前方宣言
    class CameraDebugUI;
    class GameObjectManager;
#endif
    class EngineSystem;

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

        /// @brief カメラへコントローラを取り付ける（1 カメラにつき 1 つ）
        /// @details カメラ本体は操作方法を持たない。「Blender 風に動かす」「一人称で飛ぶ」は
        ///          コントローラを付け替えるだけで切り替わる。
        ///          既にコントローラが付いている場合は置き換える。1 つに限定することで、
        ///          複数の操作系が同じカメラを奪い合う事故が構造的に起きない。
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

        /// @brief アクティブ 3D カメラのコントローラを取得
        ICameraController* GetActiveController() const;

        /// @brief アクティブ 3D カメラのコントローラを型付きで取得
        template<typename T>
        T* GetActiveControllerAs() const
        {
            return dynamic_cast<T*>(GetActiveController());
        }

        /// @brief アクティブ 3D カメラの軌道コントローラを取得（未取り付け／別種なら nullptr）
        OrbitFlyController* GetActiveOrbitController() const
        {
            return GetActiveControllerAs<OrbitFlyController>();
        }

        /// @brief アクティブカメラを設定（カメラタイプ別）
        /// @param name カメラの名前
        /// @param type カメラタイプ（3D or 2D）
        /// @return 設定に成功した場合true
        bool SetActiveCamera(const std::string& name, CameraType type);

        /// @brief アクティブカメラを取得（カメラタイプ別）
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

        /// @brief Gameビューに使用するカメラ名を上書き設定（空文字でリセット）
        void SetGameViewCameraOverride(const std::string& name) { gameViewCameraOverride_ = name; }

        /// @brief Gameビューカメラ上書き名を取得
        const std::string& GetGameViewCameraOverride() const { return gameViewCameraOverride_; }

        /// @brief デバッグUIが参照するEngineSystemを設定
        void SetEngineSystem(EngineSystem* engine) { engineSystem_ = engine; }

#ifdef USE_IMGUI
        /// @brief ImGuiデバッグウィンドウを描画
        void DrawImGui();

        /// @brief モジュール状態のみ更新（描画なし）
        void UpdateDebugModules();

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

        /// @brief アクティブなカメラの名前（タイプ別）
        std::string activeCamera3DName_;
        std::string activeCamera2DName_;

        /// @brief Gameビューカメラのオーバーライド名（空文字 = 上書きなし）
        std::string gameViewCameraOverride_;

        /// @brief アクティブなカメラのポインタ（キャッシュ、タイプ別）
        Camera* activeCamera3D_ = nullptr;
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
