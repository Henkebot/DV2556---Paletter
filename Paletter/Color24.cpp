#include "pch.h"
#include "Color24.h"

Color24 Color24::operator-(const Color24 & other) const
{
	Color24 result;
	result.r = max(r, other.r) - min(r, other.r);
	result.g = max(g, other.g) - min(g, other.g);
	result.b = max(b, other.b) - min(b, other.b);

	return result;
}

int Color24::length() const
{
	return r + g + b;
}

bool Color24::operator==(const Color24 & other) const
{
	return r == other.r && g == other.g && b == other.b;
}

bool Color24::operator<=(const Color24 & other) const
{
	return r <= other.r && g <= other.g && b <= other.b;
}

bool Color24::operator>=(const Color24 & other) const
{
	return  r >= other.r && g >= other.g && b >= other.b;
}
