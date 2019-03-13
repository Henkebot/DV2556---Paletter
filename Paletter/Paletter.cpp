// Paletter.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "Timers/Timer.h"
#include "Utillity/Input.h"
#include "Utillity/Window.h"
#include "pch.h"

constexpr int SCREEN_WIDTH  = 1920;
constexpr int SCREEN_HEIGHT = 1080;
#include "Constants.h"

struct JPEG_SETTINGS
{
	float circleRatio;
	int centerQuality;
};

JPEG_SETTINGS settings[] = {
	// Circle tests
	{1.0f, 100},
	{0.75f, 100},
	{0.50f, 100},
	{0.30f, 100},

	// Quality Tests
	{1.0f, 80},
	{1.0f, 60},
	{1.0f, 40},
	{1.0f, 20}};

constexpr unsigned int MAX_STAGES = ARRAYSIZE(settings);

int gResults[MAX_STAGES]; // Each index tells us what image was choosen

enum class States
{
	INTRO,
	FINISHED,
	SELECTION,
	PREPARE_JPEG,
	PREPARE_RAW,
	PRESENT_JPEG,
	PRESENT_RAW
};

States gCurrentState = States::INTRO;

// TOBII
#define TOBII //Uncomment for eye tracking
#include <assert.h>
#include <tobii/tobii.h>
#include <tobii/tobii_streams.h>

static void gazePointCallback(tobii_gaze_point_t const* pPoint, void* userData);
static void urlReciever(char const* pUrl, void* pUserData);

int gGazePoint[2];

// STB IMAGE
#define STB_IMAGE_IMPLEMENTATION
#include "Utillity/stb_image.h"
#define STBI_MSC_SECURE_CRT
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "Paletter.h"
#include "Utillity/stb_image_write.h"

// DX11
ComPtr<ID3D11DeviceContext> gDevice11Context;
ComPtr<ID3D11On12Device> gDevice11On12;
ComPtr<ID3D11Resource> gWrappedResource[NUM_BACKBUFFERS];
ComPtr<ID2D1Bitmap1> gD2DRenderTarget[NUM_BACKBUFFERS];

ComPtr<ID2D1SolidColorBrush> gTextBrush;
ComPtr<IDWriteTextFormat> gDTextFormat;

ComPtr<ID2D1Factory3> gD2DFactory;
ComPtr<ID2D1Device2> gD2DDevice;
ComPtr<ID2D1DeviceContext> gD2DDeviceContext;
ComPtr<IDWriteFactory> gDWriteFactory;

// DX12
bool IsTearingSupported();

ComPtr<ID3D12Device4> gDevice;

ComPtr<ID3D12CommandQueue> gDirectQueue;
ComPtr<ID3D12Fence> gDirectFence;
HANDLE gDirectEventHandle;
UINT64 gDirectFenceValue = 0;

ComPtr<ID3D12CommandAllocator> gDirectAllocator;
ComPtr<ID3D12GraphicsCommandList> gDirectList;

ComPtr<ID3D12CommandQueue> gComputeQueue;
ComPtr<ID3D12CommandAllocator> gComputeAllocator;
ComPtr<ID3D12GraphicsCommandList> gComputeList;

ComPtr<ID3D12Fence> gComputeFence;
HANDLE gComputeEventHandle;
UINT64 gComputeFenceValue = 0;

enum HEAP_LOCATIONS : UINT
{
	UAV_RESULT = 0,
	SRV_TEXTURE,
	SRV_DCT_MATRIX,
	SRV_DCT_MATRIX_TRANSPOSE,
	SRV_INTERMEDIATE_RT,
	NUM_OF_SRV_CBV_UAV
};
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

ComPtr<ID3D12Resource> gDCT_Matrix_Res;
ComPtr<ID3D12Resource> gDCT_Matrix_Upload;

ComPtr<ID3D12Resource> gDCT_Matrix_Transpose_Res;
ComPtr<ID3D12Resource> gDCT_Matrix_Transpose_Upload;

ComPtr<ID3D12Resource> gWrittableRes;

int main()
{

	int width, height, comp;

	stbi_uc* data = (stbi_load("Images/test6.jpg", &width, &height, &comp, 4));
	comp		  = 4; // Force comp = 4

	Window window(VideoMode(width, height), L"JPEG Eye Tracking");
	ComPtr<IDXGIFactory4> pFactory;

#pragma region DEVICE
	UINT d3d11DeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
	D2D1_FACTORY_OPTIONS d2dFactoryOptions;
	{
		ComPtr<ID3D12Debug> debugController;
		UINT dxgiFactoryFlags = 0;

#ifdef _DEBUG
		if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();
			dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
			d3d11DeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
			d2dFactoryOptions.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
		}
		ComPtr<ID3D12Debug1> debugcontroller1;
		/*debugController->QueryInterface(IID_PPV_ARGS(&debugcontroller1));
		debugcontroller1->SetEnableGPUBasedValidation(true);*/
#endif

		ComPtr<IDXGIAdapter1> pAdapter;
		TIF(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&pFactory)));
		//Loop through and find adapter
		for(UINT adapterIndex = 0;
			DXGI_ERROR_NOT_FOUND != pFactory->EnumAdapters1(adapterIndex, &pAdapter);
			++adapterIndex)
		{

			DXGI_ADAPTER_DESC1 desc;
			pAdapter->GetDesc1(&desc);
			printf("%ls\n", desc.Description);

			if(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
				continue;

			if(SUCCEEDED(D3D12CreateDevice(
				   pAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&gDevice))))
			{
				break;
			}
		}

		NAME_D3D12_OBJECT(gDevice);
	}
#pragma endregion

#pragma region RTV
	// RTV
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type						= D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		desc.NumDescriptors				= NUM_BACK_BUFFERS;
		desc.Flags						= D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		desc.NodeMask					= 1;

		TIF(gDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&gRTVHeap)));
		NAME_D3D12_OBJECT(gRTVHeap);

		size_t rtvDescriptorSize =
			gDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = gRTVHeap->GetCPUDescriptorHandleForHeapStart();
		for(UINT i = 0; i < NUM_BACK_BUFFERS; i++)
		{
			gRTVDescHandles[i] = rtvHandle;
			rtvHandle.ptr += rtvDescriptorSize;
		}
	}
#pragma endregion

#pragma region CommandQueue
	{
		D3D12_COMMAND_QUEUE_DESC queueDesc = {};
		queueDesc.Type					   = D3D12_COMMAND_LIST_TYPE_DIRECT;
		queueDesc.Flags					   = D3D12_COMMAND_QUEUE_FLAG_NONE;
		queueDesc.NodeMask				   = 1;
		TIF(gDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&gDirectQueue)));
		NAME_D3D12_OBJECT(gDirectQueue);
		gDevice->CreateFence(gDirectFenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gDirectFence));
		gDirectEventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);

		queueDesc.Type	 = D3D12_COMMAND_LIST_TYPE_COMPUTE;
		queueDesc.Flags	= D3D12_COMMAND_QUEUE_FLAG_NONE;
		queueDesc.NodeMask = 1;
		TIF(gDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&gComputeQueue)));
		NAME_D3D12_OBJECT(gComputeQueue);
		gDevice->CreateFence(
			gComputeFenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gComputeFence));
		gComputeEventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	}
#pragma endregion

