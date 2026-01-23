#include "Font.h"
#include "Engine/Graphics/Common/DirectXCommon.h"
#include "Engine/Graphics/Common/Core/DescriptorManager.h"
#include "Engine/Utility/Logger/Logger.h"
#include <vector>


namespace CoreEngine
{
Font::~Font() {
	if (face_) {
		FT_Done_Face(face_);
		face_ = nullptr;
	}
}

bool Font::Initialize(FT_Library ftLibrary, const std::string& fontFilePath, uint32_t fontSize, DirectXCommon* dxCommon) {
	fontFilePath_ = fontFilePath;
	fontSize_ = fontSize;
	dxCommon_ = dxCommon;
	descriptorManager_ = dxCommon->GetDescriptorManager();

	if (FT_New_Face(ftLibrary, fontFilePath.c_str(), 0, &face_)) {
		Logger::GetInstance().Log("Failed to load font: " + fontFilePath);
		return false;
	}

	FT_Set_Pixel_Sizes(face_, 0, fontSize);

	lineHeight_ = static_cast<int32_t>(face_->size->metrics.height >> 6);
	ascender_ = static_cast<int32_t>(face_->size->metrics.ascender >> 6);

	Logger::GetInstance().Log("Font initialized: " + fontFilePath + " (size: " + std::to_string(fontSize) + "px)");
	return true;
}

const Glyph* Font::GetGlyph(uint32_t charCode) {
	auto it = glyphCache_.find(charCode);
	if (it != glyphCache_.end()) {
		return it->second.get();
	}

	auto glyph = RenderGlyph(charCode);
	if (!glyph) {
		return nullptr;
	}

	const Glyph* result = glyph.get();
	glyphCache_[charCode] = std::move(glyph);
	return result;
}

std::unique_ptr<Glyph> Font::RenderGlyph(uint32_t charCode) {
	if (FT_Load_Char(face_, charCode, FT_LOAD_RENDER)) {
		Logger::GetInstance().Log("Failed to load glyph for character code: " + std::to_string(charCode));
		return nullptr;
	}

	auto glyph = std::make_unique<Glyph>();
	
	FT_GlyphSlot slot = face_->glyph;
	glyph->width = slot->bitmap.width;
	glyph->height = slot->bitmap.rows;
	glyph->bearingX = slot->bitmap_left;
	glyph->bearingY = slot->bitmap_top;
	glyph->advance = static_cast<int32_t>(slot->advance.x);

	if (!CreateGlyphTexture(slot->bitmap, glyph.get())) {
		return nullptr;
	}

	return glyph;
}

bool Font::CreateGlyphTexture(const FT_Bitmap& bitmap, Glyph* glyph) {
	if (bitmap.width == 0 || bitmap.rows == 0) {
		glyph->texture = nullptr;
		return true;
	}

	ID3D12Device* device = dxCommon_->GetDevice();

	D3D12_RESOURCE_DESC textureDesc{};
	textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	textureDesc.Width = bitmap.width;
	textureDesc.Height = bitmap.rows;
	textureDesc.DepthOrArraySize = 1;
	textureDesc.MipLevels = 1;
	textureDesc.Format = DXGI_FORMAT_R8_UNORM;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

	D3D12_HEAP_PROPERTIES heapProps{};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

	Microsoft::WRL::ComPtr<ID3D12Resource> textureResource;
	HRESULT hr = device->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&textureDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&textureResource)
	);

	if (FAILED(hr)) {
		Logger::GetInstance().Log("Failed to create glyph texture resource");
		return false;
	}

	// RowPitchを正しく計算（256バイトアライメント）
	UINT rowPitch = (bitmap.width + 255) & ~255;
	UINT uploadBufferSize = rowPitch * bitmap.rows;

	D3D12_HEAP_PROPERTIES uploadHeapProps{};
	uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

	D3D12_RESOURCE_DESC uploadBufferDesc{};
	uploadBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	uploadBufferDesc.Width = uploadBufferSize;
	uploadBufferDesc.Height = 1;
	uploadBufferDesc.DepthOrArraySize = 1;
	uploadBufferDesc.MipLevels = 1;
	uploadBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
	uploadBufferDesc.SampleDesc.Count = 1;
	uploadBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer;
	hr = device->CreateCommittedResource(
		&uploadHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&uploadBufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&uploadBuffer)
	);

	if (FAILED(hr)) {
		Logger::GetInstance().Log("Failed to create upload buffer");
		return false;
	}

	// データをアップロードバッファにコピー（RowPitchを考慮）
	uint8_t* mappedData = nullptr;
	uploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mappedData));
	
	for (UINT y = 0; y < bitmap.rows; ++y) {
		std::memcpy(
			mappedData + y * rowPitch,
			bitmap.buffer + y * bitmap.width,
			bitmap.width
		);
	}
	
	uploadBuffer->Unmap(0, nullptr);

	ID3D12GraphicsCommandList* cmdList = dxCommon_->GetCommandList();

	D3D12_TEXTURE_COPY_LOCATION srcLocation{};
	srcLocation.pResource = uploadBuffer.Get();
	srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	srcLocation.PlacedFootprint.Offset = 0;
	srcLocation.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8_UNORM;
	srcLocation.PlacedFootprint.Footprint.Width = bitmap.width;
	srcLocation.PlacedFootprint.Footprint.Height = bitmap.rows;
	srcLocation.PlacedFootprint.Footprint.Depth = 1;
	srcLocation.PlacedFootprint.Footprint.RowPitch = rowPitch;

	D3D12_TEXTURE_COPY_LOCATION dstLocation{};
	dstLocation.pResource = textureResource.Get();
	dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dstLocation.SubresourceIndex = 0;

	cmdList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);

	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = textureResource.Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	cmdList->ResourceBarrier(1, &barrier);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_R8_UNORM;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.MostDetailedMip = 0;

	descriptorManager_->CreateSRV(textureResource.Get(), srvDesc, glyph->cpuHandle, glyph->gpuHandle, "GlyphTexture");

	glyph->texture = textureResource;
	glyph->uploadBuffer = uploadBuffer;  // uploadBufferを保持してコマンド完了まで維持

	return true;
}
}
