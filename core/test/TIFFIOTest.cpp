#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <random>

#include <opencv2/core.hpp>

#include "vc/core/io/TIFFIO.hpp"
#include "vc/core/types/Exceptions.hpp"

using namespace volcart;
using namespace volcart::tiffio;
namespace fs = volcart::filesystem;

namespace
{
template <
    typename Tp,
    int Cn = 1,
    std::enable_if_t<std::is_integral_v<Tp>, bool> = true>
void FillRandom(
    cv::Mat& mat,
    Tp low = std::numeric_limits<Tp>::min(),
    Tp high = std::numeric_limits<Tp>::max())
{
    using PixelT = cv::Vec<Tp, Cn>;
    static std::random_device device;
    static std::uniform_int_distribution<Tp> dist(low, high);
    static std::default_random_engine gen(device());

    std::generate(mat.begin<PixelT>(), mat.end<PixelT>(), []() {
        PixelT pixel;
        for (int i = 0; i < Cn; i++) {
            pixel[i] = dist(gen);
        }
        return pixel;
    });
}

template <
    typename Tp,
    int Cn = 1,
    std::enable_if_t<std::is_floating_point_v<Tp>, bool> = true>
void FillRandom(cv::Mat& mat, Tp low = 0, Tp high = 1)
{
    using PixelT = cv::Vec<Tp, Cn>;
    static std::random_device device;
    static std::uniform_real_distribution<Tp> dist(low, high);
    static std::default_random_engine gen(device());

    std::generate(mat.begin<PixelT>(), mat.end<PixelT>(), []() {
        PixelT pixel;
        for (int i = 0; i < Cn; i++) {
            pixel[i] = dist(gen);
        }
        return pixel;
    });
}

const cv::Size TEST_IMG_SIZE(10, 10);
}  // namespace

TEST(TIFFIO, WriteRead8UC1)
{
    using ElemT = std::uint8_t;
    using PixelT = ElemT;
    auto cvType = CV_8UC1;

    cv::Mat img(::TEST_IMG_SIZE, cvType);
    ::FillRandom<ElemT>(img);

    const fs::path imgPath(
        "vc_core_TIFFIO_WriteRead_" + cv::typeToString(cvType) + ".tif");
    WriteTIFF(imgPath, img);
    auto result = ReadTIFF(imgPath);

    EXPECT_EQ(result.size, img.size);
    EXPECT_EQ(result.type(), img.type());

    auto equal = std::equal(
        result.begin<PixelT>(), result.end<PixelT>(), img.begin<PixelT>());
    EXPECT_TRUE(equal);
}

TEST(TIFFIO, WriteRead8UC2)
{
    using ElemT = std::uint8_t;
    using PixelT = cv::Vec<ElemT, 2>;
    auto cvType = CV_8UC2;

    cv::Mat img(::TEST_IMG_SIZE, cvType);
    ::FillRandom<ElemT, 2>(img);

    const fs::path imgPath(
        "vc_core_TIFFIO_WriteRead_" + cv::typeToString(cvType) + ".tif");
    WriteTIFF(imgPath, img);
    auto result = ReadTIFF(imgPath);

    EXPECT_EQ(result.size, img.size);
    EXPECT_EQ(result.type(), img.type());

    auto equal = std::equal(
        result.begin<PixelT>(), result.end<PixelT>(), img.begin<PixelT>());
    EXPECT_TRUE(equal);
}

TEST(TIFFIO, WriteRead8UC3)
{
    using ElemT = std::uint8_t;
    using PixelT = cv::Vec<ElemT, 3>;
    auto cvType = CV_8UC3;

    cv::Mat img(::TEST_IMG_SIZE, cvType);
    ::FillRandom<ElemT, 3>(img);

    const fs::path imgPath(
        "vc_core_TIFFIO_WriteRead_" + cv::typeToString(cvType) + ".tif");
    WriteTIFF(imgPath, img);
    auto result = ReadTIFF(imgPath);

    EXPECT_EQ(result.size, img.size);
    EXPECT_EQ(result.type(), img.type());

    auto equal = std::equal(
        result.begin<PixelT>(), result.end<PixelT>(), img.begin<PixelT>());
    EXPECT_TRUE(equal);
}

