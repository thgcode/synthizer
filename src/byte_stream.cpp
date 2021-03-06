#include "synthizer/byte_stream.hpp"
#include "synthizer/threadsafe_initializer.hpp"

#include <array>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <shared_mutex>
#include <sstream>
#include <thread>
#include <vector>

namespace synthizer {

/*
 * Infrastructure for the registry.
 * */
static std::shared_mutex byte_stream_registry_lock{};
static std::map<std::string, std::function<std::shared_ptr<ByteStream> (const std::string &, std::vector<std::tuple<std::string, std::string>> &)>> byte_stream_registry {
	{ "file", fileStream },
};

void registerByteStreamProtocol(std::string &name, std::function<std::shared_ptr<ByteStream> (const std::string &, std::vector<std::tuple<std::string, std::string>>)> factory) {
	auto guard = std::lock_guard(byte_stream_registry_lock);
	if (byte_stream_registry.count(name))
		throw EByteStreamUnsupportedOperation("Attempted duplicate registry of protocol "+name);

	byte_stream_registry[name] = factory;
}

static std::vector<std::tuple<std::string, std::string>> parseOptions(const std::string & options) {
	std::istringstream opts_stream{options};
	std::vector<std::tuple<std::string, std::string>> parsed;
	std::string current_opt;
	while (std::getline(opts_stream, current_opt, '&')) {
		std::istringstream inner{current_opt};
		std::string k;
		std::getline(inner, k, '=');
		if (k.size() ==  0)
			continue;
		std::istream_iterator<char> begin {inner};
		std::istream_iterator<char> end{};
		std::string v{begin, end};
		parsed.emplace_back(k, v);
	}
	return parsed;
}

std::shared_ptr<ByteStream> getStreamForProtocol(const std::string &protocol, const std::string &path, const std::string &options) {
	auto parsed = parseOptions(options);
	auto l = std::shared_lock{byte_stream_registry_lock};
	if (byte_stream_registry.count(protocol) == 0)
		throw EByteStreamUnsupportedOperation("Unregistered protocol " + protocol);
	auto &f = byte_stream_registry[protocol];
	auto o = f(path, parsed);
	if (o == nullptr)
		throw EByteStream("Protocol " + protocol + " returned nullptr. No further information available.");
	return o;
}

template<typename T>
class ForwardingStream: public T {
	public:
	ForwardingStream(std::shared_ptr<ByteStream> stream): stream(stream) {}

	std::string getName() override {
		return this->stream->getName();
	}

	std::size_t read(std::size_t count, char *destination) override {
		return this->stream->read(count, destination);
	}

	bool supportsSeek() override {
		return this->stream->supportsSeek();
	}

	virtual std::size_t getPosition() override {
		return this->stream->getPosition();
	}

	virtual std::size_t getLength() override {
		return this->stream->getLength();
	}

	virtual void seek(std::size_t position) override {
		return this->stream->seek(position);
	}

	AudioFormat getFormatHint() override {
		return this->stream->getFormatHint();
	}

	protected:
	std::shared_ptr<ByteStream> stream;
};

/* A LookaheadByteStream for when the underlying stream supports seeking. */
class DirectLookaheadStream: public ForwardingStream<LookaheadByteStream> {
	public:
	DirectLookaheadStream(std::shared_ptr<ByteStream> stream): ForwardingStream(stream) {}
	void reset();
	void resetFinal();
};

void DirectLookaheadStream::reset() {
	this->stream->seek(0);
}

/* Reset and resetFinal are the same, since this stream type supported seeking. */
void DirectLookaheadStream::resetFinal() {
	this->reset();
}

/* A LookaheadByteStream for circumstances in which the underlying stream doesn't support seeking. */
class MemoryLookaheadStream: public ForwardingStream<LookaheadByteStream> {
	public:
	MemoryLookaheadStream(std::shared_ptr<ByteStream> stream): ForwardingStream(stream) {
		this->blocks.reserve(5);
	}

	void reset();
	void resetFinal();
	std::size_t read(std::size_t count, char *destination);

	private:
	static const int LOOKAHEAD_BLOCK_SIZE = 1024;
	struct LookaheadBytes {
		std::array<char, LOOKAHEAD_BLOCK_SIZE> data;
		std::size_t count;
	};
	std::vector<std::shared_ptr<LookaheadBytes>> blocks;
	std::size_t current_block = 0, current_block_pos = 0;
	bool recording = true;
};

std::size_t MemoryLookaheadStream::read(std::size_t count, char *destination) {
	std::size_t got = 0;
	while (got < count) {
		if (this->blocks.size() > this->current_block) {
			auto &cur = this->blocks[this->current_block];
			char *d = cur->data.data();
			std::size_t needed = std::min(got-count, cur->count-this->current_block_pos);
			std::copy(d, d+needed, destination);
			destination += needed;
			this->current_block_pos += needed;
			if (this->current_block_pos == LOOKAHEAD_BLOCK_SIZE) this->current_block ++;
		} else {
			/* No more blocks are recorded, so we need to do a read. */
			std::array<char, LOOKAHEAD_BLOCK_SIZE> data;
			std::size_t count;
			count = this->read(LOOKAHEAD_BLOCK_SIZE, data.data());
			if (count == 0) break; // we reached the end.
			auto block = std::make_shared<LookaheadBytes>();
			block->count = count;
			block->data = data;
			this->blocks.push_back(block);
			this->current_block_pos = 0;
		}
	}
	return got;
}

void MemoryLookaheadStream::reset() {
	assert(this->recording == false);
	this->current_block = 0;
	this->current_block_pos = 0;
}

void MemoryLookaheadStream::resetFinal() {
	this->reset();
	this->recording = false;
}

std::shared_ptr<LookaheadByteStream> getLookaheadByteStream(std::shared_ptr<ByteStream> stream) {
	if (stream->supportsSeek()) {
		return std::make_shared<DirectLookaheadStream>(stream);
	} else {
		return std::make_shared<MemoryLookaheadStream>(stream);
	}
}

char *byteStreamToBuffer(std::shared_ptr<ByteStream> stream) {
	static const std::size_t BLOCK_SIZE = 8192;
	std::list<std::vector<char>> blocks{};
	std::size_t got = 0;
	std::size_t total_size = 0;

	do {
		blocks.emplace_back(BLOCK_SIZE);
		got = stream->read(BLOCK_SIZE, blocks.back().data());
		blocks.back().resize(got);
		total_size += got;
	} while(got);

	char *buffer = new char[total_size];

	char *cur = buffer;
	for(auto &b: blocks) {
		std::copy(b.begin(), b.end(), cur);
		cur += b.size();
	}

	return buffer;
}

}
