#include "synthizer.h"
#include "synthizer_constants.h"

#include "synthizer/effects/fdn_reverb.hpp"

#include "synthizer/c_api.hpp"
#include "synthizer/effects/global_effect.hpp"

#include <utility>

namespace synthizer {

class ExposedGlobalFdnReverb: public FdnReverbEffect<GlobalEffect> {
	public:
	template<typename ...ARGS>
	ExposedGlobalFdnReverb(ARGS&& ...args): FdnReverbEffect<GlobalEffect>(std::forward<ARGS>(args)...) {}

	int getObjectType() override {
		return SYZ_OTYPE_GLOBAL_FDN_REVERB;
	}
};

}

using namespace synthizer;

SYZ_CAPI syz_ErrorCode syz_createGlobalFdnReverb(syz_Handle *out, syz_Handle context) {
	SYZ_PROLOGUE
	auto ctx = fromC<Context>(context);
	auto x = ctx->createObject<ExposedGlobalFdnReverb>();
	std::shared_ptr<GlobalEffect> e = x;
	ctx->registerGlobalEffect(	e);
	*out = toC(x);
	return 0;
	SYZ_EPILOGUE
}
