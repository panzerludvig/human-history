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
inline int SPINUP_DAYS = 365;         // discarded first year
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
// The greenhouse is made of water, so it cannot be one number. Dry polar air
// holding half a millimetre returns almost as much as saturated tropical air
// holding thirty, which left the winter pole radiating a net 23 W/m2 when
// life loses 40-60 -- and no amount of cooling elsewhere could get it below
// about -19 degC. It is also why deserts are cold at night and the tropics
// are not.
// The shape of that is not free. A band absorber whose lines are already
// saturated at their centres grows on the curve of growth: opacity goes as
// the square root of the amount, or slower -- not linearly, and not as a
// simple exponential in the amount either. Measured clear-sky downward
// longwave anchors it at both ends: about 0.6 of a blackbody over the 2 mm
// of a polar winter, about 0.87 over the 25 mm of a tropical column. Those
// two numbers fix the pair below; neither is a dial.
//
//   eps = 1 - exp(-A * W^B)
//
// The previous version used a plain exponential with a reference depth, and
// that depth had to be moved from 8 mm to 2 to stop the world freezing --
// which is the sound of a wrong functional form being pushed through the
// right point.
constexpr double EMISS_A = 0.735, EMISS_B = 0.318;
inline double tauOf(double wv) { return EMISS_A * std::pow(std::max(wv, 1e-3), EMISS_B); }
inline double emissOf(double wv) { return 1.0 - std::exp(-tauOf(wv)); }
// The whole column. A thinner, more responsive layer (3e6, the lowest
// kilometre or two) gives a far better seasonal swing and much better
// mid-latitudes -- and hands the poles back to the latent pump, +22 degC in
// summer. That trade is the open question on this branch.
inline double C_AIR = 1.0e7;        // J/m2/K: cp * p / g
// This used to be a single number, and it was swept: 15, then 5 because that
// scored well on a summer target, then 18 when a transect showed what 5 was
// really doing -- land at 900 m sitting at +27.7 degC in a 53-degree-south
// summer with the air above it at -14, a forty-degree gap, humidity collapsed
// to 6%, and nothing able to rain. Three settings, none of them derived.
//
// It is not a number. It is the bulk aerodynamic formula, measured for a
// century:
//
//   H = rho * cp * C_H * |U| * (Ts - Ta)
//
// so the coefficient is rho*cp*C_H*|U| and it DEPENDS ON THE WIND. Over the
// sea at 7 m/s that is about 11 W/m2/K; over rough land at 4 it is about 19.
// Both of the numbers this was swept between are in there -- they are just
// different places, not different worlds.
constexpr double RHO_CP = 1205.0;    // J/m3/K
constexpr double CH_SEA = 1.3e-3;    // dimensionless exchange coefficient
constexpr double CH_LAND = 4.0e-3;   // rougher ground mixes harder
// Wind is never actually zero at the surface: convection stirs the air on
// its own in a dead calm, and without this the formula decouples ground from
// sky entirely wherever the flow is slack. The standard gustiness floor.
constexpr double U_GUST = 2.0;       // m/s
// TWO LAYERS. The air is a boundary layer -- the 1200 m that the dynamics
// move, with its own temperature Tb and its own heat capacity, rho*cp*H --
// under a free troposphere holding the rest of the column, with its own
// temperature Tf. One layer with one temperature could not radiate both
// ways: the face toward space fixes that temperature near -20 degC (255
// W/m2 through a 0.9 emissivity), and the face toward the ground then had
// to be faked with a constant 27 K offset, which reached 10 degC in the
// tropics where a humid boundary layer radiates from 26. The tropical probe
// measured the cost at 80 W/m2 of back-radiation, the whole cold bias.
//
// Tb is the layer's mean temperature, about 600 m up. The air the ground
// actually touches is warmer than that by the lapse through that height:
// BL_LAPSE, and it is what the fluxes and the saturation capacity see.
inline double BL_LAPSE = 3.5;       // K, near-surface air above the layer mean
// Between the two layers' emitting levels stands about five kilometres of
// air, and a parcel crossing it dry-adiabatically changes by g*dz/cp: air
// detrained upward arrives in the free troposphere colder by this, and air
// subsiding into the boundary layer arrives warmer by it. The latent heat
// a rising parcel releases on the way is NOT in this number -- it is added
// where the rain condenses, in the free troposphere, so a moist ascent
// arrives at the dry figure plus its own condensation, which is the moist
// adiabat by construction. This is also what makes the vertical exchange
// conserve energy: both directions cross the same gap, so the potential
// energy lent going up is returned coming down.
inline double GAP_BF = 49.0;        // K, dry-adiabatic gap between the layers' emitting levels
// A layer's two faces are not at one temperature either. The free
// troposphere's base sits just above the boundary layer, five kilometres
// below its emitting level and a moist lapse warmer -- and that base is the
// face the boundary layer and the ground see. With both faces at the
// emitting temperature the first two-layer run had the free troposphere
// returning 166 W/m2 to a boundary layer radiating 232 up at it, which
// cooled that layer 10 K below the sea and put back-radiation at 293,
// LOWER than the one-layer scheme managed. The boundary layer's own faces
// straddle BL_LAPSE the same way: down from its base, up from its top.
inline double FT_FACE = 27.0;       // K, the free troposphere's lower face above its emitting level
// Vapour is bottom-heavy: with the 2.4 km scale height Q_SCALE implies, the
// lowest 1200 m hold 1 - exp(-1200/2450) of the column. Both the optical
// depth and the sunlight the vapour absorbs are split by this share.
inline double BL_WV_SHARE = 0.39;   // of the column's water in the boundary layer
// Convection is a one-way street, and that too has a measured form rather
// than a second constant. Ground warmer than the air boils heat upward at
// the full exchange rate; ground colder sits under an inversion, and the
// turbulence doing the exchanging dies away as the stability grows. The
// Louis stability function is the standard shape -- exchange falling off as
// 1/(1 + b*Ri) -- and with the temperature gap standing in for the
// Richardson number that is the line below. A 25-degree inversion leaves
// about a fifteenth of the neutral rate, which is where the old 0.8 came
// from; it just no longer has to be told.
constexpr double STAB_B = 0.5;      // per K of inversion
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
// Melting holds a surface at freezing: ice takes 334 kJ/kg without changing
// temperature, which is why a polar summer sits near zero however long the
// sun is up.
//
// It works in BOTH directions and this only damped warming. Freezing water
// releases exactly the same latent heat and resists cooling by exactly as
// much, so a one-sided version is a ratchet: a cell crossed the freezing
// point downward at full speed and had to climb back at an eighth of it. Add
// the hard fifty-fold drop in heat capacity at -1 degC, where the ocean slab
// becomes a skin of sea ice, and a cell that dipped below freezing cooled
// fast and warmed slowly -- which is a trapdoor into the ice-albedo
// feedback, built out of an asymmetry that has no physical counterpart.
constexpr double MELT_DAMP = 0.12;
// Sea ice insulates. Below freezing a skin of ice cuts the ocean's 25-metre
// slab off from the air: the ice SURFACE radiates down towards -40 while the
// water beneath stays near -1.8. Without it the slab's whole heat capacity
// resists cooling and a polar winter cannot get below about -6 degC -- which
// is what left them twenty-five degrees too warm, with a radiation budget
// that was otherwise correct.
constexpr double C_SEAICE = 2.0e6; // J/m2/K: a thin skin, not an ocean
// And a skin has water under it. Sea ice does not sit on nothing: the ocean
// beneath is held at the freezing point of salt water, and heat conducts up
// through the ice to whatever the surface has cooled to. That flux is why an
// Arctic winter stops at about -35 and not at the -75 an ice surface radiating
// into a clear polar sky would otherwise reach -- which is exactly where this
// model went the moment a boundary condition stopped manufacturing heat for
// it.
//
//   F = k_ice * (T_freeze - T_surface) / h
//
// k for ice is 2.2 W/m/K. The thickness is not the ice alone: snow on top
// conducts about a seventh as well, so a metre of it counts for seven, and
// the figure that matters is the ice-equivalent depth of the whole cover.
// Two and a half metres is the Arctic's, and 2.2/2.5 gives 0.88 W/m2/K --
// some 26 W/m2 under a surface at -30, against the 10 to 40 that is
// measured.
//
// Be clear about what this is: the water below is treated as a reservoir at
// a fixed temperature, and a reservoir that never runs down is an implicit
// ocean heat transport. That is honest for the polar ocean -- the real one
// IS kept near freezing by water arriving from further south, and this model
// has no ocean circulation to do it -- but it is standing in for a mechanism
// rather than being one, and the day the ocean moves heat for itself this
// should come out.
constexpr double SEA_FREEZE = -1.8;    // degC, salt water
constexpr double K_ICE_COND = 0.88;    // W/m2/K through ice and its snow
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
// Clouds were a flat 17% taken off the sunlight everywhere -- in the desert,
// under the overcast, at the pole. That is not a cloud, it is a planetary
// albedo correction wearing a cloud's name, and it meant the one radiative
// feedback that decides where deserts are was absent from the model
// entirely: the cloud field was diagnosed at the very end, for the picture,
// and fed back into nothing.
//
// A real cloud reflects about 0.31 of what falls on it. Earth is about 0.67
// covered, and 0.31 * 0.67 is the 0.21 that clouds contribute to planetary
// albedo -- so this is anchored to the same measurement the flat number was.
// It now goes where the cloud actually is.
constexpr double CLOUD_ALB = 0.31;
// The atmosphere is not transparent to sunlight, and this model had it so.
// Every watt that was not reflected went straight to the ground: the air's
// energy budget contained absorbed longwave, sensible heat and condensation,
// and no shortwave term of any kind.
//
// Life absorbs about 77 W/m2 of the 340 arriving -- water vapour across the
// near infrared, ozone in the ultraviolet, and the cloud drops themselves --
// which is 23% of the total and about a quarter of the beam that gets past
// the cloud tops. The surface then receives 163, not 240.
//
// Getting this wrong does not just misplace heat, it misplaces it in the one
// direction that matters here: the surface was being given half again too
// much and the air none, so every watt the air needed had to arrive as
// sensible heat or condensation, and the air ran cold while the ground ran
// hot. A cold air layer holds less water (capacity is set by Ta), and less
// water is a thinner greenhouse.
constexpr double SW_ATM = 0.25;   // share of the sub-cloud beam absorbed aloft
// And clouds work the other way too, which a flat shortwave factor cannot
// express at all: a deck is nearly black in the longwave and shuts whatever
// window the vapour left open. It is why a cloudy night does not frost.
constexpr double CLOUD_LW = 0.75;   // share of the remaining window a full deck shuts
// How much of a cell is under cloud, from how near its column is to
// saturation. This cannot be derived, and it is worth being exact about why:
// cloud forms where a LAYER reaches its dew point, and a column that is 61%
// saturated on the average contains layers that are at 100%. A model with
// one layer has no access to that distribution, so the mapping from column
// humidity to cloud cover is a stand-in for structure it does not carry.
//
// Which makes it a free function -- and the discipline for a free function is
// that it gets calibrated against the thing it represents, never against
// something downstream that it happens to move. So: RH_CLOUD is set so the
// global mean cloud fraction comes out at the measured 0.67, and the shape is
// checked against the ends -- about 0.2 in the driest subtropical descent,
// about 0.9 under the rising branch. It is NOT set by what it does to the
// temperature, even though it is the strongest lever in the model on exactly
// that.
//
// The formula it replaces, (rh - 0.62)/0.33, produced a global cloud cover of
// THREE percent. Clouds were reflecting 0.009 of the sunlight instead of
// 0.21, the planet was absorbing some 68 W/m2 too much, and that was the
// whole of a six-degree warm bias.
constexpr double RH_CLOUD = 0.58;
inline double cloudOf(double rh) {
    double r = std::max(rh, 0.0) / RH_CLOUD;
    return 1.0 - std::exp(-r * r);
}
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
// Saturation capacity of the column, in millimetres, as a function of
// temperature. Two of these three are fixed by measurement and the third has
// to agree with them:
//
//   CAP_SCALE   Clausius-Clapeyron: saturation humidity doubles for every
//               ten degrees, so the e-folding is 10/ln2 = 14.4 K.
//   CAP0        saturation at CAP_T0. At 15 degC the vapour pressure is
//               17.04 hPa, so q_sat = 0.622 * 17.04 / 1013 = 0.0105 -- and
//               through the SAME column ratio the evaporation term uses,
//               Q_SCALE, that is 0.0105 * 2100 = 22 mm.
//
// It said 15. The two constants that have to be consistent with each other
// were not, by nearly a factor of two, and the column was carrying 10 mm of
// water where life carries 25 -- which is a weaker greenhouse, a colder
// world, and less rain, all from a disagreement between two lines.
constexpr double CAP0 = 31.0, CAP_T0 = 15.0, CAP_SCALE = 14.4;
// Evaporation is that same bulk formula with humidity in place of
// temperature -- it is the same turbulence doing the carrying:
//
//   E = rho * C_E * |U| * (q_sat(Ts) - q_air)
//
// C_E equals C_H to within a few percent, so it gets no symbol of its own.
// What this does need is a way to read a specific humidity off a column
// depth, since the moisture variable here is precipitable water: Earth's
// column holds about 25 mm against a surface specific humidity of 0.012, a
// ratio of about 2100 kg/m2. That is a measured property of the real
// atmosphere's vertical structure, not a fitted one.
//
// The old pair were free constants swept between 0.10 and 0.20, with a
// linear temperature factor bolted on top that double-counted what the
// saturation capacity already says.
constexpr double Q_SCALE = 2940.0;  // kg/m2 of column per unit specific humidity
// The surface exchanges with the BOUNDARY LAYER, not with the column mean.
// Vapour is bottom-heavy, so the air the sea actually touches sits far
// nearer saturation than the column: Earth's ocean column runs about 50%
// RH while its screen-level air runs about 80%, and that measured pair
// fixes this constant -- bl = 1 - 0.4*(1 - column). Reading the whole
// column's deficit instead made evaporation feed on its own rain: the
// harder it rained, the emptier the column, the drier the surface read the
// air, the harder the sea evaporated. Measured at the tropical-ocean
// probe: 145 W/m2 of latent heat off a 10 degC sea, where a 27 degC Earth
// gives 120 -- the churn that exported every watt the greenhouse returned
// straight to the poles (residence 1.8 days, polar rain 3.6 mm/day).
constexpr double BL_DRYNESS = 0.4;  // how much of the column's dryness reaches the surface
constexpr double H_FLOW = 1500.0;               // m, depth of the inflow layer
// Rain falls when moisture exceeds a fraction of the effective capacity.
// Vertical motion modulates that capacity: uplift (convergence, windward
// slopes) shrinks it -- adiabatic cooling -- and subsidence swells it, which
// is what makes descent zones and lee sides dry.
// Rain has one cause: air rises, cools, and cannot hold what it carried up.
// Everything below is a way for air to rise. What was here before -- a
// three-day timer over land, a storm term acting straight on the column, and
// convergence fiddling with the capacity rather than lifting anything -- were
// shortcuts, and they behaved like shortcuts: each switched itself off in the
// regime where it was needed, because each was keyed to the temperature it
// was supposed to be controlling.
// Rising air cools, and cooling air holds less: that is the whole of it.
// Uplift does not rain the column out, it lowers what the column may keep,
// and only the excess falls. Written the other way -- a share of the column
// rained out per unit of ascent, whatever its humidity -- lifting DRY air
// made rain, and the atmosphere was drained as fast as it evaporated: 0.66 mm
// of water in the air against life's 25, and a residence time of 0.2 days
// against nine. Nothing could survive the trip from sea to land, so 70% of
// land was desert no matter what was done to the rain over it.
// Rising air cools and loses the capacity to hold water; sinking air warms
// and gains it. That is ONE process with a sign, and it had become two
// coefficients that did not agree: ascent shrank the capacity as exp(-14 w)
// and descent grew it as the linear 1 + 50|w|. Neither number was derived,
// and the disagreement between them was doing real damage -- a cell under
// mild subsidence was granted several times its saturation capacity, filled
// up to thirty millimetres at minus twelve degrees, and then dumped the lot
// the moment it drifted somewhere the sign flipped: fifty-one millimetres a
// day of rain in a band where the wind field is entirely unremarkable.
//
// The one coefficient is not free either -- but nor is it a coefficient. A
// parcel moving vertically at w for the time it takes to rise, UPLIFT_TAU,
// is DISPLACED by w*tau, and its temperature changes by the lapse rate times
// that displacement; saturation capacity follows exp(dT/CAP_SCALE).
//
// Written as exp(-Gamma*tau/CAP_SCALE * w) that is an exponential in a
// velocity the model derives from its own wind field, and that is where it
// went wrong. The exponent worked out at 36, the hour-to-hour scatter in w
// is about 0.1 m/s, and the mean of an exponential is not the exponential of
// the mean: half a squared exponent times the variance is +1.6 in the log,
// so a band whose MEAN ascent was a perfectly ordinary +0.033 m/s came out
// with a mean capacity of 2261 mm against the 14.5 still air can hold. A
// hundred and fifty times over-saturation-proof: nothing there could ever
// reach its dew point, so nothing rained, so the air stayed at three
// hundredths of a millimetre, so it was transparent to longwave, so the
// surface sat at -65 with the air fifty degrees warmer above it. That band
// was the single largest error left in the model and this was all of it.
//
// The fix is not a smaller exponent, it is the missing bound. A parcel
// cannot be displaced further than the moist layer is deep -- 0.1 m/s for a
// day is 8.6 km, which is out through the top of the troposphere and not
// something the air actually does. Bounding the DISPLACEMENT bounds the
// capacity to a factor of 3.5 either way, which is the real range, and the
// blow-up goes with it.
constexpr double LAPSE_MOIST = 0.006;   // K/m, a saturated ascent
constexpr double H_LIFT_MAX = 3000.0;   // m, the depth that holds the water
// Large-scale ascent is centimetres a second, not metres: a whole grid cell
// does not rise like a thunderhead. First pass had fronts lifting at 22 cm/s
// and the world raining 18 mm a day.
inline double W_OROG = 0.35;       // only the windward slope of a cell rises
// Convection rises with the heat the ground is actually giving the air, not
// with a threshold on temperature. Keyed to the lapse gap it almost never
// fired -- the ground has to stand 27 K above the emitting level before that
// term wakes up -- and continental rain went with it: 70% of land came out
// desert against life's third. The sensible heat flux IS the vigour of
// convection, and it is already computed.
// Large-scale mean ascent is a centimetre a second even in the ITCZ; a grid
// cell two hundred kilometres wide does not rise like a thunderhead. At the
// first scaling this alone put a tenth of a metre a second over every warm
// surface, which collapsed the capacity and rained the column dry.
// And the flux that drives it is the whole turbulent flux, not the sensible
// part alone. Deep convection over a warm ocean is MOIST convection: the
// buoyancy comes from latent heat released aloft, and the energy available for
// it is the surface moist static energy flux, sensible plus latent. Over the
// tropical ocean that is 10 W/m2 of sensible against 120 of latent, so feeding
// this term the sensible part alone hands it a tenth of what it should have.
// Measured, it showed: equatorial ascent came to 0.0011 m/s, the weakest on the
// planet outside the poles, against the 0.005 to 0.01 of a real ITCZ -- and the
// water was there, the column peaking at 25 mm right at the equator with
// nothing to lift it. The constant below was never the problem; the comment
// above says it is scaled for a centimetre a second in the ITCZ, and with the
// full flux (about 130 W/m2) it delivers exactly that.
inline double W_CONV = 5.0e-5;     // m/s per W/m2 of turbulent heat into the air
// But the flux is the FUEL, not the SWITCH. Every warm sea gives up a
// hundred watts and more, and most of it goes into shallow cumulus that
// never leaves the trade inversion. Deep convection -- the kind that
// detrains through the top of the layer and exports mass -- needs the
// surface parcel to be buoyant against the free troposphere, and on Earth
// that is a sharp threshold: deep-convective frequency over the ocean
// jumps an order of magnitude between 26 and 28 degC sea surface. Driven
// by the flux alone, the ITCZ rate was applied to every ocean cell out to
// 30 degrees (0.009 m/s across 17S-17N, still 0.005 at 39), the mean
// export came out three times what the kUp derivation assumed (0.0074
// against 0.0025 m/s), the tropical air lost 90 W/m2 to the planetary mean
// against Earth's 30-45, and the sea beneath could not get past 12 degC.
// And because the flux is proportional to wind and the export digs the low
// that drives the wind, the loop closed on itself and only drag bounded
// it: 12 m/s where Earth runs 6-7.
//
// The switch is moist static energy: the surface parcel's h = cp*T + L*q
// (saturated at the skin) against the saturated h* of the free troposphere
// at its emitting level, cp*Tf + g*z_e + L*q*(Tf, p_e). Everything in the
// second bracket but Tf is fixed by the layer's geometry: the emitting
// level sits near 6 km at about 470 hPa, so saturation there is the surface
// curve scaled by 1000/470 (CONV_QSAT_LIFT), and the g*z_e term is folded
// into CONV_BAR. That bar is CALIBRATED, not derived: 96.6 kJ/kg is the
// figure that puts the threshold at 26.5 degC skin when the free
// troposphere stands at Earth's tropical -18 degC. Read as a height it is
// nearer 10 km than 6, and the difference is the price of one level
// standing in for the mid-tropospheric h* minimum. The ramp (CONV_RAMP)
// opens the gate over about 2.5 K of skin: half at 27.5, full at 29, which
// is the spread of the observed frequency curve.
//
// What this buys is a bar that MOVES. Tf is warmed by every rain that
// falls and mixed through the upper pool, so convection raises its own
// threshold and the warmest sea alone clears it: the weak-temperature-
// gradient limit, which is what bounds this feedback on Earth. A constant
// threshold in W/m2 would sit at one climate only.
constexpr double CONV_BAR = 96.6e3;      // J/kg, see above
constexpr double CONV_QSAT_LIFT = 2.1;   // saturation at 470 hPa over its surface value
constexpr double CONV_RAMP = 1.0e4;      // J/kg of excess for the gate to open fully
// div[] is ALREADY a vertical velocity in m/s -- convergence and orography
// both, computed in the block above. Multiplying it by 600 as though it were
// a divergence in 1/s, and adding a second orographic term on top, gave
// updraughts of metres a second: capacity collapsed everywhere, and the
// column rained itself dry the moment anything evaporated into it.
inline double W_DIVERGE = 1.0;     // it is already the velocity
// How long a parcel takes to rise through the depth that rains, and so the
// window over which condensation actually integrates. Faster than this is
// weather the grid cannot resolve, and it cancels.
constexpr double UPLIFT_TAU = 86400.0; // s
inline double W_FRONT = 200.0;    // m/s per (K/m) of temperature gradient
// (There was a W_COAST here: uplift driven by the gradient of the SURFACE
// temperature, added to wet the coasts when interiors were raining a third
// more than they should. It was a fitted duplicate of the frontal term above
// reading the wrong field, and reading that field is what made it wrong. A
// front is a slope in the LOW-LEVEL AIR, and air is continuous; a skin
// temperature is not. Across a sea-ice edge the ground jumps fifty-seven
// degrees in a single cell, which the term read as a quarter of a metre per
// second of ascent, which collapsed the saturation capacity to a fortieth
// and rained the whole column out on the spot. That is what left the band
// from 60 to 80 degrees holding a hundredth of a millimetre of water --
// transparent, radiating freely to space, and sitting at -65 with air at
// -12.5 fifty degrees above it. It also explains the coast-interior contrast
// it was hired to fix, which was never a missing sea breeze: it was the
// interiors raining out water that should have reached them.)
// Air that is sinking is warming, and warming air is further from
// saturation: that is why the subtropical oceans are deserts under the
// descending branch of the Hadley cell, and why their moisture survives to
// blow somewhere else. Removed as a "shortcut" when rain was rebuilt around
// uplift -- and without it the sea rained out everything it evaporated, so
// nothing reached a continental interior and 70% of land was desert.
// Neutered to nothing when div[] turned out to be a velocity: with the
// 0.001 that came in alongside, a centimetre a second of descent raised the
// capacity by under one per cent. Subsidence is what makes the subtropical
// highs deserts and what keeps the air over a cool coastal sea from raining
// its load before it reaches the shore.
// (SUBSIDE_DRY lived here: a second, linear, unrelated coefficient for the
// descending half of the same process. It is LIFT_K now, with the sign it
// always had.)
// Rain begins here, so the air settles just above it: at 0.80 the whole
// world sat at 85% humidity, which -- since cloudiness was humidity, one for
// one -- covered the globe in cloud. A column is about half saturated in
// life.
inline double RAIN_FRAC = 0.55;    // sub-grid: part of a cell saturates first
inline double RAIN_RATE = 0.15;    // fraction of that excess per hour
constexpr double DIV_CAP_SCALE = 0.05;          // m/s of uplift for a ~46% capacity swing
// Over land, moisture rains out progressively along its path (precipitation
// is not withheld until a convergence line): an e-folding of ~3 days, i.e.
// ~1300 km at typical winds. This is what makes coasts wetter than deep
// continental interiors.
constexpr double LAND_RAINOUT_TAU = 3.0 * 86400.0; // s
inline double K_DIFF = 2.0e5;                // m^2/s eddy diffusion of moisture
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
inline double KT_DIFF = 1.5e6;               // m^2/s eddy diffusion of heat

