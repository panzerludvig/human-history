// Population: a carrying-capacity field derived from terrain and water, and
// settlements — sparse actors with people P and land condition R that evolve
// by scheduled re-evaluations, not ticks. See Design/Population.md.
#pragma once
#include "terrain.h"
#include "hydrology.h"
#include "atmosphere.h"
#include "daylight.h"
#include <cstdio>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace population {

constexpr int W = hydrology::W, H = hydrology::H;

// Rules (Design/Population.md).
constexpr float FORAGE_KM2 = 314.0f;          // 10 km radius disc

// ------------------------------------------------------------- territory
//
// A settlement holds a claim: the ground it works and keeps others off. It
// grows with what the people need, a little ahead of them, and stops where
// it meets a claim already made -- whoever was there first keeps it, and a
// border once settled does not move on its own (challenging one is a
// mechanism still to come). Sixteen sectors, each with its own reach, so a
// claim blocked to the east can still grow west; a single radius could only
// grow until its first neighbour and then never again.
//
// Land far out is worth less than land underfoot: it is a walk each way,
// and a day spent walking is a day not spent gathering. Value per km2 falls
// as 1/(1+(d/CLAIM_TAPER)^2), which integrates over a disc to
// pi*T^2*ln(1+r^2/T^2) -- normalised below so a claim at the CAP is worth
// exactly the 314 km2 a settlement used to be given outright. The cap is
// as much ground as one place could ever work, so it is the right end to
// anchor: claims do not add food to the world, they decide who gets it.
// A floor claim is worth about a third of that.
constexpr int CLAIM_SECTORS = 16;
// The floor is what a community holds however small it is, and it sets how
// close two of them can ever stand: at 20 km they are 40 km apart, half the
// spacing the old fixed rule enforced. Ten was tried and packs the world
// four times denser again -- 54,000 settlements by year 350, and a step the
// simulation could not finish.
constexpr float CLAIM_FLOOR_KM = 20.0f;  // the least ground a settlement holds
constexpr float CLAIM_CAP_KM = 60.0f;    // the most one place can ever reach
constexpr float CLAIM_TAPER_KM = 20.0f;  // beyond this, land starts to pay less
constexpr float CLAIM_MARGIN = 1.4f;     // claim somewhat more than is needed now
constexpr float CLAIM_GROW_KM_YR = 0.1f; // ten km a century: a lifetime of ranging further

// Worked value of a disc of radius r, in km2 of land-at-the-door.
inline float claimValueKm2(float r) {
    float t2 = CLAIM_TAPER_KM * CLAIM_TAPER_KM;
    return 3.14159265f * t2 * std::log(1.0f + r * r / t2);
}
// The same, scaled so a floor-sized claim is worth today's fixed catchment.
inline float claimYieldKm2(float r) {
    static const float unit = FORAGE_KM2 / claimValueKm2(CLAIM_CAP_KM);
    return claimValueKm2(r) * unit;
}
// The radius whose worked value is `km2` -- the inverse, for asking how far
// a settlement needs to reach to feed the people it has.
inline float claimRadiusFor(float km2) {
    static const float unit = FORAGE_KM2 / claimValueKm2(CLAIM_CAP_KM);
    float t2 = CLAIM_TAPER_KM * CLAIM_TAPER_KM;
    float e = std::exp(std::max(km2, 0.0f) / unit / (3.14159265f * t2)) - 1.0f;
    return std::sqrt(std::max(e, 0.0f) * t2);
}
constexpr float WATER_L_PER_PERSON = 20.0f;   // per day
constexpr float USABLE_WATER = 0.05f;         // fraction of discharge usable
constexpr float GROWTH_MAX = 0.028f;          // per year at full surplus
// Demography (Design/Population.md). People are counted in four stocks --
// children, adult men, adult women, elderly -- and every share is emergent:
// nothing enforces a ratio. Births come from women, children take fifteen
// years to become adults and many do not, adults age out over a working
// lifetime, and the old die quickly, which is what keeps them few.
constexpr float CHILD_YEARS = 15.0f;   // to adulthood
constexpr float ADULT_YEARS = 45.0f;   // adulthood to old age
constexpr float MORT_CHILD = 0.0359f;  // /yr: about a third never grow up
constexpr float MORT_ADULT = 0.010f;   // /yr, outside famine
constexpr float MORT_ELDER = 0.25f;    // /yr: a few years past sixty
constexpr float BOY_SHARE = 0.512f;    // slightly more boys are born
// Replacement fertility for the stage durations above, per woman per year;
// food multiplies it, and the multiplier is what growth actually is now.
constexpr float BIRTHS_REPLACE = 0.1015f;
constexpr float FERT_SURPLUS = 1.35f;  // extra births at full surplus
// Famine takes the weak first.
constexpr float FAMINE_W_CHILD = 1.5f;
constexpr float FAMINE_W_ADULT = 1.0f;
constexpr float FAMINE_W_ELDER = 2.0f;
// Fighting (Design/Conflict.md): men do most of it, everyone else does some.
constexpr float FIGHT_W_MAN = 1.0f;
constexpr float FIGHT_W_WOMAN = 0.15f;
constexpr float FIGHT_W_OTHER = 0.05f;
constexpr float RAID_MEN_SHARE = 0.55f; // of the men; the rest stay home
constexpr float DECLINE_MAX = 0.07f;          // per year in famine
// The land timescales are slow relative to growth so populations overshoot
// visibly (~18% peak around year 33 from a half-capacity start) before the
// land's decline pulls them back. Their ratio (1.5) fixes R* = 0.549.
constexpr float R_REGEN_YEARS = 33.0f;        // land recovery
constexpr float R_DEPLETE_YEARS = 22.0f;      // land depletion at P = K
// Land condition settles where regeneration balances depletion:
// (1-R)/T_regen = R^2/T_deplete, giving R* = 0.549 for 12 and 8 years.
// The yield table is measured *sustained* density, so the pristine ceiling
// stored in K is table / R*; displayed capacity is K * R*.
constexpr float SUSTAIN_R = 0.5486f;
constexpr float MIN_SETTLEMENT_K = 150.0f;
// How many groups the world opens with. This is a starting condition, not
// a ceiling: founding and migration afterwards are limited by geography
// alone -- there is no cap on how many settlements or bands may exist, since
// no number we could name would be informative, and a cap that binds stops
// the simulation silently and everywhere at once.
constexpr int MAX_SETTLEMENTS = 400;

// Storage and famine (Design/Migration.md). The land offers a flow (K*R
// rations/day); the group holds a stock S of harvested rations. Famine is not
// a hard breakpoint: hoarding excludes people from low stores before they
// empty, so deaths = STARVE_MAX * excluded share * harvest shortfall.
constexpr float GATHER_SETTLED = 1.5f;   // rations/person/day gatherable settled
constexpr float GATHER_MOVING = 0.5f;    // a moving band forages on a third of its time
constexpr float CAP_DAYS_SETTLED = 90.0f;
constexpr float CAP_DAYS_BAND = 10.0f;
constexpr float HOARD_FILL = 0.25f;      // famine sets in below this fill fraction
constexpr float STARVE_MAX = 0.02f;      // /day at full exclusion and total shortfall

