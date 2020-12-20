#include "synthizer.h"
#include "synthizer_constants.h"
#include "synthizer/property_xmacros.hpp"

#include "synthizer/c_api.hpp"
#include "synthizer/config.hpp"
#include "synthizer/error.hpp"
#include "synthizer/generators/noise.hpp"

namespace synthizer {

void ExposedNoiseGenerator::generateBlock(float *out) {
	int noise_type;

	if (this->acquireNoiseType(noise_type)) {
		for (auto &g: this->generators) {
			g.setNoiseType(noise_type);
		}
	}

for (unsigned int i = 0; i < this->channels; i++) {
		this->generators[i].generateBlock(config::BLOCK_SIZE, out + i, this->channels);
	}
}

}

using namespace synthizer;

SYZ_CAPI syz_ErrorCode syz_createNoiseGenerator(syz_Handle *out, syz_Handle context, unsigned int channels) {
	SYZ_PROLOGUE
	if (channels == 0) {
		throw ERange("NoiseGenerator must have at least 1 channel");
	}
	auto ctx = fromC<Context>(context);
	auto x = ctx->createObject<ExposedNoiseGenerator>(channels);
	*out = toC(x);
	return 0;
	SYZ_EPILOGUE
}
