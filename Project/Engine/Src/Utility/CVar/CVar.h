#pragma once

#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"
#include <cstdint>
#include <functional>
#include <type_traits>

/// @brief コンソール変数（CVar）
/// @details 「値の入れ物」を 1 箇所で宣言するだけで、ImGui のウィジェット・JSON への
///          自動保存・コンソールからの操作がすべて自動的に付いてくる仕組み。
///          仕掛けは単純で、コンストラクタが CVarRegistry へ自分を登録し、
///          UI 側とシリアライズ側がそのリストを走査しているだけ。
///
///          使い方（パラメータを定義したい場所に 1 行書く）:
///          @code
///          static CVar<float> cvIntensity{ "r.Vignette.Intensity", 0.8f,
///                                          "ヴィネット強度", CVarRange{0.0f, 2.0f} };
///          ...
///          float v = cvIntensity.Get();
///          @endcode
///
///          命名は "カテゴリ.グループ.名前" のドット区切り（UI がツリー化に使う）。
///          描画系は "r."、デバッグ表示は "d."、システムは "sys." を接頭辞にする。

namespace CoreEngine
{
    /// @brief CVar が保持する値の型
    enum class CVarType
    {
        Bool,
        Int,
        Float,
        Vector2, ///< スクロール速度・UVタイリング・風向など（Phase 5 水面 CVar 化で追加）
        Vector3,
        Color,   ///< Vector4（RGBA として ColorEdit4 で編集する）
    };

    /// @brief 数値 CVar の編集範囲
    /// @details 未指定（valid == false）の場合、UI はスライダーではなくドラッグ入力になる
    struct CVarRange
    {
        float min = 0.0f;
        float max = 0.0f;
        bool  valid = false;

        constexpr CVarRange() = default;
        constexpr CVarRange(float minValue, float maxValue)
            : min(minValue), max(maxValue), valid(true) {}
    };

    /// @brief CVar の振る舞いフラグ
    enum class CVarFlags : uint32_t
    {
        None   = 0,
        NoSave = 1 << 0,  ///< JSON へ保存しない（実行時状態など、復元すると事故になる値）
        /// @brief 自動生成 UI（CVarUI::DrawTree）に出さない
        /// @details 専用の UI が既にある項目に付ける。例えばポストエフェクトの
        ///          "r.<Effect>.Enabled" は Post Effects タブのトグルスイッチが担当するため、
        ///          これを付けないと同じ値を操作する UI が 2 つ並んでしまう
        NoUI   = 1 << 1,
    };

