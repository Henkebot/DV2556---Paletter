#include <tobii/tobii.h>
#include <tobii/tobii_streams.h>
#include <assert.h>
#include <iostream>
#include <stdio.h>

void gazePointCallback(tobii_gaze_point_t const * pPoint, void* userData)
{
	if (pPoint->validity == TOBII_VALIDITY_VALID)
	{
		printf("Gaze point: %f, %f\n", pPoint->position_xy[0], pPoint->position_xy[1]);
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

	int is_running = 1000;
	while (--is_running > 0)
	{
		error = tobii_wait_for_callbacks(NULL, 1, &pDevice);
		assert(error == TOBII_ERROR_NO_ERROR || error == TOBII_ERROR_TIMED_OUT);

		error = tobii_device_process_callbacks(pDevice);
		assert(error == TOBII_ERROR_NO_ERROR);
	}

	tobii_gaze_point_unsubscribe(pDevice);
	tobii_device_destroy(pDevice);
	tobii_api_destroy(pApi);



}