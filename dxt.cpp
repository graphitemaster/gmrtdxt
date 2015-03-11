#include "dxt.h"

#include <algorithm> // std::min / std::max

template <typename T>
static inline T clamp(T current, T min, T max) {
    return std::max(min, std::min(current, max));
}

enum dxtColor {
    kDXTColor33,
    kDXTColor66,
    kDXTColor50
};

struct dxtBlock {
    uint16_t color0;
    uint16_t color1;
    uint32_t pixels;
};

static uint16_t dxtPack565(uint16_t &r, uint16_t &g, uint16_t &b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static void dxtUnpack565(uint16_t src, uint16_t &r, uint16_t &g, uint16_t &b) {
    r = (((src>>11)&0x1F)*527 + 15) >> 6;
    g = (((src>>5)&0x3F)*259 + 35) >> 6;
    b = ((src&0x1F)*527 + 15) >> 6;
}

#ifdef DXT_OPTIMIZE
template <dxtColor E>
static uint16_t dxtCalcColor(uint16_t color0, uint16_t color1) {
    uint16_t r[3], g[3], b[3];
    dxtUnpack565(color0, r[0], g[0], b[0]);
    dxtUnpack565(color1, r[1], g[1], b[1]);
    if (E == kDXTColor33) {
        r[2] = (2*r[0] + r[1]) / 3;
        g[2] = (2*g[0] + g[1]) / 3;
        b[2] = (2*b[0] + b[1]) / 3;
    } else if (E == kDXTColor66) {
        r[2] = (r[0] + 2*r[1]) / 3;
        g[2] = (g[0] + 2*g[1]) / 3;
        b[2] = (b[0] + 2*b[1]) / 3;
    } else if (E == kDXTColor50) {
        r[2] = (r[0] + r[1]) / 2;
        g[2] = (g[0] + g[1]) / 2;
        b[2] = (b[0] + b[1]) / 2;
    }
    return dxtPack565(r[2], g[2], b[2]);
}

template <dxtType T>
static size_t dxtOptimize(unsigned char *data, size_t width, size_t height) {
    size_t count = 0;
    const size_t numBlocks = (width / 4) * (height / 4);
    dxtBlock *block = ((dxtBlock*)data) + (T == kDXT5); // DXT5: alpha block is first
    for (size_t i = 0; i != numBlocks; ++i, block += (T == kDXT1 ? 1 : 2)) {
        const uint16_t color0 = block->color0;
        const uint16_t color1 = block->color1;
        const uint32_t pixels = block->pixels;
        if (pixels == 0) {
            // Solid color0
            block->color1 = 0;
            count++;
        } else if (pixels == 0x55555555u) {
            // Solid color1, fill with color0 instead, possibly encoding the block
            // as 1-bit alpha if color1 is black.
            block->color0 = color1;
            block->color1 = 0;
            block->pixels = 0;
            count++;
        } else if (pixels == 0xAAAAAAAAu) {
            // Solid color2, fill with color0 instead, possibly encoding the block
            // as 1-bit alpha if color2 is black.
            block->color0 = (color0 > color1 || T == kDXT5)
                ? dxtCalcColor<kDXTColor33>(color0, color1)
                : dxtCalcColor<kDXTColor50>(color0, color1);
            block->color1 = 0;
            block->pixels = 0;
            count++;
        } else if (pixels == 0xFFFFFFFFu) {
            // Solid color3
            if (color0 > color1 || T == kDXT5) {
                // Fill with color0 instead, possibly encoding the block as 1-bit
                // alpha if color3 is black.
                block->color0 = dxtCalcColor<kDXTColor66>(color0, color1);
                block->color1 = 0;
                block->pixels = 0;
                count++;
            } else {
                // Transparent / solid black
                block->color0 = 0;
                block->color1 = T == kDXT1 ? 0xFFFFu : 0; // kDXT1: Transparent black
                if (T == kDXT5) // Solid black
                    block->pixels = 0;
                count++;
            }
        } else if (T == kDXT5 && (pixels & 0xAAAAAAAAu) == 0xAAAAAAAAu) {
            // Only interpolated colors are used, not the endpoints
            block->color0 = dxtCalcColor<kDXTColor66>(color0, color1);
            block->color1 = dxtCalcColor<kDXTColor33>(color0, color1);
            block->pixels = ~pixels;
            count++;
        } else if (T == kDXT5 && color0 < color1) {
            // Otherwise, ensure the colors are always in the same order
            block->color0 = color1;
            block->color1 = color0;
            block->pixels ^= 0x55555555u;
            count++;
        }
    }
    return count;
}
#endif

template <size_t C>
static inline void dxtComputeColorLine(const unsigned char *const uncompressed,
    float (&point)[3], float (&direction)[3])
{
    static constexpr float kInv16 = 1.0f / 16.0f;

    float sumR = 0.0f, sumG = 0.0f, sumB = 0.0f;
    float sumRR = 0.0f, sumGG = 0.0f, sumBB = 0.0f;
    float sumRG = 0.0f, sumRB = 0.0f, sumGB = 0.0f;

    for (size_t i = 0; i < 16*C; i += C) {
        sumR += uncompressed[i+0];
        sumG += uncompressed[i+1];
        sumB += uncompressed[i+2];
        sumRR += uncompressed[i+0] * uncompressed[i+0];
        sumGG += uncompressed[i+1] * uncompressed[i+1];
        sumBB += uncompressed[i+2] * uncompressed[i+2];
        sumRG += uncompressed[i+0] * uncompressed[i+1];
        sumRB += uncompressed[i+0] * uncompressed[i+2];
        sumGB += uncompressed[i+1] * uncompressed[i+2];
    }
    // Average all sums
    sumR *= kInv16;
    sumG *= kInv16;
    sumB *= kInv16;
    // Convert squares to squares of the value minus their average
    sumRR -= 16.0f * sumR * sumR;
    sumGG -= 16.0f * sumG * sumG;
    sumBB -= 16.0f * sumB * sumB;
    sumRG -= 16.0f * sumR * sumG;
    sumRB -= 16.0f * sumR * sumB;
    sumGB -= 16.0f * sumG * sumB;
    // The point on the color line is the average
    point[0] = sumR;
    point[1] = sumG;
    point[2] = sumB;
    // RYGDXT covariance matrix
    direction[0] = 1.0f;
    direction[1] = 2.718281828f;
    direction[2] = 3.141592654f;
    for (size_t i = 0; i < 3; ++i) {
        sumR = direction[0];
        sumG = direction[1];
        sumB = direction[2];
        direction[0] = sumR*sumRR + sumG*sumRG + sumB*sumRB;
        direction[1] = sumR*sumRG + sumG*sumGG + sumB*sumGB;
        direction[2] = sumR*sumRB + sumG*sumGB + sumB*sumBB;
    }
}

template <size_t C>
static inline void dxtLSEMasterColorsClamp(uint16_t (&colors)[2],
    const unsigned char *const uncompressed)
{
    float sumx1[] = { 0.0f, 0.0f, 0.0f };
    float sumx2[] = { 0.0f, 0.0f, 0.0f };
    dxtComputeColorLine<C>(uncompressed, sumx1, sumx2);

    float length = 1.0f / (0.00001f + sumx2[0]*sumx2[0] + sumx2[1]*sumx2[1] + sumx2[2]*sumx2[2]);
    // Calcualte range for vector values
    float dotMax = sumx2[0] * uncompressed[0] +
                   sumx2[1] * uncompressed[1] +
                   sumx2[2] * uncompressed[2];
    float dotMin = dotMax;
    for (size_t i = 1; i < 16; ++i) {
        const float dot = sumx2[0] * uncompressed[i*C+0] +
                          sumx2[1] * uncompressed[i*C+1] +
                          sumx2[2] * uncompressed[i*C+2];
        if (dot < dotMin)
            dotMin = dot;
        else if (dot > dotMax)
            dotMax = dot;
    }

    // Calculate offset from the average location
    float dot = sumx2[0]*sumx1[0] + sumx2[1]*sumx1[1] + sumx2[2]*sumx1[2];
    dotMin -= dot;
    dotMax -= dot;
    dotMin *= length;
    dotMax *= length;
    // Build the master colors
    uint16_t c0[3];
    uint16_t c1[3];
    for (size_t i = 0; i < 3; ++i) {
        c0[i] = clamp(int(0.5f + sumx1[i] + dotMax * sumx2[i]), 0, 255);
        c1[i] = clamp(int(0.5f + sumx1[i] + dotMin * sumx2[i]), 0, 255);
    }
    // Down sample the master colors to RGB565
    const uint16_t i = dxtPack565(c0[0], c0[1], c0[2]);
    const uint16_t j = dxtPack565(c1[0], c1[1], c1[2]);
    if (i > j)
        colors[0] = i, colors[1] = j;
    else
        colors[1] = i, colors[0] = j;
}

template <size_t C>
static inline void dxtCompressColorBlock(const unsigned char *const uncompressed, unsigned char (&compressed)[8]) {
    uint16_t encodeColor[2];
    dxtLSEMasterColorsClamp<C>(encodeColor, uncompressed);
    // Store 565 color
    compressed[0] = encodeColor[0] & 255;
    compressed[1] = (encodeColor[0] >> 8) & 255;
    compressed[2] = encodeColor[1] & 255;
    compressed[3] = (encodeColor[1] >> 8) & 255;
    for (size_t i = 4; i < 8; i++)
        compressed[i] = 0;

    // Reconstitute master color vectors
    uint16_t c0[3];
    uint16_t c1[3];
    dxtUnpack565(encodeColor[0], c0[0], c0[1], c0[2]);
    dxtUnpack565(encodeColor[1], c1[0], c1[1], c1[2]);

    float colorLine[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float length = 0.0f;
    for (size_t i = 0; i < 3; ++i) {
        colorLine[i] = float(c1[i] - c0[i]);
        length += colorLine[i] * colorLine[i];
    }
    if (length > 0.0f)
        length = 1.0f / length;
    // Scaling
    for (size_t i = 0; i < 3; i++)
        colorLine[i] *= length;
    // Offset portion of dot product
    const float dotOffset = colorLine[0]*c0[0] + colorLine[1]*c0[1] + colorLine[2]*c0[2];
    // Store rest of bits
    size_t nextBit = 8*4;
    for (size_t i = 0; i < 16; ++i) {
        // Find the dot product for this color, to place it on the line with
        // A range of [-1, 1]
        float dotProduct = colorLine[0] * uncompressed[i*C+0] +
                           colorLine[1] * uncompressed[i*C+1] +
                           colorLine[2] * uncompressed[i*C+2] - dotOffset;
        // Map to [0, 3]
        int nextValue = clamp(int(dotProduct * 3.0f + 0.5f), 0, 3);
        compressed[nextBit >> 3] |= "\x0\x2\x3\x1"[nextValue] << (nextBit & 7);
        nextBit += 2;
    }
}

static inline void dxtCompressAlphaBlock(const unsigned char *const uncompressed, unsigned char (&compressed)[8]) {
    unsigned char a0 = uncompressed[3];
    unsigned char a1 = uncompressed[3];
    for (size_t i = 4+3; i < 16*4; i += 4) {
        if (uncompressed[i] > a0) a0 = uncompressed[i];
        if (uncompressed[i] < a1) a1 = uncompressed[i];
    }
    compressed[0] = a0;
    compressed[1] = a1;
    for (size_t i = 2; i < 8; i++)
        compressed[i] = 0;
    size_t nextBit = 8*2;
    const float scale = 7.9999f / (a0 - a1);
    for (size_t i = 3; i < 16*4; i += 4) {
        const unsigned char value = "\x1\x7\x6\x5\x4\x3\x2\x0"[size_t((uncompressed[i] - a1) * scale) & 7];
        compressed[nextBit >> 3] |= value << (nextBit & 7);
        // Spans two bytes
        if ((nextBit & 7) > 5)
            compressed[1 + (nextBit >> 3)] |= value >> (8 - (nextBit & 7));
        nextBit += 3;
    }
}

template <dxtType T>
std::vector<unsigned char> dxtCompress(const unsigned char *const uncompressed,
    size_t width, size_t height, size_t channels, size_t &outSize, size_t &optimizedBlocks)
{
    size_t index = 0;
    const size_t chanStep = channels < 3 ? 0 : 1;
    const int hasAlpha = 1 - (channels & 1);
    outSize = ((width + 3) >> 2) * ((height + 3) >> 2) * (T == kDXT1 ? 8 : 16);
    std::vector<unsigned char> compressed(outSize);
    unsigned char ublock[16 * (T == kDXT1 ? 3 : 4)];
    unsigned char cblock[8];
    for (size_t j = 0; j < height; j += 4) {
        for (size_t i = 0; i < width; i += 4) {
            size_t z = 0;
            const size_t my = j + 4 >= height ? height - j : 4;
            const size_t mx = i + 4 >= width ? width - i : 4;
            for (size_t y = 0; y < my; ++y) {
                for (size_t x = 0; x < mx; ++x) {
                    for (size_t p = 0; p < 3; ++p)
                        ublock[z++] = uncompressed[((((j+y)*width)*channels)+((i+x)*channels))+(chanStep * p)];
                    if (T == kDXT5)
                        ublock[z++] = hasAlpha * uncompressed[(j+y)*width*channels+(i+x)*channels+channels-1] + (1 - hasAlpha) * 255;
                }
                for (size_t x = mx; x < 4; ++x)
                    for (size_t p = 0; p < (T == kDXT1 ? 3 : 4); ++p)
                        ublock[z++] = ublock[p];
            }
            for (size_t y = my; y < 4; ++y)
                for (size_t x = 0; x < 4; ++x)
                    for (size_t p = 0; p < (T == kDXT1 ? 3 : 4); ++p)
                        ublock[z++] = ublock[p];
            if (T == kDXT5) {
                dxtCompressAlphaBlock(ublock, cblock);
                for (size_t x = 0; x < 8; ++x)
                    compressed[index++] = cblock[x];
            }
            dxtCompressColorBlock<(T == kDXT1 ? 3 : 4)>(ublock, cblock);
            for (size_t x = 0; x < 8; ++x)
                compressed[index++] = cblock[x];
        }
    }
#ifdef DXT_OPTIMIZE
    optimizedBlocks = dxtOptimize<T>(&compressed[0], width, height);
#endif
    return compressed;
}

template std::vector<unsigned char> dxtCompress<kDXT1>(const unsigned char *const uncompressed,
    size_t width, size_t height, size_t channels, size_t &outSize, size_t &optimizedBlocks);
template std::vector<unsigned char> dxtCompress<kDXT5>(const unsigned char *const uncompressed,
    size_t width, size_t height, size_t channels, size_t &outSize, size_t &optimizedBlocks);
