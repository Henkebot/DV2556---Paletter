#pragma once
namespace Constants
{
	static const WCHAR IntroText[] =
		L"Welcome to Henrik and Victors Study.\n\n"

		"In this study you will be presented with two images for approx 200 ms each. \n"
		"After that we ask you to pick between image [1] or image [2], depending on which one you "
		"thought looked most pleasing.\n\n"

		"Press [ENTER] to Start the study.";

	static const WCHAR SelectionText[] = L"Type the number of that image that was the most "
										 L"pleasing to view?\nImage [1] or Image [2]\n";

	static const WCHAR ExitText[] = L"Thanks for participating!.\n\n"

									"Press [ENTER] to Quit the study.";

	constexpr unsigned int PREPARE_TIME_MS = 4 * 1000;
	constexpr unsigned int DISPLAY_TIME_MS = 600;
	//3 * 1000;

} // namespace Constants
