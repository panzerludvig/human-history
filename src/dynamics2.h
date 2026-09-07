// Two-level primitive-equation atmosphere on sigma coordinates: the
// smallest atmosphere that makes weather.
//
// One moving layer under a passive one cannot make a storm track, a
// westerly belt or a winter continental high; two layers, each with wind,
// mass and temperature and coupled through their interface, can. That is
// Phillips' two-layer model of 1956, the first numerical atmosphere to
// grow baroclinic eddies, and it is what every painted rule of the
// prescribed climate has been standing in for.
//
// The temperatures are not computed from radiation -- that was the part
// of the earlier physics attempt that fought its calibration for days.
// Each level's potential temperature is relaxed toward the painted climate
// on a timescale of days (Held and Suarez, 1994): the weather moves heat
// and is pulled back, so no world can drift into a snowball, and every
// world's weather is made by the same rules on its own painted climate.
//
// Levels: k=1 the lower, at sigma 0.75, k=2 the upper, at sigma 0.25; the
// interface at sigma 0.5; equal masses. Prognostic: surface pressure ps,
// u and v at both levels, theta at both levels. Geopotential by
// hypsometry from the levels' temperatures. Vertical motion at the
// interface from the two levels' divergence, and it carries theta and
// momentum upwind across the interface. Forward-backward time stepping
// (momentum, then continuity with the new winds), which is stable for the
// gravity waves that a forward step is not. The polar rows are walls and
// the zonal filter of the old dynamics handles the crowding meridians.
#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

namespace dyn2 {

constexpr double R_GAS = 287.0;      // J/kg/K
constexpr double KAPPA = 0.2857;     // R/cp
constexpr double P0 = 1.0e5;         // Pa, reference for theta
constexpr double SIG1 = 0.75, SIG2 = 0.25;
constexpr double LN_S = 0.6931;      // ln 2: the pressure ratio across each level
// Held-Suarez-style constants
inline double TAU_RELAX_LOW = 10.0 * 86400.0; // s, the lower level's theta relaxes to the painted climate
inline double TAU_RELAX_UP = 40.0 * 86400.0;  // s, the upper's: a baroclinic wave's warmth aloft must live long enough to grow (Held-Suarez: 40 days)
inline int TARGET_SMOOTH_PASSES = 8;       // a coast's step is not a climate's gradient
// The surface's contrasts fade with height: Siberia is -30 at the ground
// and -15 at 2.5 km, and the jet aloft is nearly zonal. Each level's target
// is the zonal mean plus this share of the surface's departure from it.
inline double ANOM_LOW = 0.5, ANOM_UP = 0.25;
inline double DRAG_LOW = 1.0 / (3.0 * 86400.0);  // s^-1, drag on the lower level (Held-Suarez's day is at the ground; at sigma 0.75 it is nearer a week, and a day killed the wave)
inline double DRAG_UP = 1.0 / (20.0 * 86400.0);  // and a little aloft
inline double VISC = 5.0e5;                // m2/s, keeps the grid scale quiet (8e4 let a two-cell cold dome run away in ten hours; the old dynamics used 6e5)
inline double DT = 120.0;                  // s, the substep
inline double T2_BELOW_T1 = 40.0;          // K, the painted upper level under the lower one
inline double EXCHANGE = 1.0;              // share of the interface exchange applied (a switch for tests)
inline double CAP_LAT = 81.0;              // degrees: poleward of this the rows are walls
inline double TERRAIN_SCALE = 0.0;         // 0: a flat planet (the core must make weather on that first); 1: the template's mountains

struct Model {
    int W = 0, H = 0;
    // The polar caps are walls: poleward of CAP_LAT a cell is too narrow for
    // a gravity wave to cross in a substep, and a wave excited along such a
    // row -- by a coast at 88 degrees -- blows the model up in an hour.
    // The old dynamics filtered through this; here the rows are simply not
    // stepped, and hold the values of the last row that is.
    int capRows = 0;
    std::vector<double> ps, u1, v1, u2, v2, th1, th2;   // state
    std::vector<double> nps, nu1, nv1, nu2, nv2, nth1, nth2;
    std::vector<double> phis, th1t, th2t, latRad, fcor;  // terrain, targets, geometry
    std::vector<double> phi1, phi2, t1, t2, sdot;        // diagnostics per step
    std::vector<double> ex1, ex2, lnps;                  // Exner functions (per hour) and ln ps (per step)
    std::vector<double> tmp;
    // probes: eddy kinetic energy and pressure variance, banked per hour
    std::vector<double> ekeAcc, psAcc, ps2Acc, u1Acc, u2Acc;
    double hoursBanked = 0;

