#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <algorithm>
#include <array>
#include <memory>
#include <thread>

#include <cstddef>
#include <cmath>

#include "synthizer.h"
#include "synthizer_constants.h"

#define PI 3.1415926535
#define CHECKED(x) do { \
auto ret = x; \
	if (ret) { \
		printf(#x ": Synthizer error code %i message %s\n", ret, syz_getLastErrorMessage());\
		ecode = 1; \
		if (ending == 0) goto end; \
		else goto end2; \
	} \
} while(0)

struct RouteConfig route_config = {
	.gain = 1.0,
	.fade_in = 0.01,
};

std::array<EchoTapConfig, 2> taps = {{
		{ 0.1, 1.0, 0.0},
		{ 0.2, 0.0, 1.0 },
}};

int main(int argc, char *argv[]) {
	syz_Handle context, generator, source, buffer, effect;
	int ecode = 0, ending = 0;
	double angle, delta;

	if (argc != 2) {
		printf("Usage: wav_test <path>\n");
		return 1;
	}

	CHECKED(syz_configureLoggingBackend(SYZ_LOGGING_BACKEND_STDERR, nullptr));
	syz_setLogLevel(SYZ_LOG_LEVEL_DEBUG);
	CHECKED(syz_initialize());

	CHECKED(syz_createContext(&context));
	CHECKED(syz_createSource3D(&source, context));
	CHECKED(syz_createBufferFromStream(&buffer, "file", argv[1], ""));
	CHECKED(syz_createBufferGenerator(&generator, context));
	//CHECKED(syz_setI(generator, SYZ_P_LOOPING, 1));
	CHECKED(syz_setD(generator, SYZ_P_POSITION, 10.0));
	CHECKED(syz_setO(generator, SYZ_P_BUFFER, buffer));
	CHECKED(syz_sourceAddGenerator(source, generator));

	CHECKED(syz_createGlobalEcho(&effect, context));;
	CHECKED(syz_echoSetTaps(effect, taps.size(), &taps[0]));
	while (true) {
		CHECKED(syz_routingEstablishRoute(source, effect, &route_config));
		std::this_thread::sleep_for(std::chrono::seconds(3));
		CHECKED(syz_routingRemoveRoute(source, effect, 0.01));
		std::this_thread::sleep_for(std::chrono::seconds(3));
	}
	

end:
	ending = 1;
	CHECKED(syz_handleFree(source));
	CHECKED(syz_handleFree(generator));
	CHECKED(syz_handleFree(context));
end2:
	CHECKED(syz_shutdown());
	return ecode;
}
