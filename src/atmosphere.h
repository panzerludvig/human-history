// Atmosphere: a toy two-level circulation model run once at world generation,
// distilled into a per-season climatology. Rules in Design/Weather.md.
//
// The low level is prognostic: surface temperature from an energy budget,
// winds from thermal pressure gradients with Coriolis and drag, moisture with
// temperature-dependent capacity. The upper level is implicit: mass
// continuity turns low-level convergence into uplift (rain) and divergence
// into subsidence (drying) — the return flow's effect without its state.
#pragma once
#include "terrain.h"
#include "hydrology.h"
#include <cmath>
#include <cstdio>
#include <vector>

namespace atmosphere {

constexpr int W = 192, H = 96;   // ~208 km cells at the equator
constexpr int SEASONS = 4;       // DJF, MAM, JJA, SON

constexpr double DT = 3600.0;            // s, one step per sim hour
constexpr int SPINUP_DAYS = 365;         // discarded first year
constexpr int STAT_YEARS = 2;            // averaged years after spin-up
constexpr double R_EARTH = 6371000.0;    // m
constexpr double OMEGA = 7.292e-5;       // rad/s

// Energy budget (W/m^2, degC, J/K/m^2)
constexpr double SOLAR = 1361.0;
constexpr double OLR_A = 202.0, OLR_B = 2.0;    // outgoing = A + B*T (Budyko); A tuned so the equator sits ~24 C
constexpr double C_WATER = 1.0e8;               // ~25 m slab ocean
constexpr double C_LAND = 3.0e6;                // thin soil; scaled by inertia
// Winds: diagnostic Ekman-style balance r*u - f x u = -grad(P)/rho, solved
// per cell. Integrating momentum at this grid and step is numerically
// unstable; the balanced response keeps the same circulation (convergence on
// heat lows, Coriolis deflection into trades and westerlies) with winds
// bounded by construction.
constexpr double P_PER_DEG = 120.0;             // Pa of thermal low per degC
constexpr double FRICTION = 1.0 / (8.0 * 3600.0); // balance friction r
constexpr double RHO = 1.2;
constexpr double ADV_EFF = 1.0;                 // surface-wind moisture-advection efficiency
// Moisture (kg/m^2 precipitable water)
constexpr double CAP0 = 15.0, CAP_T0 = 15.0, CAP_SCALE = 14.4; // doubles per 10 C
constexpr double EVAP_WATER = 0.20, EVAP_LAND = 0.05;          // kg/m^2 per h at full deficit
constexpr double H_FLOW = 1500.0;               // m, depth of the inflow layer
// Rain falls when moisture exceeds a fraction of the effective capacity.
// Vertical motion modulates that capacity: uplift (convergence, windward
// slopes) shrinks it -- adiabatic cooling -- and subsidence swells it, which
// is what makes descent zones and lee sides dry.
constexpr double RAIN_FRAC = 0.65;              // rain begins above this fraction of capacity
constexpr double RAIN_RATE = 0.15;              // fraction of the excess per hour
constexpr double DIV_CAP_SCALE = 0.05;          // m/s of uplift for a ~46% capacity swing
// Over land, moisture rains out progressively along its path (precipitation
// is not withheld until a convergence line): an e-folding of ~3 days, i.e.
// ~1300 km at typical winds. This is what makes coasts wetter than deep
// continental interiors.
constexpr double LAND_RAINOUT_TAU = 3.0 * 86400.0; // s
constexpr double K_DIFF = 2.0e5;                // m^2/s eddy diffusion of moisture
// Frontal-storm rain: mid-latitude rain on Earth is mostly baroclinic storms
// riding the temperature gradient, which steady diagnostic winds cannot
// produce. Parameterized as rain ~ |grad T| * moisture: strong on the winter
// storm tracks, negligible in the flat-gradient tropics.
constexpr double K_STORM = 900.0;               // per hour, per (K/m) of gradient
constexpr double SNOW_T = 0.5;                  // degC: colder precipitation is snow
// Heat is transported by diffusion alone: the surface wind is the convergent
// branch of an overturning cell, and advecting T with it refrigerates heat
// lows (the upper return flow that closes the loop is not modelled). A large
// eddy diffusivity stands in for the whole poleward heat transport, as in
// Budyko-style energy-balance models.
constexpr double KT_DIFF = 2.5e6;               // m^2/s eddy diffusion of heat

struct Climatology {
    // [season][cell]
    std::vector<float> meanT, rainMmDay, snowMmDay, rainProb, windU, windV, cloud, diurnal;
    std::vector<float> elev; // [cell], the model's smoothed elevation (for lapse correction)
    // elev has one band; bilinearAt/annualAt want [season][cell]. A repeated
    // view is built eagerly at the end of build() -- the lazy path races when
    // parallel consumers (population's cell loop) hit it simultaneously.
    mutable std::vector<float> elevRep;
    const std::vector<float>& elev4() const {
        if (elevRep.empty() && !elev.empty()) {
            elevRep.resize(SEASONS * W * H);
            for (int se = 0; se < SEASONS; se++)
                std::copy(elev.begin(), elev.end(), elevRep.begin() + se * W * H);
        }
        return elevRep;
    }
    Climatology() {
        for (auto* v : {&meanT, &rainMmDay, &snowMmDay, &rainProb, &windU, &windV, &cloud, &diurnal})
            v->assign(SEASONS * W * H, 0.0f);
    }
    static int seasonOfDay(int doy) { // DJF=0 starting Dec 1 (day 334)
        if (doy >= 334 || doy < 59) return 0;
        if (doy < 151) return 1;
        if (doy < 243) return 2;
        return 3;
    }
};

inline double capOf(double T) { return CAP0 * std::exp((T - CAP_T0) / CAP_SCALE); }

inline int wrapX(int x) { return (x % W + W) % W; }

struct Model {
    // static per cell
    std::vector<float> elev, albedo, heatC, latRad;
    std::vector<unsigned char> water;
    // state
    std::vector<double> T, Wv, u, v;
    // scratch
    std::vector<double> nT, nW, nu, nv, div, rainStep, Tsl;
    // probe diagnostics (an equatorial cell): daily sums of the T budget terms
    int probe = 4 * W + W / 2; // south-polar cell for the current investigation
    double pSw = 0, pOlr = 0, pAdv = 0, pDif = 0;