#pragma region D3D11OnD3D12

	{
		ComPtr<ID3D11Device> d3d11Device;

		TIF(D3D11On12CreateDevice(gDevice.Get(),
								  d3d11DeviceFlags,
								  nullptr,
								  0,
								  reinterpret_cast<IUnknown**>(gDirectQueue.GetAddressOf()),
								  1,
								  0,
								  &d3d11Device,
								  &gDevice11Context,
								  nullptr));

		TIF(d3d11Device.As(&gDevice11On12));
	}
#pragma endregion

#pragma region D2D / DWrite Components
	{

		TIF(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
							  __uuidof(ID2D1Factory3),
							  &d2dFactoryOptions,
							  &gD2DFactory));

		ComPtr<IDXGIDevice> dxgiDevice;
		TIF(gDevice11On12.As(&dxgiDevice));
		TIF(gD2DFactory->CreateDevice(dxgiDevice.Get(), &gD2DDevice));
		TIF(gD2DDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &gD2DDeviceContext));
		TIF(DWriteCreateFactory(
			DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &gDWriteFactory));
	}

#pragma endregion

#pragma region Swapchain
	{

		DXGI_SWAP_CHAIN_DESC1 sd = {};

		sd.BufferCount		  = NUM_BACK_BUFFERS;
		sd.Width			  = width;
		sd.Height			  = height;
		sd.Format			  = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.Flags			  = IsTearingSupported() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
		sd.BufferUsage		  = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.SampleDesc.Count   = 1;
		sd.SampleDesc.Quality = 0;
		sd.SwapEffect		  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		sd.AlphaMode		  = DXGI_ALPHA_MODE_UNSPECIFIED;
		sd.Scaling			  = DXGI_SCALING_STRETCH;
		sd.Stereo			  = FALSE;

		ComPtr<IDXGISwapChain1> swapChain1;
		TIF(pFactory->CreateSwapChainForHwnd(
			gDirectQueue.Get(), window.getHandle(), &sd, nullptr, nullptr, &swapChain1));
		TIF(swapChain1->QueryInterface(IID_PPV_ARGS(&gSwapChain)));

		gFrameIndex = gSwapChain->GetCurrentBackBufferIndex();

		// Set fullscreen window

		HWND windowHandle = window.getHandle();
		SetWindowLongW(windowHandle,
					   GWL_STYLE,
					   WS_OVERLAPPEDWINDOW & ~(WS_CAPTION | WS_MAXIMIZEBOX | WS_MINIMIZEBOX |
											   WS_SYSMENU | WS_THICKFRAME));

		ComPtr<IDXGIOutput> pOutput;
		TIF(gSwapChain->GetContainingOutput(&pOutput));
		DXGI_OUTPUT_DESC outDesc;
		pOutput->GetDesc(&outDesc);

		RECT fullscreenWindowRect = outDesc.DesktopCoordinates;

		SetWindowPos(windowHandle,
					 HWND_TOPMOST,
					 fullscreenWindowRect.left,
					 fullscreenWindowRect.top,
					 fullscreenWindowRect.right,
					 fullscreenWindowRect.bottom,
					 SWP_FRAMECHANGED | SWP_NOACTIVATE);

		ShowWindow(windowHandle, SW_MAXIMIZE);
	}
#pragma endregion

#pragma region Command Allocatrs and Lists
	{
		TIF(gDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
											IID_PPV_ARGS(&gDirectAllocator)));
		TIF(gDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE,
											IID_PPV_ARGS(&gComputeAllocator)));
		NAME_D3D12_OBJECT(gDirectAllocator);
		NAME_D3D12_OBJECT(gComputeAllocator);

		TIF(gDevice->CreateCommandList(1,
									   D3D12_COMMAND_LIST_TYPE_DIRECT,
									   gDirectAllocator.Get(),
									   nullptr,
									   IID_PPV_ARGS(&gDirectList)));
		TIF(gDevice->CreateCommandList(1,
									   D3D12_COMMAND_LIST_TYPE_COMPUTE,
									   gComputeAllocator.Get(),
									   nullptr,
									   IID_PPV_ARGS(&gComputeList)));
		NAME_D3D12_OBJECT(gDirectList);
		NAME_D3D12_OBJECT(gComputeList);
		//gDirectList->Close(); // Used to copy texture data
		gComputeList->Close();
	}
#pragma endregion

