// DSF (DSD64, stereo, LSB first) -> 44.1 kHz float32 WAV. Measurement input only.
// Stage 1: third-order CIC, /16 (2822.4 -> 176.4 kHz). Stage 2: Kaiser-windowed
// sinc lowpass at 20 kHz, /4 (-> 44.1 kHz).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

static uint64_t u64(const unsigned char* p) { uint64_t v = 0; for (int i = 7; i >= 0; --i) v = v << 8 | p[i]; return v; }
static uint32_t u32(const unsigned char* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static double bessel0(double x) { double s = 1, t = 1; for (int k = 1; k < 40; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; } return s; }

int main(int argc, char** argv) {
    if (argc != 3) return 64;
    FILE* in = std::fopen(argv[1], "rb");
    if (!in) return 66;
    std::vector<unsigned char> file;
    unsigned char buf[1 << 16];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, in)) > 0) file.insert(file.end(), buf, buf + n);
    std::fclose(in);
    const unsigned char* fmt = file.data() + 28;
    if (std::memcmp(fmt, "fmt ", 4) != 0) return 65;
    const uint32_t channels = u32(fmt + 24), rate = u32(fmt + 28), bits = u32(fmt + 32);
    const uint64_t samples = u64(fmt + 36);
    const uint32_t block = u32(fmt + 44);
    const unsigned char* data = fmt + u64(fmt + 4);
    if (std::memcmp(data, "data", 4) != 0 || channels != 2 || bits != 1 || rate != 2822400) return 65;
    const unsigned char* payload = data + 12;
    const uint64_t blocks = (u64(data + 4) - 12) / (block * channels);

    // FIR for stage 2 at 176.4 kHz.
    const int taps = 255;
    const double fs2 = rate / 16.0, fc = 20000.0, beta = 9.0;
    std::vector<double> h(taps);
    double sum = 0;
    for (int i = 0; i < taps; ++i) {
        const double m = i - (taps - 1) / 2.0;
        const double sinc = m == 0 ? 2 * fc / fs2 : std::sin(2 * M_PI * fc / fs2 * m) / (M_PI * m);
        const double r = 2.0 * i / (taps - 1) - 1.0;
        h[i] = sinc * bessel0(beta * std::sqrt(1 - r * r)) / bessel0(beta);
        sum += h[i];
    }
    for (auto& v : h) v /= sum;

    std::vector<float> out[2];
    for (uint32_t c = 0; c < 2; ++c) {
        int64_t i1 = 0, i2 = 0, i3 = 0, c1 = 0, c2 = 0, c3 = 0;
        std::vector<double> mid;
        mid.reserve(samples / 16 + 1);
        uint64_t bit = 0;
        for (uint64_t b = 0; b < blocks && bit < samples; ++b) {
            const unsigned char* bytes = payload + b * block * channels + c * block;
            for (uint32_t k = 0; k < block && bit < samples; ++k) {
                for (int j = 0; j < 8 && bit < samples; ++j, ++bit) {
                    const int x = (bytes[k] >> j) & 1 ? 1 : -1;
                    i1 += x; i2 += i1; i3 += i2;
                    if ((bit + 1) % 16 == 0) {
                        const int64_t d1 = i3 - c1; c1 = i3;
                        const int64_t d2 = d1 - c2; c2 = d1;
                        const int64_t d3 = d2 - c3; c3 = d2;
                        mid.push_back(static_cast<double>(d3) / (16.0 * 16.0 * 16.0));
                    }
                }
            }
        }
        for (size_t k = 0; k + taps <= mid.size(); k += 4) {
            double y = 0;
            for (int t = 0; t < taps; ++t) y += h[t] * mid[k + t];
            out[c].push_back(static_cast<float>(y * 0.5));   // DSD 0 dB SACD reference is -6 dB of full modulation
        }
    }
    const uint32_t frames = static_cast<uint32_t>(std::min(out[0].size(), out[1].size()));
    FILE* w = std::fopen(argv[2], "wb");
    auto put32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, w); };
    auto put16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, w); };
    std::fwrite("RIFF", 1, 4, w); put32(36 + frames * 8); std::fwrite("WAVEfmt ", 1, 8, w);
    put32(16); put16(3); put16(2); put32(44100); put32(44100 * 8); put16(8); put16(32);
    std::fwrite("data", 1, 4, w); put32(frames * 8);
    for (uint32_t f = 0; f < frames; ++f) { std::fwrite(&out[0][f], 4, 1, w); std::fwrite(&out[1][f], 4, 1, w); }
    std::fclose(w);
    std::printf("%u frames (%.1f s) at 44.1 kHz\n", frames, frames / 44100.0);
    return 0;
}