    int idx(int x, int y) const { return y * W + x; }
    int wrapX(int x) const { return (x % W + W) % W; }

    void init(int w, int h, const std::vector<float>& elev, const std::vector<float>& lat,
              const std::vector<unsigned char>& water) {
        W = w; H = h;
        int n = W * H;
        ps.assign(n, P0); u1.assign(n, 0); v1.assign(n, 0); u2.assign(n, 0); v2.assign(n, 0);
        th1.assign(n, 300); th2.assign(n, 340);
        nps = ps; nu1 = u1; nv1 = v1; nu2 = u2; nv2 = v2; nth1 = th1; nth2 = th2;
        phis.assign(n, 0); th1t.assign(n, 300); th2t.assign(n, 340);
        latRad.assign(n, 0); fcor.assign(n, 0);
        phi1.assign(n, 0); phi2.assign(n, 0); t1.assign(n, 0); t2.assign(n, 0); sdot.assign(n, 0);
        ex1.assign(n, 1); ex2.assign(n, 1); lnps.assign(n, 0);
        tmp.assign(n, 0);
        ekeAcc.assign(n, 0); psAcc.assign(n, 0); ps2Acc.assign(n, 0); u1Acc.assign(n, 0); u2Acc.assign(n, 0);
        capRows = 0;
        for (int y = 0; y < H / 2; y++)
            if (std::fabs(((y + 0.5) / H - 0.5) * 180.0) > CAP_LAT) capRows = y + 1;
        for (int i = 0; i < n; i++) {
            phis[i] = TERRAIN_SCALE * 9.81 * (water[i] ? 0.0 : std::max((double)elev[i], 0.0));
            latRad[i] = lat[i];
            fcor[i] = 2 * 7.292e-5 * std::sin(lat[i]);
            // start hydrostatically at rest: ps under the terrain
            ps[i] = P0 * std::exp(-phis[i] / (R_GAS * 280.0));
        }
        nps = ps;
    }

    // The painted climate as targets: the near-surface air temperature per
    // cell, from which the lower level (2.5 km up) and the upper (about 9)
    // follow a fixed lapse. Called each hour.
    void setTargets(const std::vector<double>& tNearSurfaceC, bool first) {
        for (int y = 0; y < H; y++) {
            double zm = 0;
            for (int x = 0; x < W; x++) zm += tNearSurfaceC[idx(x, y)] / W;
            for (int x = 0; x < W; x++) {
                int i = idx(x, y);
                double anom = tNearSurfaceC[i] - zm;
                double tl = zm + ANOM_LOW * anom + 273.15 - 16.0;     // lapse to sigma 0.75
                double tu = zm + ANOM_UP * anom + 273.15 - 16.0 - T2_BELOW_T1; // and on to sigma 0.25
                th1t[i] = tl * std::pow(1.0 / SIG1, KAPPA);
                th2t[i] = tu * std::pow(1.0 / SIG2, KAPPA);
            }
        }
        // A coastline's 20 K step in one cell is a pressure gradient that
        // accelerates the air a metre a second per substep; the target is
        // a climate, and a climate varies over a thousand kilometres.
        for (int pass = 0; pass < TARGET_SMOOTH_PASSES; pass++) {
            for (auto* f : {&th1t, &th2t}) {
                for (int y = 0; y < H; y++)
                    for (int x = 0; x < W; x++) {
                        int i = idx(x, y), yn = std::min(y + 1, H - 1), ys = std::max(y - 1, 0);
                        tmp[i] = 0.4 * (*f)[i] + 0.15 * ((*f)[idx(wrapX(x + 1), y)] + (*f)[idx(wrapX(x - 1), y)] +
                                                        (*f)[idx(x, yn)] + (*f)[idx(x, ys)]);
                    }
                *f = tmp;
            }
        }
        if (first) {
            // Start from the zonal-mean targets: dropping every continent's
            // anomaly into a resting atmosphere at once launches gravity
            // waves of tens of hectopascals that ring for days; the
            // relaxation brings the anomalies in over its own timescale.
            for (int y = 0; y < H; y++) {
                double z1 = 0, z2 = 0;
                for (int x = 0; x < W; x++) { z1 += th1t[idx(x, y)] / W; z2 += th2t[idx(x, y)] / W; }
                for (int x = 0; x < W; x++) { th1[idx(x, y)] = z1; th2[idx(x, y)] = z2; }
            }
        }
        if (first) {
            nth1 = th1; nth2 = th2;
            // hydrostatic rest under the terrain with the column's own
            // temperature, so the two halves of the pressure-gradient force
            // cancel on the first step
            for (int i = 0; i < W * H; i++) {
                double tns = tNearSurfaceC[i] + 273.15;
                ps[i] = P0 * std::exp(-phis[i] / (R_GAS * (tns - 10.0)));
            }
            nps = ps;
        }
    }

