#include "pch.h"
#include "Script/Binding/UIBinding.h"

#include "Script/Binding/BindingRegistrar.h"
#include "Script/Binding/GameObjectBinding.h"
#include "UI/UIText.h"
#include "Utility/Logger/Logger.h"

#include <string>

namespace CoreEngine::Script
{
    namespace
    {
        /// @brief スクリプトへ渡す UIText のハンドル
        /// @details GameObject のハンドルの参照を 1 つ持ち、使うたびに UIText を引き直す。
        class ScriptUIText
        {
        public:
            explicit ScriptUIText(ScriptGameObject& owner) : owner_(owner) { owner_.AddRef(); }

            ScriptUIText(const ScriptUIText&) = delete;
            ScriptUIText& operator=(const ScriptUIText&) = delete;

            void AddRef() const { ++refCount_; }

            void Release() const
            {
                if (--refCount_ == 0) {
                    delete this;
                }
            }

            /// @brief 持ち主の GameObject があり、UIText か
            bool Exists() const { return Find() != nullptr; }

            std::string GetText() const
            {
                const UIText* const text = FindOrWarn("文字列の読み取り");
                return text ? text->GetText() : std::string();
            }

            void SetText(const std::string& value)
            {
                if (UIText* const text = FindOrWarn("文字列の変更")) {
                    text->SetText(value);
                }
            }

            float GetFontSize() const
            {
                const UIText* const text = FindOrWarn("文字の大きさの読み取り");
                return text ? text->GetFontSize() : 0.0f;
            }

            void SetFontSize(float value)
            {
                if (UIText* const text = FindOrWarn("文字の大きさの変更")) {
                    text->SetFontSize(value);
                }
            }

            Vector2 GetAnchoredPosition() const
            {
                const UIText* const text = FindOrWarn("位置の読み取り");
                return text ? text->GetAnchoredPosition() : Vector2{};
            }

            void SetAnchoredPosition(const Vector2& value)
            {
                if (UIText* const text = FindOrWarn("位置の変更")) {
                    text->SetAnchoredPosition(value);
                }
            }

            Vector2 GetPivot() const
            {
                const UIText* const text = FindOrWarn("基準点の読み取り");
                return text ? text->GetPivot() : Vector2{};
            }

            void SetPivot(const Vector2& value)
            {
                if (UIText* const text = FindOrWarn("基準点の変更")) {
                    text->SetPivot(value);
                }
            }

            Vector4 GetColor() const
            {
                const UIText* const text = FindOrWarn("色の読み取り");
                return text ? text->GetColor() : Vector4{};
            }

            void SetColor(const Vector4& value)
            {
                if (UIText* const text = FindOrWarn("色の変更")) {
                    text->SetColor(value);
                }
            }

            int GetSortOrder() const
            {
                const UIText* const text = FindOrWarn("描画順の読み取り");
                return text ? text->GetSortOrder() : 0;
            }

            void SetSortOrder(int value)
            {
                if (UIText* const text = FindOrWarn("描画順の変更")) {
                    text->SetSortOrder(value);
                }
            }

            int GetAnchor() const
            {
                const UIText* const text = FindOrWarn("アンカーの読み取り");
                return static_cast<int>(text ? text->GetAnchor() : UIAnchor::Center);
            }

            void SetAnchor(int value)
            {
                if (UIText* const text = FindOrWarn("アンカーの変更")) {
                    text->SetAnchor(static_cast<UIAnchor>(value));
                }
            }

            float GetRotation() const
            {
                const UIText* const text = FindOrWarn("回転の読み取り");
                return text ? text->GetUIRotation() : 0.0f;
            }

            void SetRotation(float value)
            {
                if (UIText* const text = FindOrWarn("回転の変更")) {
                    text->SetUIRotation(value);
                }
            }

            /// @brief 持ち主の GameObject のハンドル（参照を 1 つ足して返す）
            ScriptGameObject* GetGameObject() const
            {
                owner_.AddRef();
                return &owner_;
            }

        private:
            ~ScriptUIText() { owner_.Release(); }

            UIText* Find() const
            {
                return dynamic_cast<UIText*>(owner_.Resolve());
            }

            /// @brief UIText を引き、引けなければ 1 回だけ警告する
            UIText* FindOrWarn(const char* action) const
            {
                UIText* const text = Find();
                if (!text && !warned_) {
                    warned_ = true;
                    Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Script,
                        "UIText で{}をしようとしましたが、GameObject が無いか UIText ではありません", action);
                }
                return text;
            }

            ScriptGameObject& owner_;
            mutable int refCount_ = 1;
            mutable bool warned_ = false;
        };

