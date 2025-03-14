/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/private/SkHalf.h"
#include "include/private/base/SkTo.h"
#include "src/core/SkOpts.h"
#include "src/core/SkRasterPipeline.h"
#include "src/core/SkUtils.h"
#include "src/gpu/Swizzle.h"
#include "tests/Test.h"

#include <cmath>
#include <numeric>

DEF_TEST(SkRasterPipeline, r) {
    // Build and run a simple pipeline to exercise SkRasterPipeline,
    // drawing 50% transparent blue over opaque red in half-floats.
    uint64_t red  = 0x3c00000000003c00ull,
             blue = 0x3800380000000000ull,
             result;

    SkRasterPipeline_MemoryCtx load_s_ctx = { &blue, 0 },
                               load_d_ctx = { &red, 0 },
                               store_ctx  = { &result, 0 };

    SkRasterPipeline_<256> p;
    p.append(SkRasterPipeline::load_f16,     &load_s_ctx);
    p.append(SkRasterPipeline::load_f16_dst, &load_d_ctx);
    p.append(SkRasterPipeline::srcover);
    p.append(SkRasterPipeline::store_f16, &store_ctx);
    p.run(0,0,1,1);

    // We should see half-intensity magenta.
    REPORTER_ASSERT(r, ((result >>  0) & 0xffff) == 0x3800);
    REPORTER_ASSERT(r, ((result >> 16) & 0xffff) == 0x0000);
    REPORTER_ASSERT(r, ((result >> 32) & 0xffff) == 0x3800);
    REPORTER_ASSERT(r, ((result >> 48) & 0xffff) == 0x3c00);
}

DEF_TEST(SkRasterPipeline_ImmediateStoreUnmasked, r) {
    alignas(64) float val[SkRasterPipeline_kMaxStride_highp + 1] = {};

    float immVal = 123.0f;
    const void* immValCtx = nullptr;
    memcpy(&immValCtx, &immVal, sizeof(float));

    SkRasterPipeline_<256> p;
    p.append(SkRasterPipeline::immediate_f, immValCtx);
    p.append(SkRasterPipeline::store_unmasked, val);
    p.run(0,0,1,1);

    // `val` should be populated with `123.0` in the frontmost positions
    // (depending on the architecture that SkRasterPipeline is targeting).
    size_t index = 0;
    for (; index < SkOpts::raster_pipeline_highp_stride; ++index) {
        REPORTER_ASSERT(r, val[index] == immVal);
    }

    // The remaining slots should have been left alone.
    for (; index < std::size(val); ++index) {
        REPORTER_ASSERT(r, val[index] == 0.0f);
    }
}

DEF_TEST(SkRasterPipeline_LoadStoreUnmasked, r) {
    alignas(64) float val[SkRasterPipeline_kMaxStride_highp] = {};
    alignas(64) float data[] = {123.0f, 456.0f, 789.0f, -876.0f, -543.0f, -210.0f, 12.0f, -3.0f};
    static_assert(std::size(data) == SkRasterPipeline_kMaxStride_highp);

    SkRasterPipeline_<256> p;
    p.append(SkRasterPipeline::load_unmasked, data);
    p.append(SkRasterPipeline::store_unmasked, val);
    p.run(0,0,1,1);

    // `val` should be populated with `data` in the frontmost positions
    // (depending on the architecture that SkRasterPipeline is targeting).
    size_t index = 0;
    for (; index < SkOpts::raster_pipeline_highp_stride; ++index) {
        REPORTER_ASSERT(r, val[index] == data[index]);
    }

    // The remaining slots should have been left alone.
    for (; index < std::size(val); ++index) {
        REPORTER_ASSERT(r, val[index] == 0.0f);
    }
}

DEF_TEST(SkRasterPipeline_LoadStoreMasked, r) {
    for (size_t width = 0; width < SkOpts::raster_pipeline_highp_stride; ++width) {
        alignas(64) float val[] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
        alignas(64) float data[] = {2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f};
        alignas(64) const int32_t mask[] = {0, ~0, ~0, ~0, ~0, ~0, 0, ~0};
        static_assert(std::size(val) == SkRasterPipeline_kMaxStride_highp);
        static_assert(std::size(data) == SkRasterPipeline_kMaxStride_highp);
        static_assert(std::size(mask) == SkRasterPipeline_kMaxStride_highp);

        SkRasterPipeline_<256> p;
        p.append(SkRasterPipeline::init_lane_masks);
        p.append(SkRasterPipeline::load_condition_mask, mask);
        p.append(SkRasterPipeline::load_unmasked, data);
        p.append(SkRasterPipeline::store_masked, val);
        p.run(0, 0, width, 1);

        // Where the mask is set, and the width is sufficient, `val` should be populated.
        size_t index = 0;
        for (; index < width; ++index) {
            if (mask[index]) {
                REPORTER_ASSERT(r, val[index] == 2.0f);
            } else {
                REPORTER_ASSERT(r, val[index] == 1.0f);
            }
        }

        // The remaining slots should have been left alone.
        for (; index < std::size(val); ++index) {
            REPORTER_ASSERT(r, val[index] == 1.0f);
        }
    }
}

DEF_TEST(SkRasterPipeline_LoadStoreConditionMask, r) {
    alignas(64) int32_t mask[]  = {~0, 0, ~0,  0, ~0, ~0, ~0,  0};
    alignas(64) int32_t maskCopy[SkRasterPipeline_kMaxStride_highp] = {};
    alignas(64) int32_t dst[4 * SkRasterPipeline_kMaxStride_highp] = {};

    static_assert(std::size(mask) == SkRasterPipeline_kMaxStride_highp);

    SkRasterPipeline_<256> p;
    p.append(SkRasterPipeline::init_lane_masks);
    p.append(SkRasterPipeline::load_condition_mask, mask);
    p.append(SkRasterPipeline::store_condition_mask, maskCopy);
    p.append(SkRasterPipeline::store_dst, dst);
    p.run(0,0,SkOpts::raster_pipeline_highp_stride,1);

    {
        // `maskCopy` should be populated with `mask` in the frontmost positions
        // (depending on the architecture that SkRasterPipeline is targeting).
        size_t index = 0;
        for (; index < SkOpts::raster_pipeline_highp_stride; ++index) {
            REPORTER_ASSERT(r, maskCopy[index] == mask[index]);
        }

        // The remaining slots should have been left alone.
        for (; index < std::size(maskCopy); ++index) {
            REPORTER_ASSERT(r, maskCopy[index] == 0);
        }
    }
    {
        // `dr` and `da` should be populated with `mask`.
        // `dg` and `db` should remain initialized to true.
        const int dr = 0 * SkOpts::raster_pipeline_highp_stride;
        const int dg = 1 * SkOpts::raster_pipeline_highp_stride;
        const int db = 2 * SkOpts::raster_pipeline_highp_stride;
        const int da = 3 * SkOpts::raster_pipeline_highp_stride;
        for (size_t index = 0; index < SkOpts::raster_pipeline_highp_stride; ++index) {
            REPORTER_ASSERT(r, dst[dr + index] == mask[index]);
            REPORTER_ASSERT(r, dst[dg + index] == ~0);
            REPORTER_ASSERT(r, dst[db + index] == ~0);
            REPORTER_ASSERT(r, dst[da + index] == mask[index]);
        }
    }
}

DEF_TEST(SkRasterPipeline_LoadStoreLoopMask, r) {
    alignas(64) int32_t mask[]  = {~0, 0, ~0,  0, ~0, ~0, ~0,  0};
    alignas(64) int32_t maskCopy[SkRasterPipeline_kMaxStride_highp] = {};
    alignas(64) int32_t dst[4 * SkRasterPipeline_kMaxStride_highp] = {};

    static_assert(std::size(mask) == SkRasterPipeline_kMaxStride_highp);

    SkRasterPipeline_<256> p;
    p.append(SkRasterPipeline::init_lane_masks);
    p.append(SkRasterPipeline::load_loop_mask, mask);
    p.append(SkRasterPipeline::store_loop_mask, maskCopy);
    p.append(SkRasterPipeline::store_dst, dst);
    p.run(0,0,SkOpts::raster_pipeline_highp_stride,1);

    {
        // `maskCopy` should be populated with `mask` in the frontmost positions
        // (depending on the architecture that SkRasterPipeline is targeting).
        size_t index = 0;
        for (; index < SkOpts::raster_pipeline_highp_stride; ++index) {
            REPORTER_ASSERT(r, maskCopy[index] == mask[index]);
        }

        // The remaining slots should have been left alone.
        for (; index < std::size(maskCopy); ++index) {
            REPORTER_ASSERT(r, maskCopy[index] == 0);
        }
    }
    {
        // `dg` and `da` should be populated with `mask`.
        // `dr` and `db` should remain initialized to true.
        const int dr = 0 * SkOpts::raster_pipeline_highp_stride;
        const int dg = 1 * SkOpts::raster_pipeline_highp_stride;
        const int db = 2 * SkOpts::raster_pipeline_highp_stride;
        const int da = 3 * SkOpts::raster_pipeline_highp_stride;
        for (size_t index = 0; index < SkOpts::raster_pipeline_highp_stride; ++index) {
            REPORTER_ASSERT(r, dst[dr + index] == ~0);
            REPORTER_ASSERT(r, dst[dg + index] == mask[index]);
            REPORTER_ASSERT(r, dst[db + index] == ~0);
            REPORTER_ASSERT(r, dst[da + index] == mask[index]);
        }
    }
}

DEF_TEST(SkRasterPipeline_LoadStoreReturnMask, r) {
    alignas(64) int32_t mask[]  = {~0, 0, ~0,  0, ~0, ~0, ~0,  0};
    alignas(64) int32_t maskCopy[SkRasterPipeline_kMaxStride_highp] = {};
    alignas(64) int32_t dst[4 * SkRasterPipeline_kMaxStride_highp] = {};

    static_assert(std::size(mask) == SkRasterPipeline_kMaxStride_highp);

    SkRasterPipeline_<256> p;
    p.append(SkRasterPipeline::init_lane_masks);
    p.append(SkRasterPipeline::load_return_mask, mask);
    p.append(SkRasterPipeline::store_return_mask, maskCopy);
    p.append(SkRasterPipeline::store_dst, dst);
    p.run(0,0,SkOpts::raster_pipeline_highp_stride,1);

    {
        // `maskCopy` should be populated with `mask` in the frontmost positions
        // (depending on the architecture that SkRasterPipeline is targeting).
        size_t index = 0;
        for (; index < SkOpts::raster_pipeline_highp_stride; ++index) {
            REPORTER_ASSERT(r, maskCopy[index] == mask[index]);
        }

        // The remaining slots should have been left alone.
        for (; index < std::size(maskCopy); ++index) {
            REPORTER_ASSERT(r, maskCopy[index] == 0);
        }
    }
    {
        // `db` and `da` should be populated with `mask`.
        // `dr` and `dg` should remain initialized to true.
        const int dr = 0 * SkOpts::raster_pipeline_highp_stride;
        const int dg = 1 * SkOpts::raster_pipeline_highp_stride;
        const int db = 2 * SkOpts::raster_pipeline_highp_stride;
        const int da = 3 * SkOpts::raster_pipeline_highp_stride;
        for (size_t index = 0; index < SkOpts::raster_pipeline_highp_stride; ++index) {
            REPORTER_ASSERT(r, dst[dr + index] == ~0);
            REPORTER_ASSERT(r, dst[dg + index] == ~0);
            REPORTER_ASSERT(r, dst[db + index] == mask[index]);
            REPORTER_ASSERT(r, dst[da + index] == mask[index]);
        }
    }
}

DEF_TEST(SkRasterPipeline_MergeConditionMask, r) {
    alignas(64) int32_t mask[]  = { 0,  0, ~0, ~0, 0, ~0, 0, ~0,
                                   ~0, ~0, ~0, ~0, 0,  0, 0,  0};
    alignas(64) int32_t dst[4 * SkRasterPipeline_kMaxStride_highp] = {};
    static_assert(std::size(mask) == (2 * SkRasterPipeline_kMaxStride_highp));

    SkRasterPipeline_<256> p;
    p.append(SkRasterPipeline::init_lane_masks);
    p.append(SkRasterPipeline::merge_condition_mask, mask);
    p.append(SkRasterPipeline::store_dst, dst);
    p.run(0,0,SkOpts::raster_pipeline_highp_stride,1);

    // `dr` and `da` should be populated with `mask[x] & mask[y]` in the frontmost positions.
    // `dg` and `db` should remain initialized to true.
    const int dr = 0 * SkOpts::raster_pipeline_highp_stride;
    const int dg = 1 * SkOpts::raster_pipeline_highp_stride;
    const int db = 2 * SkOpts::raster_pipeline_highp_stride;
    const int da = 3 * SkOpts::raster_pipeline_highp_stride;
    for (size_t index = 0; index < SkOpts::raster_pipeline_highp_stride; ++index) {
        int32_t expected = mask[index] & mask[index + SkOpts::raster_pipeline_highp_stride];
        REPORTER_ASSERT(r, dst[dr + index] == expected);
        REPORTER_ASSERT(r, dst[dg + index] == ~0);
        REPORTER_ASSERT(r, dst[db + index] == ~0);
        REPORTER_ASSERT(r, dst[da + index] == expected);
    }
}