#pragma region SRV and UAV
	{

		D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
		srvHeapDesc.NumDescriptors			   = NUM_OF_SRV_CBV_UAV;
		srvHeapDesc.Type					   = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		srvHeapDesc.Flags					   = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

		TIF(gDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&gSRVHeap)));

		// Texture used
		{

			D3D12_RESOURCE_DESC textureDesc = {};
			textureDesc.MipLevels			= 1;
			textureDesc.Format				= DXGI_FORMAT_R8G8B8A8_UNORM;
			textureDesc.Width				= width;
			textureDesc.Height				= height;
			textureDesc.Flags				= D3D12_RESOURCE_FLAG_NONE;
			textureDesc.DepthOrArraySize	= 1;
			textureDesc.SampleDesc.Count	= 1;
			textureDesc.SampleDesc.Quality  = 0;
			textureDesc.Dimension			= D3D12_RESOURCE_DIMENSION_TEXTURE2D;

			D3D12_HEAP_PROPERTIES heapProp = {};
			heapProp.CPUPageProperty	   = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			heapProp.CreationNodeMask	  = 1;
			heapProp.MemoryPoolPreference  = D3D12_MEMORY_POOL_UNKNOWN;
			heapProp.Type				   = D3D12_HEAP_TYPE_DEFAULT;
			heapProp.VisibleNodeMask	   = 1;

			TIF(gDevice->CreateCommittedResource(&heapProp,
												 D3D12_HEAP_FLAG_NONE,
												 &textureDesc,
												 D3D12_RESOURCE_STATE_COPY_DEST,
												 nullptr,
												 IID_PPV_ARGS(&gInputImageRes)));
			NAME_D3D12_OBJECT(gInputImageRes);

			// Get the size needed for this texture buffer
			UINT64 uploadBufferSize;
			gDevice->GetCopyableFootprints(
				&gInputImageRes->GetDesc(), 0, 1, 0, nullptr, nullptr, nullptr, &uploadBufferSize);

			// This is the GPU upload buffer.
			D3D12_HEAP_PROPERTIES heapProp2 = {};
			heapProp2.CPUPageProperty		= D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			heapProp2.CreationNodeMask		= 1;
			heapProp2.MemoryPoolPreference  = D3D12_MEMORY_POOL_UNKNOWN;
			heapProp2.Type					= D3D12_HEAP_TYPE_UPLOAD;
			heapProp2.VisibleNodeMask		= 1;

			{

				D3D12_RESOURCE_DESC resDesc = {};
				resDesc.Dimension			= D3D12_RESOURCE_DIMENSION_BUFFER;
				resDesc.Alignment			= 0;
				resDesc.Format				= DXGI_FORMAT_UNKNOWN;
				resDesc.DepthOrArraySize	= 1;
				resDesc.Height				= 1;
				resDesc.Width				= uploadBufferSize;
				resDesc.Flags				= D3D12_RESOURCE_FLAG_NONE;
				resDesc.Layout				= D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
				resDesc.MipLevels			= 1;
				resDesc.SampleDesc.Count	= 1;

				TIF(gDevice->CreateCommittedResource(&heapProp2,
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
			textureData.pData				   = data;
			textureData.RowPitch			   = width * comp;
			textureData.SlicePitch			   = textureData.RowPitch * height;

			UINT64 requiredSize = 0;
			D3D12_PLACED_SUBRESOURCE_FOOTPRINT layouts;
			UINT NumRows;
			UINT64 RowSizeInBytes;

			D3D12_RESOURCE_DESC resDesc = gInputImageRes->GetDesc();
			gDevice->GetCopyableFootprints(
				&resDesc, 0, 1, 0, &layouts, &NumRows, &RowSizeInBytes, &requiredSize);

			BYTE* pData;
			TIF(gInputImageUpload->Map(0, nullptr, reinterpret_cast<LPVOID*>(&pData)));

			memcpy(pData, data, width * height * comp);

			gInputImageUpload->Unmap(0, nullptr);

			D3D12_TEXTURE_COPY_LOCATION texture0 = {};
			texture0.pResource					 = gInputImageRes.Get();
			texture0.Type						 = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			texture0.SubresourceIndex			 = 0;

			D3D12_TEXTURE_COPY_LOCATION texture1 = {};
			texture1.pResource					 = gInputImageUpload.Get();
			texture1.Type						 = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			texture1.PlacedFootprint			 = layouts;

			gDirectList->CopyTextureRegion(&texture0, 0, 0, 0, &texture1, nullptr);

			D3D12_RESOURCE_BARRIER barrier;
			barrier.Transition.pResource   = gInputImageRes.Get();
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
			barrier.Transition.Subresource = 0;
			barrier.Type				   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Flags				   = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			gDirectList->ResourceBarrier(1, &barrier);

			/*gDirectList->Close();

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
			gDirectFenceValue++;*/

			// Describe and create a SRV for the texture.
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Shader4ComponentMapping			= D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.Format							= textureDesc.Format;
			srvDesc.ViewDimension					= D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels				= 1;
			auto handle2 = gSRVHeap->GetCPUDescriptorHandleForHeapStart();
			handle2.ptr +=
				gDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			gDevice->CreateShaderResourceView(gInputImageRes.Get(), &srvDesc, handle2);
		}

		// Create the output resource
		{
			//The dimensions and format should match the swap-chain
			D3D12_RESOURCE_DESC resDesc = {};
			resDesc.DepthOrArraySize	= 1;
			resDesc.Dimension			= D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			resDesc.Format				= DXGI_FORMAT_R8G8B8A8_UNORM;
			resDesc.Flags				= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
			resDesc.Width				= width;
			resDesc.Height				= height;
			resDesc.Layout				= D3D12_TEXTURE_LAYOUT_UNKNOWN;
			resDesc.MipLevels			= 1;
			resDesc.SampleDesc.Count	= 1;

			D3D12_HEAP_PROPERTIES defaultHeapProps = {D3D12_HEAP_TYPE_DEFAULT,
													  D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
													  D3D12_MEMORY_POOL_UNKNOWN,
													  0,
													  0};
			TIF(gDevice->CreateCommittedResource(&defaultHeapProps,
												 D3D12_HEAP_FLAG_NONE,
												 &resDesc,
												 D3D12_RESOURCE_STATE_COPY_SOURCE,
												 nullptr,
												 IID_PPV_ARGS(&gWrittableRes)));
			NAME_D3D12_OBJECT(gWrittableRes);
			auto handle = gSRVHeap->GetCPUDescriptorHandleForHeapStart();

			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format							 = resDesc.Format;
			uavDesc.ViewDimension					 = D3D12_UAV_DIMENSION_TEXTURE2D;

			gDevice->CreateUnorderedAccessView(gWrittableRes.Get(), nullptr, &uavDesc, handle);
		}

		// Create the DCT_Matrix buffer
		{
			D3D12_RESOURCE_DESC textureDesc = {};
			textureDesc.MipLevels			= 1;
			textureDesc.Format				= DXGI_FORMAT_UNKNOWN;
			textureDesc.Width				= sizeof(float) * 64;
			textureDesc.Height				= 1;
			textureDesc.Flags				= D3D12_RESOURCE_FLAG_NONE;
			textureDesc.DepthOrArraySize	= 1;
			textureDesc.SampleDesc.Count	= 1;
			textureDesc.SampleDesc.Quality  = 0;
			textureDesc.Dimension			= D3D12_RESOURCE_DIMENSION_BUFFER;
			textureDesc.Layout				= D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

			D3D12_HEAP_PROPERTIES heapProp = {};
			heapProp.CPUPageProperty	   = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			heapProp.CreationNodeMask	  = 1;
			heapProp.MemoryPoolPreference  = D3D12_MEMORY_POOL_UNKNOWN;
			heapProp.Type				   = D3D12_HEAP_TYPE_DEFAULT;
			heapProp.VisibleNodeMask	   = 1;

			TIF(gDevice->CreateCommittedResource(&heapProp,
												 D3D12_HEAP_FLAG_NONE,
												 &textureDesc,
												 D3D12_RESOURCE_STATE_COPY_DEST,
												 nullptr,
												 IID_PPV_ARGS(&gDCT_Matrix_Res)));

			// Get the size needed for this texture buffer
			UINT64 uploadBufferSize;
			gDevice->GetCopyableFootprints(
				&gDCT_Matrix_Res->GetDesc(), 0, 1, 0, nullptr, nullptr, nullptr, &uploadBufferSize);

			// This is the GPU upload buffer.
			D3D12_HEAP_PROPERTIES heapProp2 = {};
			heapProp2.CPUPageProperty		= D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			heapProp2.CreationNodeMask		= 1;
			heapProp2.MemoryPoolPreference  = D3D12_MEMORY_POOL_UNKNOWN;
			heapProp2.Type					= D3D12_HEAP_TYPE_UPLOAD;
			heapProp2.VisibleNodeMask		= 1;

			{

				D3D12_RESOURCE_DESC resDesc = {};
				resDesc.Dimension			= D3D12_RESOURCE_DIMENSION_BUFFER;
				resDesc.Alignment			= 0;
				resDesc.Format				= DXGI_FORMAT_UNKNOWN;
				resDesc.DepthOrArraySize	= 1;
				resDesc.Height				= 1;
				resDesc.Width				= uploadBufferSize;
				resDesc.Flags				= D3D12_RESOURCE_FLAG_NONE;
				resDesc.Layout				= D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
				resDesc.MipLevels			= 1;
				resDesc.SampleDesc.Count	= 1;

				TIF(gDevice->CreateCommittedResource(&heapProp2,
													 D3D12_HEAP_FLAG_NONE,
													 &resDesc,
													 D3D12_RESOURCE_STATE_GENERIC_READ,
													 nullptr,
													 IID_PPV_ARGS(&gDCT_Matrix_Upload)));
			}

			UINT64 requiredSize = 0;
			D3D12_PLACED_SUBRESOURCE_FOOTPRINT layouts;
			UINT NumRows;
			UINT64 RowSizeInBytes;

			D3D12_RESOURCE_DESC resDesc = gDCT_Matrix_Res->GetDesc();
			gDevice->GetCopyableFootprints(
				&resDesc, 0, 1, 0, &layouts, &NumRows, &RowSizeInBytes, &requiredSize);

			BYTE* pData;
			TIF(gDCT_Matrix_Upload->Map(0, nullptr, reinterpret_cast<LPVOID*>(&pData)));

			float DCT_MATRIX[64];
			//compute dct matrix
			for(int y = 0; y < 8; y++)
				for(int x = 0; x < 8; x++)
					if(0 == y)
						DCT_MATRIX[y * 8 + x] = float(1.0 / sqrt(8.0));
					else
						DCT_MATRIX[y * 8 + x] =
							float(sqrt(2.0 / 8.0) *
								  cos(((2 * x + 1) * DirectX::XM_PI * y) / (2.0 * 8.0)));

			memcpy(pData, DCT_MATRIX, sizeof(float) * 64);

			gDCT_Matrix_Upload->Unmap(0, nullptr);

			gDirectList->CopyBufferRegion(gDCT_Matrix_Res.Get(),
										  0,
										  gDCT_Matrix_Upload.Get(),
										  layouts.Offset,
										  layouts.Footprint.RowPitch);

			D3D12_RESOURCE_BARRIER barrier;
			barrier.Transition.pResource   = gDCT_Matrix_Res.Get();
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
			barrier.Transition.Subresource = 0;
			barrier.Type				   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Flags				   = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			gDirectList->ResourceBarrier(1, &barrier);

			// Describe and create a SRV for the texture.
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Shader4ComponentMapping			= D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.Format							= DXGI_FORMAT_UNKNOWN;
			srvDesc.ViewDimension					= D3D12_SRV_DIMENSION_BUFFER;
			srvDesc.Buffer.FirstElement				= 0;
			srvDesc.Buffer.NumElements				= 64;
			srvDesc.Buffer.StructureByteStride		= sizeof(float);
			auto handle2 = gSRVHeap->GetCPUDescriptorHandleForHeapStart();
			handle2.ptr +=
				(gDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) *
				 2);
			gDevice->CreateShaderResourceView(gDCT_Matrix_Res.Get(), &srvDesc, handle2);
		}
		// Create the DCT_Matrix_Transpose
		{
			D3D12_RESOURCE_DESC textureDesc = {};
			textureDesc.MipLevels			= 1;
			textureDesc.Format				= DXGI_FORMAT_UNKNOWN;
			textureDesc.Width				= sizeof(float) * 64;
			textureDesc.Height				= 1;
			textureDesc.Flags				= D3D12_RESOURCE_FLAG_NONE;
			textureDesc.DepthOrArraySize	= 1;
			textureDesc.SampleDesc.Count	= 1;
			textureDesc.SampleDesc.Quality  = 0;
			textureDesc.Dimension			= D3D12_RESOURCE_DIMENSION_BUFFER;
			textureDesc.Layout				= D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

			D3D12_HEAP_PROPERTIES heapProp = {};
			heapProp.CPUPageProperty	   = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			heapProp.CreationNodeMask	  = 1;
			heapProp.MemoryPoolPreference  = D3D12_MEMORY_POOL_UNKNOWN;
			heapProp.Type				   = D3D12_HEAP_TYPE_DEFAULT;
			heapProp.VisibleNodeMask	   = 1;

			TIF(gDevice->CreateCommittedResource(&heapProp,
												 D3D12_HEAP_FLAG_NONE,
												 &textureDesc,
												 D3D12_RESOURCE_STATE_COPY_DEST,
												 nullptr,
												 IID_PPV_ARGS(&gDCT_Matrix_Transpose_Res)));

			// Get the size needed for this texture buffer
			UINT64 uploadBufferSize;
			gDevice->GetCopyableFootprints(&gDCT_Matrix_Transpose_Res->GetDesc(),
										   0,
										   1,
										   0,
										   nullptr,
										   nullptr,
										   nullptr,
										   &uploadBufferSize);

			// This is the GPU upload buffer.
			D3D12_HEAP_PROPERTIES heapProp2 = {};
			heapProp2.CPUPageProperty		= D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			heapProp2.CreationNodeMask		= 1;
			heapProp2.MemoryPoolPreference  = D3D12_MEMORY_POOL_UNKNOWN;
			heapProp2.Type					= D3D12_HEAP_TYPE_UPLOAD;
			heapProp2.VisibleNodeMask		= 1;

			{

				D3D12_RESOURCE_DESC resDesc = {};
				resDesc.Dimension			= D3D12_RESOURCE_DIMENSION_BUFFER;
				resDesc.Alignment			= 0;
				resDesc.Format				= DXGI_FORMAT_UNKNOWN;
				resDesc.DepthOrArraySize	= 1;
				resDesc.Height				= 1;
				resDesc.Width				= uploadBufferSize;
				resDesc.Flags				= D3D12_RESOURCE_FLAG_NONE;
				resDesc.Layout				= D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
				resDesc.MipLevels			= 1;
				resDesc.SampleDesc.Count	= 1;

				TIF(gDevice->CreateCommittedResource(&heapProp2,
													 D3D12_HEAP_FLAG_NONE,
													 &resDesc,
													 D3D12_RESOURCE_STATE_GENERIC_READ,
													 nullptr,
													 IID_PPV_ARGS(&gDCT_Matrix_Transpose_Upload)));
			}

			UINT64 requiredSize = 0;
			D3D12_PLACED_SUBRESOURCE_FOOTPRINT layouts;
			UINT NumRows;
			UINT64 RowSizeInBytes;

			D3D12_RESOURCE_DESC resDesc = gDCT_Matrix_Transpose_Res->GetDesc();
			gDevice->GetCopyableFootprints(
				&resDesc, 0, 1, 0, &layouts, &NumRows, &RowSizeInBytes, &requiredSize);

			BYTE* pData;
			TIF(gDCT_Matrix_Transpose_Upload->Map(0, nullptr, reinterpret_cast<LPVOID*>(&pData)));

			float DCT_MATRIX2[64];
			float DCT_MATRIX_TRANSPOSE[64];
			//compute dct matrix
			for(int y = 0; y < 8; y++)
				for(int x = 0; x < 8; x++)
					if(0 == y)
						DCT_MATRIX2[y * 8 + x] = float(1.0 / sqrt(8.0));
					else
						DCT_MATRIX2[y * 8 + x] =
							float(sqrt(2.0 / 8.0) *
								  cos(((2 * x + 1) * DirectX::XM_PI * y) / (2.0 * 8.0)));

			//compute dct transpose matrix
			for(int y = 0; y < 8; y++)
				for(int x = 0; x < 8; x++)
					DCT_MATRIX_TRANSPOSE[y * 8 + x] = DCT_MATRIX2[x * 8 + y];

			memcpy(pData, DCT_MATRIX_TRANSPOSE, sizeof(float) * 64);

			gDCT_Matrix_Transpose_Upload->Unmap(0, nullptr);

			gDirectList->CopyBufferRegion(gDCT_Matrix_Transpose_Res.Get(),
										  0,
										  gDCT_Matrix_Transpose_Upload.Get(),
										  layouts.Offset,
										  layouts.Footprint.RowPitch);

			D3D12_RESOURCE_BARRIER barrier;
			barrier.Transition.pResource   = gDCT_Matrix_Transpose_Res.Get();
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
			barrier.Transition.Subresource = 0;
			barrier.Type				   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Flags				   = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			gDirectList->ResourceBarrier(1, &barrier);

			gDirectList->Close();

			{
				ID3D12CommandList* listsToExceute[] = {gDirectList.Get()};
				gDirectQueue->ExecuteCommandLists(1, listsToExceute);
			}

			gDirectQueue->Signal(gDirectFence.Get(), gDirectFenceValue);
			if(gDirectFenceValue > gDirectFence->GetCompletedValue())
			{
				gDirectFence->SetEventOnCompletion(gDirectFenceValue, gDirectEventHandle);
				WaitForMultipleObjects(1, &gDirectEventHandle, TRUE, INFINITE);
			}
			gDirectFenceValue++;

			// Describe and create a SRV for the texture.
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Shader4ComponentMapping			= D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.Format							= DXGI_FORMAT_UNKNOWN;
			srvDesc.ViewDimension					= D3D12_SRV_DIMENSION_BUFFER;
			srvDesc.Buffer.FirstElement				= 0;
			srvDesc.Buffer.NumElements				= 64;
			srvDesc.Buffer.StructureByteStride		= sizeof(float);
			auto handle2 = gSRVHeap->GetCPUDescriptorHandleForHeapStart();
			handle2.ptr +=
				(gDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) *
				 3);
			gDevice->CreateShaderResourceView(gDCT_Matrix_Transpose_Res.Get(), &srvDesc, handle2);
		}
	}
