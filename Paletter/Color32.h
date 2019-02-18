#pragma once

using BYTE = unsigned char;

class Color32
{
public:
	Color32(BYTE r, BYTE g, BYTE b, BYTE a);


private:
	union
	{
		struct
		{
			BYTE r;
			BYTE g;
			BYTE b;
			BYTE a;
		};
		int color;
	};
};