// Splitting and bands (Design/Migration.md). The trigger is PHI_CONTENT
// below: a group looks for somewhere else as soon as food is what is
// holding its growth back, not only once it is visibly failing.
// Scarcity you can see. phi is built from ANNUAL-MEAN food, but starvation
// is seasonal: a settlement in a sharply seasonal place can bury people
// every winter while its yearly average reads comfortable, and so never ask
// whether to leave. Burials are the signal a community actually has, so
// losing this share of its people to hunger in a year counts as scarcity in
// its own right, whatever the mean says.
constexpr float STARVE_NOTICE = 0.02f; // of P, per trailing year
// "Going hungry" for need-driven invention (technology.h) is a genuine
// shortfall, not the ~0.98 comfort glide the split rule watches: the
// overshoot trough reaches ~0.85, so hunger is an episode, not a lifestyle.
constexpr float NEED_HUNGRY_PHI = 0.92f;
// Content: growth saturates at phi = 1.11 (the 0.11 ramp in derivatives);
// above it more food buys nothing -- no adoption utility (technology.h),
// no reason to work past sunset (daylight.h). The need ramp between these
// two thresholds is shared by adoption and the work day.
constexpr float PHI_CONTENT = 1.11f;

inline float needRamp(float phi) {
    return std::clamp((PHI_CONTENT - phi) / (PHI_CONTENT - NEED_HUNGRY_PHI), 0.0f, 1.0f);
}

// Regional wild game (Design/Population.md): a shared, slow, mortal pool.
// Every settlement in a coarse region (the climate grid, ~200 km) hunts the
// same herds. The pool recovers on a lifetime scale, not a season scale;
// hunting efficiency falls only as sqrt(health) (scarcer game is hunted
// harder); and below the Allee floor recovery stops entirely -- a pool
// hunted that low is gone forever, and the old way of life with it.
constexpr float GAME_REGEN_YEARS = 80.0f;   // full recovery timescale
constexpr float GAME_DEPLETE_YEARS = 25.0f; // full depletion at draw = capacity
// The extinction floor sits ABOVE the starvation stall: when hunters have
// eaten a pool down to ~0.11 their own famine caps the pressure, so a floor
// below that would never be crossed. At 0.15 a pool driven that low keeps
// sliding to zero under any remaining draw -- the point of no return.
constexpr float GAME_FLOOR = 0.15f;
constexpr float GAME_ACCESS = 0.5f;         // share of a region's game within reach
// Small game (Design/Technology.md): birds, hares, the rest of the animals
// too quick and too fecund to hunt out. It needs no pool of its own -- it
// lives on the land condition R, the local resource that depletes with use
// and recovers in a generation, which is exactly what small game is. What
// it needs instead is a bow: snares and thrown sticks take some of it, a
// bow takes most of it. Bows help against big game too, but only a little.
constexpr float SMALL_SNARE_FLOOR = 0.25f;  // taken without a bow
constexpr float BOW_BIG_GAIN = 0.25f;       // bows vs the herd animals
constexpr float BOW_PER_HUNTER = 0.2f;      // one bow per hunter; a fifth hunt
// A bow is craft work, not construction: one person per bow, so a crowd
// makes more bows at once but never a single bow faster.
constexpr float BOW_LABOUR_SHARE = 0.05f;   // people who can be spared to carve
constexpr float BOW_WORK_DAYS = 90.0f;      // one bowyer, at full skill
constexpr float BOW_LIFE_DAYS = 3650.0f;    // bows wear out and are replaced
constexpr double GAME_TICK_DAYS = 90.0;     // pool update cadence (a slow layer)
constexpr double SPLIT_AFTER_DAYS = 730.0;
// A group that has looked around and found nothing worth the move does not
// re-survey the horizon every other year; it settles into its life and
// looks again less often, until things get worse. Cheap in-world reason for
// what is also the expensive part of the decision (a prospect search).
constexpr double LOOK_BACKOFF_MAX = 32.0 * 365.0;
constexpr float SPLIT_MIN_P = 50.0f;
constexpr float SPLIT_SHARE = 1.0f / 3.0f;
constexpr float BAND_MIN_P = 20.0f;
constexpr float BAND_SPEED_KM_DAY = 15.0f;
// Awareness (Design/Migration.md): a base radius, growth with settled age
// (saturating -- the marginal new ground per year shrinks), a scouting bonus
// for resting bands, and a vantage bonus from prominence via the real
// horizon formula. One function each; the shader receives the result.
constexpr float AWARE_BASE_KM = 150.0f;
constexpr float AWARE_GROWTH_KM = 300.0f;      // settlements, toward base+this
constexpr double AWARE_TAU_DAYS = 30.0 * 365;  // settlement growth timescale
constexpr float AWARE_REST_KM = 100.0f;        // resting bands, toward base+this
constexpr double AWARE_REST_TAU_DAYS = 45.0;
constexpr float AWARE_CAP_KM = 600.0f;
constexpr double BAND_STEP_DAYS = 5.0;

inline float vantageKm(float promM) { return 3.57f * std::sqrt(std::max(promM, 0.0f)); }

inline float settlementAwareKm(double ageDays, float promM) {
    float r = AWARE_BASE_KM + AWARE_GROWTH_KM * (1.0f - (float)std::exp(-ageDays / AWARE_TAU_DAYS));
    return std::min(r + vantageKm(promM), AWARE_CAP_KM);
}

inline float bandAwareKm(double restDays, float promM) {
    float r = AWARE_BASE_KM + AWARE_REST_KM * (1.0f - (float)std::exp(-restDays / AWARE_REST_TAU_DAYS));
    return std::min(r + vantageKm(promM), AWARE_CAP_KM);
}
// Relocation (Design/Migration.md): moving as a whole is the DEFAULT answer
// to a failing place -- people are kin and stay together -- and fission is
// the fallback for when no known ground can hold everyone. What anchors a
// group is sunk investment: granaries and cleared fields raise the bar a
// destination must clear, so foragers and herders shift readily while a
// farming village with full granaries splits instead of abandoning them.
constexpr float RELOC_ANCHOR_GRANARY = 0.25f; // per built granary
constexpr float RELOC_ANCHOR_FARM = 0.5f;     // per unit farming expertise
// A band that sets out has somewhere in mind. Ground it passes is judged
// against that goal, not against nothing: at first it must be clearly
// better to be worth abandoning the plan for, and as the journey drags the
// bar sinks -- through parity, and then below it, until they settle for
// distinctly less than they hoped for rather than walk forever.
constexpr float HOPE_MARGIN = 0.25f;       // must beat the goal by this at first
constexpr float HOPE_FLOOR = 0.5f;         // what they will eventually accept
constexpr double HOPE_TAU_DAYS = 730.0;    // how fast hope fades

// How much better than the goal a passing site must be, after `days` on the
// road. Starts above one, crosses it within the year, and keeps falling.
inline float hopeRatio(double days) {
    return HOPE_FLOOR + (1.0f + HOPE_MARGIN - HOPE_FLOOR) *
                            (float)std::exp(-std::max(days, 0.0) / HOPE_TAU_DAYS);
}
// Ruins: only places that were invested in leave a trace, and it weathers
// away. A camp of thirty that stood a decade leaves nothing to find.
constexpr double RUIN_MIN_AGE_DAYS = 60.0 * 365.0;
constexpr double RUIN_LIFE_DAYS = 400.0 * 365.0;
// Raiding (Design/Conflict.md). The trigger is circumscription: a group
// that must move or divide and has nowhere to go. Raids are journeys with
// a task -- reach them, fight, carry it home -- so distance is a real cost
// and only neighbours are worth robbing. Casualties are low because people
// run rather than die: a raid impoverishes, it does not annihilate.
constexpr float RAID_MIN_P = 25.0f;       // a smaller party achieves nothing
constexpr float FIGHT_UNARMED = 0.3f;     // strength with no bows at all
constexpr float RAID_INITIATIVE = 1.5f;   // surprise, and the choice of the moment
constexpr float RAID_ODDS_POWER = 1.5f;   // 2:1 strength is ~70%, not a certainty
constexpr float RAID_LOSS_WINNER = 0.03f; // they break off once it turns
constexpr float RAID_LOSS_LOSER = 0.08f;
constexpr float LOOT_CARRY_DAYS = 30.0f;  // rations one raider hauls home
constexpr float RAID_WORTH_IT = 1500.0f;  // a haul worth walking days for
constexpr float LOOT_STORE_SHARE = 0.6f;  // of what is found; the rest is hidden
constexpr float LOOT_HERD_SHARE = 0.35f;  // livestock needs no carrying
constexpr float FARMYARD_SHARE_POP = 0.05f; // household animals, no pasture needed
constexpr float HERD_GROWTH_YR = 0.25f;     // logistic growth rate
constexpr float HERD_PASTURE_K = 2.0f;      // people/km2 on pure pasture at full expertise