TEST(TIFFIO, WriteRead8UC4)
{
    using ElemT = std::uint8_t;
    using PixelT = cv::Vec<ElemT, 4>;
    auto cvType = CV_8UC4;

    cv::Mat img(::TEST_IMG_SIZE, cvType);
    ::FillRandom<ElemT, 4>(img);

    const fs::path imgPath(
        "vc_core_TIFFIO_WriteRead_" + cv::typeToString(cvType) + ".tif");
    WriteTIFF(imgPath, img);
    auto result = ReadTIFF(imgPath);

    EXPECT_EQ(result.size, img.size);
    EXPECT_EQ(result.type(), img.type());

    auto equal = std::equal(
        result.begin<PixelT>(), result.end<PixelT>(), img.begin<PixelT>());
    EXPECT_TRUE(equal);
}

TEST(TIFFIO, WriteRead8SC1)
{
    using ElemT = std::int8_t;
    using PixelT = ElemT;
    auto cvType = CV_8SC1;

    cv::Mat img(::TEST_IMG_SIZE, cvType);
    ::FillRandom<ElemT, 1>(img);

    const fs::path imgPath(
        "vc_core_TIFFIO_WriteRead_" + cv::typeToString(cvType) + ".tif");
    WriteTIFF(imgPath, img);
    auto result = ReadTIFF(imgPath);

    EXPECT_EQ(result.size, img.size);
    EXPECT_EQ(result.type(), img.type());

    auto equal = std::equal(
        result.begin<PixelT>(), result.end<PixelT>(), img.begin<PixelT>());
    EXPECT_TRUE(equal);
}

TEST(TIFFIO, WriteRead8SC2)
{
    using ElemT = std::int8_t;
    using PixelT = cv::Vec<ElemT, 2>;
    auto cvType = CV_8SC2;

    cv::Mat img(::TEST_IMG_SIZE, cvType);
    ::FillRandom<ElemT, 2>(img);

    const fs::path imgPath(
        "vc_core_TIFFIO_WriteRead_" + cv::typeToString(cvType) + ".tif");
    WriteTIFF(imgPath, img);
    auto result = ReadTIFF(imgPath);

    EXPECT_EQ(result.size, img.size);
    EXPECT_EQ(result.type(), img.type());

    auto equal = std::equal(
        result.begin<PixelT>(), result.end<PixelT>(), img.begin<PixelT>());
    EXPECT_TRUE(equal);
}

TEST(TIFFIO, Write8SC3)
{
    using ElemT = std::int8_t;
    auto cvType = CV_8SC3;

    cv::Mat img(::TEST_IMG_SIZE, cvType);
    ::FillRandom<ElemT, 3>(img);

    const fs::path imgPath(
        "vc_core_TIFFIO_Write_" + cv::typeToString(cvType) + ".tif");
    EXPECT_THROW(WriteTIFF(imgPath, img), IOException);
}

TEST(TIFFIO, Write8SC4)
{
    using ElemT = std::int8_t;
    auto cvType = CV_8SC4;

    cv::Mat img(::TEST_IMG_SIZE, cvType);
    ::FillRandom<ElemT, 4>(img);

    const fs::path imgPath(
        "vc_core_TIFFIO_Write_" + cv::typeToString(cvType) + ".tif");
    EXPECT_THROW(WriteTIFF(imgPath, img), IOException);
}

TEST(TIFFIO, WriteRead16UC1)
{
    using ElemT = std::uint16_t;
    using PixelT = ElemT;
    auto cvType = CV_16UC1;

    cv::Mat img(::TEST_IMG_SIZE, cvType);
    ::FillRandom<ElemT, 1>(img);

    const fs::path imgPath(
        "vc_core_TIFFIO_WriteRead_" + cv::typeToString(cvType) + ".tif");
    WriteTIFF(imgPath, img);
    auto result = ReadTIFF(imgPath);

    EXPECT_EQ(result.size, img.size);
    EXPECT_EQ(result.type(), img.type());

    auto equal = std::equal(
        result.begin<PixelT>(), result.end<PixelT>(), img.begin<PixelT>());
    EXPECT_TRUE(equal);
}

