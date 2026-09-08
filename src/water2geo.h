// The water in two layers, on the geodesic grid, riding the two QG winds.
//
// The single column carried by the surface wind had to fake everything
// that happens above the boundary layer with a lowered ceiling and an
// hourly rate: a parcel lifted over a ridge stayed in the surface layer
// and carried its excess into the rain shadow. Here the lower layer's
// vapour rides the lower QG wind, the upper layer's rides the upper, and
// the lift moves water between them:
//
//   from the dynamics   the two-layer model's own interface velocity, the
//                       material change of the baroclinic streamfunction
//                       less its diabatic relaxation, times 2 f0 / g'
//   from the terrain    the lower wind against the slope
//   from the painting   the belt ascent and convection inside the tropics,
//                       where the QG flow is damped and not believed
//
// Rising air lifts the top of the lower column, whose vapour density at
// the interface is LIFT_SHARE of the layer's mean (a 2.2 km scale height
// over a 5 km layer); sinking air brings the upper layer's mean down.
// Each layer condenses against its own saturation -- the lower at the
// painted surface air, the upper at the air 30 K colder above it -- with
// the same sub-grid onset the column had, and the excess above saturation
// falls at once. Rain is the sum. Evaporation enters the lower layer.
// Advection is upwind on the corner flux, so nothing goes negative and
// nothing is minted.
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
#include "qg2geo.h"

namespace water2 {

inline double CAP0 = 31.0, CAP_T0 = 15.0, CAP_SCALE = 14.4; // saturation column water, mm, at the surface air's temperature
inline double BL_LAPSE = 3.5;        // K, the surface air is this much warmer than the layer it caps (as atmosphere.h)
inline double LOWER_SHARE = 0.9;     // of the saturated column that sits below 5 km
inline double UPPER_DT = 30.0;       // K, the upper layer's base is this much colder than the surface air
inline double UPPER_SHARE = 0.9;     // of a column started at that temperature that sits in the layer
inline double LIFT_SHARE = 0.26;     // vapour density at the interface over the lower layer's mean
inline double W_TERRAIN_MAX = 1.0;   // m/s, cap on the slope lift
inline double RAIN_ONSET = 0.6;      // the sub-grid onset, as a share of saturation
inline double RAIN_RATE = 0.03;      // per hour, of the capacity, at full excess
inline double TAU_DIABATIC = 20.0 * 86400.0; // the QG relaxation, subtracted from the lift (qg2geo::TAU_RELAX)
// Rain within a layer: air lifted inside the lower layer condenses at 1-3
// km long before the layer's whole column is saturated. A parcel rising
// at the interface velocity for UPLIFT_TAU has climbed dz and cooled by
// the moist lapse, and the layer's capacity for the RATE falls with it;
// the hard ceiling stays at the layer's true saturation, or every coast
// would dump its column on the first cell (measured on the lat-lon grid).
// Without this the lift alone moved 0.005 mm/h across 5 km and the
// mid-latitudes stayed dry.
inline double UPLIFT_TAU = 86400.0;  // s
inline double H_LIFT_MAX = 3000.0;   // m
inline double LAPSE_MOIST = 0.006;   // K/m
// No ascent, no rain. The rate term alone let a moist subtropical column
// rain 2-3 mm/day with nothing lifting it, and every desert was wet; a
// real desert holds 15-20 mm under a subsidence inversion and rains
// nothing. The rate scales with the ascent up to W_RATE, with a drizzle
// floor, and descending air, warmed, may hold more than the painted
// temperature says (the foehn), up to DESCENT_CAP of its saturation.
inline double W_RATE = 0.02;         // m/s of ascent at which the rate is full
inline double DRIZZLE = 0.1;         // the rate's floor, as a share
inline double DESCENT_CAP = 1.5;     // the ceiling over saturation that descent may reach

inline double capOf(double T) { return CAP0 * std::exp((T - CAP_T0) / CAP_SCALE); }

struct Model {
    const qg2geo::Model* qg = nullptr;
    int N = 0;
    std::vector<double> wl, wu;                 // mm, the two layers' vapour
    std::vector<double> capL, capU;             // mm
    std::vector<double> evapIn;                 // mm per hour, into the lower layer
    std::vector<double> wPaint;                 // m/s, the painted ascent for the tropics
    std::vector<double> elev;                   // m, the real terrain
    std::vector<unsigned char> water;
    std::vector<double> psicPrev, wIface, rainHour, rainLHour, rainUHour, liftHour, tmpL, tmpU;
    bool started = false;

    void init(const qg2geo::Model& model, const std::vector<float>& elevMesh, const std::vector<unsigned char>& waterMesh) {
        qg = &model; N = qg->N;
        for (auto* v : {&wl, &wu, &capL, &capU, &evapIn, &wPaint, &elev, &psicPrev, &wIface, &rainHour, &rainLHour, &rainUHour, &liftHour, &tmpL, &tmpU})
            v->assign(N, 0.0);
        water = waterMesh;
        for (int i = 0; i < N; i++) elev[i] = std::max((double)elevMesh[i], 0.0);
    }