// Granaries (Design/Technology.md): built structures that extend storage.
// Demand is measured, not planned, from the annual fill cycle: a build
// starts when last year filled the existing capacity (the fat season had
// more to bank) AND the lean season then nearly exhausted it (the buffer
// binds). Once capacity comfortably covers the winter drawdown the low
// mark stays high and building stops; population growth deepens the
// drawdown and reopens demand. The work total is fixed; expertise sets the
// pace, local wood and stone set the gathering, and only fed people build.
constexpr float GRANARY_STORE = 10000.0f;     // rations one granary banks
constexpr float GRANARY_WORK = 1000.0f;       // man-days per granary, constant
constexpr float GRANARY_LABOUR_SHARE = 0.02f; // share of people on the build
constexpr float GRANARY_HI = 0.95f;           // "we filled what we have"
constexpr float GRANARY_LO = 0.35f;           // "...and winter nearly drained it"

// Storage: the base cap plus what the built granaries hold. A granary banks
// a fixed absolute amount, so its worth in days shrinks as people multiply.
inline float storageCapDays(float P, float granaries) {
    return CAP_DAYS_SETTLED + granaries * GRANARY_STORE / std::max(P, 1.0f);
}

// Cultures and names (Design/Culture.md). A culture owns a small sound
// inventory, and every settlement descended from it draws its name from
// that inventory -- so Gervatti and Poetti share an ending without anyone
// coordinating it. Overlap between distant cultures is expected and
// harmless at this scale.
constexpr const char* NAME_ONSET[] = {"b",  "d",  "g",  "k",  "m",  "n",  "p",  "r",
                                      "s",  "t",  "v",  "z",  "br", "dr", "gr", "kr",
                                      "pr", "tr", "st", "sk", "th", "sh", "ch", "l",
                                      "f",  "h",  "j",  "w",  "kh", "ts", "vr", "gv"};
constexpr const char* NAME_NUCLEUS[] = {"a", "e", "i", "o", "u", "ai", "ei", "ou", "ia", "ae"};
constexpr const char* NAME_CODA[] = {"", "", "", "n", "r", "s", "l", "m", "k", "t"};
constexpr const char* NAME_ENDING[] = {"i",  "a",   "o",  "ti", "tti", "ni", "na", "os",
                                       "us", "ar",  "en", "ia", "eth", "or", "an", "il"};
constexpr int N_ONSET = 32, N_NUCLEUS = 10, N_CODA = 10, N_ENDING = 16;

struct Culture {
    char name[16] = {};   // what these people are called, as a people
    uint8_t onset[5] = {};
    uint8_t nucleus[4] = {};
    uint8_t coda[3] = {};
    uint8_t ending[2] = {};
};

// What a settlement has come to be good at, by doing it. Affinities drift
// toward what a group actually lives on, over generations, and feed back
// as a small bonus -- enough to make two settlements on identical land
// diverge, not enough to run away with the simulation.
struct Affinity {
    float hunt = 0, gather = 0, farm = 0, herd = 0, fight = 0;
};
constexpr float AFFINITY_GAIN = 0.15f;      // at full devotion
constexpr float AFFINITY_TAU_YEARS = 100.0f; // a few generations to settle
constexpr float FIGHT_LEARN = 0.06f;        // per raid, given or received
constexpr float FIGHT_FORGET_YEARS = 200.0f; // peace makes people soft

inline float affinityBonus(float a) { return 1.0f + AFFINITY_GAIN * std::clamp(a, 0.0f, 1.0f); }

// What happened while time was running (Technical/Globe Viewer.md). The
// simulation records notable moments as it goes; the news feed groups them
// by kind, and every one of them can be traced back to whoever it happened
// to. Cleared at the start of each time step, so the feed always answers
// "what happened just now".
enum : int {
    EV_RELOCATE = 0, // a whole people picked up and left
    EV_SPLIT,        // colonists set out
    EV_SETTLED,      // movers made a home again
    EV_FOUNDED,      // colonists founded somewhere new
    EV_MERGED,       // a band gave up and joined someone
    EV_PERISHED,     // a band died on the road
    EV_RAID_LAUNCH,
    EV_RAID_HIT,   // someone was robbed
    EV_RAID_HELD,  // an attack was beaten off
    EV_RAID_HOME,  // raiders came home
    EV_INVENTED,   // a technology, first anywhere
    EV_ADOPTED,    // a settlement took one up
    EV_GRANARY,    // a granary finished
    EV_GAME_GONE,  // a regional herd hunted to nothing
    EV_KINDS
};

struct Event {
    uint8_t kind = 0;
    double t = 0;
    uint32_t sid = 0;    // whoever it happened to
    uint32_t sid2 = 0;   // the other party, if there was one
    uint32_t bandId = 0; // the band involved, if any
    int cell = -1;       // where it happened, so it can always be found again
    float amount = 0;    // people, rations -- whatever the kind means
    char text[96] = {};
    // What a fight cost, counted on both sides: `lossHere` is the people of
    // the settlement the event names, `lossThem` whoever it was against.
    // Zero for everything that is not a fight.
    float lossHere = 0, lossThem = 0;
};

// A step can cover a thousand years; keep the counts exact but stop
// storing individual entries past this, so memory stays bounded.
constexpr int EVENTS_KEPT_PER_KIND = 250;

// The technology table (Design/Technology.md): per-settlement state for each
// technology. Farming's original fields generalized when husbandry arrived.
enum : int {
    TECH_FARMING = 0,
    TECH_HUSBANDRY = 1,
    TECH_GRANARY = 2,
    TECH_ARCHERY = 3, // known everywhere from the start; the bows are the scarce part
    NTECH = 4
};
struct TechState {
    bool aware = false;
    bool practising = false; // implies aware
    double practiceT = 0;    // sim day practice began (expertise grows from here)
};

// Who a group is made of. P is the sum, kept in step so everything that
// only cares about headcount keeps working.
struct Cohorts {
    float C = 0, M = 0, W = 0, E = 0; // children, men, women, elderly
    float total() const { return C + M + W + E; }
    void scale(float f) { C *= f; M *= f; W *= f; E *= f; }
    void add(const Cohorts& o) { C += o.C; M += o.M; W += o.W; E += o.E; }
    void sub(const Cohorts& o) { C -= o.C; M -= o.M; W -= o.W; E -= o.E; }
};

// The share a new group starts with, absent any history: the equilibrium
// of the flows above, used only to seed the world.
inline Cohorts seedCohorts(float P) {
    Cohorts c;
    c.C = 0.307f * P;
    c.M = 0.326f * P;
    c.W = 0.310f * P;
    c.E = 0.057f * P;
    return c;
}

