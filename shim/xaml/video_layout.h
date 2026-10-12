#pragma once
#include <windows.h>
#include <cmath>
namespace ctr2 {
// MediaElement's Stretch modes; None draws like Uniform, as before.
enum class VideoFit : unsigned char { Fill, Uniform, UniformToFill };
// The part of the frame to draw (0..1 of its width and height), and where.
struct VideoLayout {
    struct {
        float left, top, right, bottom;
    } source;
    RECT target;
};
inline VideoLayout LayoutVideo(SIZE video, SIZE window, VideoFit fit) {
    VideoLayout layout{{0, 0, 1, 1}, {0, 0, window.cx > 0 ? window.cx : 0, window.cy > 0 ? window.cy : 0}};
    if (fit == VideoFit::Fill || video.cx <= 0 || video.cy <= 0 || window.cx <= 0 || window.cy <= 0) return layout;
    const double videoAspect = static_cast<double>(video.cx) / video.cy;
    const double windowAspect = static_cast<double>(window.cx) / window.cy;
    if (fit == VideoFit::UniformToFill) {
        // Crop the side that overflows, keeping the middle.
        if (videoAspect > windowAspect) {
            layout.source.left = static_cast<float>((1 - windowAspect / videoAspect) / 2);
            layout.source.right = 1 - layout.source.left;
        } else {
            layout.source.top = static_cast<float>((1 - videoAspect / windowAspect) / 2);
            layout.source.bottom = 1 - layout.source.top;
        }
        return layout;
    }
    // Uniform: the largest centered rectangle with the video's shape.
    if (videoAspect > windowAspect) {
        const auto height = static_cast<LONG>(std::lround(window.cx / videoAspect));
        layout.target.top = (window.cy - height) / 2;
        layout.target.bottom = layout.target.top + height;
    } else {
        const auto width = static_cast<LONG>(std::lround(window.cy * videoAspect));
        layout.target.left = (window.cx - width) / 2;
        layout.target.right = layout.target.left + width;
    }
    return layout;
}
}
