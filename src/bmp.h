// A 24-bit BMP writer for the self-screenshot: the frame read back from the
// GL back buffer with glReadPixels, bottom-up and BGR, which is exactly the
// layout an uncompressed BMP wants, so the file is a 54-byte header and the
// rows padded to four bytes. Technical/Globe Viewer.md §Day-night and
// seasons describes the screenshot's place in testing (F2, and the tenth
// command-line argument); work order 05's screenshot check is built on it.
#pragma once
#include <cstdio>
#include <fstream>
#include <string>

namespace bmp {

// Write w x h pixels of bottom-up BGR (three bytes each, rows unpadded) to
// path. True if every byte went out.
inline bool write(const std::string& path, int w, int h, const unsigned char* bgr) {
    int rowPad = (4 - (w * 3) % 4) % 4, stride = w * 3 + rowPad;
    unsigned int imgSize = stride * h, fileSize = 54 + imgSize;
    unsigned char hdr[54] = {'B', 'M'};
    *(unsigned int*)(hdr + 2) = fileSize;
    *(unsigned int*)(hdr + 10) = 54;
    *(unsigned int*)(hdr + 14) = 40;
    *(int*)(hdr + 18) = w;
    *(int*)(hdr + 22) = h; // positive height: bottom-up, matching glReadPixels
    *(unsigned short*)(hdr + 26) = 1;
    *(unsigned short*)(hdr + 28) = 24;
    *(unsigned int*)(hdr + 34) = imgSize;
    std::ofstream f(path, std::ios::binary);
    f.write((char*)hdr, 54);
    unsigned char pad[4] = {};
    for (int y = 0; y < h; y++) {
        f.write((const char*)&bgr[(size_t)y * w * 3], w * 3);
        f.write((char*)pad, rowPad);
    }
    return (bool)f;
}

} // namespace bmp