// What a group can bring to a fight. Men do most of it; the rest of the
// people are why a settlement is harder to rob than an equal party of
// raiders is to beat.
inline float cohortStrength(const Cohorts& c) {
    return FIGHT_W_MAN * c.M + FIGHT_W_WOMAN * c.W + FIGHT_W_OTHER * (c.C + c.E);
}

struct Settlement {
    int cell;
    uint32_t id = 0;   // stable identity (settlements are erased when they move)
    bool leaving = false; // converted to a band this step; swept at step end
    Cohorts pop;       // who they are; P below is its total
    float P;           // people
    float R;           // land condition 0..1
    double t;          // sim day at which P and R are valid
    double nextUpdate; // sim day of the next scheduled re-evaluation
    float S = 0;               // food store, rations (person-days)
    double scarceSince = -1;   // sim day scarcity began, -1 if fed (split rule;
                               // resets on every split attempt)
    bool noProspect = false;   // last emigration attempt found nowhere to go
                               // (transient; shown in the panel, not saved)
    double hungrySince = -1;   // sim day sustained hunger began, -1 if fed --
                               // never reset by splitting (need-driven invention)
    double founded = 0;        // sim day the settlement was founded (awareness age)
    // Fixed local properties (from the terrain at the cell):
    float kFoodP = 0;  // pristine food capacity (already / SUSTAIN_R)
    float kGame = 0;   // the big-game part of kFoodP (regional pool)
    float kSmall = 0;  // the small-game part (local, needs bows)
    float bows = 0;    // made bows on hand; they wear out and are replaced
    int gRegion = 0;   // which regional game pool this settlement hunts
    float gameNow = 1; // that pool's health, refreshed by sim::gameTick
    float meanF = 1;   // annual mean forage factor (seasonal climate)
    float meanG2 = 1;  // annual mean squared growing activity (farming shape)
    float tSeason[4] = {15, 15, 15, 15}; // season temps at the site (cached)
    float kWater = 0;  // water-supply capacity
    float sFarm = 0;   // farming suitability 0..1 (grass-like cover, warm enough)
    float pasture = 0; // grazing suitability 0..1 (grass, steppe, savanna, some tundra)
    float herd = 0;    // livestock, in people-fed-per-day units (husbandry)
    float buildMat = 0.15f; // local wood and stone availability 0.15..1 (build pace)
    float granaries = 0;    // completed granaries (drawn on the map)
    float buildWork = 0;    // man-days left on the granary going up, 0 = none
    float fillLo = 2, fillHi = -1; // store-fill extremes in the current cycle
    double cycleT = 0;             // when the current fill cycle began
    float granNeedYrs = 0;         // consecutive years the fill signal held
                                   // (need-driven granary invention)
    float starvedYr = 0;           // people lost to hunger in the trailing year
    double lookAgainDays = SPLIT_AFTER_DAYS; // patience before the next survey
    // Appended deliberately: several call sites build a Settlement with
    // positional initialisers, and inserting a member above shifts them all.
    uint16_t culture = 0;
    char name[16] = {};
    Affinity aff;
    uint8_t builtGranaries = 0; // finished this step; the sim reports and clears
    // Technology state (see technology.h / Design/Technology.md):
    TechState tech[NTECH];
    double nextTech[NTECH] = {1e18, 1e18, 1e18, 1e18}; // next draw or resample moment
    bool techFires[NTECH] = {false, false, false, false};
    // How far the claim reaches in each of CLAIM_SECTORS directions, sector 0
    // due east and turning north. Set to the floor when the place is founded.
    float claim[CLAIM_SECTORS] = {};
    float claimKm2 = FORAGE_KM2; // worked value of the whole claim, cached (set on founding)
    double claimT = 0;           // when the frontier was last worked outward
};

// A migrating group: a settlement with velocity (Design/Migration.md). It
// forages the cell it stands on with a reduced time budget, carries a small
// store, and re-evaluates every few days. The first agent.
enum : int { BAND_MIGRATE = 0, BAND_RAID = 1 };

struct Band {
    uint32_t id = 0;         // stable identity (indices shift as bands die)
    Cohorts pop;             // a migrating group is families; a raid is men
    int purpose = BAND_MIGRATE;
    uint32_t homeId = 0;     // the settlement a raiding party returns to
    uint32_t targetId = 0;   // the settlement it set out to rob
    uint32_t sid = 0;        // for a whole community on the move, its own
                             // settlement id, carried so identity survives
                             // the journey (0 for colonists, who are new)
    bool returning = false;  // homeward, with whatever it got
    float loot = 0;          // rations carried
    float lootHerd = 0;      // livestock driven along
    float px, py, pz;        // unit-sphere position
    float P = 0;             // people
    float S = 0;             // food store, rations
    int targetCell = -1;     // rough destination (a rumour, re-checked up close)
    bool resting = false;    // stopped to refill the store
    double restStart = 0;
    double t = 0;            // sim day at which the state is valid
    double nextUpdate = 0;
    // What the site they left could still feed, for a whole community that
    // picked up and went (0 for a splinter band, which has left nothing).
    // New ground has to beat it: a place they judged unable to keep them
    // cannot be the place they settle again. Journey state, not saved.
    double setOut = 0; // when this journey began, for fading hope
    int fromCell = -1; // where they started, so arrivals can say how far
    float bows = 0;    // carried: the first possession that travels
    uint16_t culture = 0;
    char name[16] = {};  // the community's name; the suffix comes from purpose
    bool colonists = false; // a splinter, who will name their own new home
    Affinity aff;           // carried, so what a people is good at travels
    // Technology carried (demic diffusion):
    TechState tech[NTECH];
};

struct Field {
    std::vector<float> K;          // carrying capacity per cell (people), 0 on water
    std::vector<int> settlementAt; // settlement index per cell, -1 none
    std::vector<Settlement> settlements;
    std::vector<std::vector<int>> neighbours; // settlements within contact range
    std::vector<Band> bands;
    uint32_t nextBandId = 1;
    uint32_t nextSettlementId = 1;
    // Land memory: a vacated site keeps the condition it was left in and
    // recovers on the usual timescale, so an exhausted valley is a bad place
    // to move to for a generation. Sparse and lazily evaluated -- only sites
    // that have been lived on appear here.
    struct LandScar { float R; double t; uint32_t by; };
    std::unordered_map<int, LandScar> scars;
    // What is left standing where an invested settlement walked away.
    struct Ruin { int cell; double abandoned; char name[16]; };
    std::vector<Ruin> ruins;
    // Per-cell local properties, kept for founding settlements at runtime:
    std::vector<float> kFoodPMap, kWaterMap, sFarmMap, pastureMap, buildMatMap, kGameMap,
        kSmallMap;
    std::vector<Culture> cultures;
    std::vector<Event> events;      // this step's news
    int eventCount[EV_KINDS] = {};  // exact totals, even past what is kept
    // Regional wild game pools, on the climate grid (atmosphere::W x H):
    std::vector<float> gameG;    // health 0..1 per region
    std::vector<float> gameDmax; // sustainable draw per region, people
    double gameT = 0;            // sim day the pools are valid
    size_t peakBands = 0;        // most bands ever in flight at once (diagnostic)
    // Every name this world has ever used. Two settlements sharing a name is
    // realistic and unusable: the news says "Saeti invented farming" and the
    // player opens the wrong Saeti. Names are never released, so a name in
    // the record still means one place a century later.
    std::unordered_set<std::string> takenNames;
};

