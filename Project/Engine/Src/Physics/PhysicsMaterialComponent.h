#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Reflection/Reflect.h"

namespace CoreEngine
{
/// @brief 2 つの物体の係数から 1 つを作る規則
enum class MaterialCombine : int {
    Average,   ///< 平均
    Minimum,   ///< 小さい方
    Maximum,   ///< 大きい方
    Multiply,  ///< 掛け算
    Count,
};

/// @brief 接触したときの跳ね返りと滑りにくさ
/// @details 剛体を持たない床や壁にも付けられる。付いていなければ既定値を使う。
class PhysicsMaterialComponent : public IComponent {
public:
    /// @brief 合成規則の名前（`MaterialCombine` の並び）
    static constexpr const char* kCombineNames[] = { "平均", "小さい方", "大きい方", "掛け算" };

    /// @brief 材質が付いていないときに使う値
    static constexpr float kDefaultRestitution = 0.0f;
    static constexpr float kDefaultFriction = 0.6f;

    const char* GetTypeName() const override { return "PhysicsMaterial"; }

    REFLECT_BEGIN(PhysicsMaterialComponent, "物理マテリアル")
        REFLECT_PROPERTY(restitution_, "反発係数", p.range = Range(0.0f, 1.0f, 0.01f),
            p.tooltip = "0 で跳ねず、1 で高さが落ちない")
        REFLECT_PROPERTY(friction_, "摩擦係数", p.range = Range(0.0f, 2.0f, 0.01f),
            p.tooltip = "0 で滑り続け、大きいほど早く止まる")
        REFLECT_ENUM_ACCESSOR("restitutionCombine", "反発の合成", GetRestitutionCombine,
            SetRestitutionCombine, kCombineNames)
        REFLECT_ENUM_ACCESSOR("frictionCombine", "摩擦の合成", GetFrictionCombine,
            SetFrictionCombine, kCombineNames)
    REFLECT_END()

    // ===== 係数 =====

    float GetRestitution() const { return restitution_; }
    void SetRestitution(float restitution) { restitution_ = restitution; }

    float GetFriction() const { return friction_; }
    void SetFriction(float friction) { friction_ = friction; }

    MaterialCombine GetRestitutionCombine() const { return restitutionCombine_; }
    void SetRestitutionCombine(MaterialCombine combine) { restitutionCombine_ = combine; }

    MaterialCombine GetFrictionCombine() const { return frictionCombine_; }
    void SetFrictionCombine(MaterialCombine combine) { frictionCombine_ = combine; }

    // ===== 合成 =====

    /// @brief 2 つの値を規則に従って 1 つにする
    static float Combine(float a, float b, MaterialCombine rule);

    /// @brief 2 つの材質から反発係数を作る（材質が無ければ既定値を使う）
    /// @note 規則は両側の指定のうち順序の大きい方を採る。
    static float CombineRestitution(const PhysicsMaterialComponent* a,
                                    const PhysicsMaterialComponent* b);

    /// @brief 2 つの材質から摩擦係数を作る（材質が無ければ既定値を使う）
    static float CombineFriction(const PhysicsMaterialComponent* a,
                                 const PhysicsMaterialComponent* b);

private:
    float           restitution_ = kDefaultRestitution;
    float           friction_ = kDefaultFriction;
    MaterialCombine restitutionCombine_ = MaterialCombine::Maximum;
    MaterialCombine frictionCombine_ = MaterialCombine::Multiply;
};
}
