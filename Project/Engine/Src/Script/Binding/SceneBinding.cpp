#include "pch.h"
#include "Script/Binding/SceneBinding.h"

#include "Camera/Camera.h"
#include "EngineSystem/EngineSystem.h"
#include "Math/Vector/Vector3.h"
#include "Scene/SceneManager.h"
#include "Script/Binding/BindingRegistrar.h"
#include "Utility/Logger/Logger.h"

#include <string>

namespace CoreEngine::Script
{
    namespace
    {
        /// シーンマネージャを引く先（登録時に受け取る）
        EngineSystem* sEngineSystem = nullptr;

        SceneManager* CurrentSceneManager()
        {
            return sEngineSystem ? sEngineSystem->GetSceneManager() : nullptr;
        }

        void ChangeScene(const std::string& name)
        {
            SceneManager* const manager = CurrentSceneManager();
            if (!manager) {
                Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Script,
                    "シーンマネージャが無いので、シーン {} へ切り替えられません", name);
                return;
            }
            if (!manager->HasScene(name)) {
                Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Script,
                    "シーン {} は登録されていないので、切り替えられません", name);
                return;
            }
            manager->ChangeScene(name);
        }

        std::string GetCurrentName()
        {
            const SceneManager* const manager = CurrentSceneManager();
            return manager ? manager->GetCurrentSceneName() : std::string();
        }

        /// @brief ゲーム視点のカメラの口（使うたびに今のシーンのゲーム視点のカメラを引き直す）
        class ScriptGameCamera
        {
        public:
            ScriptGameCamera() = default;

            ScriptGameCamera(const ScriptGameCamera&) = delete;
            ScriptGameCamera& operator=(const ScriptGameCamera&) = delete;

            void AddRef() const { ++refCount_; }

            void Release() const
            {
                if (--refCount_ == 0) {
                    delete this;
                }
            }

            /// @brief 今のシーンにゲーム視点のカメラがあるか
            bool Exists() const { return Find() != nullptr; }

            Vector3 GetPosition() const
            {
                const Camera* const camera = FindOrWarn("位置の読み取り");
                return camera ? camera->GetTranslate() : Vector3{};
            }

            void SetPosition(const Vector3& position)
            {
                if (Camera* const camera = FindOrWarn("位置の変更")) {
                    camera->SetTranslate(position);
                }
            }

            /// @brief 回転（オイラー角・ラジアン）
            Vector3 GetRotation() const
            {
                const Camera* const camera = FindOrWarn("回転の読み取り");
                return camera ? camera->GetRotate() : Vector3{};
            }

            void SetRotation(const Vector3& rotation)
            {
                if (Camera* const camera = FindOrWarn("回転の変更")) {
                    camera->SetRotate(rotation);
                }
            }

            /// @brief target の方を向く（回転を書き換える）
            void LookAt(const Vector3& target)
            {
                if (Camera* const camera = FindOrWarn("注視")) {
                    camera->LookAt(target);
                }
            }

            /// @brief 位置と回転から行列を作り直す
            void UpdateMatrix()
            {
                if (Camera* const camera = FindOrWarn("行列の作り直し")) {
                    camera->UpdateMatrix();
                }
            }

        private:
            ~ScriptGameCamera() = default;

            static Camera* Find()
            {
                const SceneManager* const manager = CurrentSceneManager();
                return manager ? manager->GetGameCamera3D() : nullptr;
            }

            /// @brief カメラを引き、引けなければ 1 回だけ警告する
            Camera* FindOrWarn(const char* action) const
            {
                Camera* const camera = Find();
                if (!camera && !warned_) {
                    warned_ = true;
                    Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Script,
                        "ゲーム視点のカメラで{}をしようとしましたが、今のシーンにカメラがありません", action);
                }
                return camera;
            }

            mutable int refCount_ = 1;
            mutable bool warned_ = false;
        };

        ScriptGameCamera* GetGameCamera()
        {
            return new ScriptGameCamera();
        }

        /// @brief 今エディタ視点で覗いているか
        /// @details 覗いているカメラとゲーム視点のカメラが違えばエディタ視点。
        ///          エディタを含まないビルドでは常に false になる。
        bool IsUsingEditorCamera()
        {
            const SceneManager* const manager = CurrentSceneManager();
            if (!manager) {
                return false;
            }
            return manager->GetGameViewCamera3D() != manager->GetGameCamera3D();
        }
    }

    bool RegisterSceneBinding(asIScriptEngine* engine, EngineSystem* engineSystem)
    {
        if (!engine) {
            return false;
        }
        sEngineSystem = engineSystem;

        BindingRegistrar r(engine);
        r.ReferenceType("GameCamera", asOBJ_REF);
        r.Behaviour("GameCamera", asBEHAVE_ADDREF, "void f()", asMETHOD(ScriptGameCamera, AddRef), asCALL_THISCALL);
        r.Behaviour("GameCamera", asBEHAVE_RELEASE, "void f()", asMETHOD(ScriptGameCamera, Release), asCALL_THISCALL);
        r.Method("GameCamera", "bool get_exists() const property", asMETHOD(ScriptGameCamera, Exists), asCALL_THISCALL);
        r.Method("GameCamera", "Vector3 get_position() const property", asMETHOD(ScriptGameCamera, GetPosition), asCALL_THISCALL);
        r.Method("GameCamera", "void set_position(const Vector3 &in) property", asMETHOD(ScriptGameCamera, SetPosition), asCALL_THISCALL);
        r.Method("GameCamera", "Vector3 get_rotation() const property", asMETHOD(ScriptGameCamera, GetRotation), asCALL_THISCALL);
        r.Method("GameCamera", "void set_rotation(const Vector3 &in) property", asMETHOD(ScriptGameCamera, SetRotation), asCALL_THISCALL);
        r.Method("GameCamera", "void LookAt(const Vector3 &in target)", asMETHOD(ScriptGameCamera, LookAt), asCALL_THISCALL);
        r.Method("GameCamera", "void UpdateMatrix()", asMETHOD(ScriptGameCamera, UpdateMatrix), asCALL_THISCALL);

        r.Namespace("Scene");
        r.Function("void ChangeScene(const string &in name)", asFUNCTION(ChangeScene));
        r.Function("string GetCurrentName()", asFUNCTION(GetCurrentName));
        r.Function("GameCamera@ GetGameCamera()", asFUNCTION(GetGameCamera));
        r.Function("bool IsUsingEditorCamera()", asFUNCTION(IsUsingEditorCamera));
        r.Namespace("");
        return r.Succeeded();
    }
}