// The land condition a cell offers now: pristine unless someone has lived
// here, in which case it is what they left, recovered since (same timescale
// as an occupied settlement's regeneration).
inline float cellCondition(const Field& f, int cell, double now) {
    auto it = f.scars.find(cell);
    if (it == f.scars.end()) return 1.0f;
    float rec = 1.0f - (1.0f - it->second.R) *
                           (float)std::exp(-(now - it->second.t) / (R_REGEN_YEARS * 365.0));
    return std::clamp(rec, 0.0f, 1.0f);
}

inline void markScar(Field& f, int cell, float R, double now, uint32_t by = 0) {
    f.scars[cell] = {R, now, by};
}

// Settlements are erased when they move, so anything that outlives one
// decision (a raiding party's home and its mark) refers to them by id.
inline int indexById(const Field& f, uint32_t id) {
    if (!id) return -1;
    for (int i = 0; i < (int)f.settlements.size(); i++)
        if (f.settlements[i].id == id) return i;
    return -1;
}

constexpr float CONTACT_KM = 160.0f; // twice the minimum settlement spacing

// The regional game pool a population cell belongs to (climate-grid index).
inline int gameRegion(int cell) {
    int x = cell % W, y = cell / W;
    return (y * atmosphere::H / H) * atmosphere::W + x * atmosphere::W / W;
}

inline void computeNeighbours(Field& f) {
    auto cellN = [](int cell) {
        hydrology::V3orig d = hydrology::cellDir(cell % W, cell / W);
        return terrain::V3{d.x, d.y, d.z};
    };
    int n = (int)f.settlements.size();
    f.neighbours.assign(n, {});
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++) {
            float d = std::acos(std::clamp(terrain::dot(cellN(f.settlements[i].cell),
                                                        cellN(f.settlements[j].cell)),
                                           -1.0f, 1.0f)) * 6371.0f;
            if (d <= CONTACT_KM) {
                f.neighbours[i].push_back(j);
                f.neighbours[j].push_back(i);
            }
        }
}

// Natural food yield, people per km^2 at land condition R = 1, by cover class:
// bare, tundra, taiga, forest, rainforest, grass, steppe, savanna, shrub, marsh, desert.
constexpr float COVER_YIELD[terrain::NCOV] = {0.0f, 0.08f, 0.4f,  1.2f, 1.0f, 0.8f,
                                              0.3f, 0.6f,  0.2f, 1.0f, 0.02f};
// The share of each cover's yield that is wild game rather than gatherable
// plants: grass and tundra are only edible through the animals that eat
// them; forests feed people directly as well.
constexpr float GAME_SHARE[terrain::NCOV] = {0.0f, 0.9f, 0.6f,  0.4f, 0.25f, 0.7f,
                                             0.85f, 0.7f, 0.5f, 0.4f, 0.5f};

// Of a cover's animal food, the share that is small game. Herd country
// (tundra, steppe, grass) is dominated by the big animals in the regional
// pool; forest, marsh and rainforest hold more of what a bow is for -- so
// a megafauna collapse guts the steppe and leaves the woods a fallback.
constexpr float SMALL_SHARE[terrain::NCOV] = {0.0f, 0.15f, 0.30f, 0.50f, 0.60f, 0.25f,
                                              0.15f, 0.25f, 0.40f, 0.60f, 0.50f};

inline float coverYield(const terrain::Mixture& m) {
    float d = 0;
    for (int i = 0; i < terrain::NCOV; i++) d += m.cov[i] * COVER_YIELD[i];
    return d;
}

// The big-game part of the same yield: what the regional pool holds.
inline float coverGameYield(const terrain::Mixture& m) {
    float d = 0;
    for (int i = 0; i < terrain::NCOV; i++)
        d += m.cov[i] * COVER_YIELD[i] * GAME_SHARE[i] * (1.0f - SMALL_SHARE[i]);
    return d;
}

// The small-game part: local, quick to recover, and hard to catch unarmed.
inline float coverSmallYield(const terrain::Mixture& m) {
    float d = 0;
    for (int i = 0; i < terrain::NCOV; i++)
        d += m.cov[i] * COVER_YIELD[i] * GAME_SHARE[i] * SMALL_SHARE[i];
    return d;
}

// Farming suitability: the grass-like share of the cover (grass, steppe,
// savanna, marsh at half credit -- the real cradles were river floodplains)
// times a warmth window. You can't domesticate what doesn't grow around you.
inline float farmSuitability(const terrain::Mixture& m, float tempC) {
    float grassy = m.cov[5] + m.cov[6] + m.cov[7] + 0.5f * m.cov[9];
    return grassy * std::clamp(tempC / 8.0f, 0.0f, 1.0f);
}

// Grazing suitability: what herds can eat. No warmth gate -- cold-steppe and
// tundra herding (reindeer) are real.
inline float pastureSuitability(const terrain::Mixture& m) {
    return std::min(m.cov[5] + m.cov[6] + m.cov[7] + 0.4f * m.cov[8] + 0.3f * m.cov[1], 1.0f);
}