#pragma endregion

#pragma region RenderTarget Creation
	{
		FLOAT dpiX, dpiY;
		gD2DFactory->GetDesktopDpi(&dpiX, &dpiY);
		D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
			D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
			D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED),
			dpiX,
			dpiY);

		for(UINT i = 0; i < NUM_BACK_BUFFERS; i++)
		{

			TIF(gSwapChain->GetBuffer(i, IID_PPV_ARGS(&gRTResource[i])));
			gDevice->CreateRenderTargetView(gRTResource[i].Get(), nullptr, gRTVDescHandles[i]);

			NAME_D3D12_OBJECT_INDEXED(gRTResource, i);

			/* Create a wrapped 11on12 resource of this back buffer. Since we are
			 rendering all D3D12 content first and then all D2D content, we specify
			 the In resource state as RENDER_TARGET - because D3D12 will have last
			 used it in this state - and the Out resource state as PRESENT. When
			 ReleaseWrappedResources() is called on the 11on12 device, the resource 
			 will be transitioned to the PRESENT state.*/

			D3D11_RESOURCE_FLAGS d3d11Flags = {D3D11_BIND_RENDER_TARGET};
			TIF(gDevice11On12->CreateWrappedResource(gRTResource[i].Get(),
													 &d3d11Flags,
													 D3D12_RESOURCE_STATE_RENDER_TARGET,
													 D3D12_RESOURCE_STATE_PRESENT,
													 IID_PPV_ARGS(&gWrappedResource[i])));

			// Create a render target for the D2D to draw directly to this back buffer.
			ComPtr<IDXGISurface> surface;
			TIF(gWrappedResource[i].As(&surface));
			TIF(gD2DDeviceContext->CreateBitmapFromDxgiSurface(
				surface.Get(), &bitmapProperties, &gD2DRenderTarget[i]));
		}
	}