    constexpr CVarFlags operator|(CVarFlags a, CVarFlags b) noexcept
    {
        return static_cast<CVarFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    constexpr bool HasFlag(CVarFlags value, CVarFlags flag) noexcept
    {
        return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
    }

    // ──────────────────────────────────────────────────────────────
    // ICVar : 型を消した共通インターフェース
    // ──────────────────────────────────────────────────────────────

    /// @brief CVar の型非依存インターフェース
    /// @details UI とシリアライズはこの型だけを見て動くため、対応型を増やしても
    ///          呼び出し側（パネル・セクション）の構造は変わらない。
    class ICVar
    {
    public:
        virtual ~ICVar();

        ICVar(const ICVar&) = delete;
        ICVar& operator=(const ICVar&) = delete;

        const char* GetName()        const noexcept { return name_; }
        const char* GetDescription() const noexcept { return description_; }
        CVarType    GetType()        const noexcept { return type_; }
        CVarRange   GetRange()       const noexcept { return range_; }
        CVarFlags   GetFlags()       const noexcept { return flags_; }

        /// @brief 値が変更されるたびに増える通番
        /// @details 「前回見たときから変わったか」を安価に判定するために使う。
        ///          毎フレーム値を比較する代わりにこれを覚えておけばよい。
        uint32_t GetRevision() const noexcept { return revision_; }

        /// @brief 値変更時に呼ばれるコールバックを設定する
        /// @param callback 変更時に呼ぶ処理（定数バッファの更新など）
        /// @note コールバックが捕捉するオブジェクトは CVar より長生きさせること。
        ///       静的 CVar にインスタンスを捕捉させるのは危険なので、
        ///       通常は GetRevision() を使ったプル方式を推奨する。
        void SetOnChanged(std::function<void()> callback) { onChanged_ = std::move(callback); }

        /// @brief 値が外部（UI 等）から書き換えられたことを通知する
        void NotifyChanged();

        /// @brief 起動時のコードデフォルト値へ戻す
        virtual void ResetToDefault() = 0;

        /// @brief コードデフォルトから変更されているか
        /// @details UI で「触った項目」を強調表示するために使う
        virtual bool IsModified() const = 0;

        // ---- 型安全な生ポインタ取得（型が一致しない場合 nullptr） ----
        // ImGui へそのまま渡せるようにポインタで返す
        bool*    AsBool()    noexcept { return type_ == CVarType::Bool    ? static_cast<bool*>(storage_)    : nullptr; }
        int*     AsInt()     noexcept { return type_ == CVarType::Int     ? static_cast<int*>(storage_)     : nullptr; }
        float*   AsFloat()   noexcept { return type_ == CVarType::Float   ? static_cast<float*>(storage_)   : nullptr; }
        Vector2* AsVector2() noexcept { return type_ == CVarType::Vector2 ? static_cast<Vector2*>(storage_) : nullptr; }
        Vector3* AsVector3() noexcept { return type_ == CVarType::Vector3 ? static_cast<Vector3*>(storage_) : nullptr; }
        Vector4* AsColor()   noexcept { return type_ == CVarType::Color   ? static_cast<Vector4*>(storage_) : nullptr; }

        const bool*    AsBool()    const noexcept { return type_ == CVarType::Bool    ? static_cast<const bool*>(storage_)    : nullptr; }
        const int*     AsInt()     const noexcept { return type_ == CVarType::Int     ? static_cast<const int*>(storage_)     : nullptr; }
        const float*   AsFloat()   const noexcept { return type_ == CVarType::Float   ? static_cast<const float*>(storage_)   : nullptr; }
        const Vector2* AsVector2() const noexcept { return type_ == CVarType::Vector2 ? static_cast<const Vector2*>(storage_) : nullptr; }
        const Vector3* AsVector3() const noexcept { return type_ == CVarType::Vector3 ? static_cast<const Vector3*>(storage_) : nullptr; }
        const Vector4* AsColor()   const noexcept { return type_ == CVarType::Color   ? static_cast<const Vector4*>(storage_) : nullptr; }

    protected:
        ICVar(const char* name, const char* description, CVarType type,
              void* storage, CVarRange range, CVarFlags flags);

        /// @brief レジストリへ登録する（派生の値が初期化された後に呼ぶこと）
        void RegisterSelf();

    private:
        const char* name_ = "";
        const char* description_ = "";
        CVarType    type_ = CVarType::Float;
        void*       storage_ = nullptr;
        CVarRange   range_{};
        CVarFlags   flags_ = CVarFlags::None;
        uint32_t    revision_ = 0;
        bool        registered_ = false;
        std::function<void()> onChanged_;
    };

    // ──────────────────────────────────────────────────────────────
    // CVar<T> : 実際に値を持つ型
    // ──────────────────────────────────────────────────────────────

    namespace detail
    {
        template<class T> struct CVarTypeTraits;
        template<> struct CVarTypeTraits<bool>    { static constexpr CVarType kType = CVarType::Bool; };
        template<> struct CVarTypeTraits<int>     { static constexpr CVarType kType = CVarType::Int; };
        template<> struct CVarTypeTraits<float>   { static constexpr CVarType kType = CVarType::Float; };
        template<> struct CVarTypeTraits<Vector2> { static constexpr CVarType kType = CVarType::Vector2; };
        template<> struct CVarTypeTraits<Vector3> { static constexpr CVarType kType = CVarType::Vector3; };
        template<> struct CVarTypeTraits<Vector4> { static constexpr CVarType kType = CVarType::Color; };

        bool CVarValueEquals(bool a, bool b) noexcept;
        bool CVarValueEquals(int a, int b) noexcept;
        bool CVarValueEquals(float a, float b) noexcept;
        bool CVarValueEquals(const Vector2& a, const Vector2& b) noexcept;
        bool CVarValueEquals(const Vector3& a, const Vector3& b) noexcept;
        bool CVarValueEquals(const Vector4& a, const Vector4& b) noexcept;
    }

    /// @brief 型付きコンソール変数
    /// @tparam T bool / int / float / Vector3 / Vector4（色）のいずれか
    template<class T>
    class CVar final : public ICVar
    {
    public:
        /// @brief CVar を定義し、同時にレジストリへ登録する
        /// @param name        ドット区切りの一意な名前（例 "r.Vignette.Intensity"）
        /// @param defaultValue 初期値。ここがコードデフォルトになる
        /// @param description UI のツールチップに出る説明
        /// @param range       数値型の編集範囲（省略時はドラッグ入力）
        /// @param flags       保存/UI の抑止フラグ
        CVar(const char* name,
             const T& defaultValue,
             const char* description = "",
             CVarRange range = {},
             CVarFlags flags = CVarFlags::None)
            : ICVar(name, description, detail::CVarTypeTraits<T>::kType,
                    &value_, range, flags)
            , value_(defaultValue)
            , default_(defaultValue)
        {
            // 値の初期化が終わってから登録する（登録直後に他所から読まれても安全にするため）
            RegisterSelf();
        }

        /// @brief 現在値を取得する
        /// @note ホットループではこの値をローカルへ退避すること（毎フレーム 1 回の取得で十分）
        const T& Get() const noexcept { return value_; }

        /// @brief 値を設定する（実際に変化した場合のみ通知が走る）
        void Set(const T& newValue)
        {
            if (detail::CVarValueEquals(value_, newValue)) {
                return;
            }
            value_ = newValue;
            NotifyChanged();
        }

        /// @brief 起動時のコードデフォルト値
        const T& GetDefault() const noexcept { return default_; }

        void ResetToDefault() override { Set(default_); }

        bool IsModified() const override { return !detail::CVarValueEquals(value_, default_); }

        operator const T&() const noexcept { return value_; }

    private:
        T value_;
        T default_;
    };
}