inline Field build(const terrain::ContinentParams& cp, float seaLevel, const float rot[9],
                   terrain::V3 offset, const plates::Field& pf, const hydrology::Result& hy,
                   const atmosphere::Climatology* clim = nullptr) {
    Field f;
    f.K.assign(W * H, 0.0f);
    f.settlementAt.assign(W * H, -1);
    const std::vector<float>& hm = hy.heightM;

    // Everything the population model needs to know about one cell.
    auto evalCell = [&](int x, int y, float& kFoodP, float& kWater, float& sFarm, float& pasture,
                        float& buildMat, float& kGame, float& kSmall) {
        int i = y * W + x;
        kFoodP = kWater = sFarm = pasture = buildMat = kGame = kSmall = 0;
        float h = hm[i];
        if (h <= 0) return;                                           // land only
        if (hy.cells[i].lakeLevel > hydrology::NO_LAKE + 1 && h < hy.cells[i].lakeLevel) return;

        hydrology::V3orig cd = hydrology::cellDir(x, y);
        terrain::V3 n = {cd.x, cd.y, cd.z};
        terrain::V3 w = terrain::rotate(rot, n) + offset;
        float lat = std::asin(std::clamp(n.z, -1.0f, 1.0f));
        float lon = std::atan2(n.y, n.x);
        atmosphere::DerivedClimate dc =
            clim ? atmosphere::deriveAt(*clim, lat, lon, w, h)
                 : atmosphere::DerivedClimate{terrain::temperatureC(lat, h),
                                              terrain::moistureAt(w, lat),
                                              terrain::temperatureC(lat, h) - 4.0f, 0.0f};
        float temp = dc.temp;
        float moist = dc.moist;
        // Coarse slope from neighbouring cell heights.
        float hx = hm[y * W + hydrology::wrapX(x + 1)] - hm[y * W + hydrology::wrapX(x - 1)];
        float hyv = hm[std::min(y + 1, H - 1) * W + x] - hm[std::max(y - 1, 0) * W + x];
        float cellKm = 2 * 3.14159265f * 6371.0f / W * std::max(std::cos(lat), 0.05f);
        float slope = std::sqrt(hx * hx + hyv * hyv) / (2000.0f * cellKm);
        float uplift = pf.sample({n.x, n.y, n.z}).uplift;
        terrain::Mixture m = terrain::mixtureAt(h, slope, temp, moist, uplift,
                                                hy.cells[i].nearRiver > 0.5f, terrain::patchNoise(w),
                                                dc.swamp, dc.tCold);

        // kFood is sustained yield; the pristine ceiling is higher. Water is
        // a physical daily supply and is not scaled (it rarely binds before
        // farming and irrigation).
        kFoodP = coverYield(m) * FORAGE_KM2 / SUSTAIN_R;
        kGame = coverGameYield(m) * FORAGE_KM2 / SUSTAIN_R;
        kSmall = coverSmallYield(m) * FORAGE_KM2 / SUSTAIN_R;
        // Water within reach: accKm2 is runoff-equivalent drainage area at the
        // reference runoff (hydrology::reweight), fed by the climate's rain.
        float litresPerDay = hy.accKm2[i] * hydrology::REF_RUNOFF_MM_YR * 1.0e6f / 365.0f;
        kWater = litresPerDay * USABLE_WATER / WATER_L_PER_PERSON;
        sFarm = farmSuitability(m, temp);
        pasture = pastureSuitability(m);
        // Building materials within reach: standing timber, else bare rock.
        // Never zero on land -- driftwood and fieldstone exist everywhere,
        // just slowly.
        buildMat = std::clamp(m.cov[2] + m.cov[3] + m.cov[4] + 0.3f * m.cov[8] +
                                  0.6f * m.cov[0],
                              0.15f, 1.0f);
    };

    f.kFoodPMap.assign(W * H, 0.0f);
    f.kWaterMap.assign(W * H, 0.0f);
    f.sFarmMap.assign(W * H, 0.0f);
    f.pastureMap.assign(W * H, 0.0f);
    f.buildMatMap.assign(W * H, 0.0f);
    f.kGameMap.assign(W * H, 0.0f);
    f.kSmallMap.assign(W * H, 0.0f);
#pragma omp parallel for
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int i = y * W + x;
            evalCell(x, y, f.kFoodPMap[i], f.kWaterMap[i], f.sFarmMap[i], f.pastureMap[i],
                     f.buildMatMap[i], f.kGameMap[i], f.kSmallMap[i]);
            f.K[i] = std::min(f.kFoodPMap[i], f.kWaterMap[i]);
        }

    // Regional game pools: each region's sustainable draw is its game yield
    // summed over its area, times the accessible share. Pools start pristine.
    f.gameG.assign(atmosphere::W * atmosphere::H, 1.0f);
    f.gameDmax.assign(atmosphere::W * atmosphere::H, 0.0f);
    for (int y = 0; y < H; y++) {
        float lat = ((y + 0.5f) / H - 0.5f) * 3.14159265f;
        float cellKm2 = (2 * 3.14159265f * 6371.0f / W * std::max(std::cos(lat), 0.01f)) *
                        (3.14159265f * 6371.0f / H);
        for (int x = 0; x < W; x++) {
            int i = y * W + x;
            if (f.kGameMap[i] <= 0) continue;
            float density = f.kGameMap[i] * SUSTAIN_R / FORAGE_KM2; // people/km2 sustained
            f.gameDmax[gameRegion(i)] += density * cellKm2 * GAME_ACCESS;
        }
    }

    // Settlements at local maxima of K, best first, spaced at least ~80 km.
    struct Cand { float k; int cell; };
    std::vector<Cand> cands;
    for (int y = 2; y < H - 2; y++)
        for (int x = 0; x < W; x++) {
            int i = y * W + x;
            if (f.K[i] < MIN_SETTLEMENT_K) continue;
            bool best = true;
            for (int dy = -2; dy <= 2 && best; dy++)
                for (int dx = -2; dx <= 2 && best; dx++)
                    if (f.K[(y + dy) * W + hydrology::wrapX(x + dx)] > f.K[i]) best = false;
            if (best) cands.push_back({f.K[i], i});
        }
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.k > b.k; });
    auto cellN = [](int cell) {
        hydrology::V3orig d = hydrology::cellDir(cell % W, cell / W);
        return terrain::V3{d.x, d.y, d.z};
    };
    for (const Cand& c : cands) {
        if ((int)f.settlements.size() >= MAX_SETTLEMENTS) break;
        terrain::V3 n = cellN(c.cell);
        bool clear = true;
        for (const Settlement& s : f.settlements) {
            terrain::V3 sn = cellN(s.cell);
            float d = std::acos(std::clamp(terrain::dot(n, sn), -1.0f, 1.0f)) * 6371.0f;
            if (d < 80.0f) { clear = false; break; }
        }
        if (!clear) continue;
        f.settlementAt[c.cell] = (int)f.settlements.size();
        Settlement s{c.cell, 0, false, {}, c.k * SUSTAIN_R * 0.5f, 1.0f, 0.0, 0.0};
        s.pop = seedCohorts(s.P);
        s.id = f.nextSettlementId++;
        s.kFoodP = f.kFoodPMap[c.cell];
        s.kGame = f.kGameMap[c.cell];
        s.kSmall = f.kSmallMap[c.cell];
        s.gRegion = gameRegion(c.cell);
        s.kWater = f.kWaterMap[c.cell];
        s.sFarm = f.sFarmMap[c.cell];
        s.pasture = f.pastureMap[c.cell];
        s.buildMat = f.buildMatMap[c.cell];
        s.S = storageCapDays(s.P, s.granaries) * s.P; // the world opens on full stores
        // The world opens with each settlement holding what it needs, capped
        // at half the seeding distance so no two claims start overlapping.
        // The yields above are for the old fixed catchment, so they are
        // rescaled to the claim actually held.
        {
            float perKm2 = f.kFoodPMap[c.cell] / FORAGE_KM2 * SUSTAIN_R;
            float want = perKm2 > 0 ? claimRadiusFor(s.P / perKm2 * CLAIM_MARGIN) : CLAIM_FLOOR_KM;
            float take = std::clamp(want, CLAIM_FLOOR_KM, 40.0f);
            for (int k = 0; k < CLAIM_SECTORS; k++) s.claim[k] = take;
            s.claimKm2 = claimYieldKm2(take);
            float sc = s.claimKm2 / FORAGE_KM2;
            s.kFoodP *= sc;
            s.kGame *= sc;
            s.kSmall *= sc;
            s.kWater *= sc;
        }
        if (clim) atmosphere::seasonProfile(*clim, cellN(c.cell),
                                            std::max(hy.heightM[c.cell], 0.0f), s.tSeason,
                                            s.meanF, s.meanG2);
        f.settlements.push_back(s);
    }
    computeNeighbours(f);
    // Startup listing for testing: where the first settlements are.
    for (int i = 0; i < (int)f.settlements.size() && i < 5; i++) {
        int cx = f.settlements[i].cell % W, cy = f.settlements[i].cell / W;
        float lat = ((cy + 0.5f) / H) * 180.0f - 90.0f, lon = ((cx + 0.5f) / W) * 360.0f - 180.0f;
        fprintf(stderr, "settlement %d: lat %.2f lon %.2f K %.0f\n", i, lat, lon, f.K[f.settlements[i].cell]);
    }
    return f;
}

// Growth runs when the land's flow covers everyone; decline is famine:
// deaths need both low stores (hoarding excludes the bottom of the group) and
// an inadequate harvest flow. Calibrated offline to preserve the ~18%
// overshoot at year ~33 and settling at the sustained capacity. `flow` is
// the seasonal food flow already assembled by the caller; `capDays` grows
// with built granaries (storageCapDays).
inline void derivatives(float P, float R, float S, float flow, float K, float capDays,
                        float gather, float& dP, float& dR, float& dS, float& dStarve) {
    float H = std::min(flow, gather);                     // limited by land and time
    float cap = capDays * std::max(P, 1.0f);
    float fill = std::clamp(S / cap, 0.0f, 1.0f);
    float excl = std::clamp(1.0f - fill / HOARD_FILL, 0.0f, 1.0f);
    float shortfall = P > 0 ? std::clamp(1.0f - H / P, 0.0f, 1.0f) : 0.0f;
    float phi = P > 1 ? flow / P : 2.0f;
    float g = phi >= 1 ? GROWTH_MAX / 365.0f * std::min((phi - 1) / 0.11f, 1.0f) : 0.0f;
    dStarve = STARVE_MAX * P * excl * shortfall; // deaths/day, the felt part
    dP = P * g - dStarve;
    (void)g;
    dR = (1 - R) / (R_REGEN_YEARS * 365) - (P / std::max(K, 1.0f)) * R / (R_DEPLETE_YEARS * 365);
    dS = H - P;
}

