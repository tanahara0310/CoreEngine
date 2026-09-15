#include "pch.h"
#include "Script/Binding/UIBinding.h"

#include "EngineSystem/EngineSystem.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "Script/Binding/BindingRegistrar.h"
#include "Script/Binding/GameObjectBinding.h"
#include "Text/FontManager.h"
#include "UI/UIImage.h"
#include "UI/UIText.h"
#include "Utility/Logger/Logger.h"

#include <angelscript.h>
#include <scriptarray/scriptarray.h>

#include <memory>
#include <string>

namespace CoreEngine::Script
{
    namespace
    {
        /// 名前付きのフォントを登録する先（登録時に受け取る）
        EngineSystem* sEngineSystem = nullptr;

        /// ログに出す UI 要素の型名
        template <class Element> constexpr const char* kElementTypeName = "";
        template <> constexpr const char* kElementTypeName<UIText> = "UIText";
        template <> constexpr const char* kElementTypeName<UIImage> = "UIImage";

        /// @brief スクリプトへ渡す UI 要素（UIText / UIImage）のハンドル
        /// @details GameObject のハンドルの参照を 1 つ持ち、使うたびに UI 要素を引き直す。
        ///          文字と画像にだけある口は、その型を登録するときだけ使う。
        template <class Element>
        class ScriptUIElement
        {
        public:
            explicit ScriptUIElement(ScriptGameObject& owner) : owner_(owner) { owner_.AddRef(); }

            ScriptUIElement(const ScriptUIElement&) = delete;
            ScriptUIElement& operator=(const ScriptUIElement&) = delete;

            void AddRef() const { ++refCount_; }

            void Release() const
            {
                if (--refCount_ == 0) {
                    delete this;
                }
            }

            /// @brief 持ち主の GameObject があり、この種類の UI 要素か
            bool Exists() const { return Find() != nullptr; }

            Vector2 GetAnchoredPosition() const
            {
                const Element* const element = FindOrWarn("位置の読み取り");
                return element ? element->GetAnchoredPosition() : Vector2{};
            }

            void SetAnchoredPosition(const Vector2& value)
            {
                if (Element* const element = FindOrWarn("位置の変更")) {
                    element->SetAnchoredPosition(value);
                }
            }

            Vector2 GetPivot() const
            {
                const Element* const element = FindOrWarn("基準点の読み取り");
                return element ? element->GetPivot() : Vector2{};
            }

            void SetPivot(const Vector2& value)
            {
                if (Element* const element = FindOrWarn("基準点の変更")) {
                    element->SetPivot(value);
                }
            }

            Vector4 GetColor() const
            {
                const Element* const element = FindOrWarn("色の読み取り");
                return element ? element->GetColor() : Vector4{};
            }

            void SetColor(const Vector4& value)
            {
                if (Element* const element = FindOrWarn("色の変更")) {
                    element->SetColor(value);
                }
            }

            int GetSortOrder() const
            {
                const Element* const element = FindOrWarn("描画順の読み取り");
                return element ? element->GetSortOrder() : 0;
            }

            void SetSortOrder(int value)
            {
                if (Element* const element = FindOrWarn("描画順の変更")) {
                    element->SetSortOrder(value);
                }
            }

            int GetAnchor() const
            {
                const Element* const element = FindOrWarn("アンカーの読み取り");
                return static_cast<int>(element ? element->GetAnchor() : UIAnchor::Center);
            }

            void SetAnchor(int value)
            {
                if (Element* const element = FindOrWarn("アンカーの変更")) {
                    element->SetAnchor(static_cast<UIAnchor>(value));
                }
            }

            float GetRotation() const
            {
                const Element* const element = FindOrWarn("回転の読み取り");
                return element ? element->GetUIRotation() : 0.0f;
            }

            void SetRotation(float value)
            {
                if (Element* const element = FindOrWarn("回転の変更")) {
                    element->SetUIRotation(value);
                }
            }

            /// @brief 持ち主の GameObject のハンドル（参照を 1 つ足して返す）
            ScriptGameObject* GetGameObject() const
            {
                owner_.AddRef();
                return &owner_;
            }

