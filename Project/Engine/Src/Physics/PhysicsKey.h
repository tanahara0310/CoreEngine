#pragma once

namespace CoreEngine
{
    class PhysicsWorld;
    class ContactSolver;
    class PhysicsFeature;

    /// @brief 物理の内部の操作（積分・眠り・ぶつかった強さ・物理が扱う印）を呼ぶための鍵
    /// @note 作れるのは PhysicsWorld・ContactSolver・PhysicsFeature だけ。
    class PhysicsKey {
        friend class PhysicsWorld;
        friend class ContactSolver;
        friend class PhysicsFeature;
        PhysicsKey() = default;
    };
}
