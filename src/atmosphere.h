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
// A grey one-layer atmosphere, replacing outgoing = A + B*T (Budyko, 2026-09-01).
//
// That form is a GLOBAL feedback: it says what the planet as a whole does
// when its mean temperature moves. Used cell by cell it says a surface must
// warm 50 K to shed 100 W/m2, which is not what ground does -- ground
// convects into the air above it in minutes. Every honest flux added to the
// old budget therefore produced a huge excursion, and the model only looked
// reasonable because it never reached equilibrium: the measured summer
// maxima ran 15-20 degC hot, and winters to the -90 floor because the ground
// radiated to space with nothing above it.
//
// Instead: the surface radiates sigma*T^4 upward, the air absorbs a share of
// it and radiates the same from BOTH faces -- up to space and back down --
// which is the greenhouse in one honest number. Local damping is then about
// 5 W/m2/K of radiation plus 15 of convection, twenty times the old figure,
// and it stiffens as things warm, which is what stops a runaway.
constexpr double SIGMA = 5.670374e-8;
constexpr double EMISS = 0.955;        // how much of the surface's longwave the air holds
constexpr double C_AIR = 1.0e7;        // J/m2/K: cp * p / g, the whole column
constexpr double K_SURF_AIR = 15.0;    // W/m2/K, convection into the air above
// A one-layer atmosphere radiates from its middle, so Ta is a mid-troposphere
// temperature -- around -30 degC on a planet whose ground is at +15. The
// surface is warmer than that by the lapse rate through the depth between
// them, and convection only carries what is ABOVE that difference. Coupling
// the two without it drains some 600 W/m2 out of the ground: measured, and
// it put 60N summer at -8 degC.
constexpr double LAPSE_OFFSET = 27.0;  // K, surface warmer than the emitting level
// Convection is a one-way street. Ground warmer than the air above it boils
// heat upward; ground colder than the air sits under an inversion and barely
// exchanges at all -- which is exactly what a polar winter night is, and why
// it gets so cold. Treating both directions alike held polar winters at
// -7 degC by pouring heat back down out of the air.
constexpr double K_STABLE = 1.5;       // W/m2/K under an inversion
// Evaporation carries heat as well as water: it leaves the surface with the
// vapour and the air gets it back where that vapour condenses. This is the
// second-largest heat transport on the planet, and the reason a wet surface
// is cooler than a dry one under the same sun.
constexpr double LATENT_J_PER_KG = 2.45e6;
// And it has to be paid for. Unpriced, the water cycle is a pump: the model
// ran at 7 mm a day of global rain against Earth's 2.7, which is 200 W/m2 of
// latent flux conjured from nothing. A surface can only evaporate what the
// sun gave it plus what it stored.
constexpr double STORED_FLUX_WATER = 60.0, STORED_FLUX_LAND = 10.0; // W/m2
// Land cannot evaporate what it has not been given, either. Without a store
// to draw on the driest air draws the most water, so a desert would cool
// itself harder than a rainforest.
constexpr double SOIL_CAP_MM = 120.0, SOIL_REF_MM = 40.0;
// Melting holds a surface at freezing: ice takes 334 kJ/kg without changing
// temperature, which is why a polar summer sits near zero however long the
// sun is up.
constexpr double MELT_DAMP = 0.12;
// Snow and ice reflect most of what falls on them, and the albedo field was
// static -- painted once from an analytic first guess, so nothing got
// brighter when it froze. That is a real feedback and a strong one: it is
// most of why a polar summer stays cold.
constexpr double ALBEDO_SNOW = 0.62, ALBEDO_SEAICE = 0.55;
// Ramped, not switched. A hard step at freezing is a trapdoor: cross it once
// and the extra reflection keeps you below it, and the whole mid-latitude
// world locks into a snowball -- measured, at -17 degC in midsummer. Cover
// builds up over several degrees, as it does in life.
constexpr double SNOW_FULL_C = -8.0, SNOW_NONE_C = 2.0;
// Clouds reflect about a fifth of the sunlight. With only ground albedo the
// model absorbs some 300 W/m2 against Earth's 240, and no greenhouse setting
// can balance that.
constexpr double CLOUD_ALBEDO = 0.22;
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
// Earth evaporates about 2.7 mm a day over its whole surface, 3.2 over the
// oceans. At 0.20 this model asked for 7 once the heat was allowed to follow
// the water, and the heat released where that rain fell cooked the poles to
// +50 degC.
constexpr double EVAP_WATER = 0.09, EVAP_LAND = 0.025;         // kg/m^2 per h at full deficit
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
constexpr double KT_DIFF = 1.1e6;               // m^2/s eddy diffusion of heat