            // ---------------------------------------------------------------- UIText

            std::string GetText() const
            {
                const Element* const element = FindOrWarn("文字列の読み取り");
                return element ? element->GetText() : std::string();
            }

            void SetText(const std::string& value)
            {
                if (Element* const element = FindOrWarn("文字列の変更")) {
                    element->SetText(value);
                }
            }

            float GetFontSize() const
            {
                const Element* const element = FindOrWarn("文字の大きさの読み取り");
                return element ? element->GetFontSize() : 0.0f;
            }

            void SetFontSize(float value)
            {
                if (Element* const element = FindOrWarn("文字の大きさの変更")) {
                    element->SetFontSize(value);
                }
            }

            std::string GetFont() const
            {
                const Element* const element = FindOrWarn("フォントの読み取り");
                return element ? element->GetFontName() : std::string();
            }

            void SetFont(const std::string& value)
            {
                if (Element* const element = FindOrWarn("フォントの変更")) {
                    element->SetFontByName(value);
                }
            }

            Vector4 GetOutlineColor() const
            {
                const Element* const element = FindOrWarn("縁取りの色の読み取り");
                return element ? element->GetOutlineColor() : Vector4{};
            }

            void SetOutlineColor(const Vector4& value)
            {
                if (Element* const element = FindOrWarn("縁取りの色の変更")) {
                    element->SetOutline(value, element->GetOutlineWidth());
                }
            }

            float GetOutlineWidth() const
            {
                const Element* const element = FindOrWarn("縁取りの太さの読み取り");
                return element ? element->GetOutlineWidth() : 0.0f;
            }

            void SetOutlineWidth(float value)
            {
                if (Element* const element = FindOrWarn("縁取りの太さの変更")) {
                    element->SetOutline(element->GetOutlineColor(), value);
                }
            }

            void SetOutline(const Vector4& color, float width)
            {
                if (Element* const element = FindOrWarn("縁取りの変更")) {
                    element->SetOutline(color, width);
                }
            }

            int GetAlignH() const
            {
                const Element* const element = FindOrWarn("横の揃えの読み取り");
                return static_cast<int>(element ? element->GetAlignH() : TextAlignH::Left);
            }

            void SetAlignH(int value)
            {
                if (Element* const element = FindOrWarn("横の揃えの変更")) {
                    element->SetAlign(static_cast<TextAlignH>(value), element->GetAlignV());
                }
            }

            int GetAlignV() const
            {
                const Element* const element = FindOrWarn("縦の揃えの読み取り");
                return static_cast<int>(element ? element->GetAlignV() : TextAlignV::Top);
            }

            void SetAlignV(int value)
            {
                if (Element* const element = FindOrWarn("縦の揃えの変更")) {
                    element->SetAlign(element->GetAlignH(), static_cast<TextAlignV>(value));
                }
            }

            void SetAlign(int horizontal, int vertical)
            {
                if (Element* const element = FindOrWarn("揃えの変更")) {
                    element->SetAlign(static_cast<TextAlignH>(horizontal), static_cast<TextAlignV>(vertical));
                }
            }

            /// @brief 最後に頂点を組んだときの文字列を囲む大きさ（px）
            Vector2 GetMeasuredSize() const
            {
                const Element* const element = FindOrWarn("文字列の大きさの読み取り");
                return element ? element->GetMeasuredSize() : Vector2{};
            }

            // ---------------------------------------------------------------- UIImage

            Vector2 GetSize() const
            {
                const Element* const element = FindOrWarn("大きさの読み取り");
                return element ? element->GetSize() : Vector2{};
            }

            void SetSize(const Vector2& value)
            {
                if (Element* const element = FindOrWarn("大きさの変更")) {
                    element->SetSize(value);
                }
            }

            Vector2 GetTextureSize() const
            {
                const Element* const element = FindOrWarn("テクスチャの大きさの読み取り");
                return element ? element->GetTextureSize() : Vector2{};
            }

        private:
            ~ScriptUIElement() { owner_.Release(); }

            Element* Find() const
            {
                return dynamic_cast<Element*>(owner_.Resolve());
            }

