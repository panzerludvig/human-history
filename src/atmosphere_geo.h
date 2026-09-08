// The atmosphere, on the geodesic grid.
//
// Same physics as atmosphere.h -- every constant, every closed-form helper,
// emissOf and capOf and cloudOf, is included from there rather than copied,
// because none of it ever depended on the grid. What changes is the
// discretisation, and what that removes is the point:
//
//   no pole rows           no cap cells to fill by copying a neighbour, and so
//                          no infinite reservoir manufacturing 45 mm/day of
//                          water or a 280 hPa polar high
//   no wraparound column   the mesh closes on itself; there is no seam
//   no cosine weights      areas and edge lengths are stored, not derived from
//                          a latitude that goes singular
//   no polar filter        one timestep is stable everywhere, so nothing needs
//                          smoothing to survive, and the filter that removed
//                          every mode except the jet is gone with it
//   no metric terms        vectors are 3D and tangent, so the (f + u tan(phi)/a)
//                          curvature term never arises -- Coriolis is
//                          -2 Omega x v, projected, and that is all of it
//
// Five subsystems that existed only to paper over a singular coordinate
// system, and none of them has an equivalent here.
#pragma once
#include "atmosphere.h"
#include "geodesic.h"
#include "hydrology.h"
#include "terrain.h"

namespace atmogeo {

using geodesic::D3;
using geodesic::cross;
using geodesic::dot;
using geodesic::len;
using geodesic::norm;
using geodesic::tangent;

namespace A = atmosphere;

constexpr int LEVEL = 5;              // 10242 cells at about 240 km
constexpr double R_E = A::R_EARTH;
constexpr int DYN_SUB = 6;            // 600 s each: the CFL here allows 609

// The grid is expensive to relax and never changes, so it is built once.
inline const geodesic::Grid& grid() {
    static geodesic::Grid g = geodesic::build(LEVEL);
    return g;
}

// Nearest cell to a direction, by walking downhill through the neighbours.
// A hint from a nearby query turns this into two or three steps.
inline int nearest(const geodesic::Grid& g, D3 p, int hint) {
    int cur = (hint >= 0 && hint < g.size()) ? hint : 0;
    double best = dot(g.c[cur], p);
    for (int guard = 0; guard < 512; guard++) {
        int next = cur;
        for (int k = g.nbrStart[cur]; k < g.nbrStart[cur + 1]; k++) {
            double d = dot(g.c[g.nbr[k]], p);
            if (d > best) { best = d; next = g.nbr[k]; }
        }
        if (next == cur) break;
        cur = next;
    }
    return cur;
}

struct Model {
    const geodesic::Grid* g = nullptr;
    int N = 0;

    // surface, fixed
    std::vector<float> elev, albedo, heatC;
    std::vector<char> water;

    // state
    std::vector<double> T, Ta, Wv, soil, hP, hU, capEff, cloudF, divSm, rainStep, hWant;
    std::vector<D3> U1, U2;
    // scratch
    std::vector<double> nT, nTa, nW, nhP, nhU, sc1, sc2;
    std::vector<D3> nU1, nU2, F1, F2;
    std::vector<double> flux, cellOut;