DEF_TEST(SkRasterPipeline_MergeLoopMask, r) {
    alignas(64) int32_t initial[]  = {~0, ~0, ~0, ~0, ~0,  0, ~0, ~0,  // dr (condition)
                                      ~0,  0, ~0,  0, ~0, ~0, ~0, ~0,  // dg (loop)
                                      ~0, ~0, ~0, ~0, ~0, ~0,  0, ~0,  // db (return)
                                      ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0}; // da (combined)
    alignas(64) int32_t mask[]     = { 0, ~0, ~0,  0, ~0, ~0, ~0, ~0};
    alignas(64) int32_t dst[4 * SkRasterPipeline_kMaxStride_highp] = {};
    static_assert(std::size(initial) == (4 * SkRasterPipeline_kMaxStride_highp));

    SkRasterPipeline_<256> p;
    p.append(SkRasterPipeline::load_dst, initial);
    p.append(SkRasterPipeline::merge_loop_mask, mask);
    p.append(SkRasterPipeline::store_dst, dst);
    p.run(0,0,SkOpts::raster_pipeline_highp_stride,1);

    const int dr = 0 * SkOpts::raster_pipeline_highp_stride;
    const int dg = 1 * SkOpts::raster_pipeline_highp_stride;
    const int db = 2 * SkOpts::raster_pipeline_highp_stride;
    const int da = 3 * SkOpts::raster_pipeline_highp_stride;
    for (size_t index = 0; index < SkOpts::raster_pipeline_highp_stride; ++index) {
        // `dg` should contain `dg & mask` in each lane.
        REPORTER_ASSERT(r, dst[dg + index] == (initial[dg + index] & mask[index]));

        // `dr` and `db` should be unchanged.
        REPORTER_ASSERT(r, dst[dr + index] == initial[dr + index]);
        REPORTER_ASSERT(r, dst[db + index] == initial[db + index]);

        // `da` should contain `dr & dg & gb`.
        REPORTER_ASSERT(r, dst[da + index] == (dst[dr+index] & dst[dg+index] & dst[db+index]));
    }
}

DEF_TEST(SkRasterPipeline_ReenableLoopMask, r) {
    alignas(64) int32_t initial[]  = {~0, ~0, ~0, ~0, ~0,  0, ~0, ~0,  // dr (condition)
                                      ~0,  0, ~0,  0, ~0, ~0,  0, ~0,  // dg (loop)
                                       0, ~0, ~0, ~0,  0,  0,  0, ~0,  // db (return)
                                       0,  0, ~0,  0,  0,  0,  0, ~0}; // da (combined)
    alignas(64) int32_t mask[]     = { 0, ~0,  0,  0,  0,  0, ~0,  0};
    alignas(64) int32_t dst[4 * SkRasterPipeline_kMaxStride_highp] = {};
    static_assert(std::size(initial) == (4 * SkRasterPipeline_kMaxStride_highp));

    SkRasterPipeline_<256> p;
    p.append(SkRasterPipeline::load_dst, initial);
    p.append(SkRasterPipeline::reenable_loop_mask, mask);
    p.append(SkRasterPipeline::store_dst, dst);
    p.run(0,0,SkOpts::raster_pipeline_highp_stride,1);

    const int dr = 0 * SkOpts::raster_pipeline_highp_stride;
    const int dg = 1 * SkOpts::raster_pipeline_highp_stride;
    const int db = 2 * SkOpts::raster_pipeline_highp_stride;
    const int da = 3 * SkOpts::raster_pipeline_highp_stride;
    for (size_t index = 0; index < SkOpts::raster_pipeline_highp_stride; ++index) {
        // `dg` should contain `dg | mask` in each lane.
        REPORTER_ASSERT(r, dst[dg + index] == (initial[dg + index] | mask[index]));

        // `dr` and `db` should be unchanged.
        REPORTER_ASSERT(r, dst[dr + index] == initial[dr + index]);
        REPORTER_ASSERT(r, dst[db + index] == initial[db + index]);

        // `da` should contain `dr & dg & gb`.
        REPORTER_ASSERT(r, dst[da + index] == (dst[dr+index] & dst[dg+index] & dst[db+index]));
    }
}

DEF_TEST(SkRasterPipeline_MaskOffLoopMask, r) {
    alignas(64) int32_t initial[]  = {~0, ~0, ~0, ~0, ~0,  0, ~0, ~0,  // dr (condition)
                                      ~0,  0, ~0, ~0,  0,  0,  0, ~0,  // dg (loop)
                                      ~0, ~0,  0, ~0,  0,  0, ~0, ~0,  // db (return)
                                      ~0,  0,  0, ~0,  0,  0,  0, ~0}; // da (combined)
    alignas(64) int32_t dst[4 * SkRasterPipeline_kMaxStride_highp] = {};
    static_assert(std::size(initial) == (4 * SkRasterPipeline_kMaxStride_highp));

    SkRasterPipeline_<256> p;
    p.append(SkRasterPipeline::load_dst, initial);
    p.append(SkRasterPipeline::mask_off_loop_mask);
    p.append(SkRasterPipeline::store_dst, dst);
    p.run(0,0,SkOpts::raster_pipeline_highp_stride,1);

    const int dr = 0 * SkOpts::raster_pipeline_highp_stride;
    const int dg = 1 * SkOpts::raster_pipeline_highp_stride;
    const int db = 2 * SkOpts::raster_pipeline_highp_stride;
    const int da = 3 * SkOpts::raster_pipeline_highp_stride;
    for (size_t index = 0; index < SkOpts::raster_pipeline_highp_stride; ++index) {
        // `dg` should have masked off any lanes that are currently executing.
        int32_t expected = initial[dg + index] & ~initial[da + index];
        REPORTER_ASSERT(r, dst[dg + index] == expected);

        // `da` should contain `dr & dg & gb`.
        expected = dst[dr + index] & dst[dg + index] & dst[db + index];
        REPORTER_ASSERT(r, dst[da + index] == expected);
    }
}

DEF_TEST(SkRasterPipeline_MaskOffReturnMask, r) {
    alignas(64) int32_t initial[]  = {~0, ~0, ~0, ~0, ~0,  0, ~0, ~0,  // dr (condition)
                                      ~0,  0, ~0, ~0,  0,  0,  0, ~0,  // dg (loop)
                                      ~0, ~0,  0, ~0,  0,  0, ~0, ~0,  // db (return)
                                      ~0,  0,  0, ~0,  0,  0,  0, ~0}; // da (combined)
    alignas(64) int32_t dst[4 * SkRasterPipeline_kMaxStride_highp] = {};
    static_assert(std::size(initial) == (4 * SkRasterPipeline_kMaxStride_highp));

    SkRasterPipeline_<256> p;
    p.append(SkRasterPipeline::load_dst, initial);
    p.append(SkRasterPipeline::mask_off_return_mask);
    p.append(SkRasterPipeline::store_dst, dst);
    p.run(0,0,SkOpts::raster_pipeline_highp_stride,1);

    const int dr = 0 * SkOpts::raster_pipeline_highp_stride;
    const int dg = 1 * SkOpts::raster_pipeline_highp_stride;
    const int db = 2 * SkOpts::raster_pipeline_highp_stride;
    const int da = 3 * SkOpts::raster_pipeline_highp_stride;
    for (size_t index = 0; index < SkOpts::raster_pipeline_highp_stride; ++index) {
        // `db` should have masked off any lanes that are currently executing.
        int32_t expected = initial[db + index] & ~initial[da + index];
        REPORTER_ASSERT(r, dst[db + index] == expected);

        // `da` should contain `dr & dg & gb`.
        expected = dst[dr + index] & dst[dg + index] & dst[db + index];
        REPORTER_ASSERT(r, dst[da + index] == expected);
    }
}

DEF_TEST(SkRasterPipeline_InitLaneMasks, r) {
    for (size_t width = 1; width <= SkOpts::raster_pipeline_highp_stride; ++width) {
        SkRasterPipeline_<256> p;

        // Initialize dRGBA to unrelated values.
        SkRasterPipeline_UniformColorCtx uniformCtx;
        uniformCtx.a = 0.0f;
        uniformCtx.r = 0.25f;
        uniformCtx.g = 0.50f;
        uniformCtx.b = 0.75f;
        p.append(SkRasterPipeline::uniform_color_dst, &uniformCtx);

        // Overwrite dRGB with lane masks up to the tail width.
        p.append(SkRasterPipeline::init_lane_masks);

        // Use the store_dst command to write out dRGBA for inspection.
        alignas(64) int32_t dRGBA[4 * SkRasterPipeline_kMaxStride_highp] = {};
        p.append(SkRasterPipeline::store_dst, dRGBA);

        // Execute our program.
        p.run(0,0,width,1);

        // Initialized data should look like on/on/on/on (RGBA are all set) and is
        // striped by the raster pipeline stride because we wrote it using store_dst.
        size_t index = 0;
        int32_t* channelR = dRGBA;
        int32_t* channelG = channelR + SkOpts::raster_pipeline_highp_stride;
        int32_t* channelB = channelG + SkOpts::raster_pipeline_highp_stride;
        int32_t* channelA = channelB + SkOpts::raster_pipeline_highp_stride;
        for (; index < width; ++index) {
            REPORTER_ASSERT(r, *channelR++ == ~0);
            REPORTER_ASSERT(r, *channelG++ == ~0);
            REPORTER_ASSERT(r, *channelB++ == ~0);
            REPORTER_ASSERT(r, *channelA++ == ~0);
        }

        // The rest of the output array should be untouched (all zero).
        for (; index < SkOpts::raster_pipeline_highp_stride; ++index) {
            REPORTER_ASSERT(r, *channelR++ == 0);
            REPORTER_ASSERT(r, *channelG++ == 0);
            REPORTER_ASSERT(r, *channelB++ == 0);
            REPORTER_ASSERT(r, *channelA++ == 0);
        }
    }
}

DEF_TEST(SkRasterPipeline_CopySlotsMasked, r) {
    // Allocate space for 5 source slots and 5 dest slots.
    alignas(64) float slots[10 * SkRasterPipeline_kMaxStride_highp];
    const int srcIndex = 0, dstIndex = 5;

    struct CopySlotsOp {
        SkRasterPipeline::Stage stage;
        int numSlotsAffected;
    };

    static const CopySlotsOp kCopyOps[] = {
        {SkRasterPipeline::Stage::copy_slot_masked,    1},
        {SkRasterPipeline::Stage::copy_2_slots_masked, 2},
        {SkRasterPipeline::Stage::copy_3_slots_masked, 3},
        {SkRasterPipeline::Stage::copy_4_slots_masked, 4},
    };

    static_assert(SkRasterPipeline_kMaxStride_highp == 8);
    alignas(64) const int32_t kMask1[8] = {~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0};
    alignas(64) const int32_t kMask2[8] = { 0,  0,  0,  0,  0,  0,  0,  0};
    alignas(64) const int32_t kMask3[8] = {~0,  0, ~0, ~0, ~0, ~0,  0, ~0};
    alignas(64) const int32_t kMask4[8] = { 0, ~0,  0,  0,  0, ~0, ~0,  0};

    const int N = SkOpts::raster_pipeline_highp_stride;

    for (const CopySlotsOp& op : kCopyOps) {
        for (const int32_t* mask : {kMask1, kMask2, kMask3, kMask4}) {
            // Initialize the destination slots to 0,1,2.. and the source slots to 1000,1001,1002...
            std::iota(&slots[N * dstIndex],  &slots[N * (dstIndex + 5)], 0.0f);
            std::iota(&slots[N * srcIndex],  &slots[N * (srcIndex + 5)], 1000.0f);

            // Run `copy_slots_masked` over our data.
            SkArenaAlloc alloc(/*firstHeapAllocation=*/256);
            SkRasterPipeline p(&alloc);
            auto* ctx = alloc.make<SkRasterPipeline_BinaryOpCtx>();
            ctx->dst = &slots[N * dstIndex];
            ctx->src = &slots[N * srcIndex];

            p.append(SkRasterPipeline::init_lane_masks);
            p.append(SkRasterPipeline::load_condition_mask, mask);
            p.append(op.stage, ctx);
            p.run(0,0,N,1);

            // Verify that the destination has been overwritten in the mask-on fields, and has not
            // been overwritten in the mask-off fields, for each destination slot.
            float expectedUnchanged = 0.0f, expectedChanged = 1000.0f;
            float* destPtr = &slots[N * dstIndex];
            for (int checkSlot = 0; checkSlot < 5; ++checkSlot) {
                for (int checkMask = 0; checkMask < N; ++checkMask) {
                    if (checkSlot < op.numSlotsAffected && mask[checkMask]) {
                        REPORTER_ASSERT(r, *destPtr == expectedChanged);
                    } else {
                        REPORTER_ASSERT(r, *destPtr == expectedUnchanged);
                    }

                    ++destPtr;
                    expectedUnchanged += 1.0f;
                    expectedChanged += 1.0f;
                }
            }
        }
    }
}

