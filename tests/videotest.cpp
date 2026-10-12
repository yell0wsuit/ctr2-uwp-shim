// Video placement for each MediaElement Stretch mode.
#include "xaml/video_layout.h"
#include <cstdio>
using namespace ctr2;
static int failures;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            printf("FAIL %d: %s\n", __LINE__, #x);                                                                     \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)
static bool Same(float a, float b) { return a - b < 0.0001f && b - a < 0.0001f; }
static bool FullSource(const VideoLayout& layout) {
    return Same(layout.source.left, 0) && Same(layout.source.top, 0) && Same(layout.source.right, 1) &&
           Same(layout.source.bottom, 1);
}
static bool Target(const VideoLayout& layout, LONG left, LONG top, LONG right, LONG bottom) {
    return layout.target.left == left && layout.target.top == top && layout.target.right == right &&
           layout.target.bottom == bottom;
}
int main() {
    const SIZE wide{1920, 1080}, window{1024, 768};
    // Uniform letterboxes: 1024 wide keeps 16:9, so 576 tall, centered.
    auto uniform = LayoutVideo(wide, window, VideoFit::Uniform);
    CHECK(FullSource(uniform));
    CHECK(Target(uniform, 0, 96, 1024, 672));
    // Pillarbox when the window is wider than the video.
    auto pillar = LayoutVideo(SIZE{640, 480}, SIZE{1600, 900}, VideoFit::Uniform);
    CHECK(FullSource(pillar));
    CHECK(Target(pillar, 200, 0, 1400, 900));
    // UniformToFill crops the source and fills the window.
    auto fill = LayoutVideo(wide, window, VideoFit::UniformToFill);
    CHECK(Target(fill, 0, 0, 1024, 768));
    CHECK(Same(fill.source.top, 0) && Same(fill.source.bottom, 1));
    CHECK(Same(fill.source.left, 0.125f) && Same(fill.source.right, 0.875f));
    auto tall = LayoutVideo(SIZE{1000, 1000}, SIZE{1000, 500}, VideoFit::UniformToFill);
    CHECK(Same(tall.source.left, 0) && Same(tall.source.right, 1));
    CHECK(Same(tall.source.top, 0.25f) && Same(tall.source.bottom, 0.75f));
    // Fill stretches.
    auto stretch = LayoutVideo(wide, window, VideoFit::Fill);
    CHECK(FullSource(stretch));
    CHECK(Target(stretch, 0, 0, 1024, 768));
    // Unknown sizes draw the whole frame into the whole window.
    auto unknown = LayoutVideo(SIZE{0, 0}, window, VideoFit::Uniform);
    CHECK(FullSource(unknown));
    CHECK(Target(unknown, 0, 0, 1024, 768));
    auto empty = LayoutVideo(wide, SIZE{0, 0}, VideoFit::UniformToFill);
    CHECK(FullSource(empty));
    CHECK(Target(empty, 0, 0, 0, 0));
    printf("videotest: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