// ---------------------------------------------------------- the moving air
//
// Pressure used to be a function of temperature: smooth the temperature
// field twice, call it pressure, solve a balance for an instantaneous wind.
// Nothing persisted, nothing was carried, and a high vanished the moment the
// temperature beneath it changed. Transport was therefore a diffusion
// constant -- scale-free, memoryless, and unable to feed the mid-latitudes
// without flooding the poles, which is the wall the whole calibration ran
// into.
//
// Now the air has state. Mass converges and the layer thickens; the wind
// answers the slope; the mass moves on. Highs and lows form, drift, and die,
// and they carry heat and water with them -- which is transport that arrives
// dry, because it rained on the way.
//
// The wave speed is the atmosphere's FIRST INTERNAL mode, about 50 m/s, not
// the external mode's 300: weather travels at the former, and the latter
// would need a two-minute timestep. That is what makes ten-minute dynamics
// inside an hour of physics stable.
// Scaled to the real geostrophic relation, v = (g/f) dZ/dy. Earth's 500 mb
// surface stands about 500 m higher over the tropics than over the pole, and
// that slope is what drives a 10 m/s wind. The first attempt used a reduced
// gravity and 0.75 m per K, which gave height anomalies of a few metres and
// winds of about one -- the air moved, but it carried nothing, and the
// moisture distribution did not change at all when advection was fixed.
inline double GPRIME = 9.81;        // m/s2: it is a height, so it is gravity
// The balanced wind is g*grad(h)/f and does not depend on H at all, so the
// layer can be made thinner purely to slow the gravity waves and buy
// stability: 1200 m gives 108 m/s, comfortably inside the step even where
// the meridians crowd.
inline double H_LAYER = 1200.0;     // m, mean thickness: c = sqrt(gH) ~ 108 m/s
// A warm column stands taller -- and that is the UPPER level's pressure
// field, not the surface's. The column stretches because mass has been
// exported off the top, and the surface, which feels only the weight of
// what remains, reads LOW. This constant was scaled to the 500 mb surface
// (warm = high), and the layer's winds were then used as surface winds for
// evaporation, advection and convergence -- so surface air DIVERGED from
// every warm region: convergence at the equator measured negative, the
// trades absent, summer continents expelling their own moisture. One layer
// cannot wear both levels' pressure; this one is the surface, because
// everything that consumes its wind lives there.
//
// The surface figure is NOT the heat low's 1 hPa per K -- that is a local
// anomaly scale, and mass redistribution cancels most of it at planetary
// scale. Anchor the SPAN instead: Earth's zonal-mean sea-level pressure
// runs about 25-30 hPa from the equatorial trough to the winter highs,
// over a column-temperature span of some 60 K. That is ~0.5 hPa per K, and
// 1 hPa is 8.5 m of this layer: 4 m/K. The first flip kept the old 10 and
// produced a 72 hPa planet -- winds of 6 m/s, which was the target, but
// arrived at by overdriving the gradient rather than by the export digging
// real structure.
inline double THERM_H_PER_K = 4.0;  // m of surface trough per K of warmth
inline double THERM_TAU = 2.0 * 86400.0; // s, how fast thickness follows warmth
// The upper branch. Diabatically rising air -- convective and frontal --
// leaves the layer through its top, and the surface pressure beneath falls
// by exactly the mass that left; the upper flow spreads it, and it comes
// back down everywhere else as the area-weighted mean. That export is what
// digs the equatorial trough under the ITCZ and the subpolar lows under the
// storm tracks, and what leaves the subtropics standing high between them:
// trades, westerlies and monsoon inflow are all this one term. Without it
// no low could survive its own inflow -- frictional convergence FILLS a
// shallow-water low, and the model had no mechanism to empty one.
//
// Only the diabatic ascent exports. The resolved-convergence uplift is the
// layer's own divergence and feeding it back into the mass field is a
// positive feedback with nothing physical to bound it; orographic ascent
// moves air over a ridge without destroying column mass, and exporting it
// would pull wind toward every range.
inline double W_EXPORT = 1.0;       // share of diabatic ascent that detrains aloft
// And the branch carries heat, not only mass -- BOTH branches do, and each
// carries the heat of the air that is actually moving, at the rate its mass
// actually moves: rho*cp times a velocity, against the heat capacity of the
// layer it lands in. No constant of its own.
//
// LOWER: the boundary layer's wind advects the boundary layer's own
// temperature against the boundary layer's own capacity, rho*cp*H_LAYER.
// (When there was one column temperature this had to be scaled by the
// layer's share of the column mass, and before that it was applied at
// full column strength and froze the planet.)
//
// UPPER: detrained air leaves each column at its Tf less the gap, joins
// one well-mixed pool, and comes back down everywhere at the pool's
// temperature: through the free troposphere, then into the boundary layer
// warmed by the gap (see GAP_BF). Locating the deposit where air actually
// sinks -- warm in the subtropics rather than as a mean -- would need an
// upper wind field, and waits for that.
constexpr double CP_AIR = 1004.0;   // J/kg/K
// Away from the Coriolis balance -- at the equator, where f goes to zero --
// drag is the only thing that limits the wind, and at one part in 2.5 days
// it limited it to 260 m/s. Eight hours is what the old balanced solve used,
// and it is the honest boundary-layer figure.
inline double WIND_DRAG = 1.0 / (8.0 * 3600.0); // s^-1
inline double DYN_VISC = 6.0e5;     // m2/s, keeps the grid-scale quiet
// Gravity waves run at sqrt(gH) = 171 m/s, and the meridians crowd: a cell
// at 70 degrees is 71 km wide, so a stable step there is about 200 seconds.
// At ten minutes the dynamics were unstable and simply saturated -- 73 m/s
// of wind everywhere, sitting on the safety clamp, blowing every cell empty
// (10% humidity) before any moisture could gather.
constexpr int DYN_SUBSTEPS = 18;    // 200 seconds each

