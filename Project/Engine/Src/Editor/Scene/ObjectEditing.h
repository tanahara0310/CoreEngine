#pragma once

#ifdef CORE_EDITOR

#include "Math/Vector/Vector3.h"

#include <functional>
#include <string>

namespace CoreEngine
{
    class GameObject;
    class GameObjectManager;

    /// @brief エディタからオブジェクトを作る・複製する・消す・名前と有効を変える操作（どれも Undo に積む）
    namespace ObjectEditing
    {
        /// @brief 操作に要るもの
        struct Context
        {
            /// @brief 操作するシーンのオブジェクト
            GameObjectManager* manager = nullptr;

            /// @brief オブジェクトに削除の印を付ける直前に呼ぶ（選択の解除などに使う）
            std::function<void(const GameObject&)> beforeDestroy;
        };

        /// @brief エディタで新しく作るオブジェクトの保存キー（ファイル名と ID の元）を作る
        /// @details 名前の英数字と `-` を残し、ほかの人が作ったものと重ならない印を付ける
        ///          （例：「Enemy (1)」→「Enemy_1_7f3a91c2」）。
        std::string MakeNewObjectKey(const std::string& name);

        /// @brief 複製・削除できるか
        /// @param reason できないときの理由を書く先（要らなければ nullptr）
        /// @note 保存形から作り直せるものだけを扱う。コードが付けたコンポーネントを持つものは、
        ///       コードが作ったオブジェクトとみなして扱わない。
        bool CanDuplicateOrDelete(const GameObject& object, std::string* reason = nullptr);

        /// @brief 作る UI の種類
        enum class UIElementKind
        {
            Text,   ///< UI テキスト
            Image,  ///< UI 画像
        };

        /// @brief 作るパーティクルの種類
        enum class ParticleKind
        {
            Cpu,    ///< パーティクル（CPU で更新）
            Gpu,    ///< GPU パーティクル
        };

        /// @brief Transform だけを持つ空のオブジェクトを作る
        /// @return 作ったオブジェクト（作れなければ nullptr）
        GameObject* CreateEmpty(const Context& context, const Vector3& position);

        /// @brief Transform だけを持つ、名前のとおりのオブジェクトを作る（同じ名前があっても番号を足さない）
        /// @return 作ったオブジェクト（作れなければ nullptr）
        GameObject* CreateNamed(const Context& context, const std::string& name, const Vector3& position);

        /// @brief プレハブからオブジェクトを作る
        /// @param prefabPath プロジェクトの根からの相対パス
        /// @return 作ったオブジェクト（プレハブを読めなければ nullptr）
        GameObject* InstantiatePrefab(const Context& context, const std::string& prefabPath, const std::string& name);

        /// @brief Transform とパーティクルを持つオブジェクトを作る
        /// @return 作ったオブジェクト（作れなければ nullptr）
        GameObject* CreateParticle(const Context& context, ParticleKind kind, const Vector3& position);

        /// @brief UI トランスフォームと、UI テキストか UI 画像を持つオブジェクトを画面の中央に作る
        /// @return 作ったオブジェクト（作れなければ nullptr）
        GameObject* CreateUI(const Context& context, UIElementKind kind);

        /// @brief オブジェクトを複製する
        /// @details 名前は「名前 (1)」にし、位置は 3D なら X へ 1、UI なら右下へ 10px ずらす。
        /// @return 複製したオブジェクト（複製できなければ nullptr）
        GameObject* Duplicate(const Context& context, const GameObject& source);

        /// @brief オブジェクトを消す
        /// @return 消したら true
        /// @note Undo で、同じ ID・保存キー・値のオブジェクトを作り直す。
        bool Delete(const Context& context, GameObject& object);

        /// @brief オブジェクトを、Transform の子孫ごと消す（1 回の Undo で戻る）
        /// @return 消したら true（消せないものが 1 つでもあれば何も消さない）
        bool DeleteWithDescendants(const Context& context, GameObject& object);

        /// @brief 名前を after にし、before からの変更として Undo に積む
        /// @param before 変える前の名前（入力欄で打っている間に書き換えた分を 1 回の操作にまとめるときは、打ち始める前の名前）
        /// @return 積んだら true（before と after が同じなら積まない）
        bool Rename(GameObject& object, const std::string& before, const std::string& after);

        /// @brief 有効・無効を切り替えて Undo に積む
        /// @return 積んだら true（今と同じなら何もしない）
        bool SetActive(GameObject& object, bool active);
    }
}

#endif // CORE_EDITOR
