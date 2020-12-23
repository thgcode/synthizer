#include <memory>
#include <cstdint>
#include <algorithm>

#include "synthizer/vorbis.hpp"

#include "synthizer/byte_stream.hpp"
#include "synthizer/channel_mixing.hpp"
#include "synthizer/decoding.hpp"
#include "synthizer/error.hpp"
#include "synthizer/types.hpp"

namespace synthizer {

static std::size_t read_cb(void *user_data, void *out, std::size_t count) {
        ByteStream *stream = (ByteStream*)user_data;
        auto ret = stream->read(count, (char *)out);
        return ret;
}

static int seek_cb(void *user_data, int offset, enum STBVorbisSeekPositions origin) {
        ByteStream *stream = (ByteStream *)user_data;
        stream->seek(origin ==     STB_VORBIS_seekposition_start ? offset : stream->getPosition() + offset);
        return 1;
}

class OggDecoder: public AudioDecoder {
        public:
        OggDecoder(std::shared_ptr<LookaheadByteStream> stream);
        ~OggDecoder();
        std::int64_t writeSamplesInterleaved(std::int64_t num, float *samples, std::int64_t channels = 0);
        int getSr();
        int getChannels();
        AudioFormat getFormat();
        void seekPcm(std::int64_t pos);
        bool supportsSeek();
        bool supportsSampleAccurateSeek();
        std::int64_t getLength();

        private:
        stb_vorbis *vorbis;
        stb_vorbis_info info;
        std::shared_ptr<ByteStream> stream;
        float *tmp_buf = nullptr;
        std::int64_t frame_count = 0;

        static const int TMP_BUF_FRAMES = 1024;
};

OggDecoder::OggDecoder(std::shared_ptr<LookaheadByteStream> stream) {
        stream->resetFinal();
        this->stream = stream;

        int error = 0;

        if ((vorbis = stb_vorbis_open_callback((void *)this->stream.get(), read_cb,
                this->stream->supportsSeek() ? seek_cb : NULL, this->stream.get()->getLength(), &error, NULL)) == 0) {
                throw Error("Unable to initialize Ogg stream");
        }

        this->info = stb_vorbis_get_info(vorbis);

        if(info.channels == 0) {
                throw Error("Got a OGG file with 0 channels.");
        }

        if (stream->supportsSeek()) {
                this->frame_count = stb_vorbis_stream_length_in_samples(this->vorbis);
                if (this->frame_count == 0) {
                        //throw Error("Stream supports seek, but unable to compute frame count for Ogg stream");
                }
        }

        this->tmp_buf = new float[TMP_BUF_FRAMES*this->info.channels];
}

OggDecoder::~OggDecoder() {
        stb_vorbis_close(this->vorbis);
        delete this->tmp_buf;
}

std::int64_t OggDecoder::writeSamplesInterleaved(std::int64_t num, float *samples, std::int64_t channels) {
        auto actualChannels = channels < 1 ? this->info.channels : channels;
        /* Fast case: if the channels are equal, just write. */
        if (actualChannels == this->info.channels) {
                return stb_vorbis_get_samples_float_interleaved(this->vorbis, actualChannels, samples, num);
        }

        /* Otherwise we have to round trip via the temporary buffer. */
        std::int64_t got = stb_vorbis_get_samples_float_interleaved(this->vorbis, info.channels, this->tmp_buf, num);
        std::fill(samples, samples + got * this->info.channels, 0.0f);
        mixChannels(got, this->tmp_buf, this->info.channels, samples, actualChannels);
        return got;
}

int OggDecoder::getSr() {
        return this->info.sample_rate;
}

int OggDecoder::getChannels() {
        return this->info.channels;
}

AudioFormat OggDecoder::getFormat() {
        return AudioFormat::Ogg;
}

void OggDecoder::seekPcm(std::int64_t pos) {
        auto actualPos = std::min(this->getLength(), pos);
        if (!stb_vorbis_seek(this->vorbis, actualPos))
                throw new Error("Unable to seek.");
}

bool OggDecoder::supportsSeek() {
        return this->stream->supportsSeek();
}

bool OggDecoder::supportsSampleAccurateSeek() {
        return this->supportsSeek() && true;
}

std::int64_t OggDecoder::getLength() {
        return this->frame_count;
}

std::shared_ptr<AudioDecoder> decodeOgg(std::shared_ptr<LookaheadByteStream> stream) {
        stb_vorbis *test_vorbis;
        int error;

        test_vorbis = stb_vorbis_open_callback((void *)stream.get(), read_cb, NULL, stream.get()->getLength(), &error, NULL);

        if (test_vorbis == NULL)
                return nullptr;

        stb_vorbis_close(test_vorbis);

        /* resetFinal handled by constructor. */
        return std::make_shared<OggDecoder>(stream);
}

}
