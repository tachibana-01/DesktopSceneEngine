#include "PresentationViewport.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>

using DesktopSceneEngine::Rendering::derivePresentationViewport;

namespace
{
bool require(bool condition, const char* message)
{
    if (!condition) std::cerr << "Test failed: " << message << '\n';
    return condition;
}

bool matches(int width, int height, double aspect,
    double x, double y, double w, double h)
{
    const auto viewport = derivePresentationViewport(width, height, aspect);
    return require(viewport && std::abs(viewport->x - x) < 0.0001
            && std::abs(viewport->y - y) < 0.0001
            && std::abs(viewport->width - w) < 0.0001
            && std::abs(viewport->height - h) < 0.0001,
        "centered Fit matches independently calculated example");
}

bool verifyProperties()
{
    constexpr std::array<double, 9> aspects{
        1.0, 16.0 / 9.0, 4.0 / 3.0, 9.0 / 16.0, 1.0e-6, 1.0e6,
        1.0 / 2147483647.0, 2147483647.0, 1.001};
    for (int width = 1; width <= 33; ++width)
    {
        for (int height = 1; height <= 33; ++height)
        {
            const auto matching = derivePresentationViewport(width, height, double(width) / height);
            if (!require(matching && matching->x == 0 && matching->y == 0
                    && matching->width == width && matching->height == height,
                    "arbitrary equal aspect has no floating-point gutter")) return false;
            for (const double aspect : aspects)
            {
                const auto viewport = derivePresentationViewport(width, height, aspect);
                const auto repeated = derivePresentationViewport(width, height, aspect);
                if (!require(viewport && repeated, "positive native area and authored ratio are drawable")) return false;
                const double w = viewport->width, h = viewport->height;
                if (!require(w > 0 && h > 0 && viewport->x >= 0 && viewport->y >= 0
                        && double(viewport->x) + w <= width
                        && double(viewport->y) + h <= height
                        && std::abs(w / h / aspect - 1.0) < 0.000001
                        && std::abs(viewport->x + w / 2 - width / 2.0) < 0.00001
                        && std::abs(viewport->y + h / 2 - height / 2.0) < 0.00001
                        && (w == width || h == height),
                        "Fit preserves aspect, is maximal, centered and contained")) return false;
                if (!require(viewport->x == repeated->x && viewport->y == repeated->y
                        && viewport->width == repeated->width && viewport->height == repeated->height,
                        "fractional rounding is stable frame-to-frame")) return false;
            }
        }
    }
    return true;
}
}

int main()
{
    bool ok = matches(1600, 900, 16.0 / 9.0, 0, 0, 1600, 900);
    ok = matches(320, 320, 16.0 / 9.0, 0, 70, 320, 180) && ok;
    ok = matches(1600, 1000, 16.0 / 9.0, 0, 50, 1600, 900) && ok;
    ok = matches(1600, 900, 4.0 / 3.0, 200, 0, 1200, 900) && ok;
    ok = matches(101, 99, 16.0 / 9.0, 0, 21.09375, 101, 56.8125) && ok;
    ok = matches(1, 1, 16.0 / 9.0, 0, 0.21875, 1, 0.5625) && ok;
    ok = matches(1001, 1000, 1001.0 / 1000.0, 0, 0, 1001, 1000) && ok;
    const auto legacy = derivePresentationViewport(101, 99, std::nullopt);
    ok = require(legacy && legacy->x == 0 && legacy->y == 0
            && legacy->width == 101 && legacy->height == 99,
            "unspecified Canvas retains full available target") && ok;
    for (const double aspect : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()})
        ok = require(!derivePresentationViewport(320, 200, aspect), "invalid explicit ratio means no draw") && ok;
    for (const auto size : {std::array{0, 200}, std::array{320, 0}, std::array{-1, 200}})
        ok = require(!derivePresentationViewport(size[0], size[1], 16.0 / 9.0)
                && !derivePresentationViewport(size[0], size[1], std::nullopt),
                "non-drawable native area has no viewport") && ok;
    ok = verifyProperties() && ok;
    if (ok) std::cout << "Canvas Fit geometry passed (9801 property cases).\n";
    return ok ? 0 : 1;
}