    int idx(int x, int y) const { return y * W + x; }

    void init(const terrain::ContinentParams& cp, float seaLevel, const float rot[9],
              terrain::V3 offset, const plates::Field& pf, const hydrology::Result& hy) {
        elev.assign(W * H, 0.0f);
        albedo.assign(W * H, 0.2f);
        heatC.assign(W * H, (float)C_LAND);
        latRad.assign(W * H, 0.0f);
        water.assign(W * H, 0);
        int bx = hydrology::W / W, by = hydrology::H / H;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int i = idx(x, y);
                double hsum = 0, land = 0;
                for (int yy = 0; yy < by; yy++)
                    for (int xx = 0; xx < bx; xx++) {
                        float h = hy.heightM[(y * by + yy) * hydrology::W + (x * bx + xx)];
                        if (h > 0) { land++; hsum += h; }
                    }
                double landFrac = land / (bx * by);
                water[i] = landFrac < 0.5 ? 1 : 0;
                elev[i] = water[i] ? 0.0f : (float)(hsum / std::max(land, 1.0));
                float lat = (float)((((y + 0.5) / H) - 0.5) * 3.14159265);
                latRad[i] = lat;
                // First-guess surface properties from the painted climate.
                float lon = (float)((((x + 0.5) / W) * 2.0 - 1.0) * 3.14159265);
                terrain::V3 n = {std::cos(lat) * std::cos(lon), std::cos(lat) * std::sin(lon),
                                 std::sin(lat)};
                terrain::V3 w = terrain::rotate(rot, n) + offset;
                float temp = terrain::temperatureC(lat, elev[i]);
                if (water[i]) {
                    albedo[i] = temp < -8 ? 0.55f : 0.08f; // sea ice, crudely
                    heatC[i] = (float)C_WATER;
                } else {
                    float moist = terrain::moistureAt(w, lat);
                    terrain::Mixture m = terrain::mixtureAt(elev[i], 0.0f, temp, moist, 0.0f,
                                                            false, terrain::patchNoise(w));
                    float veg = m.cov[3] + m.cov[4] + m.cov[2];                 // forests
                    float bare = m.cov[0] + m.cov[10] + m.cov[1] * 0.5f;       // bare/desert/tundra
                    albedo[i] = temp < -10 ? 0.6f : 0.14f + 0.18f * bare - 0.03f * veg;
                    heatC[i] = (float)(C_LAND * (1.0 + 2.0 * veg));
                }
            }
        T.assign(W * H, 0.0);
        Wv.assign(W * H, 0.0);
        u.assign(W * H, 0.0);
        v.assign(W * H, 0.0);
        for (int i = 0; i < W * H; i++) {
            T[i] = terrain::temperatureC(latRad[i], elev[i]);
            Wv[i] = 0.5 * capOf(T[i]);
        }
        nT = T; nW = Wv; nu = u; nv = v;
        div.assign(W * H, 0.0);
        rainStep.assign(W * H, 0.0);
        Tsl.assign(W * H, 0.0);
    }