TEST(TIFFIO, WriteRead16UC1_mmap)
{
    using ElemT = std::uint16_t;
    using PixelT = ElemT;
    auto cvType = CV_16UC1;

    cv::Mat img(::TEST_IMG_SIZE, cvType);
    ::FillRandom<ElemT, 1>(img);

    const fs::path imgPath(
        "vc_core_TIFFIO_WriteRead_" + cv::typeToString(cvType) + "_mmap.tif");
    // Write uncompressed, so we can mmap() it in during reading
    WriteTIFF(imgPath, img, Compression::NONE);
    auto result = ReadTIFF(imgPath);

    EXPECT_EQ(result.size, img.size);
    EXPECT_EQ(result.type(), img.type());

    auto equal = std::equal(
        result.begin<PixelT>(), result.end<PixelT>(), img.begin<PixelT>());
    EXPECT_TRUE(equal);
}

TEST(TIFFIO, WriteRead16UC2)
{
    using ElemT = std::uint16_t;
    using PixelT = cv::Vec<ElemT, 2>;
    auto cvType = CV_16UC2;

    cv::Mat img(::TEST_IMG_SIZE, cvType);
    ::FillRandom<ElemT, 2>(img);

    const fs::path imgPath(
        "vc_core_TIFFIO_WriteRead_" + cv::typeToString(cvType) + ".tif");
    WriteTIFF(imgPath, img);
    auto result = ReadTIFF(imgPath);

    EXPECT_EQ(result.size, img.size);
    EXPECT_EQ(result.type(), img.type());

    auto equal = std::equal(
        result.begin<PixelT>(), result.end<PixelT>(), img.begin<PixelT>());
    EXPECT_TRUE(equal);
}

TEST(TIFFIO, WriteRead16UC3)
{
    using ElemT = std::uint16_t;
    using PixelT = cv::Vec<ElemT, 3>;
    auto cvType = CV_16UC3;

    cv::Mat img(::TEST_IMG_SIZE, cvType);
    ::FillRandom<ElemT, 3>(img);

    const fs::path imgPath(
        "vc_core_TIFFIO_WriteRead_" + cv::typeToString(cvType) + ".tif");
    WriteTIFF(imgPath, img);
    auto result = ReadTIFF(imgPath);

    EXPECT_EQ(result.size, img.size);
    EXPECT_EQ(result.type(), img.type());

    auto equal = std::equal(
        result.begin<PixelT>(), result.end<PixelT>(), img.begin<PixelT>());
    EXPECT_TRUE(equal);
}

TEST(TIFFIO, WriteRead16UC4)
{
    using ElemT = std::uint16_t;
    using PixelT = cv::Vec<ElemT, 4>;
    auto cvType = CV_16UC4;

    cv::Mat img(::TEST_IMG_SIZE, cvType);
    ::FillRandom<ElemT, 4>(img);

    const fs::path imgPath(
        "vc_core_TIFFIO_WriteRead_" + cv::typeToString(cvType) + ".tif");
    WriteTIFF(imgPath, img);
    auto result = ReadTIFF(imgPath);

    EXPECT_EQ(result.size, img.size);
    EXPECT_EQ(result.type(), img.type());

    auto equal = std::equal(
        result.begin<PixelT>(), result.end<PixelT>(), img.begin<PixelT>());
    EXPECT_TRUE(equal);
}

