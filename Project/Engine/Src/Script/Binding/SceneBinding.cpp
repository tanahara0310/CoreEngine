#include "pch.h"
#include "Script/Binding/SceneBinding.h"

#include "Camera/Camera.h"
#include "EngineSystem/EngineSystem.h"
#include "Math/MathCore.h"
#include "Math/Vector/Vector3.h"
#include "Scene/SceneManager.h"
#include "Script/Binding/BindingRegistrar.h"
#include "Utility/Logger/Logger.h"
#include "WinApp/WinApp.h"

#include <cmath>
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

            /// @brief 前の向き（正規化済み）
            Vector3 GetForward() const
            {
                const Camera* const camera = FindOrWarn("前の向きの読み取り");
                return camera ? camera->GetForward() : Vector3{ 0.0f, 0.0f, 1.0f };
            }

            /// @brief 右の向き（正規化済み）
            Vector3 GetRight() const
            {
                const Camera* const camera = FindOrWarn("右の向きの読み取り");
                return camera ? camera->GetRight() : Vector3{ 1.0f, 0.0f, 0.0f };
            }

            /// @brief 上の向き（正規化済み）
            Vector3 GetUp() const
            {
                const Camera* const camera = FindOrWarn("上の向きの読み取り");
                return camera ? camera->GetUp() : Vector3{ 0.0f, 1.0f, 0.0f };
            }

            /// @brief ワールド座標を、UI と同じ画面座標へ写す
            /// @return x・y は左上を原点とした基準解像度（WinApp::kReferenceWidth × kReferenceHeight）のピクセル、
            ///         z はカメラの前方への距離。z が 0 以下（カメラの後ろ）のときの x・y は使えない
            /// @note 最後に行列を作ったときのカメラで写す。TAA のジッタは含めない
            Vector3 WorldToScreenPoint(const Vector3& world) const
            {
                const Camera* const camera = FindOrWarn("画面座標への変換");
                if (!camera) {
                    return Vector3{};
                }
                namespace Coord = MathCore::CoordinateTransform;
                const Vector3 view = Coord::TransformCoord(world, camera->GetViewMatrix());
                const Vector4 clip = Coord::TransformCoord(
                    Vector4{ view.x, view.y, view.z, 1.0f }, camera->GetUnjitteredProjectionMatrix());

                constexpr float kMinW = 1.0e-6f;
                const float w = (std::abs(clip.w) < kMinW) ? std::copysign(kMinW, clip.w) : clip.w;
                const float ndcX = clip.x / w;
                const float ndcY = clip.y / w;
                return Vector3{
                    (ndcX * 0.5f + 0.5f) * static_cast<float>(WinApp::kReferenceWidth),
                    (0.5f - ndcY * 0.5f) * static_cast<float>(WinApp::kReferenceHeight),
                    view.z };
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
        r.Method("GameCamera", "Vector3 get_forward() const property", asMETHOD(ScriptGameCamera, GetForward), asCALL_THISCALL);
        r.Method("GameCamera", "Vector3 get_right() const property", asMETHOD(ScriptGameCamera, GetRight), asCALL_THISCALL);
        r.Method("GameCamera", "Vector3 get_up() const property", asMETHOD(ScriptGameCamera, GetUp), asCALL_THISCALL);
        r.Method("GameCamera", "Vector3 WorldToScreenPoint(const Vector3 &in world) const",
            asMETHOD(ScriptGameCamera, WorldToScreenPoint), asCALL_THISCALL);

        r.Namespace("Scene");
        r.Function("void ChangeScene(const string &in name)", asFUNCTION(ChangeScene));
        r.Function("string GetCurrentName()", asFUNCTION(GetCurrentName));
        r.Function("GameCamera@ GetGameCamera()", asFUNCTION(GetGameCamera));
        r.Function("bool IsUsingEditorCamera()", asFUNCTION(IsUsingEditorCamera));
        r.Namespace("");
        return r.Succeeded();
    }
}
