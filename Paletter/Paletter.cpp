// Paletter.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include "Window.h"
#include "ScopedTimer.h"
#define VERBOSE
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STBI_MSC_SECURE_CRT
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "Color32.h"
#include "Color24.h"

float clamp(float minV, float maxV, float val)
{

	if (val < minV)
		val = minV;
	else if (val > maxV)
		val = maxV;
	return val;
}

struct ColorCount
{
	Color24 color;
	size_t count;

	bool operator<(const ColorCount& other) const
	{
		return count > other.count;
	}
};

template<class Iter, class T>
Iter binary_find(Iter begin, Iter end, T val)
{
	// Finds the lower bound in at most log(last - first) + 1 comparisons
	Iter i = std::lower_bound(begin, end, val);

	if (i != end && !(val < *i))
		return i; // found
	else
		return end; // not found
}
BYTE merge(BYTE col0, BYTE col1)
{
	int merge = col0;
	merge += col1;
	merge /= 2;
	return (BYTE)merge;
}

void RGBtoYCBCR(float& R, float& G, float& B)
{
	float Y = 0 + (0.299 * R) + (0.587 * G) + (0.114 * B);
	float Cb = 128 - (0.168736 * R) - (0.331264 * G) + (0.5 * B);
	float Cr = 128 + (0.5 * R) - (0.418688 * G) - (0.081312 * B);


	R = Y;
	G = Cb;
	B = Cr;
}

void YCBCRtoRGB(float& Y, float& Cb, float& Cr)
{
	float R = Y + 1.402*(Cr - 128);
	float G = Y - 0.344136 *(Cb - 128) - 0.714136*(Cr - 128);
	float B = Y + 1.772 *(Cb - 128);

	Y = R;
	Cb = G;
	Cr = B;
}

void Compute8x8Idct(const float in[8][8], float out[8][8])
{
	int i, j, u, v;
	double s;

	for (i = 0; i < 8; i++)
		for (j = 0; j < 8; j++)
		{
			s = 0;

			for (u = 0; u < 8; u++)
				for (v = 0; v < 8; v++)
					s += in[u][v] * cos((2 * i + 1) * u * DirectX::XM_PI / 16) *
					cos((2 * j + 1) * v * DirectX::XM_PI / 16) *
					((u == 0) ? 1 / sqrt(2) : 1.) *
					((v == 0) ? 1 / sqrt(2) : 1.);

			out[i][j] = s / 4;
		}
}

static int YQT[] = { 16,11,10,16,24,40,51,61,12,12,14,19,26,58,60,55,14,13,16,24,40,57,69,56,14,17,22,29,51,87,80,62,18,22,
							 37,56,68,109,103,77,24,35,55,64,81,104,113,92,49,64,78,87,103,121,120,101,72,92,95,98,112,100,103,99 };



static const double S[] = {
	0.353553390593273762200422,
	0.254897789552079584470970,
	0.270598050073098492199862,
	0.300672443467522640271861,
	0.353553390593273762200422,
	0.449988111568207852319255,
	0.653281482438188263928322,
	1.281457723870753089398043,
};

static const double A[] = {
	NAN,
	0.707106781186547524400844,
	0.541196100146196984399723,
	0.707106781186547524400844,
	1.306562964876376527856643,
	0.382683432365089771728460,
};


// DCT type II, scaled. Algorithm by Arai, Agui, Nakajima, 1988.
// See: https://web.stanford.edu/class/ee398a/handouts/lectures/07-TransformCoding.pdf#page=30
void transform(float vector[8]) {
	const float v0 = vector[0] + vector[7];
	const float v1 = vector[1] + vector[6];
	const float v2 = vector[2] + vector[5];
	const float v3 = vector[3] + vector[4];
	const float v4 = vector[3] - vector[4];
	const float v5 = vector[2] - vector[5];
	const float v6 = vector[1] - vector[6];
	const float v7 = vector[0] - vector[7];

	const float v8 = v0 + v3;
	const float v9 = v1 + v2;
	const float v10 = v1 - v2;
	const float v11 = v0 - v3;
	const float v12 = -v4 - v5;
	const float v13 = (v5 + v6) * A[3];
	const float v14 = v6 + v7;

	const float v15 = v8 + v9;
	const float v16 = v8 - v9;
	const float v17 = (v10 + v11) * A[1];
	const float v18 = (v12 + v14) * A[5];

	const float v19 = -v12 * A[2] - v18;
	const float v20 = v14 * A[4] - v18;

	const float v21 = v17 + v11;
	const float v22 = v11 - v17;
	const float v23 = v13 + v7;
	const float v24 = v7 - v13;

	const float v25 = v19 + v24;
	const float v26 = v23 + v20;
	const float v27 = v23 - v20;
	const float v28 = v24 - v19;

	vector[0] = S[0] * v15;
	vector[1] = S[1] * v26;
	vector[2] = S[2] * v21;
	vector[3] = S[3] * v28;
	vector[4] = S[4] * v16;
	vector[5] = S[5] * v25;
	vector[6] = S[6] * v22;
	vector[7] = S[7] * v27;
}

