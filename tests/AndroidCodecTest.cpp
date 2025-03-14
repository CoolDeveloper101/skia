/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/codec/SkAndroidCodec.h"
#include "include/codec/SkCodec.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkData.h"
#include "include/core/SkEncodedImageFormat.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkSize.h"
#include "include/core/SkString.h"
#include "include/core/SkTypes.h"
#include "modules/skcms/skcms.h"
#include "src/core/SkMD5.h"
#include "tests/Test.h"
#include "tools/Resources.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <utility>

static SkISize times(const SkISize& size, float factor) {
    return { (int) (size.width() * factor), (int) (size.height() * factor) };
}

static SkISize plus(const SkISize& size, int term) {
    return { size.width() + term, size.height() + term };
}

static bool invalid(const SkISize& size) {
    return size.width() < 1 || size.height() < 1;
}

DEF_TEST(AndroidCodec_computeSampleSize, r) {
    if (GetResourcePath().isEmpty()) {
        return;
    }
    for (const char* file : { "images/color_wheel.webp",
                              "images/ship.png",
                              "images/dog.jpg",
                              "images/color_wheel.gif",
                              "images/rle.bmp",
                              "images/google_chrome.ico",
                              "images/mandrill.wbmp",
#ifdef SK_CODEC_DECODES_RAW
                              "images/sample_1mp.dng",
#endif
                              }) {
        auto data = GetResourceAsData(file);
        if (!data) {
            ERRORF(r, "Could not get %s", file);
            continue;
        }

        auto codec = SkAndroidCodec::MakeFromCodec(SkCodec::MakeFromData(std::move(data)));
        if (!codec) {
            ERRORF(r, "Could not create codec for %s", file);
            continue;
        }

        const auto dims = codec->getInfo().dimensions();
        const SkISize downscales[] = {
            plus(dims, -1),
            times(dims, .15f),
            times(dims, .6f),
            { (int32_t) (dims.width() * .25f), (int32_t) (dims.height() * .75f ) },
            { 1,  1 },
            { 1,  2 },
            { 2,  1 },
            { 0, -1 },
            { dims.width(), dims.height() - 1 },
        };
        for (SkISize size : downscales) {
            const auto requested = size;
            const int computedSampleSize = codec->computeSampleSize(&size);
            REPORTER_ASSERT(r, size.width() >= 1 && size.height() >= 1);
            if (codec->getEncodedFormat() == SkEncodedImageFormat::kWEBP) {
                // WebP supports arbitrary down-scaling.
                REPORTER_ASSERT(r, size == requested || invalid(requested));
            } else if (computedSampleSize == 1) {
                REPORTER_ASSERT(r, size == dims);
            } else {
                REPORTER_ASSERT(r, computedSampleSize > 1);
                if (size.width() >= dims.width() || size.height() >= dims.height()) {
                    ERRORF(r, "File %s's computed sample size (%i) is bigger than"
                              " original? original: %i x %i\tsampled: %i x %i",
                              file, computedSampleSize, dims.width(), dims.height(),
                              size.width(), size.height());
                }
                REPORTER_ASSERT(r, size.width()  >= requested.width() &&
                                   size.height() >= requested.height());
                REPORTER_ASSERT(r, size.width()  <  dims.width() &&
                                   size.height() <  dims.height());
            }
        }

        const SkISize upscales[] = {
            dims, plus(dims, 5), times(dims, 2),
        };
        for (SkISize size : upscales) {
            const int computedSampleSize = codec->computeSampleSize(&size);
            REPORTER_ASSERT(r, computedSampleSize == 1);
            REPORTER_ASSERT(r, dims == size);
        }

        // This mimics how Android's ImageDecoder uses SkAndroidCodec. A client
        // can choose their dimensions based on calling getSampledDimensions,
        // but the ImageDecoder API takes an arbitrary size. It then uses
        // computeSampleSize to determine the best dimensions and sampleSize.
        // It should return the same dimensions. the sampleSize may be different
        // due to integer division.
        for (int sampleSize : { 1, 2, 3, 4, 8, 16, 32 }) {
            const SkISize sampledDims = codec->getSampledDimensions(sampleSize);
            SkISize size = sampledDims;
            const int computedSampleSize = codec->computeSampleSize(&size);
            if (sampledDims != size) {
                ERRORF(r, "File '%s'->getSampledDimensions(%i) yields computed"
                          " sample size of %i\n\tsampledDimensions: %i x %i\t"
                          "computed dimensions: %i x %i",
                          file, sampleSize, computedSampleSize,
                          sampledDims.width(), sampledDims.height(),
                          size.width(), size.height());
            }
        }
    }
}