DEF_TEST(SkRasterPipeline_CopySlotsUnmasked, r) {
    // Allocate space for 5 source slots and 5 dest slots.
    alignas(64) float slots[10 * SkRasterPipeline_kMaxStride_highp];
    const int srcIndex = 0, dstIndex = 5;
    const int N = SkOpts::raster_pipeline_highp_stride;

    struct CopySlotsOp {
        SkRasterPipeline::Stage stage;
        int numSlotsAffected;
    };

    static const CopySlotsOp kCopyOps[] = {
        {SkRasterPipeline::Stage::copy_slot_unmasked,    1},
        {SkRasterPipeline::Stage::copy_2_slots_unmasked, 2},
        {SkRasterPipeline::Stage::copy_3_slots_unmasked, 3},
        {SkRasterPipeline::Stage::copy_4_slots_unmasked, 4},
    };

    for (const CopySlotsOp& op : kCopyOps) {
        // Initialize the destination slots to 0,1,2.. and the source slots to 1000,1001,1002...
        std::iota(&slots[N * dstIndex],  &slots[N * (dstIndex + 5)], 0.0f);
        std::iota(&slots[N * srcIndex],  &slots[N * (srcIndex + 5)], 1000.0f);

        // Run `copy_slots_unmasked` over our data.
        SkArenaAlloc alloc(/*firstHeapAllocation=*/256);
        SkRasterPipeline p(&alloc);
        auto* ctx = alloc.make<SkRasterPipeline_BinaryOpCtx>();
        ctx->dst = &slots[N * dstIndex];
        ctx->src = &slots[N * srcIndex];
        p.append(op.stage, ctx);
        p.run(0,0,1,1);

        // Verify that the destination has been overwritten in each slot.
        float expectedUnchanged = 0.0f, expectedChanged = 1000.0f;
        float* destPtr = &slots[N * dstIndex];
        for (int checkSlot = 0; checkSlot < 5; ++checkSlot) {
            for (int checkLane = 0; checkLane < N; ++checkLane) {
                if (checkSlot < op.numSlotsAffected) {
                    REPORTER_ASSERT(r, *destPtr == expectedChanged);
                } else {
                    REPORTER_ASSERT(r, *destPtr == expectedUnchanged);
                }

                ++destPtr;
                expectedUnchanged += 1.0f;
                expectedChanged += 1.0f;
            }
        }
    }
}

DEF_TEST(SkRasterPipeline_ZeroSlotsUnmasked, r) {
    // Allocate space for 5 dest slots.
    alignas(64) float slots[5 * SkRasterPipeline_kMaxStride_highp];
    const int N = SkOpts::raster_pipeline_highp_stride;

    struct ZeroSlotsOp {
        SkRasterPipeline::Stage stage;
        int numSlotsAffected;
    };

    static const ZeroSlotsOp kZeroOps[] = {
        {SkRasterPipeline::Stage::zero_slot_unmasked,    1},
        {SkRasterPipeline::Stage::zero_2_slots_unmasked, 2},
        {SkRasterPipeline::Stage::zero_3_slots_unmasked, 3},
        {SkRasterPipeline::Stage::zero_4_slots_unmasked, 4},
    };

    for (const ZeroSlotsOp& op : kZeroOps) {
        // Initialize the destination slots to 1,2,3...
        std::iota(&slots[0], &slots[5 * N], 1.0f);

        // Run `zero_slots_unmasked` over our data.
        SkArenaAlloc alloc(/*firstHeapAllocation=*/256);
        SkRasterPipeline p(&alloc);
        p.append(op.stage, &slots[0]);
        p.run(0,0,1,1);

        // Verify that the destination has been zeroed out in each slot.
        float expectedUnchanged = 1.0f;
        float* destPtr = &slots[0];
        for (int checkSlot = 0; checkSlot < 5; ++checkSlot) {
            for (int checkLane = 0; checkLane < N; ++checkLane) {
                if (checkSlot < op.numSlotsAffected) {
                    REPORTER_ASSERT(r, *destPtr == 0.0f);
                } else {
                    REPORTER_ASSERT(r, *destPtr == expectedUnchanged);
                }

                ++destPtr;
                expectedUnchanged += 1.0f;
            }
        }
    }
}

DEF_TEST(SkRasterPipeline_CopyConstants, r) {
    // Allocate space for 5 dest slots.
    alignas(64) float slots[5 * SkRasterPipeline_kMaxStride_highp];
    float constants[5];
    const int N = SkOpts::raster_pipeline_highp_stride;

    struct CopySlotsOp {
        SkRasterPipeline::Stage stage;
        int numSlotsAffected;
    };

    static const CopySlotsOp kCopyOps[] = {
        {SkRasterPipeline::Stage::copy_constant,    1},
        {SkRasterPipeline::Stage::copy_2_constants, 2},
        {SkRasterPipeline::Stage::copy_3_constants, 3},
        {SkRasterPipeline::Stage::copy_4_constants, 4},
    };

    for (const CopySlotsOp& op : kCopyOps) {
        // Initialize the destination slots to 1,2,3...
        std::iota(&slots[0], &slots[5 * N], 1.0f);
        // Initialize the constant buffer to 1000,1001,1002...
        std::iota(&constants[0], &constants[5], 1000.0f);

        // Run `copy_constants` over our data.
        SkArenaAlloc alloc(/*firstHeapAllocation=*/256);
        SkRasterPipeline p(&alloc);
        auto* ctx = alloc.make<SkRasterPipeline_BinaryOpCtx>();
        ctx->dst = slots;
        ctx->src = constants;
        p.append(op.stage, ctx);
        p.run(0,0,1,1);

        // Verify that our constants have been broadcast into each slot.
        float expectedUnchanged = 1.0f;
        float expectedChanged = 1000.0f;
        float* destPtr = &slots[0];
        for (int checkSlot = 0; checkSlot < 5; ++checkSlot) {
            for (int checkLane = 0; checkLane < N; ++checkLane) {
                if (checkSlot < op.numSlotsAffected) {
                    REPORTER_ASSERT(r, *destPtr == expectedChanged);
                } else {
                    REPORTER_ASSERT(r, *destPtr == expectedUnchanged);
                }

                ++destPtr;
                expectedUnchanged += 1.0f;
            }
            expectedChanged += 1.0f;
        }
    }
}

DEF_TEST(SkRasterPipeline_Swizzle, r) {
    // Allocate space for 4 dest slots.
    alignas(64) float slots[4 * SkRasterPipeline_kMaxStride_highp];
    const int N = SkOpts::raster_pipeline_highp_stride;

    struct TestPattern {
        SkRasterPipeline::Stage stage;
        uint16_t swizzle[4];
        uint16_t expectation[4];
    };
    static const TestPattern kPatterns[] = {
            {SkRasterPipeline::swizzle_1, {3},          {3, 1, 2, 3}}, // (1,2,3,4).w    = (4)
            {SkRasterPipeline::swizzle_2, {1, 0},       {1, 0, 2, 3}}, // (1,2,3,4).yx   = (2,1)
            {SkRasterPipeline::swizzle_3, {2, 2, 2},    {2, 2, 2, 3}}, // (1,2,3,4).zzz  = (3,3,3)
            {SkRasterPipeline::swizzle_4, {0, 0, 1, 2}, {0, 0, 1, 2}}, // (1,2,3,4).xxyz = (1,1,2,3)
    };
    static_assert(sizeof(TestPattern::swizzle) == sizeof(SkRasterPipeline_SwizzleCtx::offsets));

    for (const TestPattern& pattern : kPatterns) {
        // Initialize the destination slots to 0,1,2,3...
        std::iota(&slots[0], &slots[4 * N], 0.0f);

        // Apply the test-pattern swizzle.
        SkArenaAlloc alloc(/*firstHeapAllocation=*/256);
        SkRasterPipeline p(&alloc);
        SkRasterPipeline_SwizzleCtx ctx;
        ctx.ptr = slots;
        for (size_t index = 0; index < std::size(ctx.offsets); ++index) {
            ctx.offsets[index] = pattern.swizzle[index] * N * sizeof(float);
        }
        p.append(pattern.stage, &ctx);
        p.run(0,0,1,1);

        // Verify that the swizzle has been applied in each slot.
        float* destPtr = &slots[0];
        for (int checkSlot = 0; checkSlot < 4; ++checkSlot) {
            float expected = pattern.expectation[checkSlot] * N;
            for (int checkLane = 0; checkLane < N; ++checkLane) {
                REPORTER_ASSERT(r, *destPtr == expected);

                ++destPtr;
                expected += 1.0f;
            }
        }
    }
}

DEF_TEST(SkRasterPipeline_FloatArithmeticWithNSlots, r) {
    // Allocate space for 5 dest and 5 source slots.
    alignas(64) float slots[10 * SkRasterPipeline_kMaxStride_highp];
    const int N = SkOpts::raster_pipeline_highp_stride;

    struct ArithmeticOp {
        SkRasterPipeline::Stage stage;
        std::function<float(float, float)> verify;
    };

    static const ArithmeticOp kArithmeticOps[] = {
        {SkRasterPipeline::Stage::add_n_floats, [](float a, float b) { return a + b; }},
        {SkRasterPipeline::Stage::sub_n_floats, [](float a, float b) { return a - b; }},
        {SkRasterPipeline::Stage::mul_n_floats, [](float a, float b) { return a * b; }},
        {SkRasterPipeline::Stage::div_n_floats, [](float a, float b) { return a / b; }},
    };

    for (const ArithmeticOp& op : kArithmeticOps) {
        for (int numSlotsAffected = 1; numSlotsAffected <= 5; ++numSlotsAffected) {
            // Initialize the slot values to 1,2,3...
            std::iota(&slots[0], &slots[10 * N], 1.0f);

            // Run the arithmetic op over our data.
            SkArenaAlloc alloc(/*firstHeapAllocation=*/256);
            SkRasterPipeline p(&alloc);
            auto* ctx = alloc.make<SkRasterPipeline_BinaryOpCtx>();
            ctx->dst = &slots[0];
            ctx->src = &slots[numSlotsAffected * N];
            p.append(op.stage, ctx);
            p.run(0,0,1,1);

            // Verify that the affected slots now equal (1,2,3...) op (4,5,6...).
            float leftValue = 1.0f;
            float rightValue = float(numSlotsAffected * N) + 1.0f;
            float* destPtr = &slots[0];
            for (int checkSlot = 0; checkSlot < 10; ++checkSlot) {
                for (int checkLane = 0; checkLane < N; ++checkLane) {
                    if (checkSlot < numSlotsAffected) {
                        REPORTER_ASSERT(r, *destPtr == op.verify(leftValue, rightValue));
                    } else {
                        REPORTER_ASSERT(r, *destPtr == leftValue);
                    }

                    ++destPtr;
                    leftValue += 1.0f;
                    rightValue += 1.0f;
                }
            }
        }
    }
}

DEF_TEST(SkRasterPipeline_FloatArithmeticWithHardcodedSlots, r) {
    // Allocate space for 5 dest and 5 source slots.
    alignas(64) float slots[10 * SkRasterPipeline_kMaxStride_highp];
    const int N = SkOpts::raster_pipeline_highp_stride;

    struct ArithmeticOp {
        SkRasterPipeline::Stage stage;
        int numSlotsAffected;
        std::function<float(float, float)> verify;
    };

    static const ArithmeticOp kArithmeticOps[] = {
        {SkRasterPipeline::Stage::add_float,    1, [](float a, float b) { return a + b; }},
        {SkRasterPipeline::Stage::sub_float,    1, [](float a, float b) { return a - b; }},
        {SkRasterPipeline::Stage::mul_float,    1, [](float a, float b) { return a * b; }},
        {SkRasterPipeline::Stage::div_float,    1, [](float a, float b) { return a / b; }},

        {SkRasterPipeline::Stage::add_2_floats, 2, [](float a, float b) { return a + b; }},
        {SkRasterPipeline::Stage::sub_2_floats, 2, [](float a, float b) { return a - b; }},
        {SkRasterPipeline::Stage::mul_2_floats, 2, [](float a, float b) { return a * b; }},
        {SkRasterPipeline::Stage::div_2_floats, 2, [](float a, float b) { return a / b; }},

        {SkRasterPipeline::Stage::add_3_floats, 3, [](float a, float b) { return a + b; }},
        {SkRasterPipeline::Stage::sub_3_floats, 3, [](float a, float b) { return a - b; }},
        {SkRasterPipeline::Stage::mul_3_floats, 3, [](float a, float b) { return a * b; }},
        {SkRasterPipeline::Stage::div_3_floats, 3, [](float a, float b) { return a / b; }},

        {SkRasterPipeline::Stage::add_4_floats, 4, [](float a, float b) { return a + b; }},
        {SkRasterPipeline::Stage::sub_4_floats, 4, [](float a, float b) { return a - b; }},
        {SkRasterPipeline::Stage::mul_4_floats, 4, [](float a, float b) { return a * b; }},
        {SkRasterPipeline::Stage::div_4_floats, 4, [](float a, float b) { return a / b; }},
    };

    for (const ArithmeticOp& op : kArithmeticOps) {
        // Initialize the slot values to 1,2,3...
        std::iota(&slots[0], &slots[10 * N], 1.0f);

        // Run the arithmetic op over our data.
        SkArenaAlloc alloc(/*firstHeapAllocation=*/256);
        SkRasterPipeline p(&alloc);
        p.append(op.stage, &slots[0]);
        p.run(0,0,1,1);

        // Verify that the affected slots now equal (1,2,3...) op (4,5,6...).
        float leftValue = 1.0f;
        float rightValue = float(op.numSlotsAffected * N) + 1.0f;
        float* destPtr = &slots[0];
        for (int checkSlot = 0; checkSlot < 10; ++checkSlot) {
            for (int checkLane = 0; checkLane < N; ++checkLane) {
                if (checkSlot < op.numSlotsAffected) {
                    REPORTER_ASSERT(r, *destPtr == op.verify(leftValue, rightValue));
                } else {
                    REPORTER_ASSERT(r, *destPtr == leftValue);
                }

                ++destPtr;
                leftValue += 1.0f;
                rightValue += 1.0f;
            }
        }
    }
}