TEST(TIFFIO, WriteRead16SC1)
{
    using ElemT = std::int16_t;
    using PixelT = ElemT;
    auto cvType = CV_16SC1;

    cv::Mat img(::TEST_IMG_SIZE, cvType);
    ::FillRandom<ElemT, 1>(img);

    const fs::path imgPath(
        "vc_core_TIFFIO_WriteRead_" + cv::typeToString(cvType) + ".tif");
    WriteTIFF(imgPath, img);
    auto result = ReadTIFF(imgPath);

    EXPECT_EQ(result.size, img.size);
    EXPECT_EQ(result.type(), img.type());

    auto equal = std::equal(
        result.begin<PixelT>(), result.end<PixelT>(), img.begin<PixelT>());
    EXPECT_TRUE(equal);
}

TEST(TIFFIO, WriteRead16SC2)
{
    using ElemT = std::int16_t;
    using PixelT = cv::Vec<ElemT, 2>;
    auto cvType = CV_16SC2;

    cv::Mat img(::TEST_IMG_SIZE, cvType);
    ::FillRandom<ElemT, 2>(img);

    const fs::path imgPath(
        "vc_core_TIFFIO_WriteRead_" + cv::typeToString(cvType) + ".tif");
    WriteTIFF(imgPath, img);
    auto result = ReadTIFF(imgPath);

    EXPECT_EQ(result.size, img.size);
    EXPECT_EQ(result.type(), img.type());

    auto equal = std::equal(
        result.begin<PixelT>(), result.end<PixelT>(), img.begin<PixelT>());
    EXPECT_TRUE(equal);
}

TEST(TIFFIO, Write16SC3)
{
    using ElemT = std::int16_t;
    auto cvType = CV_16SC3;

    cv::Mat img(::TEST_IMG_SIZE, cvType);
    ::FillRandom<ElemT, 3>(img);

    const fs::path imgPath(
        "vc_core_TIFFIO_Write_" + cv::typeToString(cvType) + ".tif");
    EXPECT_THROW(WriteTIFF(imgPath, img), IOException);
}

TEST(TIFFIO, Write16SC4)
{
    using ElemT = std::int16_t;
    auto cvType = CV_16SC4;

    cv::Mat img(::TEST_IMG_SIZE, cvType);
    ::FillRandom<ElemT, 4>(img);

    const fs::path imgPath(
        "vc_core_TIFFIO_Write_" + cv::typeToString(cvType) + ".tif");
    EXPECT_THROW(WriteTIFF(imgPath, img), IOException);
}

TEST(TIFFIO, WriteRead32SC1)
{
    using ElemT = std::int32_t;
    using PixelT = ElemT;
    auto cvType = CV_32SC1;

    cv::Mat img(::TEST_IMG_SIZE, cvType);
    ::FillRandom<ElemT, 1>(img);

    const fs::path imgPath(
        "vc_core_TIFFIO_WriteRead_" + cv::typeToString(cvType) + ".tif");
    WriteTIFF(imgPath, img);
    auto result = ReadTIFF(imgPath);

    EXPECT_EQ(result.size, img.size);
    EXPECT_EQ(result.type(), img.type());

    auto equal = std::equal(
        result.begin<PixelT>(), result.end<PixelT>(), img.begin<PixelT>());
    EXPECT_TRUE(equal);
}

TEST(TIFFIO, WriteRead32SC2)
{
    using ElemT = std::int32_t;
    using PixelT = cv::Vec<ElemT, 2>;
    auto cvType = CV_32SC2;

    cv::Mat img(::TEST_IMG_SIZE, cvType);
    ::FillRandom<ElemT, 2>(img);

    const fs::path imgPath(
        "vc_core_TIFFIO_WriteRead_" + cv::typeToString(cvType) + ".tif");
    WriteTIFF(imgPath, img);
    auto result = ReadTIFF(imgPath);

    EXPECT_EQ(result.size, img.size);
    EXPECT_EQ(result.type(), img.type());

    auto equal = std::equal(
        result.begin<PixelT>(), result.end<PixelT>(), img.begin<PixelT>());
    EXPECT_TRUE(equal);
}

