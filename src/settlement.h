// The settlement as data: what a settlement, a band and the field they live
// in are made of, with the constants that price them. Every rule of
// Design/Population.md, Design/Migration.md, Design/Technology.md and
// Design/Conflict.md that is a number lives here; the arithmetic that moves
// the numbers lives in population.h (integration) and the sim headers
// (decisions). A member added to Settlement is set in newSettlement below,
// the one place both founding sites start from.
#pragma once
#include "terrain.h"
#include "hydrology.h"
#include "atmosphere.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace population {

constexpr int W = hydrology::W, H = hydrology::H;

// Rules (Design/Population.md).
constexpr float FORAGE_KM2 = 314.0f; // 10 km radius disc

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
constexpr float WATER_L_PER_PERSON = 20.0f; // per day
constexpr float USABLE_WATER = 0.05f;       // fraction of discharge usable
constexpr float GROWTH_MAX = 0.028f;        // per year at full surplus
// Demography (Design/Population.md). People are counted in four stocks --
// children, adult men, adult women, elderly -- and every share is emergent:
// nothing enforces a ratio. Births come from women, children take fifteen
// years to become adults and many do not, adults age out over a working
// lifetime, and the old die quickly, which is what keeps them few.
constexpr float CHILD_YEARS = 15.0f;  // to adulthood
constexpr float ADULT_YEARS = 45.0f;  // adulthood to old age
constexpr float MORT_CHILD = 0.0359f; // /yr: about a third never grow up
constexpr float MORT_ADULT = 0.010f;  // /yr, outside famine
constexpr float MORT_ELDER = 0.25f;   // /yr: a few years past sixty
constexpr float BOY_SHARE = 0.512f;   // slightly more boys are born
// Replacement fertility for the stage durations above, per woman per year;
// food multiplies it, and the multiplier is what growth actually is now.
constexpr float BIRTHS_REPLACE = 0.1015f;
constexpr float FERT_SURPLUS = 1.35f; // extra births at full surplus
// Famine takes the weak first.
constexpr float FAMINE_W_CHILD = 1.5f;
constexpr float FAMINE_W_ADULT = 1.0f;
constexpr float FAMINE_W_ELDER = 2.0f;
// Fighting (Design/Conflict.md): men do most of it, everyone else does some.
constexpr float FIGHT_W_MAN = 1.0f;
constexpr float FIGHT_W_WOMAN = 0.15f;
constexpr float FIGHT_W_OTHER = 0.05f;
constexpr float RAID_MEN_SHARE = 0.55f; // of the men; the rest stay home
constexpr float DECLINE_MAX = 0.07f;    // per year in famine
// The land timescales are slow relative to growth so populations overshoot
// visibly (~18% peak around year 33 from a half-capacity start) before the
// land's decline pulls them back. Their ratio (1.5) fixes R* = 0.549.
constexpr float R_REGEN_YEARS = 33.0f;   // land recovery
constexpr float R_DEPLETE_YEARS = 22.0f; // land depletion at P = K
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
constexpr float GATHER_SETTLED = 1.5f; // rations/person/day gatherable settled
constexpr float GATHER_MOVING = 0.5f;  // a moving band forages on a third of its time
constexpr float CAP_DAYS_SETTLED = 90.0f;
constexpr float CAP_DAYS_BAND = 10.0f;
// Water is carried too, and on a different clock. Food runs on weeks and
// bleeds a band; water runs on days and kills it. That difference is the
// whole reason a desert or an open sea is a barrier while poor country is
// merely expensive -- no amount of hunger stops anybody in three days.
//
// Three days is what people carry in skins and gourds. Pots would carry
// more, which is a job waiting for a technology that does not exist yet.
constexpr float CAP_WATER_DAYS = 3.0f;
// Dehydration kills in three or four days, so a dry day has to cost a great
// deal more than a hungry one. At a tenth a day, a band could still cross
// 200 km of open sea by dying down to a remnant on the way.
constexpr float THIRST_DEATH_RATE = 0.30f;