static int divide_unsigned(int a, int b) { return int(uint32_t(a) / uint32_t(b)); }
static int min_unsigned   (int a, int b) { return uint32_t(a) < uint32_t(b) ? a : b; }
static int max_unsigned   (int a, int b) { return uint32_t(a) > uint32_t(b) ? a : b; }

DEF_TEST(SkRasterPipeline_IntArithmeticWithNSlots, r) {
    // Allocate space for 5 dest and 5 source slots.
    alignas(64) int slots[10 * SkRasterPipeline_kMaxStride_highp];
    const int N = SkOpts::raster_pipeline_highp_stride;

    struct ArithmeticOp {
        SkRasterPipeline::Stage stage;
        std::function<int(int, int)> verify;
    };

    static const ArithmeticOp kArithmeticOps[] = {
        {SkRasterPipeline::Stage::add_n_ints,         [](int a, int b) { return a + b; }},
        {SkRasterPipeline::Stage::sub_n_ints,         [](int a, int b) { return a - b; }},
        {SkRasterPipeline::Stage::mul_n_ints,         [](int a, int b) { return a * b; }},
        {SkRasterPipeline::Stage::div_n_ints,         [](int a, int b) { return a / b; }},
        {SkRasterPipeline::Stage::div_n_uints,        divide_unsigned},
        {SkRasterPipeline::Stage::bitwise_and_n_ints, [](int a, int b) { return a & b; }},
        {SkRasterPipeline::Stage::bitwise_or_n_ints,  [](int a, int b) { return a | b; }},
        {SkRasterPipeline::Stage::bitwise_xor_n_ints, [](int a, int b) { return a ^ b; }},
        {SkRasterPipeline::Stage::min_n_ints,         [](int a, int b) { return a < b ? a : b; }},
        {SkRasterPipeline::Stage::min_n_uints,        min_unsigned},
        {SkRasterPipeline::Stage::max_n_ints,         [](int a, int b) { return a > b ? a : b; }},
        {SkRasterPipeline::Stage::max_n_uints,        max_unsigned},
    };

    for (const ArithmeticOp& op : kArithmeticOps) {
        for (int numSlotsAffected = 1; numSlotsAffected <= 5; ++numSlotsAffected) {
            // Initialize the slot values to 1,2,3...
            std::iota(&slots[0], &slots[10 * N], 1);
            int leftValue = slots[0];
            int rightValue = slots[numSlotsAffected * N];

            // Run the op (e.g. `add_n_ints`) over our data.
            SkArenaAlloc alloc(/*firstHeapAllocation=*/256);
            SkRasterPipeline p(&alloc);
            auto* ctx = alloc.make<SkRasterPipeline_BinaryOpCtx>();
            ctx->dst = (float*)&slots[0];
            ctx->src = (float*)&slots[numSlotsAffected * N];
            p.append(op.stage, ctx);
            p.run(0,0,1,1);

            // Verify that the affected slots now equal (1,2,3...) op (4,5,6...).
            int* destPtr = &slots[0];
            for (int checkSlot = 0; checkSlot < 10; ++checkSlot) {
                for (int checkLane = 0; checkLane < N; ++checkLane) {
                    if (checkSlot < numSlotsAffected) {
                        REPORTER_ASSERT(r, *destPtr == op.verify(leftValue, rightValue));
                    } else {
                        REPORTER_ASSERT(r, *destPtr == leftValue);
                    }

                    ++destPtr;
                    leftValue += 1;
                    rightValue += 1;
                }
            }
        }
    }
}

DEF_TEST(SkRasterPipeline_IntArithmeticWithHardcodedSlots, r) {
    // Allocate space for 5 dest and 5 source slots.
    alignas(64) int slots[10 * SkRasterPipeline_kMaxStride_highp];
    const int N = SkOpts::raster_pipeline_highp_stride;

    struct ArithmeticOp {
        SkRasterPipeline::Stage stage;
        int numSlotsAffected;
        std::function<int(int, int)> verify;
    };

    static const ArithmeticOp kArithmeticOps[] = {
        {SkRasterPipeline::Stage::add_int,            1, [](int a, int b) { return a + b; }},
        {SkRasterPipeline::Stage::sub_int,            1, [](int a, int b) { return a - b; }},
        {SkRasterPipeline::Stage::mul_int,            1, [](int a, int b) { return a * b; }},
        {SkRasterPipeline::Stage::div_int,            1, [](int a, int b) { return a / b; }},
        {SkRasterPipeline::Stage::div_uint,           1, divide_unsigned},
        {SkRasterPipeline::Stage::bitwise_and_int,    1, [](int a, int b) { return a & b; }},
        {SkRasterPipeline::Stage::bitwise_or_int,     1, [](int a, int b) { return a | b; }},
        {SkRasterPipeline::Stage::bitwise_xor_int,    1, [](int a, int b) { return a ^ b; }},
        {SkRasterPipeline::Stage::min_int,            1, [](int a, int b) { return a < b ? a: b; }},
        {SkRasterPipeline::Stage::min_uint,           1, min_unsigned},
        {SkRasterPipeline::Stage::max_int,            1, [](int a, int b) { return a > b ? a: b; }},
        {SkRasterPipeline::Stage::max_uint,           1, max_unsigned},

        {SkRasterPipeline::Stage::add_2_ints,         2, [](int a, int b) { return a + b; }},
        {SkRasterPipeline::Stage::sub_2_ints,         2, [](int a, int b) { return a - b; }},
        {SkRasterPipeline::Stage::mul_2_ints,         2, [](int a, int b) { return a * b; }},
        {SkRasterPipeline::Stage::div_2_ints,         2, [](int a, int b) { return a / b; }},
        {SkRasterPipeline::Stage::div_2_uints,        2, divide_unsigned},
        {SkRasterPipeline::Stage::bitwise_and_2_ints, 2, [](int a, int b) { return a & b; }},
        {SkRasterPipeline::Stage::bitwise_or_2_ints,  2, [](int a, int b) { return a | b; }},
        {SkRasterPipeline::Stage::bitwise_xor_2_ints, 2, [](int a, int b) { return a ^ b; }},
        {SkRasterPipeline::Stage::min_2_ints,         2, [](int a, int b) { return a < b ? a: b; }},
        {SkRasterPipeline::Stage::min_2_uints,        2, min_unsigned},
        {SkRasterPipeline::Stage::max_2_ints,         2, [](int a, int b) { return a > b ? a: b; }},
        {SkRasterPipeline::Stage::max_2_uints,        2, max_unsigned},

        {SkRasterPipeline::Stage::add_3_ints,         3, [](int a, int b) { return a + b; }},
        {SkRasterPipeline::Stage::sub_3_ints,         3, [](int a, int b) { return a - b; }},
        {SkRasterPipeline::Stage::mul_3_ints,         3, [](int a, int b) { return a * b; }},
        {SkRasterPipeline::Stage::div_3_ints,         3, [](int a, int b) { return a / b; }},
        {SkRasterPipeline::Stage::div_3_uints,        3, divide_unsigned},
        {SkRasterPipeline::Stage::bitwise_and_3_ints, 3, [](int a, int b) { return a & b; }},
        {SkRasterPipeline::Stage::bitwise_or_3_ints,  3, [](int a, int b) { return a | b; }},
        {SkRasterPipeline::Stage::bitwise_xor_3_ints, 3, [](int a, int b) { return a ^ b; }},
        {SkRasterPipeline::Stage::min_3_ints,         3, [](int a, int b) { return a < b ? a: b; }},
        {SkRasterPipeline::Stage::min_3_uints,        3, min_unsigned},
        {SkRasterPipeline::Stage::max_3_ints,         3, [](int a, int b) { return a > b ? a: b; }},
        {SkRasterPipeline::Stage::max_3_uints,        3, max_unsigned},

        {SkRasterPipeline::Stage::add_4_ints,         4, [](int a, int b) { return a + b; }},
        {SkRasterPipeline::Stage::sub_4_ints,         4, [](int a, int b) { return a - b; }},
        {SkRasterPipeline::Stage::mul_4_ints,         4, [](int a, int b) { return a * b; }},
        {SkRasterPipeline::Stage::div_4_ints,         4, [](int a, int b) { return a / b; }},
        {SkRasterPipeline::Stage::div_4_uints,        4, divide_unsigned},
        {SkRasterPipeline::Stage::bitwise_and_4_ints, 4, [](int a, int b) { return a & b; }},
        {SkRasterPipeline::Stage::bitwise_or_4_ints,  4, [](int a, int b) { return a | b; }},
        {SkRasterPipeline::Stage::bitwise_xor_4_ints, 4, [](int a, int b) { return a ^ b; }},
        {SkRasterPipeline::Stage::min_4_ints,         4, [](int a, int b) { return a < b ? a: b; }},
        {SkRasterPipeline::Stage::min_4_uints,        4, min_unsigned},
        {SkRasterPipeline::Stage::max_4_ints,         4, [](int a, int b) { return a > b ? a: b; }},
        {SkRasterPipeline::Stage::max_4_uints,        4, max_unsigned},
    };

    for (const ArithmeticOp& op : kArithmeticOps) {
        // Initialize the slot values to 1,2,3...
        std::iota(&slots[0], &slots[10 * N], 1);
        int leftValue = slots[0];
        int rightValue = slots[op.numSlotsAffected * N];

        // Run the op (e.g. `add_2_ints`) over our data.
        SkArenaAlloc alloc(/*firstHeapAllocation=*/256);
        SkRasterPipeline p(&alloc);
        p.append(op.stage, &slots[0]);
        p.run(0,0,1,1);

        // Verify that the affected slots now equal (1,2,3...) op (4,5,6...).
        int* destPtr = &slots[0];
        for (int checkSlot = 0; checkSlot < 10; ++checkSlot) {
            for (int checkLane = 0; checkLane < N; ++checkLane) {
                if (checkSlot < op.numSlotsAffected) {
                    REPORTER_ASSERT(r, *destPtr == op.verify(leftValue, rightValue));
                } else {
                    REPORTER_ASSERT(r, *destPtr == leftValue);
                }

                ++destPtr;
                leftValue += 1;
                rightValue += 1;
            }
        }
    }
}

DEF_TEST(SkRasterPipeline_CompareFloatsWithNSlots, r) {
    // Allocate space for 5 dest and 5 source slots.
    alignas(64) float slots[10 * SkRasterPipeline_kMaxStride_highp];
    const int N = SkOpts::raster_pipeline_highp_stride;

    struct CompareOp {
        SkRasterPipeline::Stage stage;
        std::function<bool(float, float)> verify;
    };

    static const CompareOp kCompareOps[] = {
        {SkRasterPipeline::Stage::cmpeq_n_floats, [](float a, float b) { return a == b; }},
        {SkRasterPipeline::Stage::cmpne_n_floats, [](float a, float b) { return a != b; }},
        {SkRasterPipeline::Stage::cmplt_n_floats, [](float a, float b) { return a <  b; }},
        {SkRasterPipeline::Stage::cmple_n_floats, [](float a, float b) { return a <= b; }},
    };

    for (const CompareOp& op : kCompareOps) {
        for (int numSlotsAffected = 1; numSlotsAffected <= 5; ++numSlotsAffected) {
            // Initialize the slot values to 0,1,2,0,1,2,0,1,2...
            for (int index = 0; index < 10 * N; ++index) {
                slots[index] = std::fmod(index, 3.0f);
            }

            float leftValue  = slots[0];
            float rightValue = slots[numSlotsAffected * N];

            // Run the comparison op over our data.
            SkArenaAlloc alloc(/*firstHeapAllocation=*/256);
            SkRasterPipeline p(&alloc);
            auto* ctx = alloc.make<SkRasterPipeline_BinaryOpCtx>();
            ctx->dst = &slots[0];
            ctx->src = &slots[numSlotsAffected * N];
            p.append(op.stage, ctx);
            p.run(0, 0, 1, 1);

            // Verify that the affected slots now contain "(0,1,2,0...) op (1,2,0,1...)".
            float* destPtr = &slots[0];
            for (int checkSlot = 0; checkSlot < 10; ++checkSlot) {
                for (int checkLane = 0; checkLane < N; ++checkLane) {
                    if (checkSlot < numSlotsAffected) {
                        bool compareIsTrue = op.verify(leftValue, rightValue);
                        REPORTER_ASSERT(r, *(int*)destPtr == (compareIsTrue ? ~0 : 0));
                    } else {
                        REPORTER_ASSERT(r, *destPtr == leftValue);
                    }

                    ++destPtr;
                    leftValue = std::fmod(leftValue + 1.0f, 3.0f);
                    rightValue = std::fmod(rightValue + 1.0f, 3.0f);
                }
            }
        }
    }
}

