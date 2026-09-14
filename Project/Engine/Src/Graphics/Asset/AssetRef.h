#pragma once

#include "Graphics/Asset/AssetType.h"
#include "Reflection/PropertyDescriptor.h"

#include <string>
#include <string_view>

namespace CoreEngine
{
    struct AssetInfo;

    /// @brief `AssetRef` がテクスチャを指すことを表す型
    struct TextureAsset { static constexpr AssetType kType = AssetType::Texture; };

    /// @brief `AssetRef` がモデルを指すことを表す型
    struct ModelAsset { static constexpr AssetType kType = AssetType::Model; };

    /// @brief `AssetRef` が音声を指すことを表す型
    struct AudioAsset { static constexpr AssetType kType = AssetType::Audio; };

    /// @brief アセットの、プロジェクトの根からの相対パス（区切りは `/`）
    std::string ToAssetPath(const AssetInfo& info);

    /// @brief パスかファイル名でアセットを引く
    /// @param pathOrName `Application/Assets/…` のような相対パス、絶対パス、またはファイル名（UTF-8）
    /// @return 見つからなければ nullptr
    const AssetInfo* FindAssetInfo(std::string_view pathOrName);

    /// @brief 参照の値からアセットを引く（GUID で引けなければパスで引く）
    /// @return 見つからなければ nullptr
    const AssetInfo* ResolveAssetRef(const Reflection::AssetRefValue& value);

    /// @brief ディスク上のアセットを GUID とパスで指す参照の、型に依らない部分
    class AssetRefBase
    {
    public:
        /// @brief 控えている GUID（無ければ空）
        const std::string& GetGuid() const noexcept { return guid_; }

        /// @brief 指す先のパス
        /// @return 引ければその今のパス、引けなければ控えているパス
        std::string GetPath() const;

        /// @brief 何かを指しているか
        bool IsSet() const noexcept { return !guid_.empty() || !path_.empty(); }

        /// @brief 指せるアセットの種類
        AssetType GetAssetType() const noexcept { return type_; }

        /// @brief 型記述子とやり取りする値にする（控えている GUID とパスそのまま）
        Reflection::AssetRefValue GetValue() const { return Reflection::AssetRefValue{ guid_, path_ }; }

        /// @brief 型記述子から受け取った値を設定する
        void SetValue(const Reflection::AssetRefValue& value);

        /// @brief 何も指さない状態に戻す
        void Reset();

    protected:
        explicit AssetRefBase(AssetType type) noexcept : type_(type) {}

        /// @brief パスかファイル名で指す（引ければ GUID と今のパスを控える）
        void AssignPath(std::string_view pathOrName);

    private:
        AssetType   type_ = AssetType::Unknown;
        std::string guid_;
        std::string path_;
    };

    /// @brief `T` が表す種類のアセットを指す参照（`T` は `AudioAsset` など）
    /// @details 型記述子に `REFLECT_ASSET_REF` で載せると `{"guid": …, "path": …}` で保存され、
    ///          インスペクタで指す先を選べる。
    template <class T>
    class AssetRef final : public AssetRefBase
    {
    public:
        static constexpr AssetType kAssetType = T::kType;

        AssetRef() noexcept : AssetRefBase(kAssetType) {}

        /// @brief パスかファイル名で指す
        explicit AssetRef(std::string_view pathOrName) : AssetRefBase(kAssetType) { AssignPath(pathOrName); }

        /// @brief パスかファイル名で指し直す（空なら何も指さない）
        void SetPath(std::string_view pathOrName) { AssignPath(pathOrName); }
    };
}
