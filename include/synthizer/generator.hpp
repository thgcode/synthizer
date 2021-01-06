#pragma once

#include "synthizer/base_object.hpp"
#include "synthizer/fade_driver.hpp"
#include "synthizer/pausable.hpp"
#include "synthizer/property_internals.hpp"
#include "synthizer/types.hpp"

#include <memory>

namespace synthizer {

class Context;

/*
 * A generator: an abstraction over the concept of producing audio.
 * 
 * Examples of things that could be generators include noise, basic sine waves, and reading from streams.
 * 
 * Generators have two pieces of functionality:
 * 
 * - They output a block of samples, of up to config::MAX_CHANNELS channels (truncating if more).
 * - They adopt to pitch bends in a generator-defined fashion to participate in doppler for moving sources, and/or if asked by the user.
 * */
class Generator: public Pausable, public BaseObject {
	public:
	Generator(const std::shared_ptr<Context> &ctx): BaseObject(ctx) {}

	/* Return the number of channels this generator wants to output on the next block. */
	virtual unsigned int getChannels() = 0;

	/**
	 * Non-virtual internal callback. Implements pausing, gain, etc.
	 * 
	 * Derived classes should override generateBlock.
	 * */
	void run(float *output);

	/**
	 * Output a complete block of audio of config::BLOCK_SIZE. Is expected to add to the output, not replace.
	 * 
	 * Should respect the passed in FadeDriver for gain.
	 * */
	virtual void generateBlock(float *output, FadeDriver *gain_driver) = 0;

	#define PROPERTY_CLASS Generator
	#define PROPERTY_LIST GENERATOR_PROPERTIES
	#define PROPERTY_BASE BaseObject
	#include "synthizer/property_impl.hpp"

	private:
	FadeDriver gain_driver{1.0, 1};
};

}