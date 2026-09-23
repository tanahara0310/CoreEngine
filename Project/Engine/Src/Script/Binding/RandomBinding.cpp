#include "pch.h"
#include "Script/Binding/RandomBinding.h"

#include "Script/Binding/BindingRegistrar.h"
#include "Utility/Random/RandomGenerator.h"

#include <angelscript.h>

#include <cstdint>
#include <random>

namespace CoreEngine::Script
{
    namespace
    {
        /// @brief 最小が最大より大きい範囲を渡されたらスクリプトの例外にする
        /// @return 範囲が正しければ true
        bool IsValidRange(bool valid)
        {
            if (valid) {
                return true;
            }
            if (asIScriptContext* const context = asGetActiveContext()) {
                context->SetException("乱数の範囲の最小が最大より大きい");
            }
            return false;
        }

        /// @brief 種を決めて同じ列を出す乱数（std::mt19937 と標準の一様分布をそのまま使う）
        class ScriptRandomStream
        {
        public:
            explicit ScriptRandomStream(std::uint32_t seed) : engine_(seed) {}

            ScriptRandomStream(const ScriptRandomStream&) = delete;
            ScriptRandomStream& operator=(const ScriptRandomStream&) = delete;

            void AddRef() const { ++refCount_; }

            void Release() const
            {
                if (--refCount_ == 0) {
                    delete this;
                }
            }

            std::uint32_t Next() { return engine_(); }

            /// @brief std::uniform_real_distribution<float>(minimum, maximum) で 1 つ出す
            float Uniform(float minimum, float maximum)
            {
                if (!IsValidRange(minimum <= maximum)) {
                    return minimum;
                }
                return std::uniform_real_distribution<float>(minimum, maximum)(engine_);
            }

            /// @brief std::uniform_int_distribution<int>(minimum, maximum) で 1 つ出す
            int UniformInt(int minimum, int maximum)
            {
                if (!IsValidRange(minimum <= maximum)) {
                    return minimum;
                }
                return std::uniform_int_distribution<int>(minimum, maximum)(engine_);
            }

        private:
            ~ScriptRandomStream() = default;

            std::mt19937 engine_;
            mutable int refCount_ = 1;
        };

        ScriptRandomStream* CreateRandomStream(std::uint32_t seed)
        {
            return new ScriptRandomStream(seed);
        }

        float RangeFloat(float minimum, float maximum)
        {
            if (!IsValidRange(minimum <= maximum)) {
                return minimum;
            }
            return RandomGenerator::GetInstance().GetFloat(minimum, maximum);
        }

        int RangeInt(int minimum, int maximum)
        {
            if (!IsValidRange(minimum <= maximum)) {
                return minimum;
            }
            return RandomGenerator::GetInstance().GetInt(minimum, maximum);
        }

        bool Chance(float probability)
        {
            return RandomGenerator::GetInstance().GetBool(probability);
        }
    }

    bool RegisterRandomBinding(asIScriptEngine* engine)
    {
        if (!engine) {
            return false;
        }
        BindingRegistrar r(engine);
        r.ReferenceType("RandomStream", asOBJ_REF);
        r.Behaviour("RandomStream", asBEHAVE_FACTORY, "RandomStream@ f(uint seed)", asFUNCTION(CreateRandomStream), asCALL_CDECL);
        r.Behaviour("RandomStream", asBEHAVE_ADDREF, "void f()", asMETHOD(ScriptRandomStream, AddRef), asCALL_THISCALL);
        r.Behaviour("RandomStream", asBEHAVE_RELEASE, "void f()", asMETHOD(ScriptRandomStream, Release), asCALL_THISCALL);
        r.Method("RandomStream", "uint Next()", asMETHOD(ScriptRandomStream, Next), asCALL_THISCALL);
        r.Method("RandomStream", "float Uniform(float minimum, float maximum)", asMETHOD(ScriptRandomStream, Uniform), asCALL_THISCALL);
        r.Method("RandomStream", "int UniformInt(int minimum, int maximum)", asMETHOD(ScriptRandomStream, UniformInt), asCALL_THISCALL);

        r.Namespace("Random");
        r.Function("float Range(float minimum, float maximum)", asFUNCTION(RangeFloat));
        r.Function("int Range(int minimum, int maximum)", asFUNCTION(RangeInt));
        r.Function("bool Chance(float probability = 0.5f)", asFUNCTION(Chance));
        r.Namespace("");
        return r.Succeeded();
    }
}