    // One hour. doy in [0,365), hourOfDay in [0,24).
    void step(double doy, double hour) {
        double dec = 23.5 * 3.14159265 / 180.0 * std::cos(2 * 3.14159265 * (doy - 171.0) / 365.0);
        double dx0 = 2 * 3.14159265 * R_EARTH / W;   // m at equator
        double dy = 3.14159265 * R_EARTH / H;

        // Sea-level-equivalent temperature: radiation, diffusion, pressure,
        // and the storm gradient all operate on it, so equilibrium surface
        // temperature naturally sits 6.5 C/km below the lowlands and plateau
        // cliffs create neither false mixing nor phantom storm tracks. The
        // surface processes (evaporation, capacity, snow) use actual T.
#pragma omp parallel for
        for (int i = 0; i < W * H; i++) Tsl[i] = T[i] + 6.5 * elev[i] / 1000.0;

        // Pressure field from twice-smoothed T, then the balanced wind:
        // r*u - f v = -Px/rho ; f*u + r*v = -Py/rho.
#pragma omp parallel for
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int i = idx(x, y);
                int yn = std::min(y + 1, H - 1), ys = std::max(y - 1, 0);
                nT[i] = 0.5 * Tsl[i] + 0.125 * (Tsl[idx(wrapX(x + 1), y)] + Tsl[idx(wrapX(x - 1), y)] +
                                                Tsl[idx(x, yn)] + Tsl[idx(x, ys)]);
            }
#pragma omp parallel for
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int i = idx(x, y);
                int yn = std::min(y + 1, H - 1), ys = std::max(y - 1, 0);
                nW[i] = 0.5 * nT[i] + 0.125 * (nT[idx(wrapX(x + 1), y)] + nT[idx(wrapX(x - 1), y)] +
                                               nT[idx(x, yn)] + nT[idx(x, ys)]);
            }
#pragma omp parallel for
        for (int y = 1; y < H - 1; y++) {
            double cosl = std::max(std::cos((((y + 0.5) / (double)H) - 0.5) * 3.14159265), 0.2);
            double dx = dx0 * cosl;
            for (int x = 0; x < W; x++) {
                int i = idx(x, y);
                double px = -P_PER_DEG * (nW[idx(wrapX(x + 1), y)] - nW[idx(wrapX(x - 1), y)]) / (2 * dx);
                double py = -P_PER_DEG * (nW[idx(x, y + 1)] - nW[idx(x, y - 1)]) / (2 * dy);
                double X = -px / RHO, Y = -py / RHO;
                double f = 2 * OMEGA * std::sin(latRad[i]);
                double r = FRICTION, den = r * r + f * f;
                u[i] = (r * X + f * Y) / den;
                v[i] = (-f * X + r * Y) / den;
            }
        }
        for (int x = 0; x < W; x++) { u[idx(x, 0)] = v[idx(x, 0)] = u[idx(x, H - 1)] = v[idx(x, H - 1)] = 0; }

        // Divergence -> uplift; orographic uplift from wind into slope.