struct Climatology {
    // PROBE: the tropical-ocean column budget, term by term -- every
    // open-sea cell within 15 degrees of the equator, every hour after
    // spin-up. Order: surface SW, air SW, LW up, LW down, sensible,
    // latent, OLR, Ts, Tb, Wv, emissivity, cloud, Tf, cell-hours. The point:
    // tropical SST is set by this column's balance and almost nothing
    // else, so each term can face its measured Earth value no matter what
    // the continents are doing.
    double tropBud[14] = {};
    double dbgEvap = 0, dbgRain = 0, dbgClamp = 0; // PROBE: is water conserved?
    double dbgWv = 0, dbgWind = 0, dbgRH = 0;     // PROBE: water, wind, saturation
    // [season][cell]
    std::vector<float> meanT, rainMmDay, snowMmDay, rainProb, windU, windV, cloud, diurnal;
    // The air itself, kept for diagnosis and for the map: water in the
    // column, how near saturation it is, the height field that is the
    // pressure map, and the air's own temperature.
    std::vector<float> wv, rh, press, airT, airTf;
    std::vector<float> upConv, upDiv, upFront, upOrog, capX; // PROBE: what lifts the air
    std::vector<float> evapF, advF, difF, latF, advZF, advMF; // PROBE: the water budget
    std::vector<float> spdF, capSkinF, supplyF, affordF;     // PROBE: and the evaporation
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
        for (auto* v : {&meanT, &rainMmDay, &snowMmDay, &rainProb, &windU, &windV, &cloud,
                        &diurnal, &wv, &rh, &press, &airT, &airTf, &upConv, &upDiv, &upFront,
                        &upOrog, &capX, &evapF, &advF, &difF, &latF, &advZF, &advMF, &spdF,
                        &capSkinF, &supplyF, &affordF})
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
// How much water the COLUMN can hold, which is a property of the air and not
// of the ground beneath it. This was capOf(T_surface), and that is the
// mechanism the whole polar collapse was hiding behind: ground at -67 has a
// saturation capacity of five hundredths of a millimetre, so polar air was
// allowed to hold essentially nothing -- measured at 0.01 mm where life
// carries one or two. Nothing that dry has any greenhouse effect at all
// (emissivity 0.15), so it radiated freely to space, got colder, and was
// permitted less water still. A runaway with a positive feedback on both
// ends, driven entirely by asking the wrong thermometer.
//
// The near-surface air is the emitting layer plus the lapse through the
// depth between them -- the same quantity the sensible heat term already
// uses to decide which way the convection goes.
inline double capAirOf(double Tb) { return capOf(Tb + BL_LAPSE); }

inline int wrapX(int x) { return (x % W + W) % W; }

struct Model {
    // static per cell
    std::vector<float> elev, albedo, heatC, latRad;
    std::vector<unsigned char> water;
    // state
    std::vector<double> T, Wv, u, v;
    // scratch
    std::vector<double> nT, nW, nu, nv, div, rainStep, Tsl;
    std::vector<double> Tb, nTb, Tf, nTf; // the air: boundary layer and free troposphere
    std::vector<double> hP, nhP, nu2, nv2; // the moving air: thickness and momentum
    std::vector<double> hWant, hTmp;       // what the warmth asks of the height, smoothed
    std::vector<double> wTop;              // diabatic ascent leaving through the top, m/s
    double wTopMean = 0;                   // its area-weighted mean: what comes back down
    double tfPool = 0;                     // export-weighted mean Tf: what the upper branch carries
    // PROBE: tropical-ocean budget rows (13 terms x H); each thread owns
    // its own row of the parallel loop, so no atomics. Gated on stat.
    std::vector<double> budRow;
    bool recordBudget = false;
    std::vector<double> soil;          // land water store, mm: what there is to evaporate
    std::vector<double> evapAcc, rainAcc, madeAcc; // PROBE, one cell per thread: no atomics
    std::vector<double> advAcc, difAcc, advZ, advM; // PROBE: and what the wind and eddies bring
    std::vector<double> pSpd, pCapSkin, pSupply, pAfford; // PROBE: the evaporation, term by term
    std::vector<double> capArr;                    // how much each cell's air can hold
    std::vector<double> cloudF;                    // and how much of it has condensed out
    std::vector<double> pConv, pDiv, pFront, pOrog, pOro; // PROBE: uplift, by cause
    std::vector<double> divSm; // the ascent that lasts, as opposed to the ascent that wobbles
    // ONE capacity per cell, and everything that asks what the air can hold
    // asks this. It used to be two: transport priced a parcel's humidity
    // against the capacity of still air, while rain measured the excess
    // against the capacity the vertical motion had left. A cell could
    // therefore be filled to the first ceiling and only rain against the
    // second, and it did -- thirty millimetres of water at minus twelve
    // degrees, a diagnosed relative humidity of 2903%, and fifty-one
    // millimetres a day falling out of it wherever the sign of the ascent
    // happened to change.
    std::vector<double> capEff;
    std::vector<double> fluxE, fluxN;              // moisture across each cell's east/north face
    std::vector<double> wvAcc;                     // PROBE: column water over time
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
        }
        nT = T; nW = Wv; nu = u; nv = v;
        div.assign(W * H, 0.0);
        rainStep.assign(W * H, 0.0);
        Tsl.assign(W * H, 0.0);
        Tb = T; // the boundary layer starts wherever the ground is,
        // and the free troposphere on a first-guess moist adiabat above it
        Tf.assign(W * H, 0.0);
        for (int i = 0; i < W * H; i++) Tf[i] = Tb[i] - GAP_BF + 12.0;
        // and the seed humidity follows the air, not the ground -- Tb has to
        // exist before it can be asked.
        for (int i = 0; i < W * H; i++) Wv[i] = 0.5 * capAirOf(Tb[i]);
        nTb.assign(W * H, 0.0);
        nTf.assign(W * H, 0.0);
        hP.assign(W * H, 0.0);
        nhP.assign(W * H, 0.0);
        hWant.assign(W * H, 0.0);
        hTmp.assign(W * H, 0.0);
        wTop.assign(W * H, 0.0);
        budRow.assign(14 * H, 0.0);
        nu2.assign(W * H, 0.0);
        nv2.assign(W * H, 0.0);
        soil.assign(W * H, SOIL_REF_MM); // half full; the spin-up settles it
        evapAcc.assign(W * H, 0.0);
        rainAcc.assign(W * H, 0.0);
        madeAcc.assign(W * H, 0.0);
        capArr.assign(W * H, 0.0);
        cloudF.assign(W * H, 0.5);
        pConv.assign(W * H, 0.0);
        pDiv.assign(W * H, 0.0);
        pFront.assign(W * H, 0.0);
        pOrog.assign(W * H, 0.0);
        pOro.assign(W * H, 0.0);
        divSm.assign(W * H, 0.0);
        capEff.assign(W * H, 0.0);
        advAcc.assign(W * H, 0.0);
        difAcc.assign(W * H, 0.0);
        advZ.assign(W * H, 0.0);
        advM.assign(W * H, 0.0);
        pSpd.assign(W * H, 0.0);
        pCapSkin.assign(W * H, 0.0);
        pSupply.assign(W * H, 0.0);
        pAfford.assign(W * H, 0.0);
        fluxE.assign(W * H, 0.0);
        fluxN.assign(W * H, 0.0);
        wvAcc.assign(W * H, 0.0);
    }

    // Zonal smoothing towards the poles, where the meridians crowd together
    // and a stable timestep would otherwise be seconds. The classic polar
    // filter: the closer to the pole, the more passes.
    // Two different jobs wear this one name, and running them at the same
    // rate is what made a mess of the mid-latitudes.
    //
    // STABILITY. Where the meridians have crowded the cells narrower than a
    // gravity wave crosses in one substep -- dx < c*dt, which is 21.6 km at
    // 108 m/s and 200 s, so poleward of about 84 degrees -- the scheme cannot
    // integrate at all and has to be smoothed every substep or it diverges.
    //
    // RESOLUTION. Everywhere else the grid is merely ANISOTROPIC: a cell at
    // 60 degrees is 104 km by 208, so the zonal direction carries structure
    // the meridional cannot, and that structure is not weather. Removing it
    // is a once-an-hour tidy, not a per-substep necessity.
    //
    // Running the second at the rate of the first is a zonal diffusion of
    // 1.4e7 m2/s -- twenty times the model's own viscosity -- and it took the
    // global mean wind from 3.8 m/s to 2.0 and flattened the westerlies from
    // 9 to 2.8. One 1-2-1 pass looks harmless until it is applied eighteen
    // times an hour, a hundred and sixty thousand times a year.
    void polarFilter(std::vector<double>& f, bool stabilityOnly) {
        static std::vector<double> tmp;
        tmp.resize(W * H);
        for (int y = 1; y < H - 1; y++) {
            double cosl = std::cos(((y + 0.5) / (double)H - 0.5) * 3.14159265);
            // How much to smooth is not a taste. A cell here is dx wide and
            // dy tall, and dy does not change with latitude while dx goes as
            // the cosine: at 76 degrees the cell is 50 km by 208, an aspect
            // ratio of four. Zonal derivatives are therefore four times
            // sharper than meridional ones at the same physical scale, and a
            // height field carrying structure at the zonal grid scale gives
            // -g*grad(h) of about 1e-2 m/s2, which against the drag is a wind
            // of some seventy metres a second.
            //
            // That is what the band from 71 to 79 degrees was doing: a mean
            // SPEED of 33.7 m/s on a mean VELOCITY of 6, which is to say a
            // wind that reverses rather than blows. Every quantity that is
            // linear in the wind averaged out and looked reasonable; the ones
            // that are not did not, and the bulk formula -- which is linear in
            // speed, not in velocity -- evaporated 5 mm/day off a surface at
            // -8 degC where it should manage one.
            //
            // The old threshold, 0.35/cos - 0.8, gave its first pass at 79
            // degrees and NOTHING to the band below it, which is exactly the
            // band that blew up. The criterion is the anisotropy itself:
            // smooth zonally until the zonal resolution matches the
            // meridional one, so passes go as dy/dx - 1 = 1/cos - 1. It comes
            // on gradually from about 50 degrees, where one pass removes only
            // the two-cell mode -- structure finer than the grid resolves in
            // the other direction, and so not structure at all.
            int passes;
            if (stabilityOnly) {
                // Only where a wave outruns the cell in one substep.
                double dxHere = (2 * 3.14159265 * R_EARTH / W) * std::max(cosl, 0.02);
                double need = std::sqrt(GPRIME * H_LAYER) * (DT / DYN_SUBSTEPS);
                passes = dxHere < need ? (int)std::clamp(std::lround(need / dxHere), (long)1,
                                                         (long)8)
                                       : 0;
            } else {
                // Zonal resolution brought level with meridional.
                passes = (int)std::clamp(std::lround(1.0 / std::max(cosl, 0.02) - 1.0), (long)0,
                                         (long)8);
            }
            for (int k = 0; k < passes; k++) {
                for (int x = 0; x < W; x++)
                    tmp[idx(x, y)] = 0.25 * f[idx(wrapX(x - 1), y)] + 0.5 * f[idx(x, y)] +
                                     0.25 * f[idx(wrapX(x + 1), y)];
                for (int x = 0; x < W; x++) f[idx(x, y)] = tmp[idx(x, y)];
            }
        }
    }

    // What the warmth asks of the height field, smoothed. A height is the
    // depth-averaged warmth of a column, so it varies over a thousand
    // kilometres, not over one grid cell: taken raw, a coastline's 20 K
    // land-sea contrast became a 200 m step in 200 km, which is a geostrophic
    // wind of a hundred metres a second. Measured at 72 m/s -- pinned to the
    // safety clamp -- with the air at 12% humidity, because a wind like that
    // empties every cell it crosses before anything can gather in it. The old
    // model smoothed its pressure field twice for exactly this reason.
    void thermalTarget() {
        // NEGATIVE: this layer is the surface, and a warm column is a heat
        // low there -- the sign the old diagnostic model had ("Pa of thermal
        // low per degC") and the accepted monsoon behaviour was built on.
        // The rebuild flipped it to the 500 mb sign and the trades went with
        // it. See THERM_H_PER_K.
        // The heat low is the whole column's warmth (hypsometric), so the
        // two layers are weighted by their mass.
        const double fb = RHO * CP_AIR * H_LAYER / C_AIR;
        for (int i = 0; i < W * H; i++) {
            double tcol = fb * Tb[i] + (1.0 - fb) * Tf[i];
            hWant[i] = -THERM_H_PER_K * std::clamp(tcol - (-25.0), -60.0, 60.0);
        }
        for (int pass = 0; pass < 4; pass++) {
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++) {
                    int i = idx(x, y);
                    int yn = std::min(y + 1, H - 1), ys = std::max(y - 1, 0);
                    hTmp[i] = 0.4 * hWant[i] +
                              0.15 * (hWant[idx(wrapX(x + 1), y)] + hWant[idx(wrapX(x - 1), y)] +
                                      hWant[idx(x, yn)] + hWant[idx(x, ys)]);
                }
            std::swap(hWant, hTmp);
        }
    }

    // Ten minutes of air. Thickness answers convergence and warmth; the wind
    // answers the slope of the thickness, turned by the Coriolis force and
    // slowed by the ground.
    void stepDynamics(double dt) {
        double dx0 = 2 * 3.14159265 * R_EARTH / W;
        double dy = 3.14159265 * R_EARTH / H;
#pragma omp parallel for
        for (int y = 1; y < H - 1; y++) {
            double cosl = std::max(std::cos((((y + 0.5) / (double)H) - 0.5) * 3.14159265), 0.05);
            double dx = dx0 * cosl;
            double cosN = std::max(std::cos(((y + 1.0) / (double)H - 0.5) * 3.14159265), 0.02);
            double cosS = std::max(std::cos(((y + 0.0) / (double)H - 0.5) * 3.14159265), 0.02);
            for (int x = 0; x < W; x++) {
                int i = idx(x, y);
                int xe = idx(wrapX(x + 1), y), xw = idx(wrapX(x - 1), y);
                int yn = idx(x, y + 1), ys = idx(x, y - 1);
                double f = 2 * OMEGA * std::sin(latRad[i]);
                // Momentum: down the slope of the thickness, turned by the
                // planet's spin, dragged by the surface.
                double dhx = (hP[xe] - hP[xw]) / (2 * dx);
                double dhy = (hP[yn] - hP[ys]) / (2 * dy);
                double lapU = (u[xe] + u[xw] - 2 * u[i]) / (dx * dx) +
                              (u[yn] + u[ys] - 2 * u[i]) / (dy * dy);
                double lapV = (v[xe] + v[xw] - 2 * v[i]) / (dx * dx) +
                              (v[yn] + v[ys] - 2 * v[i]) / (dy * dy);
                nu2[i] = u[i] + dt * (-GPRIME * dhx + f * v[i] - WIND_DRAG * u[i] + DYN_VISC * lapU);
                nv2[i] = v[i] + dt * (-GPRIME * dhy - f * u[i] - WIND_DRAG * v[i] + DYN_VISC * lapV);
                // Nothing on this planet blows at 200 m/s; a cap keeps a bad
                // gradient from taking the whole field with it.
                nu2[i] = std::clamp(nu2[i], -70.0, 70.0);
                nv2[i] = std::clamp(nv2[i], -70.0, 70.0);
                // Continuity: what the wind carries in, the column keeps --
                // with the meridians' convergence counted, or mass appears
                // from nowhere at high latitude.
                double hh = H_LAYER + hP[i];
                double fluxE = 0.5 * (u[i] + u[xe]) * 0.5 * (hh + H_LAYER + hP[xe]);
                double fluxW = 0.5 * (u[xw] + u[i]) * 0.5 * (H_LAYER + hP[xw] + hh);
                double fluxN = 0.5 * (v[i] + v[yn]) * 0.5 * (hh + H_LAYER + hP[yn]) * (cosN / cosl);
                double fluxS = 0.5 * (v[ys] + v[i]) * 0.5 * (H_LAYER + hP[ys] + hh) * (cosS / cosl);
                double conv = -((fluxE - fluxW) / (2 * dx) + (fluxN - fluxS) / (2 * dy));
                // Thickness relaxes towards the surface thermal target (a
                // heat low over warm ground), loses what diabatic ascent
                // carries out through the top, and receives everyone's
                // exported mass back as the global mean. The difference of
                // the last two is what digs the ITCZ trough and the storm
                // lows and leaves the subtropics high -- the overturning
                // cell's surface signature, which conv alone cannot make
                // because frictional inflow only ever FILLS a low.
                double want = hWant[i];
                nhP[i] = hP[i] + dt * (conv + (want - hP[i]) / THERM_TAU -
                                       W_EXPORT * (wTop[i] - wTopMean));
                nhP[i] = std::clamp(nhP[i], -0.5 * H_LAYER, 0.5 * H_LAYER);
            }
        }
        for (int x = 0; x < W; x++) {
            nu2[idx(x, 0)] = nv2[idx(x, 0)] = nu2[idx(x, H - 1)] = nv2[idx(x, H - 1)] = 0.0;
            nhP[idx(x, 0)] = nhP[idx(x, 1)];
            nhP[idx(x, H - 1)] = nhP[idx(x, H - 2)];
        }
        std::swap(u, nu2);
        std::swap(v, nv2);
        std::swap(hP, nhP);
        polarFilter(u, true);
        polarFilter(v, true);
        polarFilter(hP, true);
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
            // The SURFACE gets a sea-level reduction, because a mountain top
            // really is colder than the valley and mixing that away would
            // erase every highland. The AIR does not: neither layer gets
            // colder because the ground under it is higher. Adding the lapse
            // put a twenty-degree cliff in the air field at every coast with
            // mountains behind it -- invisible while the air only diffused,
            // and a large phantom heat source the moment the wind began to
            // advect it. (Tasl, the un-reduced alias this used to fill, is
            // gone; the layers are diffused and advected directly.)
            capArr[i] = std::max(capEff[i] > 0.0 ? capEff[i] : capAirOf(Tb[i]), 0.05);
        }

        // What rose out of the layer last hour comes back down as the mean,
        // so the export moves mass about without creating or destroying it.
        // The upper branch is one well-mixed pool: it receives each column's
        // detrained air at that column's free-troposphere temperature and
        // hands the same mass back everywhere, so its temperature is the
        // export-weighted mean of Tf (see GAP_BF).
        {
            double sum = 0, tsum = 0, wsum = 0;
            for (int y = 0; y < H; y++) {
                double cw = std::cos(((y + 0.5) / (double)H - 0.5) * 3.14159265);
                for (int x = 0; x < W; x++) {
                    double wt = std::max(wTop[idx(x, y)], 0.0) * cw;
                    sum += wt;
                    tsum += Tf[idx(x, y)] * wt;
                    wsum += cw;
                }
            }
            wTopMean = sum / wsum;
            tfPool = sum > 0 ? tsum / sum : 0.0;
        }
        // The layers' heat capacities: the moving layer is its own mass,
        // the free troposphere is the rest of the column.
        const double C_BL = RHO * CP_AIR * H_LAYER;
        const double C_FT = C_AIR - C_BL;
        // The air moves itself: six ten-minute steps of thickness and wind
        // inside this hour of radiation and water.
        thermalTarget();
        for (int k = 0; k < DYN_SUBSTEPS; k++) stepDynamics(DT / DYN_SUBSTEPS);
        // and once, now the hour is done, the anisotropy.
        polarFilter(u, false);
        polarFilter(v, false);
        polarFilter(hP, false);

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
                // W_OROG is the windward fraction -- only part of a cell's
                // flow actually climbs the slope. It was applied before the
                // rain rebuild moved orography in here, and dropped in the
                // move; with honest winds the term tripled, mountains rained
                // at 4 mm/day and the columns downwind were left dry.
                double oro = W_OROG *
                             (u[i] * (elev[idx(wrapX(x + 1), y)] - elev[idx(wrapX(x - 1), y)]) / (2 * dx) +
                              v[i] * (elev[idx(x, y + 1)] - elev[idx(x, y - 1)]) / (2 * dy));
                div[i] = wup + std::max(oro, 0.0) - std::max(-oro, 0.0) * 0.5;
                pOro[i] = std::max(oro, 0.0);
            }
        }
        // Divergence is a DERIVATIVE of a field that is only resolved down to
        // a few cells, so at the grid scale it is noise, and this one gets
        // multiplied by 1500 m and read as a vertical wind. Measured: 0.59 m/s
        // of ascent in the band from 68 to 76 degrees against 0.002 in the
        // tropics -- three orders of magnitude, where life varies by less than
        // one. That collapsed the saturation capacity to a fortieth, rained
        // the band dry to a hundredth of a millimetre, made it transparent to
        // longwave, and sat it at -65 with the air fifty degrees warmer.
        //
        // The wind that produced it is already smoothed to the scale the grid
        // can carry -- that is what polarFilter does for u, v and the height.
        // Its derivative has to be held to the same standard, or the model
        // reads its own truncation error as weather. One Laplacian pass takes
        // out the two-cell component a centred difference cannot represent;
        // the polar filter takes out what the crowding meridians add.
        {
            static std::vector<double> sm;
            sm.resize(W * H);
#pragma omp parallel for
            for (int y = 1; y < H - 1; y++)
                for (int x = 0; x < W; x++) {
                    int i = idx(x, y);
                    sm[i] = 0.5 * div[i] +
                            0.125 * (div[idx(wrapX(x + 1), y)] + div[idx(wrapX(x - 1), y)] +
                                     div[idx(x, y + 1)] + div[idx(x, y - 1)]);
                }
            for (int x = 0; x < W; x++) {
                sm[idx(x, 0)] = div[idx(x, 0)];
                sm[idx(x, H - 1)] = div[idx(x, H - 1)];
            }
            div.swap(sm);
            polarFilter(div, false);
            // And smoothing in space is only half of it, because the error
            // that mattered was in TIME. Uplift enters the rain through
            // max(div, 0) and dryness through max(-div, 0), and a rectifier
            // does not care that the two halves of a wave cancel: a gravity
            // wave rocking a cell up and down condenses nothing over its
            // period, but taking only the positive half of it reads as a
            // permanent updraught. Measured: 0.39 m/s of mean ascent in the
            // band from 68 to 80 degrees out of a wind field whose largest
            // seasonal-mean speed there is 6.4 m/s and whose meridional shear
            // accounts for 0.017. The rest was the wave, counted once per
            // hour and never allowed to come back down.
            //
            // Condensation integrates over the time a parcel takes to rise,
            // which is about a day. Anything faster than that is weather the
            // grid cannot resolve, and it averages out.
            for (int i = 0; i < W * H; i++)
                divSm[i] += (div[i] - divSm[i]) * (DT / UPLIFT_TAU);
        }

        // Moisture transport, done conservatively and in three passes.
        //
        // One pass cannot do it: a cell has four faces, and each on its own
        // may pass the CFL check while the four together export more water
        // than the cell contains. Whatever was clamped away then reappeared
        // at the neighbours, who had already been promised the full flux --
        // rain came to four and a half times the world's evaporation, and
        // that phantom water is what cooked the poles every time this model
        // was pushed. So: compute every face, find what each cell would lose,
        // scale ITS OWN faces down until it can afford them, and only then
        // apply. What one cell sends is what the next receives.
        {
            double dx0 = 2 * 3.14159265 * R_EARTH / W;
            double dy = 3.14159265 * R_EARTH / H;
#pragma omp parallel for
            for (int y = 0; y < H; y++) {
                double cosl = std::max(std::cos((((y + 0.5) / (double)H) - 0.5) * 3.14159265), 0.05);
                double dx = dx0 * cosl;
                for (int x = 0; x < W; x++) {
                    int i = idx(x, y), xe = idx(wrapX(x + 1), y);
                    int yn = idx(x, std::min(y + 1, H - 1));
                    double ue = 0.5 * (u[i] + u[xe]);
                    double vn = 0.5 * (v[i] + v[yn]);
                    // Advection carries what the air holds, upwind. The
                    // humidity rule belongs to DIFFUSION, which is an
                    // exchange of parcels; a wind blowing inland really does
                    // bring its whole load with it, and drops the excess as
                    // rain when it gets there. Throttling it by the colder
                    // side's capacity left 70% of all land desert -- nothing
                    // could reach an interior.
                    fluxE[i] = ue * (ue > 0 ? Wv[i] : Wv[xe]) / dx;
                    // A pole is a point, not a row. Rows 0 and H-1 are caps
                    // that exist so the interior has something to reference,
                    // and they are filled by copying their neighbour -- which
                    // makes them an INFINITE RESERVOIR if anything is allowed
                    // to draw on them: whatever the last real row takes is
                    // replenished from that same row's own value on the next
                    // line of code. Measured at the north cap: 45.3 mm/day of
                    // moisture flowing in through a face whose neighbour to
                    // the south was losing 0.577, so nobody was paying for
                    // it. It rained 22.7 mm/day, released 1267 W/m2 of latent
                    // heat, and held the polar air at -6 degC while 76
                    // degrees sat at -25 -- warm air, high capacity, and so
                    // more inflow still.
                    //
                    // The boundary condition a pole actually has is no flux
                    // through it, and that means the face BESIDE the cap, not
                    // just the one past it.
                    fluxN[i] = (y == 0 || y >= H - 2) ? 0.0
                                                      : vn * (vn > 0 ? Wv[i] : Wv[yn]) / dy;
                }
            }
#pragma omp parallel for
            for (int y = 1; y < H - 1; y++) {
                double cosl = std::max(std::cos((((y + 0.5) / (double)H) - 0.5) * 3.14159265), 0.05);
                double cosN = std::max(std::cos(((y + 1.0) / (double)H - 0.5) * 3.14159265), 0.02);
                double cosS = std::max(std::cos(((y + 0.0) / (double)H - 0.5) * 3.14159265), 0.02);
                for (int x = 0; x < W; x++) {
                    int i = idx(x, y), xw = idx(wrapX(x - 1), y), ys = idx(x, y - 1);
                    double out = std::max(fluxE[i], 0.0) + std::max(-fluxE[xw], 0.0) +
                                 (cosN / cosl) * std::max(fluxN[i], 0.0) +
                                 (cosS / cosl) * std::max(-fluxN[ys], 0.0);
                    capArr[i] = out * DT > 1e-12 ? std::min(1.0, 0.9 * Wv[i] / (out * DT)) : 1.0;
                }
            }
            // capArr now holds each cell's affordable share; scale its own
            // outgoing faces by it, then it is free to be capacity again.
#pragma omp parallel for
            for (int y = 1; y < H - 1; y++)
                for (int x = 0; x < W; x++) {
                    int i = idx(x, y), xe = idx(wrapX(x + 1), y), yn = idx(x, y + 1);
                    fluxE[i] *= fluxE[i] > 0 ? capArr[i] : capArr[xe];
                    fluxN[i] *= fluxN[i] > 0 ? capArr[i] : capArr[yn];
                }
#pragma omp parallel for
            for (int i = 0; i < W * H; i++)
                capArr[i] = std::max(capEff[i] > 0.0 ? capEff[i] : capAirOf(Tb[i]), 0.05);
        }

        // Thermodynamics + moisture, upwind advection + diffusion.
