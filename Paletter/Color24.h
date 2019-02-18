#pragma once

using BYTE = unsigned char;
#define max(a,b) (a < b ? b: a)
#define min(a,b)(a < b ? a: b)
class Color24
{
public:
	BYTE r;
	BYTE g;
	BYTE b;

	Color24 operator-(const Color24& other) const;
	int length() const;

	bool operator==(const Color24& other) const;
	bool operator<=(const Color24& other) const;
	bool operator>=(const Color24& other) const;


};