    void init(const terrain::ContinentParams& cp, float seaLevel, const float rot[9],
              terrain::V3 offset, const plates::Field& pf, const hydrology::Result& hy) {
        g = &grid();
        N = g->size();
        elev.assign(N, 0.0f);
        albedo.assign(N, 0.2f);
        heatC.assign(N, (float)A::C_LAND);
        water.assign(N, 0);

        // Hydrology is a lat-lon raster; scatter it onto the cells rather than
        // gathering, so every source pixel lands somewhere and none is missed.
        std::vector<double> hSum(N, 0.0), hLand(N, 0.0), hAll(N, 0.0);
        {
            int hint = 0;
            for (int y = 0; y < hydrology::H; y++) {
                double lat = ((y + 0.5) / hydrology::H - 0.5) * 3.14159265;
                for (int x = 0; x < hydrology::W; x++) {
                    double lon = ((x + 0.5) / hydrology::W * 2.0 - 1.0) * 3.14159265;
                    D3 n{std::cos(lat) * std::cos(lon), std::cos(lat) * std::sin(lon),
                         std::sin(lat)};
                    hint = nearest(*g, n, hint);
                    float h = hy.heightM[y * hydrology::W + x];
                    hAll[hint] += 1;
                    if (h > 0) { hLand[hint] += 1; hSum[hint] += h; }
                }
            }
        }
        for (int i = 0; i < N; i++) {
            double frac = hAll[i] > 0 ? hLand[i] / hAll[i] : 0.0;
            water[i] = frac < 0.5 ? 1 : 0;
            elev[i] = water[i] ? 0.0f : (float)(hSum[i] / std::max(hLand[i], 1.0));
            float lat = (float)std::asin(std::clamp(g->c[i].z, -1.0, 1.0));
            terrain::V3 n{(float)g->c[i].x, (float)g->c[i].y, (float)g->c[i].z};
            terrain::V3 w = terrain::rotate(rot, n) + offset;
            float temp = terrain::temperatureC(lat, elev[i]);
            if (water[i]) {
                albedo[i] = temp < -8 ? 0.55f : 0.08f;
                heatC[i] = (float)A::C_WATER;
            } else {
                float moist = terrain::moistureAt(w, lat);
                terrain::Mixture mx = terrain::mixtureAt(elev[i], 0.0f, temp, moist, 0.0f, false,
                                                        terrain::patchNoise(w));
                float veg = mx.cov[3] + mx.cov[4] + mx.cov[2];
                float bare = mx.cov[0] + mx.cov[10] + mx.cov[1] * 0.5f;
                albedo[i] = temp < -10 ? 0.6f : 0.14f + 0.18f * bare - 0.03f * veg;
                heatC[i] = (float)(A::C_LAND * (1.0 + 2.0 * veg));
            }
        }

        T.assign(N, 0.0);
        for (int i = 0; i < N; i++)
            T[i] = terrain::temperatureC((float)std::asin(std::clamp(g->c[i].z, -1.0, 1.0)),
                                         elev[i]);
        Ta = T;
        Wv.assign(N, 0.0);
        for (int i = 0; i < N; i++) Wv[i] = 0.5 * A::capAirOf(Ta[i]);
        soil.assign(N, A::SOIL_REF_MM);
        hP.assign(N, 0.0);
        hU.assign(N, 0.0);
        capEff.assign(N, 0.0);
        cloudF.assign(N, 0.5);
        divSm.assign(N, 0.0);
        rainStep.assign(N, 0.0);
        hWant.assign(N, 0.0);
        U1.assign(N, D3{0, 0, 0});
        U2.assign(N, D3{0, 0, 0});
        nT = nTa = nW = nhP = nhU = sc1 = sc2 = std::vector<double>(N, 0.0);
        nU1 = nU2 = F1 = F2 = std::vector<D3>(N, D3{0, 0, 0});
        flux.assign(g->nbr.size(), 0.0);
        cellOut.assign(N, 0.0);
    }

    // ------------------------------------------------------------- dynamics
    D3 lapVec(const std::vector<D3>& V, int i) const {
        D3 s{0, 0, 0};
        for (int k = g->nbrStart[i]; k < g->nbrStart[i + 1]; k++)
            s = s + (V[g->nbr[k]] - V[i]) * (g->elen[k] / g->edist[k]);
        return s * (1.0 / (g->area[i] * R_E * R_E));
    }