#pragma omp parallel for
        for (int y = 1; y < H - 1; y++) {
            double cosl = std::max(std::cos((((y + 0.5) / (double)H) - 0.5) * 3.14159265), 0.2);
            double dx = dx0 * cosl;
            for (int x = 0; x < W; x++) {
                int i = idx(x, y);
                double dudx = (u[idx(wrapX(x + 1), y)] - u[idx(wrapX(x - 1), y)]) / (2 * dx);
                double dvdy = (v[idx(x, y + 1)] - v[idx(x, y - 1)]) / (2 * dy);
                double wup = -(dudx + dvdy) * H_FLOW;
                double oro = (u[i] * (elev[idx(wrapX(x + 1), y)] - elev[idx(wrapX(x - 1), y)]) / (2 * dx) +
                              v[i] * (elev[idx(x, y + 1)] - elev[idx(x, y - 1)]) / (2 * dy));
                div[i] = wup + std::max(oro, 0.0) - std::max(-oro, 0.0) * 0.5;
            }
        }

        // Thermodynamics + moisture, upwind advection + diffusion.
#pragma omp parallel for
        for (int y = 1; y < H - 1; y++) {
            double cosl = std::max(std::cos((((y + 0.5) / (double)H) - 0.5) * 3.14159265), 0.2);
            double dx = dx0 * cosl;
            double kx = std::min(K_DIFF * DT / (dx * dx), 0.2), ky = std::min(K_DIFF * DT / (dy * dy), 0.2);
            double ktx = std::min(KT_DIFF * DT / (dx * dx), 0.22), kty = std::min(KT_DIFF * DT / (dy * dy), 0.22);
            for (int x = 0; x < W; x++) {
                int i = idx(x, y);
                int xe = idx(wrapX(x + 1), y), xw = idx(wrapX(x - 1), y);
                int yn = idx(x, y + 1), ys = idx(x, y - 1);
                // solar
                double lat = latRad[i];
                double ha = 2 * 3.14159265 * (hour / 24.0 + (x + 0.5) / (double)W) + 3.14159265;
                double cosz = std::sin(lat) * std::sin(dec) + std::cos(lat) * std::cos(dec) * std::cos(ha);
                double sw = SOLAR * std::max(cosz, 0.0) * (1.0 - albedo[i]);
                double olr = OLR_A + OLR_B * Tsl[i];
                // heat: radiation + diffusion only (see KT_DIFF note)
                double uMax = 0.8 * dx / DT, vMax = 0.8 * dy / DT;
                double ua = std::clamp(u[i], -uMax, uMax), va = std::clamp(v[i], -vMax, vMax);
                double difT = ktx * (Tsl[xe] + Tsl[xw] - 2 * Tsl[i]) + kty * (Tsl[yn] + Tsl[ys] - 2 * Tsl[i]);
                nT[i] = std::clamp(T[i] + (sw - olr) / heatC[i] * DT + difT, -90.0, 65.0);
                if (i == probe) { // one cell only: no write contention
                    pSw += sw / heatC[i] * DT;
                    pOlr -= olr / heatC[i] * DT;
                    pDif += difT;
                }
                // moisture: flux-form advection so convergence piles it up,
                // rain from the excess over the motion-modulated capacity
                double capMul = 1.0 - 0.5 * std::tanh(div[i] / DIV_CAP_SCALE);
                double cap = capOf(T[i]) * capMul;
                double evap = (water[i] ? EVAP_WATER : EVAP_LAND) *
                              std::max(1.0 - Wv[i] / std::max(cap, 1.0), 0.0) *
                              std::clamp(0.3 + T[i] / 25.0, 0.0, 1.5); // cold seas barely evaporate
                double rain = std::max(Wv[i] - RAIN_FRAC * cap, 0.0) * RAIN_RATE;
                if (!water[i]) rain += Wv[i] * (DT / LAND_RAINOUT_TAU);
                double gtx = (Tsl[xe] - Tsl[xw]) / (2 * dx), gty = (Tsl[yn] - Tsl[ys]) / (2 * dy);
                rain += K_STORM * std::sqrt(gtx * gtx + gty * gty) * Wv[i];
                auto face = [&](double ur, double Wl, double Wr, double dd) {
                    double uc = std::clamp(ur, -0.8 * dd / DT, 0.8 * dd / DT);
                    return (uc > 0 ? Wl : Wr) * uc / dd;
                };
                double fe = face(0.5 * (u[i] + u[xe]), Wv[i], Wv[xe], dx);
                double fw = face(0.5 * (u[xw] + u[i]), Wv[xw], Wv[i], dx);
                double fn = face(0.5 * (v[i] + v[yn]), Wv[i], Wv[yn], dy);
                double fs = face(0.5 * (v[ys] + v[i]), Wv[ys], Wv[i], dy);
                double advW = ADV_EFF * (fe - fw + fn - fs);
                double difW = kx * (Wv[xe] + Wv[xw] - 2 * Wv[i]) + ky * (Wv[yn] + Wv[ys] - 2 * Wv[i]);
                rain = std::min(rain, Wv[i]);
                nW[i] = std::clamp(Wv[i] + evap - rain - advW * DT + difW, 0.0, 90.0);
                rainStep[i] = rain;
                (void)ua; (void)va;
            }
        }
        // polar rows: copy neighbours
        for (int x = 0; x < W; x++) {
            nT[idx(x, 0)] = nT[idx(x, 1)];
            nT[idx(x, H - 1)] = nT[idx(x, H - 2)];
            nW[idx(x, 0)] = nW[idx(x, 1)];
            nW[idx(x, H - 1)] = nW[idx(x, H - 2)];
        }
        // Polar filter: the shrinking cells near the poles go unstable
        // otherwise (moisture spikes, temperature pinned at the clamp).
        // Relax the polar rows toward their zonal means, strength fading
        // with distance from the pole.
        for (int y = 0; y < H; y++) {
            int dPole = std::min(y, H - 1 - y);
            if (dPole > 5) continue;
            double f = 0.5 * (1.0 - dPole / 6.0);
            double mT = 0, mW = 0;
            for (int x = 0; x < W; x++) { mT += nT[idx(x, y)]; mW += nW[idx(x, y)]; }
            mT /= W; mW /= W;
            for (int x = 0; x < W; x++) {
                nT[idx(x, y)] += f * (mT - nT[idx(x, y)]);
                nW[idx(x, y)] += f * (mW - nW[idx(x, y)]);
            }
        }
        std::swap(T, nT);
        std::swap(Wv, nW);
    }
};

