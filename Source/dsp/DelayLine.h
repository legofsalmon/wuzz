#pragma once

#include "DspUtil.h"
#include <vector>

namespace nd
{

/** Fractional delay line with cubic interpolation.

    Modulated delays are the whole point of the chorus and phaser stages, and linear
    interpolation on a swept delay produces audible zipper artefacts on sustained
    tones, so the read is a 4-point Hermite. */
class DelayLine
{
public:
    void prepare (double sampleRate, float maxDelaySeconds)
    {
        const int n = (int) (maxDelaySeconds * sampleRate) + 4;
        int size = 4;
        while (size < n)
            size <<= 1;

        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        writePos = 0;
    }

    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
    }

    void write (float x) noexcept
    {
        writePos = (writePos + 1) & mask;
        buffer[(size_t) writePos] = x;
    }

    /** @param delaySamples  must be >= 1 and < buffer size - 2 */
    float read (float delaySamples) const noexcept
    {
        const float d = clampf (delaySamples, 1.0f, (float) mask - 2.0f);
        const int   i = (int) d;
        const float f = d - (float) i;

        const int p = writePos - i;
        const float y0 = buffer[(size_t) ((p + 1) & mask)];
        const float y1 = buffer[(size_t) ( p      & mask)];
        const float y2 = buffer[(size_t) ((p - 1) & mask)];
        const float y3 = buffer[(size_t) ((p - 2) & mask)];

        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

        return ((c3 * f + c2) * f + c1) * f + c0;
    }

private:
    std::vector<float> buffer;
    int mask = 0;
    int writePos = 0;
};

} // namespace nd