// How much a person drinks is not a constant. At rest in cool country it is
// two or three litres a day; walking in tropical heat it is four to six, and
// in desert heat more than ten. Without this the skins last three days in
// the Sahara and three days in Siberia, which quietly makes hot country the
// easier crossing.
inline float thirstFactor(float tempC) {
    return std::clamp(1.0f + (tempC - 12.0f) / 22.0f, 1.0f, 2.2f);
}
constexpr float HOARD_FILL = 0.25f; // famine sets in below this fill fraction
constexpr float STARVE_MAX = 0.02f; // /day at full exclusion and total shortfall

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
constexpr float GAME_ACCESS = 0.5f; // share of a region's game within reach
// Small game (Design/Technology.md): birds, hares, the rest of the animals
// too quick and too fecund to hunt out. It needs no pool of its own -- it
// lives on the land condition R, the local resource that depletes with use
// and recovers in a generation, which is exactly what small game is. What
// it needs instead is a bow: snares and thrown sticks take some of it, a
// bow takes most of it. Bows help against big game too, but only a little.
constexpr float SMALL_SNARE_FLOOR = 0.25f; // taken without a bow
constexpr float BOW_BIG_GAIN = 0.25f;      // bows vs the herd animals
constexpr float BOW_PER_HUNTER = 0.2f;     // one bow per hunter; a fifth hunt
// A bow is craft work, not construction: one person per bow, so a crowd
// makes more bows at once but never a single bow faster.
constexpr float BOW_LABOUR_SHARE = 0.05f; // people who can be spared to carve
constexpr float BOW_WORK_DAYS = 90.0f;    // one bowyer, at full skill
constexpr float BOW_LIFE_DAYS = 3650.0f;  // bows wear out and are replaced
constexpr double GAME_TICK_DAYS = 90.0;   // pool update cadence (a slow layer)
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
constexpr float AWARE_GROWTH_KM = 300.0f;     // settlements, toward base+this
constexpr double AWARE_TAU_DAYS = 30.0 * 365; // settlement growth timescale
constexpr float AWARE_REST_KM = 100.0f;       // resting bands, toward base+this
constexpr double AWARE_REST_TAU_DAYS = 45.0;
constexpr float AWARE_CAP_KM = 600.0f;
constexpr double BAND_STEP_DAYS = 5.0;

inline float vantageKm(float promM) { return 3.57f * std::sqrt(std::max(promM, 0.0f)); }

inline float settlementAwareKm(double ageDays, float promM) {
    float r = AWARE_BASE_KM + AWARE_GROWTH_KM * (1.0f - (float)std::exp(-ageDays / AWARE_TAU_DAYS));
    return std::min(r + vantageKm(promM), AWARE_CAP_KM);
}