inline Climatology build(const terrain::ContinentParams& cp, float seaLevel, const float rot[9],
                         terrain::V3 offset, const plates::Field& pf, const hydrology::Result& hy,
                         bool verbose = false, void (*progress)(int day, int totalDays) = nullptr) {
    Model m;
    m.init(cp, seaLevel, rot, offset, pf, hy);
    Climatology c;
    c.elev.assign(m.elev.begin(), m.elev.end());
    std::vector<double> dayMin(W * H), dayMax(W * H);
    std::vector<double> cnt(SEASONS, 0.0);
    int totalDays = SPINUP_DAYS + STAT_YEARS * 365;
    for (int day = 0; day < totalDays; day++) {
        int doy = day % 365;
        int season = Climatology::seasonOfDay(doy);
        bool stat = day >= SPINUP_DAYS;
        std::fill(dayMin.begin(), dayMin.end(), 1e9);
        std::fill(dayMax.begin(), dayMax.end(), -1e9);
        for (int h = 0; h < 24; h++) {
            m.step(doy, h + 0.5);
            if (!stat) continue;
            for (int i = 0; i < W * H; i++) {
                dayMin[i] = std::min(dayMin[i], m.T[i]);
                dayMax[i] = std::max(dayMax[i], m.T[i]);
                int si = season * W * H + i;
                c.meanT[si] += (float)m.T[i];
                c.rainMmDay[si] += (float)(m.rainStep[i] * 24.0);      // kg/m2/h -> mm/day
                if (m.T[i] < SNOW_T) c.snowMmDay[si] += (float)(m.rainStep[i] * 24.0);
                c.rainProb[si] += m.rainStep[i] > 0.05 ? 1.0f : 0.0f;
                c.windU[si] += (float)m.u[i];
                c.windV[si] += (float)m.v[i];
                c.cloud[si] += (float)std::clamp(m.Wv[i] / capOf(m.T[i]), 0.0, 1.0);
            }
        }
        if (stat) {
            for (int i = 0; i < W * H; i++) c.diurnal[season * W * H + i] += (float)(dayMax[i] - dayMin[i]);
            cnt[season] += 1.0;
        }
        if (progress && day % 15 == 0) progress(day, totalDays);
        if (verbose && day % 30 == 0) {
            fprintf(stderr,
                    "  probe T %.1f  W %.2f rain/h %.4f  day-sums: sw %+.2f olr %+.2f dif %+.2f (K/day)%c",
                    m.T[m.probe], m.Wv[m.probe], m.rainStep[m.probe], m.pSw / 30, m.pOlr / 30,
                    m.pDif / 30, 10);
            m.pSw = m.pOlr = m.pAdv = m.pDif = 0;
            double tmin = 1e9, tmax = -1e9, umax = 0, wmax = 0;
            for (int i = 0; i < W * H; i++) {
                tmin = std::min(tmin, m.T[i]);
                tmax = std::max(tmax, m.T[i]);
                umax = std::max(umax, std::fabs(m.u[i]) + std::fabs(m.v[i]));
                wmax = std::max(wmax, m.Wv[i]);
            }
            fprintf(stderr, "atmo: day %d/%d  T [%.0f, %.0f]  |u|max %.0f  Wmax %.0f\n",
                    day, totalDays, tmin, tmax, umax, wmax);
        }
    }
    for (int s = 0; s < SEASONS; s++) {
        double hours = cnt[s] * 24.0;
        for (int i = 0; i < W * H; i++) {
            int si = s * W * H + i;
            c.meanT[si] /= (float)hours;
            c.rainMmDay[si] /= (float)hours;
            c.snowMmDay[si] /= (float)hours;
            c.rainProb[si] /= (float)hours;
            c.windU[si] /= (float)hours;
            c.windV[si] /= (float)hours;
            c.cloud[si] /= (float)hours;
            c.diurnal[si] /= (float)cnt[s];
        }
    }
    // A mild Gaussian pass (sigma ~ one cell) over every field: transition
    // width on the map is ramp width over gradient, so softening gradients
    // widens the visible bands without erasing the coast/interior structure.
    // Elevation is blurred identically so the lapse correction stays honest.
    auto blur = [&](std::vector<float>& v, int bands) {
        std::vector<float> t(v.size());
        for (int b = 0; b < bands; b++)
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++) {
                    double sum = 0, wsum = 0;
                    for (int dy = -1; dy <= 1; dy++) {
                        int yy = y + dy;
                        if (yy < 0 || yy >= H) continue;
                        for (int dx = -1; dx <= 1; dx++) {
                            double wgt = (dx == 0 ? 2.0 : 1.0) * (dy == 0 ? 2.0 : 1.0);
                            sum += wgt * v[b * W * H + yy * W + wrapX(x + dx)];
                            wsum += wgt;
                        }
                    }
                    t[b * W * H + y * W + x] = (float)(sum / wsum);
                }
        v = t;
    };
    for (auto* v : {&c.meanT, &c.rainMmDay, &c.snowMmDay, &c.rainProb, &c.windU, &c.windV,
                    &c.cloud, &c.diurnal})
        blur(*v, SEASONS);
    blur(c.elev, 1);
    c.elevRep.clear();
    c.elev4(); // build the repeated view now, before parallel consumers race the lazy path

    return c;
}