    void stepDynamics(double dt) {
        // forward: thicknesses, from the winds as they stand
        for (int i = 0; i < N; i++) {
            F1[i] = U1[i] * (A::H_LAYER + hP[i]);
            F2[i] = U2[i] * (A::H_UPPER + hU[i]);
        }
#pragma omp parallel for
        for (int i = 0; i < N; i++) {
            double c1 = -geodesic::div(*g, F1, i, R_E);
            double c2 = -geodesic::div(*g, F2, i, R_E);
            nhP[i] = std::clamp(hP[i] + dt * (c1 + (hWant[i] - hP[i]) / A::THERM_TAU),
                                -0.4 * A::H_LAYER, 0.4 * A::H_LAYER);
            nhU[i] = std::clamp(hU[i] + dt * (c2 - hU[i] / A::THERM_TAU), -0.4 * A::H_UPPER,
                                0.4 * A::H_UPPER);
        }
        hP.swap(nhP);
        hU.swap(nhU);
        // backward: winds, down the slopes the thicknesses now have
        for (int i = 0; i < N; i++) {
            sc1[i] = hP[i] + A::RHO_UPPER * hU[i]; // what layer 1 feels
            sc2[i] = hP[i] + hU[i];                // what layer 2 feels
        }
#pragma omp parallel for
        for (int i = 0; i < N; i++) {
            D3 n = g->c[i];
            // Coriolis, exactly: -2 Omega x v, kept in the tangent plane. No
            // latitude, no f, no curvature correction -- those were all
            // artefacts of writing vectors in a frame that rotates.
            auto cor = [&](D3 v) { return tangent(cross(D3{0, 0, 1}, v) * (-2.0 * A::OMEGA), n); };
            D3 g1 = geodesic::grad(*g, sc1, i, R_E);
            D3 g2 = geodesic::grad(*g, sc2, i, R_E);
            D3 a1 = g1 * -A::GPRIME + cor(U1[i]) - U1[i] * A::WIND_DRAG + lapVec(U1, i) * A::DYN_VISC;
            D3 a2 = g2 * -A::GPRIME + cor(U2[i]) + lapVec(U2, i) * A::DYN_VISC;
            D3 v1 = tangent(U1[i] + a1 * dt, n);
            D3 v2 = tangent(U2[i] + a2 * dt, n);
            // What crosses between the layers takes its momentum with it.
            double mUp = divSm[i];
            if (mUp > 0) v2 = v2 + (v1 - v2) * (dt * mUp / A::H_UPPER);
            else v1 = v1 + (v2 - v1) * (dt * -mUp / A::H_LAYER);
            double s1 = len(v1), s2 = len(v2);
            nU1[i] = s1 > 70.0 ? v1 * (70.0 / s1) : v1;
            nU2[i] = s2 > 110.0 ? v2 * (110.0 / s2) : v2;
        }
        U1.swap(nU1);
        U2.swap(nU2);
    }

