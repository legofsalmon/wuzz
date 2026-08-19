#pragma once

#include "DspUtil.h"
#include <vector>

namespace nd
{

/** Interpolation order for table lookup.

    Measured on this engine (worst-case saw, 16 voices of 7x unison on both
    oscillators, 2.8 GHz Xeon):

        cubic  @ 16 samples/harmonic -> -93 dB alias, 68% of a core, 1.5 MB
        linear @ 32 samples/harmonic -> -80 dB alias, 59% of a core, 1.9 MB

    Linear wins because the drive stages, not the oscillators, set the engine's alias
    floor at about -55 dB; 25 dB of headroom below that buys nothing audible, while
    the two-tap read is cheaper and touches half as much cache per sample. Flip this
    to 0 to trade the CPU back for the extra 13 dB. */
#ifndef ND_LINEAR_INTERP
 #define ND_LINEAR_INTERP 1
#endif

/** Shared bank of band-limited wavetables.

    polyBLEP only buys about 16 dB over a naive ramp, which is audible once you
    stack fourteen detuned saws. These tables are built by additive synthesis with
    every harmonic that fits under Nyquist, so the oscillator is band-limited by
    construction rather than by correction.

    Tables are indexed by *phase increment* (freq / sampleRate) rather than by
    frequency, which makes the whole bank sample-rate independent - it is built
    once, statically, and shared by every voice.

    Each mip is sized at kSamplesPerHarmonic times its harmonic count, so the
    content always sits well below the table's own Nyquist and interpolation error
    stays far below the signal. */
class WaveTables
{
public:
    static constexpr int   kTablesPerOctave    = 3;
    static constexpr int   kNumTables          = 40;
    static constexpr float kIncBase            = 1.0e-4f;
    static constexpr int   kMaxHarmonics       = 1024;
    static constexpr int   kSamplesPerHarmonic = 32;
    static constexpr int   kMinTableSize       = 256;
    static constexpr int   kMaxTableSize       = 16384;

    struct Table
    {
        const float* data = nullptr;
        int size = 0;
        int mask = 0;
    };

    static const WaveTables& instance()
    {
        static const WaveTables tables;
        return tables;
    }

    /** Selects the mip whose band limit is safe for this increment. */
    static int indexForIncrement (float inc) noexcept
    {
        const float a = std::abs (inc);
        if (a <= kIncBase)
            return 0;

        const int idx = (int) (std::log2 (a / kIncBase) * (float) kTablesPerOctave);
        return idx < 0 ? 0 : (idx >= kNumTables ? kNumTables - 1 : idx);
    }

    const Table& saw (int idx) const noexcept { return sawTables[idx]; }
    const Table& tri (int idx) const noexcept { return triTables[idx]; }

    size_t memoryBytes() const noexcept { return storage.size() * sizeof (float); }

    /** Interpolated lookup. phase must be in [0, 1). */
    static float read (const Table& t, float phase) noexcept
    {
#if ND_LINEAR_INTERP
        const float pos = phase * (float) t.size;
        const int   i   = (int) pos;
        const float f   = pos - (float) i;
        const float a = t.data[i & t.mask];
        const float b = t.data[(i + 1) & t.mask];
        return a + f * (b - a);
#else
        const float pos = phase * (float) t.size;
        const int   i   = (int) pos;
        const float f   = pos - (float) i;

        const float y0 = t.data[(i - 1) & t.mask];
        const float y1 = t.data[ i      & t.mask];
        const float y2 = t.data[(i + 1) & t.mask];
        const float y3 = t.data[(i + 2) & t.mask];

        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

        return ((c3 * f + c2) * f + c1) * f + c0;
#endif
    }

private:
    WaveTables()
    {
        // One full-period sine, used as an exact lookup for every harmonic. This
        // avoids both millions of std::sin calls and the drift of an angle
        // recurrence, since sin(2*pi*k*n/N) == sineRef[(k*n) mod N].
        constexpr int N = kMaxTableSize;
        std::vector<double> sineRef ((size_t) N);
        for (int n = 0; n < N; ++n)
            sineRef[(size_t) n] = std::sin (6.283185307179586 * (double) n / (double) N);

        int sizes[kNumTables];
        int harms[kNumTables];
        size_t total = 0;

        for (int idx = 0; idx < kNumTables; ++idx)
        {
            const float incHigh = kIncBase * std::exp2 ((float) (idx + 1) / (float) kTablesPerOctave);
            int h = (int) (0.5f / incHigh);
            h = h < 1 ? 1 : (h > kMaxHarmonics ? kMaxHarmonics : h);
            harms[idx] = h;

            int s = kMinTableSize;
            while (s < h * kSamplesPerHarmonic && s < kMaxTableSize)
                s <<= 1;
            sizes[idx] = s;
            total += (size_t) s * 2;   // saw + triangle
        }

        storage.assign (total, 0.0f);

        size_t offset = 0;
        for (int idx = 0; idx < kNumTables; ++idx)
        {
            const int S = sizes[idx];
            const int H = harms[idx];

            float* sawPtr = storage.data() + offset; offset += (size_t) S;
            float* triPtr = storage.data() + offset; offset += (size_t) S;

            buildSaw (sawPtr, S, H, sineRef.data(), N);
            buildTriangle (triPtr, S, H, sineRef.data(), N);

            sawTables[idx] = { sawPtr, S, S - 1 };
            triTables[idx] = { triPtr, S, S - 1 };
        }
    }

    /** saw(t) = -(2/pi) * sum_{k>=1} sin(2*pi*k*t) / k, rising from -1 to +1. */
    static void buildSaw (float* out, int S, int H, const double* sine, int N)
    {
        std::vector<double> acc ((size_t) S, 0.0);
        const int step = N / S;   // both are powers of two, so this is exact

        for (int k = 1; k <= H; ++k)
        {
            const double amp = -0.6366197723675814 / (double) k;   // -2/pi/k
            for (int n = 0; n < S; ++n)
                acc[(size_t) n] += amp * sine[(size_t) (((k * n) & (S - 1)) * step)];
        }

        for (int n = 0; n < S; ++n)
            out[n] = (float) acc[(size_t) n];
    }

    /** tri(t) = (8/pi^2) * sum_{k odd} (-1)^((k-1)/2) * sin(2*pi*k*t) / k^2. */
    static void buildTriangle (float* out, int S, int H, const double* sine, int N)
    {
        std::vector<double> acc ((size_t) S, 0.0);
        const int step = N / S;

        for (int k = 1; k <= H; k += 2)
        {
            const double sign = (((k - 1) / 2) & 1) ? -1.0 : 1.0;
            const double amp = sign * 0.8105694691387022 / (double) (k * k);   // 8/pi^2/k^2
            for (int n = 0; n < S; ++n)
                acc[(size_t) n] += amp * sine[(size_t) (((k * n) & (S - 1)) * step)];
        }

        for (int n = 0; n < S; ++n)
            out[n] = (float) acc[(size_t) n];
    }

    std::vector<float> storage;
    Table sawTables[kNumTables];
    Table triTables[kNumTables];
};

} // namespace nd