void CompressBlock(float in[8][8], float out[8][8])
{

	float dct[8][8];

	float ci, cj, dct1, sum;
	for (int i = 0; i < 8; i++)
	{
		for (int j = 0; j < 8; j++)
		{
			if (i == 0)
			{
				ci = 1.0 / sqrt(8);
			}
			else
			{
				ci = sqrt(2) / sqrt(8);
			}

			if (j == 0)
			{
				cj = 1.0 / sqrt(8);
			}
			else
			{
				cj = sqrt(2) / sqrt(8);
			}

			sum = 0.0f;
			for (int k = 0; k < 8; k++)
			{
				for (int l = 0; l < 8; l++)
				{
					dct1 = (in[k][l] - 127)* cos((2.0 * k + 1) * i * DirectX::XM_PI / (2 * 8)) *
						cos((2 * l + 1) * j * DirectX::XM_PI / (2 * 8));
					sum += dct1;
				}
			}
			dct[i][j] = ci * cj * sum;
		}
	}
#ifdef VERBOSE
	printf("Before\n");

	for (int i = 0; i < 8; i++) {
		for (int j = 0; j < 8; j++) {
			printf("%f\t", in[i][j]);
		}
		printf("\n");
	}

	printf("After\n");
	for (int i = 0; i < 8; i++) {
		for (int j = 0; j < 8; j++) {
			printf("%f\t", dct[i][j]);
		}
		printf("\n");
	}

	printf("After2\n");
	for (int i = 0; i < 8; i++) {
		transform(in[i]);
		for (int j = 0; j < 8; j++) {
			printf("%f\t", in[i][j]);
		}
		printf("\n");
	}
#endif


	float quant[8][8];
	for (int i = 0; i < 8; i++)
	{
		for (int j = 0; j < 8; j++)
		{
			quant[i][j] = roundf(dct[i][j] / YQT[i + j * 8]);
		}
	}
#ifdef VERBOSE
	printf("After Quant\n");
	for (int i = 0; i < 8; i++) {
		for (int j = 0; j < 8; j++) {
			printf("%f\t", quant[i][j]);
		}
		printf("\n");
	}


	printf("Reapply quant\n");
#endif
	float reapplyQuant[8][8];
	for (int i = 0; i < 8; i++)
	{
		for (int j = 0; j < 8; j++)
		{
			reapplyQuant[i][j] = quant[i][j] * YQT[i + j * 8];
		}
	}
#ifdef VERBOSE
	for (int i = 0; i < 8; i++) {
		for (int j = 0; j < 8; j++) {
			printf("%f\t", reapplyQuant[i][j]);
		}
		printf("\n");
	}

	printf("Reconstruction\n");
#endif
	// Inverse
	Compute8x8Idct(reapplyQuant, out);
	for (int i = 0; i < 8; i++) {
		for (int j = 0; j < 8; j++) {
			out[i][j] += 127.0f;
#ifdef VERBOSE
			printf("%f\t", out[i][j]);
#endif
		}
#ifdef VERBOSE
		printf("\n");
#endif
	}
}

// DX12
ComPtr<ID3D12Device4> gDevice;

ComPtr<ID3D12CommandQueue> gDirectQueue;
ComPtr< ID3D12Fence> gDirectFence;
HANDLE gDirectEventHandle;
UINT64 gDirectFenceValue = 0;

ComPtr<ID3D12CommandAllocator> gDirectAllocator;
ComPtr<ID3D12GraphicsCommandList> gDirectList;

ComPtr<ID3D12CommandQueue> gComputeQueue;
ComPtr<ID3D12CommandAllocator> gComputeAllocator;
ComPtr<ID3D12GraphicsCommandList> gComputeList;