            /// @brief UI 要素を引き、引けなければ 1 回だけ警告する
            Element* FindOrWarn(const char* action) const
            {
                Element* const element = Find();
                if (!element && !warned_) {
                    warned_ = true;
                    Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Script,
                        "{} で{}をしようとしましたが、GameObject が無いか {} ではありません",
                        kElementTypeName<Element>, action, kElementTypeName<Element>);
                }
                return element;
            }

            ScriptGameObject& owner_;
            mutable int refCount_ = 1;
            mutable bool warned_ = false;
        };

        using ScriptUIText = ScriptUIElement<UIText>;
        using ScriptUIImage = ScriptUIElement<UIImage>;

        /// @brief GameObject のハンドルから UI 要素のハンドルを作る
        /// @return 参照を 1 つ持ったハンドル
        template <class Element>
        ScriptUIElement<Element>* GetElement(ScriptGameObject& self)
        {
            return new ScriptUIElement<Element>(self);
        }

        /// @brief 作った UI 要素を指すハンドルを作る
        /// @return 参照を 1 つ持ったハンドル
        template <class Element>
        ScriptUIElement<Element>* WrapElement(const Element& element)
        {
            ScriptGameObject* const object = ScriptGameObject::CreateForObject(&element);
            if (!object) {
                return nullptr;
            }
            auto* const handle = new ScriptUIElement<Element>(*object);
            object->Release();
            return handle;
        }

