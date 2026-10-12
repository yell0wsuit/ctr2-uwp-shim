#include <windows.h>
#include <cstdio>
#include <vector>
int main(int argc, char**) {
    HWND hwnd = FindWindowW(L"CTR2ShimWindow", nullptr);
    if (!hwnd) {
        puts("No shim window");
        return 1;
    }
    printf("window=%p visible=%d\n", hwnd, IsWindowVisible(hwnd));
    RECT rect{};
    GetClientRect(hwnd, &rect);
    int w = rect.right, h = rect.bottom;
    HDC screen = GetDC(hwnd), memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, w, h);
    auto* old = SelectObject(memory, bitmap);
    if (!BitBlt(memory, 0, 0, w, h, screen, 0, 0, SRCCOPY)) {
        printf("BitBlt error=%lu size=%dx%d screen=%p memory=%p bitmap=%p\n", GetLastError(), w, h, screen, memory,
               bitmap);
        return 2;
    }
    SelectObject(memory, old);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    std::vector<BYTE> pixels(w * h * 4);
    if (!GetDIBits(memory, bitmap, 0, h, pixels.data(), &info, DIB_RGB_COLORS)) return 3;
    BITMAPFILEHEADER header{};
    header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
    header.bfSize = header.bfOffBits + static_cast<DWORD>(pixels.size());
    FILE* file{};
    fopen_s(&file, "capture.bmp", "wb");
    if (!file) return 4;
    fwrite(&header, sizeof(header), 1, file);
    fwrite(&info.bmiHeader, sizeof(BITMAPINFOHEADER), 1, file);
    fwrite(pixels.data(), pixels.size(), 1, file);
    fclose(file);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(hwnd, screen);
    printf("captured %dx%d\n", w, h);
    if (argc > 1) PostMessageW(hwnd, WM_CLOSE, 0, 0);
    return 0;
}
