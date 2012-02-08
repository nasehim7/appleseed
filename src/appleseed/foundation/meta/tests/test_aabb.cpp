
//
// This source file is part of appleseed.
// Visit http://appleseedhq.net/ for additional information and resources.
//
// This software is released under the MIT license.
//
// Copyright (c) 2010-2012 Francois Beaune, Jupiter Jazz
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

// appleseed.foundation headers.
#include "foundation/math/aabb.h"
#include "foundation/math/vector.h"
#include "foundation/utility/iostreamop.h"
#include "foundation/utility/test.h"

// Imath headers.
#ifdef APPLESEED_ENABLE_IMATH_INTEROP
#include "openexr/ImathBox.h"
#endif

// Standard headers.
#include <cstddef>

TEST_SUITE(Foundation_Math_AABB)
{
    using namespace foundation;

    TEST_CASE(ConstructWithMinMax)
    {
        const AABB3d bbox(
            Vector3d(1.0, 2.0, 3.0),
            Vector3d(4.0, 5.0, 6.0));

        EXPECT_EQ(Vector3d(1.0, 2.0, 3.0), bbox.min);
        EXPECT_EQ(Vector3d(4.0, 5.0, 6.0), bbox.max);
    }

    TEST_CASE(ConstructByTypeConversion)
    {
        const AABB3d bbox(
            Vector3d(1.0, 2.0, 3.0),
            Vector3d(4.0, 5.0, 6.0));

        const AABB3f bboxf(bbox);

        EXPECT_FEQ(Vector3f(1.0f, 2.0f, 3.0f), bboxf.min);
        EXPECT_FEQ(Vector3f(4.0f, 5.0f, 6.0f), bboxf.max);
    }

#ifdef APPLESEED_ENABLE_IMATH_INTEROP

    TEST_CASE(ConstructFromImathBox)
    {
        const Imath::Box2d source(
            Imath::V2d(1.0, 2.0),
            Imath::V2d(3.0, 4.0));

        const AABB2d copy(source);

        EXPECT_EQ(Vector2d(1.0, 2.0), copy.min);
        EXPECT_EQ(Vector2d(3.0, 4.0), copy.max);
    }

    TEST_CASE(ConvertToImathBox)
    {
        const AABB2d source(
            Vector2d(1.0, 2.0),
            Vector2d(3.0, 4.0));

        const Imath::Box2d copy(source);

        EXPECT_EQ(Imath::V2d(1.0, 2.0), copy.min);
        EXPECT_EQ(Imath::V2d(3.0, 4.0), copy.max);
    }

#endif

    TEST_CASE(ConstructInvalidAABB)
    {
        const AABB3d bbox(AABB3d::invalid());

        EXPECT_FALSE(bbox.is_valid());
    }

    TEST_CASE(TestArraySubscripting)
    {
        const AABB3d bbox(
            Vector3d(1.0, 2.0, 3.0),
            Vector3d(4.0, 5.0, 6.0));

        EXPECT_EQ(Vector3d(1.0, 2.0, 3.0), bbox[0]);
        EXPECT_EQ(Vector3d(4.0, 5.0, 6.0), bbox[1]);
    }

    TEST_CASE(TestInvalidate)
    {
        AABB3d bbox(
            Vector3d(1.0, 2.0, 3.0),
            Vector3d(4.0, 5.0, 6.0));

        bbox.invalidate();

        EXPECT_FALSE(bbox.is_valid());
    }

    TEST_CASE(VerifyThatRank0AABBOverlapsWithItself)
    {
        const AABB3d bbox(
            Vector3d(1.0, 2.0, 3.0),
            Vector3d(1.0, 2.0, 3.0));

        EXPECT_TRUE(AABB3d::overlap(bbox, bbox));
    }

    TEST_CASE(VerifyThatRank3AABBOverlapsWithItself)
    {
        const AABB3d bbox(
            Vector3d(1.0, 2.0, 3.0),
            Vector3d(4.0, 5.0, 6.0));

        EXPECT_TRUE(AABB3d::overlap(bbox, bbox));
    }

    TEST_CASE(TestOverlapWithOverlappingRank3AABB)
    {
        const AABB3d bbox1(
            Vector3d(1.0, 2.0, 3.0),
            Vector3d(4.0, 5.0, 6.0));

        const AABB3d bbox2(
            Vector3d(0.0, 1.0, 5.0),
            Vector3d(5.0, 3.0, 7.0));

        EXPECT_TRUE(AABB3d::overlap(bbox1, bbox2));
        EXPECT_TRUE(AABB3d::overlap(bbox2, bbox1));
    }

    TEST_CASE(TestOverlapWithNonOverlappingRank3AABB)
    {
        const AABB3d bbox1(
            Vector3d(1.0, 2.0, 3.0),
            Vector3d(4.0, 5.0, 6.0));

        const AABB3d bbox2(
            Vector3d(0.0, 1.0, 5.0),
            Vector3d(5.0, 3.0, 7.0));

        EXPECT_TRUE(AABB3d::overlap(bbox1, bbox2));
        EXPECT_TRUE(AABB3d::overlap(bbox2, bbox1));
    }

    TEST_CASE(TestOverlapRatio)
    {
        EXPECT_FEQ(0.0,
            AABB2d::overlap_ratio(
                AABB2d(Vector2d(0.0, 0.0), Vector2d(1.0, 1.0)),
                AABB2d(Vector2d(2.0, 0.0), Vector2d(3.0, 1.0))));

        EXPECT_FEQ(0.0,
            AABB2d::overlap_ratio(
                AABB2d(Vector2d(2.0, 0.0), Vector2d(3.0, 1.0)),
                AABB2d(Vector2d(0.0, 0.0), Vector2d(1.0, 1.0))));

        EXPECT_FEQ(1.0,
            AABB2d::overlap_ratio(
                AABB2d(Vector2d(1.0, 1.0), Vector2d(2.0, 2.0)),
                AABB2d(Vector2d(1.0, 1.0), Vector2d(2.0, 2.0))));

        EXPECT_FEQ(1.0,
            AABB2d::overlap_ratio(
                AABB2d(Vector2d(1.0, 1.0), Vector2d(2.0, 2.0)),
                AABB2d(Vector2d(0.0, 0.0), Vector2d(3.0, 3.0))));

        EXPECT_FEQ(1.0,
            AABB2d::overlap_ratio(
                AABB2d(Vector2d(0.0, 0.0), Vector2d(3.0, 3.0)),
                AABB2d(Vector2d(1.0, 1.0), Vector2d(2.0, 2.0))));

        EXPECT_FEQ(0.5,
            AABB2d::overlap_ratio(
                AABB2d(Vector2d(0.0, 0.0), Vector2d(2.0, 2.0)),
                AABB2d(Vector2d(1.0, 0.0), Vector2d(3.0, 2.0))));

        EXPECT_FEQ(0.5,
            AABB2d::overlap_ratio(
                AABB2d(Vector2d(1.0, 0.0), Vector2d(3.0, 2.0)),
                AABB2d(Vector2d(0.0, 0.0), Vector2d(2.0, 2.0))));

        EXPECT_FEQ(0.25,
            AABB2d::overlap_ratio(
                AABB2d(Vector2d(0.0, 0.0), Vector2d(2.0, 2.0)),
                AABB2d(Vector2d(1.0, 1.0), Vector2d(3.0, 3.0))));
    }

    TEST_CASE(TestExtentRatio)
    {
        EXPECT_FEQ(1.0,
            AABB3d::extent_ratio(
                AABB3d(Vector3d(0.0), Vector3d(0.0, 0.0, 0.0)),
                AABB3d(Vector3d(0.0), Vector3d(0.0, 0.0, 0.0))));

        EXPECT_FEQ(1.0,
            AABB3d::extent_ratio(
                AABB3d(Vector3d(0.0), Vector3d(1.0, 1.0, 1.0)),
                AABB3d(Vector3d(0.0), Vector3d(1.0, 1.0, 1.0))));

        EXPECT_FEQ(1.0,
            AABB3d::extent_ratio(
                AABB3d(Vector3d(0.0), Vector3d(0.0, 1.0, 1.0)),
                AABB3d(Vector3d(0.0), Vector3d(0.0, 1.0, 1.0))));

        EXPECT_FEQ(1.0,
            AABB3d::extent_ratio(
                AABB3d(Vector3d(0.0), Vector3d(1.0, 0.0, 1.0)),
                AABB3d(Vector3d(0.0), Vector3d(1.0, 0.0, 1.0))));

        EXPECT_FEQ(1.0,
            AABB3d::extent_ratio(
                AABB3d(Vector3d(0.0), Vector3d(1.0, 1.0, 0.0)),
                AABB3d(Vector3d(0.0), Vector3d(1.0, 1.0, 0.0))));

        EXPECT_FEQ(2.0,
            AABB3d::extent_ratio(
                AABB3d(Vector3d(0.0), Vector3d(2.0, 1.0, 1.0)),
                AABB3d(Vector3d(0.0), Vector3d(1.0, 1.0, 1.0))));

        EXPECT_FEQ(8.0,
            AABB3d::extent_ratio(
                AABB3d(Vector3d(0.0), Vector3d(2.0, 2.0, 2.0)),
                AABB3d(Vector3d(0.0), Vector3d(1.0, 1.0, 1.0))));
    }

    TEST_CASE(TestInsertPointIntoInvalidAABB)
    {
        AABB3d bbox(AABB3d::invalid());

        bbox.insert(Vector3d(1.0, 2.0, 3.0));

        EXPECT_EQ(Vector3d(1.0, 2.0, 3.0), bbox.min);
        EXPECT_EQ(Vector3d(1.0, 2.0, 3.0), bbox.max);
    }

    TEST_CASE(TestInsertPointIntoValidAABB)
    {
        AABB3d bbox(
            Vector3d(1.0, 2.0, 3.0),
            Vector3d(4.0, 5.0, 6.0));

        bbox.insert(Vector3d(-1.0, 50.0, 60.0));

        EXPECT_EQ(Vector3d(-1.0, 2.0, 3.0), bbox.min);
        EXPECT_EQ(Vector3d(4.0, 50.0, 60.0), bbox.max);
    }

    TEST_CASE(TestInsertAABBIntoInvalidAABB)
    {
        AABB3d bbox(AABB3d::invalid());

        bbox.insert(
            AABB3d(
                Vector3d(1.0, 2.0, 3.0),
                Vector3d(4.0, 5.0, 6.0)));

        EXPECT_EQ(Vector3d(1.0, 2.0, 3.0), bbox.min);
        EXPECT_EQ(Vector3d(4.0, 5.0, 6.0), bbox.max);
    }

    TEST_CASE(TestInsertAABBIntoValidAABB)
    {
        AABB3d bbox(
            Vector3d(1.0, 2.0, 3.0),
            Vector3d(4.0, 5.0, 6.0));

        bbox.insert(
            AABB3d(
                Vector3d(7.0, 0.0, 2.0),
                Vector3d(8.0, 3.0, 9.0)));

        EXPECT_EQ(Vector3d(1.0, 0.0, 2.0), bbox.min);
        EXPECT_EQ(Vector3d(8.0, 5.0, 9.0), bbox.max);
    }

    TEST_CASE(TestGrow)
    {
        AABB3d bbox(
            Vector3d(1.0, 2.0, 3.0),
            Vector3d(4.0, 5.0, 6.0));

        bbox.grow(Vector3d(2.0, 0.0, -1.0));

        EXPECT_FEQ(Vector3d(-1.0, 2.0, 4.0), bbox.min);
        EXPECT_FEQ(Vector3d(6.0, 5.0, 5.0), bbox.max);
    }

    TEST_CASE(TestRobustGrow)
    {
        const Vector3d a(1.0, 2.0, 3.0);
        const Vector3d b(4.0, 5.0, 6.0);

        AABB3d bbox(a, b);

        bbox.robust_grow(1.0);

        EXPECT_LT(a[0], bbox.min[0]);
        EXPECT_LT(a[1], bbox.min[1]);
        EXPECT_LT(a[2], bbox.min[2]);

        EXPECT_GT(b[0], bbox.max[0]);
        EXPECT_GT(b[1], bbox.max[1]);
        EXPECT_GT(b[2], bbox.max[2]);
    }

    TEST_CASE(TestIsValid)
    {
        const AABB3d bbox(
            Vector3d(1.0, 2.0, 3.0),
            Vector3d(4.0, 5.0, 6.0));

        EXPECT_TRUE(bbox.is_valid());

        EXPECT_FALSE(AABB3d::invalid().is_valid());
    }

    TEST_CASE(TestRank1)
    {
        const AABB3d bbox(
            Vector3d(1.0, 2.0, 3.0),
            Vector3d(1.0, 2.0, 3.0));

        EXPECT_EQ(0, bbox.rank());
    }

    TEST_CASE(TestRank2)
    {
        const AABB3d bbox(
            Vector3d(1.0, 2.0, 3.0),
            Vector3d(10.0, 20.0, 30.0));

        EXPECT_EQ(3, bbox.rank());
    }

    TEST_CASE(TestCenter)
    {
        const AABB3d bbox(
            Vector3d(1.0, 2.0, 3.0),
            Vector3d(5.0, 6.0, 7.0));

        EXPECT_FEQ(Vector3d(3.0, 4.0, 5.0), bbox.center());
    }

    TEST_CASE(TestExtent)
    {
        const AABB3d bbox(
            Vector3d(-1.0, -2.0, -3.0),
            Vector3d(4.0, 5.0, 6.0));

        EXPECT_FEQ(Vector3d(5.0, 7.0, 9.0), bbox.extent());
    }

    TEST_CASE(TestVolume)
    {
        const AABB3d bbox(
            Vector3d(-1.0, -2.0, -3.0),
            Vector3d(4.0, 5.0, 6.0));

        EXPECT_FEQ(5.0 * 7.0 * 9.0, bbox.volume());
    }

    TEST_CASE(TestHalfSurfaceArea)
    {
        const AABB3d bbox(
            Vector3d(-1.0, -2.0, -3.0),
            Vector3d(4.0, 5.0, 6.0));

        EXPECT_FEQ(5.0 * 7.0 + 5.0 * 9.0 + 7.0 * 9.0, bbox.half_surface_area()); 
    }

    TEST_CASE(TestSurfaceArea)
    {
        const AABB3d bbox(
            Vector3d(-1.0, -2.0, -3.0),
            Vector3d(4.0, 5.0, 6.0));

        EXPECT_FEQ(2.0 * (5.0 * 7.0 + 5.0 * 9.0 + 7.0 * 9.0), bbox.surface_area()); 
    }

    TEST_CASE(TestComputeCorners)
    {
        const AABB3d bbox(
            Vector3d(-1.0, -2.0, -3.0),
            Vector3d(4.0, 5.0, 6.0));

        const Vector3d Sentinel(12.34, 56.78, 90.12);

        Vector3d corners[9];
        corners[8] = Sentinel;

        bbox.compute_corners(corners);

        EXPECT_EQ(Vector3d(-1.0, -2.0, -3.0), corners[0]);
        EXPECT_EQ(Vector3d( 4.0, -2.0, -3.0), corners[1]);
        EXPECT_EQ(Vector3d(-1.0,  5.0, -3.0), corners[2]);
        EXPECT_EQ(Vector3d( 4.0,  5.0, -3.0), corners[3]);
        EXPECT_EQ(Vector3d(-1.0, -2.0,  6.0), corners[4]);
        EXPECT_EQ(Vector3d( 4.0, -2.0,  6.0), corners[5]);
        EXPECT_EQ(Vector3d(-1.0,  5.0,  6.0), corners[6]);
        EXPECT_EQ(Vector3d( 4.0,  5.0,  6.0), corners[7]);

        EXPECT_EQ(Sentinel, corners[8]);
    }

    TEST_CASE(TestContainsOnRank0AABB)
    {
        const AABB3d bbox(
            Vector3d(1.0, 2.0, 3.0),
            Vector3d(1.0, 2.0, 3.0));

        EXPECT_TRUE(bbox.contains(Vector3d(1.0, 2.0, 3.0)));
        EXPECT_FALSE(bbox.contains(Vector3d(1.0, 1.0, 3.0)));
    }

    TEST_CASE(TestContainsOnRank3AABB)
    {
        const AABB3d bbox(
            Vector3d(1.0, 2.0, 3.0),
            Vector3d(4.0, 5.0, 6.0));

        EXPECT_TRUE(bbox.contains(Vector3d(2.0, 3.0, 4.0)));
        EXPECT_FALSE(bbox.contains(Vector3d(2.0, 6.0, 4.0)));
    }

    TEST_CASE(TestEquality)
    {
        const AABB3d bbox1(
            Vector3d(1.0, 2.0, 3.0),
            Vector3d(4.0, 5.0, 6.0));

        const AABB3d bbox2(
            Vector3d(1.0, 2.0, 3.0),
            Vector3d(4.0, 5.0, 6.0));

        const AABB3d bbox3(
            Vector3d(0.0, 2.0, 3.0),
            Vector3d(4.0, 5.0, 6.0));

        EXPECT_TRUE(bbox1 == bbox2);
        EXPECT_FALSE(bbox1 == bbox3);
    }

    TEST_CASE(TestInequality)
    {
        const AABB3d bbox1(
            Vector3d(1.0, 2.0, 3.0),
            Vector3d(4.0, 5.0, 6.0));

        const AABB3d bbox2(
            Vector3d(1.0, 2.0, 3.0),
            Vector3d(4.0, 5.0, 6.0));

        const AABB3d bbox3(
            Vector3d(0.0, 2.0, 3.0),
            Vector3d(4.0, 5.0, 6.0));

        EXPECT_FALSE(bbox1 != bbox2);
        EXPECT_TRUE(bbox1 != bbox3);
    }
}
