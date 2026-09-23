#include "pch.h"
#include "Script/Binding/CameraShakeBinding.h"

#include "Camera/Shake/CameraShake.h"
#include "Camera/Shake/CameraShakePresets.h"
#include "Script/Binding/BindingRegistrar.h"

#include <new>
#include <string>
#include <type_traits>

namespace CoreEngine::Script
{
    namespace
    {
        static_assert(std::is_trivially_copyable_v<CameraShakeParams>);

        void ConstructParams(CameraShakeParams* self)
        {
            new (self) CameraShakeParams();
        }

        ShakeHandle PlayShake(const CameraShakeParams& params)
        {
            return CameraShake::Play(params);
        }

        ShakeHandle PlayShakeAt(const CameraShakeParams& params, const Vector3& worldOrigin)
        {
            return CameraShake::Play(params, worldOrigin);
        }

        ShakeHandle PlayPreset(const std::string& name, float scale)
        {
            return CameraShake::PlayPreset(name, scale);
        }

        void StopShake(ShakeHandle handle, float fadeOutSeconds)
        {
            CameraShake::Stop(handle, fadeOutSeconds);
        }

        void StopAllShakes(float fadeOutSeconds)
        {
            CameraShake::StopAll(fadeOutSeconds);
        }

        void AddTrauma(float amount)
        {
            CameraShake::AddTrauma(amount);
        }

        CameraShakeParams PresetHit() { return CameraShakePresets::Hit(); }
        CameraShakeParams PresetHeavyHit() { return CameraShakePresets::HeavyHit(); }
        CameraShakeParams PresetExplosion() { return CameraShakePresets::Explosion(); }
        CameraShakeParams PresetLanding() { return CameraShakePresets::Landing(); }
        CameraShakeParams PresetRecoil() { return CameraShakePresets::Recoil(); }
        CameraShakeParams PresetEarthquake() { return CameraShakePresets::Earthquake(); }
        CameraShakeParams PresetHandheld() { return CameraShakePresets::Handheld(); }
        CameraShakeParams PresetRumble() { return CameraShakePresets::Rumble(); }

        void RegisterEnums(BindingRegistrar& r)
        {
            r.Enum("ShakeWaveform");
            r.EnumValue("ShakeWaveform", "Perlin", static_cast<int>(ShakeWaveform::Perlin));
            r.EnumValue("ShakeWaveform", "Random", static_cast<int>(ShakeWaveform::Random));
            r.EnumValue("ShakeWaveform", "Sine", static_cast<int>(ShakeWaveform::Sine));
            r.EnumValue("ShakeWaveform", "Kick", static_cast<int>(ShakeWaveform::Kick));
            r.Enum("ShakeSpace");
            r.EnumValue("ShakeSpace", "CameraLocal", static_cast<int>(ShakeSpace::CameraLocal));
            r.EnumValue("ShakeSpace", "World", static_cast<int>(ShakeSpace::World));
            r.Enum("ShakeTimeMode");
            r.EnumValue("ShakeTimeMode", "Scaled", static_cast<int>(ShakeTimeMode::Scaled));
            r.EnumValue("ShakeTimeMode", "Unscaled", static_cast<int>(ShakeTimeMode::Unscaled));
        }

        void RegisterParams(BindingRegistrar& r)
        {
            const char* const type = "CameraShakeParams";
            r.ValueType(type, sizeof(CameraShakeParams), asOBJ_VALUE | asOBJ_POD | asGetTypeTraits<CameraShakeParams>());
            r.Behaviour(type, asBEHAVE_CONSTRUCT, "void f()", asFUNCTION(ConstructParams), asCALL_CDECL_OBJLAST);
            r.Property(type, "Vector3 positionAmplitude", asOFFSET(CameraShakeParams, positionAmplitude));
            r.Property(type, "Vector3 rotationAmplitude", asOFFSET(CameraShakeParams, rotationAmplitude));
            r.Property(type, "float fovAmplitude", asOFFSET(CameraShakeParams, fovAmplitude));
            r.Property(type, "float frequency", asOFFSET(CameraShakeParams, frequency));
            r.Property(type, "Vector3 frequencyScale", asOFFSET(CameraShakeParams, frequencyScale));
            r.Property(type, "float duration", asOFFSET(CameraShakeParams, duration));
            r.Property(type, "float attack", asOFFSET(CameraShakeParams, attack));
            r.Property(type, "EaseType decayEase", asOFFSET(CameraShakeParams, decayEase));
            r.Property(type, "Vector3 direction", asOFFSET(CameraShakeParams, direction));
            r.Property(type, "float directionality", asOFFSET(CameraShakeParams, directionality));
            r.Property(type, "bool useWorldFalloff", asOFFSET(CameraShakeParams, useWorldFalloff));
            r.Property(type, "float innerRadius", asOFFSET(CameraShakeParams, innerRadius));
            r.Property(type, "float outerRadius", asOFFSET(CameraShakeParams, outerRadius));
            r.Property(type, "EaseType falloffEase", asOFFSET(CameraShakeParams, falloffEase));
            r.Property(type, "ShakeWaveform waveform", asOFFSET(CameraShakeParams, waveform));
            r.Property(type, "ShakeSpace space", asOFFSET(CameraShakeParams, space));
            r.Property(type, "ShakeTimeMode timeMode", asOFFSET(CameraShakeParams, timeMode));
            r.Property(type, "uint seed", asOFFSET(CameraShakeParams, seed));
        }

        void RegisterFunctions(BindingRegistrar& r)
        {
            r.Namespace("CameraShake");
            r.Function("uint Play(const CameraShakeParams &in params)", asFUNCTION(PlayShake));
            r.Function("uint Play(const CameraShakeParams &in params, const Vector3 &in worldOrigin)", asFUNCTION(PlayShakeAt));
            r.Function("uint PlayPreset(const string &in name, float scale = 1.0f)", asFUNCTION(PlayPreset));
            r.Function("void Stop(uint handle, float fadeOutSeconds = 0.0f)", asFUNCTION(StopShake));
            r.Function("void StopAll(float fadeOutSeconds = 0.0f)", asFUNCTION(StopAllShakes));
            r.Function("void AddTrauma(float amount)", asFUNCTION(AddTrauma));

            r.Namespace("CameraShakePresets");
            r.Function("CameraShakeParams Hit()", asFUNCTION(PresetHit));
            r.Function("CameraShakeParams HeavyHit()", asFUNCTION(PresetHeavyHit));
            r.Function("CameraShakeParams Explosion()", asFUNCTION(PresetExplosion));
            r.Function("CameraShakeParams Landing()", asFUNCTION(PresetLanding));
            r.Function("CameraShakeParams Recoil()", asFUNCTION(PresetRecoil));
            r.Function("CameraShakeParams Earthquake()", asFUNCTION(PresetEarthquake));
            r.Function("CameraShakeParams Handheld()", asFUNCTION(PresetHandheld));
            r.Function("CameraShakeParams Rumble()", asFUNCTION(PresetRumble));
            r.Namespace("");
        }
    }

    bool RegisterCameraShakeBinding(asIScriptEngine* engine)
    {
        if (!engine) {
            return false;
        }
        BindingRegistrar r(engine);
        RegisterEnums(r);
        RegisterParams(r);
        RegisterFunctions(r);
        return r.Succeeded();
    }
}