    // A balanced start: the surface pressure that leaves the lower level's
    // pressure-gradient force zero at rest under the target temperatures,
    // ps proportional to T1 to the power -ln(1/sigma1). The upper level is
    // then out of balance by the thermal wind, which is the jet it should
    // spin up to.
    void balancePs() {
        double mean = 0;
        for (int i = 0; i < W * H; i++) mean += th1t[i] / (W * H);
        for (int i = 0; i < W * H; i++)
            ps[i] = P0 * std::exp(-phis[i] / (R_GAS * 280.0)) * std::pow(th1t[i] / mean, -std::log(1.0 / SIG1));
        nps = ps;
    }
    // The Exner functions, once an hour: ps moves little in an hour and
    // the power function is most of a substep's cost.
    void refreshExner() {
#pragma omp parallel for
        for (int i = 0; i < W * H; i++) {
            ex1[i] = std::pow(SIG1 * ps[i] / P0, KAPPA);
            ex2[i] = std::pow(SIG2 * ps[i] / P0, KAPPA);
        }
    }
    // One substep.
    void step(double dt, void (*polarFilter)(std::vector<double>&, void*), void* ctx) {
        const double dx0 = 2 * 3.14159265 * 6371000.0 / W;
        const double dy = 3.14159265 * 6371000.0 / H;
        // temperatures and geopotentials from theta and ps
#pragma omp parallel for
        for (int i = 0; i < W * H; i++) {
            t1[i] = th1[i] * ex1[i];
            t2[i] = th2[i] * ex2[i];
            lnps[i] = std::log(ps[i]);
            // hypsometric: sigma 1 -> 0.75 with T1, 0.75 -> 0.5 with T1, 0.5 -> 0.25 with T2
            phi1[i] = phis[i] + R_GAS * t1[i] * std::log(1.0 / SIG1);
            phi2[i] = phis[i] + R_GAS * t1[i] * LN_S + R_GAS * t2[i] * LN_S;
        }
        // momentum and theta
#pragma omp parallel for
        for (int y = capRows; y < H - capRows; y++) {
            double cosl = std::max(std::cos((((y + 0.5) / (double)H) - 0.5) * 3.14159265), 0.05);
            double dx = dx0 * cosl;
            for (int x = 0; x < W; x++) {
                int i = idx(x, y), xe = idx(wrapX(x + 1), y), xw = idx(wrapX(x - 1), y);
                int yn = idx(x, y + 1), ys = idx(x, y - 1);
                double f = fcor[i];
                double dlnpx = (lnps[xe] - lnps[xw]) / (2 * dx);
                double dlnpy = (lnps[yn] - lnps[ys]) / (2 * dy);
                auto level = [&](const std::vector<double>& u, const std::vector<double>& v,
                                 const std::vector<double>& phi, const std::vector<double>& t,
                                 const std::vector<double>& th, double drag, double& nu, double& nv,
                                 double& nth, double tht, double tau) {
                    // pressure gradient: grad(phi) + R T grad(ln ps)
                    double pgx = (phi[xe] - phi[xw]) / (2 * dx) + R_GAS * t[i] * dlnpx;
                    double pgy = (phi[yn] - phi[ys]) / (2 * dy) + R_GAS * t[i] * dlnpy;
                    // advection, upwind
                    double ui = u[i], vi = v[i];
                    double dudx = ui > 0 ? (ui - u[xw]) / dx : (u[xe] - ui) / dx;
                    double dudy = vi > 0 ? (ui - u[ys]) / dy : (u[yn] - ui) / dy;
                    double dvdx = ui > 0 ? (vi - v[xw]) / dx : (v[xe] - vi) / dx;
                    double dvdy = vi > 0 ? (vi - v[ys]) / dy : (v[yn] - vi) / dy;
                    double lapU = (u[xe] + u[xw] - 2 * ui) / (dx * dx) + (u[yn] + u[ys] - 2 * ui) / (dy * dy);
                    double lapV = (v[xe] + v[xw] - 2 * vi) / (dx * dx) + (v[yn] + v[ys] - 2 * vi) / (dy * dy);
                    nu = ui + dt * (-(ui * dudx + vi * dudy) - pgx + f * vi - drag * ui + VISC * lapU);
                    nv = vi + dt * (-(ui * dvdx + vi * dvdy) - pgy - f * ui - drag * vi + VISC * lapV);
                    nu = std::clamp(nu, -90.0, 90.0);
                    nv = std::clamp(nv, -90.0, 90.0);
                    double dthx = ui > 0 ? (th[i] - th[xw]) / dx : (th[xe] - th[i]) / dx;
                    double dthy = vi > 0 ? (th[i] - th[ys]) / dy : (th[yn] - th[i]) / dy;
                    double lapT = (th[xe] + th[xw] - 2 * th[i]) / (dx * dx) + (th[yn] + th[ys] - 2 * th[i]) / (dy * dy);
                    nth = th[i] + dt * (-(ui * dthx + vi * dthy) - (th[i] - tht) / tau + VISC * lapT);
                };
                level(u1, v1, phi1, t1, th1, DRAG_LOW, nu1[i], nv1[i], nth1[i], th1t[i], TAU_RELAX_LOW);
                level(u2, v2, phi2, t2, th2, DRAG_UP, nu2[i], nv2[i], nth2[i], th2t[i], TAU_RELAX_UP);
            }
        }
        // continuity with the new winds (forward-backward), and the
        // interface motion that the two levels' divergence implies
#pragma omp parallel for
        for (int y = capRows; y < H - capRows; y++) {
            double cosl = std::max(std::cos((((y + 0.5) / (double)H) - 0.5) * 3.14159265), 0.05);
            double cosN = std::max(std::cos(((y + 1.0) / (double)H - 0.5) * 3.14159265), 0.02);
            double cosS = std::max(std::cos(((y + 0.0) / (double)H - 0.5) * 3.14159265), 0.02);
            double dx = dx0 * cosl;
            for (int x = 0; x < W; x++) {
                int i = idx(x, y), xe = idx(wrapX(x + 1), y), xw = idx(wrapX(x - 1), y);
                int yn = idx(x, y + 1), ys = idx(x, y - 1);
                auto divOf = [&](const std::vector<double>& u, const std::vector<double>& v) {
                    double fe = 0.5 * (u[i] + u[xe]) * 0.5 * (ps[i] + ps[xe]);
                    double fw = 0.5 * (u[xw] + u[i]) * 0.5 * (ps[xw] + ps[i]);
                    double fn = 0.5 * (v[i] + v[yn]) * 0.5 * (ps[i] + ps[yn]) * cosN;
                    double fs = 0.5 * (v[ys] + v[i]) * 0.5 * (ps[ys] + ps[i]) * cosS;
                    return (fe - fw) / dx + (fn - fs) / (dy * cosl);
                };
                double d1 = divOf(nu1, nv1), d2 = divOf(nu2, nv2);
                nps[i] = ps[i] - dt * 0.5 * (d1 + d2);
                nps[i] = std::clamp(nps[i], 0.3 * P0, 1.2 * P0);
                // sigma-dot at the interface: positive downward
                sdot[i] = 0.25 * (d1 - d2) / ps[i];
            }
        }
        // the interface carries theta and momentum upwind: the level that
        // receives air gets the other's, at the fraction of its own mass
        // that crossed
#pragma omp parallel for
        for (int y = capRows; y < H - capRows; y++)
            for (int x = 0; x < W; x++) {
                int i = idx(x, y);
                double frac = EXCHANGE * std::min(std::fabs(sdot[i]) * dt / 0.5, 0.5);
                if (sdot[i] < 0) { // upward: the upper level receives the lower's air
                    nth2[i] += frac * (th1[i] - th2[i]);
                    nu2[i] += frac * (u1[i] - u2[i]);
                    nv2[i] += frac * (v1[i] - v2[i]);
                } else if (sdot[i] > 0) {
                    nth1[i] += frac * (th2[i] - th1[i]);
                    nu1[i] += frac * (u2[i] - u1[i]);
                    nv1[i] += frac * (v2[i] - v1[i]);
                }
            }
        // walls at the poles: the cap rows hold the last stepped row's
        // mass and warmth and no wind; the edge rows of the stepped band
        // see a wall of their own values to the poleward side.
        for (int x = 0; x < W; x++) {
            for (int y = 0; y < capRows; y++) {
                int i = idx(x, y), j = idx(x, capRows);
                nu1[i] = nv1[i] = nu2[i] = nv2[i] = 0.0;
                nps[i] = nps[j]; nth1[i] = nth1[j]; nth2[i] = nth2[j];
                i = idx(x, H - 1 - y); j = idx(x, H - 1 - capRows);
                nu1[i] = nv1[i] = nu2[i] = nv2[i] = 0.0;
                nps[i] = nps[j]; nth1[i] = nth1[j]; nth2[i] = nth2[j];
            }
            // and no flow through the wall itself
            nv1[idx(x, capRows)] = nv2[idx(x, capRows)] = 0.0;
            nv1[idx(x, H - 1 - capRows)] = nv2[idx(x, H - 1 - capRows)] = 0.0;
        }
        std::swap(ps, nps); std::swap(u1, nu1); std::swap(v1, nv1); std::swap(u2, nu2); std::swap(v2, nv2);
        std::swap(th1, nth1); std::swap(th2, nth2);
        (void)polarFilter; (void)ctx;
        zonalFilter(u1); zonalFilter(v1); zonalFilter(u2); zonalFilter(v2);
        zonalFilter(ps); zonalFilter(th1); zonalFilter(th2);
    }