DEF_TEST(SkRasterPipeline_CompareFloatsWithHardcodedSlots, r) {
    // Allocate space for 5 dest and 5 source slots.
    alignas(64) float slots[10 * SkRasterPipeline_kMaxStride_highp];
    const int N = SkOpts::raster_pipeline_highp_stride;

    struct CompareOp {
        SkRasterPipeline::Stage stage;
        int numSlotsAffected;
        std::function<bool(float, float)> verify;
    };

    static const CompareOp kCompareOps[] = {
        {SkRasterPipeline::Stage::cmpeq_float,    1, [](float a, float b) { return a == b; }},
        {SkRasterPipeline::Stage::cmpne_float,    1, [](float a, float b) { return a != b; }},
        {SkRasterPipeline::Stage::cmplt_float,    1, [](float a, float b) { return a <  b; }},
        {SkRasterPipeline::Stage::cmple_float,    1, [](float a, float b) { return a <= b; }},

        {SkRasterPipeline::Stage::cmpeq_2_floats, 2, [](float a, float b) { return a == b; }},
        {SkRasterPipeline::Stage::cmpne_2_floats, 2, [](float a, float b) { return a != b; }},
        {SkRasterPipeline::Stage::cmplt_2_floats, 2, [](float a, float b) { return a <  b; }},
        {SkRasterPipeline::Stage::cmple_2_floats, 2, [](float a, float b) { return a <= b; }},

        {SkRasterPipeline::Stage::cmpeq_3_floats, 3, [](float a, float b) { return a == b; }},
        {SkRasterPipeline::Stage::cmpne_3_floats, 3, [](float a, float b) { return a != b; }},
        {SkRasterPipeline::Stage::cmplt_3_floats, 3, [](float a, float b) { return a <  b; }},
        {SkRasterPipeline::Stage::cmple_3_floats, 3, [](float a, float b) { return a <= b; }},

        {SkRasterPipeline::Stage::cmpeq_4_floats, 4, [](float a, float b) { return a == b; }},
        {SkRasterPipeline::Stage::cmpne_4_floats, 4, [](float a, float b) { return a != b; }},
        {SkRasterPipeline::Stage::cmplt_4_floats, 4, [](float a, float b) { return a <  b; }},
        {SkRasterPipeline::Stage::cmple_4_floats, 4, [](float a, float b) { return a <= b; }},
    };

    for (const CompareOp& op : kCompareOps) {
        // Initialize the slot values to 0,1,2,0,1,2,0,1,2...
        for (int index = 0; index < 10 * N; ++index) {
            slots[index] = std::fmod(index, 3.0f);
        }

        float leftValue  = slots[0];
        float rightValue = slots[op.numSlotsAffected * N];

        // Run the comparison op over our data.
        SkArenaAlloc alloc(/*firstHeapAllocation=*/256);
        SkRasterPipeline p(&alloc);
        p.append(op.stage, &slots[0]);
        p.run(0, 0, 1, 1);

        // Verify that the affected slots now contain "(0,1,2,0...) op (1,2,0,1...)".
        float* destPtr = &slots[0];
        for (int checkSlot = 0; checkSlot < 10; ++checkSlot) {
            for (int checkLane = 0; checkLane < N; ++checkLane) {
                if (checkSlot < op.numSlotsAffected) {
                    bool compareIsTrue = op.verify(leftValue, rightValue);
                    REPORTER_ASSERT(r, *(int*)destPtr == (compareIsTrue ? ~0 : 0));
                } else {
                    REPORTER_ASSERT(r, *destPtr == leftValue);
                }

                ++destPtr;
                leftValue = std::fmod(leftValue + 1.0f, 3.0f);
                rightValue = std::fmod(rightValue + 1.0f, 3.0f);
            }
        }
    }
}

static bool compare_lt_uint  (int a, int b) { return uint32_t(a) <  uint32_t(b); }
static bool compare_lteq_uint(int a, int b) { return uint32_t(a) <= uint32_t(b); }

DEF_TEST(SkRasterPipeline_CompareIntsWithNSlots, r) {
    // Allocate space for 5 dest and 5 source slots.
    alignas(64) int slots[10 * SkRasterPipeline_kMaxStride_highp];
    const int N = SkOpts::raster_pipeline_highp_stride;

    struct CompareOp {
        SkRasterPipeline::Stage stage;
        std::function<bool(int, int)> verify;
    };

    static const CompareOp kCompareOps[] = {
        {SkRasterPipeline::Stage::cmpeq_n_ints,  [](int a, int b) { return a == b; }},
        {SkRasterPipeline::Stage::cmpne_n_ints,  [](int a, int b) { return a != b; }},
        {SkRasterPipeline::Stage::cmplt_n_ints,  [](int a, int b) { return a <  b; }},
        {SkRasterPipeline::Stage::cmple_n_ints,  [](int a, int b) { return a <= b; }},
        {SkRasterPipeline::Stage::cmplt_n_uints, compare_lt_uint},
        {SkRasterPipeline::Stage::cmple_n_uints, compare_lteq_uint},
    };

    for (const CompareOp& op : kCompareOps) {
        for (int numSlotsAffected = 1; numSlotsAffected <= 5; ++numSlotsAffected) {
            // Initialize the slot values to -1,0,1,-1,0,1,-1,0,1,-1...
            for (int index = 0; index < 10 * N; ++index) {
                slots[index] = (index % 3) - 1;
            }

            int leftValue = slots[0];
            int rightValue = slots[numSlotsAffected * N];

            // Run the comparison op over our data.
            SkArenaAlloc alloc(/*firstHeapAllocation=*/256);
            SkRasterPipeline p(&alloc);
            auto* ctx = alloc.make<SkRasterPipeline_BinaryOpCtx>();
            ctx->dst = (float*)&slots[0];
            ctx->src = (float*)&slots[numSlotsAffected * N];
            p.append(op.stage, ctx);
            p.run(0, 0, 1, 1);

            // Verify that the affected slots now contain "(-1,0,1,-1...) op (0,1,-1,0...)".
            int* destPtr = &slots[0];
            for (int checkSlot = 0; checkSlot < 10; ++checkSlot) {
                for (int checkLane = 0; checkLane < N; ++checkLane) {
                    if (checkSlot < numSlotsAffected) {
                        bool compareIsTrue = op.verify(leftValue, rightValue);
                        REPORTER_ASSERT(r, *destPtr == (compareIsTrue ? ~0 : 0));
                    } else {
                        REPORTER_ASSERT(r, *destPtr == leftValue);
                    }

                    ++destPtr;
                    if (++leftValue == 2) {
                        leftValue = -1;
                    }
                    if (++rightValue == 2) {
                        rightValue = -1;
                    }
                }
            }
        }
    }
}

DEF_TEST(SkRasterPipeline_CompareIntsWithHardcodedSlots, r) {
    // Allocate space for 5 dest and 5 source slots.
    alignas(64) int slots[10 * SkRasterPipeline_kMaxStride_highp];
    const int N = SkOpts::raster_pipeline_highp_stride;

    struct CompareOp {
        SkRasterPipeline::Stage stage;
        int numSlotsAffected;
        std::function<bool(int, int)> verify;
    };

    static const CompareOp kCompareOps[] = {
        {SkRasterPipeline::Stage::cmpeq_int,     1, [](int a, int b) { return a == b; }},
        {SkRasterPipeline::Stage::cmpne_int,     1, [](int a, int b) { return a != b; }},
        {SkRasterPipeline::Stage::cmplt_int,     1, [](int a, int b) { return a <  b; }},
        {SkRasterPipeline::Stage::cmple_int,     1, [](int a, int b) { return a <= b; }},
        {SkRasterPipeline::Stage::cmplt_uint,    1, compare_lt_uint},
        {SkRasterPipeline::Stage::cmple_uint,    1, compare_lteq_uint},

        {SkRasterPipeline::Stage::cmpeq_2_ints,  2, [](int a, int b) { return a == b; }},
        {SkRasterPipeline::Stage::cmpne_2_ints,  2, [](int a, int b) { return a != b; }},
        {SkRasterPipeline::Stage::cmplt_2_ints,  2, [](int a, int b) { return a <  b; }},
        {SkRasterPipeline::Stage::cmple_2_ints,  2, [](int a, int b) { return a <= b; }},
        {SkRasterPipeline::Stage::cmplt_2_uints, 2, compare_lt_uint},
        {SkRasterPipeline::Stage::cmple_2_uints, 2, compare_lteq_uint},

        {SkRasterPipeline::Stage::cmpeq_3_ints,  3, [](int a, int b) { return a == b; }},
        {SkRasterPipeline::Stage::cmpne_3_ints,  3, [](int a, int b) { return a != b; }},
        {SkRasterPipeline::Stage::cmplt_3_ints,  3, [](int a, int b) { return a <  b; }},
        {SkRasterPipeline::Stage::cmple_3_ints,  3, [](int a, int b) { return a <= b; }},
        {SkRasterPipeline::Stage::cmplt_3_uints, 3, compare_lt_uint},
        {SkRasterPipeline::Stage::cmple_3_uints, 3, compare_lteq_uint},

        {SkRasterPipeline::Stage::cmpeq_4_ints,  4, [](int a, int b) { return a == b; }},
        {SkRasterPipeline::Stage::cmpne_4_ints,  4, [](int a, int b) { return a != b; }},
        {SkRasterPipeline::Stage::cmplt_4_ints,  4, [](int a, int b) { return a <  b; }},
        {SkRasterPipeline::Stage::cmple_4_ints,  4, [](int a, int b) { return a <= b; }},
        {SkRasterPipeline::Stage::cmplt_4_uints, 4, compare_lt_uint},
        {SkRasterPipeline::Stage::cmple_4_uints, 4, compare_lteq_uint},
    };

    for (const CompareOp& op : kCompareOps) {
        // Initialize the slot values to -1,0,1,-1,0,1,-1,0,1,-1...
        for (int index = 0; index < 10 * N; ++index) {
            slots[index] = (index % 3) - 1;
        }

        int leftValue = slots[0];
        int rightValue = slots[op.numSlotsAffected * N];

        // Run the comparison op over our data.
        SkArenaAlloc alloc(/*firstHeapAllocation=*/256);
        SkRasterPipeline p(&alloc);
        p.append(op.stage, &slots[0]);
        p.run(0, 0, 1, 1);

        // Verify that the affected slots now contain "(0,1,2,0...) op (1,2,0,1...)".
        int* destPtr = &slots[0];
        for (int checkSlot = 0; checkSlot < 10; ++checkSlot) {
            for (int checkLane = 0; checkLane < N; ++checkLane) {
                if (checkSlot < op.numSlotsAffected) {
                    bool compareIsTrue = op.verify(leftValue, rightValue);
                    REPORTER_ASSERT(r, *destPtr == (compareIsTrue ? ~0 : 0));
                } else {
                    REPORTER_ASSERT(r, *destPtr == leftValue);
                }

                ++destPtr;
                if (++leftValue == 2) {
                    leftValue = -1;
                }
                if (++rightValue == 2) {
                    rightValue = -1;
                }
            }
        }
    }
}

static int to_float(int a) { return sk_bit_cast<int>((float)a); }

