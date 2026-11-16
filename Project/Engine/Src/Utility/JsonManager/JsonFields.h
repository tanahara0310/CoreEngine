#pragma once

#include "Utility/Enum/EnumNames.h"
#include "Utility/JsonManager/JsonManager.h"

#include <type_traits>
#include <utility>

/// @file
/// @brief 構造体の項目の並びを 1 回だけ書き、JSON の書き出しと読み込みの両方に通す補助
/// @details 項目の並びは `[](auto& object, auto&& field) { field("保存キー", object.member, 読むときの補足...); }`
///          の形で書く。WriteFields は const の object を、ReadFields は書き換えられる object を渡して同じ並びを通す。
///          読むときの補足は書き出しでは使わない。補足ごとの読み方は次のとおり。
///          - 補足なし：無い・null・型が違うときは今の値を残す
///          - (count, fallback)：番号で保存した列挙を読み、0〜count-1 の外なら fallback にする
///          - kIfPresent：欄があるときだけ FromJsonValue で読む（null・型の違いは FromJsonValue のとおり）
///          - IfMissing(value)：無い・null・型が違うときは value にする
///
/// @code
///     constexpr auto kFields = [](auto& data, auto&& field) {
///         field("speed", data.speed);
///         field("mode", data.mode, kModeCount, Mode::Default);
///     };
///     JsonFields::WriteFields(out, data, kFields);
///     JsonFields::ReadFields(in, data, kFields);
/// @endcode

namespace CoreEngine::JsonFields
{
    /// @brief 欄があるときだけ読む印の型
    struct IfPresentTag {};

    /// @brief 欄があるときだけ読む（読むときの補足）
    inline constexpr IfPresentTag kIfPresent{};

    /// @brief 無い・null・型が違うときに入れる値（読むときの補足）
    template <class T>
    struct MissingValue
    {
        T value;
    };

    /// @brief 無い・null・型が違うときに入れる値を補足にする
    template <class T>
    MissingValue<T> IfMissing(T value)
    {
        return MissingValue<T>{ std::move(value) };
    }

    /// @brief 項目の値を JSON の値にする（列挙は int・Vector3 / Vector4 / Quaternion は配列・ほかはそのまま）
    template <class T>
    json ToJsonValue(const T& value)
    {
        if constexpr (std::is_enum_v<T>) {
            return static_cast<int>(value);
        } else if constexpr (std::is_same_v<T, Vector3>) {
            return JsonManager::Vector3ToJson(value);
        } else if constexpr (std::is_same_v<T, Vector4>) {
            return JsonManager::Vector4ToJson(value);
        } else if constexpr (std::is_same_v<T, Quaternion>) {
            return JsonManager::QuaternionToJson(value);
        } else {
            return value;
        }
    }

    /// @brief JSON の値を項目の型で読む（列挙は int・Vector3 / Vector4 / Quaternion は配列・ほかは get）
    /// @note 型が違えば get が例外を投げる。配列でない・要素が足りないベクトルは 0 になる
    template <class T>
    T FromJsonValue(const json& value)
    {
        if constexpr (std::is_enum_v<T>) {
            return static_cast<T>(value.get<int>());
        } else if constexpr (std::is_same_v<T, Vector3>) {
            return JsonManager::JsonToVector3(value);
        } else if constexpr (std::is_same_v<T, Vector4>) {
            return JsonManager::JsonToVector4(value);
        } else if constexpr (std::is_same_v<T, Quaternion>) {
            return JsonManager::JsonToQuaternion(value);
        } else {
            return value.get<T>();
        }
    }

    /// @brief 欄の値を読む（無い・null・型が違うときは今の値を残す）
    template <class T>
    void ReadValue(const json& in, const char* key, T& value)
    {
        if constexpr (std::is_enum_v<T>) {
            value = static_cast<T>(JsonManager::SafeGet(in, key, static_cast<int>(value)));
        } else if constexpr (std::is_same_v<T, Vector3>) {
            value = JsonManager::SafeGetVector3(in, key, value);
        } else if constexpr (std::is_same_v<T, Vector4>) {
            value = JsonManager::SafeGetVector4(in, key, value);
        } else if constexpr (std::is_same_v<T, Quaternion>) {
            value = JsonManager::SafeGetQuaternion(in, key, value);
        } else {
            value = JsonManager::SafeGet(in, key, value);
        }
    }

    /// @brief 番号で保存した列挙を読む（無い・null・型が違うときは今の番号・0〜count-1 の外なら fallback）
    template <class E>
        requires std::is_enum_v<E>
    void ReadValue(const json& in, const char* key, E& value, int count, E fallback)
    {
        value = ToEnum(JsonManager::SafeGet(in, key, static_cast<int>(value)), count, fallback);
    }

    /// @brief 欄があるときだけ FromJsonValue で読む
    template <class T>
    void ReadValue(const json& in, const char* key, T& value, IfPresentTag)
    {
        if (in.contains(key)) {
            value = JsonFields::FromJsonValue<T>(in[key]);
        }
    }

    /// @brief 欄の値を読む（無い・null・型が違うときは missing の値）
    template <class T, class U>
    void ReadValue(const json& in, const char* key, T& value, const MissingValue<U>& missing)
    {
        value = missing.value;
        JsonFields::ReadValue(in, key, value);
    }

    /// @brief 項目の並びを通して、object の項目を out の欄へ書く
    /// @param fields `void(const Object&, field)`。field は `(保存キー, 値, 読むときの補足...)` で呼ぶ
    template <class Object, class Fields>
    void WriteFields(json& out, const Object& object, Fields&& fields)
    {
        fields(object, [&out](const char* key, const auto& value, const auto&...) {
            out[key] = JsonFields::ToJsonValue(value);
        });
    }

    /// @brief 項目の並びを通して、in の欄から object の項目を読む（読み方は項目ごとの補足で決まる）
    /// @param fields `void(Object&, field)`。field は `(保存キー, 値, 読むときの補足...)` で呼ぶ
    template <class Object, class Fields>
    void ReadFields(const json& in, Object& object, Fields&& fields)
    {
        fields(object, [&in](const char* key, auto& value, const auto&... extra) {
            JsonFields::ReadValue(in, key, value, extra...);
        });
    }

    /// @brief 項目の並びを通して、object を新しい JSON のオブジェクトにする
    template <class Object, class Fields>
    json ToJsonObject(const Object& object, Fields&& fields)
    {
        json out;
        JsonFields::WriteFields(out, object, fields);
        return out;
    }

    /// @brief 項目の並びを通して、in の欄を既定の値の Object へ読む
    template <class Object, class Fields>
    Object FromJsonObject(const json& in, Fields&& fields)
    {
        Object object{};
        JsonFields::ReadFields(in, object, fields);
        return object;
    }
}