        /// @brief 呼び出し元の GameObject が属する管理者（同じシーン）を引き、引けなければ警告する
        GameObjectManager* FindManager(const ScriptGameObject& self, const char* action)
        {
            const GameObject* const object = self.Resolve();
            GameObjectManager* const manager = object ? object->GetObjectManager() : nullptr;
            if (!manager) {
                Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Script,
                    "指す先の無い GameObject から{}をしようとしました", action);
            }
            return manager;
        }

        /// @brief 同じシーンへ UIImage を作る（シーンの保存には含めない）
        /// @return 参照を 1 つ持ったハンドル。作れなければ nullptr
        ScriptUIImage* SpawnUIImage(const std::string& texturePath, const std::string& name, const ScriptGameObject& self)
        {
            GameObjectManager* const manager = FindManager(self, "UIImage の生成");
            UIImage* const image = manager ? manager->AddObject(std::make_unique<UIImage>()) : nullptr;
            if (!image) {
                return nullptr;
            }
            image->Initialize(texturePath, name);
            image->SetSerializeEnabled(false);
            return WrapElement(*image);
        }

        /// @brief 同じシーンへ、名前で引いたフォントの UIText を作る（シーンの保存には含めない）
        /// @return 参照を 1 つ持ったハンドル。作れなければ nullptr
        ScriptUIText* SpawnUIText(const std::string& fontName, const std::string& text, const std::string& name,
                                  const ScriptGameObject& self)
        {
            GameObjectManager* const manager = FindManager(self, "UIText の生成");
            UIText* const label = manager ? manager->AddObject(std::make_unique<UIText>()) : nullptr;
            if (!label) {
                return nullptr;
            }
            label->SetFontByName(fontName);
            label->SetText(text);
            if (!name.empty()) {
                label->SetName(name);
            }
            label->Initialize();
            label->SetSerializeEnabled(false);
            return WrapElement(*label);
        }

        /// @brief 名前付きのフォントを登録する（`UIText.font` と `SpawnUIText` の fontName で引ける）
        void RegisterFont(const std::string& name, const std::string& filePath, const CScriptArray& systemFamilies,
                          const std::string& charset)
        {
            if (name.empty()) {
                if (asIScriptContext* const context = asGetActiveContext()) {
                    context->SetException("Font::Register には空でない名前を渡します");
                }
                return;
            }
            FontManager* const fonts = sEngineSystem ? sEngineSystem->GetService<FontManager>() : nullptr;
            if (!fonts) {
                Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Script,
                    "フォントの管理が見つからないので、フォント {} を登録できませんでした", name);
                return;
            }

            Logger& logger = Logger::GetInstance();
            MsdfFontDesc desc;
            if (!filePath.empty()) {
                desc.filePath = logger.Utf8ToPath(filePath).wstring();
            }
            desc.systemFamilyNames.reserve(systemFamilies.GetSize());
            for (asUINT i = 0; i < systemFamilies.GetSize(); ++i) {
                desc.systemFamilyNames.push_back(logger.Utf8ToWide(*static_cast<const std::string*>(systemFamilies.At(i))));
            }
            desc.charsetUtf8 = charset;
            fonts->RegisterNamedFont(name, desc);
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

        void RegisterTextAlign(BindingRegistrar& r)
        {
            r.Enum("TextAlignH");
            r.EnumValue("TextAlignH", "Left", static_cast<int>(TextAlignH::Left));
            r.EnumValue("TextAlignH", "Center", static_cast<int>(TextAlignH::Center));
            r.EnumValue("TextAlignH", "Right", static_cast<int>(TextAlignH::Right));
            r.Enum("TextAlignV");
            r.EnumValue("TextAlignV", "Top", static_cast<int>(TextAlignV::Top));
            r.EnumValue("TextAlignV", "Middle", static_cast<int>(TextAlignV::Middle));
            r.EnumValue("TextAlignV", "Bottom", static_cast<int>(TextAlignV::Bottom));
        }

        /// @brief UIText / UIImage に共通の型・配置・色の口を登録する
        template <class Element>
        void RegisterElement(BindingRegistrar& r, const char* type)
        {
            using Handle = ScriptUIElement<Element>;
            r.ReferenceType(type, asOBJ_REF);
            r.Behaviour(type, asBEHAVE_ADDREF, "void f()", asMETHOD(Handle, AddRef), asCALL_THISCALL);
            r.Behaviour(type, asBEHAVE_RELEASE, "void f()", asMETHOD(Handle, Release), asCALL_THISCALL);
            r.Method(type, "bool get_exists() const property", asMETHOD(Handle, Exists), asCALL_THISCALL);
            r.Method(type, "Vector2 get_anchoredPosition() const property", asMETHOD(Handle, GetAnchoredPosition), asCALL_THISCALL);
            r.Method(type, "void set_anchoredPosition(const Vector2 &in) property", asMETHOD(Handle, SetAnchoredPosition), asCALL_THISCALL);
            r.Method(type, "Vector2 get_pivot() const property", asMETHOD(Handle, GetPivot), asCALL_THISCALL);
            r.Method(type, "void set_pivot(const Vector2 &in) property", asMETHOD(Handle, SetPivot), asCALL_THISCALL);
            r.Method(type, "Vector4 get_color() const property", asMETHOD(Handle, GetColor), asCALL_THISCALL);
            r.Method(type, "void set_color(const Vector4 &in) property", asMETHOD(Handle, SetColor), asCALL_THISCALL);
            r.Method(type, "int get_sortOrder() const property", asMETHOD(Handle, GetSortOrder), asCALL_THISCALL);
            r.Method(type, "void set_sortOrder(int) property", asMETHOD(Handle, SetSortOrder), asCALL_THISCALL);
            r.Method(type, "UIAnchor get_anchor() const property", asMETHOD(Handle, GetAnchor), asCALL_THISCALL);
            r.Method(type, "void set_anchor(UIAnchor) property", asMETHOD(Handle, SetAnchor), asCALL_THISCALL);
            r.Method(type, "float get_rotation() const property", asMETHOD(Handle, GetRotation), asCALL_THISCALL);
            r.Method(type, "void set_rotation(float) property", asMETHOD(Handle, SetRotation), asCALL_THISCALL);
            r.Method(type, "GameObject@ get_gameObject() const property", asMETHOD(Handle, GetGameObject), asCALL_THISCALL);
        }

        void RegisterText(BindingRegistrar& r)
        {
            using Handle = ScriptUIText;
            RegisterElement<UIText>(r, "UIText");
            r.Method("UIText", "string get_text() const property", asMETHOD(Handle, GetText), asCALL_THISCALL);
            r.Method("UIText", "void set_text(const string &in) property", asMETHOD(Handle, SetText), asCALL_THISCALL);
            r.Method("UIText", "float get_fontSize() const property", asMETHOD(Handle, GetFontSize), asCALL_THISCALL);
            r.Method("UIText", "void set_fontSize(float) property", asMETHOD(Handle, SetFontSize), asCALL_THISCALL);
            r.Method("UIText", "string get_font() const property", asMETHOD(Handle, GetFont), asCALL_THISCALL);
            r.Method("UIText", "void set_font(const string &in) property", asMETHOD(Handle, SetFont), asCALL_THISCALL);
            r.Method("UIText", "Vector4 get_outlineColor() const property", asMETHOD(Handle, GetOutlineColor), asCALL_THISCALL);
            r.Method("UIText", "void set_outlineColor(const Vector4 &in) property", asMETHOD(Handle, SetOutlineColor), asCALL_THISCALL);
            r.Method("UIText", "float get_outlineWidth() const property", asMETHOD(Handle, GetOutlineWidth), asCALL_THISCALL);
            r.Method("UIText", "void set_outlineWidth(float) property", asMETHOD(Handle, SetOutlineWidth), asCALL_THISCALL);
            r.Method("UIText", "void SetOutline(const Vector4 &in color, float width)", asMETHOD(Handle, SetOutline), asCALL_THISCALL);
            r.Method("UIText", "TextAlignH get_alignH() const property", asMETHOD(Handle, GetAlignH), asCALL_THISCALL);
            r.Method("UIText", "void set_alignH(TextAlignH) property", asMETHOD(Handle, SetAlignH), asCALL_THISCALL);
            r.Method("UIText", "TextAlignV get_alignV() const property", asMETHOD(Handle, GetAlignV), asCALL_THISCALL);
            r.Method("UIText", "void set_alignV(TextAlignV) property", asMETHOD(Handle, SetAlignV), asCALL_THISCALL);
            r.Method("UIText", "void SetAlign(TextAlignH horizontal, TextAlignV vertical)", asMETHOD(Handle, SetAlign), asCALL_THISCALL);
            r.Method("UIText", "Vector2 get_measuredSize() const property", asMETHOD(Handle, GetMeasuredSize), asCALL_THISCALL);
            r.Method("GameObject", "UIText@ get_uiText() property", asFUNCTION(GetElement<UIText>), asCALL_CDECL_OBJLAST);
            r.Method("GameObject", "UIText@ SpawnUIText(const string &in fontName, const string &in text, const string &in name) const",
                asFUNCTION(SpawnUIText), asCALL_CDECL_OBJLAST);
        }

        void RegisterImage(BindingRegistrar& r)
        {
            using Handle = ScriptUIImage;
            RegisterElement<UIImage>(r, "UIImage");
            r.Method("UIImage", "Vector2 get_size() const property", asMETHOD(Handle, GetSize), asCALL_THISCALL);
            r.Method("UIImage", "void set_size(const Vector2 &in) property", asMETHOD(Handle, SetSize), asCALL_THISCALL);
            r.Method("UIImage", "Vector2 get_textureSize() const property", asMETHOD(Handle, GetTextureSize), asCALL_THISCALL);
            r.Method("GameObject", "UIImage@ get_uiImage() property", asFUNCTION(GetElement<UIImage>), asCALL_CDECL_OBJLAST);
            r.Method("GameObject", "UIImage@ SpawnUIImage(const string &in texturePath, const string &in name) const",
                asFUNCTION(SpawnUIImage), asCALL_CDECL_OBJLAST);
        }

        void RegisterFontFunctions(BindingRegistrar& r)
        {
            r.Namespace("Font");
            r.Function("void Register(const string &in name, const string &in filePath, const array<string> &in systemFamilies, "
                "const string &in charset = \"\")", asFUNCTION(RegisterFont));
            r.Namespace("");
        }
    }

    bool RegisterUIBinding(asIScriptEngine* engine, EngineSystem* engineSystem)
    {
        if (!engine) {
            return false;
        }
        sEngineSystem = engineSystem;
        BindingRegistrar r(engine);
        RegisterAnchor(r);
        RegisterTextAlign(r);
        RegisterText(r);
        RegisterImage(r);
        RegisterFontFunctions(r);
        return r.Succeeded();
    }
}