TEST(TIFFIO, Write32SC3)
{
    using ElemT = std::int32_t;
    auto cvType = CV_32SC3;

    cv::Mat img(::TEST_IMG_SIZE, cvType);
    ::FillRandom<ElemT, 3>(img);

    const fs::path imgPath(
        "vc_core_TIFFIO_Write_" + cv::typeToString(cvType) + ".tif");
    EXPECT_THROW(WriteTIFF(imgPath, img), IOException);
}

TEST(TIFFIO, Write32SC4)
{
    using ElemT = std::int32_t;
    auto cvType = CV_32SC4;

    cv::Mat img(::TEST_IMG_SIZE, cvType);
    ::FillRandom<ElemT, 4>(img);

    const fs::path imgPath(
        "vc_core_TIFFIO_Write_" + cv::typeToString(cvType) + ".tif");
    EXPECT_THROW(WriteTIFF(imgPath, img), IOException);
}

TEST(TIFFIO, WriteRead32FC1)
{
    using ElemT = std::float_t;
    using PixelT = ElemT;
    auto cvType = CV_32FC1;

    cv::Mat img(::TEST_IMG_SIZE, cvType);
    ::FillRandom<ElemT, 1>(img);

    const fs::path imgPath(
        "vc_core_TIFFIO_WriteRead_" + cv::typeToString(cvType) + ".tif");
    WriteTIFF(imgPath, img);
    auto result = ReadTIFF(imgPath);

    EXPECT_EQ(result.size, img.size);
    EXPECT_EQ(result.type(), img.type());

    auto equal = std::equal(
        result.begin<PixelT>(), result.end<PixelT>(), img.begin<PixelT>());
    EXPECT_TRUE(equal);
}

TEST(TIFFIO, WriteRead32FC2)
{
    using ElemT = std::float_t;
    using PixelT = cv::Vec<ElemT, 2>;
    auto cvType = CV_32FC2;

    cv::Mat img(::TEST_IMG_SIZE, cvType);
    ::FillRandom<ElemT, 2>(img);

    const fs::path imgPath(
        "vc_core_TIFFIO_WriteRead_" + cv::typeToString(cvType) + ".tif");
    WriteTIFF(imgPath, img);
    auto result = ReadTIFF(imgPath);

    EXPECT_EQ(result.size, img.size);
    EXPECT_EQ(result.type(), img.type());

    auto equal = std::equal(
        result.begin<PixelT>(), result.end<PixelT>(), img.begin<PixelT>());
    EXPECT_TRUE(equal);
}

TEST(TIFFIO, WriteRead32FC3)
{
    using ElemT = std::float_t;
    using PixelT = cv::Vec<ElemT, 3>;
    auto cvType = CV_32FC3;

    cv::Mat img(::TEST_IMG_SIZE, cvType);
    ::FillRandom<ElemT, 3>(img);

    const fs::path imgPath(
        "vc_core_TIFFIO_WriteRead_" + cv::typeToString(cvType) + ".tif");
    WriteTIFF(imgPath, img);
    auto result = ReadTIFF(imgPath);

    EXPECT_EQ(result.size, img.size);
    EXPECT_EQ(result.type(), img.type());

    auto equal = std::equal(
        result.begin<PixelT>(), result.end<PixelT>(), img.begin<PixelT>());
    EXPECT_TRUE(equal);
}

TEST(TIFFIO, WriteRead32FC4)
{
    using ElemT = std::float_t;
    using PixelT = cv::Vec<ElemT, 4>;
    auto cvType = CV_32FC4;

    cv::Mat img(::TEST_IMG_SIZE, cvType);
    ::FillRandom<ElemT, 4>(img);

    const fs::path imgPath(
        "vc_core_TIFFIO_WriteRead_" + cv::typeToString(cvType) + ".tif");
    WriteTIFF(imgPath, img);
    auto result = ReadTIFF(imgPath);

    EXPECT_EQ(result.size, img.size);
    EXPECT_EQ(result.type(), img.type());

    auto equal = std::equal(
        result.begin<PixelT>(), result.end<PixelT>(), img.begin<PixelT>());
    EXPECT_TRUE(equal);
}