inline float bandAwareKm(double restDays, float promM) {
    float r =
        AWARE_BASE_KM + AWARE_REST_KM * (1.0f - (float)std::exp(-restDays / AWARE_REST_TAU_DAYS));
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
constexpr float HOPE_MARGIN = 0.25f;    // must beat the goal by this at first
constexpr float HOPE_FLOOR = 0.5f;      // what they will eventually accept
constexpr double HOPE_TAU_DAYS = 730.0; // how fast hope fades

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
constexpr float LOOT_CARRY_DAYS = 30.0f;    // rations one raider hauls home
constexpr float RAID_WORTH_IT = 1500.0f;    // a haul worth walking days for
constexpr float LOOT_STORE_SHARE = 0.6f;    // of what is found; the rest is hidden
constexpr float LOOT_HERD_SHARE = 0.35f;    // livestock needs no carrying
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

// Farming's reach is the day's walk, derived, not defined (von Thuenen by
// way of the labour ledger): a field at distance d costs its round trip out
// of the working day, every day it is worked, so its value falls linearly
// to zero where the walk would eat the whole day -- at 4.8 km/h and a
// 12-hour day, 28.8 km. Foraging keeps the gentler claim taper (a forager
// ranges and camps; a farmer commutes to the same field daily).
constexpr float FARM_WALK_KMH = 4.8f;
constexpr float FARM_DAY_H = 12.0f;
inline float farmCommute(float km) {
    return std::max(1.0f - 2.0f * km / (FARM_WALK_KMH * FARM_DAY_H), 0.0f);
}

// Farmsteads: past the walk, the answer is to move the household to the
// field. A farmstead is a building of the mother settlement -- drawn as a
// lone house among its fields -- standing for the hamlet-scale cluster
// that works a block of the claim too far to commute to. Its people stay
// the settlement's people: no new agent, no new event. It is built like a
// granary (measured demand, fixed work, only the fed build) and its worked
// land is priced at ITS OWN cell's suitability, so a river village whose
// claim runs into hills gets farmsteads only where the grass is.
constexpr float FSTEAD_KM2 = 12.0f;   // worked claim-km2 one farmstead re-enables
constexpr float FSTEAD_WORK = 500.0f; // man-days: houses, byres, clearing
constexpr float FSTEAD_LABOUR_SHARE = 0.02f;
constexpr int FSTEAD_MAX = 20;               // slots on the spiral; claims cap sooner
constexpr float RELOC_ANCHOR_FSTEAD = 0.15f; // sunk investment, like granaries
// Where slot k stands: a golden-angle spiral walking outward from the
// village at field scale, mirrored exactly in shaders/globe.frag. The last
// slot stands at ~23 km -- just inside where the commute value hits zero.
constexpr float FSTEAD_R0_KM = 2.5f, FSTEAD_DR_KM = 1.1f;

// Tilled land is a built thing (decided 2026-09-14): a settlement selects a
// plot, clears and breaks it as a work order with a start and a finish, and
// the farm yield comes from the plots that stand -- so what the map draws
// and what the people eat are one quantity. The commute curve above decides
// WHERE plots can be: daily fields end where the walk costs ~15% of the day
// (4.3 km -- where the ethnography puts the village-field edge), and land
// past that is opened by farmsteads, each working its own block.
constexpr float FIELD_WORTH = 0.85f; // the commute value where daily fields end
constexpr float VILLAGE_FIELDS_KM2 =
    58.0f; // pi * 4.32^2: the daily-walk disc, from farmCommute >= FIELD_WORTH
// What a tilled km2 feeds at full suitability and expertise: 4 ha a head --
// early-cereal figures with the long fallow priced in. At s*e = 0.5 a
// square kilometre feeds 12, which is the margin where farming is barely
// worth the clearing.
constexpr float TILLED_YIELD_PKM2 = 25.0f;
// Nobody clears land they cannot work: the whole rotation mosaic a person
// tends is about 8 ha, so tilled land is capped by hands, not only ground.
constexpr float FARM_KM2_PER_PERSON = 0.08f;
// One work order: half a square kilometre of new ground -- not one field
// but a season's clearing of many, since a stone-age plot is garden-small,
// a hectare or two, and a holding is a scatter of them. Girdle, burn,
// stump and break at ~20 man-days a hectare, 2,000 to the km2: a village
// crew of six finishes an order in most of a year, and a farm is a
// generation's work.
constexpr float PLOT_KM2 = 0.5f;
constexpr float TILL_WORK_PER_KM2 = 2000.0f;
constexpr float TILL_LABOUR_SHARE = 0.02f;

// Heat (Design/Resources.md): the second need, the first that is not
// calories. The demand is warmth -- cooking fires always, hearths against
// the cold -- and burning wood is only the leading MODE of meeting it:
// herd dung is fuel too, which is how the treeless steppe stayed warm.
// Demand is in kilograms of wood-equivalent per person per day: about a
// kilogram to cook anywhere, plus heating that grows with the cold --
// roughly a tonne a year in the temperate belt, two or three in the
// subarctic, which is what the ethnographic record measures.
constexpr float FUEL_COOK_KG = 1.0f;      // kg/person/day, any climate
constexpr float FUEL_HEAT_BASE_C = 15.0f; // below this the hearth burns for warmth
constexpr float FUEL_HEAT_KG_DEG = 0.12f; // kg/person/day per degree below base
// The woodpile: an ambient stockpile like food's 90 days -- it must fill
// in the mild seasons and drain in winter, but a pile needs no walls and
// no build (Design/Resources.md: stockpiles without architecture).
constexpr float FUEL_CAP_KG = 500.0f;   // pile per person: about a hard winter
constexpr float FUEL_PILE_DAYS = 90.0f; // filled over a season, labour allowing
// The labour ledger's wood column. A man-day of dedicated cutting in full
// woods brings home 50 kg; sparser cover means longer walks for less. The
// byproduct is what ordinary rounds sweep up regardless -- deadfall on the
// way home -- which covers the cooking fire wherever there are woods at
// all, so dedicated woodcutters only appear where need outruns it.
constexpr float WOOD_GATHER_KG = 50.0f;   // per man-day, at full wood cover
constexpr float WOOD_BYPRODUCT_KG = 1.5f; // per person-day of ordinary rounds
constexpr float DUNG_KG_PER_FED = 4.0f;   // fuel per people-fed unit of herd
// A cold hearth kills the way famine does -- the weak first -- but people
// huddle, ration and endure long before they die, so the rate only bites
// as the shortfall becomes total (quadratic in the unmet share).
constexpr float COLD_MAX = 0.02f; // deaths/day at a totally unmet need

inline float fuelNeedKg(float tempC) {
    return FUEL_COOK_KG + FUEL_HEAT_KG_DEG * std::max(FUEL_HEAT_BASE_C - tempC, 0.0f);
}
// Measurement switch (test_resources.cpp): the same world run with the
// hearth cold, so the heat need's effect can be measured against a
// baseline. Not a game setting.
inline bool HEAT_ENABLED = true;

// Cultures and names (Design/Culture.md). A culture owns a small sound
// inventory, and every settlement descended from it draws its name from
// that inventory -- so Gervatti and Poetti share an ending without anyone
// coordinating it. Overlap between distant cultures is expected and
// harmless at this scale.
constexpr const char* NAME_ONSET[] = {
    "b",  "d",  "g",  "k",  "m",  "n",  "p",  "r", "s", "t", "v", "z", "br", "dr", "gr", "kr",
    "pr", "tr", "st", "sk", "th", "sh", "ch", "l", "f", "h", "j", "w", "kh", "ts", "vr", "gv"};
constexpr const char* NAME_NUCLEUS[] = {"a", "e", "i", "o", "u", "ai", "ei", "ou", "ia", "ae"};
constexpr const char* NAME_CODA[] = {"", "", "", "n", "r", "s", "l", "m", "k", "t"};
constexpr const char* NAME_ENDING[] = {"i",  "a",  "o",  "ti", "tti", "ni", "na", "os",
                                       "us", "ar", "en", "ia", "eth", "or", "an", "il"};
constexpr int N_ONSET = 32, N_NUCLEUS = 10, N_CODA = 10, N_ENDING = 16;

struct Culture {
    char name[16] = {}; // what these people are called, as a people
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
constexpr float AFFINITY_GAIN = 0.15f;       // at full devotion
constexpr float AFFINITY_TAU_YEARS = 100.0f; // a few generations to settle
constexpr float FIGHT_LEARN = 0.06f;         // per raid, given or received
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
    EV_RAID_HIT,  // someone was robbed
    EV_RAID_HELD, // an attack was beaten off
    EV_RAID_HOME, // raiders came home
    EV_INVENTED,  // a technology, first anywhere
    EV_ADOPTED,   // a settlement took one up
    EV_GRANARY,   // a granary finished
    EV_GAME_GONE, // a regional herd hunted to nothing
    EV_TECH_LOST, // nobody here can do it any more
    EV_FARMSTEAD, // a farmstead raised on the far fields
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

// ---------------------------------------------------- losing a technology
//
// Skill is not a stock that keeps. It lives in the people doing the thing,
// and it goes two ways: nobody does it any more, or there are too few doing
// it to teach it faithfully. Expertise is a function of how long ago
// practice began, so both are expressed by pushing that day forward -- at
// one day per day the skill stands still, faster than that it slides back.
//
// Disuse runs at the rate it was learned: you lose it as fast as you got
// it. Isolation runs far faster, because that is the catastrophic case --
// the Polar Inuit had lost the kayak, the leister and the bow within a
// generation of the epidemic that took their elders.
// Expertise is a function of how long ago practice began, so a day of it is
// lost by pushing that day forward. At k = 2 the skill stands still; above
// that it slides back, at (k - 2) days of skill per day.
constexpr double SKILL_DISUSE_K = 3.0;   // idle: lost as fast as it was learned
constexpr double SKILL_ISOLATED_K = 9.0; // nobody left to teach: seven times faster
constexpr float SKILL_USE_SLOWS = 0.25f; // using it daily blunts even that
// A skill cannot fall below a beginner's hands, and a beginner is still a
// practitioner. Practice lapses only after a people have been down there for
// this long -- which is also what lets a new one survive its first day, when
// it has no skill to lose and the means may not exist yet: a village that has
// just taken up granaries has a generation to lay the first stone.
constexpr double SKILL_GRACE_YEARS = 40.0;
// Once practice has lapsed, knowledge follows it: three generations after
// the last person anywhere in reach did the thing, a people no longer knows
// it can be done. A story about ancestors growing grain does not grow grain.
constexpr double AWARE_FORGET_YEARS = 75.0;
// Reinventing what your grandmother did is not the problem your grandmother
// had: the terraces are still there, and someone watched.
constexpr float REDISCOVER_GAIN = 2.0f;
constexpr double REDISCOVER_TAU_YEARS = 300.0;

// The technology table (Design/Technology.md): per-settlement state for each
// technology. Farming's original fields generalized when husbandry arrived.
enum : int {
    TECH_FARMING = 0,
    TECH_HUSBANDRY = 1,
    TECH_GRANARY = 2,
    TECH_ARCHERY = 3, // known everywhere from the start; the bows are the scarce part
    TECH_FISHING = 4, // weirs, traps and nets: the bank is free, the gear is not
    NTECH = 5
};
// The practitioners who can reach each other, below which a complex skill
// stops being copied faithfully -- the settlement's own people plus those
// of every neighbour who also practises it. The unit is the connected group,
// not the village: Tasmania was an island of thousands, and the Polar Inuit
// who lost the kayak were about two hundred.
inline float criticalPractitioners(int tech) {
    switch (tech) {
    case TECH_FARMING:
    case TECH_HUSBANDRY:
        return 200.0f; // a whole way of living
    case TECH_GRANARY:
    case TECH_FISHING:
        return 140.0f; // a craft
    default:
        return 0.0f; // archery is never lost
    }
}
struct TechState {
    bool aware = false;
    bool practising = false; // implies aware
    double practiceT = 0;    // sim day practice began (expertise grows from here)
    double lostT = -1;       // sim day practice lapsed, or the last day anyone near did it
    double strainT = -1;     // sim day this skill fell to a beginner's and stayed there
};

// Who a group is made of. P is the sum, kept in step so everything that
// only cares about headcount keeps working.
struct Cohorts {
    float C = 0, M = 0, W = 0, E = 0; // children, men, women, elderly
    float total() const { return C + M + W + E; }
    void scale(float f) {
        C *= f;
        M *= f;
        W *= f;
        E *= f;
    }
    void add(const Cohorts& o) {
        C += o.C;
        M += o.M;
        W += o.W;
        E += o.E;
    }
    void sub(const Cohorts& o) {
        C -= o.C;
        M -= o.M;
        W -= o.W;
        E -= o.E;
    }
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

// Built by newSettlement below, never by positional initialisers: the
// members are in the order they were added and can be inserted anywhere.
struct Settlement {
    int cell = -1;
    uint32_t id = 0;         // stable identity (settlements are erased when they move)
    bool leaving = false;    // converted to a band this step; swept at step end
    Cohorts pop;             // who they are; P below is its total
    float P = 0;             // people
    float R = 1;             // land condition 0..1
    double t = 0;            // sim day at which P and R are valid
    double nextUpdate = 0;   // sim day of the next scheduled re-evaluation
    float S = 0;             // food store, rations (person-days)
    double scarceSince = -1; // sim day scarcity began, -1 if fed (split rule;
                             // resets on every split attempt)
    bool noProspect = false; // last emigration attempt found nowhere to go
                             // (transient; shown in the panel, not saved)
    double hungrySince = -1; // sim day sustained hunger began, -1 if fed --
                             // never reset by splitting (need-driven invention)
    double founded = 0;      // sim day the settlement was founded (awareness age)
    // Fixed local properties (from the terrain at the cell):
    float kFoodP = 0;                    // pristine food capacity (already / SUSTAIN_R)
    float kGame = 0;                     // the big-game part of kFoodP (regional pool)
    float kSmall = 0;                    // the small-game part (local, needs bows)
    float bows = 0;                      // made bows on hand; they wear out and are replaced
    int gRegion = 0;                     // which regional game pool this settlement hunts
    float gameNow = 1;                   // that pool's health, refreshed by sim::gameTick
    float meanF = 1;                     // annual mean forage factor (seasonal climate)
    float meanG2 = 1;                    // annual mean squared growing activity (farming shape)
    float tSeason[4] = {15, 15, 15, 15}; // season temps at the site (cached)
    float kWater = 0;                    // water-supply capacity
    float sFarm = 0;                     // farming suitability 0..1 (grass-like cover, warm enough)
    float pasture = 0;             // grazing suitability 0..1 (grass, steppe, savanna, some tundra)
    float herd = 0;                // livestock, in people-fed-per-day units (husbandry)
    float buildMat = 0.15f;        // local wood and stone availability 0.15..1 (build pace)
    float granaries = 0;           // completed granaries (drawn on the map)
    float buildWork = 0;           // man-days left on the granary going up, 0 = none
    float fillLo = 2, fillHi = -1; // store-fill extremes in the current cycle
    double cycleT = 0;             // when the current fill cycle began
    float granNeedYrs = 0;         // consecutive years the fill signal held
                                   // (need-driven granary invention)
    float starvedYr = 0;           // people lost to hunger in the trailing year
    double lookAgainDays = SPLIT_AFTER_DAYS; // patience before the next survey
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
    float kFish = 0; // people the water in reach could feed, at full gear
    float sFish = 0; // how good this shoreline is, 0..1, for taking up the trade
    float claim[CLAIM_SECTORS] = {};
    float claimKm2 = FORAGE_KM2; // worked value of the whole claim, cached (set on founding)
    double claimT = 0;           // when the frontier was last worked outward
    // Heat and the labour ledger (Design/Resources.md):
    float sWood = 0;   // wood cover in reach, 0..1 (fuel gathering pace)
    float fuelS = 0;   // the woodpile, kg of wood-equivalent (dung dries into it too)
    float coldYr = 0;  // people the cold took in the trailing year (subset of starvedYr)
    float labFuel = 0; // share of the labour budget on fuel, last integrated day (readout)
    // Farming's reach, and the farmsteads that extend it:
    float farmsteads = 0;     // standing farmsteads (drawn on the map, slot order)
    float fsteadWork = 0;     // man-days left on the farmstead going up, 0 = none
    uint8_t builtFsteads = 0; // finished this step; the sim reports and clears
    // The fields themselves: built plots, village first, then per farmstead.
    float tilled[1 + FSTEAD_MAX] = {}; // km2 under the rotation at each site
    float farmK = 0;                   // people the standing fields feed at full expertise
                                       // (sim::updateFarmland caches it from tilled and the
                                       // suitability where each site stands)
    float tillWork = 0;                // man-days left on the plot being cleared, 0 = none
    int8_t tillSite = -1;              // where that plot is: 0 the village, 1+k farmstead k
    int8_t tillSiteNext = -1;          // where the next order would go, -1 = no room
    uint8_t fsteadMax = 0;             // farmstead slots the claim can hold
    bool fsteadNextOk = false;         // the next slot stands on land worth tilling
};

// A migrating group: a settlement with velocity (Design/Migration.md). It
// forages the cell it stands on with a reduced time budget, carries a small
// store, and re-evaluates every few days. The first agent.
enum : int { BAND_MIGRATE = 0, BAND_RAID = 1 };

struct Band {
    uint32_t id = 0; // stable identity (indices shift as bands die)
    Cohorts pop;     // a migrating group is families; a raid is men
    int purpose = BAND_MIGRATE;
    uint32_t homeId = 0;    // the settlement a raiding party returns to
    uint32_t targetId = 0;  // the settlement it set out to rob
    uint32_t sid = 0;       // for a whole community on the move, its own
                            // settlement id, carried so identity survives
                            // the journey (0 for colonists, who are new)
    bool returning = false; // homeward, with whatever it got
    float loot = 0;         // rations carried
    float lootHerd = 0;     // livestock driven along
    float px, py, pz;       // unit-sphere position
    float P = 0;            // people
    float S = 0;            // food store, rations
    int targetCell = -1;    // rough destination (a rumour, re-checked up close)
    bool resting = false;   // stopped to refill the store
    double restStart = 0;
    double t = 0; // sim day at which the state is valid
    double nextUpdate = 0;
    // What the site they left could still feed, for a whole community that
    // picked up and went (0 for a splinter band, which has left nothing).
    // New ground has to beat it: a place they judged unable to keep them
    // cannot be the place they settle again. Journey state, not saved.
    double setOut = 0; // when this journey began, for fading hope
    int fromCell = -1; // where they started, so arrivals can say how far
    float water = 0;   // person-days of water in the skins
    float bows = 0;    // carried: the first possession that travels
    uint16_t culture = 0;
    char name[16] = {};     // the community's name; the suffix comes from purpose
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
    struct LandScar {
        float R;
        double t;
        uint32_t by;
    };
    std::unordered_map<int, LandScar> scars;
    // What is left standing where an invested settlement walked away.
    struct Ruin {
        int cell;
        double abandoned;
        char name[16];
    };
    std::vector<Ruin> ruins;
    // Per-cell local properties, kept for founding settlements at runtime:
    std::vector<float> kFoodPMap, kWaterMap, sFarmMap, pastureMap, buildMatMap, kGameMap, kSmallMap,
        kFishMap, sFishMap, sWoodMap;
    std::vector<Culture> cultures;
    std::vector<Event> events;     // this step's news
    int eventCount[EV_KINDS] = {}; // exact totals, even past what is kept
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
            float d = std::acos(std::clamp(
                          terrain::dot(cellN(f.settlements[i].cell), cellN(f.settlements[j].cell)),
                          -1.0f, 1.0f)) *
                      6371.0f;
            if (d <= CONTACT_KM) {
                f.neighbours[i].push_back(j);
                f.neighbours[j].push_back(i);
            }
        }
}

// ------------------------------------------------------------- fishing
//
// Water feeds people, and it feeds them differently from land: a coast is
// not worn out by being fished, and it does not deliver evenly through the
// year. Both of those matter. The first means a fishing settlement never
// degrades its way into moving; the second is why fishing and storage
// belong together -- a run is a glut you must keep or waste.
//
// Yields are people per km^2 of claim at the same scale as the land's, so a
// rich cold coast is worth about as much as forest and a warm one rather
// less. Cold water carries more life than warm: herring, cod and salmon
// have no tropical equal.
// Calibrated down from a first pass that made fishing universal: with a
// small stream worth as much as a coast, 5,710 of 6,211 settlements lived
// "on water" and fish came to a third of the world's food. A creek is not a
// fishery. Only the sea, a real lake and a major river are.
constexpr float FISH_SEA_KM2 = 0.50f;       // a coast worth living on
constexpr float FISH_LAKE_KM2 = 0.30f;      // a lake shore
constexpr float FISH_RIVER_KM2 = 0.30f;     // a river worth a weir, at full size
constexpr float FISH_RIVER_FULL = 20000.0f; // km2 of drainage that counts as full
constexpr float FISH_BASE = 0.30f;          // what a bank and a spear take without gear
constexpr float FISH_WINTER = 0.35f;        // the share of a run that is there off-season
// Above this a shoreline is as good as it needs to be for anyone to bother
// learning the trade; below it, proportionally less.
constexpr float FISH_SUIT_FULL = 0.45f;
// What the gear is worth: a bank and a spear take a third of what weirs,
// traps and nets take.
inline float fishEff(float expertise) { return FISH_BASE + (1.0f - FISH_BASE) * expertise; }

// What a settlement's food flow depends on besides its own fields: the
// climate at its site, its expertise in each trade, its affinities, its
// bows, the health of its regional game pool. Filled once per wake by
// sim::seasonCtx and read by population::foodFlow and population::advance.
struct SeasonCtx {
    const atmosphere::Climatology* clim = nullptr;
    terrain::V3 n{};
    float h = 0;
    float farmFlow = 0; // people the fields feed at current expertise (farmK * exp)
    float farmExp = 0;  // farming expertise (clearing pace and demand)
    float husbExp = 0;  // husbandry expertise
    float granExp = 0;  // granary expertise, 0 unless practising (build pace)
    Affinity aff;       // what these people are good at, from doing it
    float gameG = 1;    // regional game pool health (Settlement::gameNow)
    float bowCover = 0; // share of hunters carrying a bow
    float archExp = 0;  // archery expertise
    float fishExp = 0;  // fishing expertise
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

// The season-interpolated site temperature from the cached profile.
inline float cachedSeasonT(const Settlement& s, double t) {
    double sf = std::fmod(t, 365.0) / 365.0 * 4.0 - 0.5;
    int s0 = ((int)std::floor(sf) % 4 + 4) % 4, s1 = (s0 + 1) % 4;
    float f = (float)(sf - std::floor(sf));
    return s.tSeason[s0] * (1 - f) + s.tSeason[s1] * f;
}

// A settlement at the moment it comes to be: its site, its people, the
// condition of the ground, valid from `now`. Both founding sites -- the
// world's opening (population::build) and a band arriving
// (sim::foundSettlement) -- start here, so every other member keeps its
// default and a member added above is initialised in one place.
inline Settlement newSettlement(int cell, const Cohorts& pop, float P, float R, double now) {
    Settlement s;
    s.cell = cell;
    s.pop = pop;
    s.P = P;
    s.R = R;
    s.t = now;
    s.nextUpdate = now;
    return s;
}

} // namespace population