// The seasonal food flow at time t: foraging follows the forage factor,
// farming follows squared growing activity normalized to keep its annual
// total (a prominent harvest season; year-round cropping in the tropics),
// and water caps the whole.
struct SeasonCtx {
    const atmosphere::Climatology* clim = nullptr;
    terrain::V3 n{};
    float h = 0;
    float farmMult = 1; // 1 + gain*s*expertise, from technology
    float husbExp = 0;  // husbandry expertise
    float granExp = 0;  // granary expertise, 0 unless practising (build pace)
    Affinity aff;       // what these people are good at, from doing it
    float gameG = 1;    // regional game pool health (Settlement::gameNow)
    float bowCover = 0;  // share of hunters carrying a bow
    float archExp = 0;   // archery expertise
};

// Hunting efficiency against a depleted pool: scarcer game is hunted
// harder, so the take falls only as sqrt(health) -- which is also why a
// pool can be pushed past its extinction floor instead of being left alone.
inline float huntEff(float gameG) { return std::sqrt(std::max(gameG, 0.0f)); }

// Knowledge x means: what share of a hunter's bows this group actually has.
inline float bowCoverage(float bows, float P) {
    return std::clamp(bows / std::max(P * BOW_PER_HUNTER, 1.0f), 0.0f, 1.0f);
}

// Small game taken, as a share of what is there: snares get a quarter of it,
// bows in skilled hands get all of it.
inline float smallGameEff(float coverage, float archExp) {
    return SMALL_SNARE_FLOOR + (1.0f - SMALL_SNARE_FLOOR) * coverage * archExp;
}


// One day of demography, in place. Births come from the women and are
// multiplied by food; children take fifteen years to become adults and
// many die first; adults age into a short old age. Famine deaths are
// handed out by vulnerability -- the young and the old go first -- which
// is why a hungry settlement loses its next generation before its
// workers. No share is enforced anywhere: the structure is what the flows
// leave behind.
inline void stepCohorts(Cohorts& c, float phi, float starveDeaths, float dt) {
    float yr = dt / 365.0f;
    float surplus = phi >= 1 ? std::min((phi - 1.0f) / 0.11f, 1.0f) : 0.0f;
    float births = BIRTHS_REPLACE * (1.0f + FERT_SURPLUS * surplus) * c.W * yr;
    float grow = c.C / CHILD_YEARS * yr;      // reaching adulthood
    float dieC = MORT_CHILD * c.C * yr;
    float ageM = c.M / ADULT_YEARS * yr, ageW = c.W / ADULT_YEARS * yr;
    float dieM = MORT_ADULT * c.M * yr, dieW = MORT_ADULT * c.W * yr;
    float dieE = MORT_ELDER * c.E * yr;
    // Famine, weighted by who survives it worst.
    float wsum = FAMINE_W_CHILD * c.C + FAMINE_W_ADULT * (c.M + c.W) + FAMINE_W_ELDER * c.E;
    float f = wsum > 1e-6f ? starveDeaths * dt / wsum : 0.0f;
    c.C = std::max(c.C + births - grow - dieC - f * FAMINE_W_CHILD * c.C, 0.0f);
    c.M = std::max(c.M + BOY_SHARE * grow - ageM - dieM - f * FAMINE_W_ADULT * c.M, 0.0f);
    c.W = std::max(c.W + (1.0f - BOY_SHARE) * grow - ageW - dieW - f * FAMINE_W_ADULT * c.W,
                   0.0f);
    c.E = std::max(c.E + ageM + ageW - dieE - f * FAMINE_W_ELDER * c.E, 0.0f);
}

// A band on the road: no births, but famine still takes the weak first.
inline void bandStarve(Cohorts& c, float deaths) {
    float wsum = FAMINE_W_CHILD * c.C + FAMINE_W_ADULT * (c.M + c.W) + FAMINE_W_ELDER * c.E;
    if (wsum <= 1e-6f) return;
    float f = deaths / wsum;
    c.C = std::max(c.C - f * FAMINE_W_CHILD * c.C, 0.0f);
    c.M = std::max(c.M - f * FAMINE_W_ADULT * c.M, 0.0f);
    c.W = std::max(c.W - f * FAMINE_W_ADULT * c.W, 0.0f);
    c.E = std::max(c.E - f * FAMINE_W_ELDER * c.E, 0.0f);
}

// The season-interpolated site temperature from the cached profile.
inline float cachedSeasonT(const Settlement& s, double t) {
    double sf = std::fmod(t, 365.0) / 365.0 * 4.0 - 0.5;
    int s0 = ((int)std::floor(sf) % 4 + 4) % 4, s1 = (s0 + 1) % 4;
    float f = (float)(sf - std::floor(sf));
    return s.tSeason[s0] * (1 - f) + s.tSeason[s1] * f;
}

inline float foodFlow(const Settlement& s, const SeasonCtx& ctx, float R, double t) {
    // Three kinds of food from the land: plants, the herds of the regional
    // pool, and the small game a bow is for.
    float bigEff = huntEff(ctx.gameG) * (1.0f + BOW_BIG_GAIN * ctx.bowCover * ctx.archExp);
    float hunted = (s.kGame * bigEff + s.kSmall * smallGameEff(ctx.bowCover, ctx.archExp)) *
                   affinityBonus(ctx.aff.hunt);
    float forage = (s.kFoodP - s.kGame - s.kSmall) * affinityBonus(ctx.aff.gather) + hunted;
    float farm = s.kFoodP * (ctx.farmMult - 1.0f);
    float fF = s.meanF, fG2 = 1.0f;
    if (ctx.clim) {
        float tC = cachedSeasonT(s, t);
        fF = atmosphere::forageFactor(tC);
        float g = atmosphere::growthActivity(tC);
        fG2 = g * g / std::max(s.meanG2, 0.05f);
    }
    // Husbandry: the herd is a walking store -- its flow barely dips in
    // winter (fodder and slaughter). Plus the pasture-free farmyard animals.
    float gNow = std::clamp((fF - 0.12f) / 0.88f, 0.0f, 1.0f);
    float husb = (s.herd * (0.7f + 0.3f * gNow) +
                  FARMYARD_SHARE_POP * s.kFoodP * ctx.husbExp) *
                 R * affinityBonus(ctx.aff.herd);
    return std::min((forage * fF + farm * fG2 * affinityBonus(ctx.aff.farm)) * R + husb,
                    s.kWater);
}