inline terrain::V3 unitAt(float latRad, float lonRad) {
    return {std::cos(latRad) * std::cos(lonRad), std::cos(latRad) * std::sin(lonRad),
            std::sin(latRad)};
}

// The coarse climate grid shows through as straight bilinear creases if
// sampled directly, so every climate lookup goes through a small noise warp
// (~60 km) that turns grid lines into organic wiggles, plus bilinear
// interpolation. Mirrored in the shader.
inline terrain::V3 climFuzz(terrain::V3 n) {
    terrain::V3 o = {terrain::fbm(n * 23.0f + 5.0f, 2, 0.5f),
                     terrain::fbm(n * 23.0f + 11.0f, 2, 0.5f),
                     terrain::fbm(n * 23.0f + 17.0f, 2, 0.5f)};
    terrain::V3 r = n + o * 0.010f;
    float l = std::sqrt(terrain::dot(r, r));
    return {r.x / l, r.y / l, r.z / l};
}

// Bilinear sample of one season band of a climatology field at a (fuzzed)
// unit-sphere position.
inline float bilinearAt(const std::vector<float>& v, int season, terrain::V3 n) {
    float lat = std::asin(std::clamp(n.z, -1.0f, 1.0f));
    float lon = std::atan2(n.y, n.x);
    float u = ((lon + 3.14159265f) / (2 * 3.14159265f)) * W - 0.5f;
    float vv = ((lat + 3.14159265f / 2) / 3.14159265f) * H - 0.5f;
    int x0 = (int)std::floor(u), y0 = (int)std::floor(vv);
    float fx = u - x0, fy = vv - y0;
    auto at = [&](int xx, int yy) {
        xx = (xx % W + W) % W;
        yy = std::clamp(yy, 0, H - 1);
        return v[season * W * H + yy * W + xx];
    };
    return (at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx) * (1 - fy) +
           (at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx) * fy;
}

