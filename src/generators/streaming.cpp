#include "synthizer.h"
#include "synthizer_constants.h"

#include "synthizer/generators/streaming.hpp"

#include "synthizer/block_buffer_cache.hpp"
#include "synthizer/c_api.hpp"
#include "synthizer/cells.hpp"
#include "synthizer/config.hpp"
#include "synthizer/context.hpp"
#include "synthizer/decoding.hpp"
#include "synthizer/fade_driver.hpp"
#include "synthizer/logging.hpp"
#include "synthizer/memory.hpp"

#include "WDL/resample.h"

#include <algorithm>
#include <memory>

namespace synthizer {

StreamingGenerator::StreamingGenerator(const std::shared_ptr<Context> &ctx, const std::shared_ptr<AudioDecoder> &decoder): Generator(ctx),
	/* 100 MS latency. */
	background_thread(decoder->getChannels(), nextMultipleOf(0.1 * config::SR, config::BLOCK_SIZE)),
	decoder(decoder) {
	this->channels = decoder->getChannels();
	double old_sr = decoder->getSr();
	if (old_sr != config::SR) {
		this->resampler = std::make_shared<WDL_Resampler>();
		/* Configure resampler to use sinc filters and have required sample rates. */
		this->resampler->SetMode(false, 0, true);
		this->resampler->SetRates(old_sr, config::SR);
	}

	background_thread.start([this] (std::size_t channels, float *dest) {
		this->generateBlockInBackground(channels, dest);
	});
}

StreamingGenerator::~StreamingGenerator() {
	/* We can't rely on the destructor of background_thread because it runs after ours. */
	this->background_thread.stop();
}

int StreamingGenerator::getObjectType() {
	return SYZ_OTYPE_STREAMING_GENERATOR;
}

unsigned int StreamingGenerator::getChannels() {
	return channels;
}

void StreamingGenerator::generateBlock(float *output, FadeDriver *gain_driver) {
	auto tmp_buf_guard = acquireBlockBuffer();
	float *tmp_buf_ptr = tmp_buf_guard;

	double new_pos;
	bool pos_changed = this->acquirePosition(new_pos);
	if (pos_changed) {
		this->next_position.write(new_pos);
	}

	auto got = this->background_thread.read(config::BLOCK_SIZE, tmp_buf_ptr);
	gain_driver->drive(this->getContextRaw()->getBlockTime(), [&](auto &gain_cb) {
		for (unsigned int i = 0; i < got; i++) {
			float g = gain_cb(i);
			for (unsigned int ch = 0; ch < this->channels; ch++) {
				output[i* this->channels + ch] += g * tmp_buf_ptr[i * this->channels + ch];
			}
		}
	});

	/* important to set this without tracking changes. Tracking changes will infinite loop. */
	this->setPosition(this->background_position.load(std::memory_order_relaxed), false);
}

/*
 * Returns the new position, given the old one.
 * 
 * Decoders intentionally don't know how to give us this info, so we have to book keep it ourselves.
 * */
static double fillBufferFromDecoder(AudioDecoder &decoder, unsigned int size, unsigned int channels, float *dest, bool looping, double position_in) {
	auto sr = decoder.getSr();
	unsigned int needed = size;
	bool justLooped = false;

	float *cursor = dest;
	while (needed) {
		unsigned int got = decoder.writeSamplesInterleaved(needed, cursor);
		cursor += channels*got;
		needed -= got;
		position_in += got / (double)sr;
		/*
		 * justLooped stops us from seeking to the beginning, getting no data, and then looping forever.
		 * If we got data, we didn't just loop.
		 * 	 */
		justLooped = justLooped && got > 0;
		if (needed > 0 && justLooped == false && looping && decoder.supportsSeek()) {
			decoder.seekSeconds(0.0);
			/* We just looped. Keep this set until we get data. */
			justLooped = true;
			position_in = 0.0;
		} else {
			break;
		}
	}
	std::fill(cursor, cursor + needed*channels, 0.0f);
	return position_in;
}

void StreamingGenerator::generateBlockInBackground(std::size_t channels, float *out) {
	try {
		bool looping = this->getLooping() == 1;

		double position;
		if (this->next_position.read(&position)) {
			this->decoder->seekSeconds(position);
		} else {
			position = this->background_position.load(std::memory_order_relaxed);
		}

		if (this->resampler == nullptr) {
			position = fillBufferFromDecoder(*this->decoder, config::BLOCK_SIZE, this->getChannels(), out, looping, position);
		} else {
			float *rs_buf;
			int needed = this->resampler->ResamplePrepare(config::BLOCK_SIZE, this->getChannels(), &rs_buf);
			position = fillBufferFromDecoder(*this->decoder, needed, this->getChannels(), rs_buf, looping, position);
			unsigned int resampled = this->resampler->ResampleOut(out, needed, config::BLOCK_SIZE, this->getChannels());
			if(resampled < config::BLOCK_SIZE) {
				std::fill(out + resampled * this->getChannels(), out + config::BLOCK_SIZE * this->getChannels(), 0.0f);
			}
		}

		this->background_position.store(position, std::memory_order_relaxed);
	} catch(std::exception &e) {
		logError("Background thread for streaming generator had error: %s. Trying to recover...", e.what());
	}
}

}

using namespace synthizer;

SYZ_CAPI syz_ErrorCode syz_createStreamingGenerator(syz_Handle *out, syz_Handle context, const char *protocol, const char *path, const char *options) {
	SYZ_PROLOGUE
	auto ctx = fromC<Context>(context);
	auto decoder = getDecoderForProtocol(protocol, path, options);
	auto generator = ctx->createObject<StreamingGenerator>(decoder);
	*out = toC(generator);
	return 0;
	SYZ_EPILOGUE
}