    // ---------------------------------------------------------- one hour
    void step(double doy, double hour) {
        const double PI = 3.14159265358979;
        double dec = 23.5 * PI / 180.0 * std::cos(2 * PI * (doy - 171.0) / 365.0);
        double lam = PI - 2 * PI * (hour / 24.0);
        D3 sun{std::cos(dec) * std::cos(lam), std::cos(dec) * std::sin(lam), std::sin(dec)};

        for (int i = 0; i < N; i++)
            hWant[i] = A::THERM_H_PER_K * std::clamp(Ta[i] + 25.0, -60.0, 60.0);
        for (int k = 0; k < DYN_SUB; k++) stepDynamics(A::DT / DYN_SUB);

        // Vertical motion, from the lower layer's convergence, and held to the
        // day a parcel takes to rise -- a rectified instantaneous divergence
        // reads gravity waves as permanent ascent.
        for (int i = 0; i < N; i++) F1[i] = U1[i];
#pragma omp parallel for
        for (int i = 0; i < N; i++) {
            double w = -geodesic::div(*g, F1, i, R_E) * A::H_FLOW;
            double oro = dot(U1[i], geodesic::grad(*g, sc1, i, R_E)); // sc1 reused below
            (void)oro;
            divSm[i] += (w - divSm[i]) * (A::DT / A::UPLIFT_TAU);
        }

        // ----- moisture transport, conservative and limited
        for (int i = 0; i < N; i++) capEff[i] = std::max(capEff[i], 0.05);
#pragma omp parallel for
        for (int i = 0; i < N; i++) {
            double out = 0;
            for (int k = g->nbrStart[i]; k < g->nbrStart[i + 1]; k++) {
                int j = g->nbr[k];
                double un = dot((U1[i] + U1[j]) * 0.5, g->enorm[k]);
                double capMin = std::min(capEff[i], capEff[j]);
                double rh = un > 0 ? Wv[i] / capEff[i] : Wv[j] / capEff[j];
                flux[k] = un * rh * capMin * g->elen[k]; // outward positive
                if (flux[k] > 0) out += flux[k];
            }
            double have = Wv[i] * g->area[i] * R_E;
            cellOut[i] = out * A::DT > 1e-12 ? std::min(1.0, 0.9 * have / (out * A::DT)) : 1.0;
        }
        // Scale each cell's own exports, then a face carries what its donor
        // could afford -- so what leaves one cell is exactly what reaches the
        // next, and no clamp anywhere invents the difference.
#pragma omp parallel for
        for (int i = 0; i < N; i++) {
            double net = 0;
            for (int k = g->nbrStart[i]; k < g->nbrStart[i + 1]; k++) {
                int j = g->nbr[k];
                double f = flux[k];
                net -= f * (f > 0 ? cellOut[i] : cellOut[j]);
            }
            sc2[i] = net / (g->area[i] * R_E); // mm per second gained
        }

        // ----- the column, cell by cell
#pragma omp parallel for
        for (int i = 0; i < N; i++) {
            D3 n = g->c[i];
            double cosz = dot(n, sun);
            double lat = std::asin(std::clamp(n.z, -1.0, 1.0));
            double white = std::clamp((A::SNOW_NONE_C - T[i]) / (A::SNOW_NONE_C - A::SNOW_FULL_C),
                                      0.0, 1.0);
            double alb = albedo[i] +
                         white * ((water[i] ? A::ALBEDO_SEAICE : A::ALBEDO_SNOW) - albedo[i]);
            double cf = cloudF[i];
            double inc = A::SOLAR * std::max(cosz, 0.0) * (1.0 - A::CLOUD_ALB * cf);
            double swAir = inc * A::SW_ATM;
            double sw = (inc - swAir) * (1.0 - alb);

            double Tk = T[i] + 273.15, Tak = Ta[i] + 273.15;
            double lwUp = A::SIGMA * Tk * Tk * Tk * Tk;
            double sTa4 = A::SIGMA * Tak * Tak * Tak * Tak;
            double em0 = A::emissOf(Wv[i]);
            double cld = (1.0 - em0) * A::CLOUD_LW * cf;
            double em = em0 + cld;
            double Tcb = Ta[i] + A::LAPSE_OFFSET + 273.15;
            double lwDown = em0 * sTa4 + cld * A::SIGMA * Tcb * Tcb * Tcb * Tcb;

            double spd = std::sqrt(A::W_SURFACE * A::W_SURFACE * dot(U1[i], U1[i]) +
                                   A::U_GUST * A::U_GUST);
            double kExch = A::RHO_CP * (water[i] ? A::CH_SEA : A::CH_LAND) * spd;
            double gap = T[i] - Ta[i] - A::LAPSE_OFFSET;
            double stab = gap > 0 ? 1.0 : 1.0 / (1.0 + A::STAB_B * (-gap));
            double sens = kExch * stab * gap;

            double cap = A::capAirOf(Ta[i]);
            double capSkin = A::capOf(T[i]);
            double supply = water[i] ? 1.0 : std::clamp(soil[i] / A::SOIL_REF_MM, 0.0, 1.0);
            double dq = std::max(capSkin - Wv[i], 0.0) / A::Q_SCALE;
            double evap = A::RHO * (water[i] ? A::CH_SEA : A::CH_LAND) * spd * dq * supply * A::DT;
            double h0 = std::acos(std::clamp(-std::tan(lat) * std::tan(dec), -1.0, 1.0));
            double swDay = A::SOLAR / PI *
                           (h0 * std::sin(lat) * std::sin(dec) +
                            std::cos(lat) * std::cos(dec) * std::sin(h0)) *
                           (1.0 - alb) * (1.0 - A::CLOUD_ALB * cf) * (1.0 - A::SW_ATM);
            double afford = std::max(swDay, 0.0) +
                            (water[i] ? A::STORED_FLUX_WATER : A::STORED_FLUX_LAND);
            double lFlux = evap * A::LATENT_J_PER_KG / A::DT;
            if (lFlux > afford) { evap *= afford / lFlux; lFlux = afford; }

            double heatHere = (water[i] && T[i] < -1.0) ? A::C_SEAICE : heatC[i];
            double cond = (water[i] && T[i] < A::SEA_FREEZE)
                              ? A::K_ICE_COND * (A::SEA_FREEZE - T[i])
                              : 0.0;
            double dT = (sw - lwUp + lwDown - sens - lFlux + cond) / heatHere * A::DT;
            if (water[i] && T[i] > -2.0 && T[i] < 2.0) dT *= A::MELT_DAMP;
            nT[i] = std::clamp(T[i] + dT, -90.0, 65.0);

            // rain: what the ascent leaves no room for
            double wUp = A::W_CONV * std::max(sens, 0.0) + A::W_DIVERGE * divSm[i];
            double dz = std::clamp(wUp * A::UPLIFT_TAU, -A::H_LIFT_MAX, A::H_LIFT_MAX);
            double capLift = std::min(cap, cap * std::exp(-A::LAPSE_MOIST * dz / A::CAP_SCALE));
            double rain = std::max(Wv[i] - A::RAIN_FRAC * capLift, 0.0) * A::RAIN_RATE;
            rain = std::min(rain, Wv[i]);
            double raw = Wv[i] + evap - rain + sc2[i] * A::DT;
            if (raw < 0) { rain = std::max(0.0, rain + raw); raw = 0; }
            if (raw > capLift) { rain += raw - capLift; raw = capLift; }
            capEff[i] = std::max(capLift, 0.05);
            cloudF[i] = A::cloudOf(raw / capEff[i]);
            nW[i] = raw;
            rainStep[i] = rain;
            if (!water[i]) soil[i] = std::clamp(soil[i] + rain - evap, 0.0, A::SOIL_CAP_MM);

            double condense = rain * A::LATENT_J_PER_KG / A::C_AIR;
            nTa[i] = std::clamp(Ta[i] +
                                    (swAir + em * lwUp - lwDown - em * sTa4 + sens) / A::C_AIR *
                                        A::DT +
                                    condense,
                                -95.0, 70.0);
        }
        T.swap(nT);
        Ta.swap(nTa);
        Wv.swap(nW);

        // ----- the air carries its heat, by the column's own flow
        for (int i = 0; i < N; i++) F2[i] = (U1[i] + U2[i]) * 0.5;
        double worst = 0;
        for (int i = 0; i < N; i++) {
            double d = 1e30;
            for (int k = g->nbrStart[i]; k < g->nbrStart[i + 1]; k++)
                d = std::min(d, g->edist[k] * R_E);
            worst = std::max(worst, len(F2[i]) / d + 4 * A::KT_DIFF / (d * d));
        }
        int sub = (int)std::clamp(std::ceil(worst * A::DT / 0.5), 1.0, 64.0);
        double dt = A::DT / sub;
        for (int s = 0; s < sub; s++) {
#pragma omp parallel for
            for (int i = 0; i < N; i++)
                nTa[i] = Ta[i] + dt * (geodesic::advect(*g, Ta, F2, i, R_E) +
                                       A::KT_DIFF * geodesic::lap(*g, Ta, i, R_E));
            Ta.swap(nTa);
        }
    }
};

// ---------------------------------------------------------------- output
//
// The rest of the simulation reads climate as a function of position, so the
// grid stops at this boundary: sample the cells onto the lat-lon climatology
// everything else already expects, and nothing downstream needs to know.
inline A::Climatology build(const terrain::ContinentParams& cp, float seaLevel, const float rot[9],
                            terrain::V3 offset, const plates::Field& pf,
                            const hydrology::Result& hy, bool verbose = false,
                            void (*progress)(int day, int totalDays) = nullptr) {
    Model m;
    m.init(cp, seaLevel, rot, offset, pf, hy);
    const geodesic::Grid& g = *m.g;
    A::Climatology c;

    // which cell each output pixel belongs to, once
    std::vector<int> own(A::W * A::H);
    {
        int hint = 0;
        for (int y = 0; y < A::H; y++) {
            double lat = ((y + 0.5) / A::H - 0.5) * 3.14159265;
            for (int x = 0; x < A::W; x++) {
                double lon = ((x + 0.5) / A::W * 2.0 - 1.0) * 3.14159265;
                D3 n{std::cos(lat) * std::cos(lon), std::cos(lat) * std::sin(lon), std::sin(lat)};
                hint = nearest(g, n, hint);
                own[y * A::W + x] = hint;
            }
        }
    }
    // elev is the one Climatology field its constructor does not size, because
    // the lat-lon build fills it by assign().
    c.elev.assign(A::W * A::H, 0.0f);
    for (int i = 0; i < A::W * A::H; i++) c.elev[i] = m.elev[own[i]];

    std::vector<double> acT(g.size() * A::SEASONS, 0.0), acR(acT), acW(acT), acC(acT), acA(acT),
        acU(acT), acV(acT), acRH(acT);
    std::vector<double> hours(A::SEASONS, 0.0);
    int totalDays = A::SPINUP_DAYS + A::STAT_YEARS * 365;
    for (int day = 0; day < totalDays; day++) {
        double doy = std::fmod((double)day, 365.0);
        bool stat = day >= A::SPINUP_DAYS;
        int se = (int)(std::fmod(doy + 30.0, 365.0) / 365.0 * A::SEASONS) % A::SEASONS;
        for (int h = 0; h < 24; h++) {
            m.step(doy, h);
            if (!stat) continue;
            for (int i = 0; i < g.size(); i++) {
                int q = se * g.size() + i;
                acT[q] += m.T[i];
                acR[q] += m.rainStep[i] * 24.0;
                acW[q] += m.Wv[i];
                acC[q] += m.cloudF[i];
                acA[q] += m.Ta[i];
                D3 e = norm(tangent(D3{0, 0, 1}, g.c[i])); // local north, for reporting only
                D3 east = norm(cross(D3{0, 0, 1}, g.c[i]));
                acU[q] += dot(m.U1[i], east);
                acV[q] += dot(m.U1[i], e);
                acRH[q] += m.Wv[i] / std::max(m.capEff[i], 0.05);
            }
            hours[se] += 1;
        }
        if (progress && day % 15 == 0) progress(day, totalDays);
    }
    for (int se = 0; se < A::SEASONS; se++) {
        double n = std::max(hours[se], 1.0);
        for (int i = 0; i < A::W * A::H; i++) {
            int q = se * g.size() + own[i], o = se * A::W * A::H + i;
            c.meanT[o] = (float)(acT[q] / n);
            c.rainMmDay[o] = (float)(acR[q] / n);
            c.snowMmDay[o] = c.meanT[o] < A::SNOW_T ? c.rainMmDay[o] : 0.0f;
            c.rainProb[o] = c.rainMmDay[o] > 0.5f ? 1.0f : 0.0f;
            c.wv[o] = (float)(acW[q] / n);
            c.cloud[o] = (float)(acC[q] / n);
            c.airT[o] = (float)(acA[q] / n);
            c.windU[o] = (float)(acU[q] / n);
            c.windV[o] = (float)(acV[q] / n);
            c.rh[o] = (float)(acRH[q] / n);
            c.press[o] = 0.0f;
            c.diurnal[o] = 6.0f;
        }
    }
    (void)verbose;
    return c;
}

} // namespace atmogeo