DEF_TEST(SkRasterPipeline_UnaryIntOps, r) {
    // Allocate space for 5 slots.
    alignas(64) int slots[5 * SkRasterPipeline_kMaxStride_highp];
    const int N = SkOpts::raster_pipeline_highp_stride;

    struct UnaryOp {
        SkRasterPipeline::Stage stage;
        int numSlotsAffected;
        std::function<int(int)> verify;
    };

    static const UnaryOp kUnaryOps[] = {
        {SkRasterPipeline::Stage::bitwise_not_int,    1, [](int a) { return ~a; }},
        {SkRasterPipeline::Stage::bitwise_not_2_ints, 2, [](int a) { return ~a; }},
        {SkRasterPipeline::Stage::bitwise_not_3_ints, 3, [](int a) { return ~a; }},
        {SkRasterPipeline::Stage::bitwise_not_4_ints, 4, [](int a) { return ~a; }},

        {SkRasterPipeline::Stage::cast_to_float_from_int,    1, to_float},
        {SkRasterPipeline::Stage::cast_to_float_from_2_ints, 2, to_float},
        {SkRasterPipeline::Stage::cast_to_float_from_3_ints, 3, to_float},
        {SkRasterPipeline::Stage::cast_to_float_from_4_ints, 4, to_float},

        {SkRasterPipeline::Stage::abs_int,    1, [](int a) { return a < 0 ? -a : a; }},
        {SkRasterPipeline::Stage::abs_2_ints, 2, [](int a) { return a < 0 ? -a : a; }},
        {SkRasterPipeline::Stage::abs_3_ints, 3, [](int a) { return a < 0 ? -a : a; }},
        {SkRasterPipeline::Stage::abs_4_ints, 4, [](int a) { return a < 0 ? -a : a; }},
    };

    for (const UnaryOp& op : kUnaryOps) {
        // Initialize the slot values to -10,-9,-8...
        std::iota(&slots[0], &slots[5 * N], -10);
        int inputValue = slots[0];

        // Run the unary op over our data.
        SkArenaAlloc alloc(/*firstHeapAllocation=*/256);
        SkRasterPipeline p(&alloc);
        p.append(op.stage, &slots[0]);
        p.run(0, 0, 1, 1);

        // Verify that the destination slots have been updated.
        int* destPtr = &slots[0];
        for (int checkSlot = 0; checkSlot < 5; ++checkSlot) {
            for (int checkLane = 0; checkLane < N; ++checkLane) {
                if (checkSlot < op.numSlotsAffected) {
                    int expected = op.verify(inputValue);
                    REPORTER_ASSERT(r, *destPtr == expected);
                } else {
                    REPORTER_ASSERT(r, *destPtr == inputValue);
                }

                ++destPtr;
                ++inputValue;
            }
        }
    }
}

static float to_int(float a)  { return sk_bit_cast<float>((int)a); }
static float to_uint(float a) { return sk_bit_cast<float>((unsigned int)a); }

DEF_TEST(SkRasterPipeline_UnaryFloatOps, r) {
    // Allocate space for 5 slots.
    alignas(64) float slots[5 * SkRasterPipeline_kMaxStride_highp];
    const int N = SkOpts::raster_pipeline_highp_stride;

    struct UnaryOp {
        SkRasterPipeline::Stage stage;
        int numSlotsAffected;
        std::function<float(float)> verify;
    };

    static const UnaryOp kUnaryOps[] = {
        {SkRasterPipeline::Stage::cast_to_int_from_float,    1, to_int},
        {SkRasterPipeline::Stage::cast_to_int_from_2_floats, 2, to_int},
        {SkRasterPipeline::Stage::cast_to_int_from_3_floats, 3, to_int},
        {SkRasterPipeline::Stage::cast_to_int_from_4_floats, 4, to_int},

        {SkRasterPipeline::Stage::cast_to_uint_from_float,    1, to_uint},
        {SkRasterPipeline::Stage::cast_to_uint_from_2_floats, 2, to_uint},
        {SkRasterPipeline::Stage::cast_to_uint_from_3_floats, 3, to_uint},
        {SkRasterPipeline::Stage::cast_to_uint_from_4_floats, 4, to_uint},

        {SkRasterPipeline::Stage::abs_float,    1, [](float a) { return a < 0 ? -a : a; }},
        {SkRasterPipeline::Stage::abs_2_floats, 2, [](float a) { return a < 0 ? -a : a; }},
        {SkRasterPipeline::Stage::abs_3_floats, 3, [](float a) { return a < 0 ? -a : a; }},
        {SkRasterPipeline::Stage::abs_4_floats, 4, [](float a) { return a < 0 ? -a : a; }},

        {SkRasterPipeline::Stage::floor_float,    1, [](float a) { return floorf(a); }},
        {SkRasterPipeline::Stage::floor_2_floats, 2, [](float a) { return floorf(a); }},
        {SkRasterPipeline::Stage::floor_3_floats, 3, [](float a) { return floorf(a); }},
        {SkRasterPipeline::Stage::floor_4_floats, 4, [](float a) { return floorf(a); }},

        {SkRasterPipeline::Stage::ceil_float,    1, [](float a) { return ceilf(a); }},
        {SkRasterPipeline::Stage::ceil_2_floats, 2, [](float a) { return ceilf(a); }},
        {SkRasterPipeline::Stage::ceil_3_floats, 3, [](float a) { return ceilf(a); }},
        {SkRasterPipeline::Stage::ceil_4_floats, 4, [](float a) { return ceilf(a); }},
    };

    for (const UnaryOp& op : kUnaryOps) {
        // The result of some ops are undefined with negative inputs, so only test positive values.
        bool positiveOnly = (op.stage == SkRasterPipeline::Stage::cast_to_uint_from_float ||
                             op.stage == SkRasterPipeline::Stage::cast_to_uint_from_2_floats ||
                             op.stage == SkRasterPipeline::Stage::cast_to_uint_from_3_floats ||
                             op.stage == SkRasterPipeline::Stage::cast_to_uint_from_4_floats);

        float iotaStart = positiveOnly ? 1.0f : -9.75f;
        std::iota(&slots[0], &slots[5 * N], iotaStart);
        float inputValue = slots[0];

        // Run the unary op over our data.
        SkArenaAlloc alloc(/*firstHeapAllocation=*/256);
        SkRasterPipeline p(&alloc);
        p.append(op.stage, &slots[0]);
        p.run(0, 0, 1, 1);

        // Verify that the destination slots have been updated.
        float* destPtr = &slots[0];
        for (int checkSlot = 0; checkSlot < 5; ++checkSlot) {
            for (int checkLane = 0; checkLane < N; ++checkLane) {
                if (checkSlot < op.numSlotsAffected) {
                    float expected = op.verify(inputValue);
                    // The casting tests can generate NaN, depending on the input value, so a value
                    // match (via ==) might not succeed.
                    // The ceil tests can generate negative zeros _sometimes_, depending on the
                    // exact implementation of ceil(), so a bitwise match might not succeed.
                    // Because of this, we allow either a value match or a bitwise match.
                    bool bitwiseMatch = (0 == memcmp(destPtr, &expected, sizeof(float)));
                    bool valueMatch   = (*destPtr == expected);
                    REPORTER_ASSERT(r, valueMatch || bitwiseMatch);
                } else {
                    REPORTER_ASSERT(r, *destPtr == inputValue);
                }

                ++destPtr;
                ++inputValue;
            }
        }
    }
}

static float to_mix_weight(float value) {
    // Convert a positive value to a mix-weight (a number between 0 and 1).
    value /= 16.0f;
    return value - std::floor(value);
}

DEF_TEST(SkRasterPipeline_MixTest, r) {
    // Allocate space for 5 dest and 10 source slots.
    alignas(64) float slots[15 * SkRasterPipeline_kMaxStride_highp];
    const int N = SkOpts::raster_pipeline_highp_stride;

    struct MixOp {
        int numSlotsAffected;
        std::function<void(SkRasterPipeline*, SkArenaAlloc*)> append;
    };

    static const MixOp kMixOps[] = {
        {1, [&](SkRasterPipeline* p, SkArenaAlloc* alloc) {
                p->append(SkRasterPipeline::mix_float, slots);
            }},
        {2, [&](SkRasterPipeline* p, SkArenaAlloc* alloc) {
                p->append(SkRasterPipeline::mix_2_floats, slots);
            }},
        {3, [&](SkRasterPipeline* p, SkArenaAlloc* alloc) {
                p->append(SkRasterPipeline::mix_3_floats, slots);
            }},
        {4, [&](SkRasterPipeline* p, SkArenaAlloc* alloc) {
                p->append(SkRasterPipeline::mix_4_floats, slots);
            }},
        {5, [&](SkRasterPipeline* p, SkArenaAlloc* alloc) {
                auto* ctx = alloc->make<SkRasterPipeline_TernaryOpCtx>();
                ctx->dst = &slots[0];
                ctx->src0 = &slots[5 * N];
                ctx->src1 = &slots[10 * N];
                p->append(SkRasterPipeline::mix_n_floats, ctx);
            }},
    };

    for (const MixOp& op : kMixOps) {
        // Initialize the values to 1,2,3...
        std::iota(&slots[0], &slots[15 * N], 1.0f);

        float fromValue   = slots[0];
        float toValue     = slots[1 * op.numSlotsAffected * N];
        float weightValue = slots[2 * op.numSlotsAffected * N];

        // The third group of values (the weight) must be between zero and one.
        for (int idx = 2 * op.numSlotsAffected * N; idx < 3 * op.numSlotsAffected * N; ++idx) {
            slots[idx] = to_mix_weight(slots[idx]);
        }

        // Run the mix op over our data.
        SkArenaAlloc alloc(/*firstHeapAllocation=*/256);
        SkRasterPipeline p(&alloc);
        op.append(&p, &alloc);
        p.run(0,0,1,1);

        // Verify that the affected slots now equal mix({1,2...}, {3,4...}, {0.25, 0.3125...).
        float* destPtr = &slots[0];
        for (int checkSlot = 0; checkSlot < op.numSlotsAffected; ++checkSlot) {
            for (int checkLane = 0; checkLane < N; ++checkLane) {
                float checkValue = (toValue - fromValue) * to_mix_weight(weightValue) + fromValue;
                REPORTER_ASSERT(r, *destPtr == checkValue);

                ++destPtr;
                fromValue += 1.0f;
                toValue += 1.0f;
                weightValue += 1.0f;
            }
        }
    }
}

DEF_TEST(SkRasterPipeline_Jump, r) {
    // Allocate space for 4 slots.
    alignas(64) float slots[4 * SkRasterPipeline_kMaxStride_highp] = {};
    const int N = SkOpts::raster_pipeline_highp_stride;

    alignas(64) static constexpr float kColorDarkRed[4] = {0.5f, 0.0f, 0.0f, 0.75f};
    alignas(64) static constexpr float kColorGreen[4]   = {0.0f, 1.0f, 0.0f, 1.0f};
    const int offset = 2;

    // Make a program which jumps over an append_constant_color op.
    SkArenaAlloc alloc(/*firstHeapAllocation=*/256);
    SkRasterPipeline p(&alloc);
    p.append_constant_color(&alloc, kColorGreen);    // assign green
    p.append(SkRasterPipeline::jump, &offset);       // jump over the dark-red color assignment
    p.append_constant_color(&alloc, kColorDarkRed);  // (not executed)
    p.append(SkRasterPipeline::store_src, slots);    // store the result so we can check it
    p.run(0,0,1,1);

    // Verify that the slots contain green.
    float* destPtr = &slots[0];
    for (int checkSlot = 0; checkSlot < 4; ++checkSlot) {
        for (int checkLane = 0; checkLane < N; ++checkLane) {
            REPORTER_ASSERT(r, *destPtr == kColorGreen[checkSlot]);
            ++destPtr;
        }
    }
}

DEF_TEST(SkRasterPipeline_BranchIfAnyActiveLanes, r) {
    // Allocate space for 4 slots.
    alignas(64) float slots[4 * SkRasterPipeline_kMaxStride_highp] = {};
    const int N = SkOpts::raster_pipeline_highp_stride;

    alignas(64) static constexpr float kColorDarkRed[4] = {0.5f, 0.0f, 0.0f, 0.75f};
    alignas(64) static constexpr float kColorGreen[4]   = {0.0f, 1.0f, 0.0f, 1.0f};
    const int offset = 2;

    // An array of all zeros.
    alignas(64) static constexpr int32_t kNoLanesActive[4 * SkRasterPipeline_kMaxStride_highp] = {};

    // An array of all zeros, except for a single ~0 in the first dA slot.
    alignas(64) int32_t oneLaneActive[4 * SkRasterPipeline_kMaxStride_highp] = {};
    oneLaneActive[3*N] = ~0;

    // Make a program which conditionally branches past two append_constant_color ops.
    SkArenaAlloc alloc(/*firstHeapAllocation=*/256);
    SkRasterPipeline p(&alloc);
    p.append_constant_color(&alloc, kColorDarkRed);                  // set the color to dark red
    p.append(SkRasterPipeline::load_dst, kNoLanesActive);            // make no lanes active
    p.append(SkRasterPipeline::branch_if_any_active_lanes, &offset); // do not skip past next line
    p.append_constant_color(&alloc, kColorGreen);                    // set the color to green
    p.append(SkRasterPipeline::load_dst, oneLaneActive);             // set one lane active
    p.append(SkRasterPipeline::branch_if_any_active_lanes, &offset); // skip past next line
    p.append_constant_color(&alloc, kColorDarkRed);                  // (not executed)
    p.append(SkRasterPipeline::init_lane_masks);                     // set all lanes active
    p.append(SkRasterPipeline::branch_if_any_active_lanes, &offset); // skip past next line
    p.append_constant_color(&alloc, kColorDarkRed);                  // (not executed)
    p.append(SkRasterPipeline::store_src, slots);                    // store final color
    p.run(0,0,1,1);

    // Verify that the slots contain green.
    float* destPtr = &slots[0];
    for (int checkSlot = 0; checkSlot < 4; ++checkSlot) {
        for (int checkLane = 0; checkLane < N; ++checkLane) {
            REPORTER_ASSERT(r, *destPtr == kColorGreen[checkSlot]);
            ++destPtr;
        }
    }
}