#pragma endregion

#pragma region D2D / DWrite Objects
	{
		TIF(gD2DDeviceContext->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::AntiqueWhite),
													 &gTextBrush));

		TIF(gDWriteFactory->CreateTextFormat(L"Verdana",
											 nullptr,
											 DWRITE_FONT_WEIGHT_NORMAL,
											 DWRITE_FONT_STYLE_NORMAL,
											 DWRITE_FONT_STRETCH_NORMAL,
											 45,
											 L"en-us",
											 &gDTextFormat));
		TIF(gDTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER));
		TIF(gDTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER));
	}
#pragma endregion

#pragma region Compute Root Signature
	{
		D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
		featureData.HighestVersion					  = D3D_ROOT_SIGNATURE_VERSION_1_1;
		if(FAILED(gDevice->CheckFeatureSupport(
			   D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
		{
			featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
		}

		D3D12_DESCRIPTOR_RANGE1 ranges[2];
		ranges[1].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		ranges[1].NumDescriptors					= 3;
		ranges[1].BaseShaderRegister				= 0;
		ranges[1].RegisterSpace						= 0;
		ranges[1].Flags								= D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
		ranges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		ranges[0].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		ranges[0].NumDescriptors					= 1;
		ranges[0].BaseShaderRegister				= 0;
		ranges[0].RegisterSpace						= 0;
		ranges[0].Flags								= D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
		ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		D3D12_ROOT_PARAMETER1 rootParameters[3];
		rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
		rootParameters[0].DescriptorTable.pDescriptorRanges   = &ranges[0];
		rootParameters[0].ShaderVisibility					  = D3D12_SHADER_VISIBILITY_ALL;

		rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
		rootParameters[1].DescriptorTable.pDescriptorRanges   = &ranges[1];
		rootParameters[1].ShaderVisibility					  = D3D12_SHADER_VISIBILITY_ALL;

		rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		rootParameters[2].Constants.Num32BitValues =
			4; // two for gaze point, two for quality and circle ratio
		rootParameters[2].Constants.RegisterSpace  = 0;
		rootParameters[2].Constants.ShaderRegister = 0;
		rootParameters[2].ShaderVisibility		   = D3D12_SHADER_VISIBILITY_ALL;

		D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSig;
		rootSig.Version					   = D3D_ROOT_SIGNATURE_VERSION_1_1;
		rootSig.Desc_1_1.NumParameters	 = _countof(rootParameters);
		rootSig.Desc_1_1.pParameters	   = rootParameters;
		rootSig.Desc_1_1.NumStaticSamplers = 0;
		rootSig.Desc_1_1.pStaticSamplers   = nullptr;
		rootSig.Desc_1_1.Flags			   = D3D12_ROOT_SIGNATURE_FLAG_NONE;

		ComPtr<ID3DBlob> signature;
		ComPtr<ID3DBlob> error;

		TIF(D3D12SerializeVersionedRootSignature(&rootSig, &signature, &error));
		if(error)
		{
			std::cout << (char*)error->GetBufferPointer() << std::endl;
		}
		TIF(gDevice->CreateRootSignature(0,
										 signature->GetBufferPointer(),
										 signature->GetBufferSize(),
										 IID_PPV_ARGS(&gRootCompute)));

		NAME_D3D12_OBJECT(gRootCompute);
	}
#pragma endregion

#pragma region Compute Pipeline
	{
		ComPtr<ID3DBlob> computeBlob;
		ComPtr<ID3DBlob> errorPtr;
		TIF(D3DCompileFromFile(L"Paletter/Shaders/JPEGDecodeEncode.hlsl",
							   nullptr,
							   nullptr,
							   "main",
							   "cs_5_1",
							   0,
							   0,
							   &computeBlob,
							   &errorPtr));

		if(errorPtr)
		{
			std::cout << (char*)errorPtr->GetBufferPointer() << std::endl;
		}
		D3D12_COMPUTE_PIPELINE_STATE_DESC cpsd = {};
		cpsd.pRootSignature					   = gRootCompute.Get();
		cpsd.CS.pShaderBytecode				   = computeBlob->GetBufferPointer();
		cpsd.CS.BytecodeLength				   = computeBlob->GetBufferSize();
		cpsd.NodeMask						   = 0;

		TIF(gDevice->CreateComputePipelineState(&cpsd, IID_PPV_ARGS(&gComputePipeline)));
	}
#pragma endregion

	// Wait for compute
	gComputeQueue->Signal(gComputeFence.Get(), gComputeFenceValue);
	if(gComputeFenceValue > gComputeFence->GetCompletedValue())
	{
		gComputeFence->SetEventOnCompletion(gComputeFenceValue, gComputeEventHandle);
		WaitForMultipleObjects(1, &gComputeEventHandle, TRUE, INFINITE);
	}
	gComputeFenceValue++;

#ifdef TOBII
	tobii_api_t* pApi;
	tobii_error_t error = tobii_api_create(&pApi, NULL, NULL);
	assert(error == TOBII_ERROR_NO_ERROR);

	char url[256] = {};
	error		  = tobii_enumerate_local_device_urls(pApi, urlReciever, url);
	assert(error == TOBII_ERROR_NO_ERROR && *url != '\0');

	tobii_device_t* pDevice;
	error = tobii_device_create(pApi, url, &pDevice);
	assert(error == TOBII_ERROR_NO_ERROR);

	error = tobii_gaze_point_subscribe(pDevice, gazePointCallback, 0);
	assert(error == TOBII_ERROR_NO_ERROR);
#endif
	Timer t;
	float timeCounter		  = 0;
	unsigned int stageCounter = 0;

	while(window.isOpen() && (false == Input::IsKeyPressed(VK_ESCAPE)))
	{
		float dt = t.RestartAndGetElapsedTimeMS();

		window.pollEvents();

#ifdef TOBII
		error = tobii_wait_for_callbacks(NULL, 1, &pDevice);
		assert(error == TOBII_ERROR_NO_ERROR || error == TOBII_ERROR_TIMED_OUT);

		error = tobii_device_process_callbacks(pDevice);
		assert(error == TOBII_ERROR_NO_ERROR);
#endif
		if(stageCounter == MAX_STAGES)
		{
			gCurrentState = States::FINISHED;
		}
		switch(gCurrentState)
		{
		case States::INTRO:
		{
			//Logic

			// Start the test
			if(Input::IsKeyTyped(VK_RETURN))
			{
				// Start with the RAW image
				gCurrentState = States::PREPARE_RAW;
				timeCounter   = Constants::PREPARE_TIME_MS;
			}

			// Render

			gDirectAllocator->Reset();
			gDirectList->Reset(gDirectAllocator.Get(), nullptr);
			{
				D3D12_RESOURCE_BARRIER barrier = {};
				barrier.Type				   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barrier.Transition.pResource   = gRTResource[gFrameIndex].Get();
				barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
				barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
				gDirectList->ResourceBarrier(1, &barrier);
			}

			gDirectList->Close();
			{
				ID3D12CommandList* listsToExceute[] = {gDirectList.Get()};
				gDirectQueue->ExecuteCommandLists(1, listsToExceute);
			}

			D2D1_SIZE_F rtSize   = gD2DRenderTarget[gFrameIndex]->GetSize();
			D2D1_RECT_F textRect = D2D1::RectF(0, 0, rtSize.width, rtSize.height);

			// Accquire our wrapped render target resource for he current back buffer.
			gDevice11On12->AcquireWrappedResources(gWrappedResource[gFrameIndex].GetAddressOf(), 1);

			// Render text directly to the backbuffer.
			gD2DDeviceContext->SetTarget(gD2DRenderTarget[gFrameIndex].Get());
			gD2DDeviceContext->BeginDraw();
			gD2DDeviceContext->Clear();
			gD2DDeviceContext->SetTransform(D2D1::Matrix3x2F::Translation(0, 0));
			gD2DDeviceContext->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_DEFAULT);
			gD2DDeviceContext->DrawTextW(Constants::IntroText,
										 ARRAYSIZE(Constants::IntroText),
										 gDTextFormat.Get(),
										 &textRect,
										 gTextBrush.Get());
			gD2DDeviceContext->EndDraw();

			// Release our wrapped render target resource. Releasing
			// transitions the back buffer resource to the state
			// specified as the OutState when the wrapped resource was
			// created.
			gDevice11On12->ReleaseWrappedResources(gWrappedResource[gFrameIndex].GetAddressOf(), 1);
			// Flush to submit the 11 command list to the shared command queue
			gDevice11Context->Flush();
		}
		break;
		case States::FINISHED:
		{
			//Logic

			// Start the test
			if(Input::IsKeyTyped(VK_RETURN))
			{
				// Exit the main loop
				window.closeWindow();
			}

			// Render

			gDirectAllocator->Reset();
			gDirectList->Reset(gDirectAllocator.Get(), nullptr);
			{
				D3D12_RESOURCE_BARRIER barrier = {};
				barrier.Type				   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barrier.Transition.pResource   = gRTResource[gFrameIndex].Get();
				barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
				barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
				gDirectList->ResourceBarrier(1, &barrier);
			}

			gDirectList->Close();
			{
				ID3D12CommandList* listsToExceute[] = {gDirectList.Get()};
				gDirectQueue->ExecuteCommandLists(1, listsToExceute);
			}

			D2D1_SIZE_F rtSize   = gD2DRenderTarget[gFrameIndex]->GetSize();
			D2D1_RECT_F textRect = D2D1::RectF(0, 0, rtSize.width, rtSize.height);

			// Accquire our wrapped render target resource for he current back buffer.
			gDevice11On12->AcquireWrappedResources(gWrappedResource[gFrameIndex].GetAddressOf(), 1);

			// Render text directly to the backbuffer.
			gD2DDeviceContext->SetTarget(gD2DRenderTarget[gFrameIndex].Get());
			gD2DDeviceContext->BeginDraw();
			gD2DDeviceContext->Clear();
			gD2DDeviceContext->SetTransform(D2D1::Matrix3x2F::Translation(0, 0));
			gD2DDeviceContext->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_DEFAULT);
			gD2DDeviceContext->DrawTextW(Constants::ExitText,
										 ARRAYSIZE(Constants::ExitText),
										 gDTextFormat.Get(),
										 &textRect,
										 gTextBrush.Get());
			gD2DDeviceContext->EndDraw();

			// Release our wrapped render target resource. Releasing
			// transitions the back buffer resource to the state
			// specified as the OutState when the wrapped resource was
			// created.
			gDevice11On12->ReleaseWrappedResources(gWrappedResource[gFrameIndex].GetAddressOf(), 1);
			// Flush to submit the 11 command list to the shared command queue
			gDevice11Context->Flush();
		}
		break;
		case States::SELECTION:
		{

			//Logic
			WCHAR displayText[256];
			int textSize;
			textSize = swprintf_s(displayText,
								  L"Stage[%i/%i]\n\n%s",
								  stageCounter,
								  MAX_STAGES,
								  Constants::SelectionText);
			if(Input::IsKeyPressed('1'))
			{
				gCurrentState		   = States::PREPARE_RAW;
				gResults[stageCounter] = 1;
				timeCounter			   = Constants::PREPARE_TIME_MS;
				stageCounter++;
			}
			else if(Input::IsKeyPressed('2'))
			{
				gCurrentState		   = States::PREPARE_RAW;
				timeCounter			   = Constants::PREPARE_TIME_MS;
				gResults[stageCounter] = 2;
				stageCounter++;
			} // Render

			gDirectAllocator->Reset();
			gDirectList->Reset(gDirectAllocator.Get(), nullptr);
			{
				D3D12_RESOURCE_BARRIER barrier = {};
				barrier.Type				   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barrier.Transition.pResource   = gRTResource[gFrameIndex].Get();
				barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
				barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
				gDirectList->ResourceBarrier(1, &barrier);
			}

			gDirectList->Close();
			{
				ID3D12CommandList* listsToExceute[] = {gDirectList.Get()};
				gDirectQueue->ExecuteCommandLists(1, listsToExceute);
			}

			D2D1_SIZE_F rtSize   = gD2DRenderTarget[gFrameIndex]->GetSize();
			D2D1_RECT_F textRect = D2D1::RectF(0, 0, rtSize.width, rtSize.height);

			// Accquire our wrapped render target resource for he current back buffer.
			gDevice11On12->AcquireWrappedResources(gWrappedResource[gFrameIndex].GetAddressOf(), 1);

			// Render text directly to the backbuffer.
			gD2DDeviceContext->SetTarget(gD2DRenderTarget[gFrameIndex].Get());
			gD2DDeviceContext->BeginDraw();
			gD2DDeviceContext->Clear();
			gD2DDeviceContext->SetTransform(D2D1::Matrix3x2F::Translation(0, 0));
			gD2DDeviceContext->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_DEFAULT);
			gD2DDeviceContext->DrawTextW(displayText, textSize,
										 gDTextFormat.Get(),
										 &textRect,
										 gTextBrush.Get());
			gD2DDeviceContext->EndDraw();

			// Release our wrapped render target resource. Releasing
			// transitions the back buffer resource to the state
			// specified as the OutState when the wrapped resource was
			// created.
			gDevice11On12->ReleaseWrappedResources(gWrappedResource[gFrameIndex].GetAddressOf(), 1);
			// Flush to submit the 11 command list to the shared command queue
			gDevice11Context->Flush();
		}
		break;
		case States::PREPARE_RAW:
		{
			timeCounter -= dt;
			if(timeCounter < 1)
			{
				gCurrentState = States::PRESENT_RAW;
				timeCounter   = 0;
			}
			int displayNumber = timeCounter / 1000;
			//Logic

			// RAW image is always image number 1
			WCHAR displayText[32];
			int size = swprintf_s(displayText, L"Image [1]\n %i", displayNumber);

			// Render

			gDirectAllocator->Reset();
			gDirectList->Reset(gDirectAllocator.Get(), nullptr);
			{
				D3D12_RESOURCE_BARRIER barrier = {};
				barrier.Type				   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barrier.Transition.pResource   = gRTResource[gFrameIndex].Get();
				barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
				barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
				gDirectList->ResourceBarrier(1, &barrier);
			}

			gDirectList->Close();
			{
				ID3D12CommandList* listsToExceute[] = {gDirectList.Get()};
				gDirectQueue->ExecuteCommandLists(1, listsToExceute);
			}

			D2D1_SIZE_F rtSize   = gD2DRenderTarget[gFrameIndex]->GetSize();
			D2D1_RECT_F textRect = D2D1::RectF(0, 0, rtSize.width, rtSize.height);

			// Accquire our wrapped render target resource for he current back buffer.
			gDevice11On12->AcquireWrappedResources(gWrappedResource[gFrameIndex].GetAddressOf(), 1);

			// Render text directly to the backbuffer.
			gD2DDeviceContext->SetTarget(gD2DRenderTarget[gFrameIndex].Get());
			gD2DDeviceContext->BeginDraw();
			gD2DDeviceContext->Clear();
			gD2DDeviceContext->SetTransform(D2D1::Matrix3x2F::Translation(0, 0));
			gD2DDeviceContext->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_DEFAULT);
			gD2DDeviceContext->DrawTextW(
				displayText, size, gDTextFormat.Get(), &textRect, gTextBrush.Get());
			gD2DDeviceContext->EndDraw();

			// Release our wrapped render target resource. Releasing
			// transitions the back buffer resource to the state
			// specified as the OutState when the wrapped resource was
			// created.
			gDevice11On12->ReleaseWrappedResources(gWrappedResource[gFrameIndex].GetAddressOf(), 1);
			// Flush to submit the 11 command list to the shared command queue
			gDevice11Context->Flush();
		}
		break;
		case States::PREPARE_JPEG:
		{
			timeCounter -= dt;
			if(timeCounter < 1)
			{
				gCurrentState = States::PRESENT_JPEG;
				timeCounter   = 0;
			}
			int displayNumber = timeCounter / 1000;
			//Logic

			// RAW image is always image number 1
			WCHAR displayText[32];
			int size = swprintf_s(displayText, L"Image [2]\n %i", displayNumber);

			// Render

			gDirectAllocator->Reset();
			gDirectList->Reset(gDirectAllocator.Get(), nullptr);
			{
				D3D12_RESOURCE_BARRIER barrier = {};
				barrier.Type				   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barrier.Transition.pResource   = gRTResource[gFrameIndex].Get();
				barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
				barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
				gDirectList->ResourceBarrier(1, &barrier);
			}

			gDirectList->Close();
			{
				ID3D12CommandList* listsToExceute[] = {gDirectList.Get()};
				gDirectQueue->ExecuteCommandLists(1, listsToExceute);
			}

			D2D1_SIZE_F rtSize   = gD2DRenderTarget[gFrameIndex]->GetSize();
			D2D1_RECT_F textRect = D2D1::RectF(0, 0, rtSize.width, rtSize.height);

			// Accquire our wrapped render target resource for he current back buffer.
			gDevice11On12->AcquireWrappedResources(gWrappedResource[gFrameIndex].GetAddressOf(), 1);

			// Render text directly to the backbuffer.
			gD2DDeviceContext->SetTarget(gD2DRenderTarget[gFrameIndex].Get());
			gD2DDeviceContext->BeginDraw();
			gD2DDeviceContext->Clear();
			gD2DDeviceContext->SetTransform(D2D1::Matrix3x2F::Translation(0, 0));
			gD2DDeviceContext->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_DEFAULT);
			gD2DDeviceContext->DrawTextW(
				displayText, size, gDTextFormat.Get(), &textRect, gTextBrush.Get());
			gD2DDeviceContext->EndDraw();

			// Release our wrapped render target resource. Releasing
			// transitions the back buffer resource to the state
			// specified as the OutState when the wrapped resource was
			// created.
			gDevice11On12->ReleaseWrappedResources(gWrappedResource[gFrameIndex].GetAddressOf(), 1);
			// Flush to submit the 11 command list to the shared command queue
			gDevice11Context->Flush();
		}
		break;
		case States::PRESENT_JPEG:
		{
			//Logic

			timeCounter += dt;
			if(timeCounter > Constants::DISPLAY_TIME_MS)
			{
				// After JPEG we go to select screen!
				gCurrentState = States::SELECTION;
			}
			// Render
			TIF(gComputeAllocator->Reset());
			gComputeList->Reset(gComputeAllocator.Get(), gComputePipeline.Get());

			gComputeList->SetComputeRootSignature(gRootCompute.Get());

			ID3D12DescriptorHeap* heaps[] = {gSRVHeap.Get()};
			gComputeList->SetDescriptorHeaps(1, heaps);

			{
				D3D12_RESOURCE_BARRIER barrier1 = {};
				barrier1.Type					= D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barrier1.Transition.pResource   = gWrittableRes.Get();
				barrier1.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				barrier1.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
				barrier1.Transition.StateAfter  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
				gComputeList->ResourceBarrier(1, &barrier1);
			}

			// Run Compute
			auto h = gSRVHeap->GetGPUDescriptorHandleForHeapStart();
			UINT size =
				gDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			gComputeList->SetComputeRootDescriptorTable(0, h);
			h.ptr += size;
			gComputeList->SetComputeRootDescriptorTable(1, h);

			//printf("Gaze Point(%i,%i)\n", gazePoint[0], gazePoint[1]);
#ifndef TOBII
			auto pos	  = Input::GetMousePosition();
			gGazePoint[0] = pos.x;
			gGazePoint[1] = pos.y;
#endif
			struct P
			{
				int point[2];
				JPEG_SETTINGS setting;
			} payload;
			payload.point[0] = gGazePoint[0];
			payload.point[1] = gGazePoint[1];
			payload.setting  = settings[stageCounter];
			gComputeList->SetComputeRoot32BitConstants(
				2, 4, reinterpret_cast<const LPVOID>(&payload), 0);
			gComputeList->Dispatch((width / 8), (height / 8), 1);

			{
				D3D12_RESOURCE_BARRIER barrier = {};
				barrier.Type				   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barrier.Transition.pResource   = gWrittableRes.Get();
				barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
				barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
				gComputeList->ResourceBarrier(1, &barrier);
			}
			gComputeList->Close();

			{
				ID3D12CommandList* listsToExceute[] = {gComputeList.Get()};
				gComputeQueue->ExecuteCommandLists(1, listsToExceute);
			}

			// Wait for compute
			gComputeQueue->Signal(gComputeFence.Get(), gComputeFenceValue);
			if(gComputeFenceValue > gComputeFence->GetCompletedValue())
			{
				gComputeFence->SetEventOnCompletion(gComputeFenceValue, gComputeEventHandle);
				WaitForMultipleObjects(1, &gComputeEventHandle, TRUE, INFINITE);
			}
			gComputeFenceValue++;

			gDirectAllocator->Reset();
			gDirectList->Reset(gDirectAllocator.Get(), nullptr);
			//Copy from compute shader to backbuffer
			{
				D3D12_RESOURCE_BARRIER barrier = {};
				barrier.Type				   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barrier.Transition.pResource   = gRTResource[gFrameIndex].Get();
				barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
				barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
				gDirectList->ResourceBarrier(1, &barrier);
			}

			gDirectList->CopyResource(gRTResource[gFrameIndex].Get(), gWrittableRes.Get());

			{
				D3D12_RESOURCE_BARRIER barrier = {};
				barrier.Type				   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barrier.Transition.pResource   = gRTResource[gFrameIndex].Get();
				barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
				barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
				gDirectList->ResourceBarrier(1, &barrier);
			}

			gDirectList->Close();
			{
				ID3D12CommandList* listsToExceute[] = {gDirectList.Get()};
				gDirectQueue->ExecuteCommandLists(1, listsToExceute);
			}
		}
		break;
		case States::PRESENT_RAW:
		{
			timeCounter += dt;
			if(timeCounter > Constants::DISPLAY_TIME_MS)
			{
				// After RAW we go to JPEG
				gCurrentState = States::PREPARE_JPEG;
				timeCounter   = Constants::PREPARE_TIME_MS;
			}
			// Render
			gDirectAllocator->Reset();
			gDirectList->Reset(gDirectAllocator.Get(), nullptr);
			//Copy from compute shader to backbuffer
			{
				D3D12_RESOURCE_BARRIER barrier = {};
				barrier.Type				   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barrier.Transition.pResource   = gRTResource[gFrameIndex].Get();
				barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
				barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
				gDirectList->ResourceBarrier(1, &barrier);

				D3D12_RESOURCE_BARRIER barrier2 = {};
				barrier2.Type					= D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barrier2.Transition.pResource   = gInputImageRes.Get();
				barrier2.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				barrier2.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
				barrier2.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
				gDirectList->ResourceBarrier(1, &barrier2);
			}

			gDirectList->CopyResource(gRTResource[gFrameIndex].Get(), gInputImageRes.Get());

			{
				D3D12_RESOURCE_BARRIER barrier = {};
				barrier.Type				   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barrier.Transition.pResource   = gRTResource[gFrameIndex].Get();
				barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
				barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
				gDirectList->ResourceBarrier(1, &barrier);

				D3D12_RESOURCE_BARRIER barrier2 = {};
				barrier2.Type					= D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barrier2.Transition.pResource   = gInputImageRes.Get();
				barrier2.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				barrier2.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
				barrier2.Transition.StateAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
				gDirectList->ResourceBarrier(1, &barrier2);
			}

			gDirectList->Close();
			{
				ID3D12CommandList* listsToExceute[] = {gDirectList.Get()};
				gDirectQueue->ExecuteCommandLists(1, listsToExceute);
			}
		}
		break;
		}

		// Wait for the render
		gDirectQueue->Signal(gDirectFence.Get(), gDirectFenceValue);
		if(gDirectFenceValue > gDirectFence->GetCompletedValue())
		{
			gDirectFence->SetEventOnCompletion(gDirectFenceValue, gDirectEventHandle);
			WaitForMultipleObjects(1, &gDirectEventHandle, TRUE, INFINITE);
		}
		gDirectFenceValue++;

		gSwapChain->Present(1, 0);
		gFrameIndex = gSwapChain->GetCurrentBackBufferIndex();
	}

