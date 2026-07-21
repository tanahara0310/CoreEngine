Texture2D<float4> gHeightDisplacementXSpectrum : register(t0);
Texture2D<float4> gDisplacementZSpectrum : register(t1);
// 出力はカスケード配列テクスチャのスライス。UAV は ArraySize=1 で対象スライスを
// 単独に見せているため、書き込みインデックスの配列成分は常に 0。
RWTexture2DArray<float4> gDisplacementOutput : register(u0);
RWTexture2DArray<float4> gNormalOutput : register(u1);
RWTexture2DArray<float4> gJacobianOutput : register(u2);

cbuffer FFTOceanSimulationConstants : register(b0)
{
    uint gResolution;
    uint gActiveComponentCount;
    float gPatchLength;
    float gTimeSeconds;
    float gChoppiness;
    float gGravity;
    float gAmplitudeScale;
    float gPadding;
};

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gResolution || dispatchThreadId.y >= gResolution)
    {
        return;
    }

    const uint2 coord = dispatchThreadId.xy;
    const uint one = 1;
    const uint2 coordLeft = uint2((coord.x + gResolution - one) % gResolution, coord.y);
    const uint2 coordRight = uint2((coord.x + one) % gResolution, coord.y);
    const uint2 coordDown = uint2(coord.x, (coord.y + gResolution - one) % gResolution);
    const uint2 coordUp = uint2(coord.x, (coord.y + one) % gResolution);

    const float4 spectrumA = gHeightDisplacementXSpectrum.Load(int3(coord, 0));
    const float4 spectrumB = gDisplacementZSpectrum.Load(int3(coord, 0));

    const uint oddMask = 1;
    const float checker = ((coord.x + coord.y) & oddMask) != 0 ? -1.0f : 1.0f;
    const float checkerLeft = ((coordLeft.x + coordLeft.y) & oddMask) != 0 ? -1.0f : 1.0f;
    const float checkerRight = ((coordRight.x + coordRight.y) & oddMask) != 0 ? -1.0f : 1.0f;
    const float checkerDown = ((coordDown.x + coordDown.y) & oddMask) != 0 ? -1.0f : 1.0f;
    const float checkerUp = ((coordUp.x + coordUp.y) & oddMask) != 0 ? -1.0f : 1.0f;

    const float debugScale = 1.0f;
    const float displacementX = spectrumA.z * checker * debugScale;
    const float height = spectrumA.x * checker * debugScale;
    const float displacementZ = spectrumB.x * checker * debugScale;

    const float4 leftSpectrumA = gHeightDisplacementXSpectrum.Load(int3(coordLeft, 0));
    const float4 rightSpectrumA = gHeightDisplacementXSpectrum.Load(int3(coordRight, 0));
    const float4 downSpectrumA = gHeightDisplacementXSpectrum.Load(int3(coordDown, 0));
    const float4 upSpectrumA = gHeightDisplacementXSpectrum.Load(int3(coordUp, 0));
    const float4 leftSpectrumB = gDisplacementZSpectrum.Load(int3(coordLeft, 0));
    const float4 rightSpectrumB = gDisplacementZSpectrum.Load(int3(coordRight, 0));
    const float4 downSpectrumB = gDisplacementZSpectrum.Load(int3(coordDown, 0));
    const float4 upSpectrumB = gDisplacementZSpectrum.Load(int3(coordUp, 0));

    const float leftHeight = leftSpectrumA.x * checkerLeft;
    const float rightHeight = rightSpectrumA.x * checkerRight;
    const float downHeight = downSpectrumA.x * checkerDown;
    const float upHeight = upSpectrumA.x * checkerUp;

    const float leftDisplacementX = leftSpectrumA.z * checkerLeft * debugScale;
    const float rightDisplacementX = rightSpectrumA.z * checkerRight * debugScale;
    const float downDisplacementX = downSpectrumA.z * checkerDown * debugScale;
    const float upDisplacementX = upSpectrumA.z * checkerUp * debugScale;
    const float leftDisplacementZ = leftSpectrumB.x * checkerLeft * debugScale;
    const float rightDisplacementZ = rightSpectrumB.x * checkerRight * debugScale;
    const float downDisplacementZ = downSpectrumB.x * checkerDown * debugScale;
    const float upDisplacementZ = upSpectrumB.x * checkerUp * debugScale;

    const float texelLength = gPatchLength / (float)gResolution;
    const float invTwoTexelLength = 1.0f / max(2.0f * texelLength, 1.0e-4f);
    const float slopeX = (rightHeight - leftHeight) * invTwoTexelLength;
    const float slopeZ = (upHeight - downHeight) * invTwoTexelLength;

    const float dDx_dx = (rightDisplacementX - leftDisplacementX) * invTwoTexelLength;
    const float dDx_dz = (upDisplacementX - downDisplacementX) * invTwoTexelLength;
    const float dDz_dx = (rightDisplacementZ - leftDisplacementZ) * invTwoTexelLength;
    const float dDz_dz = (upDisplacementZ - downDisplacementZ) * invTwoTexelLength;

    // 法線は高さ勾配だけでなく choppiness の水平変位勾配も含めた
    // 「実際に描画される変位後サーフェス」の接ベクトルから求める。
    // 高さ勾配のみの normalize(-slopeX, 1, -slopeZ) では、水平変位で
    // 波頭が圧縮される箇所（detJ < 1）ほど実ジオメトリと法線がズレて、
    // スペキュラ・フレネルの明暗が波の面と一致しなくなる。
    // 水平変位ゼロなら tangentX=(1,slopeX,0), tangentZ=(0,slopeZ,1) となり
    // cross(tangentZ, tangentX) = (-slopeX, 1, -slopeZ) で従来式と一致する。
    const float3 tangentX = float3(1.0f + dDx_dx, slopeX, dDz_dx);
    const float3 tangentZ = float3(dDx_dz, slopeZ, 1.0f + dDz_dz);
    float3 normal = normalize(cross(tangentZ, tangentX));
    // 波の折り返し（foldover, detJ < 0）で面が裏返った場合も上向きを保つ
    normal = (normal.y < 0.0f) ? -normal : normal;

    const float detJ = (1.0f + dDx_dx) * (1.0f + dDz_dz) - dDx_dz * dDz_dx;
    const float compression = max(1.0f - detJ, 0.0f);
    const float foldover = detJ < 0.0f ? 1.0f : 0.0f;
    const float breakingCandidate = saturate(compression * 2.0f + foldover);

    gDisplacementOutput[uint3(coord, 0)] = float4(displacementX, height, displacementZ, 1.0f);
    gNormalOutput[uint3(coord, 0)] = float4(normalize(normal) * 0.5f + 0.5f, 1.0f);
    gJacobianOutput[uint3(coord, 0)] = float4(detJ, breakingCandidate, compression, foldover);
}
