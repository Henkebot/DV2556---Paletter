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

#include "Color32.h"
#include "Color24.h"

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
constexpr int TABLE_SIZE = 256 * 256;
int main()
{
	std::vector<ColorCount> colorCounter;
	Color24 colorTable[TABLE_SIZE];

	int width, height, comp;
	Color24* data = reinterpret_cast<Color24*>(stbi_load("Images/lol1.jpg", &width, &height, &comp, 0));
	if (data)
	{
		printf("Reading image done!\nWidth=(%i)\nHeight=(%i)\nComp=(%i)\n", width, height, comp);

	}
	size_t imageSize = width * height;
	size_t imageSizeBytes = imageSize * comp;

	for (size_t i = 0; i < imageSize; i++)
	{
		bool unique = true;
		for (auto& color : colorCounter)
		{
			if (color.color == data[i])
			{
				color.count++;
				unique = false;
				break;
			}
		}
		if (unique)
		{
			ColorCount cc;
			cc.color = data[i];
			cc.count = 1;
			colorCounter.push_back(cc);
		}
	}
	std::sort(colorCounter.begin(), colorCounter.end());
	printf("Unique Colors=(%u)\n", colorCounter.size());

	int colorCounterSize = colorCounter.size();
	std::vector<Color24> colorCounter2;
	//if(false)
	if (colorCounterSize > TABLE_SIZE)
	{
		for (int i = 0; i < colorCounterSize - 1; i++)
		{
			if (colorCounterSize <= TABLE_SIZE)
				break;

			int closestMatch = 0;
			int minimumSize = 9999;

			for (int j = i + 1; j < colorCounterSize; j++)
			{
				int colDiff = (colorCounter[i].color - colorCounter[j].color).length();
				if (colDiff < minimumSize)
				{
					closestMatch = j;
					minimumSize = colDiff;

				}
			}
			if (minimumSize < 120)
			{

				/*Color24 colorMerge;
				colorMerge.r = merge(colorCounter[i].color.r, colorCounter[closestMatch].color.r);
				colorMerge.g = merge(colorCounter[i].color.g, colorCounter[closestMatch].color.g);
				colorMerge.b = merge(colorCounter[i].color.b, colorCounter[closestMatch].color.b);
				colorCounter[i].color = colorMerge;*/

				colorCounter.erase(colorCounter.begin() + closestMatch);
			
				colorCounterSize = colorCounter.size();

			}
		}
		printf("Reduced colors to=(%u)\n", colorCounterSize);
	}


	int size = TABLE_SIZE > colorCounter.size() ? colorCounter.size() : TABLE_SIZE;


	for (int i = 0; i < size; i++)
		colorTable[i] = colorCounter[i].color;

	Color24* output = new Color24[imageSize];

	for (size_t i = 0; i < imageSize; i++)
	{
		int closestindex = 0;
		int minSize = 9999;

		for (size_t acceptColor = 0; acceptColor < size; acceptColor++)
		{
			Color24 col = data[i] - colorTable[acceptColor];
			int length = col.length();
			if (length < minSize)
			{
				minSize = length;
				closestindex = acceptColor;
			}



		}
		output[i] = colorTable[closestindex];
	}

	stbi_write_bmp("Images/Sample2_out.bmp", width, height, comp, output);
	float compressionSize = ((imageSize + (size * sizeof(Color24))) / (float)imageSizeBytes)*100.0;
	printf("Original Size Bytes=(%u)\nCompressed Size Bytes=(%u)\nTotal Compression=(%f)\n", imageSizeBytes, (imageSize + (size* sizeof(Color24))), compressionSize);


	system("pause");
	return 0;
}