    // once an hour, from the painted fields
    void setInputs(const std::vector<double>& tbMesh, const std::vector<double>& evapMesh, const std::vector<double>& wUpMesh) {
        for (int i = 0; i < N; i++) {
            capL[i] = LOWER_SHARE * capOf(tbMesh[i] + BL_LAPSE);
            capU[i] = UPPER_SHARE * capOf(tbMesh[i] + BL_LAPSE - UPPER_DT);
            evapIn[i] = evapMesh[i];
            wPaint[i] = wUpMesh[i];
        }
        if (!started) {
            for (int i = 0; i < N; i++) { wl[i] = 0.5 * capL[i]; wu[i] = 0.3 * capU[i]; }
            started = true;
        }
        std::fill(rainHour.begin(), rainHour.end(), 0.0);
        std::fill(rainLHour.begin(), rainLHour.end(), 0.0);
        std::fill(rainUHour.begin(), rainUHour.end(), 0.0);
        std::fill(liftHour.begin(), liftHour.end(), 0.0);
    }

    void beginStep() { psicPrev = qg->psic; }

    // after qg->step(dt): the lift this step made, then the water's own step
    void step(double dt) {
        const geodesic::Grid& g = qg->g;
        double R = qg2geo::A_EARTH;
        // the interface velocity
#pragma omp parallel for
        for (int i = 0; i < N; i++) {
            double f0s = qg->invF0[i] * qg->f0 * qg->f0;      // signed f0
            double dpsic = (qg->psic[i] - psicPrev[i]) / dt;
            double adv = -qg->advection(qg->psib, qg->psic, i);  // V . grad psic
            double diab = (qg->psicT[i] - qg->psic[i]) / TAU_DIABATIC * (1.0 + qg2geo::TROP_RELAX * qg->trop[i]);
            double wq = 2.0 * f0s / qg2geo::G_REDUCED * (dpsic + adv - diab);
            geodesic::D3 gz = geodesic::grad(g, elev, i, R);
            double wt = std::clamp(geodesic::dot(qg->V2[i], gz), -W_TERRAIN_MAX, W_TERRAIN_MAX);
            double t = qg->trop[i];
            wIface[i] = (1.0 - t) * wq + t * wPaint[i] + wt;
        }
        // advection, upwind on the corner flux
#pragma omp parallel for
        for (int i = 0; i < N; i++) {
            double sl = 0, su = 0; int d = g.deg(i), b = g.nbrStart[i];
            for (int k = 0; k < d; k++) {
                int j = g.nbr[b + k];
                double f2 = qg->cornerFlux(qg->psi2, i, k), f1 = qg->cornerFlux(qg->psi1, i, k);
                sl += (f2 > 0 ? wl[i] : wl[j]) * f2;
                su += (f1 > 0 ? wu[i] : wu[j]) * f1;
            }
            double a = g.area[i] * R * R;
            tmpL[i] = -sl / a * dt;
            tmpU[i] = -su / a * dt;
        }
        double hours = dt / 3600.0;
#pragma omp parallel for
        for (int i = 0; i < N; i++) {
            double l = std::max(wl[i] + tmpL[i], 0.0), u = std::max(wu[i] + tmpU[i], 0.0);
            // the lift
            double w = wIface[i];
            double frac = std::min(std::fabs(w) * dt / qg2geo::H_LAYER, 1.0);
            if (w > 0) { double x = l * LIFT_SHARE * frac; l -= x; u += x; liftHour[i] += x; }
            else { double x = u * frac; u -= x; l += x; liftHour[i] -= x; }
            // evaporation
            l += evapIn[i] * hours;
            // condensation, each layer against its own saturation
            double dz = std::clamp(w * UPLIFT_TAU, -H_LIFT_MAX, H_LIFT_MAX);
            double liftFactor = std::exp(-LAPSE_MOIST * dz / CAP_SCALE);
            double ascent = std::clamp(DRIZZLE + w / W_RATE, DRIZZLE, 1.0);
            auto condense = [&](double& wv, double cap) {
                double capRate = std::min(cap, cap * liftFactor);
                double ceiling = cap * std::clamp(liftFactor, 1.0, DESCENT_CAP);
                double sat = wv / std::max(capRate, 0.02);
                double sx = std::clamp((sat - RAIN_ONSET) / (1.0 - RAIN_ONSET), 0.0, 1.0);
                double r = RAIN_RATE * hours * capRate * sx * sx * ascent;
                if (wv - r > ceiling) r += wv - r - ceiling;
                r = std::min(r, wv);
                wv -= r;
                return r;
            };
            double rl = condense(l, capL[i]);
            double ru = condense(u, capU[i]);
            wl[i] = l; wu[i] = u;
            rainLHour[i] += rl; rainUHour[i] += ru; rainHour[i] += rl + ru;
        }
    }
};

} // namespace water2
