/*
 * Copyright 2017 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkAlphaType.h"
#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorType.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTypes.h"
#include "include/gpu/GpuTypes.h"
#include "include/gpu/GrDirectContext.h"
#include "include/gpu/GrTypes.h"
#include "tests/CtsEnforcement.h"
#include "tests/Test.h"

#include <cstdint>

class GrRecordingContext;
struct GrContextOptions;

static SkBitmap read_pixels(sk_sp<SkSurface> surface, SkColor initColor) {
    SkBitmap bmp;
    bmp.allocN32Pixels(surface->width(), surface->height());
    bmp.eraseColor(initColor);
    if (!surface->readPixels(bmp, 0, 0)) {
        SkDebugf("readPixels failed\n");
    }
    return bmp;
}

static sk_sp<SkSurface> make_surface(GrRecordingContext* rContext) {
    SkImageInfo info = SkImageInfo::Make(50, 50, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    return SkSurface::MakeRenderTarget(
            rContext, skgpu::Budgeted::kNo, info, 4, kBottomLeft_GrSurfaceOrigin, nullptr);
}

static void test_bug_6653(GrDirectContext* dContext,
                          skiatest::Reporter* reporter,
                          const char* label) {
    SkRect rect = SkRect::MakeWH(50, 50);

    SkPaint paint;
    paint.setColor(SK_ColorWHITE);
    paint.setStrokeWidth(5);
    paint.setStyle(SkPaint::kStroke_Style);

    // The one device that fails this test (Galaxy S6) does so in a flaky fashion. Trying many
    // times makes it more likely to fail. Also, interacting with the phone (eg swiping between
    // different home screens) while the test is running makes it fail close to 100%.
    static const int kNumIterations = 50;

    for (int i = 0; i < kNumIterations; ++i) {
        auto s0 = make_surface(dContext);
        if (!s0) {
            // MSAA may not be supported
            return;
        }

        auto s1 = make_surface(dContext);
        s1->getCanvas()->clear(SK_ColorBLACK);
        s1->getCanvas()->drawOval(rect, paint);
        SkBitmap b1 = read_pixels(s1, SK_ColorBLACK);
        s1 = nullptr;

        // The bug requires that all three of the following surfaces are cleared to the same color
        auto s2 = make_surface(dContext);
        s2->getCanvas()->clear(SK_ColorBLUE);
        SkBitmap b2 = read_pixels(s2, SK_ColorBLACK);
        s2 = nullptr;

        auto s3 = make_surface(dContext);
        s3->getCanvas()->clear(SK_ColorBLUE);
        s0->getCanvas()->drawImage(read_pixels(s3, SK_ColorBLACK).asImage(), 0, 0);
        s3 = nullptr;

        auto s4 = make_surface(dContext);
        s4->getCanvas()->clear(SK_ColorBLUE);
        s4->getCanvas()->drawOval(rect, paint);

        // When this fails, b4 will "succeed", but return an empty bitmap (containing just the
        // clear color). Regardless, b5 will contain the oval that was just drawn, so diffing the
        // two bitmaps tests for the failure case. Initialize the bitmaps to different colors so
        // that if the readPixels doesn't work, this test will always fail.
        SkBitmap b4 = read_pixels(s4, SK_ColorRED);
        SkBitmap b5 = read_pixels(s4, SK_ColorGREEN);

        bool match = true;
        for (int y = 0; y < b4.height() && match; ++y) {
            for (int x = 0; x < b4.width() && match; ++x) {
                uint32_t pixelA = *b4.getAddr32(x, y);
                uint32_t pixelB = *b5.getAddr32(x, y);
                if (pixelA != pixelB) {
                    match = false;
                }
            }
        }

        REPORTER_ASSERT(reporter, match, "%s", label);
    }
}

// Tests that readPixels returns up-to-date results. This has failed on several GPUs,
// from multiple vendors, in MSAA mode.
DEF_GANESH_TEST_FOR_RENDERING_CONTEXTS(skbug6653, reporter, ctxInfo, CtsEnforcement::kApiLevel_T) {
    auto ctx = ctxInfo.directContext();
    test_bug_6653(ctx, reporter, "Default");
}