#pragma omp parallel for
        for (int y = 1; y < H - 1; y++) {
            double cosl = std::max(std::cos((((y + 0.5) / (double)H) - 0.5) * 3.14159265), 0.2);
            double dx = dx0 * cosl;
            // Meridians converge, and a flux per unit area does not. Cells at
            // 80 degrees hold a seventh of the area of cells at the equator, so
            // moving a kg per square metre out of a big cell and into a small
            // one CREATES water -- and heat, in the temperature diffusion. The
            // spherical divergence weights each face by its own length; these
            // are those weights, relative to this row.
            double cosN = std::max(std::cos(((y + 1.0) / (double)H - 0.5) * 3.14159265), 0.05);
            double cosS = std::max(std::cos(((y + 0.0) / (double)H - 0.5) * 3.14159265), 0.05);
            double fN = cosN / cosl, fS = cosS / cosl;
            // Moisture mixing, capped well below the old 0.2: at that rate two
            // fifths of a cell's water crossed into its neighbours every hour,
            // which is a pipeline rather than a diffusion, and it flooded the
            // poles with water no polar air could hold.
            double kx = std::min(K_DIFF * DT / (dx * dx), 0.09), ky = std::min(K_DIFF * DT / (dy * dy), 0.09);
            double ktx = std::min(KT_DIFF * DT / (dx * dx), 0.22), kty = std::min(KT_DIFF * DT / (dy * dy), 0.22);
            for (int x = 0; x < W; x++) {
                int i = idx(x, y);
                int xe = idx(wrapX(x + 1), y), xw = idx(wrapX(x - 1), y);
                // The cap rows are a copy of this one, and their air
                // temperature is never integrated at all -- it sits wherever
                // the allocator left it, near zero, which the row below then
                // diffuses towards. Referring to the cell itself instead
                // gives a zero gradient, which is what no flux through the
                // pole means for everything, not only the water.
                int yn = (y + 1 >= H - 1) ? i : idx(x, y + 1);
                int ys = (y - 1 <= 0) ? i : idx(x, y - 1);
                // solar
                double lat = latRad[i];
                double ha = 2 * 3.14159265 * (hour / 24.0 + (x + 0.5) / (double)W) + 3.14159265;
                double cosz = std::sin(lat) * std::sin(dec) + std::cos(lat) * std::cos(dec) * std::cos(ha);
                double white = std::clamp((SNOW_NONE_C - T[i]) / (SNOW_NONE_C - SNOW_FULL_C),
                                          0.0, 1.0);
                double alb = albedo[i] + white * ((water[i] ? ALBEDO_SEAICE : ALBEDO_SNOW) -
                                                  albedo[i]);
                // What the cloud overhead is doing, both ways. It was
                // drawn on the map and nowhere else until now.
                double cf = cloudF[i];
                // Sunlight, in the order it actually meets things: cloud
                // tops reflect, the column absorbs its share of what gets
                // through, and the ground takes what is left.
                double inc = SOLAR * std::max(cosz, 0.0) * (1.0 - CLOUD_ALB * cf);
                double swAir = inc * SW_ATM;
                double sw = (inc - swAir) * (1.0 - alb);
                // Longwave, both ways. The air holds EMISS of what the ground
                // sends up and radiates that much again from each of its two
                // faces: half to space, half back down. The half coming down
                // is the greenhouse, and it is what the old budget had no way
                // to express.
                double Tk = T[i] + 273.15, Tbk = Tb[i] + 273.15, Tfk = Tf[i] + 273.15;
                double lwUp = SIGMA * Tk * Tk * Tk * Tk;
                // Two grey layers (see BL_LAPSE, GAP_BF). The vapour's
                // optical depth is the calibrated column curve, split between
                // the layers in proportion to the water each holds, so the
                // column's total transmission is exactly what the anchors
                // fixed. The cloud closes its share of the window the same
                // way: its base is in the boundary layer and its top in the
                // free troposphere, so half its depth goes to each -- and
                // that is what makes a cloudy night warm and a cloud top
                // cold, without a special case for either.
                double tauW = tauOf(Wv[i]);
                double tauC = -std::log(1.0 - CLOUD_LW * cf);
                double emB = 1.0 - std::exp(-(tauW * BL_WV_SHARE + 0.5 * tauC));
                double emF = 1.0 - std::exp(-(tauW * (1.0 - BL_WV_SHARE) + 0.5 * tauC));
                double em = 1.0 - (1.0 - emB) * (1.0 - emF); // the column, for the probe
                // Each layer radiates from its two faces at those faces'
                // own temperatures (see FT_FACE): the boundary layer down
                // from its base and up from its top, the free troposphere
                // down from its base and up from its emitting level.
                auto face = [](double em, double tk) { return em * SIGMA * tk * tk * tk * tk; };
                double eBdn = face(emB, Tbk + BL_LAPSE), eBup = face(emB, Tbk - BL_LAPSE);
                double eFdn = face(emF, Tfk + FT_FACE), eFup = face(emF, Tfk);
                // Upward through the boundary layer, then through the free
                // troposphere; downward from space (nothing) to the ground.
                // Down is warm and up is cold because the faces that look
                // down ARE the warm ones, which is what the one-layer
                // scheme's fixed offset was standing in for.
                double up1 = (1.0 - emB) * lwUp + eBup;   // leaving the BL top
                double olr = (1.0 - emF) * up1 + eFup;    // leaving the planet
                double lwDown = eBdn + (1.0 - emB) * eFdn; // arriving at the ground
                double heatHere = (water[i] && T[i] < -1.0) ? C_SEAICE : heatC[i];
                // The exchange coefficient, from the wind that is actually
                // blowing here. Surface wind is about seven tenths of the
                // layer's, and never less than the stirring convection does
                // on its own.
                double spd = std::sqrt(0.49 * (u[i] * u[i] + v[i] * v[i]) + U_GUST * U_GUST);
                double kExch = RHO_CP * (water[i] ? CH_SEA : CH_LAND) * spd;
                double lapseGap = T[i] - Tb[i] - BL_LAPSE;
                double stab = lapseGap > 0 ? 1.0 : 1.0 / (1.0 + STAB_B * (-lapseGap));
                double sens = kExch * stab * lapseGap;
                // Evaporation, priced: what the air can still hold, what the
                // ground has to give, and what the sun can pay for.
                // Two different capacities, and confusing them was the bug.
                // What the air can HOLD is set by the air's own temperature;
                // what the surface OFFERS is the saturation humidity of the
                // skin, which is why a warm sea steams into cool air.
                double cap = capAirOf(Tb[i]);
                double capSkin = capOf(T[i]);
                double supply = water[i] ? 1.0 : std::clamp(soil[i] / SOIL_REF_MM, 0.0, 1.0);
                // Same turbulence, same coefficient, humidity deficit in
                // place of temperature difference. The deficit is read off
                // the column through the depth Earth actually has.
                double rhCol = std::clamp(Wv[i] / std::max(cap, 0.05), 0.0, 1.0);
                double qAir = (1.0 - BL_DRYNESS * (1.0 - rhCol)) * cap;
                double dq = std::max(capSkin - qAir, 0.0) / Q_SCALE;
                double evap = RHO * (water[i] ? CH_SEA : CH_LAND) * spd * dq * supply * DT;
                pSpd[i] = spd;
                pCapSkin[i] = capSkin;
                pSupply[i] = supply;
                // What the sun pays over a whole day, not what it pays at noon:
                // capping against the instantaneous figure lets the daylight
                // hours evaporate three or four times a day's worth of water.
                double h0 = std::acos(std::clamp(-std::tan(lat) * std::tan(dec), -1.0, 1.0));
                double swDay = SOLAR / 3.14159265 *
                               (h0 * std::sin(lat) * std::sin(dec) +
                                std::cos(lat) * std::cos(dec) * std::sin(h0)) *
                               (1.0 - alb) * (1.0 - CLOUD_ALB * cf) * (1.0 - SW_ATM);
                double afford = std::max(swDay, 0.0) +
                                (water[i] ? STORED_FLUX_WATER : STORED_FLUX_LAND);
                double lFlux = evap * LATENT_J_PER_KG / DT;
                pAfford[i] = lFlux > afford ? 1.0 : 0.0;
                if (lFlux > afford) {
                    evap *= afford / lFlux;
                    lFlux = afford;
                }
                // An explicit step cannot move more than a cell's worth of
                // anything per step, and diffusion and advection spend from
                // the same budget; measure what the wind spends and let the
                // diffusion have what is left.
                double uMax = 0.8 * dx / DT, vMax = 0.8 * dy / DT;
                double ua = std::clamp(u[i], -uMax, uMax), va = std::clamp(v[i], -vMax, vMax);
                double kxa = ktx, kya = kty;
                {
                    double cour = std::fabs(ua) * DT / dx + std::fabs(va) * DT / dy;
                    double want = 2 * kxa + kya * (fN + fS);
                    double room = std::max(0.0, 0.8 - cour);
                    if (want > room) {
                        double sc = want > 0 ? room / want : 0.0;
                        kxa *= sc;
                        kya *= sc;
                    }
                }
                double difT = kxa * (Tb[xe] + Tb[xw] - 2 * Tb[i]) +
                              kya * (fN * (Tb[yn] - Tb[i]) + fS * (Tb[ys] - Tb[i]));
                // The free troposphere has no wind of its own here, so the
                // eddies are all it gets sideways -- at the full coefficient,
                // since it spends nothing on advection.
                double difTf = ktx * (Tf[xe] + Tf[xw] - 2 * Tf[i]) +
                               kty * (fN * (Tf[yn] - Tf[i]) + fS * (Tf[ys] - Tf[i]));
                // The boundary layer's heat, carried upwind by its own wind
                // at full strength. The layer owns its temperature and its
                // heat capacity now, so the moving air carries exactly the
                // heat it holds. (This used to be the whole column's
                // temperature scaled by the layer's share of the column mass
                // -- the same physics in disguise, and before that the
                // column's temperature at full strength, which froze the
                // planet: a 1200 m wind handed the heat of a 10 km column.)
                double advT = -(ua > 0 ? ua * (Tb[i] - Tb[xw]) : ua * (Tb[xe] - Tb[i])) / dx -
                              (va > 0 ? va * (Tb[i] - Tb[ys]) : va * (Tb[yn] - Tb[i])) / dy;
                advT *= DT;
                // Under ice, the sea below conducts heat up to the surface.
                double cond = (water[i] && T[i] < SEA_FREEZE)
                                  ? K_ICE_COND * (SEA_FREEZE - T[i])
                                  : 0.0;
                double dT = (sw - lwUp + lwDown - sens - lFlux + cond) / heatHere * DT;
                if (water[i] && T[i] > -2.0 && T[i] < 2.0) dT *= MELT_DAMP;
                nT[i] = std::clamp(T[i] + dT, -90.0, 65.0);
                // PROBE: the tropical-ocean column, term by term. OLR is
                // what escapes the top: the window through the greenhouse
                // plus the air's own upward face.
                if (recordBudget && water[i] && std::fabs(latRad[i]) < 0.2618) {
                    double* b = &budRow[14 * y];
                    b[0] += sw;    b[1] += swAir;  b[2] += lwUp;
                    b[3] += lwDown; b[4] += sens;  b[5] += lFlux;
                    b[6] += olr;
                    b[7] += T[i];  b[8] += Tb[i];  b[9] += Wv[i];
                    b[10] += em;   b[11] += cf;    b[12] += Tf[i];
                    b[13] += 1.0;
                }
                // Two layers, two budgets (see GAP_BF).
                // The boundary layer: its share of the sunlight; what it
                // absorbs of the ground's face and of the free troposphere's
                // lower face, less its own two faces; the sensible heat the
                // ground gives it; its own wind and the eddies; and the
                // subsiding air the upper branch hands back, arriving
                // dry-adiabatically warmed by the gap.
                const double xch = RHO * CP_AIR * W_EXPORT;
                double swB = swAir * BL_WV_SHARE, swF = swAir - swB;
                nTb[i] = std::clamp(Tb[i] +
                                        (swB + emB * (lwUp + eFdn) - eBup - eBdn + sens) / C_BL * DT +
                                        difT + advT +
                                        xch * std::max(wTopMean, 0.0) * (Tf[i] + GAP_BF - Tb[i]) /
                                            C_BL * DT,
                                    -95.0, 70.0);
                // The free troposphere: the rest of the sunlight; what it
                // absorbs of everything coming up through the boundary
                // layer, less its two faces; the latent heat of every rain,
                // which condenses aloft; the eddies; the detrained air
                // arriving from below, cooled by the gap; and the pool's air
                // passing through on its way down.
                double condense = rainStep[i] * LATENT_J_PER_KG / C_FT;
                nTf[i] = std::clamp(Tf[i] +
                                        (swF + emF * up1 - eFup - eFdn) / C_FT * DT +
                                        condense + difTf +
                                        xch * (std::max(wTop[i], 0.0) * (Tb[i] - GAP_BF - Tf[i]) +
                                               std::max(wTopMean, 0.0) * (tfPool - Tf[i])) /
                                            C_FT * DT,
                                    -95.0, 70.0);
                if (i == probe) { // one cell only: no write contention
                    pSw += sw / heatHere * DT;
                    pOlr -= (lwUp - lwDown) / heatHere * DT;
                    pDif += difT;
                }
                // How fast the air here is rising, from every cause there is.
                double gtx = (Tsl[xe] - Tsl[xw]) / (2 * dx), gty = (Tsl[yn] - Tsl[ys]) / (2 * dy);
                // (Orography is already in div[], from the block above.)
                // Convective: the heat the surface actually gives the air,
                // both ways it gives it. Latent is the larger by an order of
                // magnitude over warm water, and it is the half that makes a
                // thunderhead; sens alone left the ITCZ becalmed.
                // Gated by moist buoyancy (see CONV_BAR): the flux is the
                // fuel, and it only becomes deep convection where the skin
                // parcel's moist static energy clears the column's.
                double hSfc = CP_AIR * T[i] + LATENT_J_PER_KG * capSkin / Q_SCALE;
                double hEnv = CP_AIR * Tf[i] + CONV_BAR +
                              LATENT_J_PER_KG * CONV_QSAT_LIFT * capOf(Tf[i]) / Q_SCALE;
                double buoy = std::clamp((hSfc - hEnv) / CONV_RAMP, 0.0, 1.0);
                double wConv = W_CONV * std::max(sens + lFlux, 0.0) * buoy;
                // Large-scale ascent where the flow converges, and frontal
                // lifting where warm air meets cold.
                // Signed: convergence lifts, divergence sinks, and the
                // subtropical deserts are the sinking half of the Hadley
                // cell. Taking only the positive part is what made the
                // rectifier that had to be dealt with above.
                double wDiv = W_DIVERGE * divSm[i];
                double wFront = W_FRONT * std::sqrt(gtx * gtx + gty * gty);
                double wUp = wConv + wDiv + wFront;
                // The diabatic part detrains into the upper branch (see
                // W_EXPORT); read next hour by the dynamics.
                wTop[i] = wConv + wFront;
                pConv[i] = wConv;
                pDiv[i] = wDiv;
                pFront[i] = wFront;
                pOrog[i] = pOro[i];
                // Ascent cools the air and takes its capacity down with it;
                // descent warms it and gives capacity back. What is left over
                // is what falls.
                // How far the air is actually displaced, and how much
                // colder that leaves it.
                //
                // Ascent lowers the ceiling and what is above it condenses:
                // that is real, and it is where rain comes from. Descent is
                // NOT the mirror image, and treating it as one was a faulty
                // assumption with a large consequence. A subsiding column was
                // granted up to three and a half times the water its own
                // temperature can hold, and it used the room: the global
                // column climbed to 71 mm against life's 25, with a residence
                // time of 47 days against 9, because water arriving in a
                // descent zone had no ceiling to rain against.
                //
                // Real subsiding air is dry, and it is dry for a reason this
                // model states backwards. It is not that the air has room to
                // spare -- it is that the air CAME FROM somewhere cold and
                // high, having already rained its water out on the way up.
                // The dryness belongs in the moisture budget, as an absence
                // of water; it does not belong in the ceiling, as permission
                // to hold more.
                //
                // So the ceiling is saturation at the air's own temperature,
                // and only ascent may lower it.
                double dz = std::clamp(wUp * UPLIFT_TAU, -H_LIFT_MAX, H_LIFT_MAX);
                double capLift =
                    std::min(cap, cap * std::exp(-LAPSE_MOIST * dz / CAP_SCALE));
                capEff[i] = std::max(capLift, 0.05);
                double rain = std::max(Wv[i] - RAIN_FRAC * capLift, 0.0) * RAIN_RATE;
                double fe = fluxE[i], fw = fluxE[xw], fn = fluxN[i], fs = fluxN[ys];
                // Flux form with per-face CFL limiting still lets four faces
                // between them export more than the cell contains, and the
                // clamp below then invents the shortfall. Measured: rain came
                // to 4.6x evaporation, and the whole difference was made at
                // that clamp -- water conjured, rained out, and (once latent
                // heat was coupled) used to cook the poles to +50 degC.
                double advW = ADV_EFF * (fe - fw + fN * fn - fS * fs);
                advZ[i] += -ADV_EFF * (fe - fw) * DT;
                advM[i] += -ADV_EFF * (fN * fn - fS * fs) * DT;
                // (An export limiter here was a water source: capping what a
                // cell gives up does not stop its neighbours taking the full
                // flux, so every capped cell minted the difference. Measured at
                // 20 mm a day of phantom water against 2.8 of evaporation. The
                // shortfall is taken out of the rain below instead, which
                // conserves.)
                // Eddies exchange air, and air carries its HUMIDITY, not an
                // absolute load: a parcel moving to a colder place arrives at
                // saturation and drops the rest on the way. Diffusing the
                // absolute column instead delivered the tropics' whole load to
                // the pole, and one constant then had to choose between
                // feeding the mid-latitudes and flooding the ice -- 10.9 mm a
                // day of polar rain at the setting where everything else was
                // right. What the colder side cannot hold never arrives.
                auto qFlux = [&](int j, double k) {
                    double capF = std::min(capArr[i], capArr[j]);
                    return k * (Wv[j] / capArr[j] - Wv[i] / capArr[i]) * capF;
                };
                double difW = qFlux(xe, kx) + qFlux(xw, kx) + fN * qFlux(yn, ky) +
                              fS * qFlux(ys, ky);
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
                // Air cannot hold more than it can hold. The rain term above
                // is a sub-grid onset -- part of a cell saturates before the
                // whole of it does -- and it takes a fraction of the excess
                // per hour, which is right for the approach to saturation and
                // wrong past it. Without a ceiling the model reported a mean
                // relative humidity of 112%, which is not a calibration error
                // but water sitting in air that physically cannot contain it,
                // radiating like a greenhouse that is not there.
                if (raw > capLift) {
                    rain += raw - capLift;
                    raw = capLift;
                }
                // Cloud is what condensed: the saturated share of the cell,
                // reckoned against the capacity the ascent has left it. This
                // is read back at the top of the next hour, and is now the
                // only thing standing between the sun and the ground.
                cloudF[i] = cloudOf(raw / std::max(capLift, 0.05));
                evapAcc[i] += evap;
                advAcc[i] += -advW * DT;
                difAcc[i] += difW;
                wvAcc[i] += Wv[i];
                madeAcc[i] += 0; // (kept for the water probe)
                rainAcc[i] += 0;
                rainAcc[i] += rain;
                madeAcc[i] += std::clamp(raw, 0.0, 90.0) - raw;
                nW[i] = std::clamp(raw, 0.0, 90.0);
                if (!water[i]) soil[i] = std::clamp(soil[i] + rain - evap, 0.0, SOIL_CAP_MM);
                rainStep[i] = rain;

            }
        }
        // polar rows: copy neighbours
        for (int x = 0; x < W; x++) {
            nT[idx(x, 0)] = nT[idx(x, 1)];
            nT[idx(x, H - 1)] = nT[idx(x, H - 2)];
            nW[idx(x, 0)] = nW[idx(x, 1)];
            nW[idx(x, H - 1)] = nW[idx(x, H - 2)];
            // The air too: these were never written by the loop above, so the
            // caps carried a stale temperature for the whole run.
            nTb[idx(x, 0)] = nTb[idx(x, 1)];
            nTb[idx(x, H - 1)] = nTb[idx(x, H - 2)];
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
        std::swap(Tb, nTb);
        std::swap(Tf, nTf);
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
    // running totals, so each sample sees the interval and not the epoch
    std::vector<double> lastE(W * H, 0.0), lastA(W * H, 0.0), lastD(W * H, 0.0);
    std::vector<double> lastZ(W * H, 0.0), lastM(W * H, 0.0);
    bool statSeeded = false;
    std::vector<double> cnt(SEASONS, 0.0);
    int totalDays = SPINUP_DAYS + STAT_YEARS * 365;
    for (int day = 0; day < totalDays; day++) {
        int doy = day % 365;
        int season = Climatology::seasonOfDay(doy);
        bool stat = day >= SPINUP_DAYS;
        m.recordBudget = stat;
        // These probes read a RUNNING TOTAL and bank the difference since the
        // last sample. The totals start at day zero, the sampling starts after
        // the spin-up, and the last* baselines started at zero -- so the first
        // sample banked the entire spin-up in one lump and every flux built on
        // them came out high, by more the longer the spin-up ran. Measured:
        // evaporation read 2.66 mm/day at a one-year spin-up and 4.28 at three,
        // against a true 1.70, which looked exactly like a third of the world's
        // water evaporating and never falling. Seed the baselines when the
        // sampling starts, so the first difference is a difference.
        if (stat && !statSeeded) {
            lastE = m.evapAcc; lastA = m.advAcc; lastD = m.difAcc;
            lastZ = m.advZ;    lastM = m.advM;
            statSeeded = true;
        }
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
                // Cloud is not humidity: dry air has none, and cover comes on
                // over the last third of the way to saturation.
                {
                    double rh = m.Wv[i] / std::max(m.capEff[i], 0.05);
                    c.cloud[si] += (float)cloudOf(rh);
                    c.wv[si] += (float)m.Wv[i];
                    c.rh[si] += (float)rh;
                    c.press[si] += (float)m.hP[i];
                    c.airT[si] += (float)m.Tb[i];
                    c.airTf[si] += (float)m.Tf[i];
                    c.upConv[si] += (float)m.pConv[i];
                    c.upDiv[si] += (float)m.pDiv[i];
                    c.upFront[si] += (float)m.pFront[i];
                    c.upOrog[si] += (float)m.pOrog[i];
                    c.capX[si] += (float)m.capEff[i];
                    c.evapF[si] += (float)(m.evapAcc[i] - lastE[i]);
                    c.advF[si] += (float)(m.advAcc[i] - lastA[i]);
                    c.difF[si] += (float)(m.difAcc[i] - lastD[i]);
                    c.latF[si] += (float)(m.rainStep[i] * LATENT_J_PER_KG / DT);
                    c.advZF[si] += (float)(m.advZ[i] - lastZ[i]);
                    c.advMF[si] += (float)(m.advM[i] - lastM[i]);
                    c.spdF[si] += (float)m.pSpd[i];
                    c.capSkinF[si] += (float)m.pCapSkin[i];
                    c.supplyF[si] += (float)m.pSupply[i];
                    c.affordF[si] += (float)m.pAfford[i];
                    lastZ[i] = m.advZ[i];
                    lastM[i] = m.advM[i];
                    lastE[i] = m.evapAcc[i];
                    lastA[i] = m.advAcc[i];
                    lastD[i] = m.difAcc[i];
                }
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
    {
        double e = 0, r = 0, mk = 0, wsum = 0, wv = 0, wnd = 0, rh = 0;
        for (int y = 0; y < H; y++) {
            double wgt = std::cos(((y + 0.5) / (double)H - 0.5) * 3.14159265);
            for (int x = 0; x < W; x++) {
                int i = y * W + x;
                e += m.evapAcc[i] * wgt;
                r += m.rainAcc[i] * wgt;
                mk += m.madeAcc[i] * wgt;
                wv += m.wvAcc[i] * wgt;
                wnd += std::sqrt(m.u[i] * m.u[i] + m.v[i] * m.v[i]) * wgt;
                rh += (m.Wv[i] / std::max(m.capEff[i], 0.05)) * wgt;
                wsum += wgt;
            }
        }
        double hours = 0;
        for (int s2 = 0; s2 < SEASONS; s2++) hours += cnt[s2] * 24.0;
        // These accumulate over EVERY hour of the run, spin-up included,
        // and were being divided by the sampled hours alone -- so they read
        // four times high at a six-year spin-up and 1.5 times at one year,
        // which is a diagnostic that changes its answer with the length of
        // the run.
        double allHours = (double)(SPINUP_DAYS + STAT_YEARS * 365) * 24.0;
        c.dbgEvap = e / wsum / allHours * 24.0;
        c.dbgRain = r / wsum / allHours * 24.0;
        c.dbgClamp = mk / wsum / allHours * 24.0;
        c.dbgWv = wv / wsum / allHours;
        c.dbgWind = wnd / wsum;   // instantaneous, at the end of the run
        c.dbgRH = rh / wsum;
    }
    {
        double s[14] = {};
        for (int y = 0; y < H; y++)
            for (int k = 0; k < 14; k++) s[k] += m.budRow[14 * y + k];
        double n = std::max(s[13], 1.0);
        for (int k = 0; k < 13; k++) c.tropBud[k] = s[k] / n;
        c.tropBud[13] = s[13];
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
            c.wv[si] /= (float)hours;
            c.rh[si] /= (float)hours;
            c.press[si] /= (float)hours;
            c.airT[si] /= (float)hours;
            c.airTf[si] /= (float)hours;
            c.upConv[si] /= (float)hours;
            c.upDiv[si] /= (float)hours;
            c.upFront[si] /= (float)hours;
            c.upOrog[si] /= (float)hours;
            c.capX[si] /= (float)hours;
            c.evapF[si] *= (float)(24.0 / hours);
            c.advF[si] *= (float)(24.0 / hours);
            c.difF[si] *= (float)(24.0 / hours);
            c.advZF[si] *= (float)(24.0 / hours);
            c.advMF[si] *= (float)(24.0 / hours);
            c.spdF[si] /= (float)hours;
            c.capSkinF[si] /= (float)hours;
            c.supplyF[si] /= (float)hours;
            c.affordF[si] /= (float)hours;
            c.latF[si] /= (float)hours;
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
    float temp, moist, tCold, tWarm, swamp;
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
        d.tWarm = d.temp + 4.0f;
        d.swamp = 0.0f;
        return d;
    }
    terrain::V3 n = climFuzz(unitAt(latRad, lonRad));
    float coarseE = annualAt(c.elev4(), n);
    float lapse = 6.5f * (std::max(hLocal, 0.0f) - coarseE) / 1000.0f;
    float annT = 0, rain = 0, tMin = 1e9f, tMax = -1e9f;
    for (int se = 0; se < SEASONS; se++) {
        float t = bilinearAt(c.meanT, se, n);
        annT += t / SEASONS;
        tMin = std::min(tMin, t);
        tMax = std::max(tMax, t);
        rain += bilinearAt(c.rainMmDay, se, n) / SEASONS;
    }
    d.temp = annT - lapse;
    d.tCold = tMin - lapse;
    d.tWarm = tMax - lapse;
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