ComPtr< ID3D12Fence> gComputeFence;
HANDLE gComputeEventHandle;
UINT64 gComputeFenceValue = 0;
ComPtr<ID3D12RootSignature> gRootCompute;
ComPtr<ID3D12PipelineState> gComputePipeline;

constexpr int NUM_BACK_BUFFERS = 3;
ComPtr<ID3D12DescriptorHeap> gRTVHeap;
D3D12_CPU_DESCRIPTOR_HANDLE gRTVDescHandles[NUM_BACK_BUFFERS];
ComPtr<IDXGISwapChain4> gSwapChain;
ComPtr<ID3D12Resource> gRTResource[NUM_BACK_BUFFERS];
UINT gFrameIndex = 0;

ComPtr<ID3D12DescriptorHeap> gSRVHeap;
ComPtr<ID3D12Resource> gInputImageRes;
ComPtr<ID3D12Resource> gInputImageUpload;

ComPtr<ID3D12Resource> gWrittableRes;

constexpr int TABLE_SIZE = 256 * 256;
int main()
{

	int width, height, comp;
	//Color24* data = reinterpret_cast<Color24*>(stbi_load("Images/lol1.jpg", &width, &height, &comp, 0));
	stbi_uc* data = (stbi_load("Images/test1.png", &width, &height, &comp, 4));
	comp = 4; // Force comp = 4

	Window window(VideoMode(width, height), L"JPEG Eye Tracking");
	ComPtr<IDXGIFactory4> pFactory;

	// Device
	{
		ComPtr<ID3D12Debug> debugController;
		UINT dxgiFactoryFlags = 0;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();
			dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
		ComPtr<ID3D12Debug1> debugcontroller1;
		debugController->QueryInterface(IID_PPV_ARGS(&debugcontroller1));
		debugcontroller1->SetEnableGPUBasedValidation(true);


		ComPtr<IDXGIAdapter1> pAdapter;
		TIF(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&pFactory)));
		//Loop through and find adapter
		for (UINT adapterIndex = 0;
			DXGI_ERROR_NOT_FOUND != pFactory->EnumAdapters1(adapterIndex, &pAdapter);
			++adapterIndex)
		{
			//if (adapterIndex == 0) continue;
			DXGI_ADAPTER_DESC1 desc;
			pAdapter->GetDesc1(&desc);
			printf("%ls\n", desc.Description);

			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
				continue;

			if (SUCCEEDED(
				D3D12CreateDevice(pAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&gDevice))))
			{
				break;
			}
		}

		NAME_D3D12_OBJECT(gDevice);
	}
	// RTV
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		desc.NumDescriptors = NUM_BACK_BUFFERS;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		desc.NodeMask = 1;

		TIF(gDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&gRTVHeap)));
		NAME_D3D12_OBJECT(gRTVHeap);

		size_t rtvDescriptorSize =
			gDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = gRTVHeap->GetCPUDescriptorHandleForHeapStart();
		for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
		{
			gRTVDescHandles[i] = rtvHandle;
			rtvHandle.ptr += rtvDescriptorSize;
		}
	}

	// Command Queue
	{
		D3D12_COMMAND_QUEUE_DESC queueDesc = {};
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		queueDesc.NodeMask = 1;
		TIF(gDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&gDirectQueue)));
		NAME_D3D12_OBJECT(gDirectQueue);
		gDevice->CreateFence(gDirectFenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gDirectFence));
		gDirectEventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);


		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		queueDesc.NodeMask = 1;
		TIF(gDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&gComputeQueue)));
		NAME_D3D12_OBJECT(gComputeQueue);
		gDevice->CreateFence(gComputeFenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gComputeFence));
		gComputeEventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);


	}

	// Swapchain
	{


		DXGI_SWAP_CHAIN_DESC1 sd = {};
		sd.BufferCount = NUM_BACK_BUFFERS;
		sd.Width = width;
		sd.Height = height;
		sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.SampleDesc.Count = 1;
		sd.SampleDesc.Quality = 0;
		sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
		sd.Scaling = DXGI_SCALING_STRETCH;
		sd.Stereo = FALSE;


		ComPtr<IDXGISwapChain1> swapChain1;


		TIF(pFactory->CreateSwapChainForHwnd(
			gDirectQueue.Get(), window.getHandle(), &sd, nullptr, nullptr, &swapChain1));
		TIF(swapChain1->QueryInterface(IID_PPV_ARGS(&gSwapChain)));

		gFrameIndex = gSwapChain->GetCurrentBackBufferIndex();

		gSwapChain->SetMaximumFrameLatency(NUM_BACK_BUFFERS);

		HANDLE m_hSwapChainWait = gSwapChain->GetFrameLatencyWaitableObject();
		assert(m_hSwapChainWait != nullptr);

		for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
		{

			TIF(gSwapChain->GetBuffer(i, IID_PPV_ARGS(&gRTResource[i])));
			gDevice->CreateRenderTargetView(gRTResource[i].Get(), nullptr, gRTVDescHandles[i]);
			NAME_D3D12_OBJECT_INDEXED(gRTResource, i);
		}

	}

	//Command Allocatrs and Lists
	{
		TIF(gDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&gDirectAllocator)));
		TIF(gDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&gComputeAllocator)));
		NAME_D3D12_OBJECT(gDirectAllocator);
		NAME_D3D12_OBJECT(gComputeAllocator);

		TIF(gDevice->CreateCommandList(1, D3D12_COMMAND_LIST_TYPE_DIRECT, gDirectAllocator.Get(), nullptr, IID_PPV_ARGS(&gDirectList)));
		TIF(gDevice->CreateCommandList(1, D3D12_COMMAND_LIST_TYPE_COMPUTE, gComputeAllocator.Get(), nullptr, IID_PPV_ARGS(&gComputeList)));
		NAME_D3D12_OBJECT(gDirectList);
		NAME_D3D12_OBJECT(gComputeList);
		//gDirectList->Close(); // Used to copy texture data
		gComputeList->Close();
	}


	// SRV and UAV
	{



		D3D12_RESOURCE_DESC textureDesc = {};
		textureDesc.MipLevels = 1;
		textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		textureDesc.Width = width;
		textureDesc.Height = height;
		textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		textureDesc.DepthOrArraySize = 1;
		textureDesc.SampleDesc.Count = 1;
		textureDesc.SampleDesc.Quality = 0;
		textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

		D3D12_HEAP_PROPERTIES heapProp = {};
		heapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapProp.CreationNodeMask = 1;
		heapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heapProp.Type = D3D12_HEAP_TYPE_DEFAULT;
		heapProp.VisibleNodeMask = 1;

		TIF(
			gDevice->CreateCommittedResource(&heapProp,
				D3D12_HEAP_FLAG_NONE,
				&textureDesc,
				D3D12_RESOURCE_STATE_COPY_DEST,
				nullptr,
				IID_PPV_ARGS(&gInputImageRes)));


		// Get the size needed for this texture buffer
		UINT64 uploadBufferSize;
		gDevice->GetCopyableFootprints(&gInputImageRes->GetDesc(), 0, 1, 0, nullptr, nullptr, nullptr, &uploadBufferSize);

		// This is the GPU upload buffer.
		D3D12_HEAP_PROPERTIES heapProp2 = {};
		heapProp2.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapProp2.CreationNodeMask = 1;
		heapProp2.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heapProp2.Type = D3D12_HEAP_TYPE_UPLOAD;
		heapProp2.VisibleNodeMask = 1;

		{

			D3D12_RESOURCE_DESC resDesc = {};
			resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			resDesc.Alignment = 0;
			resDesc.Format = DXGI_FORMAT_UNKNOWN;
			resDesc.DepthOrArraySize = 1;
			resDesc.Height = 1;
			resDesc.Width = uploadBufferSize;
			resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
			resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			resDesc.MipLevels = 1;
			resDesc.SampleDesc.Count = 1;

			TIF(
				gDevice->CreateCommittedResource(&heapProp2,
					D3D12_HEAP_FLAG_NONE,
					&resDesc,
					D3D12_RESOURCE_STATE_GENERIC_READ,
					nullptr,
					IID_PPV_ARGS(&gInputImageUpload)));
		}

		// This is the texture data.


		// Now do we copy the data to the heap we created in this scope and then schedule a copy
		// from this "upload heap" to the real texture 2d?

		D3D12_SUBRESOURCE_DATA textureData = {};
		textureData.pData = data;
		textureData.RowPitch = width * comp;
		textureData.SlicePitch = textureData.RowPitch * height;

		UINT64 requiredSize = 0;
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT layouts;
		UINT NumRows;
		UINT64 RowSizeInBytes;

		D3D12_RESOURCE_DESC resDesc = gInputImageRes->GetDesc();
		gDevice->GetCopyableFootprints(&resDesc, 0, 1, 0, &layouts, &NumRows, &RowSizeInBytes, &requiredSize);

		BYTE* pData;
		TIF(gInputImageUpload->Map(0, nullptr, reinterpret_cast<LPVOID*>(&pData)));

		memcpy(pData, data, width * height * comp);

		gInputImageUpload->Unmap(0, nullptr);

		D3D12_TEXTURE_COPY_LOCATION texture0 = {};
		texture0.pResource = gInputImageRes.Get();
		texture0.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		texture0.SubresourceIndex = 0;

		D3D12_TEXTURE_COPY_LOCATION texture1 = {};
		texture1.pResource = gInputImageUpload.Get();
		texture1.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		texture1.PlacedFootprint = layouts;

		gDirectList->CopyTextureRegion(&texture0, 0, 0, 0, &texture1, nullptr);

		D3D12_RESOURCE_BARRIER barrier;
		barrier.Transition.pResource = gInputImageRes.Get();
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		barrier.Transition.Subresource = 0;
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		gDirectList->ResourceBarrier(1, &barrier);

		gDirectList->Close();

		{
			ID3D12CommandList* listsToExceute[] = { gDirectList.Get() };
			gDirectQueue->ExecuteCommandLists(1, listsToExceute);
		}

		gDirectQueue->Signal(gDirectFence.Get(), gDirectFenceValue);
		if (gDirectFenceValue > gDirectFence->GetCompletedValue())
		{
			gDirectFence->SetEventOnCompletion(gDirectFenceValue, gDirectEventHandle);
			WaitForMultipleObjects(1, &gDirectEventHandle, TRUE, INFINITE);
		}
		gDirectFenceValue++;

		D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
		srvHeapDesc.NumDescriptors =
			2; // One for UAV and one for SRV
		srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

		TIF(
			gDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&gSRVHeap)));

		// Describe and create a SRV for the texture.
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = textureDesc.Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		auto handle2 = gSRVHeap->GetCPUDescriptorHandleForHeapStart();
		handle2.ptr += gDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		gDevice->CreateShaderResourceView(
			gInputImageRes.Get(), &srvDesc, handle2);




		{
			// Create the output resource. The dimensions and format should match the swap-chain
			D3D12_RESOURCE_DESC resDesc = {};
			resDesc.DepthOrArraySize = 1;
			resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			resDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // The backbuffer is actually DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, but sRGB formats can't be used with UAVs. We will convert to sRGB ourselves in the shader
			resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
			resDesc.Width = width;
			resDesc.Height = height;
			resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
			resDesc.MipLevels = 1;
			resDesc.SampleDesc.Count = 1;

			D3D12_HEAP_PROPERTIES defaultHeapProps =
			{
				D3D12_HEAP_TYPE_DEFAULT,
				D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
				D3D12_MEMORY_POOL_UNKNOWN,
				0,
				0
			};
			TIF(gDevice->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr, IID_PPV_ARGS(&gWrittableRes)));
			NAME_D3D12_OBJECT(gWrittableRes);
			auto handle = gSRVHeap->GetCPUDescriptorHandleForHeapStart();

			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = resDesc.Format;
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;


			gDevice->CreateUnorderedAccessView(gWrittableRes.Get(), nullptr, &uavDesc, handle);

		}




	}

	// Compute Root Signature
	{
		D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
		featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
		if (FAILED(gDevice->CheckFeatureSupport(
			D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
		{
			featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
		}

		D3D12_DESCRIPTOR_RANGE1 ranges[2];
		ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		ranges[1].NumDescriptors = 1;
		ranges[1].BaseShaderRegister = 0;
		ranges[1].RegisterSpace = 0;
		ranges[1].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
		ranges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		ranges[0].NumDescriptors = 1;
		ranges[0].BaseShaderRegister = 0;
		ranges[0].RegisterSpace = 0;
		ranges[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
		ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		D3D12_ROOT_PARAMETER1 rootParameters[2];
		rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
		rootParameters[0].DescriptorTable.pDescriptorRanges = &ranges[0];
		rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
		rootParameters[1].DescriptorTable.pDescriptorRanges = &ranges[1];
		rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSig;
		rootSig.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
		rootSig.Desc_1_1.NumParameters = _countof(rootParameters);
		rootSig.Desc_1_1.pParameters = rootParameters;
		rootSig.Desc_1_1.NumStaticSamplers = 0;
		rootSig.Desc_1_1.pStaticSamplers = nullptr;
		rootSig.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

		ComPtr<ID3DBlob> signature;
		ComPtr<ID3DBlob> error;

		TIF(D3D12SerializeVersionedRootSignature(
			&rootSig, &signature, &error));
		if (error)
		{
			std::cout << (char*)error->GetBufferPointer() << std::endl;
		}
		TIF(gDevice->CreateRootSignature(0,
			signature->GetBufferPointer(),
			signature->GetBufferSize(),
			IID_PPV_ARGS(&gRootCompute)));

		NAME_D3D12_OBJECT(gRootCompute);
	}
	// Compute Pipeline
	{
		ComPtr<ID3DBlob> computeBlob;
		ComPtr<ID3DBlob> errorPtr;
		TIF(D3DCompileFromFile(L"Paletter/ComputeShader.hlsl",
			nullptr,
			nullptr,
			"main",
			"cs_5_1",
			0,
			0,
			&computeBlob,
			&errorPtr));

		if (errorPtr)
		{
			std::cout << (char*)errorPtr->GetBufferPointer() << std::endl;
		}
		D3D12_COMPUTE_PIPELINE_STATE_DESC cpsd = {};
		cpsd.pRootSignature = gRootCompute.Get();
		cpsd.CS.pShaderBytecode = computeBlob->GetBufferPointer();
		cpsd.CS.BytecodeLength = computeBlob->GetBufferSize();
		cpsd.NodeMask = 0;

		TIF(gDevice->CreateComputePipelineState(&cpsd, IID_PPV_ARGS(&gComputePipeline)));
	}
	// Wait for compute
	gComputeQueue->Signal(gComputeFence.Get(), gComputeFenceValue);
	if (gComputeFenceValue > gComputeFence->GetCompletedValue())
	{
		gComputeFence->SetEventOnCompletion(gComputeFenceValue, gComputeEventHandle);
		WaitForMultipleObjects(1, &gComputeEventHandle, TRUE, INFINITE);
	}
	gComputeFenceValue++;

	while (window.isOpen())
	{
		ScopedTimer timer("Render");
		window.pollEvents();

		TIF(gComputeAllocator->Reset());
		gComputeList->Reset(gComputeAllocator.Get(), gComputePipeline.Get());

		gComputeList->SetComputeRootSignature(gRootCompute.Get());

		ID3D12DescriptorHeap* heaps[] = { gSRVHeap.Get() };
		gComputeList->SetDescriptorHeaps(1, heaps);

		{
			D3D12_RESOURCE_BARRIER barrier1 = {};
			barrier1.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier1.Transition.pResource = gWrittableRes.Get();
			barrier1.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			barrier1.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
			barrier1.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
			gComputeList->ResourceBarrier(1, &barrier1);

		}

		// Run Compute 
		auto h = gSRVHeap->GetGPUDescriptorHandleForHeapStart();
		gComputeList->SetComputeRootDescriptorTable(0, h);
		h.ptr += gDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		gComputeList->SetComputeRootDescriptorTable(1, h);
		gComputeList->Dispatch((width / 8), (height / 8) , 1);
		//gComputeList->Dispatch(width, height, 1);

		{
			D3D12_RESOURCE_BARRIER barrier = {};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = gWrittableRes.Get();
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
			gComputeList->ResourceBarrier(1, &barrier);
		}
		gComputeList->Close();

		{
			ID3D12CommandList* listsToExceute[] = { gComputeList.Get() };
			gComputeQueue->ExecuteCommandLists(1, listsToExceute);
		}

		// Wait for compute
		gComputeQueue->Signal(gComputeFence.Get(), gComputeFenceValue);
		if (gComputeFenceValue > gComputeFence->GetCompletedValue())
		{
			gComputeFence->SetEventOnCompletion(gComputeFenceValue, gComputeEventHandle);
			WaitForMultipleObjects(1, &gComputeEventHandle, TRUE, INFINITE);
		}
		gComputeFenceValue++;

		// Copy result
		gDirectAllocator->Reset();
		gDirectList->Reset(gDirectAllocator.Get(), nullptr);

		{
			D3D12_RESOURCE_BARRIER barrier = {};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = gRTResource[gFrameIndex].Get();
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
			gDirectList->ResourceBarrier(1, &barrier);
		}

		gDirectList->CopyResource(gRTResource[gFrameIndex].Get(), gWrittableRes.Get());

		{
			D3D12_RESOURCE_BARRIER barrier = {};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = gRTResource[gFrameIndex].Get();
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
			gDirectList->ResourceBarrier(1, &barrier);
		}

		gDirectList->Close();

		{
			ID3D12CommandList* listsToExceute[] = { gDirectList.Get() };
			gDirectQueue->ExecuteCommandLists(1, listsToExceute);
		}

		gDirectQueue->Signal(gDirectFence.Get(), gDirectFenceValue);
		if (gDirectFenceValue > gDirectFence->GetCompletedValue())
		{
			gDirectFence->SetEventOnCompletion(gDirectFenceValue, gDirectEventHandle);
			WaitForMultipleObjects(1, &gDirectEventHandle, TRUE, INFINITE);
		}
		gDirectFenceValue++;

		gSwapChain->Present(0, 0);
		gFrameIndex = gSwapChain->GetCurrentBackBufferIndex();





		// Present


	}

	
	std::vector<ColorCount> colorCounter;
	Color24 colorTable[TABLE_SIZE];


	stbi_uc* output = new stbi_uc[width * height * comp];
	if (data)
	{
		printf("Reading image done!\nWidth=(%i)\nHeight=(%i)\nComp=(%i)\n", width, height, comp);

	}

	int quality = 100;

	quality = quality ? quality : 90;
	quality = quality < 1 ? 1 : quality > 100 ? 100 : quality;
	quality = quality < 50 ? 5000 / quality : 200 - quality * 2;

	for (int i = 0; i < 64; i++)
	{
		if (i % 8 == 0)printf("\n");
		printf("%i ", YQT[i]);
	}
	printf("\n");

	for (int i = 0; i < 64; i++)
	{
		int 	yti = (YQT[i] * quality + 50) / 100;
		yti = (yti < 1 ? 1 : yti > 255 ? 255 : yti);
		if (i % 8 == 0)printf("\n");
		printf("%i ", yti);
		YQT[i] = yti;
	}

	using namespace DirectX;

	const int BLOCK_SIZE = 8;

	float in[BLOCK_SIZE][BLOCK_SIZE];
	float out[BLOCK_SIZE][BLOCK_SIZE];

	int nThreads = (width / BLOCK_SIZE)*(height / BLOCK_SIZE);




	for (int y = 0; y < height; y += BLOCK_SIZE)
	{
		for (int x = 0; x < width; x += BLOCK_SIZE)
		{

			// R
			for (int i = 0; i < BLOCK_SIZE; i++)
			{
				for (int j = 0; j < BLOCK_SIZE; j++)
				{
					in[i][j] = data[((x + i) + (y + j) * width) * comp + 0];
				}
			}

			CompressBlock(in, out);

			for (int i = 0; i < BLOCK_SIZE; i++)
			{
				for (int j = 0; j < BLOCK_SIZE; j++)
				{
					output[((x + i) + (y + j) * width) * comp + 0] = clamp(0.0f, 255.0f, out[i][j]);

				}
			}

			// G
			for (int i = 0; i < BLOCK_SIZE; i++)
			{
				for (int j = 0; j < BLOCK_SIZE; j++)
				{
					in[i][j] = data[((x + i) + (y + j) * width) * comp + 1];
				}
			}

			CompressBlock(in, out);

			for (int i = 0; i < BLOCK_SIZE; i++)
			{
				for (int j = 0; j < BLOCK_SIZE; j++)
				{

					output[((x + i) + (y + j) * width) * comp + 1] = clamp(0.0f, 255.0f, out[i][j]);

				}
			}

			// B
			for (int i = 0; i < BLOCK_SIZE; i++)
			{
				for (int j = 0; j < BLOCK_SIZE; j++)
				{
					in[i][j] = data[((x + i) + (y + j) * width) * comp + 2];
				}
			}

			CompressBlock(in, out);

			for (int i = 0; i < BLOCK_SIZE; i++)
			{
				for (int j = 0; j < BLOCK_SIZE; j++)
				{

					output[((x + i) + (y + j) * width) * comp + 2] = clamp(0.0f, 255.0f, out[i][j]);
				}
			}


		}
	}




	for (int i = 0; i < width*height; i++)
	{

		float r = data[(i*comp) + 0];
		float g = data[(i*comp) + 1];
		float b = data[(i*comp) + 2];
		r /= 255.0f;
		g /= 255.0f;
		b /= 255.0f;
		DirectX::XMVECTOR color = { r,g,b };

		XMVECTOR yuv = XMColorRGBToYUV(color);

		XMFLOAT3 ynn;
		XMStoreFloat3(&ynn, yuv);

		yuv = XMLoadFloat3(&ynn);
		//XMVECTOR rgb = XMColorYUVToRGB(yuv);


		output[(i* comp) + 0] = ynn.x * 255.0f;
		output[(i* comp) + 1] = ynn.x* 255.0f;
		output[(i* comp) + 2] = ynn.x* 255.0f;

	}



	// Try JPEG
	stbi_write_bmp("Images/Sample2_out.bmp", width, height, comp, output);

#pragma region OLDCODE
	//size_t imageSize = width * height;
	//size_t imageSizeBytes = imageSize * comp;

	//for (size_t i = 0; i < imageSize; i++)
	//{
	//	bool unique = true;
	//	for (auto& color : colorCounter)
	//	{
	//		if (color.color == data[i])
	//		{
	//			color.count++;
	//			unique = false;
	//			break;
	//		}
	//	}
	//	if (unique)
	//	{
	//		ColorCount cc;
	//		cc.color = data[i];
	//		cc.count = 1;
	//		colorCounter.push_back(cc);
	//	}
	//}
	//std::sort(colorCounter.begin(), colorCounter.end());
	//printf("Unique Colors=(%u)\n", colorCounter.size());

	//int colorCounterSize = colorCounter.size();
	//std::vector<Color24> colorCounter2;
	////if(false)
	//if (colorCounterSize > TABLE_SIZE)
	//{
	//	for (int i = 0; i < colorCounterSize - 1; i++)
	//	{
	//		if (colorCounterSize <= TABLE_SIZE)
	//			break;

	//		int closestMatch = 0;
	//		int minimumSize = 9999;

	//		for (int j = i + 1; j < colorCounterSize; j++)
	//		{
	//			int colDiff = (colorCounter[i].color - colorCounter[j].color).length();
	//			if (colDiff < minimumSize)
	//			{
	//				closestMatch = j;
	//				minimumSize = colDiff;

	//			}
	//		}
	//		if (minimumSize < 120)
	//		{

	//			/*Color24 colorMerge;
	//			colorMerge.r = merge(colorCounter[i].color.r, colorCounter[closestMatch].color.r);
	//			colorMerge.g = merge(colorCounter[i].color.g, colorCounter[closestMatch].color.g);
	//			colorMerge.b = merge(colorCounter[i].color.b, colorCounter[closestMatch].color.b);
	//			colorCounter[i].color = colorMerge;*/

	//			colorCounter.erase(colorCounter.begin() + closestMatch);
	//		
	//			colorCounterSize = colorCounter.size();

	//		}
	//	}
	//	printf("Reduced colors to=(%u)\n", colorCounterSize);
	//}


	//int size = TABLE_SIZE > colorCounter.size() ? colorCounter.size() : TABLE_SIZE;


	//for (int i = 0; i < size; i++)
	//	colorTable[i] = colorCounter[i].color;

	//Color24* output = new Color24[imageSize];

	//for (size_t i = 0; i < imageSize; i++)
	//{
	//	int closestindex = 0;
	//	int minSize = 9999;

	//	for (size_t acceptColor = 0; acceptColor < size; acceptColor++)
	//	{
	//		Color24 col = data[i] - colorTable[acceptColor];
	//		int length = col.length();
	//		if (length < minSize)
	//		{
	//			minSize = length;
	//			closestindex = acceptColor;
	//		}



	//	}
	//	output[i] = colorTable[closestindex];
	//}

	//stbi_write_bmp("Images/Sample2_out.bmp", width, height, comp, output);
	//float compressionSize = ((imageSize + (size * sizeof(Color24))) / (float)imageSizeBytes)*100.0;
	//printf("Original Size Bytes=(%u)\nCompressed Size Bytes=(%u)\nTotal Compression=(%f)\n", imageSizeBytes, (imageSize + (size* sizeof(Color24))), compressionSize);

#pragma endregion


	system("pause");
	return 0;
}