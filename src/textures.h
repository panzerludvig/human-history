// The world's layers as GPU textures: plates, hydrology, climatology and
// the Earth template feed the terrain function in shaders/globe.frag, and
// the population, band and site textures tell it where people are. Each
// upload packs one CPU table into the layout the shader indexes; the texel
// layouts are documented at the packing functions and mirrored in the
// shader. Technical/Globe Viewer.md (§Tectonic plates, §Hydrology,
// §Atmosphere and climatology, §Population) says what each layer holds.
#pragma once
#include <algorithm>
#include <vector>
#include "gl.h"
#include "world.h"
#include "sim.h"

namespace textures {

// The texture names, created on first upload and reused after.
struct State {
    GLuint hydroTex = 0;
    GLuint plateTex = 0;
    GLuint earthTex = 0; // the Earth template, when the world is one
    GLuint popTex = 0;
    GLuint bandTex = 0;
    int bandRows = 0;
    GLuint siteTex = 0;
    int siteRows = 0;
    GLuint climTex = 0;
    GLuint clim2Tex = 0;
};

// Push the plate table to texture unit 1, bilinear so belts are smooth.
inline void uploadPlates(State& tx, const plates::Field& pf) {
    glActiveTexture(GL_TEXTURE1);
    if (!tx.plateTex) {
        glGenTextures(1, &tx.plateTex);
        glBindTexture(GL_TEXTURE_2D, tx.plateTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, tx.plateTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, plates::W, plates::H, 0, GL_RGBA, GL_FLOAT,
                 pf.cells.data());
    glActiveTexture(GL_TEXTURE0);
}

inline std::vector<float> popTexData(const population::Field& pf) {
    std::vector<float> d(population::W * population::H * 4, 0.0f);
    for (int i = 0; i < population::W * population::H; i++) d[i * 4] = pf.K[i];
    for (const population::Field::Ruin& r : pf.ruins) d[r.cell * 4 + 1] = -1.0f; // abandoned
    // Green names the settlement standing on the cell (index + 1, or -1 for
    // a ruin); everything else about it -- its people, its granaries, how far
    // its fields reach -- lives in the site texture, one texel each, so
    // adding another thing to draw costs a channel there and not a texture
    // the size of the world.
    for (size_t i = 0; i < pf.settlements.size(); i++)
        d[pf.settlements[i].cell * 4 + 1] = (float)(i + 1);
    // A cell holds the index of a band standing on it, not its headcount:
    // a walking band is at a point, not in a square, and the marker is drawn
    // at that point. Where two bands share a cell the bigger one is shown.
    for (size_t i = 0; i < pf.bands.size(); i++) {
        const population::Band& b = pf.bands[i];
        int cell = sim::cellOf({b.px, b.py, b.pz});
        int held = (int)(d[cell * 4 + 2] + 0.5f);
        if (held && pf.bands[held - 1].P >= b.P) continue;
        d[cell * 4 + 2] = (float)(i + 1);
    }
    // Alpha points from any cell holding a FARMSTEAD back to its village's
    // cell (index + 1), so the shader's farmstead passes look at their own
    // nine cells instead of scanning a 50 km box per pixel -- that scan was
    // a full-screen frame-rate bill at close zoom.
    for (const population::Settlement& s : pf.settlements)
        for (int k = 0; k < (int)(s.farmsteads + 0.5f) && k < population::FSTEAD_MAX; k++)
            d[sim::cellOf(sim::farmsteadPos(s.cell, k)) * 4 + 3] = (float)(s.cell + 1);
    return d;
}

// Ten texels per settlement: its people, granaries, village field reach and
// farmstead count; the sixteen sectors of its claim, four to a texel; then
// each farmstead's own field radius, four to a texel. A settlement is
// SITE_STRIDE texels along the row, so the shader indexes site*10 + k.
constexpr int SITE_TEX_W = 256;
constexpr int SITE_STRIDE = 10;
inline std::vector<float> siteTexData(const population::Field& pf, int& rows) {
    const std::vector<population::Settlement>& ss = pf.settlements;
    size_t texels = ss.size() * SITE_STRIDE;
    rows = std::max(1, (int)((texels + SITE_TEX_W - 1) / SITE_TEX_W));
    std::vector<float> d((size_t)SITE_TEX_W * rows * 4, 0.0f);
    for (size_t i = 0; i < ss.size(); i++) {
        size_t o = i * SITE_STRIDE * 4;
        d[o + 0] = std::max(ss[i].P, 1.0f);
        d[o + 1] = ss[i].granaries;
        d[o + 2] = ss[i].tilled[0]; // village plots, km2 (one drawn patch each)
        d[o + 3] = ss[i].farmsteads;
        for (int k = 0; k < population::CLAIM_SECTORS; k++) d[o + 4 + k] = ss[i].claim[k];
        for (int k = 0; k < population::FSTEAD_MAX; k++) d[o + 20 + k] = ss[i].tilled[k + 1];
    }
    return d;
}

// Where every band actually is, to the metre: xyz on the unit sphere plus
// its headcount, one texel each, in rows of BAND_TEX_W.
constexpr int BAND_TEX_W = 256;
inline std::vector<float> bandTexData(const population::Field& pf, int& rows) {
    const std::vector<population::Band>& bs = pf.bands;
    rows = std::max(1, ((int)bs.size() + BAND_TEX_W - 1) / BAND_TEX_W);
    std::vector<float> d((size_t)BAND_TEX_W * rows * 4, 0.0f);
    for (size_t i = 0; i < bs.size(); i++) {
        d[i * 4 + 0] = bs[i].px;
        d[i * 4 + 1] = bs[i].py;
        d[i * 4 + 2] = bs[i].pz;
        d[i * 4 + 3] = std::max(bs[i].P, 1.0f);
    }
    return d;
}

// The population, band and site textures on units 2, 5 and 6.
inline void uploadPopulation(State& tx, const population::Field& pf) {
    glActiveTexture(GL_TEXTURE2);
    if (!tx.popTex) {
        glGenTextures(1, &tx.popTex);
        glBindTexture(GL_TEXTURE_2D, tx.popTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    glBindTexture(GL_TEXTURE_2D, tx.popTex);
    std::vector<float> d = popTexData(pf);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, population::W, population::H, 0, GL_RGBA, GL_FLOAT,
                 d.data());
    glActiveTexture(GL_TEXTURE5);
    if (!tx.bandTex) {
        glGenTextures(1, &tx.bandTex);
        glBindTexture(GL_TEXTURE_2D, tx.bandTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    glBindTexture(GL_TEXTURE_2D, tx.bandTex);
    std::vector<float> bd = bandTexData(pf, tx.bandRows);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, BAND_TEX_W, tx.bandRows, 0, GL_RGBA, GL_FLOAT,
                 bd.data());
    glActiveTexture(GL_TEXTURE6);
    if (!tx.siteTex) {
        glGenTextures(1, &tx.siteTex);
        glBindTexture(GL_TEXTURE_2D, tx.siteTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    glBindTexture(GL_TEXTURE_2D, tx.siteTex);
    std::vector<float> sd = siteTexData(pf, tx.siteRows);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, SITE_TEX_W, tx.siteRows, 0, GL_RGBA, GL_FLOAT,
                 sd.data());
    glActiveTexture(GL_TEXTURE0);
}

// Climatology texture: four season bands stacked vertically, RGBA =
// {cloud, rain mm/day, wind u, wind v}.
inline void uploadClimatology(State& tx, const atmosphere::Climatology& c) {
    glActiveTexture(GL_TEXTURE3);
    if (!tx.climTex) {
        glGenTextures(1, &tx.climTex);
        glBindTexture(GL_TEXTURE_2D, tx.climTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, tx.climTex);
    int W = atmosphere::W, H = atmosphere::H, S = atmosphere::SEASONS;
    std::vector<float> d(W * H * S * 4);
    for (int i = 0; i < W * H * S; i++) {
        d[i * 4 + 0] = c.cloud[i];
        d[i * 4 + 1] = c.rainMmDay[i];
        d[i * 4 + 2] = c.windU[i];
        d[i * 4 + 3] = c.windV[i];
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, W, H * S, 0, GL_RGBA, GL_FLOAT, d.data());
    // Second climatology texture: seasonal mean temperature, snowfall, and the
    // model's smoothed elevation (so the shader can lapse-correct to local
    // terrain height for snow cover).
    glActiveTexture(GL_TEXTURE4);
    if (!tx.clim2Tex) {
        glGenTextures(1, &tx.clim2Tex);
        glBindTexture(GL_TEXTURE_2D, tx.clim2Tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, tx.clim2Tex);
    // Alpha carries the annual water balance (rain - PET, mm/day): the
    // shader's pond density follows it.
    std::vector<float> balance(W * H, 0.0f);
    for (int i = 0; i < W * H; i++) {
        float rain = 0, tC = 0;
        for (int se = 0; se < S; se++) {
            rain += c.rainMmDay[se * W * H + i] / S;
            tC += c.meanT[se * W * H + i] / S;
        }
        balance[i] = rain - hydrology::petMmDay(tC);
    }
    for (int se = 0; se < S; se++)
        for (int i = 0; i < W * H; i++) {
            int si = se * W * H + i;
            d[si * 4 + 0] = c.meanT[si];
            d[si * 4 + 1] = c.snowMmDay[si];
            d[si * 4 + 2] = c.elev.empty() ? 0.0f : c.elev[i];
            d[si * 4 + 3] = balance[i];
        }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, W, H * S, 0, GL_RGBA, GL_FLOAT, d.data());
    glActiveTexture(GL_TEXTURE0);
}

// Push the world's hydrology table to the GPU as one RGBA32F texel per cell.
// The Earth template, when the world is one: metres in the red channel.
inline void uploadEarth(State& tx, bool earth) {
    if (!earth || !terrain::TEMPLATE.active) return;
    glActiveTexture(GL_TEXTURE0 + 8);
    if (!tx.earthTex) {
        glGenTextures(1, &tx.earthTex);
        glBindTexture(GL_TEXTURE_2D, tx.earthTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, tx.earthTex);
    const terrain::Template& tp = terrain::TEMPLATE;
    // One channel: at ETOPO5's 4320 x 2160 four channels would be 150 MB.
    glTexImage2D(GL_TEXTURE_2D, 0, 0x822E /*GL_R32F*/, tp.w, tp.h, 0, 0x1903 /*GL_RED*/, GL_FLOAT,
                 tp.elev.data());
    glActiveTexture(GL_TEXTURE0);
}

// Every layer of a freshly built or loaded world, the hydrology table last
// on unit 0.
inline void uploadAll(State& tx, const world::World& w) {
    uploadEarth(tx, w.earth);
    uploadPlates(tx, w.plateField);
    uploadPopulation(tx, w.pop);
    uploadClimatology(tx, w.clim);
    if (!tx.hydroTex) {
        glGenTextures(1, &tx.hydroTex);
        glBindTexture(GL_TEXTURE_2D, tx.hydroTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    glBindTexture(GL_TEXTURE_2D, tx.hydroTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, hydrology::W, hydrology::H, 0, GL_RGBA, GL_FLOAT,
                 w.hydro.cells.data());
}

} // namespace textures