inline float annualAt(const std::vector<float>& v, terrain::V3 n) {
    float t = 0;
    for (int se = 0; se < SEASONS; se++) t += bilinearAt(v, se, n) / SEASONS;
    return t;
}

// Season-interpolated field at a fuzzed position.
inline float seasonalAt(const std::vector<float>& v, terrain::V3 n, double now) {
    double sf = std::fmod(now, 365.0) / 365.0 * 4.0 - 0.5;
    int s0 = ((int)std::floor(sf) % 4 + 4) % 4, s1 = (s0 + 1) % 4;
    float f = (float)(sf - std::floor(sf));
    return bilinearAt(v, s0, n) * (1 - f) + bilinearAt(v, s1, n) * f;
}

// Annual water balance (rain - PET, mm/day) at a lat/lon, nearest cell.
inline float annualBalanceAt(const Climatology& c, float latRad, float lonRad) {
    if (c.rainMmDay.empty()) return 0.0f;
    terrain::V3 n = climFuzz(unitAt(latRad, lonRad));
    float rain = annualAt(c.rainMmDay, n);
    float t = annualAt(c.meanT, n);
    return rain - std::max(0.4f, 0.11f * (t + 8.0f));
}

// The derived climate fields that retire the painted temperatureC /
// moistureAt (Design/Weather.md, the unification): annual mean temperature
// lapse-corrected to local height, and moisture as an aridity index
// (rain / potential evapotranspiration) with mirrored detail noise.
inline float derivedTempC(const Climatology& c, float latRad, float lonRad, float hLocal) {
    if (c.meanT.empty()) return terrain::temperatureC(latRad, hLocal);
    terrain::V3 n = climFuzz(unitAt(latRad, lonRad));
    return annualAt(c.meanT, n) -
           6.5f * (std::max(hLocal, 0.0f) - annualAt(c.elev4(), n)) / 1000.0f;
}