DEF_TEST(AndroidCodec_wide, r) {
    if (GetResourcePath().isEmpty()) {
        return;
    }

    const char* path = "images/wide-gamut.png";
    auto data = GetResourceAsData(path);
    if (!data) {
        ERRORF(r, "Missing file %s", path);
        return;
    }

    auto codec = SkAndroidCodec::MakeFromCodec(SkCodec::MakeFromData(std::move(data)));
    if (!codec) {
        ERRORF(r, "Failed to create codec from %s", path);
        return;
    }

    auto info = codec->getInfo();
    auto cs = codec->computeOutputColorSpace(info.colorType(), nullptr);
    if (!cs) {
        ERRORF(r, "%s should have a color space", path);
        return;
    }

    auto expected = SkColorSpace::MakeRGB(SkNamedTransferFn::kSRGB, SkNamedGamut::kDisplayP3);
    REPORTER_ASSERT(r, SkColorSpace::Equals(cs.get(), expected.get()));
}

DEF_TEST(AndroidCodec_P3, r) {
    if (GetResourcePath().isEmpty()) {
        return;
    }

    const char* path = "images/purple-displayprofile.png";
    auto data = GetResourceAsData(path);
    if (!data) {
        ERRORF(r, "Missing file %s", path);
        return;
    }

    auto codec = SkAndroidCodec::MakeFromCodec(SkCodec::MakeFromData(std::move(data)));
    if (!codec) {
        ERRORF(r, "Failed to create codec from %s", path);
        return;
    }

    auto info = codec->getInfo();
    auto cs = codec->computeOutputColorSpace(info.colorType(), nullptr);
    if (!cs) {
        ERRORF(r, "%s should have a color space", path);
        return;
    }

    REPORTER_ASSERT(r, !cs->isSRGB());
    REPORTER_ASSERT(r, cs->gammaCloseToSRGB());

    skcms_Matrix3x3 matrix;
    cs->toXYZD50(&matrix);

    static constexpr skcms_Matrix3x3 kExpected = {{
        { 0.426254272f,  0.369018555f,  0.168914795f  },
        { 0.226013184f,  0.685974121f,  0.0880126953f },
        { 0.0116729736f, 0.0950927734f, 0.71812439f   },
    }};
    REPORTER_ASSERT(r, 0 == memcmp(&matrix, &kExpected, sizeof(skcms_Matrix3x3)));
}

DEF_TEST(AndroidCodec_xmpMetadata, r) {
    const struct Rec {
        const char* path;
        SkMD5::Digest digest;
    } recs[] = {
            {"images/wide_gamut_yellow_224_224_64.jpeg",
             {0x36, 0x62, 0xa2, 0xeb, 0x29, 0xbe, 0x6e, 0x6d,
              0x0a, 0x09, 0x15, 0x38, 0x65, 0xa2, 0x19, 0x0b}},
    };

    for (const auto& rec : recs) {
        auto data = GetResourceAsData(rec.path);
        if (!data) {
            continue;
        }

        auto codec = SkAndroidCodec::MakeFromCodec(SkCodec::MakeFromData(std::move(data)));
        if (!codec) {
            ERRORF(r, "Failed to create a codec from %s", rec.path);
            continue;
        }

        const auto xmpMetadata = codec->getXmpMetadata();
        if (!xmpMetadata) {
            ERRORF(r, "Expected %s to have XMP metadata", rec.path);
            continue;
        }

        SkMD5 md5;
        md5.write(xmpMetadata->data(), xmpMetadata->size());

        if (md5.finish() != rec.digest) {
            ERRORF(r, "XMP metadata for %s didn't match expected value", rec.path);
            continue;
        }
    }
}