// Integrate a settlement from its valid time to `now` and schedule the next
// re-evaluation at the moment its state will have drifted about 5%. Famine
// can move at percent-per-day, so steps stay short and the horizon also
// watches for the store crossing the hoarding threshold.
inline bool advance(Settlement& s, float K, const SeasonCtx& ctx, double now) {
    if (K <= 0) { s.t = now; s.nextUpdate = now + 3650; return false; }
    float herdCap = s.pasture * s.claimKm2 * HERD_PASTURE_K / SUSTAIN_R *
                    (0.3f + 0.7f * ctx.husbExp);
    Cohorts pop = s.pop;
    float P = pop.total(), R = s.R, S = s.S;
    float granaries = s.granaries, buildWork = s.buildWork;
    float fillLo = s.fillLo, fillHi = s.fillHi, granNeed = s.granNeedYrs;
    float starved = s.starvedYr;
    float bows = s.bows;
    double cycleT = s.cycleT;
    float lat = std::asin(std::clamp(ctx.n.z, -1.0f, 1.0f));
    float lon = std::atan2(ctx.n.y, ctx.n.x);
    double span = now - s.t;
    int steps = std::clamp((int)(span / 5.0) + 1, 1, 800);
    float hstep = (float)(span / steps);
    for (int k = 0; k < steps && hstep > 0; k++) {
        double tk = s.t + (k + 0.5) * hstep;
        float capDays = storageCapDays(P, granaries);
        float flow = foodFlow(s, ctx, R, tk);
        // The work day (daylight.h): daylight up to the waking cap, plus a
        // firelight extension bought by hunger. The gather budget follows
        // the hours; the 1.5/day constant is the 12-hour baseline.
        float phiNow = P > 1 ? flow / P : 2.0f;
        float wh = daylight::workHours(lat, tk, needRamp(phiNow));
        float dP, dR, dS;
        float dStarve;
        derivatives(P, R, S, flow, K, capDays, GATHER_SETTLED * P * wh / 12.0f, dP, dR, dS,
                    dStarve);
        starved += dStarve * hstep;
        starved *= std::max(1.0f - hstep / 365.0f, 0.0f); // trailing year
        // Sub-day steps see the rhythm: harvesting and eating happen inside
        // the day's activity window, so stores hold flat through the night.
        double a = s.t + k * (double)hstep;
        float act = hstep >= 1.0f
                        ? 1.0f
                        : (float)(daylight::activeDays(lon, a, a + hstep, wh) / hstep);
        stepCohorts(pop, phiNow, dStarve, hstep);
        P = pop.total();
        R = std::clamp(R + dR * hstep, 0.0f, 1.0f);
        float cap = capDays * std::max(P, 1.0f);
        S = std::clamp(S + dS * hstep * act, 0.0f, cap);
        // The annual fill cycle: track the store-fill extremes and judge
        // granary demand once a year (see the constants above). The signal
        // also feeds need-driven invention: consecutive binding years make
        // an unaware settlement desperate enough to invent (technology.h).
        float fill = S / cap;
        fillLo = std::min(fillLo, fill);
        fillHi = std::max(fillHi, fill);
        if (tk - cycleT >= 365.0) {
            bool binds = fillHi > GRANARY_HI && fillLo < GRANARY_LO;
            granNeed = binds ? granNeed + 1.0f : 0.0f;
            if (binds && buildWork <= 0 && ctx.granExp > 0) buildWork = GRANARY_WORK;
            cycleT = tk;
            fillLo = fillHi = fill;
        }
        // Granary building (Design/Technology.md): only the fed divert
        // labour, expertise sets the pace, materials set the gathering; the
        // work total itself never changes.
        if (buildWork > 0 && ctx.granExp > 0 && fill > HOARD_FILL) {
            buildWork -= P * GRANARY_LABOUR_SHARE * ctx.granExp * s.buildMat * hstep;
            if (buildWork <= 0) {
                granaries += 1;
                buildWork = 0;
                if (s.builtGranaries < 250) s.builtGranaries++;
            }
        }
        // Bows: one bowyer finishes one bow in BOW_WORK_DAYS however large
        // the settlement, so a crowd only carves more of them at once. They
        // are made up to one per hunter and no further, and they wear out.
        if (ctx.archExp > 0) {
            float want = P * BOW_PER_HUNTER;
            float rate = 0;
            if (bows < want)
                rate = P * BOW_LABOUR_SHARE * s.buildMat /
                       (BOW_WORK_DAYS / std::max(ctx.archExp, 0.2f));
            bows = std::max(bows + (rate - bows / BOW_LIFE_DAYS) * hstep, 0.0f);
        }
        if (s.herd > 0 && herdCap > 0)
            s.herd = std::clamp(s.herd + HERD_GROWTH_YR / 365.0f * s.herd *
                                             (1.0f - s.herd / herdCap) * hstep,
                                0.0f, herdCap * 1.05f);
        else if (herdCap <= 0)
            s.herd = 0;
    }
    bool changed = std::fabs(P - s.P) > 0.5f || std::fabs(R - s.R) > 0.002f ||
                   granaries != s.granaries;
    // What they have been living on pulls their affinities that way, over
    // generations. Fighting is not fed from food; it comes from raiding
    // and being raided, and fades in peace (sim.h).
    {
        float plant = (s.kFoodP - s.kGame - s.kSmall) * s.meanF;
        float game = (s.kGame + s.kSmall) * s.meanF;
        float crop = s.kFoodP * (ctx.farmMult - 1.0f);
        float stock = s.herd * 0.85f + FARMYARD_SHARE_POP * s.kFoodP * ctx.husbExp;
        float tot = std::max(plant + game + crop + stock, 1e-3f);
        float k = std::min((float)(span / (AFFINITY_TAU_YEARS * 365.0)), 1.0f);
        s.aff.gather += (plant / tot - s.aff.gather) * k;
        s.aff.hunt += (game / tot - s.aff.hunt) * k;
        s.aff.farm += (crop / tot - s.aff.farm) * k;
        s.aff.herd += (stock / tot - s.aff.herd) * k;
        s.aff.fight *= std::exp(-(float)(span / (FIGHT_FORGET_YEARS * 365.0)));
    }
    s.pop = pop;
    s.P = P;
    s.R = R;
    s.S = S;
    s.granaries = granaries;
    s.buildWork = buildWork;
    s.fillLo = fillLo;
    s.fillHi = fillHi;
    s.granNeedYrs = granNeed;
    s.starvedYr = starved;
    s.bows = bows;
    s.cycleT = cycleT;
    s.t = now;
    float capDays = storageCapDays(s.P, s.granaries);
    // Horizon from the ANNUAL-MEAN flow: the seasonal oscillation is
    // recurring, so a settlement in seasonal equilibrium still sleeps long.
    float bigEffMean = huntEff(ctx.gameG) * (1.0f + BOW_BIG_GAIN * ctx.bowCover * ctx.archExp);
    float forageBase =
        (s.kFoodP - s.kGame - s.kSmall) * affinityBonus(ctx.aff.gather) +
        (s.kGame * bigEffMean + s.kSmall * smallGameEff(ctx.bowCover, ctx.archExp)) *
            affinityBonus(ctx.aff.hunt);
    float meanFlow = std::min(
        (forageBase * s.meanF + s.kFoodP * (ctx.farmMult - 1.0f)) * s.R, s.kWater);
    float dP, dR, dS, dStarveMean;
    derivatives(s.P, s.R, s.S, meanFlow, K, capDays, GATHER_SETTLED * s.P, dP, dR, dS,
                dStarveMean);
    double horizon = 1800;
    if (std::fabs(dP) > 1e-9) horizon = std::min(horizon, 0.05 * std::max(s.P, 50.0f) / std::fabs(dP));
    if (std::fabs(dR) > 1e-9) horizon = std::min(horizon, 0.05 * std::max(s.R, 0.1f) / std::fabs(dR));
    if (dS < -1e-9) {
        double toHoard = (s.S - HOARD_FILL * capDays * s.P) / -dS;
        if (toHoard > 0) horizon = std::min(horizon, std::max(toHoard, 15.0));
    }
    s.nextUpdate = now + std::max(horizon, 5.0);
    return changed;
}

} // namespace population
