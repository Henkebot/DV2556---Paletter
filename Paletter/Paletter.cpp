// Paletter.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STBI_MSC_SECURE_CRT
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include <vector>
#include <algorithm>
#include <DirectXColors.h>

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

void CompressBlock(const float in[8][8], float out[8][8])
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
#endif
	static const int YQT[] = { 16,11,10,16,24,40,51,61,12,12,14,19,26,58,60,55,14,13,16,24,40,57,69,56,14,17,22,29,51,87,80,62,18,22,
							 37,56,68,109,103,77,24,35,55,64,81,104,113,92,49,64,78,87,103,121,120,101,72,92,95,98,112,100,103,99 };

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

constexpr int TABLE_SIZE = 256 * 256;
int main()
{
	std::vector<ColorCount> colorCounter;
	Color24 colorTable[TABLE_SIZE];

	int width, height, comp;
	//Color24* data = reinterpret_cast<Color24*>(stbi_load("Images/lol1.jpg", &width, &height, &comp, 0));
	stbi_uc* data = (stbi_load("Images/test3.png", &width, &height, &comp, 0));
	stbi_uc* output = new stbi_uc[width * height * comp];
	if (data)
	{
		printf("Reading image done!\nWidth=(%i)\nHeight=(%i)\nComp=(%i)\n", width, height, comp);

	}

	using namespace DirectX;

	const int BLOCK_SIZE = 8;

	float in[BLOCK_SIZE][BLOCK_SIZE];
	float out[BLOCK_SIZE][BLOCK_SIZE];

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




	/*for (int i = 0; i < width*height; i++)
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
		XMVECTOR rgb = XMColorYUVToRGB(yuv);


		output[(i* comp) + 0] = inverseDCT[i / 8][i % 8];
		output[(i* comp) + 1] = inverseDCT[i / 8][i % 8];
		output[(i* comp) + 2] = inverseDCT[i / 8][i % 8];

	}
*/


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