struct Climatology {
    double dbgEvap = 0, dbgRain = 0, dbgClamp = 0; // PROBE: is water conserved?
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
    std::vector<double> Ta, nTa, Tasl; // the air: its own heat, and reduced to sea level
    std::vector<double> soil;          // land water store, mm: what there is to evaporate
    double dbgEvap = 0, dbgRain = 0, dbgClamp = 0; // PROBE: is water conserved?
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
        Ta = T; // the air starts wherever the ground is
        nTa.assign(W * H, 0.0);
        Tasl.assign(W * H, 0.0);
        soil.assign(W * H, SOIL_REF_MM); // half full; the spin-up settles it
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
        for (int i = 0; i < W * H; i++) {
            Tsl[i] = T[i] + 6.5 * elev[i] / 1000.0;
            Tasl[i] = Ta[i] + 6.5 * elev[i] / 1000.0;
        }

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
            // Moisture mixing, capped well below the old 0.2: at that rate two
            // fifths of a cell's water crossed into its neighbours every hour,
            // which is a pipeline rather than a diffusion, and it flooded the
            // poles with water no polar air could hold.
            double kx = std::min(K_DIFF * DT / (dx * dx), 0.09), ky = std::min(K_DIFF * DT / (dy * dy), 0.09);
            double ktx = std::min(KT_DIFF * DT / (dx * dx), 0.22), kty = std::min(KT_DIFF * DT / (dy * dy), 0.22);
            for (int x = 0; x < W; x++) {
                int i = idx(x, y);
                int xe = idx(wrapX(x + 1), y), xw = idx(wrapX(x - 1), y);
                int yn = idx(x, y + 1), ys = idx(x, y - 1);
                // solar
                double lat = latRad[i];
                double ha = 2 * 3.14159265 * (hour / 24.0 + (x + 0.5) / (double)W) + 3.14159265;
                double cosz = std::sin(lat) * std::sin(dec) + std::cos(lat) * std::cos(dec) * std::cos(ha);
                double white = std::clamp((SNOW_NONE_C - T[i]) / (SNOW_NONE_C - SNOW_FULL_C),
                                          0.0, 1.0);
                double alb = albedo[i] + white * ((water[i] ? ALBEDO_SEAICE : ALBEDO_SNOW) -
                                                  albedo[i]);
                double sw = SOLAR * std::max(cosz, 0.0) * (1.0 - alb) * (1.0 - CLOUD_ALBEDO);
                // Longwave, both ways. The air holds EMISS of what the ground
                // sends up and radiates that much again from each of its two
                // faces: half to space, half back down. The half coming down
                // is the greenhouse, and it is what the old budget had no way
                // to express.
                double Tk = T[i] + 273.15, Tak = Ta[i] + 273.15;
                double lwUp = SIGMA * Tk * Tk * Tk * Tk;
                double lwDown = EMISS * SIGMA * Tak * Tak * Tak * Tak;
                double lapseGap = T[i] - Ta[i] - LAPSE_OFFSET;
                double sens = (lapseGap > 0 ? K_SURF_AIR : K_STABLE) * lapseGap;
                // Evaporation, priced: what the air can still hold, what the
                // ground has to give, and what the sun can pay for.
                double capMul = 1.0 - 0.5 * std::tanh(div[i] / DIV_CAP_SCALE);
                double cap = capOf(T[i]) * capMul;
                double supply = water[i] ? 1.0 : std::clamp(soil[i] / SOIL_REF_MM, 0.0, 1.0);
                double evap = (water[i] ? EVAP_WATER : EVAP_LAND) * supply *
                              std::max(1.0 - Wv[i] / std::max(cap, 1.0), 0.0) *
                              std::clamp(0.3 + T[i] / 25.0, 0.0, 1.5);
                // What the sun pays over a whole day, not what it pays at noon:
                // capping against the instantaneous figure lets the daylight
                // hours evaporate three or four times a day's worth of water.
                double h0 = std::acos(std::clamp(-std::tan(lat) * std::tan(dec), -1.0, 1.0));
                double swDay = SOLAR / 3.14159265 *
                               (h0 * std::sin(lat) * std::sin(dec) +
                                std::cos(lat) * std::cos(dec) * std::sin(h0)) *
                               (1.0 - alb) * (1.0 - CLOUD_ALBEDO);
                double afford = std::max(swDay, 0.0) +
                                (water[i] ? STORED_FLUX_WATER : STORED_FLUX_LAND);
                double lFlux = evap * LATENT_J_PER_KG / DT;
                if (lFlux > afford) {
                    evap *= afford / lFlux;
                    lFlux = afford;
                }
                // heat: radiation + diffusion only (see KT_DIFF note). The air
                // is what moves heat sideways now; the ground follows the air
                // above it.
                double uMax = 0.8 * dx / DT, vMax = 0.8 * dy / DT;
                double ua = std::clamp(u[i], -uMax, uMax), va = std::clamp(v[i], -vMax, vMax);
                double difT = ktx * (Tasl[xe] + Tasl[xw] - 2 * Tasl[i]) +
                              kty * (Tasl[yn] + Tasl[ys] - 2 * Tasl[i]);
                double dT = (sw - lwUp + lwDown - sens - lFlux) / heatC[i] * DT;
                if (water[i] && T[i] > -2.0 && T[i] < 2.0 && dT > 0) dT *= MELT_DAMP;
                nT[i] = std::clamp(T[i] + dT, -90.0, 65.0);
                // The air keeps what the ground gave it and what the rain
                // released, and radiates from both its faces.
                double condense = rainStep[i] * LATENT_J_PER_KG / C_AIR;
                nTa[i] = std::clamp(Ta[i] + (EMISS * lwUp - 2.0 * lwDown + sens) / C_AIR * DT +
                                        condense + difT,
                                    -95.0, 70.0);
                if (i == probe) { // one cell only: no write contention
                    pSw += sw / heatC[i] * DT;
                    pOlr -= (lwUp - lwDown) / heatC[i] * DT;
                    pDif += difT;
                }
                // moisture: flux-form advection so convergence piles it up,
                // rain from the excess over the motion-modulated capacity
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
                // Flux form with per-face CFL limiting still lets four faces
                // between them export more than the cell contains, and the
                // clamp below then invents the shortfall. Measured: rain came
                // to 4.6x evaporation, and the whole difference was made at
                // that clamp -- water conjured, rained out, and (once latent
                // heat was coupled) used to cook the poles to +50 degC.
                double advW = ADV_EFF * (fe - fw + fn - fs);
                advW = std::min(advW, 0.5 * Wv[i] / DT);
                double difW = kx * (Wv[xe] + Wv[xw] - 2 * Wv[i]) + ky * (Wv[yn] + Wv[ys] - 2 * Wv[i]);
                rain = std::min(rain, Wv[i]);
                // Rain cannot exceed the water that is actually here. Advection
                // and diffusion between them can ask for more than the cell
                // holds, and clamping the result at zero used to invent the
                // difference: measured at 4.6 times the world's evaporation,
                // conjured, rained out, and -- once rain carried heat -- used
                // to cook the poles to +50 degC. Take the shortfall out of the
                // rain, which is the term that matters, before the clamp.
                double raw = Wv[i] + evap - rain - advW * DT + difW;
                if (raw < 0.0) {
                    rain = std::max(0.0, rain + raw);
                    raw = 0.0;
                }
                nW[i] = std::clamp(raw, 0.0, 90.0);
                if (!water[i]) soil[i] = std::clamp(soil[i] + rain - evap, 0.0, SOIL_CAP_MM);
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
        std::swap(Ta, nTa);
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
    c.dbgEvap = m.dbgEvap;
    c.dbgRain = m.dbgRain;
    c.dbgClamp = m.dbgClamp;
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

// Season-interpolated surface temperature (fuzzed, bilinear, lapse-corrected).
inline float seasonalTempC(const Climatology& c, terrain::V3 nRaw, float hLocal, double now) {
    if (c.meanT.empty()) return 10.0f;
    terrain::V3 nf = climFuzz(nRaw);
    float t = seasonalAt(c.meanT, nf, now);
    return t - 6.5f * (std::max(hLocal, 0.0f) - annualAt(c.elev4(), nf)) / 1000.0f;
}

// Growing activity from temperature: nothing grows at freezing, full growth
// above ~12 C. Foraging keeps a 12% winter floor (what stays huntable).
inline float growthActivity(float tC) { return std::clamp(tC / 12.0f, 0.0f, 1.0f); }
inline float forageFactor(float tC) { return 0.12f + 0.88f * growthActivity(tC); }

// Annual means of the forage factor and squared activity (the farming shape),
// from the four season bands at a settlement's site.
// The full seasonal profile at a fixed site: the four season temperatures
// (cached per settlement -- their site never moves, so the expensive fuzz
// and bilinear sampling happens once, not per integration substep) plus the
// annual means derived from them.
inline void seasonProfile(const Climatology& c, terrain::V3 nRaw, float hLocal, float tOut[4],
                          float& meanF, float& meanG2) {
    meanF = 1.0f;
    meanG2 = 1.0f;
    for (int se = 0; se < SEASONS; se++) tOut[se] = 15.0f;
    if (c.meanT.empty()) return;
    terrain::V3 nf = climFuzz(nRaw);
    float lapse = 6.5f * (std::max(hLocal, 0.0f) - annualAt(c.elev4(), nf)) / 1000.0f;
    meanF = 0;
    meanG2 = 0;
    for (int se = 0; se < SEASONS; se++) {
        float t = bilinearAt(c.meanT, se, nf) - lapse;
        tOut[se] = t;
        float g = growthActivity(t);
        meanF += forageFactor(t) / SEASONS;
        meanG2 += g * g / SEASONS;
    }
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