// Coldest-season surface temperature: the Koppen-style gate for rainforest
// (a true tropical climate never cools off).
inline float coldestSeasonTempC(const Climatology& c, float latRad, float lonRad, float hLocal) {
    if (c.meanT.empty()) return terrain::temperatureC(latRad, hLocal) - 4.0f;
    terrain::V3 n = climFuzz(unitAt(latRad, lonRad));
    float t = 1e9f;
    for (int se = 0; se < SEASONS; se++) t = std::min(t, bilinearAt(c.meanT, se, n));
    return t - 6.5f * (std::max(hLocal, 0.0f) - annualAt(c.elev4(), n)) / 1000.0f;
}

inline float derivedMoisture(const Climatology& c, float latRad, float lonRad, terrain::V3 w,
                             float hLocal) {
    if (c.rainMmDay.empty()) return terrain::moistureAt(w, latRad);
    terrain::V3 n = climFuzz(unitAt(latRad, lonRad));
    float rain = annualAt(c.rainMmDay, n);
    float t = derivedTempC(c, latRad, lonRad, hLocal);
    float pet = std::max(0.4f, 0.11f * (t + 8.0f));
    float m = std::clamp(0.5f * rain / pet, 0.0f, 1.0f);
    return std::clamp(m + terrain::moistureDetail(w), 0.0f, 1.0f);
}

// All derived climate values at a point with a single fuzz + sample pass:
// the per-cell consumers (population yields, tooltip) were paying for the
// fuzz noise four times over.
struct DerivedClimate {
    float temp, moist, tCold, swamp;
};

inline float swampFromBalance(float b) {
    float x = std::clamp((b - 1.5f) / (4.0f - 1.5f), 0.0f, 1.0f);
    return 0.45f * x * x * (3 - 2 * x);
}

inline DerivedClimate deriveAt(const Climatology& c, float latRad, float lonRad, terrain::V3 w,
                               float hLocal) {
    DerivedClimate d{};
    if (c.meanT.empty()) {
        d.temp = terrain::temperatureC(latRad, hLocal);
        d.moist = terrain::moistureAt(w, latRad);
        d.tCold = d.temp - 4.0f;
        d.swamp = 0.0f;
        return d;
    }
    terrain::V3 n = climFuzz(unitAt(latRad, lonRad));
    float coarseE = annualAt(c.elev4(), n);
    float lapse = 6.5f * (std::max(hLocal, 0.0f) - coarseE) / 1000.0f;
    float annT = 0, rain = 0, tMin = 1e9f;
    for (int se = 0; se < SEASONS; se++) {
        float t = bilinearAt(c.meanT, se, n);
        annT += t / SEASONS;
        tMin = std::min(tMin, t);
        rain += bilinearAt(c.rainMmDay, se, n) / SEASONS;
    }
    d.temp = annT - lapse;
    d.tCold = tMin - lapse;
    float pet = std::max(0.4f, 0.11f * (d.temp + 8.0f));
    d.moist = std::clamp(std::clamp(0.5f * rain / pet, 0.0f, 1.0f) + terrain::moistureDetail(w),
                         0.0f, 1.0f);
    d.swamp = swampFromBalance(rain - std::max(0.4f, 0.11f * (annT + 8.0f)));
    return d;
}

// Waterlogging 0..1 from the balance: the marsh pull in terrain::mixtureAt.
inline float swampinessAt(const Climatology& c, float latRad, float lonRad) {
    return swampFromBalance(annualBalanceAt(c, latRad, lonRad));
}

} // namespace atmosphere