DEF_TEST(SkRasterPipeline_BranchIfNoActiveLanes, r) {
    // Allocate space for 4 slots.
    alignas(64) float slots[4 * SkRasterPipeline_kMaxStride_highp] = {};
    const int N = SkOpts::raster_pipeline_highp_stride;

    alignas(64) static constexpr float kColorBlack[4]   = {0.0f, 0.0f, 0.0f, 0.0f};
    alignas(64) static constexpr float kColorRed[4]     = {1.0f, 0.0f, 0.0f, 1.0f};
    alignas(64) static constexpr float kColorBlue[4]    = {0.0f, 0.0f, 1.0f, 1.0f};
    const int offset = 2;

    // An array of all zeros.
    alignas(64) static constexpr int32_t kNoLanesActive[4 * SkRasterPipeline_kMaxStride_highp] = {};

    // An array of all zeros, except for a single ~0 in the first dA slot.
    alignas(64) int32_t oneLaneActive[4 * SkRasterPipeline_kMaxStride_highp] = {};
    oneLaneActive[3*N] = ~0;

    // Make a program which conditionally branches past a append_constant_color op.
    SkArenaAlloc alloc(/*firstHeapAllocation=*/256);
    SkRasterPipeline p(&alloc);
    p.append_constant_color(&alloc, kColorBlack);                    // set the color to black
    p.append(SkRasterPipeline::init_lane_masks);                     // set all lanes active
    p.append(SkRasterPipeline::branch_if_no_active_lanes, &offset);  // do not skip past next line
    p.append_constant_color(&alloc, kColorRed);                      // sets the color to red
    p.append(SkRasterPipeline::load_dst, oneLaneActive);             // set one lane active
    p.append(SkRasterPipeline::branch_if_no_active_lanes, &offset);  // do not skip past next line
    p.append(SkRasterPipeline::swap_rb);                             // swap R and B (making blue)
    p.append(SkRasterPipeline::load_dst, kNoLanesActive);            // make no lanes active
    p.append(SkRasterPipeline::branch_if_no_active_lanes, &offset);  // skip past next line
    p.append_constant_color(&alloc, kColorBlack);                    // (not executed)
    p.append(SkRasterPipeline::store_src, slots);                    // store final blue color
    p.run(0,0,1,1);

    // Verify that the slots contain blue.
    float* destPtr = &slots[0];
    for (int checkSlot = 0; checkSlot < 4; ++checkSlot) {
        for (int checkLane = 0; checkLane < N; ++checkLane) {
            REPORTER_ASSERT(r, *destPtr == kColorBlue[checkSlot]);
            ++destPtr;
        }
    }
}

DEF_TEST(SkRasterPipeline_empty, r) {
    // No asserts... just a test that this is safe to run.
    SkRasterPipeline_<256> p;
    p.run(0,0,20,1);
}

DEF_TEST(SkRasterPipeline_nonsense, r) {
    // No asserts... just a test that this is safe to run and terminates.
    // srcover() calls st->next(); this makes sure we've always got something there to call.
    SkRasterPipeline_<256> p;
    p.append(SkRasterPipeline::srcover);
    p.run(0,0,20,1);
}

DEF_TEST(SkRasterPipeline_JIT, r) {
    // This tests a couple odd corners that a JIT backend can stumble over.

    uint32_t buf[72] = {
         0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
         1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12,
        13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
         0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
         0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
         0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    };

    SkRasterPipeline_MemoryCtx src = { buf +  0, 0 },
                               dst = { buf + 36, 0 };

    // Copy buf[x] to buf[x+36] for x in [15,35).
    SkRasterPipeline_<256> p;
    p.append(SkRasterPipeline:: load_8888, &src);
    p.append(SkRasterPipeline::store_8888, &dst);
    p.run(15,0, 20,1);

    for (int i = 0; i < 36; i++) {
        if (i < 15 || i == 35) {
            REPORTER_ASSERT(r, buf[i+36] == 0);
        } else {
            REPORTER_ASSERT(r, buf[i+36] == (uint32_t)(i - 11));
        }
    }
}

static uint16_t h(float f) {
    // Remember, a float is 1-8-23 (sign-exponent-mantissa) with 127 exponent bias.
    uint32_t sem;
    memcpy(&sem, &f, sizeof(sem));
    uint32_t s  = sem & 0x80000000,
             em = sem ^ s;

    // Convert to 1-5-10 half with 15 bias, flushing denorm halfs (including zero) to zero.
    auto denorm = (int32_t)em < 0x38800000;  // I32 comparison is often quicker, and always safe
    // here.
    return denorm ? SkTo<uint16_t>(0)
                  : SkTo<uint16_t>((s>>16) + (em>>13) - ((127-15)<<10));
}

DEF_TEST(SkRasterPipeline_tail, r) {
    {
        float data[][4] = {
            {00, 01, 02, 03},
            {10, 11, 12, 13},
            {20, 21, 22, 23},
            {30, 31, 32, 33},
        };

        float buffer[4][4];

        SkRasterPipeline_MemoryCtx src = { &data[0][0], 0 },
                           dst = { &buffer[0][0], 0 };

        for (unsigned i = 1; i <= 4; i++) {
            memset(buffer, 0xff, sizeof(buffer));
            SkRasterPipeline_<256> p;
            p.append(SkRasterPipeline::load_f32, &src);
            p.append(SkRasterPipeline::store_f32, &dst);
            p.run(0,0, i,1);
            for (unsigned j = 0; j < i; j++) {
                for (unsigned k = 0; k < 4; k++) {
                    if (buffer[j][k] != data[j][k]) {
                        ERRORF(r, "(%u, %u) - a: %g r: %g\n", j, k, data[j][k], buffer[j][k]);
                    }
                }
            }
            for (int j = i; j < 4; j++) {
                for (auto f : buffer[j]) {
                    REPORTER_ASSERT(r, SkScalarIsNaN(f));
                }
            }
        }
    }

    {
        float data[][2] = {
            {00, 01},
            {10, 11},
            {20, 21},
            {30, 31},
        };

        float buffer[4][4];

        SkRasterPipeline_MemoryCtx src = { &data[0][0], 0 },
                dst = { &buffer[0][0], 0 };

        for (unsigned i = 1; i <= 4; i++) {
            memset(buffer, 0xff, sizeof(buffer));
            SkRasterPipeline_<256> p;
            p.append(SkRasterPipeline::load_rgf32, &src);
            p.append(SkRasterPipeline::store_f32, &dst);
            p.run(0,0, i,1);
            for (unsigned j = 0; j < i; j++) {
                for (unsigned k = 0; k < 2; k++) {
                    if (buffer[j][k] != data[j][k]) {
                        ERRORF(r, "(%u, %u) - a: %g r: %g\n", j, k, data[j][k], buffer[j][k]);
                    }
                }
                if (buffer[j][2] != 0) {
                    ERRORF(r, "(%u, 2) - a: 0 r: %g\n", j, buffer[j][2]);
                }
                if (buffer[j][3] != 1) {
                    ERRORF(r, "(%u, 3) - a: 1 r: %g\n", j, buffer[j][3]);
                }
            }
            for (int j = i; j < 4; j++) {
                for (auto f : buffer[j]) {
                    REPORTER_ASSERT(r, SkScalarIsNaN(f));
                }
            }
        }
    }

    {
        float data[][4] = {
            {00, 01, 02, 03},
            {10, 11, 12, 13},
            {20, 21, 22, 23},
            {30, 31, 32, 33},
        };

        float buffer[4][2];

        SkRasterPipeline_MemoryCtx src = { &data[0][0], 0 },
                dst = { &buffer[0][0], 0 };

        for (unsigned i = 1; i <= 4; i++) {
            memset(buffer, 0xff, sizeof(buffer));
            SkRasterPipeline_<256> p;
            p.append(SkRasterPipeline::load_f32, &src);
            p.append(SkRasterPipeline::store_rgf32, &dst);
            p.run(0,0, i,1);
            for (unsigned j = 0; j < i; j++) {
                for (unsigned k = 0; k < 2; k++) {
                    if (buffer[j][k] != data[j][k]) {
                        ERRORF(r, "(%u, %u) - a: %g r: %g\n", j, k, data[j][k], buffer[j][k]);
                    }
                }
            }
            for (int j = i; j < 4; j++) {
                for (auto f : buffer[j]) {
                    REPORTER_ASSERT(r, SkScalarIsNaN(f));
                }
            }
        }
    }

    {
        alignas(8) uint16_t data[][4] = {
            {h(00), h(01), h(02), h(03)},
            {h(10), h(11), h(12), h(13)},
            {h(20), h(21), h(22), h(23)},
            {h(30), h(31), h(32), h(33)},
        };
        alignas(8) uint16_t buffer[4][4];
        SkRasterPipeline_MemoryCtx src = { &data[0][0], 0 },
                           dst = { &buffer[0][0], 0 };

        for (unsigned i = 1; i <= 4; i++) {
            memset(buffer, 0xff, sizeof(buffer));
            SkRasterPipeline_<256> p;
            p.append(SkRasterPipeline::load_f16, &src);
            p.append(SkRasterPipeline::store_f16, &dst);
            p.run(0,0, i,1);
            for (unsigned j = 0; j < i; j++) {
                for (int k = 0; k < 4; k++) {
                    REPORTER_ASSERT(r, buffer[j][k] == data[j][k]);
                }
            }
            for (int j = i; j < 4; j++) {
                for (auto f : buffer[j]) {
                    REPORTER_ASSERT(r, f == 0xffff);
                }
            }
        }
    }

    {
        alignas(8) uint16_t data[]= {
            h(00),
            h(10),
            h(20),
            h(30),
        };
        alignas(8) uint16_t buffer[4][4];
        SkRasterPipeline_MemoryCtx src = { &data[0], 0 },
                dst = { &buffer[0][0], 0 };

        for (unsigned i = 1; i <= 4; i++) {
            memset(buffer, 0xff, sizeof(buffer));
            SkRasterPipeline_<256> p;
            p.append(SkRasterPipeline::load_af16, &src);
            p.append(SkRasterPipeline::store_f16, &dst);
            p.run(0,0, i,1);
            for (unsigned j = 0; j < i; j++) {
                uint16_t expected[] = {0, 0, 0, data[j]};
                REPORTER_ASSERT(r, !memcmp(expected, &buffer[j][0], sizeof(buffer[j])));
            }
            for (int j = i; j < 4; j++) {
                for (auto f : buffer[j]) {
                    REPORTER_ASSERT(r, f == 0xffff);
                }
            }
        }
    }

    {
        alignas(8) uint16_t data[][4] = {
            {h(00), h(01), h(02), h(03)},
            {h(10), h(11), h(12), h(13)},
            {h(20), h(21), h(22), h(23)},
            {h(30), h(31), h(32), h(33)},
        };
        alignas(8) uint16_t buffer[4];
        SkRasterPipeline_MemoryCtx src = { &data[0][0], 0 },
                dst = { &buffer[0], 0 };

        for (unsigned i = 1; i <= 4; i++) {
            memset(buffer, 0xff, sizeof(buffer));
            SkRasterPipeline_<256> p;
            p.append(SkRasterPipeline::load_f16, &src);
            p.append(SkRasterPipeline::store_af16, &dst);
            p.run(0,0, i,1);
            for (unsigned j = 0; j < i; j++) {
                REPORTER_ASSERT(r, !memcmp(&data[j][3], &buffer[j], sizeof(buffer[j])));
            }
            for (int j = i; j < 4; j++) {
                REPORTER_ASSERT(r, buffer[j] == 0xffff);
            }
        }
    }

    {
        alignas(8) uint16_t data[][4] = {
            {h(00), h(01), h(02), h(03)},
            {h(10), h(11), h(12), h(13)},
            {h(20), h(21), h(22), h(23)},
            {h(30), h(31), h(32), h(33)},
        };
        alignas(8) uint16_t buffer[4][2];
        SkRasterPipeline_MemoryCtx src = { &data[0][0], 0 },
                dst = { &buffer[0][0], 0 };

        for (unsigned i = 1; i <= 4; i++) {
            memset(buffer, 0xff, sizeof(buffer));
            SkRasterPipeline_<256> p;
            p.append(SkRasterPipeline::load_f16, &src);
            p.append(SkRasterPipeline::store_rgf16, &dst);
            p.run(0,0, i,1);
            for (unsigned j = 0; j < i; j++) {
                REPORTER_ASSERT(r, !memcmp(&buffer[j], &data[j], 2 * sizeof(uint16_t)));
            }
            for (int j = i; j < 4; j++) {
                for (auto h : buffer[j]) {
                    REPORTER_ASSERT(r, h == 0xffff);
                }
            }
        }
    }

    {
        alignas(8) uint16_t data[][2] = {
            {h(00), h(01)},
            {h(10), h(11)},
            {h(20), h(21)},
            {h(30), h(31)},
        };
        alignas(8) uint16_t buffer[4][4];
        SkRasterPipeline_MemoryCtx src = { &data[0][0], 0 },
                dst = { &buffer[0][0], 0 };

        for (unsigned i = 1; i <= 4; i++) {
            memset(buffer, 0xff, sizeof(buffer));
            SkRasterPipeline_<256> p;
            p.append(SkRasterPipeline::load_rgf16, &src);
            p.append(SkRasterPipeline::store_f16, &dst);
            p.run(0,0, i,1);
            for (unsigned j = 0; j < i; j++) {
                uint16_t expected[] = {data[j][0], data[j][1], h(0), h(1)};
                REPORTER_ASSERT(r, !memcmp(&buffer[j], expected, sizeof(expected)));
            }
            for (int j = i; j < 4; j++) {
                for (auto h : buffer[j]) {
                    REPORTER_ASSERT(r, h == 0xffff);
                }
            }
        }
    }
}

