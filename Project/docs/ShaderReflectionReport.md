# Shader Reflection Automation Report

## Overview

This document summarizes the automation status of the shader reflection system.

---

## Automated Features

### 1. Automatic Root Signature Construction
Automatically detects resource bindings from shader reflection and builds RootSignature.

### 2. Automatic Root Parameter Index Mapping
Automatically retrieves root parameter index from resource name.
```cpp
int idx = renderer->GetRootParamIndex("gMaterial");
```

### 3. Automatic Input Layout Setup
Use `SetInputLayoutFromReflection()` to automatically set input layout from shader.

### 4. Semantic-Based Slot Auto Detection (NEW)
Skinning-related semantics like WEIGHT/INDEX are automatically assigned to slot 1.

### 5. Constant Buffer Size Validation
Automatically validates that C++ struct size matches HLSL struct size.

---

## Renderer Automation Status

| Renderer | InputLayout | Auto Slot | CBV Validation | Status |
|----------|:-----------:|:---------:|:--------------:|--------|
| ModelRenderer | YES | - | YES | **Fully Automated** |
| SkinnedModelRenderer | YES | Slot1 Auto | YES | **Fully Automated** (NEW) |
| SpriteRenderer | YES | - | Pending | Fully Automated |
| TextRenderer | YES | - | Pending | Fully Automated |
| SkyBoxRenderer | YES | - | Pending | Fully Automated |
| ParticleRenderer | YES | - | N/A | Fully Automated |
| ModelParticleRenderer | YES | - | N/A | **Fully Automated** (NEW) |
| LineRendererPipeline | YES | - | Pending | Fully Automated |
| ShadowMapRenderer (Normal) | YES | - | Pending | **Fully Automated** (NEW) |
| ShadowMapRenderer (Skinned) | YES | Slot1 Auto | Pending | **Fully Automated** (NEW) |
| PostEffectBase | N/A | N/A | Pending | No InputLayout |

**Legend:**
- YES: Fully automated
- Pending: Validation can be added
- N/A: Not applicable
- (NEW): Automated in this update

---

## Slot Auto Detection Rules

Input slots are automatically determined based on semantic names:

| Semantic | Auto-Assigned Slot |
|----------|-------------------|
| WEIGHT, INDEX, BONEINDEX, BONEWEIGHT, BLENDWEIGHT, BLENDINDICES | **Slot 1** |
| INSTANCE, INSTANCEID, INSTANCEDATA | **Slot 2** |
| Others (POSITION, TEXCOORD, NORMAL, TANGENT, etc.) | **Slot 0** |

---

## Shader Resource Details

### ModelRenderer
```
Summary: CBV=4  SRV=14  UAV=0  Sampler=2  InputLayout=4

[Constant Buffers]
  - gTransformationMatrix -> b0 (256 bytes) [VS] - Validated
  - gMaterial -> b0 (176 bytes) [PS] - Validated
  - gCamera -> b2 (16 bytes) [PS]
  - gLightCounts -> b1 (16 bytes) [PS]

[Input Layout]
  - POSITION0 (Slot:0, R32G32B32A32_FLOAT)
  - TEXCOORD0 (Slot:0, R32G32_FLOAT)
  - NORMAL0 (Slot:0, R32G32B32_FLOAT)
  - TANGENT0 (Slot:0, R32G32B32_FLOAT)

Root Parameters: 18
```

### SkinnedModelRenderer
```
Summary: CBV=4  SRV=15  UAV=0  Sampler=2  InputLayout=6

[Constant Buffers]
  - gTransformationMatrix -> b0 (256 bytes) [VS] - Validated
  - gMaterial -> b0 (176 bytes) [PS] - Validated
  - gCamera -> b2 (16 bytes) [PS]
  - gLightCounts -> b1 (16 bytes) [PS]

[Input Layout] - Auto Slot Detection
  - POSITION0 (Slot:0, R32G32B32A32_FLOAT)
  - TEXCOORD0 (Slot:0, R32G32_FLOAT)
  - NORMAL0 (Slot:0, R32G32B32_FLOAT)
  - TANGENT0 (Slot:0, R32G32B32_FLOAT)
  - WEIGHT0 (Slot:1, R32G32B32A32_FLOAT) <- Auto-detected
  - INDEX0 (Slot:1, R32G32B32A32_SINT) <- Auto-detected

Root Parameters: 19
```

### ShadowMapRenderer
```
Normal Model:
  - POSITION0 only (Slot:0)

Skinned Model (Auto Slot Detection):
  - POSITION0 (Slot:0)
  - TEXCOORD0 (Slot:0)
  - NORMAL0 (Slot:0)
  - WEIGHT0 (Slot:1) <- Auto-detected
  - INDEX0 (Slot:1) <- Auto-detected

Root Parameters: 2
```

### ParticleRenderer / ModelParticleRenderer
```
Summary: CBV=0  SRV=2  UAV=0  Sampler=1  InputLayout=3

[Input Layout]
  - POSITION0 (Slot:0, R32G32B32A32_FLOAT)
  - TEXCOORD0 (Slot:0, R32G32_FLOAT)
  - NORMAL0 (Slot:0, R32G32B32_FLOAT)

Root Parameters: 2
```

---

## PostEffect CBV List

| Effect | CBV Name | Size |
|--------|----------|------|
| FadeEffect | FadeParams | 48 bytes |
| Blur | BlurParams | 16 bytes |
| RadialBlur | RadialBlurParams | 16 bytes |
| Shockwave | ShockwaveParams | 32 bytes |
| Vignette | VignetteParams | 16 bytes |
| ColorGrading | ColorGradingParams | 80 bytes |
| ChromaticAberration | ChromaticAberrationParams | 64 bytes |
| Sepia | SepiaParams | 16 bytes |
| RasterScroll | RasterScrollParams | 32 bytes |
| Bloom | BloomParams | 16 bytes |
| Dissolve | DissolveParams | 32 bytes |

---

## C++ Struct Size Reference

| Struct Name | Size | Corresponding CBV |
|-------------|------|-------------------|
| TransformationMatrix | 256 bytes | gTransformationMatrix |
| MaterialConstants | 176 bytes | gMaterial (Model) |
| SpriteMaterial | 80 bytes | gMaterial (Sprite) |
| LineCameraConstants | 64 bytes | Camera |

---

## Usage Examples

### Automatic Input Layout Setup
```cpp
psoMg_->CreateBuilder()
    .SetInputLayoutFromReflection(*reflectionData_)
    .SetRasterizer(D3D12_CULL_MODE_BACK, D3D12_FILL_MODE_SOLID)
    .BuildAllBlendModes(device, vs, ps, rootSignature);
```

### CBV Size Validation
```cpp
reflectionData_->ValidateAllCBVSizes({
    {"gTransformationMatrix", sizeof(TransformationMatrix)},
    {"gMaterial", sizeof(MaterialConstants)}
});
```

### Get Root Parameter Index
```cpp
int materialIdx = renderer->GetRootParamIndex("gMaterial");
cmdList->SetGraphicsRootConstantBufferView(materialIdx, materialBuffer->GetGPUVirtualAddress());
```

---

*Generated by Shader Reflection System*
*Last Updated: 2026-02-19*
