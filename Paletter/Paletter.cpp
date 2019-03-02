// Paletter.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include "Window.h"
#include "Input.h"
#include "ScopedTimer.h"
#include <tobii/tobii.h>
#include <tobii/tobii_streams.h>
#include <assert.h>
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


static void transform(float *d0p, float *d1p, float *d2p, float *d3p, float *d4p, float *d5p, float *d6p, float *d7p) {
	float d0 = *d0p, d1 = *d1p, d2 = *d2p, d3 = *d3p, d4 = *d4p, d5 = *d5p, d6 = *d6p, d7 = *d7p;
	float z1, z2, z3, z4, z5, z11, z13;

	float tmp0 = d0 + d7;
	float tmp7 = d0 - d7;
	float tmp1 = d1 + d6;
	float tmp6 = d1 - d6;
	float tmp2 = d2 + d5;
	float tmp5 = d2 - d5;
	float tmp3 = d3 + d4;
	float tmp4 = d3 - d4;

	// Even part
	float tmp10 = tmp0 + tmp3;   // phase 2
	float tmp13 = tmp0 - tmp3;
	float tmp11 = tmp1 + tmp2;
	float tmp12 = tmp1 - tmp2;

	d0 = tmp10 + tmp11;       // phase 3
	d4 = tmp10 - tmp11;

	z1 = (tmp12 + tmp13) * 0.707106781f; // c4
	d2 = tmp13 + z1;       // phase 5
	d6 = tmp13 - z1;

	// Odd part
	tmp10 = tmp4 + tmp5;       // phase 2
	tmp11 = tmp5 + tmp6;
	tmp12 = tmp6 + tmp7;

	// The rotator is modified from fig 4-8 to avoid extra negations.
	z5 = (tmp10 - tmp12) * 0.382683433f; // c6
	z2 = tmp10 * 0.541196100f + z5; // c2-c6
	z4 = tmp12 * 1.306562965f + z5; // c2+c6
	z3 = tmp11 * 0.707106781f; // c4

	z11 = tmp7 + z3;      // phase 5
	z13 = tmp7 - z3;

	*d5p = z13 + z2;         // phase 6
	*d3p = z13 - z2;
	*d1p = z11 + z4;
	*d7p = z11 - z4;

	*d0p = d0;  *d2p = d2;  *d4p = d4;  *d6p = d6;
}

void FastDct8_inverseTransform(float *d0p, float *d1p, float *d2p, float *d3p, float *d4p, float *d5p, float *d6p, float *d7p) {
	float d0 = *d0p, d1 = *d1p, d2 = *d2p, d3 = *d3p, d4 = *d4p, d5 = *d5p, d6 = *d6p, d7 = *d7p;
	const double v15 = d0 / S[0];
	const double v26 = d1 / S[1];
	const double v21 = d2 / S[2];
	const double v28 = d3 / S[3];
	const double v16 = d4 / S[4];
	const double v25 = d5 / S[5];
	const double v22 = d6 / S[6];
	const double v27 = d7 / S[7];

	const double v19 = (v25 - v28) / 2;
	const double v20 = (v26 - v27) / 2;
	const double v23 = (v26 + v27) / 2;
	const double v24 = (v25 + v28) / 2;

	const double v7 = (v23 + v24) / 2;
	const double v11 = (v21 + v22) / 2;
	const double v13 = (v23 - v24) / 2;
	const double v17 = (v21 - v22) / 2;

	const double v8 = (v15 + v16) / 2;
	const double v9 = (v15 - v16) / 2;

	const double v18 = (v19 - v20) * A[5];  // Different from original
	const double v12 = (v19 * A[4] - v18) / (A[2] * A[5] - A[2] * A[4] - A[4] * A[5]);
	const double v14 = (v18 - v20 * A[2]) / (A[2] * A[5] - A[2] * A[4] - A[4] * A[5]);

	const double v6 = v14 - v7;
	const double v5 = v13 / A[3] - v6;
	const double v4 = -v5 - v12;
	const double v10 = v17 / A[1] - v11;

	const double v0 = (v8 + v11) / 2;
	const double v1 = (v9 + v10) / 2;
	const double v2 = (v9 - v10) / 2;
	const double v3 = (v8 - v11) / 2;

	*d0p = (v0 + v7) / 2;
	*d1p = (v1 + v6) / 2;
	*d2p = (v2 + v5) / 2;
	*d3p = (v3 + v4) / 2;
	*d4p = (v3 - v4) / 2;
	*d5p = (v2 - v5) / 2;
	*d6p = (v1 - v6) / 2;
	*d7p = (v0 - v7) / 2;
}

