// Copyright (c) 2007, 2008 libmv authors.
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.

#include "libmv/image/image.h"
#include "libmv/correspondence/klt.h"
#include "testing/testing.h"

using libmv::KltContext;
using libmv::FloatImage;
using libmv::ByteImage;

namespace {

TEST(KltContext, ComputeGradientMatrix) {
  FloatImage image(200, 100);
  KltContext klt;
  FloatImage gxx, gxy, gyy;
  klt.ComputeGradientMatrix(image, &gxx, &gxy, &gyy);
}

TEST(KltContext, DetectGoodFeatures) {
  FloatImage image(200, 100);
  KltContext klt;
  std::vector<KltContext::DetectedFeature> features;
  klt.DetectGoodFeatures(image, &features);
}

TEST(KltContext, DetectGoodFeaturesLenna) {
  ByteImage byte_image;
  FloatImage float_image;
  EXPECT_NE(0, ReadPgm("src/libmv/correspondence/klt_test/Lenna.pgm",
                       &byte_image));
  ConvertByteImageToFloatImage(byte_image, &float_image);

  KltContext klt;
  std::vector<KltContext::DetectedFeature> features;
  klt.DetectGoodFeatures(float_image, &features);
}

}  // namespace