DEF_TEST(SkRasterPipeline_u16, r) {
    {
        alignas(8) uint16_t data[][2] = {
            {0x0000, 0x0111},
            {0x1010, 0x1111},
            {0x2020, 0x2121},
            {0x3030, 0x3131},
        };
        uint8_t buffer[4][4];
        SkRasterPipeline_MemoryCtx src = { &data[0][0], 0 },
                dst = { &buffer[0][0], 0 };

        for (unsigned i = 1; i <= 4; i++) {
            memset(buffer, 0xab, sizeof(buffer));
            SkRasterPipeline_<256> p;
            p.append(SkRasterPipeline::load_rg1616, &src);
            p.append(SkRasterPipeline::store_8888, &dst);
            p.run(0,0, i,1);
            for (unsigned j = 0; j < i; j++) {
                uint8_t expected[] = {
                    SkToU8(data[j][0] >> 8),
                    SkToU8(data[j][1] >> 8),
                    000,
                    0xff
                };
                REPORTER_ASSERT(r, !memcmp(&buffer[j], expected, sizeof(expected)));
            }
            for (int j = i; j < 4; j++) {
                for (auto b : buffer[j]) {
                    REPORTER_ASSERT(r, b == 0xab);
                }
            }
        }
    }

    {
        alignas(8) uint16_t data[] = {
                0x0000,
                0x1010,
                0x2020,
                0x3030,
        };
        uint8_t buffer[4][4];
        SkRasterPipeline_MemoryCtx src = { &data[0], 0 },
                dst = { &buffer[0][0], 0 };

        for (unsigned i = 1; i <= 4; i++) {
            memset(buffer, 0xff, sizeof(buffer));
            SkRasterPipeline_<256> p;
            p.append(SkRasterPipeline::load_a16, &src);
            p.append(SkRasterPipeline::store_8888, &dst);
            p.run(0,0, i,1);
            for (unsigned j = 0; j < i; j++) {
                uint8_t expected[] = {0x00, 0x00, 0x00, SkToU8(data[j] >> 8)};
                REPORTER_ASSERT(r, !memcmp(&buffer[j], expected, sizeof(expected)));
            }
            for (int j = i; j < 4; j++) {
                for (auto b : buffer[j]) {
                    REPORTER_ASSERT(r, b == 0xff);
                }
            }
        }
    }

    {
        uint8_t data[][4] = {
            {0x00, 0x01, 0x02, 0x03},
            {0x10, 0x11, 0x12, 0x13},
            {0x20, 0x21, 0x22, 0x23},
            {0x30, 0x31, 0x32, 0x33},
        };
        alignas(8) uint16_t buffer[4];
        SkRasterPipeline_MemoryCtx src = { &data[0][0], 0 },
                dst = { &buffer[0], 0 };

        for (unsigned i = 1; i <= 4; i++) {
            memset(buffer, 0xff, sizeof(buffer));
            SkRasterPipeline_<256> p;
            p.append(SkRasterPipeline::load_8888, &src);
            p.append(SkRasterPipeline::store_a16, &dst);
            p.run(0,0, i,1);
            for (unsigned j = 0; j < i; j++) {
                uint16_t expected = (data[j][3] << 8) | data[j][3];
                REPORTER_ASSERT(r, buffer[j] == expected);
            }
            for (int j = i; j < 4; j++) {
                REPORTER_ASSERT(r, buffer[j] == 0xffff);
            }
        }
    }

    {
        alignas(8) uint16_t data[][4] = {
            {0x0000, 0x1000, 0x2000, 0x3000},
            {0x0001, 0x1001, 0x2001, 0x3001},
            {0x0002, 0x1002, 0x2002, 0x3002},
            {0x0003, 0x1003, 0x2003, 0x3003},
        };
        alignas(8) uint16_t buffer[4][4];
        SkRasterPipeline_MemoryCtx src = { &data[0][0], 0 },
                dst = { &buffer[0], 0 };

        for (unsigned i = 1; i <= 4; i++) {
            memset(buffer, 0xff, sizeof(buffer));
            SkRasterPipeline_<256> p;
            p.append(SkRasterPipeline::load_16161616, &src);
            p.append(SkRasterPipeline::swap_rb);
            p.append(SkRasterPipeline::store_16161616, &dst);
            p.run(0,0, i,1);
            for (unsigned j = 0; j < i; j++) {
                uint16_t expected[4] = {data[j][2], data[j][1], data[j][0], data[j][3]};
                REPORTER_ASSERT(r, !memcmp(&expected[0], &buffer[j], sizeof(expected)));
            }
            for (int j = i; j < 4; j++) {
                for (uint16_t u16 : buffer[j])
                REPORTER_ASSERT(r, u16 == 0xffff);
            }
        }
    }
}

DEF_TEST(SkRasterPipeline_lowp, r) {
    uint32_t rgba[64];
    for (int i = 0; i < 64; i++) {
        rgba[i] = (4*i+0) << 0
                | (4*i+1) << 8
                | (4*i+2) << 16
                | (4*i+3) << 24;
    }

    SkRasterPipeline_MemoryCtx ptr = { rgba, 0 };

    SkRasterPipeline_<256> p;
    p.append(SkRasterPipeline::load_8888,  &ptr);
    p.append(SkRasterPipeline::swap_rb);
    p.append(SkRasterPipeline::store_8888, &ptr);
    p.run(0,0,64,1);

    for (int i = 0; i < 64; i++) {
        uint32_t want = (4*i+0) << 16
                      | (4*i+1) << 8
                      | (4*i+2) << 0
                      | (4*i+3) << 24;
        if (rgba[i] != want) {
            ERRORF(r, "got %08x, want %08x\n", rgba[i], want);
        }
    }
}

DEF_TEST(SkRasterPipeline_swizzle, r) {
    // This takes the lowp code path
    {
        uint16_t rg[64];
        for (int i = 0; i < 64; i++) {
            rg[i] = (4*i+0) << 0
                  | (4*i+1) << 8;
        }

        skgpu::Swizzle swizzle("g1b1");

        SkRasterPipeline_MemoryCtx ptr = { rg, 0 };
        SkRasterPipeline_<256> p;
        p.append(SkRasterPipeline::load_rg88,  &ptr);
        swizzle.apply(&p);
        p.append(SkRasterPipeline::store_rg88, &ptr);
        p.run(0,0,64,1);

        for (int i = 0; i < 64; i++) {
            uint32_t want = 0xff    << 8
                          | (4*i+1) << 0;
            if (rg[i] != want) {
                ERRORF(r, "got %08x, want %08x\n", rg[i], want);
            }
        }
    }
    // This takes the highp code path
    {
        float rg[64][2];
        for (int i = 0; i < 64; i++) {
            rg[i][0] = i + 1;
            rg[i][1] = 2 * i + 1;
        }

        skgpu::Swizzle swizzle("0gra");

        uint16_t buffer[64][4];
        SkRasterPipeline_MemoryCtx src = { rg,     0 },
                                   dst = { buffer, 0};
        SkRasterPipeline_<256> p;
        p.append(SkRasterPipeline::load_rgf32,  &src);
        swizzle.apply(&p);
        p.append(SkRasterPipeline::store_f16, &dst);
        p.run(0,0,64,1);

        for (int i = 0; i < 64; i++) {
            uint16_t want[4] {
                h(0),
                h(2 * i + 1),
                h(i + 1),
                h(1),
            };
            REPORTER_ASSERT(r, !memcmp(want, buffer[i], sizeof(buffer[i])));
        }
    }
}

DEF_TEST(SkRasterPipeline_lowp_clamp01, r) {
    // This may seem like a funny pipeline to create,
    // but it certainly shouldn't crash when you run it.

    uint32_t rgba = 0xff00ff00;

    SkRasterPipeline_MemoryCtx ptr = { &rgba, 0 };

    SkRasterPipeline_<256> p;
    p.append(SkRasterPipeline::load_8888,  &ptr);
    p.append(SkRasterPipeline::swap_rb);
    p.append(SkRasterPipeline::clamp_01);
    p.append(SkRasterPipeline::store_8888, &ptr);
    p.run(0,0,1,1);
}

// Helper struct that can be used to scrape stack addresses at different points in a pipeline
class StackCheckerCtx : SkRasterPipeline_CallbackCtx {
public:
    StackCheckerCtx() {
        this->fn = [](SkRasterPipeline_CallbackCtx* self, int active_pixels) {
            auto ctx = (StackCheckerCtx*)self;
            ctx->fStackAddrs.push_back(&active_pixels);
        };
    }

    enum class Behavior {
        kGrowth,
        kBaseline,
        kUnknown,
    };

    static Behavior GrowthBehavior() {
        // Only some stages use the musttail attribute, so we have no way of knowing what's going to
        // happen. In release builds, it's likely that the compiler will apply tail-call
        // optimization. Even in some debug builds (on Windows), we don't see stack growth.
        return Behavior::kUnknown;
    }

    // Call one of these two each time the checker callback is added:
    StackCheckerCtx* expectGrowth() {
        fExpectedBehavior.push_back(GrowthBehavior());
        return this;
    }

    StackCheckerCtx* expectBaseline() {
        fExpectedBehavior.push_back(Behavior::kBaseline);
        return this;
    }

    void validate(skiatest::Reporter* r) {
        REPORTER_ASSERT(r, fStackAddrs.size() == fExpectedBehavior.size());

        // This test is storing and comparing stack pointers (to dead stack frames) as a way of
        // measuring stack usage. Unsurprisingly, ASAN doesn't like that. HWASAN actually inserts
        // tag bytes in the pointers, causing them not to match. Newer versions of vanilla ASAN
        // also appear to salt the stack slightly, causing repeated calls to scrape different
        // addresses, even though $rsp is identical on each invocation of the lambda.
#if !defined(SK_SANITIZE_ADDRESS)
        void* baseline = fStackAddrs[0];
        for (size_t i = 1; i < fStackAddrs.size(); i++) {
            if (fExpectedBehavior[i] == Behavior::kGrowth) {
                REPORTER_ASSERT(r, fStackAddrs[i] != baseline);
            } else if (fExpectedBehavior[i] == Behavior::kBaseline) {
                REPORTER_ASSERT(r, fStackAddrs[i] == baseline);
            } else {
                // Unknown behavior, nothing we can assert here
            }
        }
#endif
    }

private:
    std::vector<void*>    fStackAddrs;
    std::vector<Behavior> fExpectedBehavior;
};

DEF_TEST(SkRasterPipeline_stack_rewind, r) {
    // This test verifies that we can control stack usage with stack_rewind

    // Without stack_rewind, we should (maybe) see stack growth
    {
        StackCheckerCtx stack;
        uint32_t rgba = 0xff0000ff;
        SkRasterPipeline_MemoryCtx ptr = { &rgba, 0 };

        SkRasterPipeline_<256> p;
        p.append(SkRasterPipeline::callback, stack.expectBaseline());
        p.append(SkRasterPipeline::load_8888,  &ptr);
        p.append(SkRasterPipeline::callback, stack.expectGrowth());
        p.append(SkRasterPipeline::swap_rb);
        p.append(SkRasterPipeline::callback, stack.expectGrowth());
        p.append(SkRasterPipeline::store_8888, &ptr);
        p.run(0,0,1,1);

        REPORTER_ASSERT(r, rgba == 0xffff0000); // Ensure the pipeline worked
        stack.validate(r);
    }

    // With stack_rewind, we should (always) be able to get back to baseline
    {
        StackCheckerCtx stack;
        uint32_t rgba = 0xff0000ff;
        SkRasterPipeline_MemoryCtx ptr = { &rgba, 0 };

        SkRasterPipeline_<256> p;
        p.append(SkRasterPipeline::callback, stack.expectBaseline());
        p.append(SkRasterPipeline::load_8888,  &ptr);
        p.append(SkRasterPipeline::callback, stack.expectGrowth());
        p.append_stack_rewind();
        p.append(SkRasterPipeline::callback, stack.expectBaseline());
        p.append(SkRasterPipeline::swap_rb);
        p.append(SkRasterPipeline::callback, stack.expectGrowth());
        p.append_stack_rewind();
        p.append(SkRasterPipeline::callback, stack.expectBaseline());
        p.append(SkRasterPipeline::store_8888, &ptr);
        p.run(0,0,1,1);

        REPORTER_ASSERT(r, rgba == 0xffff0000); // Ensure the pipeline worked
        stack.validate(r);
    }
}