void CompressBlock(float in[8][8], float out[8][8])
{
	for (int i = 0; i < 8; i++)
		for (int j = 0; j < 8; j++)
			in[i][j] = 255.0f;
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

#endif
	float t0[64];

	for (int i = 0; i < 8; i++)
	{
		for (int j = 0; j < 8; j++)
		{

			t0[i * 8 + j] = in[i][j] - 127.0f;
		}
	}


	for (int dataOff = 0; dataOff < 64; dataOff += 8) {
		transform(&t0[dataOff], &t0[dataOff + 1], &t0[dataOff + 2], &t0[dataOff + 3], &t0[dataOff + 4], &t0[dataOff + 5], &t0[dataOff + 6], &t0[dataOff + 7]);
	}
	// DCT columns
	for (int dataOff = 0; dataOff < 8; ++dataOff) {
		transform(&t0[dataOff], &t0[dataOff + 8], &t0[dataOff + 16], &t0[dataOff + 24], &t0[dataOff + 32], &t0[dataOff + 40], &t0[dataOff + 48], &t0[dataOff + 56]);
	}
	for (int i = 0; i < 64; i++)
	{
		if (i % 8 == 0)printf("\n");
		printf("%f ", t0[i])
			;
	}

	for (int dataOff = 0; dataOff < 64; dataOff += 8) {
		FastDct8_inverseTransform(&t0[dataOff], &t0[dataOff + 1], &t0[dataOff + 2], &t0[dataOff + 3], &t0[dataOff + 4], &t0[dataOff + 5], &t0[dataOff + 6], &t0[dataOff + 7]);
	}
	// DCT columns
	for (int dataOff = 0; dataOff < 8; ++dataOff) {
		FastDct8_inverseTransform(&t0[dataOff], &t0[dataOff + 8], &t0[dataOff + 16], &t0[dataOff + 24], &t0[dataOff + 32], &t0[dataOff + 40], &t0[dataOff + 48], &t0[dataOff + 56]);
	}
	printf("\n");
	for (int i = 0; i < 64; i++)
	{
		if (i % 8 == 0)printf("\n");
		printf("%f ", t0[i])
			;
	}
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

ComPtr<ID3D12Resource> gDCT_Matrix_Res;
ComPtr<ID3D12Resource> gDCT_Matrix_Upload;

ComPtr<ID3D12Resource> gDCT_Matrix_Transpose_Res;
ComPtr<ID3D12Resource> gDCT_Matrix_Transpose_Upload;

ComPtr<ID3D12Resource> gWrittableRes;

constexpr int TABLE_SIZE = 256 * 256;
int gazePoint[2];
void gazePointCallback(tobii_gaze_point_t const * pPoint, void* userData)
{
	if (pPoint->validity == TOBII_VALIDITY_VALID)
	{
		gazePoint[0] = pPoint->position_xy[0] * 1600.0f;
		gazePoint[1] = pPoint->position_xy[1] * 900.0f;

	}
}

static void urlReciever(char const* pUrl, void* pUserData)
{
	char* buffer = (char*)pUserData;
	if (*buffer != '\0') return;

	if (strlen(pUrl) < 256)
		strcpy_s(buffer, 256, pUrl);
}


int main()
{

	int width, height, comp;
	//Color24* data = reinterpret_cast<Color24*>(stbi_load("Images/lol1.jpg", &width, &height, &comp, 0));
	stbi_uc* data = (stbi_load("Images/test3.png", &width, &height, &comp, 4));
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
			if (adapterIndex == 0) continue;
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
		sd.Flags = 0;
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

		D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
		srvHeapDesc.NumDescriptors =
			2 + 2; // One for UAV and one for SRV + 2 for DCT_Matrix and DCT_Matrix_Transpose
		srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

		TIF(
			gDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&gSRVHeap)));

		// Texture used
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
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.Format = textureDesc.Format;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels = 1;
			auto handle2 = gSRVHeap->GetCPUDescriptorHandleForHeapStart();
			handle2.ptr += gDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			gDevice->CreateShaderResourceView(
				gInputImageRes.Get(), &srvDesc, handle2);
		}

		// Create the output resource
		{
			//The dimensions and format should match the swap-chain
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

		// Create the DCT_Matrix buffer
		{
			D3D12_RESOURCE_DESC textureDesc = {};
			textureDesc.MipLevels = 1;
			textureDesc.Format = DXGI_FORMAT_UNKNOWN;
			textureDesc.Width = sizeof(float) * 64;
			textureDesc.Height = 1;
			textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
			textureDesc.DepthOrArraySize = 1;
			textureDesc.SampleDesc.Count = 1;
			textureDesc.SampleDesc.Quality = 0;
			textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			textureDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

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
					IID_PPV_ARGS(&gDCT_Matrix_Res)));


			// Get the size needed for this texture buffer
			UINT64 uploadBufferSize;
			gDevice->GetCopyableFootprints(&gDCT_Matrix_Res->GetDesc(), 0, 1, 0, nullptr, nullptr, nullptr, &uploadBufferSize);

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
						IID_PPV_ARGS(&gDCT_Matrix_Upload)));
			}

			UINT64 requiredSize = 0;
			D3D12_PLACED_SUBRESOURCE_FOOTPRINT layouts;
			UINT NumRows;
			UINT64 RowSizeInBytes;

			D3D12_RESOURCE_DESC resDesc = gDCT_Matrix_Res->GetDesc();
			gDevice->GetCopyableFootprints(&resDesc, 0, 1, 0, &layouts, &NumRows, &RowSizeInBytes, &requiredSize);

			BYTE* pData;
			TIF(gDCT_Matrix_Upload->Map(0, nullptr, reinterpret_cast<LPVOID*>(&pData)));

			float DCT_MATRIX[64];
			//compute dct matrix
			for (int y = 0; y < 8; y++)
				for (int x = 0; x < 8; x++)
					if (0 == y)
						DCT_MATRIX[y * 8 + x] = float(1.0 / sqrt(8.0));
					else
						DCT_MATRIX[y * 8 + x] = float(sqrt(2.0 / 8.0) * cos(((2 * x + 1)*DirectX::XM_PI*y) / (2.0 * 8.0)));

			memcpy(pData, DCT_MATRIX, sizeof(float) * 64);

			gDCT_Matrix_Upload->Unmap(0, nullptr);



			gDirectList->CopyBufferRegion(gDCT_Matrix_Res.Get(), 0, gDCT_Matrix_Upload.Get(), layouts.Offset, layouts.Footprint.RowPitch);

			D3D12_RESOURCE_BARRIER barrier;
			barrier.Transition.pResource = gDCT_Matrix_Res.Get();
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
			barrier.Transition.Subresource = 0;
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			gDirectList->ResourceBarrier(1, &barrier);

		/*	gDirectList->Close();

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
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.Format = DXGI_FORMAT_UNKNOWN;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			srvDesc.Buffer.FirstElement = 0;
			srvDesc.Buffer.NumElements = 64;
			srvDesc.Buffer.StructureByteStride = sizeof(float);
			auto handle2 = gSRVHeap->GetCPUDescriptorHandleForHeapStart();
			handle2.ptr += (gDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) * 2);
			gDevice->CreateShaderResourceView(
				gDCT_Matrix_Res.Get(), &srvDesc, handle2);

		}
		// Create the DCT_Matrix_Transpose
		{
			D3D12_RESOURCE_DESC textureDesc = {};
			textureDesc.MipLevels = 1;
			textureDesc.Format = DXGI_FORMAT_UNKNOWN;
			textureDesc.Width = sizeof(float) * 64;
			textureDesc.Height = 1;
			textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
			textureDesc.DepthOrArraySize = 1;
			textureDesc.SampleDesc.Count = 1;
			textureDesc.SampleDesc.Quality = 0;
			textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			textureDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

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
					IID_PPV_ARGS(&gDCT_Matrix_Transpose_Res)));


			// Get the size needed for this texture buffer
			UINT64 uploadBufferSize;
			gDevice->GetCopyableFootprints(&gDCT_Matrix_Transpose_Res->GetDesc(), 0, 1, 0, nullptr, nullptr, nullptr, &uploadBufferSize);

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
						IID_PPV_ARGS(&gDCT_Matrix_Transpose_Upload)));
			}

			UINT64 requiredSize = 0;
			D3D12_PLACED_SUBRESOURCE_FOOTPRINT layouts;
			UINT NumRows;
			UINT64 RowSizeInBytes;

			D3D12_RESOURCE_DESC resDesc = gDCT_Matrix_Transpose_Res->GetDesc();
			gDevice->GetCopyableFootprints(&resDesc, 0, 1, 0, &layouts, &NumRows, &RowSizeInBytes, &requiredSize);

			BYTE* pData;
			TIF(gDCT_Matrix_Transpose_Upload->Map(0, nullptr, reinterpret_cast<LPVOID*>(&pData)));

			float DCT_MATRIX2[64];
			float DCT_MATRIX_TRANSPOSE[64];
			//compute dct matrix
			for (int y = 0; y < 8; y++)
				for (int x = 0; x < 8; x++)
					if (0 == y)
						DCT_MATRIX2[y * 8 + x] = float(1.0 / sqrt(8.0));
					else
						DCT_MATRIX2[y * 8 + x] = float(sqrt(2.0 / 8.0) * cos(((2 * x + 1)*DirectX::XM_PI*y) / (2.0 * 8.0)));

			//compute dct transpose matrix
			for (int y = 0; y < 8; y++)
				for (int x = 0; x < 8; x++)
					DCT_MATRIX_TRANSPOSE[y * 8 + x] = DCT_MATRIX2[x * 8 + y];

			memcpy(pData, DCT_MATRIX_TRANSPOSE, sizeof(float) * 64);

			gDCT_Matrix_Transpose_Upload->Unmap(0, nullptr);



			gDirectList->CopyBufferRegion(gDCT_Matrix_Transpose_Res.Get(), 0, gDCT_Matrix_Transpose_Upload.Get(), layouts.Offset, layouts.Footprint.RowPitch);

			D3D12_RESOURCE_BARRIER barrier;
			barrier.Transition.pResource = gDCT_Matrix_Transpose_Res.Get();
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

			// Describe and create a SRV for the texture.
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.Format = DXGI_FORMAT_UNKNOWN;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			srvDesc.Buffer.FirstElement = 0;
			srvDesc.Buffer.NumElements = 64;
			srvDesc.Buffer.StructureByteStride = sizeof(float);
			auto handle2 = gSRVHeap->GetCPUDescriptorHandleForHeapStart();
			handle2.ptr += (gDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) * 3);
			gDevice->CreateShaderResourceView(
				gDCT_Matrix_Transpose_Res.Get(), &srvDesc, handle2);

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
		ranges[1].NumDescriptors = 3;
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

		D3D12_ROOT_PARAMETER1 rootParameters[3];
		rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
		rootParameters[0].DescriptorTable.pDescriptorRanges = &ranges[0];
		rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
		rootParameters[1].DescriptorTable.pDescriptorRanges = &ranges[1];
		rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		rootParameters[2].Constants.Num32BitValues = 2;
		rootParameters[2].Constants.RegisterSpace = 0;
		rootParameters[2].Constants.ShaderRegister = 0;
		rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

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

#ifdef TOBII
	tobii_api_t* pApi;
	tobii_error_t error = tobii_api_create(&pApi, NULL, NULL);
	assert(error == TOBII_ERROR_NO_ERROR);

	char url[256] = {};
	error = tobii_enumerate_local_device_urls(pApi, urlReciever, url);
	assert(error == TOBII_ERROR_NO_ERROR && *url != '\0');

	tobii_device_t* pDevice;
	error = tobii_device_create(pApi, url, &pDevice);
	assert(error == TOBII_ERROR_NO_ERROR);

	error = tobii_gaze_point_subscribe(pDevice, gazePointCallback, 0);
	assert(error == TOBII_ERROR_NO_ERROR);
#endif
	while (window.isOpen())
	{
		ScopedTimer timer("Render");
		window.pollEvents();
#ifdef TOBII
		error = tobii_wait_for_callbacks(NULL, 1, &pDevice);
		assert(error == TOBII_ERROR_NO_ERROR || error == TOBII_ERROR_TIMED_OUT);

		error = tobii_device_process_callbacks(pDevice);
		assert(error == TOBII_ERROR_NO_ERROR);
#endif
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
		UINT size = gDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		gComputeList->SetComputeRootDescriptorTable(0, h);
		h.ptr += size;
		gComputeList->SetComputeRootDescriptorTable(1, h);
	

		//printf("Gaze Point(%i,%i)\n", gazePoint[0], gazePoint[1]);
#ifndef TOBII
		auto pos = Input::GetMousePosition();
		gazePoint[0] = pos.x;
		gazePoint[1] = pos.y;
#endif
		gComputeList->SetComputeRoot32BitConstants(2, 2, reinterpret_cast<const LPVOID>(&gazePoint), 0);
		gComputeList->Dispatch((width / 8), (height / 8), 1);
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

#ifdef TOBII
	tobii_gaze_point_unsubscribe(pDevice);
	tobii_device_destroy(pDevice);
	tobii_api_destroy(pApi);
#endif
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