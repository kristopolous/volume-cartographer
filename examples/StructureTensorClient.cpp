#include <iostream>
#include <algorithm>
#include <opencv2/opencv.hpp>
#include "volumepkg.h"

void draw(const volcart::Volume& v, const int32_t zSlice, const EigenVector vec,
          const cv::Point p1);

int main(int argc, char** argv)
{
    if (argc < 5) {
        std::cerr << "Usage:\n";
        std::cerr << "    " << argv[0] << " volpkg x y z [radius]\n";
        std::exit(1);
    }

    int32_t radius = 1;
    if (argc > 5) {
        radius = std::stoi(argv[5]);
    }

    const VolumePkg vpkg{std::string(argv[1])};
    const volcart::Volume v = vpkg.volume();
    const int32_t x = std::stoi(argv[2]);
    const int32_t y = std::stoi(argv[3]);
    const int32_t z = std::stoi(argv[4]);
    std::cout << "{" << x << ", " << y << ", " << z << "} @ " << radius << "\n";

    std::cout << "structure tensor:\n"
              << v.structureTensorAtIndex(x, y, z, radius) << "\n";
    try {
        auto pairs = v.eigenPairsAtIndex(x, y, z, radius);
        std::cout << "eigenvalues/eigenvectors\n";
        std::for_each(pairs.begin(), pairs.end(),
                      [](const std::pair<EigenValue, EigenVector>& p) {
                          std::cout << p.first << ":  " << p.second << "\n";
                      });
        draw(v, z, pairs[0].second, {x, y});
    } catch (const volcart::ZeroStructureTensorException& ex) {
        std::cout << ex.what() << std::endl;
    }
}

void draw(const volcart::Volume& v, const int32_t zSlice, const EigenVector vec,
          const cv::Point p1)
{
    auto slice = v.getSliceData(zSlice);
    slice /= 255.0;
    slice.convertTo(slice, CV_8UC3);
    cv::cvtColor(slice, slice, CV_GRAY2BGR);

    constexpr int32_t scale = 20;
    const cv::Point p2{cvRound(p1.x + vec(0) * scale),
                       cvRound(p1.y + vec(1) * scale)};
    cv::line(slice, p1, p2, cv::Scalar(0, 0, 0xFF), 2);
    cv::namedWindow("slice", cv::WINDOW_NORMAL);
    cv::imshow("slice", slice);
    cv::waitKey(0);
}
