#include "qg2.h"
#include <cstdio>
static double mx(const std::vector<double>& v, bool* fin) { double m = 0; *fin = true; for (double d : v) { if (!std::isfinite(d)) *fin = false; m = std::max(m, std::fabs(d)); } return m; }
int main() {
    const int W = 192, H = 96; qg2::Model m;
    std::vector<float> elev(W * H, 0.0f), lat(W * H, 0.0f); std::vector<unsigned char> water(W * H, 1);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) lat[y * W + x] = (float)((((y + 0.5) / H) - 0.5) * 3.14159265);
    m.init(W, H, elev, lat, water);
    std::vector<double> tns(W * H);
    for (int i = 0; i < W * H; i++) { double a = std::fabs(lat[i]) * 180.0 / 3.14159265; tns[i] = 27.0 - 55.0 * std::pow(a / 90.0, 1.5); }
    m.setTargets(tns, true);
    bool f; double v;
    v = mx(m.psicT, &f); printf("psicT %.3e %d\n", v, f);
    v = mx(m.q1, &f); printf("q1 %.3e %d\n", v, f);
    m.invert();
    v = mx(m.psi1, &f); printf("psi1 after invert %.3e %d\n", v, f);
    v = mx(m.psi2, &f); printf("psi2 after invert %.3e %d\n", v, f);
    // which rows are bad?
    for (int y = 0; y < H; y++) { bool ok = true; for (int x = 0; x < W; x++) if (!std::isfinite(m.psi1[y * W + x])) ok = false; if (!ok) { printf("row %d not finite\n", y); break; } }
    return 0;
}
