#pragma once

#include <d3d12.h>
#include <wrl.h>


namespace CoreEngine
{
class ResourceFactory {
public:
	// Resourceの生成
	static Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(Microsoft::WRL::ComPtr<ID3D12Device> device, size_t sizeInBytes);
};
}
