// The record of what happened: notable moments written into the field's
// news feed as the simulation goes (Technical/Globe Viewer.md), each traced
// to whoever it happened to. Counts stay exact; stored entries stop at a
// cap so a thousand-year step cannot eat memory.
#pragma once
#include "settlement.h"
#include <cmath>
#include <cstdio>

namespace sim {

// Record something worth telling. Counts stay exact; the stored entries
// stop at a cap so a thousand-year step cannot eat memory.
inline void note(population::Field& pf, int kind, double t, uint32_t sid, uint32_t sid2,
                 uint32_t bandId, float amount, const char* text, float lossHere = 0,
                 float lossThem = 0) {
    using namespace population;
    if (kind < 0 || kind >= EV_KINDS) return;
    pf.eventCount[kind]++;
    if (pf.eventCount[kind] > EVENTS_KEPT_PER_KIND) return;
    Event e;
    e.kind = (uint8_t)kind;
    e.t = t;
    e.lossHere = lossHere;
    e.lossThem = lossThem;
    // Note where it happened while the people involved are still findable:
    // they may be gone by the time anyone reads this.
    int at = indexById(pf, sid);
    if (at < 0) at = indexById(pf, sid2);
    if (at >= 0) e.cell = pf.settlements[at].cell;
    e.sid = sid;
    e.sid2 = sid2;
    e.bandId = bandId;
    e.amount = amount;
    for (int i = 0; i < 95 && text[i]; i++) e.text[i] = text[i];
    pf.events.push_back(e);
}

inline void logAt(const char* what, int id, terrain::V3 n, float P, double day) {
    float lat = std::asin(std::clamp(n.z, -1.0f, 1.0f)) * 180.0f / 3.14159265f;
    float lon = std::atan2(n.y, n.x) * 180.0f / 3.14159265f;
    fprintf(stderr, "band: %s %d at lat %.2f lon %.2f, %d people, day %.0f\n", what, id, lat, lon,
            (int)P, day);
}

// A granary finished during the last integration: advance() counts them,
// the simulation is what tells anyone about it.
inline void reportGranaries(population::Field& pf, population::Settlement& s, double now) {
    while (s.builtGranaries > 0) {
        s.builtGranaries--;
        char txt[96];
        snprintf(txt, sizeof txt, "%s finished a granary (%d standing)", s.name, (int)s.granaries);
        note(pf, population::EV_GRANARY, now, s.id, 0, 0, s.granaries, txt);
    }
    while (s.builtFsteads > 0) {
        s.builtFsteads--;
        char txt[96];
        snprintf(txt, sizeof txt, "%s raised a farmstead on the far fields (%d standing)", s.name,
                 (int)s.farmsteads);
        note(pf, population::EV_FARMSTEAD, now, s.id, 0, 0, s.farmsteads, txt);
    }
}

} // namespace sim