        ScriptUIText* GetUIText(ScriptGameObject& self)
        {
            return new ScriptUIText(self);
        }

        void RegisterAnchor(BindingRegistrar& r)
        {
            r.Enum("UIAnchor");
            r.EnumValue("UIAnchor", "TopLeft", static_cast<int>(UIAnchor::TopLeft));
            r.EnumValue("UIAnchor", "TopCenter", static_cast<int>(UIAnchor::TopCenter));
            r.EnumValue("UIAnchor", "TopRight", static_cast<int>(UIAnchor::TopRight));
            r.EnumValue("UIAnchor", "MiddleLeft", static_cast<int>(UIAnchor::MiddleLeft));
            r.EnumValue("UIAnchor", "Center", static_cast<int>(UIAnchor::Center));
            r.EnumValue("UIAnchor", "MiddleRight", static_cast<int>(UIAnchor::MiddleRight));
            r.EnumValue("UIAnchor", "BottomLeft", static_cast<int>(UIAnchor::BottomLeft));
            r.EnumValue("UIAnchor", "BottomCenter", static_cast<int>(UIAnchor::BottomCenter));
            r.EnumValue("UIAnchor", "BottomRight", static_cast<int>(UIAnchor::BottomRight));
        }

        void RegisterText(BindingRegistrar& r)
        {
            r.ReferenceType("UIText", asOBJ_REF);
            r.Behaviour("UIText", asBEHAVE_ADDREF, "void f()", asMETHOD(ScriptUIText, AddRef), asCALL_THISCALL);
            r.Behaviour("UIText", asBEHAVE_RELEASE, "void f()", asMETHOD(ScriptUIText, Release), asCALL_THISCALL);
            r.Method("UIText", "bool get_exists() const property", asMETHOD(ScriptUIText, Exists), asCALL_THISCALL);
            r.Method("UIText", "string get_text() const property", asMETHOD(ScriptUIText, GetText), asCALL_THISCALL);
            r.Method("UIText", "void set_text(const string &in) property", asMETHOD(ScriptUIText, SetText), asCALL_THISCALL);
            r.Method("UIText", "float get_fontSize() const property", asMETHOD(ScriptUIText, GetFontSize), asCALL_THISCALL);
            r.Method("UIText", "void set_fontSize(float) property", asMETHOD(ScriptUIText, SetFontSize), asCALL_THISCALL);
            r.Method("UIText", "Vector2 get_anchoredPosition() const property",
                asMETHOD(ScriptUIText, GetAnchoredPosition), asCALL_THISCALL);
            r.Method("UIText", "void set_anchoredPosition(const Vector2 &in) property",
                asMETHOD(ScriptUIText, SetAnchoredPosition), asCALL_THISCALL);
            r.Method("UIText", "Vector2 get_pivot() const property", asMETHOD(ScriptUIText, GetPivot), asCALL_THISCALL);
            r.Method("UIText", "void set_pivot(const Vector2 &in) property", asMETHOD(ScriptUIText, SetPivot), asCALL_THISCALL);
            r.Method("UIText", "Vector4 get_color() const property", asMETHOD(ScriptUIText, GetColor), asCALL_THISCALL);
            r.Method("UIText", "void set_color(const Vector4 &in) property", asMETHOD(ScriptUIText, SetColor), asCALL_THISCALL);
            r.Method("UIText", "int get_sortOrder() const property", asMETHOD(ScriptUIText, GetSortOrder), asCALL_THISCALL);
            r.Method("UIText", "void set_sortOrder(int) property", asMETHOD(ScriptUIText, SetSortOrder), asCALL_THISCALL);
            r.Method("UIText", "UIAnchor get_anchor() const property", asMETHOD(ScriptUIText, GetAnchor), asCALL_THISCALL);
            r.Method("UIText", "void set_anchor(UIAnchor) property", asMETHOD(ScriptUIText, SetAnchor), asCALL_THISCALL);
            r.Method("UIText", "float get_rotation() const property", asMETHOD(ScriptUIText, GetRotation), asCALL_THISCALL);
            r.Method("UIText", "void set_rotation(float) property", asMETHOD(ScriptUIText, SetRotation), asCALL_THISCALL);
            r.Method("UIText", "GameObject@ get_gameObject() const property", asMETHOD(ScriptUIText, GetGameObject), asCALL_THISCALL);
            r.Method("GameObject", "UIText@ get_uiText() property", asFUNCTION(GetUIText), asCALL_CDECL_OBJLAST);
        }
    }

    bool RegisterUIBinding(asIScriptEngine* engine)
    {
        if (!engine) {
            return false;
        }
        BindingRegistrar r(engine);
        RegisterAnchor(r);
        RegisterText(r);
        return r.Succeeded();
    }
}
