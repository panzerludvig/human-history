// What the viewer says about a place or a people, as text: the tooltip line
// under the cursor and the tabs of a settlement or band panel. Every function
// reads a const World and returns a string; nothing here draws or knows a
// window, so the same words could go to a probe's stderr. Technical/Globe
// Viewer.md §Tooltip and §Selection panels say what each line shows and why.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
#include "camera.h"
#include "world.h"
#include "sim.h"

namespace inspect {

// What is under the cursor, from the CPU mirror of the terrain function.
constexpr const char* SUBSTRATE_NAMES[] = {"soil", "sand", "rock", "scree", "silt", "mud", "ice"};
constexpr const char* COVER_NAMES[] = {"bare",       "tundra",    "taiga",  "forest",
                                       "rainforest", "grassland", "steppe", "savanna",
                                       "shrubland",  "marsh",     "desert"};

// "forest 68%, grassland 22%, rock 10%": cover fractions, with bare ground
// named by its substrate, largest first, down to 5%. Written into out; at
// most about 110 characters, since the list stops once it passes 60.
inline void describeMixture(const terrain::Mixture& m, char* out, size_t outSize) {
    struct Part {
        float share;
        const char* name;
    };
    Part parts[terrain::NCOV - 1 + terrain::NSUB];
    int n = 0;
    for (int i = 1; i < terrain::NCOV; i++) parts[n++] = {m.cov[i], COVER_NAMES[i]};
    for (int i = 0; i < terrain::NSUB; i++) parts[n++] = {m.cov[0] * m.sub[i], SUBSTRATE_NAMES[i]};
    std::sort(parts, parts + n, [](const Part& a, const Part& b) { return a.share > b.share; });
    size_t len = 0;
    out[0] = 0;
    for (int i = 0; i < n; i++) {
        if (parts[i].share < 0.05f || len > 60) break;
        int wrote = snprintf(out + len, outSize - len, "%s%s %d%%", len == 0 ? "" : ", ",
                             parts[i].name, (int)std::lround(parts[i].share * 100));
        if (wrote < 0 || (size_t)wrote >= outSize - len) break; // truncated: stop here
        len += (size_t)wrote;
    }
}

// What the ice on water is at a seasonal local temperature, as the globe
// draws it: solid below sim::FROZEN_T (bands walk on it), still forming
// between that and sim::ICE_FORMING_T, open water above.
inline const char* iceWord(float tC) {
    if (tC < sim::FROZEN_T) return " (frozen)";
    if (tC < sim::ICE_FORMING_T) return " (thin ice)";
    return "";
}

// "1234 m", for depths and heights.
inline void fmtM(float m, char* b, size_t n) { snprintf(b, n, "%d m", (int)std::lround(m)); }

// What is under the cursor at n, one line for the tooltip: the water or the
// ground, the temperature now, the cover mixture, the rain, the capacity, and
// any building the cursor is on. octaves is the level of detail on screen,
// so the height here is the one the shader drew. Written into out (at most
// outSize bytes) rather than returned: this runs on every mouse move, so
// it does not allocate (standards/cpp.md §Hot paths).
inline void describePoint(const world::World& wd, const camera::Camera& cam, int octaves,
                          camera::Vec3 n, char* out, size_t outSize) {
    terrain::V3 nf = {(float)n.x, (float)n.y, (float)n.z};
    terrain::V3 off = {(float)wd.offset.x, (float)wd.offset.y, (float)wd.offset.z};
    float h = terrain::heightMeters(terrain::rotate(wd.rot, nf) + off, nf, wd.cp, wd.seaLevel,
                                    octaves, wd.plateField, wd.rot);
    float lat = (float)std::asin(std::clamp(n.z, -1.0, 1.0));
    float lon = (float)std::atan2(n.y, n.x);
    terrain::V3 wDerive = terrain::rotate(wd.rot, nf) + off;
    // Annual mean drives the mixture (biomes don't change by the hour);
    // the displayed temperature is the current one: seasonal mean plus the
    // diurnal swing phased to local solar time, peaking when the atmosphere
    // model's day does (atmosphere::diurnalPhase).
    atmosphere::DerivedClimate dcTip = atmosphere::deriveAt(wd.clim, lat, lon, wDerive, h);
    float temp = dcTip.temp;
    float tempNow = temp;
    if (!wd.clim.meanT.empty()) {
        float tSeason = sim::seasonalT(wd.clim, nf, std::max(h, 0.0f), wd.simTime);
        float amp = atmosphere::seasonalAt(wd.clim.diurnal, atmosphere::climFuzz(nf), wd.simTime);
        double tod = fmod(wd.simTime, 1.0);
        double hLoc = fmod(lon * (12.0 / camera::PI) + 24.0 * tod + 48.0, 24.0);
        tempNow = tSeason + 0.5f * amp * (float)atmosphere::diurnalPhase(hLoc);
    }
    char hm[32]; // a depth or height, formatted
    // Climatology at the cursor, season-interpolated: shown for sea, lake, and land.
    char climTxt[48] = "";
    if (!wd.clim.rainMmDay.empty()) {
        int ax = (int)(((lon + camera::PI) / (2 * camera::PI)) * atmosphere::W) % atmosphere::W;
        int ay = std::clamp((int)(((lat + camera::PI / 2) / camera::PI) * atmosphere::H), 0,
                            atmosphere::H - 1);
        const atmosphere::SeasonBlend sb = atmosphere::seasonBlendAt(wd.simTime);
        double f = sb.f;
        int i0 = sb.s0 * atmosphere::W * atmosphere::H + ay * atmosphere::W + ax;
        int i1 = sb.s1 * atmosphere::W * atmosphere::H + ay * atmosphere::W + ax;
        double rain = wd.clim.rainMmDay[i0] * (1 - f) + wd.clim.rainMmDay[i1] * f;
        double snow = wd.clim.snowMmDay[i0] * (1 - f) + wd.clim.snowMmDay[i1] * f;
        if (snow > 0.5 * rain && rain > 0.05)
            snprintf(climTxt, sizeof climTxt, "  |  snow %.1f mm/d", rain);
        else
            snprintf(climTxt, sizeof climTxt, "  |  rain %.1f mm/d", rain);
    }

    if (h < 0) {
        fmtM(-h, hm, sizeof hm);
        snprintf(out, outSize, "Sea%s, %s deep  |  %.0f C%s",
                 iceWord(sim::seasonalT(wd.clim, nf, 0.0f, wd.simTime)), hm, tempNow, climTxt);
        return;
    }
    int cx = (int)std::floor((lon + camera::PI) / (2 * camera::PI) * hydrology::W),
        cy = (int)std::floor((lat + camera::PI / 2) / camera::PI * hydrology::H);
    cx = hydrology::wrapX(cx);
    cy = std::clamp(cy, 0, hydrology::H - 1);
    // A building under the cursor names itself: same marker positions the
    // shader draws (sim::granaryPos, defined once), pick radius = draw
    // radius plus ~3 px of slop.
    char building[80] = "";
    {
        float pickR =
            (float)std::clamp(cam.kmPerPixel() * 4.0, 1.5, 6.0) + (float)(cam.kmPerPixel() * 3.0);
        for (const population::Field::Ruin& r : wd.pop.ruins)
            if (sim::distKm(nf, sim::cellCentre(r.cell)) < pickR) {
                char rb[64];
                if (r.name[0])
                    snprintf(rb, sizeof rb, "Ruins of %s, abandoned year %d  |  ", r.name,
                             (int)(r.abandoned / 365.0) + 1);
                else
                    snprintf(rb, sizeof rb, "Ruins, abandoned year %d  |  ",
                             (int)(r.abandoned / 365.0) + 1);
                snprintf(building, sizeof building, "%s", rb);
                break;
            }
    }
    if (!building[0] && !wd.pop.settlementAt.empty()) {
        float pickR =
            (float)std::clamp(cam.kmPerPixel() * 2.0, 0.6, 2.5) + (float)(cam.kmPerPixel() * 3.0);
        for (int dy = -1; dy <= 1 && !building[0]; dy++)
            for (int dx = -1; dx <= 1 && !building[0]; dx++) {
                int yy = std::clamp(cy + dy, 0, hydrology::H - 1);
                int cell = yy * hydrology::W + hydrology::wrapX(cx + dx);
                int si = wd.pop.settlementAt[cell];
                if (si < 0) continue;
                const population::Settlement& st = wd.pop.settlements[si];
                for (int k = 0; k < (int)(st.granaries + 0.5f) && k < 8; k++)
                    if (sim::distKm(nf, sim::granaryPos(st.cell, k)) < pickR) {
                        snprintf(building, sizeof building, "Granary  |  ");
                        break;
                    }
            }
    }
    if (!building[0] && !wd.pop.settlementAt.empty()) {
        // Farmsteads stand kilometres from their village, so the search box
        // has to reach further than the granaries' one-cell ring.
        float pickR =
            (float)std::clamp(cam.kmPerPixel() * 2.0, 0.6, 2.5) + (float)(cam.kmPerPixel() * 3.0);
        float lat = std::asin(std::clamp(nf.z, -1.0f, 1.0f));
        int rx =
            std::min((int)std::ceil(1.6f / std::max(std::cos(lat), 0.05f)) + 1, hydrology::W / 2);
        for (int dy = -2; dy <= 2 && !building[0]; dy++)
            for (int dx = -rx; dx <= rx && !building[0]; dx++) {
                int yy = std::clamp(cy + dy, 0, hydrology::H - 1);
                int cell = yy * hydrology::W + hydrology::wrapX(cx + dx);
                int si = wd.pop.settlementAt[cell];
                if (si < 0) continue;
                const population::Settlement& st = wd.pop.settlements[si];
                for (int k = 0; k < (int)(st.farmsteads + 0.5f) && k < population::FSTEAD_MAX; k++)
                    if (sim::distKm(nf, sim::farmsteadPos(st.cell, k)) < pickR) {
                        char fb[64];
                        snprintf(fb, sizeof fb, "Farmstead of %s  |  ", st.name);
                        snprintf(building, sizeof building, "%s", fb);
                        break;
                    }
            }
    }
    if (!building[0] && !wd.pop.settlementAt.empty() && cam.kmPerPixel() < 4.0) {
        // Fields: the same annulus the shader draws, so what the cursor
        // names and what the eye sees are one definition.
        for (int dy = -1; dy <= 1 && !building[0]; dy++)
            for (int dx = -1; dx <= 1 && !building[0]; dx++) {
                int yy = std::clamp(cy + dy, 0, hydrology::H - 1);
                int cell = yy * hydrology::W + hydrology::wrapX(cx + dx);
                int si = wd.pop.settlementAt[cell];
                if (si < 0) continue;
                const population::Settlement& st = wd.pop.settlements[si];
                float fr = sim::farmRadiusKm(st, wd.simTime);
                float d = sim::distKm(nf, sim::cellCentre(st.cell));
                float inner = sim::fieldInnerKm(st.P);
                if (fr > inner && d < fr && d > inner) {
                    char fb[64];
                    snprintf(fb, sizeof fb, "Fields of %s  |  ", st.name);
                    snprintf(building, sizeof building, "%s", fb);
                }
            }
    }
    bool nearRiver = false;
    if (!wd.hydro.cells.empty()) {
        const hydrology::Cell& c = wd.hydro.cells[cy * hydrology::W + cx];
        nearRiver = c.nearRiver > 0.5f;
        // Lake: the shore rule the globe draws by (hydrology::lakeLevelAt).
        float lake = hydrology::lakeLevelAt(wd.hydro, nf);
        float surface = lake + hydrology::LAKE_SHORE_RISE_M;
        if (lake > hydrology::NO_LAKE + 1 && h < surface) {
            fmtM(surface - h, hm, sizeof hm);
            snprintf(out, outSize, "Lake%s, %s deep  |  %.0f C%s",
                     iceWord(sim::seasonalT(wd.clim, nf, h, wd.simTime)), hm, tempNow, climTxt);
            return;
        }
    }
    terrain::V3 w = wDerive;
    float moist = dcTip.moist;
    float slope =
        terrain::slopeAt(nf, wd.cp, wd.seaLevel, std::min(octaves, 12), wd.plateField, wd.rot, off);
    float uplift = wd.plateField.sample({nf.x, nf.y, nf.z}).uplift;
    terrain::Mixture m =
        terrain::mixtureAt(h, slope, temp, moist, uplift, nearRiver, terrain::patchNoise(w),
                           dcTip.swamp, dcTip.tCold, dcTip.tWarm);
    char extra[72] = "";
    if (!wd.pop.K.empty()) {
        int ci = cy * hydrology::W + cx;
        if (wd.pop.K[ci] > 0) {
            char gameB[24] = "";
            if (!wd.pop.gameG.empty() && wd.pop.kGameMap[ci] > 0) {
                float g = wd.pop.gameG[population::gameRegion(ci)];
                if (g < 0.98f)
                    snprintf(gameB, sizeof gameB, "  |  game %d%%", (int)std::lround(g * 100));
            }
            snprintf(extra, sizeof extra, "  |  capacity %d%s",
                     (int)(wd.pop.K[ci] * population::SUSTAIN_R), gameB);
        }
    }
    char mix[128];
    fmtM(h, hm, sizeof hm);
    describeMixture(m, mix, sizeof mix);
    snprintf(out, outSize, "%s%s  |  %.0f C  |  %s%s%s", building, hm, tempNow, mix, climTxt,
             extra);
}

constexpr const char* TECH_NAMES[population::NTECH] = {"Farming", "Husbandry", "Granary building",
                                                       "Archery", "Fishing"};

// Settlements are erased when they pick up and leave, so panels hold an id
// and resolve the index whenever they draw.
inline int settlementIndexById(const population::Field& pf, uint32_t sid) {
    const std::vector<population::Settlement>& v = pf.settlements;
    for (int i = 0; i < (int)v.size(); i++)
        if (v[i].id == sid) return i;
    return -1;
}

inline std::string fmtYears(double yr) {
    char b[32];
    if (yr >= 100000) return "100k+ yr";
    if (yr >= 1000)
        snprintf(b, sizeof b, "%.1fk yr", yr / 1000.0);
    else
        snprintf(b, sizeof b, "%.0f yr", yr);
    return b;
}

inline std::string techStateLine(const population::TechState& ts, int techId, double now) {
    if (!ts.practising)
        return std::string(TECH_NAMES[techId]) +
               (ts.aware ? ": known, not practised" : ": unknown");
    char b[64];
    snprintf(b, sizeof b, "%s: practising, expertise %d%%", TECH_NAMES[techId],
             (int)std::lround(technology::expertise(ts, now) * 100));
    return b;
}

// Who these people are, what they believe themselves good at, and what
// they have to hand. The land they live on is the next tab along.
inline std::string peopleText(const world::World& wd, const population::Settlement& st) {
    double now = wd.simTime;
    char b[128];
    std::string out;
    snprintf(b, sizeof b, "People: %d (capacity %d)\n", (int)st.P,
             (int)(technology::effectiveK(st, now) * population::SUSTAIN_R));
    out += b;
    snprintf(b, sizeof b, "  %d men, %d women, %d children, %d elderly\n", (int)st.pop.M,
             (int)st.pop.W, (int)st.pop.C, (int)st.pop.E);
    out += b;
    if (st.culture < wd.pop.cultures.size()) {
        snprintf(b, sizeof b, "A people of the %s\n", wd.pop.cultures[st.culture].name);
        out += b;
    }
    {
        struct {
            const char* n;
            float v;
        } af[5] = {{"hunting", st.aff.hunt},
                   {"gathering", st.aff.gather},
                   {"farming", st.aff.farm},
                   {"herding", st.aff.herd},
                   {"fighting", st.aff.fight}};
        int bi = 0;
        for (int i = 1; i < 5; i++)
            if (af[i].v > af[bi].v) bi = i;
        if (af[bi].v > 0.05f) {
            snprintf(b, sizeof b, "Known for: %s (%d%%)\n", af[bi].n,
                     (int)std::lround(af[bi].v * 100));
            out += b;
        }
    }
    if (st.starvedYr >= 0.5f) {
        snprintf(b, sizeof b, "Hunger: %d lost this year\n", (int)std::lround(st.starvedYr));
        out += b;
    }
    if (st.coldYr >= 0.5f) {
        snprintf(b, sizeof b, "  of them to cold hearths: %d\n", (int)std::lround(st.coldYr));
        out += b;
    }
    out += "\n";
    out += "What they carry:\n";
    snprintf(b, sizeof b, "  Food: %d days (of %d)\n", (int)(st.S / std::max(st.P, 1.0f)),
             (int)population::storageCapDays(st.P, st.granaries));
    out += b;
    snprintf(b, sizeof b, "  Bows: %d (%d%% of hunters)\n", (int)st.bows,
             (int)std::lround(population::bowCoverage(st.bows, st.P) * 100));
    out += b;
    if (st.herd > 0.5f) {
        snprintf(b, sizeof b, "  Livestock: feeds %d\n", (int)st.herd);
        out += b;
    }
    {
        float need =
            population::fuelNeedKg(population::cachedSeasonT(st, now)) * std::max(st.P, 1.0f);
        snprintf(b, sizeof b, "  Firewood: %d days at this season\n",
                 (int)(st.fuelS / std::max(need, 1.0f)));
        out += b;
    }
    return out;
}

inline std::string envText(const world::World& wd, const population::Settlement& st) {
    double now = wd.simTime;
    terrain::V3 n = sim::cellCentre(st.cell);
    float lat = std::asin(std::clamp(n.z, -1.0f, 1.0f)) * 180.0f / 3.14159265f;
    float lon = std::atan2(n.y, n.x) * 180.0f / 3.14159265f;
    float awareKm = population::settlementAwareKm(now - st.founded,
                                                  sim::prominenceM(wd.hydro, wd.clim, st.cell));
    char b[128];
    std::string out;
    snprintf(b, sizeof b, "%.1f%c  %.1f%c\n", std::fabs(lat), lat >= 0 ? 'N' : 'S', std::fabs(lon),
             lon >= 0 ? 'E' : 'W');
    out += b;
    snprintf(b, sizeof b, "Settled year %d\n", (int)(st.founded / 365.0) + 1);
    out += b;
    snprintf(b, sizeof b, "Land condition: %d%%\n", (int)std::lround(st.R * 100));
    out += b;
    {
        float lo = 1e9f, hi = 0;
        for (int k = 0; k < population::CLAIM_SECTORS; k++) {
            lo = std::min(lo, st.claim[k]);
            hi = std::max(hi, st.claim[k]);
        }
        float want = sim::wantedReachKm(wd.pop, st.cell, st.P, st.R);
        const char* state = hi >= population::CLAIM_CAP_KM - 0.01f ? "all one place can reach"
                            : want > hi + 0.01f                    ? "pressing outward"
                                                                   : "as much as they need";
        snprintf(b, sizeof b, "Territory: %d-%d km, %d km2 worked -- %s\n", (int)std::lround(lo),
                 (int)std::lround(hi), (int)std::lround(st.claimKm2), state);
        out += b;
    }
    if (st.kFish > 0) {
        float ex = technology::expertise(st.tech[population::TECH_FISHING], now);
        float fish = st.kFish * population::fishEff(ex);
        float k = technology::effectiveK(st, now);
        snprintf(b, sizeof b, "Water: feeds %d%s (%d%% of the food)\n", (int)std::lround(fish),
                 st.tech[population::TECH_FISHING].practising ? ", weirs and nets" : ", by hand",
                 (int)std::lround(fish / std::max(k, 1.0f) * 100));
        out += b;
    }
    if (st.kGame > 0) {
        float plantF = st.kFoodP - st.kGame;
        float gameF = st.kGame * population::huntEff(st.gameNow);
        int dietG = (int)std::lround(gameF / std::max(plantF + gameF, 1e-6f) * 100);
        snprintf(b, sizeof b, "Wild game: %d%%%s (diet %d%% game)\n",
                 (int)std::lround(st.gameNow * 100),
                 st.gameNow < population::GAME_FLOOR ? " -- gone" : "", dietG);
        out += b;
    }
    snprintf(b, sizeof b, "Farm suitability: %d%%   pasture: %d%%\n",
             (int)std::lround(st.sFarm * 100), (int)std::lround(st.pasture * 100));
    out += b;
    snprintf(b, sizeof b, "Build materials: %d%%\n", (int)std::lround(st.buildMat * 100));
    out += b;
    snprintf(b, sizeof b, "Woodland: %d%%\n", (int)std::lround(st.sWood * 100));
    out += b;
    if (st.labFuel > 0.005f) {
        snprintf(b, sizeof b, "Woodcutters: %d%% of the day's labour\n",
                 (int)std::lround(st.labFuel * 100));
        out += b;
    }
    snprintf(b, sizeof b, "Awareness: %d km\n", (int)awareKm);
    out += b;
    if (st.scarceSince >= 0) {
        snprintf(b, sizeof b, "Scarce since year %d%s\n", (int)(st.scarceSince / 365.0) + 1,
                 st.noProspect ? " -- nowhere to go" : "");
        out += b;
    }
    return out;
}

// Everything that happened to these people during the step just taken --
// the same events the news feed groups, filtered to this settlement.
inline std::string historyText(const world::World& wd, const population::Settlement& st) {
    std::string out;
    char b[160];
    int shown = 0;
    for (const population::Event& e : wd.pop.events) {
        if (e.sid != st.id && e.sid2 != st.id) continue;
        if (++shown > 11) {
            out += "  ...\n";
            break;
        }
        snprintf(b, sizeof b, "%d: %s\n", (int)(e.t / 365.0) + 1, e.text);
        out += b;
        if (e.lossHere > 0 || e.lossThem > 0) {
            // Whose dead are whose depends on which side of it you are.
            bool theirs = e.sid != st.id;
            snprintf(b, sizeof b, "   %d of theirs dead, %d of ours\n",
                     (int)std::lround(theirs ? e.lossHere : e.lossThem),
                     (int)std::lround(theirs ? e.lossThem : e.lossHere));
            out += b;
        }
    }
    if (!shown) out += "Nothing happened to them this step.\n";
    return out;
}

// The exact numbers behind one technology's chances here: invention weight,
// contact odds, and every adoption gate, with the blocked one named.
inline std::string techDetailText(const world::World& wd, const population::Settlement& st, int idx,
                                  int t) {
    double now = wd.simTime;
    char b[160];
    std::string out = "< back\n";
    const population::TechState& ts = st.tech[t];
    out += TECH_NAMES[t];
    out += ts.practising ? " -- practising" : ts.aware ? " -- known, not practised" : " -- unknown";
    out += "\n";
    if (ts.practising) {
        snprintf(b, sizeof b, "Expertise %d%% since year %d\n(matures toward 100%% over ~50 yr)\n",
                 (int)std::lround(technology::expertise(ts, now) * 100),
                 (int)(ts.practiceT / 365.0) + 1);
        out += b;
        return out;
    }
    if (!ts.aware) {
        if (technology::needDriven(t)) {
            float years =
                t == population::TECH_FARMING
                    ? (st.hungrySince >= 0 ? (float)((now - st.hungrySince) / 365.0) : 0.0f)
                    : st.granNeedYrs;
            float acute = std::clamp((years - technology::NEED_YEARS_ON) /
                                         (technology::NEED_YEARS_SAT - technology::NEED_YEARS_ON),
                                     0.0f, 1.0f);
            float w = technology::needWeight(st, t, now);
            double W = 0;
            for (const population::Settlement& o : wd.pop.settlements)
                W += technology::needWeight(o, t, now);
            out += "Invention, driven by need:\n";
            snprintf(b, sizeof b, " %s %.1f yr -> desperation %d%%\n",
                     t == population::TECH_FARMING ? "hungry" : "stores binding", years,
                     (int)std::lround(acute * 100));
            out += b;
            snprintf(b, sizeof b, " suitability %d%%, people x%.2f\n",
                     (int)std::lround(technology::suitability(st, t) * 100),
                     std::min(st.P / 300.0f, 3.0f));
            out += b;
            if (W > 0) {
                snprintf(b, sizeof b, " weight %.2f of world %.1f (share %d%%)\n", w, W,
                         (int)std::lround(w / W * 100));
                out += b;
                snprintf(b, sizeof b, " world mean %s\n",
                         fmtYears(technology::NEED_MEAN_YEARS / std::sqrt(W)).c_str());
                out += b;
            } else
                out += " nobody in the world needs it yet\n";
        } else {
            double unaware = 0, total = 0;
            for (const population::Settlement& o : wd.pop.settlements) {
                total += o.P;
                if (!o.tech[t].aware) unaware += o.P;
            }
            double share = total > 0 ? unaware / total : 0;
            out += "Invention, serendipity:\n";
            snprintf(b, sizeof b, " unaware share %d%% -> world mean %s\n",
                     (int)std::lround(share * 100),
                     share > 0 ? fmtYears(technology::INVENT_MEAN_YEARS / share).c_str() : "never");
            out += b;
        }
        int knowing = 0;
        for (int j : wd.pop.neighbours[idx]) knowing += wd.pop.settlements[j].tech[t].aware ? 1 : 0;
        if (knowing)
            snprintf(b, sizeof b, "Hearing of it: %d neighbour%s -> mean %.1f yr\n", knowing,
                     knowing == 1 ? " knows it" : "s know it",
                     technology::AWARE_MEAN_YEARS / knowing);
        else
            snprintf(b, sizeof b, "Hearing of it: nobody within %d km knows\n",
                     (int)population::CONTACT_KM);
        out += b;
        return out;
    }
    float suit = technology::suitability(st, t);
    float need = technology::adoptionNeed(st, t, now);
    float esum = 0;
    int teachers = 0;
    for (int j : wd.pop.neighbours[idx]) {
        float e = technology::expertise(wd.pop.settlements[j].tech[t], now);
        if (e > 0) {
            esum += e;
            teachers++;
        }
    }
    float phi = st.P > 1 ? technology::effectiveK(st, now) * st.R / st.P : 2.0f;
    out += "Adoption gates (all must be open):\n";
    snprintf(b, sizeof b, " suitable ground: %d%%%s\n", (int)std::lround(suit * 100),
             suit <= 0 ? "  <- BLOCKED" : "");
    out += b;
    if (t == population::TECH_GRANARY)
        snprintf(b, sizeof b, " need: %d%% (stores bound %.0f yr running)%s\n",
                 (int)std::lround(need * 100), st.granNeedYrs, need <= 0 ? "  <- BLOCKED" : "");
    else
        snprintf(b, sizeof b, " need: %d%% (phi %.2f, content at 1.11)%s\n",
                 (int)std::lround(need * 100), phi, need <= 0 ? "  <- BLOCKED" : "");
    out += b;
    snprintf(b, sizeof b, " teachers in %d km: %d, expertise sum %.2f%s\n",
             (int)population::CONTACT_KM, teachers, esum, esum <= 0 ? "  <- BLOCKED" : "");
    out += b;
    double rate = (double)suit * need * esum;
    if (rate > 0)
        snprintf(b, sizeof b, "-> mean wait %s\n",
                 fmtYears(technology::PRACT_MEAN_YEARS / rate).c_str());
    else
        snprintf(b, sizeof b, "-> will not adopt until unblocked\n");
    out += b;
    return out;
}

inline std::string buildingsText(const population::Settlement& st, double now) {
    char b[96];
    std::string out;
    if (st.granaries > 0.5f)
        snprintf(b, sizeof b, "Granaries: %d\n", (int)st.granaries);
    else
        snprintf(b, sizeof b, "Granaries: none\n");
    out += b;
    if (st.buildWork > 0) {
        snprintf(b, sizeof b, "Under construction: %d%% done\n",
                 (int)std::lround((1.0 - st.buildWork / population::GRANARY_WORK) * 100));
        out += b;
    }
    snprintf(b, sizeof b, "Storage: %d + %d days per person\n", (int)population::CAP_DAYS_SETTLED,
             (int)(population::storageCapDays(st.P, st.granaries) - population::CAP_DAYS_SETTLED));
    out += b;
    if (st.farmsteads > 0.5f) {
        snprintf(b, sizeof b, "Farmsteads: %d\n", (int)st.farmsteads);
        out += b;
    }
    if (st.fsteadWork > 0) {
        snprintf(b, sizeof b, "Farmstead going up: %d%% done\n",
                 (int)std::lround((1.0 - st.fsteadWork / population::FSTEAD_WORK) * 100));
        out += b;
    }
    if (st.tech[population::TECH_FARMING].practising) {
        float sum = 0;
        for (int k = 0; k <= population::FSTEAD_MAX; k++) sum += st.tilled[k];
        if (sum > 0.05f) {
            snprintf(b, sizeof b, "Fields: %.1f km2 tilled (%.1f at the village)\n", sum,
                     st.tilled[0]);
            out += b;
            snprintf(
                b, sizeof b, "  feeding %d at their skill\n",
                (int)(st.farmK * technology::expertise(st.tech[population::TECH_FARMING], now)));
            out += b;
        }
        if (st.tillWork > 0) {
            snprintf(b, sizeof b, "Clearing a plot%s: %d%% done\n",
                     st.tillSite > 0 ? " at a farmstead" : "",
                     (int)std::lround((1.0 - st.tillWork / (population::PLOT_KM2 *
                                                            population::TILL_WORK_PER_KM2)) *
                                      100));
            out += b;
        }
    }
    const population::TechState& gt = st.tech[population::TECH_GRANARY];
    if (gt.practising) {
        snprintf(b, sizeof b, "Build pace: craft %d%% x materials %d%%\n",
                 (int)std::lround(technology::expertise(gt, now) * 100),
                 (int)std::lround(st.buildMat * 100));
        out += b;
        if (st.buildWork <= 0)
            out += "No build under way: stores have\nnot both filled and drained.\n";
    } else if (gt.aware)
        out += "(knows the craft is possible,\nhas not learned it)\n";
    else
        out += "(granary building unknown here)\n";
    return out;
}

inline std::string bandText(const world::World& wd, uint32_t bandId) {
    double now = wd.simTime;
    for (const population::Band& bd : wd.pop.bands)
        if (bd.id == bandId) {
            char b[128];
            std::string out;
            float away = sim::distKm({bd.px, bd.py, bd.pz}, sim::cellCentre(bd.targetCell));
            double rest = bd.resting ? now - bd.restStart : 0.0;
            float awareKm = population::bandAwareKm(
                rest, sim::prominenceM(wd.hydro, wd.clim, sim::cellOf({bd.px, bd.py, bd.pz})));
            if (bd.purpose == population::BAND_RAID)
                snprintf(b, sizeof b,
                         "Raiding party\nPeople: %d\nState: %s\n%s: %d km away\n"
                         "Carrying: %d rations, %d livestock\n",
                         (int)bd.P, bd.returning ? "homeward" : "outward",
                         bd.returning ? "Home" : "Their mark", (int)away, (int)bd.loot,
                         (int)bd.lootHerd);
            else
                snprintf(b, sizeof b,
                         "People: %d\nStores: %d days\nState: %s\nTarget: %d km away\n", (int)bd.P,
                         (int)(bd.S / std::max(bd.P, 1.0f)), bd.resting ? "resting" : "moving",
                         (int)away);
            out += b;
            for (int t = 0; t < population::NTECH; t++)
                out += techStateLine(bd.tech[t], t, now) + "\n";
            snprintf(b, sizeof b, "Awareness: %d km\n", (int)awareKm);
            out += b;
            return out;
        }
    return "No longer on the move:\nsettled, merged, or perished.";
}

} // namespace inspect