    // The crowding meridians: smooth zonally until the zonal resolution
    // matches the meridional, as the old dynamics did -- passes go as
    // 1/cos - 1, and a wave that outruns a polar cell in one step is
    // removed before it can grow.
    void zonalFilter(std::vector<double>& f) {
        for (int y = capRows; y < H - capRows; y++) {
            double cosl = std::max(std::cos(((y + 0.5) / (double)H - 0.5) * 3.14159265), 0.02);
            int passes = (int)std::clamp(std::lround(1.0 / cosl - 1.0), (long)0, (long)12);
            for (int k = 0; k < passes; k++) {
                for (int x = 0; x < W; x++)
                    tmp[idx(x, y)] = 0.25 * f[idx(wrapX(x - 1), y)] + 0.5 * f[idx(x, y)] + 0.25 * f[idx(wrapX(x + 1), y)];
                for (int x = 0; x < W; x++) f[idx(x, y)] = tmp[idx(x, y)];
            }
        }
    }
    // Bank an hour for the probes.
    void bank() {
        for (int i = 0; i < W * H; i++) {
            psAcc[i] += ps[i]; ps2Acc[i] += ps[i] * ps[i];
            u1Acc[i] += u1[i]; u2Acc[i] += u2[i];
        }
        hoursBanked += 1;
    }
    void bankEddy() {
        // eddy kinetic energy against the zonal mean, per cell, this hour
        for (int y = 0; y < H; y++) {
            double mu = 0, mv = 0;
            for (int x = 0; x < W; x++) { mu += u1[idx(x, y)]; mv += v1[idx(x, y)]; }
            mu /= W; mv /= W;
            for (int x = 0; x < W; x++) {
                int i = idx(x, y);
                ekeAcc[i] += 0.5 * ((u1[i] - mu) * (u1[i] - mu) + (v1[i] - mv) * (v1[i] - mv));
            }
        }
    }
};

} // namespace dyn2