#ifdef TOBII
	tobii_gaze_point_unsubscribe(pDevice);
	tobii_device_destroy(pDevice);
	tobii_api_destroy(pApi);
#endif

	// Output the results to file!
	std::ofstream myfile;
	myfile.open("results.txt", std::ios::app);
	myfile << "Start of Test-------------------------\n";
	for(int i = 0; i < MAX_STAGES; i++)
	{
		myfile << "CirlceRadius: " << settings[i].circleRatio << '\n';
		myfile << "CenterQuality: " << settings[i].centerQuality << '\n';
		myfile << "RAW[1] or JPEG[2]: " << gResults[i] << "\n\n";
	}

	myfile.close();

	return 0;
}

void gazePointCallback(tobii_gaze_point_t const* pPoint, void* userData)
{
	if(pPoint->validity == TOBII_VALIDITY_VALID)
	{
		gGazePoint[0] = pPoint->position_xy[0] * (float)SCREEN_WIDTH;
		gGazePoint[1] = pPoint->position_xy[1] * (float)SCREEN_HEIGHT;
	}
}

void urlReciever(char const* pUrl, void* pUserData)
{
	char* buffer = (char*)pUserData;
	if(*buffer != '\0')
		return;

	if(strlen(pUrl) < 256)
		strcpy_s(buffer, 256, pUrl);
}

bool IsTearingSupported()
{
	ComPtr<IDXGIFactory6> factory;
	HRESULT hr		  = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
	BOOL allowTearing = FALSE;
	if(SUCCEEDED(hr))
	{
		hr = factory->CheckFeatureSupport(
			DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing));
	}

	return SUCCEEDED(hr) && allowTearing;
}
