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
#include "dynamics2.h"
#include "qg2.h"
#include "qg2geo.h"
#include "water2geo.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <algorithm>
#include <vector>

namespace atmosphere {

constexpr int W = 192, H = 96;   // ~208 km cells at the equator
constexpr int SEASONS = 4;       // DJF, MAM, JJA, SON

constexpr double DT = 3600.0;            // s, one step per sim hour
inline int SPINUP_DAYS = 365;         // discarded first year
inline int STAT_YEARS = 2;               // averaged years after spin-up
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
// TWO BANDS. Grey layers over-cool the boundary layer: a layer that emits
// in the window as freely as in the bands sends a quarter of its upward
// face straight past the free troposphere to space, and the two-layer
// scheme measured that at 60-80 W/m2 of net loss off the tropical boundary
// layer, against Earth's 30 -- paid for by a sea 7 K warmer than the air
// giving two and a half times Earth's sensible heat. In life the water
// vapour spectrum has a window, 8 to 13 microns, holding about 28% of the
// Planck emission at these temperatures. There the vapour is nearly
// transparent (only the continuum, which goes as the square of the
// humidity and so lives near the surface) and cloud is opaque; in the
// bands the vapour is opaque within a few millimetres and the layers
// exchange with what is next to them. The surface loses to space through
// the window; the boundary layer does not.
//
// The same two anchors fix both curves. Clear-sky emissivity over the
// near-surface blackbody is 0.6 at 2 mm and 0.87 at 25 mm, and the total
// is (1-w)*eBand + w*eWin. Bands: 0.82 at 2 mm, 0.98 at 25 -- the column
// curve above scaled by BAND_K, same exponent. Window: 0.05 at 2 mm, 0.57
// at 25 -- a continuum optical depth of 0.84 at 25 mm going as W^1.1. Both
// reproduce the anchors to a hundredth; neither is a dial.
constexpr double WINDOW_FRAC = 0.28;  // of the Planck emission in the window
constexpr double BAND_K = 1.85;       // band optical depth over the column curve
constexpr double WIN_TAU25 = 0.84;    // continuum optical depth at 25 mm
constexpr double WIN_EXP = 1.1;       // and its power in W
// The continuum goes as the square of the humidity, so its scale height
// is half the vapour's and the lowest 1200 m hold 1 - exp(-1200/1225).
constexpr double WIN_BL_SHARE = 0.62; // of the continuum in the boundary layer
inline double tauBandOf(double wv) { return BAND_K * tauOf(wv); }
inline double tauWinOf(double wv) { return WIN_TAU25 * std::pow(std::max(wv, 1e-3) / 25.0, WIN_EXP); }
// The whole column's clear-sky emissivity, for diagnostics.
inline double emissOf(double wv) {
    return (1.0 - WINDOW_FRAC) * (1.0 - std::exp(-tauBandOf(wv))) +
           WINDOW_FRAC * (1.0 - std::exp(-tauWinOf(wv)));
}
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
//
// Physically this is the ENVIRONMENT's lapse over the same five kilometres
// the dry parcel crosses in GAP_BF, about 7 K/km against the parcel's 9.8,
// which says 35: the base would then meet the boundary layer's top in the
// tropics and stand above it over a winter pole, which is the inversion.
// Measured, 35 did not do what that argument promised. The tropical
// boundary layer warmed 1.2 K (the free troposphere cooled 2.4 to pay for
// its warmer face, and the rest went into wind and evaporation), while the
// poles and the mid-latitude winters warmed 3 to 7 K and the calibration
// error went from 10 to 19. The boundary layer's 7 K deficit under the sea
// is not this constant: it is the grey approximation, which has the layer
// emit in the window as freely as in the bands, so a quarter of its upward
// face goes straight to space past the free troposphere. That needs a
// window band, not a warmer face. 27 stays until then.
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
// SEA ICE IS A MASS. It used to be four rules keyed on the surface
// temperature of the moment -- a heat-capacity switch at -1 degC, a fixed
// conduction below -1.8, a damping of the temperature change within two
// degrees of zero standing in for latent heat, and an albedo ramp -- and a
// cell had no memory of having been frozen. Two holes followed. The latent
// buffer sat on the slab between -1 and +2, worth seven metres of ice, so
// the winter sea at 56-64N sat at +8 and +3 and never reached the skin
// regime at all. And the albedo followed the thermometer, so a polar sea
// held at 0 degC by melting -- which on Earth is under ice at albedo 0.6 --
// was 80% open water taking 147 W/m2 of summer sun into a 25 m slab and
// paying it back all winter: 60N winter 20 K warm, with the radiation and
// the transport both Earth's.
//
// Now: a thickness of ice per sea cell. Net cooling of open water below
// freezing makes ice from the deficit. Under ice the surface is the ice
// skin (C_SEAICE), heat conducts up from water held at SEA_FREEZE through
// the thickness, and what it conducts it freezes at the bottom. Net heating
// of the skin above the melting point melts from the top before any water
// warms; when the last of it goes the water resumes at freezing. Albedo
// follows cover, not temperature.
constexpr double L_ICE = 3.06e8;        // J per m3 of ice: 334 kJ/kg at 917 kg/m3
constexpr double ICE_K = 2.2;           // W/m/K, ice and its snow together (see SEA_FREEZE)
constexpr double ICE_FULL_COVER = 0.3;  // m: thin ice is already white
constexpr double ICE_MIN = 0.005;       // m: below this it is gone
constexpr double ICE_MAX = 20.0;        // m: nothing here is an ice shelf
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
// (K_ICE_COND, 0.88 W/m2/K, was this conductivity over a fixed 2.5 m; the
// thickness is prognostic now and the conductivity is ICE_K above.)
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
// And the exponential that replaced it, 1 - exp(-(rh/0.58)^2), read a
// layer at 80 percent saturation as 85 percent cloud. That was tolerable
// while the column ran at 60 percent; with the water in two layers the
// lower layer sits at 80-90 percent like Earth's marine boundary layer,
// which carries 40-60 percent cloud, and the rule turned the tropics into
// a 0.95 overcast: 35 W/m2 of sunlight reflected, the column 6-8 K cold,
// and colder air more saturated still. Sundqvist's relation (no cloud
// below a critical humidity of 0.7, full cover at saturation, the square
// root between) fixed the tropical column to the watt and cleared the
// extratropics to 10-20 percent cloud against 70 observed, +13 W/m2 on
// the planet and 35-degree summers. Earth's cloud depends on saturation
// more weakly than either form: 50-60 percent at 80 percent saturation,
// 70 in the storm tracks at 70. The exponential stays, with its scale
// re-derived for the two-layer saturation so that 85 percent saturation
// gives 60 percent cloud; the fronts' cloud and the anvils are added
// where the water model makes them (see WATER2's cloud).
constexpr double RH_CLOUD = 0.89;
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
inline double W_CONV = 3.5e-5;     // m/s per W/m2 of turbulent heat into the air (5e-5 rained 9.7 mm/day on the Earth template's ITCZ; Earth peaks near 6)
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
inline double FRONT_SIDE_K = 4.0; // K above the neighbours for full lift, below for none
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
constexpr double DIV_CAP_SCALE = 0.05;          // m/s of uplift for a ~46% capacity swing
// Over land, moisture rains out progressively along its path (precipitation
// is not withheld until a convergence line): an e-folding of ~3 days, i.e.
// ~1300 km at typical winds. This is what makes coasts wetter than deep
// continental interiors.
// (LAND_RAINOUT_TAU, the 3-day e-folding, is no longer referenced anywhere.)
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
inline double KT_BL_SHARE = 0.0;             // of KT_DIFF the boundary layer keeps when the mesh weather carries its heat (see QG2GEO)

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


// PRESCRIBED CLIMATE (Design/Weather.md, decision of 2026-09-04). The
// prognostic atmosphere below is parked: without baroclinic eddies it
// cannot make a cold winter continent and westerlies at the same time, and
// eddies need a dynamic second layer and a ten-minute step. What the game
// needs is a climate realistic enough that any continent layout gets its
// deserts, forests, tundra and monsoons in the right places. So the surface
// temperature and the surface wind are PAINTED from the same Earth targets
// the calibration aimed at -- zonal curves for land and sea, a seasonal
// swing scaled by continentality and lagged, the lapse rate on elevation,
// a diurnal swing, and the belt circulation bent by the land-sea thermal
// contrast -- and everything downstream is still DERIVED by the machinery
// that worked: moisture transport on the wind, evaporation, uplift by
// convection, convergence, fronts and slopes, rain, snow, humidity and
// cloud. Rain shadows, interior deserts and the monsoon's seasonal march
// are consequences of transport on a bounded temperature field, not of
// painting. Set PRESCRIBED false to run the physics instead.
inline bool PRESCRIBED = true;
// THE HOUR IN THE GAME (work order 02): with the climate painted, what
// survives an hour is the evaporation, the soil, the humidity mirror, the
// cloud and the rule rain. The radiation, the surface and ice balance, the
// layer budgets and the uplift by cause are overwritten by the next hour's
// painting and exist only to fill the sweep's probes (uplift by cause, the
// tropical column, the zonal budget, the energy line). The sweep sets
// PROBES and runs every stage, so its figures are the full column's; the
// game leaves it off and runs only what survives (see Model::step).
inline bool PROBES = false;
// THE TWO-LEVEL DYNAMICS (dynamics2.h): when on, the wind the water rides
// on is made by a two-level primitive-equation atmosphere whose levels
// relax toward the painted temperatures, instead of by the belt tables.
// Temperatures stay painted; only the circulation is computed. Judged on
// its wind and pressure fields first, before it is trusted with the rain.
inline bool DYN2 = false;
// THE TWO-LAYER QUASI-GEOSTROPHIC WEATHER (qg2.h): when on, the wind the
// water rides on poleward of the tropics is the lower layer of a two-layer
// QG model relaxed toward the painted climate, and the belts stay painted
// equatorward, blended across the channel's edge.
inline bool QG2 = false;
constexpr double QG2_BLEND_DEG = 6.0;   // degrees over which the QG wind fades into the painted belts
// THE SAME WEATHER ON THE GEODESIC GRID (qg2geo.h): one global domain,
// sampled onto the mesh from this grid's painted fields each hour and
// sampled back by nearest cell for the water. The lat-lon QG2 stays as
// the comparison until this one beats it.
inline bool QG2GEO = false;
// THE WATER IN TWO LAYERS ON THE MESH (water2geo.h), riding the two QG
// winds, with the lift between them from the dynamics. Needs QG2GEO. The
// lat-lon column below is then only a mirror: evaporation is still
// computed here from the mirrored column, the rain and the vapour come
// back by nearest mesh cell.
inline bool WATER2 = false;
// NOTHING PAINTED BUT THE LAND (decision of 2026-09-08): with PRESCRIBED
// off, the painted climate may set the state once, at the first hour, as
// the initial guess every climate model starts from, and after that no
// climate field is set by anything but the equations. The physics path
// (see PRESCRIBED for what it is), the mesh weather for the wind, the
// two-layer water on the mesh. sweep.exe earth 0 physgeo 1.
inline bool PAINT_INIT = true;
constexpr int DYN2_SUBSTEPS = 30;   // of dyn2::DT, per hour
constexpr double PRE_LAPSE = 6.5;          // K/km on the model's smoothed elevation
constexpr double PRE_CONT_KM = 500.0;      // e-folding of continentality with distance from the sea: 500 km inland is already continental
// The old diagnostic model's 120 Pa/K was tuned on its own small land-sea
// contrasts; on the painted climate's 20 K summer continents it made 10-30
// m/s over every continent, diverging off winter land and converging onto
// every summer coast -- a ring of coastal rain round dark interiors, and a
// dry west coast at 45N where it overrode the westerlies. Earth's monsoon
// low is about 10 hPa on a 20 K anomaly: 50 Pa/K, and the flow it drives
// is a few metres a second.
// And the two signs are not alike. A summer heat low is deep and draws the
// sea's air a thousand kilometres inland -- the monsoon, and the rain on
// every subtropical east coast. A winter high is a shallow pool of cold air
// under an inversion, and its outflow is a fraction of that: cut to the
// same strength as the low it made every 45N west coast a desert.
constexpr double PRE_P_PER_DEG = 140.0;    // Pa of thermal-anomaly pressure per K, warm anomalies (the summer low that pulls Gulf air over the plains)
constexpr double PRE_COLD_SHARE = 0.25;    // of that, for cold ones
constexpr double PRE_ANOM_WIND_MAX = 12.0; // m/s, the most the anomaly may add
constexpr double PRE_FRICTION = 1.0 / (8.0 * 3600.0); // the Ekman balance's friction
constexpr double PRE_ITCZ_SHIFT = 8.0;     // degrees the belts follow the sun over the sea
constexpr double PRE_ITCZ_LAND_SHIFT = 4.0;  // and this much further over a continent (8 put the monsoon on Iran and the Sahara)
// THE SEA HAS CURRENTS, painted. Under the equatorward flow on the eastern
// flank of every subtropical high the surface water is upwelled and cold
// -- Peru, Namibia, California, the Canaries, western Australia -- and the
// coast beside it is a desert with fog. On the western flank a warm current
// runs poleward along the continent's east coast -- the Gulf Stream, the
// Kuroshio, the Brazil and Agulhas currents -- and then drifts across the
// ocean to warm the far shore in the 45-65 band: this is why Europe is 8 K
// warmer than its latitude and Vancouver mild. Without them the painted
// Peru coast sat on 27 degC water and rained 12 mm/day in summer, and
// central Europe was 0 degC and taiga.
constexpr double PRE_UPWELL_K = -6.0;      // K on the sea with land to its east, 8-32 degrees
constexpr double PRE_UPWELL_KM = 700.0;
constexpr double PRE_WBC_K = 3.0;          // K on the sea with land to its west, 25-45 degrees
constexpr double PRE_WBC_KM = 600.0;
constexpr double PRE_DRIFT_K = 5.0;        // K on the sea with land to its east, 45-65 degrees
constexpr double PRE_DRIFT_KM = 1800.0;
// And the land's mean follows the sea UPWIND of it, not the nearest sea:
// in the westerlies a west coast's mildness reaches a thousand kilometres
// inland (Europe), an east coast's does not (New York at Lisbon's latitude).
constexpr double PRE_UPWIND_KM = 2500.0;
constexpr double PRE_MONSOON_KM = 1500.0;  // the continent scale that decides it
// The subtropical highs sit over the OCEANS, and their flanks are the
// asymmetry that makes a subtropical east coast wet and a west coast dry:
// on the western flank of an ocean high the flow is poleward, warm and
// moist off the sea (the US Southeast, south China, southern Brazil), on
// the eastern flank equatorward and dry (California, the Sahara's coast,
// the Atacama). Painted as a pressure anomaly over sea in the 20-40 band,
// in the same units as the thermal anomaly.
constexpr double PRE_SUBTROP_HIGH_K = 8.0;  // K-equivalent: about 8 hPa at PRE_P_PER_DEG
constexpr double PRE_SUBTROP_LAT = 30.0, PRE_SUBTROP_WIDTH = 9.0;
constexpr double PRE_DIURNAL_LAND = 5.0;   // K half-swing, deep interior; coasts less
constexpr double PRE_DIURNAL_SEA = 0.5;
struct Prescribed {
    std::vector<float> cont;    // continentality: 0 at sea, 1 deep in a continent
    std::vector<float> wide;    // the same on the continent scale (see PRE_MONSOON_KM)
    std::vector<float> dEast, dWest;  // sea cells: km to the nearest land eastward / westward along the row
    std::vector<float> dUpwind;       // land cells: km to the sea upwind (west in the westerlies, east in the trades)
    std::vector<float> lonDeg, latDeg;
    // Zonal targets on |lat|, knots every 15 degrees from the equator to the
    // pole. Means are annual; amplitudes are the seasonal half-swing, the
    // land's at full continentality.
    // The sea's curve is the SURFACE the air sees: open water where it
    // is above freezing, the ice's skin where it is not -- which is why it
    // runs to -30 at the winter pole though the water beneath never does.
    // The land curves are the deep INTERIOR's; a coast has the sea's
    // climate, and continentality mixes between them -- mean and swing
    // both, since a maritime coast is warmer than the interior in the
    // annual mean as well as flatter through the year.
    static constexpr double LAND_MEAN[7] = {26.0, 25.0, 21.0, 13.0, -4.0, -22.0, -36.0};
    static constexpr double LAND_AMP[7] = {1.5, 5.0, 12.0, 15.0, 24.0, 24.0, 16.0};
    static constexpr double SEA_MEAN[7] = {27.5, 26.5, 21.0, 13.0, 3.0, -10.0, -18.0};
    static constexpr double SEA_AMP[7] = {1.0, 1.5, 4.0, 5.5, 6.0, 12.0, 16.0};
    // The belts on latitude relative to the shifted ITCZ, knots every 10
    // degrees: zonal wind (east positive) and the poleward component.
    // These are the LAYER's winds: the surface wind the fluxes see is
    // seven tenths of them, so Earth's 6-7 m/s surface trades are 8.5
    // here and the 8 m/s surface westerlies 12.
    static constexpr double BELT_U[10] = {-3.0, -8.5, -8.5, -1.5, 8.0, 12.0, 10.0, 1.5, -4.0, -3.0};
    static constexpr double BELT_VP[10] = {0.0, -2.5, -2.2, -0.7, 1.5, 2.0, 1.5, -0.7, -1.5, 0.0};
    static double knots(const double* v, int n, double step, double a) {
        double p = std::clamp(a / step, 0.0, (double)(n - 1));
        int k = std::min((int)p, n - 2);
        double t = p - k;
        return v[k] + t * (v[k + 1] - v[k]);
    }
    void init(const std::vector<unsigned char>& water, const std::vector<float>& latRad) {
        cont.assign(W * H, 0.0f);
        lonDeg.assign(W * H, 0.0f);
        latDeg.assign(W * H, 0.0f);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int i = y * W + x;
                lonDeg[i] = (float)(((x + 0.5) / W) * 360.0 - 180.0);
                latDeg[i] = (float)(latRad[i] * 180.0 / 3.14159265);
            }
        // Distance to the sea, in km, by relaxation over the grid: each land
        // cell is the nearest neighbour's distance plus the step to it.
        const double dyKm = 3.14159265 * R_EARTH / H / 1000.0;
        std::vector<float> d(W * H, 1e9f);
        for (int i = 0; i < W * H; i++) if (water[i]) d[i] = 0.0f;
        for (int pass = 0; pass < 64; pass++) {
            bool changed = false;
            for (int y = 0; y < H; y++) {
                double dxKm = dyKm * 2.0 * std::max(std::cos((double)latRad[y * W]), 0.05);
                for (int x = 0; x < W; x++) {
                    int i = y * W + x;
                    if (water[i]) continue;
                    float best = d[i];
                    best = std::min(best, d[y * W + (x + 1) % W] + (float)dxKm);
                    best = std::min(best, d[y * W + (x + W - 1) % W] + (float)dxKm);
                    if (y + 1 < H) best = std::min(best, d[(y + 1) * W + x] + (float)dyKm);
                    if (y > 0) best = std::min(best, d[(y - 1) * W + x] + (float)dyKm);
                    if (best < d[i]) { d[i] = best; changed = true; }
                }
            }
            if (!changed) break;
        }
        for (int i = 0; i < W * H; i++)
            cont[i] = water[i] ? 0.0f : (float)(1.0 - std::exp(-d[i] / PRE_CONT_KM));
        // Along each row: how far a sea cell is from land to its east and
        // west, and how far a land cell is from the sea upwind of it.
        dEast.assign(W * H, 1e9f); dWest.assign(W * H, 1e9f); dUpwind.assign(W * H, 1e9f);
        for (int y = 0; y < H; y++) {
            double dxKm = dyKm * 2.0 * std::max(std::cos((double)latRad[y * W]), 0.05);
            double alat = std::fabs(latRad[y * W] * 180.0 / 3.14159265);
            // westerlies above 32 degrees, trades below 28, a blend between
            double west = std::clamp((alat - 28.0) / 4.0, 0.0, 1.0);
            for (int x = 0; x < W; x++) {
                int i = y * W + x;
                if (water[i]) {
                    for (int k = 1; k < W; k++) {
                        if (!water[y * W + (x + k) % W]) { dEast[i] = (float)(k * dxKm); break; }
                    }
                    for (int k = 1; k < W; k++) {
                        if (!water[y * W + (x - k + W) % W]) { dWest[i] = (float)(k * dxKm); break; }
                    }
                } else {
                    double dw = 1e9, de = 1e9;
                    for (int k = 1; k < W; k++) {
                        if (water[y * W + (x - k + W) % W]) { dw = k * dxKm; break; }
                    }
                    for (int k = 1; k < W; k++) {
                        if (water[y * W + (x + k) % W]) { de = k * dxKm; break; }
                    }
                    dUpwind[i] = (float)(west * dw + (1.0 - west) * de);
                }
            }
        }
        wide.assign(W * H, 0.0f);
        for (int i = 0; i < W * H; i++)
            wide[i] = water[i] ? 0.0f : (float)(1.0 - std::exp(-d[i] / PRE_MONSOON_KM));
    }
    // The land's zonal temperature at a moderately continental coast: what
    // the air over a mid-latitude sea in winter has just blown off.
    double landZonalT(int i, double doy) const {
        double lat = latDeg[i], alat = std::fabs(lat);
        double peak = 200.0 + (lat < 0 ? 182.5 : 0.0);
        double phase = std::cos(2 * 3.14159265 * (doy - peak) / 365.0);
        const double c = 0.7;
        double mean = (1 - c) * knots(SEA_MEAN, 7, 15.0, alat) + c * knots(LAND_MEAN, 7, 15.0, alat);
        double amp = (1 - c) * (knots(SEA_AMP, 7, 15.0, alat) + 2.0) + c * knots(LAND_AMP, 7, 15.0, alat);
        return mean + amp * phase;
    }
    // The surface temperature of a cell at a moment.
    double surfaceT(int i, bool water, double elevM, double doy, double hour) const {
        double lat = latDeg[i], alat = std::fabs(lat);
        // Land peaks about a month after the solstice, the sea two; the
        // south is half a year behind.
        double peak = (water ? 230.0 : 200.0) + (lat < 0 ? 182.5 : 0.0);
        double phase = std::cos(2 * 3.14159265 * (doy - peak) / 365.0);
        double mean, amp;
        auto band = [](double a, double lo, double hi) { // 1 inside lo..hi, fading over 5 degrees
            return std::clamp((a - lo) / 5.0 + 1.0, 0.0, 1.0) * std::clamp((hi - a) / 5.0 + 1.0, 0.0, 1.0);
        };
        if (water) {
            mean = knots(SEA_MEAN, 7, 15.0, alat);
            amp = knots(SEA_AMP, 7, 15.0, alat);
            // the currents (see PRE_UPWELL_K)
            mean += PRE_UPWELL_K * std::exp(-dEast[i] / PRE_UPWELL_KM) * band(alat, 8, 32) +
                    PRE_WBC_K * std::exp(-dWest[i] / PRE_WBC_KM) * band(alat, 25, 45) +
                    PRE_DRIFT_K * std::exp(-dEast[i] / PRE_DRIFT_KM) * band(alat, 45, 65);
        } else {
            double c = cont[i];
            // the mean follows the sea upwind (see PRE_UPWIND_KM), the
            // swing the nearest sea
            double cm = 1.0 - std::exp(-dUpwind[i] / PRE_UPWIND_KM);
            double seaMean = knots(SEA_MEAN, 7, 15.0, alat);
            // and a west coast in the westerlies gets the drift's warmth too
            double west = std::clamp((alat - 28.0) / 4.0, 0.0, 1.0);
            seaMean += west * PRE_DRIFT_K * band(alat, 45, 65);
            mean = (1 - cm) * seaMean + cm * knots(LAND_MEAN, 7, 15.0, alat);
            // the swing too: maritime damping comes with air that has been
            // over the sea, and New York's air has not
            amp = (1 - cm) * (knots(SEA_AMP, 7, 15.0, alat) + 1.0) + cm * knots(LAND_AMP, 7, 15.0, alat);
            (void)c;
        }
        double T = mean + amp * phase;
        if (!water) T -= PRE_LAPSE * std::max(elevM, 0.0) / 1000.0;
        double lh = hour + lonDeg[i] / 15.0;
        double di = water ? PRE_DIURNAL_SEA : PRE_DIURNAL_LAND * (0.5 + 0.5 * cont[i]);
        T += di * std::cos(2 * 3.14159265 * (lh - 15.0) / 24.0);
        return T;
    }
    // The belt wind of a latitude at a time of year.
    // The mean vertical motion of the overturning that the painted belts
    // imply and the model cannot make for itself: the eddies' ascent
    // along the storm tracks at 45-65, the subtropical subsidence under
    // the highs, and the weak polar sinking. The ITCZ's own ascent comes
    // from convection and convergence already and gets nothing here.
    // Knots every 10 degrees from the shifted ITCZ, m/s.
    // Sized against the ITCZ, whose convection plus convergence lift about
    // 0.005 m/s here and make 6 mm/day: the storm tracks lift as hard.
    // Earth's mean storm track is at 45; put at 50-60 the rain landed on
    // the taiga and the 30-45 band stayed tan.
    static constexpr double BELT_W[10] = {0.0, 0.0, -0.002, -0.002, 0.003, 0.004, 0.002, 0.001, 0.0, -0.001};
    // How far the ITCZ has followed the sun here: 8 degrees over the sea,
    // and over a continent as far as 20, because the thermal equator goes
    // where the heated land is. That is the monsoon -- India's rain, the
    // Sahel's, northern Australia's -- and with a uniform 8 degrees India
    // got no rain in any season.
    double shiftAt(int i, double doy) const {
        return (PRE_ITCZ_SHIFT + PRE_ITCZ_LAND_SHIFT * wide[i]) *
               std::cos(2 * 3.14159265 * (doy - 200.0) / 365.0);
    }
    double beltUplift(int i, double doy) const {
        double shift = shiftAt(i, doy);
        return knots(BELT_W, 10, 10.0, std::fabs(latDeg[i] - shift));
    }
    void beltWind(int i, double doy, double& u, double& v) const {
        double shift = shiftAt(i, doy);
        double p = latDeg[i] - shift;
        double a = std::fabs(p), sgn = p >= 0 ? 1.0 : -1.0;
        u = knots(BELT_U, 10, 10.0, a);
        v = sgn * knots(BELT_VP, 10, 10.0, a);
    }
};

// THE CLIMATE AS RULES OF THUMB (decision of 2026-09-09). The textbook
// "hypothetical continent" in code: a zonal rain by latitude that follows
// the sun, and on land the rules every physical-geography course teaches
// to explain the Koppen map -- interiors dry in proportion to their
// distance upwind from the sea; subtropical west coasts deserts under the
// high and the cold current; subtropical east coasts humid, and monsoonal
// when the continent behind them is large; Mediterranean west coasts at
// 30-42 with their winter rain; temperate west coasts the wettest places;
// windward slopes wet and the lee dry; continental interiors with their
// summer rain. Temperature stays the painted one, which the review found
// right. Nothing is transported, so nothing leaks, accumulates or floods.
// Judged, like everything, by THE WORLD REVIEW.
//
// This is the only rain there is. The column water it replaced -- the
// conservative moisture transport on this grid, the rain against a
// lift-lowered ceiling, and the two-layer water on the mesh -- was
// unreachable from every build after the decision and was removed on
// 2026-09-15 (work order 01); it lives on the reference branches listed in
// Meta/Git.md.
constexpr double RUL_INTERIOR_KM = 4000.0;   // the e-folding of the coast's rain inland, along the wind
constexpr double RUL_EAST_KM = 2800.0;       // and from the east coast, on the subtropical high's western flank
constexpr double RUL_MONSOON_KM = 1000.0;    // and from the sea equatorward, in the monsoon
constexpr double RUL_MONSOON_PLATEAU_KM = 2600.0;   // its reach with a high plateau poleward, whose summer heat low pulls it on (India against Africa)
constexpr double RUL_PLATEAU_M = 3000.0, RUL_PLATEAU_REACH_KM = 1800.0;
constexpr double RUL_MONSOON_LAT = 18.0, RUL_MONSOON_PLATEAU_LAT = 32.0;   // the monsoon's poleward limit, without and with the plateau (Yemen against the Punjab)
constexpr double RUL_MONSOON = 2.2;          // the monsoon coast's multiple
constexpr double RUL_SUBTROPICAL_DRY = 0.3;   // land under the subtropical high's share, 18-32 degrees, unless a coast rule reaches it
constexpr double RUL_TROPICS = 1.3;          // the tropical land's multiple: recycling and convergence, no interior decay
constexpr double RUL_INTERIOR_FLOOR = 0.4;   // the interior keeps this share of the belt's rain, as summer convection
constexpr double RUL_WEST_DESERT = 0.15;     // the subtropical west coast's share of the belt's rain
constexpr double RUL_EAST_HUMID = 2.2;       // the subtropical east coast's multiple
constexpr double RUL_TEMPERATE_WEST = 1.5;   // the temperate west coast's multiple
constexpr double RUL_LIFT_M = 700.0;         // metres of rise along the wind that doubles the rain
constexpr double RUL_SHADOW_M = 600.0;       // a ridge this much above the cell, upwind, starts the shadow
constexpr double RUL_SHADOW_FLOOR = 0.3;     // the deepest shadow's share
constexpr double RUL_SHADOW_KM = 500.0;      // how far upwind a ridge casts it
constexpr double RUL_MEDITERRANEAN = 0.7;    // the Mediterranean coast's share: half a year under the high
constexpr double RUL_PLATEAU = 0.45;         // a high interior plateau's share
constexpr double RUL_TRADE_COAST = 1.5;      // the trade-wind east coast's multiple, within RUL_TRADE_KM
constexpr double RUL_TRADE_KM = 400.0;
constexpr double RUL_OROGRAPHIC_KM = 800.0;  // the lift needs sea air: it acts within this distance of the sea
constexpr double RUL_ITCZ_SHIFT = 8.0, RUL_ITCZ_LAND_SHIFT = 4.0;   // degrees the belts follow the sun; more over land
struct Rules {
    int W = 0, H = 0;
    std::vector<float> base;      // land: the geographic multiple on the belt's rain
    std::vector<float> dSeaW, dSeaE, dSea;   // km
    std::vector<float> dLandE;               // sea cells: km to land eastward (the upwelling coast's sea)
    std::vector<float> dSeaEq, dSeaPole;     // land: km to the sea equatorward and poleward along the column
    std::vector<unsigned char> water;
    std::vector<float> latDeg, elev, elevS;   // and the elevation smoothed over three cells
    std::vector<float> cont, wide;
    std::vector<unsigned char> kind;   // 0 sea, 1 interior, 2 west desert, 3 east humid, 4 mediterranean, 5 temperate west, 6 monsoon, 7 tropics
    // Earth's zonal-mean rain by latitude, mm/day, every 10 degrees from the equator
    static constexpr double R0[10] = {4.5, 3.8, 2.0, 1.2, 2.2, 3.0, 2.6, 1.2, 0.5, 0.3};
    static double knots(const double* v, int n, double step, double a) {
        double p = std::clamp(a / step, 0.0, (double)(n - 1));
        int k = std::min((int)p, n - 2);
        return v[k] + (p - k) * (v[k + 1] - v[k]);
    }
    // Every rule fades in and out over a few degrees and a few hundred
    // kilometres: hard switches drew the 208 km cells as blocks on the map.
    static double band(double a, double lo, double hi, double w = 4.0) {
        return std::clamp((a - lo) / w + 0.5, 0.0, 1.0) * std::clamp((hi - a) / w + 0.5, 0.0, 1.0);
    }
    static double within(double d, double km, double w = 300.0) { return std::clamp((km - d) / w + 0.5, 0.0, 1.0); }
    static double season(double doy, double latDegSigned) {   // +1 at midsummer, -1 at midwinter, for this hemisphere
        double s = std::cos(2 * 3.14159265 * (doy - 202.0) / 365.0);
        return latDegSigned >= 0 ? s : -s;
    }
    void init(int w, int h, const std::vector<float>& elevM, const std::vector<unsigned char>& wat,
              const std::vector<float>& latRad, const Prescribed& pre) {
        W = w; H = h; water = wat; elev = elevM; cont = pre.cont; wide = pre.wide; dLandE = pre.dEast;
        elevS = elev;
        for (int pass = 0; pass < 2; pass++) {
            std::vector<float> t = elevS;
            for (int y = 1; y < H - 1; y++)
                for (int x = 0; x < W; x++) {
                    int i = y * W + x; double sum = 0, wsum = 0;
                    for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
                        int j = (y + dy) * W + (x + dx + W) % W;
                        double wgt = (dx == 0 ? 2.0 : 1.0) * (dy == 0 ? 2.0 : 1.0);
                        sum += wgt * std::max(elevS[j], 0.0f); wsum += wgt;
                    }
                    t[i] = (float)(sum / wsum);
                }
            elevS = t;
        }
        latDeg.assign(W * H, 0.0f); base.assign(W * H, 1.0f); kind.assign(W * H, 0);
        dSeaW.assign(W * H, 1e9f); dSeaE.assign(W * H, 1e9f); dSea.assign(W * H, 0.0f); dSeaEq.assign(W * H, 1e9f); dSeaPole.assign(W * H, 1e9f);
        const double dyKm = 3.14159265 * R_EARTH / H / 1000.0;
        for (int y = 0; y < H; y++) {
            double dxKm = dyKm * 2.0 * std::max(std::cos((double)latRad[y * W]), 0.05);
            for (int x = 0; x < W; x++) {
                int i = y * W + x;
                latDeg[i] = (float)(latRad[i] * 180.0 / 3.14159265);
                if (water[i]) continue;
                for (int k = 1; k < W; k++) if (water[y * W + (x - k + W) % W]) { dSeaW[i] = (float)(k * dxKm); break; }
                for (int k = 1; k < W; k++) if (water[y * W + (x + k) % W]) { dSeaE[i] = (float)(k * dxKm); break; }
                dSea[i] = (float)(-PRE_CONT_KM * std::log(std::max(1.0 - cont[i], 1e-6)));
                int dir = latDeg[i] >= 0 ? -1 : 1;   // toward the equator
                for (int k = 1; k < H / 2; k++) {
                    int yy = y + dir * k;
                    if (yy < 0 || yy >= H) break;
                    if (water[yy * W + x]) { dSeaEq[i] = (float)(k * dyKm); break; }
                }
                for (int k = 1; k < H / 2; k++) {
                    int yy = y - dir * k;
                    if (yy < 0 || yy >= H) { dSeaPole[i] = (float)(k * dyKm); break; }
                    if (water[yy * W + x]) { dSeaPole[i] = (float)(k * dyKm); break; }
                }
            }
        }
        for (int y = 0; y < H; y++) {
            double dxKm = dyKm * 2.0 * std::max(std::cos((double)latRad[y * W]), 0.05);
            for (int x = 0; x < W; x++) {
                int i = y * W + x;
                if (water[i]) continue;
                double a = std::fabs(latDeg[i]);
                double west = std::clamp((a - 28.0) / 4.0, 0.0, 1.0);   // 1: the wind comes from the west
                double dUp = west * dSeaW[i] + (1.0 - west) * dSeaE[i];
                // the interior: the coast's rain decays along the wind, to a floor of summer
                // convection that the cold high latitudes do not have
                double floorHere = RUL_INTERIOR_FLOOR * std::clamp((75.0 - a) / 25.0, 0.3, 1.0);
                double f = std::max(std::exp(-dUp / RUL_INTERIOR_KM), floorHere);
                unsigned char k = 1;
                // the subtropical high: the belt's land is desert unless a coast rule below reaches it
                {
                    double under = std::clamp((a - 13.0) / 4.0, 0.0, 1.0) * std::clamp((36.0 - a) / 4.0, 0.0, 1.0);
                    f *= 1.0 - (1.0 - RUL_SUBTROPICAL_DRY) * under;
                }
                // the subtropical high's western flank pushes sea air poleward and inland
                // from the east coast: the humid subtropics, decaying from THAT coast
                {
                    double b = band(a, 25, 50);
                    double fe = RUL_EAST_HUMID * std::exp(-dSeaE[i] / RUL_EAST_KM);
                    double fx = f + (std::max(fe, f) - f) * b;
                    if (fx > f) { f = fx; k = 3; }
                }
                // the trade-wind coast: wet where the trades come ashore, and only there
                {
                    double b = band(a, 8, 25);
                    double ft = RUL_TRADE_COAST * std::exp(-dSeaE[i] / RUL_TRADE_KM);
                    double fx = f + (std::max(ft, f) - f) * b;
                    if (fx > f) { f = fx; k = 3; }
                }
                // the monsoon: a large continent with the sea equatorward of it
                if (band(a, 8, 30) > 0 && dSeaPole[i] > 900) {   // a continent behind the coast: the heat low that draws the sea air in
                    // a plateau poleward of the cell: the heat low that draws the monsoon inland
                    // a plateau the size of Tibet, not a range: several cells above RUL_PLATEAU_M
                    // (the Alps stood poleward of the Sahara and gave the Sahel India's reach)
                    int high = 0;
                    int pdir = latDeg[i] >= 0 ? 1 : -1;
                    int span = (int)(20.0 / (360.0 / W));   // and twenty degrees of longitude either side: Tibet stands west of China
                    for (int c = 1; c * dyKm <= RUL_PLATEAU_REACH_KM; c++) {
                        int yy = y + pdir * c;
                        if (yy < 0 || yy >= H) break;
                        for (int dx = -span; dx <= span; dx++) {
                            int j = yy * W + (x + dx + W) % W;
                            if (!water[j] && elev[j] > RUL_PLATEAU_M) high++;
                        }
                    }
                    bool plateau = high >= 6;
                    double limit = plateau ? RUL_MONSOON_PLATEAU_LAT : RUL_MONSOON_LAT;
                    double fm = RUL_MONSOON * std::exp(-dSeaEq[i] / (plateau ? RUL_MONSOON_PLATEAU_KM : RUL_MONSOON_KM)) *
                                std::clamp((limit + 3.0 - a) / 3.0, 0.0, 1.0) * band(a, 8, 30) *
                                std::clamp((dSeaPole[i] - 900.0) / 600.0, 0.0, 1.0);
                    double fx = f + (std::max(fm, f) - f);
                    if (fm > f) { f = fx; k = 6; }
                }
                // the tropics: no interior decay, recycling and convergence instead
                if (a < 15) { double t = std::clamp((15.0 - a) / 5.0, 0.0, 1.0); f = std::max(f, 1.0 + (RUL_TROPICS - 1.0) * t); if (t > 0.5) k = 7; }
                // subtropical west coast: the desert under the high and the cold current
                {
                    double b = band(a, 8, 32) * within(dSeaW[i], 1400) * (1.0 - within(dSeaE[i], 600));
                    double share = RUL_WEST_DESERT + (1 - RUL_WEST_DESERT) * std::clamp((dSeaW[i] - 600.0) / 800.0, 0.0, 1.0);
                    f *= 1.0 - (1.0 - share) * b;
                    if (b > 0.5) k = 2;
                }
                // temperate west coast, and the Mediterranean one equatorward of it
                {
                    double bm = band(a, 30, 42) * within(dSeaW[i], 800) * (k == 2 ? 0.0 : 1.0);
                    double bt = band(a, 38, 60) * within(dSeaW[i], 600) * (1.0 - bm);
                    f *= 1.0 - (1.0 - RUL_MEDITERRANEAN) * bm;
                    f *= 1.0 + (RUL_TEMPERATE_WEST - 1.0) * bt;
                    if (bm > 0.5) k = 4; else if (bt > 0.5) k = 5;
                }
                // orography along the wind: the rise from the upwind cell, and the ridge upwind
                // on the smoothed elevation, over two cells, so a range reads as a slope
                int up = west > 0.5 ? (x - 1 + W) % W : (x + 1) % W;
                int up2 = west > 0.5 ? (x - 2 + W) % W : (x + 2) % W;
                double rise = 0.5 * ((elevS[i] - elevS[y * W + up]) + (elevS[y * W + up] - elevS[y * W + up2]));
                if (rise > 0) f *= 1.0 + (std::min(1.0 + rise / RUL_LIFT_M, k == 4 || k == 2 ? 1.3 : 1.8) - 1.0) * within(dSea[i], RUL_OROGRAPHIC_KM);
                double ridge = 0;
                int cells = (int)(RUL_SHADOW_KM / dxKm) + 1;
                for (int c = 1; c <= cells; c++) {
                    if (c * dxKm > dUp) break;   // only a ridge between the cell and its sea
                    int xx = west > 0.5 ? (x - c + W) % W : (x + c) % W;
                    ridge = std::max(ridge, (double)elevS[y * W + xx] - elevS[i]);
                }
                if (ridge > RUL_SHADOW_M) f *= std::max(RUL_SHADOW_FLOOR, 1.0 - (ridge - RUL_SHADOW_M) / (2 * RUL_SHADOW_M));
                f *= 1.0 - (1.0 - RUL_PLATEAU) * std::clamp((elevS[i] - 1200.0) / 600.0, 0.0, 1.0) * (rise <= 0 ? 1.0 : 0.5) * (1.0 - within(dSea[i], 500));
                base[i] = (float)f; kind[i] = k;
            }
        }
        smoothBase();
    }
    // The multiple smoothed over land, two passes: what the rules say
    // changes by a factor of two across one cell where a rule switches on,
    // and the map drew the 208 km cells as blocks.
    void smoothBase() {
        for (int pass = 0; pass < 2; pass++) {
            std::vector<float> t = base;
            for (int y = 1; y < H - 1; y++)
                for (int x = 0; x < W; x++) {
                    int i = y * W + x;
                    if (water[i]) continue;
                    double sum = 0, wsum = 0;
                    for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
                        int j = (y + dy) * W + (x + dx + W) % W;
                        if (water[j]) continue;
                        double wgt = (dx == 0 ? 2.0 : 1.0) * (dy == 0 ? 2.0 : 1.0);
                        sum += wgt * base[j]; wsum += wgt;
                    }
                    t[i] = (float)(sum / wsum);
                }
            base = t;
        }
    }
    // mm/day at this cell on this day
    double rain(int i, double doy) const {
        double lat = latDeg[i];
        double shift = (RUL_ITCZ_SHIFT + RUL_ITCZ_LAND_SHIFT * wide[i]) * std::cos(2 * 3.14159265 * (doy - 202.0) / 365.0);
        double r = knots(R0, 10, 10.0, std::fabs(lat - shift));
        if (water[i]) {
            // the upwelling coast under the subtropical high: the sea with land to its east
            double a = std::fabs(lat);
            if (a >= 8 && a <= 32) r *= 0.3 + 0.7 * std::clamp(dLandE[i] / 700.0, 0.0, 1.0);
            return r;
        }
        double s = season(doy, lat);
        double a = std::fabs(lat);
        double f = base[i];
        switch (kind[i]) {
            case 6: f *= 1.0 + 1.0 * s; break;                       // monsoon: summer three times the winter
            case 4: f *= 1.0 - 0.65 * s; break;                      // Mediterranean: the winter's rain
            case 5: f *= 1.0 - 0.3 * s; break;                       // temperate west coast: a winter maximum
            case 1: if (a > 35) f *= 1.0 + 0.5 * s; break;           // the interior's summer convection
            default: break;
        }
        return r * f;
    }
};

struct Climatology {
    // PROBE: the tropical-ocean column budget, term by term -- every
    // open-sea cell within 15 degrees of the equator, every hour after
    // spin-up. Order: surface SW, air SW, LW up, LW down, sensible,
    // latent, OLR, Ts, Tb, Wv, emissivity, cloud, Tf, cell-hours. The point:
    // tropical SST is set by this column's balance and almost nothing
    // else, so each term can face its measured Earth value no matter what
    // the continents are doing.
    static constexpr int NTB = 24;
    double tropBud[NTB] = {};
    // PROBE: the zonal energy budget, every cell, every hour after spin-up,
    // as W/m2 means per row. Order: absorbed SW (TOA), OLR, surface SW,
    // LW down, LW up, sensible, latent, BL horizontal (wind+eddies), FT
    // horizontal (eddies), deposit into BL, FT entrainment from BL, FT pool
    // exchange, condensation, cell-hours. The question it answers: where
    // the planet's pole-to-equator contrast is made and where it is lost.
    static constexpr int NZB = 14;
    std::vector<double> zonBud;  // annual, [H][NZB]
    std::vector<double> zonBudS; // by season, [SEASONS][H][NZB]
    std::vector<double> zonBudLS; // by season and surface, [SEASONS][2: sea, land][H][NZB]
    double dbgEvap = 0, dbgRain = 0;              // PROBE: is water conserved?
    double dbgWv = 0, dbgWind = 0, dbgRH = 0;     // PROBE: water, wind, saturation
    // [season][cell]
    std::vector<float> meanT, rainMmDay, snowMmDay, rainProb, windU, windV, cloud, diurnal;
    // The air itself, kept for diagnosis and for the map: water in the
    // column, how near saturation it is, the height field that is the
    // pressure map, and the air's own temperature.
    std::vector<float> wv, rh, press, airT, airTf;
    std::vector<float> d2u1, d2u2, d2ps, d2psSd, d2eke; // [cell] annual: the two-level dynamics' probes (see DYN2)
    std::vector<float> upConv, upDiv, upFront, upOrog, capX; // PROBE: what lifts the air
    std::vector<float> evapF, latF;                          // PROBE: the water budget
    std::vector<float> spdF, capSkinF, supplyF, affordF;     // PROBE: and the evaporation
    std::vector<float> iceM;                                 // sea ice, m, by season
    std::vector<float> soilM;                                // PROBE: land water store, mm, by season
    std::vector<unsigned char> isWater;                      // the model's own mask [cell]
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
                        &upOrog, &capX, &evapF, &latF, &spdF,
                        &capSkinF, &supplyF, &affordF, &iceM, &soilM})
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
    std::vector<double> nT, nW, nu, nv, div, divTmp, rainStep, Tsl;
    std::vector<double> Tb, nTb, Tf, nTf; // the air: boundary layer and free troposphere
    std::vector<double> hP, nhP, nu2, nv2; // the moving air: thickness and momentum
    std::vector<double> hWant, hTmp;       // what the warmth asks of the height, smoothed
    std::vector<double> wTop;              // diabatic ascent leaving through the top, m/s
    // The hour's mass movements, banked by the dynamics for the heat to
    // ride on: face fluxes east and north (m2 per unit width), the thermal
    // relaxation's mass source (m), the thickness at the hour's start, and
    // each cell's affordable share of its outgoing mass (see FLUX FORM).
    std::vector<double> hT, nhT, tSub;     // thickness x temperature: the heat the mass carries
    std::vector<double> hPrev, TbPrev, stabArr; // the hour's start, and last hour's stability
    // PROBE: is the air's heat conserved? Per hour: the two layers' energy
    // change, the physics that entered them, and the mass the heat rode on
    // against the mass the dynamics ended with. Summed per day.
    std::vector<double> hbNew, physRow;
    double dbgE[3] = {0, 0, 0};
    long dbgN[3] = {0, 0, 0};                 // cell-hours at the Tb clamp, Tf clamp, flux limiter
    double dbgX[4] = {1e9, -1e9, -1e9, 1e9};  // hP min, hP max, Tb max, Tf min
    double tRelaxPool = 0; // temperature of the mass the relaxation takes, this hour
    double wTopMean = 0;                   // its area-weighted mean: what comes back down
    double tfPool = 0;                     // export-weighted mean Tf: what the upper branch carries
    // PROBE: tropical-ocean budget rows (13 terms x H); each thread owns
    // its own row of the parallel loop, so no atomics. Gated on stat.
    std::vector<double> budRow;
    std::vector<double> zonRow; // PROBE: zonal energy budget, [season][row][term]
    int curSeason = 0;
    bool recordBudget = false;
    std::vector<double> soil;          // land water store, mm: what there is to evaporate
    std::vector<double> ice, nIce;     // sea ice, m of thickness (see L_ICE)
    Prescribed pre;                    // the painted climate (see PRESCRIBED)
    Rules rul;                         // the climate as rules of thumb (see THE CLIMATE AS RULES OF THUMB)
    std::vector<double> anomA, anomB;  // its thermal-anomaly pressure, smoothed
    dyn2::Model d2;                    // the two-level dynamics (see DYN2)
    qg2::Model qg;                     // the two-layer QG weather (see QG2)
    bool qgInit = false;
    qg2geo::Model qgg;                 // the same on the geodesic grid (see QG2GEO)
    bool qggInit = false;
    bool painted = false;              // the initial state has been painted (see PAINT_INIT)
    std::vector<int> meshOfCell;       // nearest mesh cell for each cell here
    std::vector<int> cellOfMesh;       // and the cell here under each mesh cell
    std::vector<double> qggT;          // the mesh's near-surface temperature
    water2::Model w2;                  // the two-layer water on the mesh (see WATER2)
    std::vector<float> qggElev; std::vector<unsigned char> qggWater;
    std::vector<double> evapBuf, wUpBuf, wConvBuf;   // this grid's evaporation, large-scale and convective ascent, for the mesh
    std::vector<double> w2Evap, w2Wup, w2Conv;         // the same averaged onto the mesh
    std::vector<int> meshCount;
    bool d2init = false;
    std::vector<double> tnsBuf;
    static void d2Filter(std::vector<double>& f, void* ctx) { ((Model*)ctx)->polarFilter(f, false); }
    std::vector<double> evapAcc, rainAcc;          // PROBE, one cell per thread: no atomics
    std::vector<double> pSpd, pCapSkin, pSupply, pAfford; // PROBE: the evaporation, term by term
    std::vector<double> cloudF;                    // and how much of it has condensed out
    std::vector<double> pConv, pDiv, pFront, pOrog, pOro; // PROBE: uplift, by cause
    std::vector<double> divSm; // the ascent that lasts, as opposed to the ascent that wobbles
    std::vector<double> capEff;                    // what each cell's air can hold, at its own temperature
    std::vector<double> wvAcc;                     // PROBE: column water over time
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
        // Each cell covers its own slice of the hydrology raster, with the
        // slice edges in exact proportion. This was `bx = hydrology::W / W`,
        // an integer division: 2048/192 is 10, not 10.67, so every cell read
        // the raster 6% too near the origin, the climate's whole map was the
        // south-western 94% of the terrain stretched over the globe, and a
        // cell's climate was applied up to 12 degrees of longitude and 6 of
        // latitude away from the ground that had made it. On the Earth
        // template the model's Georgia was the Gulf of Mexico. Every desert
        // on a coast that made no sense was this.
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int i = idx(x, y);
                int x0 = (int)((long long)x * hydrology::W / W), x1 = (int)((long long)(x + 1) * hydrology::W / W);
                int y0 = (int)((long long)y * hydrology::H / H), y1 = (int)((long long)(y + 1) * hydrology::H / H);
                double hsum = 0, land = 0, cells = 0;
                for (int yy = y0; yy < y1; yy++)
                    for (int xx = x0; xx < x1; xx++) {
                        float h = hy.heightM[yy * hydrology::W + xx];
                        if (h > 0) { land++; hsum += h; }
                        cells++;
                    }
                double landFrac = land / std::max(cells, 1.0);
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
        divTmp.assign(W * H, 0.0);
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
        hT.assign(W * H, 0.0);
        nhT.assign(W * H, 0.0);
        tSub.assign(W * H, 0.0);
        hPrev.assign(W * H, 0.0);
        TbPrev.assign(W * H, 0.0);
        stabArr.assign(W * H, 1.0);
        hbNew.assign(W * H, H_LAYER);
        physRow.assign(H, 0.0);
        budRow.assign(Climatology::NTB * H, 0.0);
        zonRow.assign(SEASONS * 2 * Climatology::NZB * H, 0.0);
        nu2.assign(W * H, 0.0);
        nv2.assign(W * H, 0.0);
        soil.assign(W * H, SOIL_REF_MM); // half full; the spin-up settles it
        ice.assign(W * H, 0.0);          // the first winter makes it
        nIce.assign(W * H, 0.0);
        anomA.assign(W * H, 0.0);
        anomB.assign(W * H, 0.0);
        if (PRESCRIBED || PAINT_INIT) pre.init(water, latRad);
        rul.init(W, H, elev, water, latRad, pre);
        evapAcc.assign(W * H, 0.0);
        rainAcc.assign(W * H, 0.0);
        cloudF.assign(W * H, 0.5);
        pConv.assign(W * H, 0.0);
        pDiv.assign(W * H, 0.0);
        pFront.assign(W * H, 0.0);
        pOrog.assign(W * H, 0.0);
        pOro.assign(W * H, 0.0);
        divSm.assign(W * H, 0.0);
        capEff.assign(W * H, 0.0);
        pSpd.assign(W * H, 0.0);
        pCapSkin.assign(W * H, 0.0);
        pSupply.assign(W * H, 0.0);
        pAfford.assign(W * H, 0.0);
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
        // FLUX FORM. The boundary layer's heat rides on this substep's own
        // mass fluxes: the prognostic is thickness times temperature, and
        // it moves with the same face fluxes, the same relaxation, the
        // same export and deposit as the thickness itself. A temperature
        // equation against a fixed capacity could not see the heat that
        // leaves with mass exported through the top or arrives with mass
        // converging to replace it, and that blindness summed to a 9 W/m2
        // sink over the planet -- absorbed sunlight exceeding emitted
        // longwave by that much in a steady state, with every greenhouse
        // lever being pushed against it. Doing it hourly on the banked
        // fluxes needed a limiter where the meridians crowd, and a limiter
        // that scales heat but not mass left cells holding the heat of air
        // that had gone: 70 degC and a runaway. On the substep the
        // dynamics' own stability is the limiter.
        //
        // The relaxation's mass is a redistribution -- taken from columns
        // standing above their thermal target, given to those below -- and
        // it carries heat like any other mass: out at the column's own
        // temperature, in at the pooled temperature of what was taken.
        double tRelaxPool = 0;
        {
            double neg = 0, negT = 0;
            for (int y = 1; y < H - 1; y++) {
                double cw = std::cos(((y + 0.5) / (double)H - 0.5) * 3.14159265);
                for (int x = 0; x < W; x++) {
                    int i = idx(x, y);
                    tSub[i] = hT[i] / (H_LAYER + hP[i]);
                    double r = hWant[i] - hP[i];
                    if (r < 0) { neg -= r * cw; negT -= r * cw * tSub[i]; }
                }
            }
            for (int x = 0; x < W; x++) {
                tSub[idx(x, 0)] = tSub[idx(x, 1)];
                tSub[idx(x, H - 1)] = tSub[idx(x, H - 2)];
            }
            tRelaxPool = neg > 0 ? negT / neg : 0.0;
        }
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
                double relax = (want - hP[i]) / THERM_TAU;
                double wExp = W_EXPORT * std::max(wTop[i], 0.0);
                double wDep = W_EXPORT * std::max(wTopMean, 0.0);
                double raw = hP[i] + dt * (conv + relax - wExp + wDep);
                nhP[i] = std::clamp(raw, -0.5 * H_LAYER, 0.5 * H_LAYER);
                // And the heat on every one of those masses (see FLUX FORM
                // above): upwind on the faces; the relaxation's out at this
                // column's temperature and in at the pool's; export out at
                // this column's; the deposit in warmed by the gap -- where
                // the layer is stirred enough to take it (stabArr, from the
                // surface's stability), and at the layer's own temperature
                // where it is not, its warmth staying aloft in the free
                // troposphere's books.
                double tE = fluxE > 0 ? tSub[i] : tSub[xe], tW = fluxW > 0 ? tSub[xw] : tSub[i];
                double tN = fluxN > 0 ? tSub[i] : tSub[yn], tS = fluxS > 0 ? tSub[ys] : tSub[i];
                double qconv = -((fluxE * tE - fluxW * tW) / (2 * dx) +
                                 (fluxN * tN - fluxS * tS) / (2 * dy));
                double st = stabArr[i];
                double q = qconv + (relax > 0 ? relax * tRelaxPool : relax * tSub[i]) -
                           wExp * tSub[i] +
                           wDep * (st * (Tf[i] + GAP_BF) + (1.0 - st) * tSub[i]);
                nhT[i] = hT[i] + dt * q;
                // If the thickness was clamped, the heat keeps its ratio:
                // the temperature is what the clamp must not change.
                if (nhP[i] != raw && H_LAYER + raw > 1.0)
                    nhT[i] *= (H_LAYER + nhP[i]) / (H_LAYER + raw);
            }
        }
        for (int x = 0; x < W; x++) {
            nu2[idx(x, 0)] = nv2[idx(x, 0)] = nu2[idx(x, H - 1)] = nv2[idx(x, H - 1)] = 0.0;
            nhP[idx(x, 0)] = nhP[idx(x, 1)];
            nhP[idx(x, H - 1)] = nhP[idx(x, H - 2)];
            nhT[idx(x, 0)] = nhT[idx(x, 1)];
            nhT[idx(x, H - 1)] = nhT[idx(x, H - 2)];
        }
        std::swap(u, nu2);
        std::swap(v, nv2);
        if (QG2GEO && qggInit) {
            // The mesh weather's wind is the wind poleward of the tropics
            // (see QG2GEO): this layer's own momentum stands only where the
            // QG approximation does not, and the heat rides the storms.
            for (int i = 0; i < W * H; i++) {
                double la = std::fabs(latRad[i] * 180.0 / 3.14159265);
                double w = std::clamp((la - qg2geo::QG_EQ) / QG2_BLEND_DEG, 0.0, 1.0);
                int j = meshOfCell[i];
                u[i] = (1 - w) * u[i] + w * qgg.u2[j];
                v[i] = (1 - w) * v[i] + w * qgg.v2[j];
            }
        }
        std::swap(hP, nhP);
        std::swap(hT, nhT);
        polarFilter(u, true);
        polarFilter(v, true);
        polarFilter(hP, true);
    }

    // One hour. doy in [0,365), hourOfDay in [0,24).
    // PRESCRIBED: paint this hour's surface temperature and wind. The wind
    // is the belt pattern plus the Ekman-balanced flow down the gradient of
    // the thermal anomaly -- the temperature's departure from its zonal
    // mean, twice smoothed -- which is the old diagnostic model's monsoon:
    // summer continents draw the sea's air in, winter continents push it
    // out. The air above follows the surface at fixed gaps, and the ice is
    // wherever the sea is at freezing.
    // The painting: the surface and air temperatures, the ice and the
    // belt wind from the Earth targets (see PRESCRIBED), every hour when
    // PRESCRIBED, once as the initial state otherwise (see PAINT_INIT).
    void paintHour(double doy, double hour) {
        double dx0 = 2 * 3.14159265 * R_EARTH / W;
        double dy = 3.14159265 * R_EARTH / H;
        painted = true;
#pragma omp parallel for
        for (int i = 0; i < W * H; i++) {
            T[i] = pre.surfaceT(i, water[i] != 0, elev[i], doy, hour);
            // Over the sea the air is a little cooler than the water -- and
            // in winter a lot cooler, because it came off the continent.
            // Earth's 45N ocean evaporates 2.5-3 mm/day mostly in cold-air
            // outbreaks; painted 1.5 K under the sea all year it managed
            // half that, and the temperate column stayed too dry to rain.
            double chill = 0.0;
            if (water[i])
                chill = std::min(0.25 * std::max(T[i] - pre.landZonalT(i, doy), 0.0), 6.0);
            Tb[i] = T[i] - BL_LAPSE - (water[i] ? 1.5 + chill : 2.0);
            Tf[i] = Tb[i] - 40.0;
            ice[i] = (water[i] && T[i] <= SEA_FREEZE + 0.05) ? 1.0 : 0.0;
            anomA[i] = T[i] + PRE_LAPSE * std::max((double)elev[i], 0.0) / 1000.0;
        }
        for (int y = 0; y < H; y++) {
            double m = 0;
            for (int x = 0; x < W; x++) m += anomA[y * W + x];
            m /= W;
            for (int x = 0; x < W; x++) {
                double a = anomA[y * W + x] - m;
                a = a > 0 ? a : a * PRE_COLD_SHARE;
                // and the ocean's subtropical high (see PRE_SUBTROP_HIGH_K)
                if (water[y * W + x]) {
                    double la = std::fabs(pre.latDeg[y * W + x]) - PRE_SUBTROP_LAT;
                    a -= PRE_SUBTROP_HIGH_K *
                         std::exp(-(la * la) / (PRE_SUBTROP_WIDTH * PRE_SUBTROP_WIDTH));
                }
                anomA[y * W + x] = a;
            }
        }
        for (int pass = 0; pass < 2; pass++) {
#pragma omp parallel for
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++) {
                    int i = idx(x, y);
                    int yn = std::min(y + 1, H - 1), ys = std::max(y - 1, 0);
                    anomB[i] = 0.5 * anomA[i] +
                               0.125 * (anomA[idx(wrapX(x + 1), y)] + anomA[idx(wrapX(x - 1), y)] +
                                        anomA[idx(x, yn)] + anomA[idx(x, ys)]);
                }
            std::swap(anomA, anomB);
        }
#pragma omp parallel for
        for (int y = 1; y < H - 1; y++) {
            double cosl = std::max(std::cos((((y + 0.5) / (double)H) - 0.5) * 3.14159265), 0.2);
            double dx = dx0 * cosl;
            for (int x = 0; x < W; x++) {
                int i = idx(x, y);
                double px = -PRE_P_PER_DEG *
                            (anomA[idx(wrapX(x + 1), y)] - anomA[idx(wrapX(x - 1), y)]) / (2 * dx);
                double py =
                    -PRE_P_PER_DEG * (anomA[idx(x, y + 1)] - anomA[idx(x, y - 1)]) / (2 * dy);
                double X = -px / RHO, Y = -py / RHO;
                double f = 2 * OMEGA * std::sin(latRad[i]);
                double r = PRE_FRICTION, den = r * r + f * f;
                double bu, bv;
                pre.beltWind(i, doy, bu, bv);
                double au = (r * X + f * Y) / den, av = (-f * X + r * Y) / den;
                double am = std::sqrt(au * au + av * av);
                if (am > PRE_ANOM_WIND_MAX) {
                    au *= PRE_ANOM_WIND_MAX / am;
                    av *= PRE_ANOM_WIND_MAX / am;
                }
                u[i] = bu + au;
                v[i] = bv + av;
            }
        }
        for (int x = 0; x < W; x++)
            u[idx(x, 0)] = v[idx(x, 0)] = u[idx(x, H - 1)] = v[idx(x, H - 1)] = 0.0;
    }

    // The painting, then the dynamics hooks, which run either way.
    void prescribeHour(double doy, double hour) {
        if (PRESCRIBED || (PAINT_INIT && !painted)) paintHour(doy, hour);
        if (DYN2) {
            // The two-level atmosphere makes the wind instead (see DYN2):
            // its levels are relaxed toward the painted near-surface air,
            // and its lower level's wind is what the water rides on.
            if (tnsBuf.empty()) tnsBuf.assign(W * H, 0.0);
            // the near-surface air, reduced to sea level: a plateau's cold
            // surface is not a cold column at 2.5 km beside the lowland's
            for (int i = 0; i < W * H; i++)
                tnsBuf[i] = T[i] - (water[i] ? 1.5 : 2.0) + (water[i] ? 0.0 : 6.5 * std::max((double)elev[i], 0.0) / 1000.0);
            if (!d2init) {
                d2.init(W, H, elev, latRad, water);
                d2.setTargets(tnsBuf, true);
                d2init = true;
            } else {
                d2.setTargets(tnsBuf, false);
            }
            d2.refreshExner();
            static int dbgHours = 0;
            bool dbg = std::getenv("HH_DYN_DEBUG") && dbgHours < 36;
            if (dbg) {
                double tmn = 1e9, tmx = -1e9, thmn = 1e9, thmx = -1e9;
                for (int i = 0; i < W * H; i++) {
                    tmn = std::min(tmn, tnsBuf[i]); tmx = std::max(tmx, tnsBuf[i]);
                    thmn = std::min(thmn, d2.th1t[i]); thmx = std::max(thmx, d2.th1t[i]);
                }
                fprintf(stderr, "DYN hour %d: tns [%.1f, %.1f]  th1t [%.1f, %.1f]\n", dbgHours, tmn, tmx, thmn, thmx);
            }
            for (int k = 0; k < DYN2_SUBSTEPS; k++) {
                d2.step(dyn2::DT, &Model::d2Filter, this);
                if (dbg && k == DYN2_SUBSTEPS - 1) {
                    double pmn = 1e9, pmx = -1e9, umx = 0; int iu = 0;
                    for (int i = 0; i < W * H; i++) {
                        pmn = std::min(pmn, d2.ps[i]); pmx = std::max(pmx, d2.ps[i]);
                        double sp = std::fabs(d2.u1[i]) + std::fabs(d2.v1[i]);
                        if (sp > umx) { umx = sp; iu = i; }
                    }
                    fprintf(stderr, "  hour %2d: ps [%.1f, %.1f] hPa  |u1| max %.2f at x %d y %d (lon %.0f lat %.0f)  th1 [%.0f, %.0f]\n",
                            dbgHours, pmn / 100, pmx / 100, umx, iu % W, iu / W, ((iu % W) + 0.5) / W * 360.0 - 180.0,
                            ((iu / W) + 0.5) / (double)H * 180.0 - 90.0,
                            *std::min_element(d2.th1.begin(), d2.th1.end()), *std::max_element(d2.th1.begin(), d2.th1.end()));
                }
            }
            if (dbg) { dbgHours++; if (dbgHours >= 36 && std::getenv("HH_DYN_STOP")) std::exit(0); }
            for (int i = 0; i < W * H; i++) { u[i] = d2.u1[i]; v[i] = d2.v1[i]; }
            d2.bank();
            d2.bankEddy();
        }
        if (QG2) {
            // The QG weather makes the wind poleward of the tropics (see
            // QG2). Its thickness target is the painted near-surface air,
            // reduced to sea level; its lower layer's wind replaces the
            // painted belts inside the channels, fading into them over
            // QG2_BLEND_DEG at the equatorward wall.
            if (tnsBuf.empty()) tnsBuf.assign(W * H, 0.0);
            for (int i = 0; i < W * H; i++)
                tnsBuf[i] = T[i] - (water[i] ? 1.5 : 2.0) + (water[i] ? 0.0 : 6.5 * std::max((double)elev[i], 0.0) / 1000.0);
            if (!qgInit) {
                qg.init(W, H, elev, latRad, water);
                qg.setTargets(tnsBuf, true);
                // a seed for the eddies
                for (int y = 0; y < H; y++)
                    if (qg.inChannel(y))
                        for (int x = 0; x < W; x++) qg.q2[idx(x, y)] += 1e-6 * std::sin(5.0 * 2 * 3.14159265 * (x + 0.5) / W + 0.7 * y);
                qgInit = true;
            } else {
                qg.setTargets(tnsBuf, false);
            }
            for (int k = 0; k < (int)(DT / qg2::DT); k++) qg.step(qg2::DT);
            if (std::getenv("HH_QG_DEBUG")) {
                static int qgHours = 0; qgHours++;
                double m1 = 0, m2 = 0; int bad = -1, i1 = 0, i2 = 0;
                for (int i = 0; i < W * H; i++) {
                    if (!std::isfinite(qg.u2[i]) || !std::isfinite(qg.u1[i])) { bad = i; break; }
                    double a1 = std::fabs(qg.u1[i]) + std::fabs(qg.v1[i]), a2 = std::fabs(qg.u2[i]) + std::fabs(qg.v2[i]);
                    if (a1 > m1) { m1 = a1; i1 = i; }
                    if (a2 > m2) { m2 = a2; i2 = i; }
                }
                if (bad >= 0) { fprintf(stderr, "QG non-finite at hour %d, cell %d (x %d y %d)\n", qgHours, bad, bad % W, bad / W); std::exit(1); }
                if (qgHours % 24 == 0 || qgHours > 2690)
                    fprintf(stderr, "QG day %d h %d |u up| %.1f at (%d, %.0f)  |u low| %.1f at (%d, %.0f)\n", qgHours / 24, qgHours, m1, i1 % W,
                            latRad[i1] * 180 / 3.14159265, m2, i2 % W, latRad[i2] * 180 / 3.14159265);
            }
            for (int i = 0; i < W * H; i++) {
                double la = std::fabs(latRad[i] * 180.0 / 3.14159265);
                double w = std::clamp((la - qg2::QG_EQ) / QG2_BLEND_DEG, 0.0, 1.0) *
                           std::clamp((qg2::QG_CAP - la) / QG2_BLEND_DEG, 0.0, 1.0);
                u[i] = (1 - w) * u[i] + w * qg.u2[i];
                v[i] = (1 - w) * v[i] + w * qg.v2[i];
            }
            qg.bank();
        }
        if (QG2GEO) {
            // The mesh weather (see QG2GEO): the painted near-surface air,
            // reduced to sea level, goes onto the mesh by the cell under
            // each mesh cell; the lower layer's wind comes back by the
            // nearest mesh cell and replaces the belts poleward of the
            // tropics, fading in over QG2_BLEND_DEG.
            if (tnsBuf.empty()) tnsBuf.assign(W * H, 0.0);
            for (int i = 0; i < W * H; i++)
                tnsBuf[i] = T[i] - (water[i] ? 1.5 : 2.0) + (water[i] ? 0.0 : 6.5 * std::max((double)elev[i], 0.0) / 1000.0);
            auto dirOf = [&](int i) {
                double la = latRad[i], lo = ((i % W) + 0.5) / W * 2 * 3.14159265 - 3.14159265;
                return geodesic::D3{std::cos(la) * std::cos(lo), std::cos(la) * std::sin(lo), std::sin(la)};
            };
            if (!qggInit) {
                geodesic::Grid probe = geodesic::build(qg2geo::LEVEL);
                int M = probe.size();
                cellOfMesh.assign(M, 0);
                std::vector<float> mElev(M, 0.0f); std::vector<unsigned char> mWater(M, 1);
                for (int j = 0; j < M; j++) {
                    const geodesic::D3& c = probe.c[j];
                    double la = std::asin(std::clamp(c.z, -1.0, 1.0)), lo = std::atan2(c.y, c.x);
                    int x = std::clamp((int)((lo + 3.14159265) / (2 * 3.14159265) * W), 0, W - 1);
                    int y = std::clamp((int)((la / 3.14159265 + 0.5) * H), 0, H - 1);
                    cellOfMesh[j] = idx(x, y);
                    mElev[j] = elev[cellOfMesh[j]]; mWater[j] = water[cellOfMesh[j]];
                }
                meshOfCell.assign(W * H, 0);
#pragma omp parallel for
                for (int i = 0; i < W * H; i++) {
                    geodesic::D3 d = dirOf(i);
                    int best = 0; double bd = -2;
                    for (int j = 0; j < M; j++) { double t = geodesic::dot(d, probe.c[j]); if (t > bd) { bd = t; best = j; } }
                    meshOfCell[i] = best;
                }
                qgg.init(mElev, mWater);
                qggElev = mElev; qggWater = mWater;
                meshCount.assign(M, 0);
                for (int i = 0; i < W * H; i++) meshCount[meshOfCell[i]]++;
                qggT.assign(M, 0.0);
                for (int j = 0; j < M; j++) qggT[j] = tnsBuf[cellOfMesh[j]];
                qgg.setTargets(qggT, true);
                for (int j = 0; j < M; j++) qgg.q2[j] += 1e-6 * std::sin(5.0 * qgg.lon[j] + 0.7 * qgg.lat[j] * 180 / 3.14159265);
                qggInit = true;
            } else {
                for (int j = 0; j < qgg.N; j++) qggT[j] = tnsBuf[cellOfMesh[j]];
                qgg.setTargets(qggT, false);
            }
            if (WATER2) {
                int M = qgg.N;
                if (evapBuf.empty()) { evapBuf.assign(W * H, 0.0); wUpBuf.assign(W * H, 0.0); wConvBuf.assign(W * H, 0.0); w2Evap.assign(M, 0.0); w2Wup.assign(M, 0.0); w2Conv.assign(M, 0.0); }
                if (!w2.started) w2.init(qgg, qggElev, qggWater);
                // last hour's evaporation and painted ascent, averaged over
                // the cells under each mesh cell
                std::fill(w2Evap.begin(), w2Evap.end(), 0.0); std::fill(w2Wup.begin(), w2Wup.end(), 0.0); std::fill(w2Conv.begin(), w2Conv.end(), 0.0);
                for (int i = 0; i < W * H; i++) { int j = meshOfCell[i]; w2Evap[j] += evapBuf[i]; w2Wup[j] += wUpBuf[i]; w2Conv[j] += wConvBuf[i]; }
                for (int j = 0; j < M; j++) {
                    if (meshCount[j] > 0) { w2Evap[j] /= meshCount[j]; w2Wup[j] /= meshCount[j]; w2Conv[j] /= meshCount[j]; }
                    else { w2Evap[j] = evapBuf[cellOfMesh[j]]; w2Wup[j] = wUpBuf[cellOfMesh[j]]; w2Conv[j] = wConvBuf[cellOfMesh[j]]; }
                }
                for (int j = 0; j < M; j++) qggT[j] = Tb[cellOfMesh[j]];
                w2.setInputs(qggT, w2Evap, w2Wup, w2Conv);
                for (int k = 0; k < (int)(DT / qg2geo::DT); k++) { w2.beginStep(); qgg.step(qg2geo::DT); w2.step(qg2geo::DT); }
            } else {
                for (int k = 0; k < (int)(DT / qg2geo::DT); k++) qgg.step(qg2geo::DT);
            }
            if (std::getenv("HH_QG_DEBUG")) {
                static int qgHours = 0; qgHours++;
                double m1 = 0, m2 = 0; int bad = -1, i1 = 0, i2 = 0;
                if (WATER2 && qgHours % 24 == 0) {
                    double zw[6] = {0, 0, 0, 0, 0, 0}, za = 0, lw = 0, lu = 0, la2 = 0;   // 30-60N: wl, wu, rainL, rainU, lift
                    for (int j = 0; j < qgg.N; j++) {
                        double la = qgg.lat[j] * 180 / 3.14159265, a = qgg.g.area[j];
                        if (la > 30 && la < 60) { zw[0] += w2.wl[j] * a; zw[1] += w2.wu[j] * a; zw[2] += w2.rainLHour[j] * a; zw[3] += w2.rainUHour[j] * a; zw[4] += w2.liftHour[j] * a; za += a; }
                        lw += w2.wl[j] * a; lu += w2.wu[j] * a; la2 += a;
                    }
                    fprintf(stderr, "W2 day %d  30-60N: wl %.1f wu %.1f mm, rain low %.2f up %.2f mm/h, lift %.3f mm/h | global wl %.1f wu %.1f\n",
                            qgHours / 24, zw[0] / za, zw[1] / za, zw[2] / za, zw[3] / za, zw[4] / za, lw / la2, lu / la2);
                }
                for (int j = 0; j < qgg.N; j++) {
                    if (!std::isfinite(qgg.u2[j]) || !std::isfinite(qgg.u1[j])) { bad = j; break; }
                    double a1 = geodesic::len(qgg.V1[j]), a2 = geodesic::len(qgg.V2[j]);
                    if (a1 > m1) { m1 = a1; i1 = j; }
                    if (a2 > m2) { m2 = a2; i2 = j; }
                }
                if (bad >= 0) { fprintf(stderr, "QG non-finite at hour %d, mesh cell %d (lat %.0f)\n", qgHours, bad, qgg.lat[bad] * 180 / 3.14159265); std::exit(1); }
                if (qgHours % 24 == 0) {
                    double zn = 0, zs = 0, an = 0, as = 0, en = 0, es = 0;
                    for (int j = 0; j < qgg.N; j++) {
                        double la = qgg.lat[j] * 180 / 3.14159265;
                        if (la > 40 && la < 55) { zn += qgg.u2[j] * qgg.g.area[j]; en += qgg.u1[j] * qgg.g.area[j]; an += qgg.g.area[j]; }
                        if (la < -40 && la > -55) { zs += qgg.u2[j] * qgg.g.area[j]; es += qgg.u1[j] * qgg.g.area[j]; as += qgg.g.area[j]; }
                    }
                    fprintf(stderr, "QG day %d |V up| %.1f at lat %.0f  |V low| %.1f at lat %.0f  %d iterations  u 40-55N low %.1f up %.1f  S low %.1f up %.1f\n",
                            qgHours / 24, m1, qgg.lat[i1] * 180 / 3.14159265, m2, qgg.lat[i2] * 180 / 3.14159265, qgg.solveIterations,
                            zn / an, en / an, zs / as, es / as);
                }
            }
            for (int i = 0; i < W * H; i++) {
                double la = std::fabs(latRad[i] * 180.0 / 3.14159265);
                double w = std::clamp((la - qg2geo::QG_EQ) / QG2_BLEND_DEG, 0.0, 1.0);
                int j = meshOfCell[i];
                u[i] = (1 - w) * u[i] + w * qgg.u2[j];
                v[i] = (1 - w) * v[i] + w * qgg.v2[j];
            }
            qgg.bank();
        }
    }

    // THE HOUR, IN STAGES (work order 02). `step` is the list; each stage
    // is one function, named for what it computes, taking what the hour
    // shares (HourCtx), what the row shares (RowCtx) and the cell's own
    // terms (CellHour), which are handed from stage to stage in the order
    // the stages run. The probes read them once the stages are done.
    struct HourCtx {
        double doy = 0, hour = 0;
        double dec = 0;            // the sun's declination, rad
        double dx0 = 0, dy = 0;    // m: a cell's width at the equator, and its height
        double C_BL = 0, C_FT = 0; // J/m2/K: the two layers' heat capacities
    };
    struct RowCtx {
        int y = 0;
        double dx = 0;           // m, this row's cell width
        double fN = 0, fS = 0;   // the north and south faces' lengths, relative to this row
        double ktx = 0, kty = 0; // the eddy diffusion, as a fraction per hour, each way
    };
    struct CellHour {
        int i = 0, x = 0, y = 0, xe = 0, xw = 0, yn = 0, ys = 0;
        double dx = 0, fN = 0, fS = 0, ktx = 0, kty = 0;
        double lat = 0, alb = 0, cf = 0, inc = 0, swAir = 0, sw = 0; // solarAt
        double lwUp = 0, lwDown = 0, olr = 0, em = 0;                // radiationAt
        double eBdn = 0, eBup = 0, eFdn = 0, eFup = 0, absB = 0, absF = 0;
        double heatHere = 0, spd = 0, stab = 0, sens = 0; // surfaceExchangeAt
        double cap = 0, capSkin = 0, evap = 0, lFlux = 0; // evaporationAt
        double difT = 0, difTf = 0, advT = 0;             // layerBudgetsAt
        double swB = 0, swF = 0, condense = 0, xch = 0;
    };

    HourCtx hourContext(double doy, double hour) const {
        HourCtx h;
        h.doy = doy;
        h.hour = hour;
        h.dec = 23.5 * 3.14159265 / 180.0 * std::cos(2 * 3.14159265 * (doy - 171.0) / 365.0);
        h.dx0 = 2 * 3.14159265 * R_EARTH / W; // m at equator
        h.dy = 3.14159265 * R_EARTH / H;
        // The layers' heat capacities: the moving layer is its own mass,
        // the free troposphere is the rest of the column.
        h.C_BL = RHO * CP_AIR * H_LAYER;
        h.C_FT = C_AIR - h.C_BL;
        return h;
    }

    RowCtx rowContext(const HourCtx& h, int y) const {
        RowCtx r;
        r.y = y;
        double cosl = std::max(std::cos((((y + 0.5) / (double)H) - 0.5) * 3.14159265), 0.2);
        r.dx = h.dx0 * cosl;
        // Meridians converge, and a flux per unit area does not. Cells at
        // 80 degrees hold a seventh of the area of cells at the equator, so
        // moving a kg per square metre out of a big cell and into a small
        // one CREATES water -- and heat, in the temperature diffusion. The
        // spherical divergence weights each face by its own length; these
        // are those weights, relative to this row.
        double cosN = std::max(std::cos(((y + 1.0) / (double)H - 0.5) * 3.14159265), 0.05);
        double cosS = std::max(std::cos(((y + 0.0) / (double)H - 0.5) * 3.14159265), 0.05);
        r.fN = cosN / cosl;
        r.fS = cosS / cosl;
        r.ktx = std::min(KT_DIFF * DT / (r.dx * r.dx), 0.22);
        r.kty = std::min(KT_DIFF * DT / (h.dy * h.dy), 0.22);
        return r;
    }

    void cellSetup(const RowCtx& r, int x, CellHour& c) const {
        c.x = x;
        c.y = r.y;
        c.i = idx(x, r.y);
        c.xe = idx(wrapX(x + 1), r.y);
        c.xw = idx(wrapX(x - 1), r.y);
        // The cap rows are a copy of this one, and their air
        // temperature is never integrated at all -- it sits wherever
        // the allocator left it, near zero, which the row below then
        // diffuses towards. Referring to the cell itself instead
        // gives a zero gradient, which is what no flux through the
        // pole means for everything, not only the water.
        c.yn = (r.y + 1 >= H - 1) ? c.i : idx(x, r.y + 1);
        c.ys = (r.y - 1 <= 0) ? c.i : idx(x, r.y - 1);
        c.dx = r.dx;
        c.fN = r.fN;
        c.fS = r.fS;
        c.ktx = r.ktx;
        c.kty = r.kty;
        c.lat = latRad[c.i];
    }

    // Sea-level-equivalent temperature: radiation, diffusion, pressure,
    // and the storm gradient all operate on it, so equilibrium surface
    // temperature naturally sits 6.5 C/km below the lowlands and plateau
    // cliffs create neither false mixing nor phantom storm tracks. The
    // surface processes (evaporation, capacity, snow) use actual T.
    void reduceToSeaLevel() {
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
        }
    }

    // What rose out of the layer last hour comes back down as the mean,
    // so the export moves mass about without creating or destroying it.
    // The upper branch is one well-mixed pool: it receives each column's
    // detrained air at that column's free-troposphere temperature and
    // hands the same mass back everywhere, so its temperature is the
    // export-weighted mean of Tf (see GAP_BF).
    void upperPoolMean() {
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

    // The air moves itself: six ten-minute steps of thickness and wind
    // inside this hour of radiation and water (see PRESCRIBED for when it
    // does not).
    void moveAir() {
        thermalTarget();
        // The heat goes into the dynamics as thickness times temperature
        // (see FLUX FORM in stepDynamics) and comes back out as the ratio.
        for (int i = 0; i < W * H; i++) hT[i] = (H_LAYER + hP[i]) * Tb[i];
        for (int k = 0; k < DYN_SUBSTEPS; k++) stepDynamics(DT / DYN_SUBSTEPS);
        // and once, now the hour is done, the anisotropy.
        polarFilter(u, false);
        polarFilter(v, false);
        polarFilter(hP, false);
        polarFilter(hT, false);
        for (int i = 0; i < W * H; i++) Tb[i] = hT[i] / (H_LAYER + hP[i]);
    }

    // Divergence -> uplift; orographic uplift from wind into slope.
    void divergenceAndOrography(const HourCtx& h) {
#pragma omp parallel for
        for (int y = 1; y < H - 1; y++) {
            double cosl = std::max(std::cos((((y + 0.5) / (double)H) - 0.5) * 3.14159265), 0.2);
            double dx = h.dx0 * cosl;
            for (int x = 0; x < W; x++) {
                int i = idx(x, y);
                double dudx = (u[idx(wrapX(x + 1), y)] - u[idx(wrapX(x - 1), y)]) / (2 * dx);
                double dvdy = (v[idx(x, y + 1)] - v[idx(x, y - 1)]) / (2 * h.dy);
                double wup = -(dudx + dvdy) * H_FLOW;
                // W_OROG is the windward fraction -- only part of a cell's
                // flow actually climbs the slope. It was applied before the
                // rain rebuild moved orography in here, and dropped in the
                // move; with honest winds the term tripled, mountains rained
                // at 4 mm/day and the columns downwind were left dry.
                double oro =
                    W_OROG *
                    (u[i] * (elev[idx(wrapX(x + 1), y)] - elev[idx(wrapX(x - 1), y)]) / (2 * dx) +
                     v[i] * (elev[idx(x, y + 1)] - elev[idx(x, y - 1)]) / (2 * h.dy));
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
#pragma omp parallel for
        for (int y = 1; y < H - 1; y++)
            for (int x = 0; x < W; x++) {
                int i = idx(x, y);
                divTmp[i] =
                    0.5 * div[i] + 0.125 * (div[idx(wrapX(x + 1), y)] + div[idx(wrapX(x - 1), y)] +
                                            div[idx(x, y + 1)] + div[idx(x, y - 1)]);
            }
        for (int x = 0; x < W; x++) {
            divTmp[idx(x, 0)] = div[idx(x, 0)];
            divTmp[idx(x, H - 1)] = div[idx(x, H - 1)];
        }
        div.swap(divTmp);
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
        for (int i = 0; i < W * H; i++) divSm[i] += (div[i] - divSm[i]) * (DT / UPLIFT_TAU);
    }

    // Sunlight, in the order it actually meets things: cloud tops
    // reflect, the column absorbs its share of what gets through, and
    // the ground takes what is left.
    void solarAt(const HourCtx& h, CellHour& c) const {
        const int i = c.i;
        double ha = 2 * 3.14159265 * (h.hour / 24.0 + (c.x + 0.5) / (double)W) + 3.14159265;
        double cosz =
            std::sin(c.lat) * std::sin(h.dec) + std::cos(c.lat) * std::cos(h.dec) * std::cos(ha);
        // Snow on land still follows the temperature ramp; sea ice
        // follows its own cover (see L_ICE).
        double white =
            water[i] ? std::clamp(ice[i] / ICE_FULL_COVER, 0.0, 1.0)
                     : std::clamp((SNOW_NONE_C - T[i]) / (SNOW_NONE_C - SNOW_FULL_C), 0.0, 1.0);
        c.alb = albedo[i] + white * ((water[i] ? ALBEDO_SEAICE : ALBEDO_SNOW) - albedo[i]);
        // What the cloud overhead is doing, both ways. It was
        // drawn on the map and nowhere else until now.
        c.cf = cloudF[i];
        c.inc = SOLAR * std::max(cosz, 0.0) * (1.0 - CLOUD_ALB * c.cf);
        c.swAir = c.inc * SW_ATM;
        c.sw = (c.inc - c.swAir) * (1.0 - c.alb);
    }

    // Longwave, both ways. The air holds EMISS of what the ground
    // sends up and radiates that much again from each of its two
    // faces: half to space, half back down. The half coming down
    // is the greenhouse, and it is what the old budget had no way
    // to express.
    void radiationAt(CellHour& c) const {
        const int i = c.i;
        double Tk = T[i] + 273.15, Tbk = Tb[i] + 273.15, Tfk = Tf[i] + 273.15;
        c.lwUp = SIGMA * Tk * Tk * Tk * Tk;
        // Two layers (see BL_LAPSE, GAP_BF) in two bands (see
        // WINDOW_FRAC). Each band's optical depth is split between
        // the layers by where its absorber lives -- band vapour by
        // the water share, the window continuum by its own, deeper
        // share -- so the column's total transmission in each band
        // is exactly what the anchors fixed. The cloud is grey: it
        // closes both bands alike, its base in the boundary layer
        // and its top in the free troposphere, half its depth to
        // each -- which is what makes a cloudy night warm and a
        // cloud top cold, without a special case for either.
        //
        // Each layer radiates from its two faces at those faces'
        // own temperatures (see FT_FACE): the boundary layer down
        // from its base and up from its top, the free troposphere
        // down from its base and up from its emitting level. Down
        // is warm and up is cold because the faces that look down
        // ARE the warm ones.
        double tauC = -std::log(1.0 - CLOUD_LW * c.cf);
        double tauBand = tauBandOf(Wv[i]), tauWin = tauWinOf(Wv[i]);
        const double frac[2] = {1.0 - WINDOW_FRAC, WINDOW_FRAC};
        const double tB[2] = {tauBand * BL_WV_SHARE + 0.5 * tauC,
                              tauWin * WIN_BL_SHARE + 0.5 * tauC};
        const double tF[2] = {tauBand * (1.0 - BL_WV_SHARE) + 0.5 * tauC,
                              tauWin * (1.0 - WIN_BL_SHARE) + 0.5 * tauC};
        auto planck = [](double tk) { return SIGMA * tk * tk * tk * tk; };
        double pBdn = planck(Tbk + BL_LAPSE), pBup = planck(Tbk - BL_LAPSE);
        double pFdn = planck(Tfk + FT_FACE), pFup = planck(Tfk);
        // Summed over the bands: what each layer emits from each
        // face, what each absorbs, and what reaches the ground and
        // space. Upward through the boundary layer, then through the
        // free troposphere; downward from space (nothing) down.
        double eBdn = 0, eBup = 0, eFdn = 0, eFup = 0, absB = 0, absF = 0;
        double olr = 0, lwDown = 0, up1 = 0, em = 0;
        for (int k = 0; k < 2; k++) {
            double emB = 1.0 - std::exp(-tB[k]), emF = 1.0 - std::exp(-tF[k]);
            double f = frac[k];
            double bdn = f * emB * pBdn, bup = f * emB * pBup;
            double fdn = f * emF * pFdn, fup = f * emF * pFup;
            double u1 = (1.0 - emB) * f * c.lwUp + bup; // leaving the BL top
            eBdn += bdn;
            eBup += bup;
            eFdn += fdn;
            eFup += fup;
            absB += emB * (f * c.lwUp + fdn);
            absF += emF * u1;
            up1 += u1;
            olr += (1.0 - emF) * u1 + fup;               // leaving the planet
            lwDown += bdn + (1.0 - emB) * fdn;           // arriving at the ground
            em += f * (1.0 - (1.0 - emB) * (1.0 - emF)); // the column, for the probe
        }
        c.eBdn = eBdn;
        c.eBup = eBup;
        c.eFdn = eFdn;
        c.eFup = eFup;
        c.absB = absB;
        c.absF = absF;
        c.olr = olr;
        c.lwDown = lwDown;
        c.em = em;
    }

    // The exchange coefficient, from the wind that is actually
    // blowing here. Surface wind is about seven tenths of the
    // layer's, and never less than the stirring convection does
    // on its own.
    void surfaceExchangeAt(CellHour& c) const {
        const int i = c.i;
        c.heatHere = (water[i] && ice[i] > 0.0) ? C_SEAICE : heatC[i];
        c.spd = std::sqrt(0.49 * (u[i] * u[i] + v[i] * v[i]) + U_GUST * U_GUST);
        double kExch = RHO_CP * (water[i] ? CH_SEA : CH_LAND) * c.spd;
        double lapseGap = T[i] - Tb[i] - BL_LAPSE;
        c.stab = lapseGap > 0 ? 1.0 : 1.0 / (1.0 + STAB_B * (-lapseGap));
        c.sens = kExch * c.stab * lapseGap;
    }

    // Evaporation, priced: what the air can still hold, what the
    // ground has to give, and what the sun can pay for.
    // Two different capacities, and confusing them was the bug.
    // What the air can HOLD is set by the air's own temperature;
    // what the surface OFFERS is the saturation humidity of the
    // skin, which is why a warm sea steams into cool air.
    void evaporationAt(const HourCtx& h, CellHour& c) {
        const int i = c.i;
        c.cap = capAirOf(Tb[i]);
        c.capSkin = capOf(T[i]);
        double supply = water[i] ? 1.0 : std::clamp(soil[i] / SOIL_REF_MM, 0.0, 1.0);
        // Same turbulence, same coefficient, humidity deficit in
        // place of temperature difference. The deficit is read off
        // the column through the depth Earth actually has.
        double rhCol = std::clamp(Wv[i] / std::max(c.cap, 0.05), 0.0, 1.0);
        double qAir = (1.0 - BL_DRYNESS * (1.0 - rhCol)) * c.cap;
        double dq = std::max(c.capSkin - qAir, 0.0) / Q_SCALE;
        double evap = RHO * (water[i] ? CH_SEA : CH_LAND) * c.spd * dq * supply * DT;
        pSpd[i] = c.spd;
        pCapSkin[i] = c.capSkin;
        pSupply[i] = supply;
        // What the sun pays over a whole day, not what it pays at noon:
        // capping against the instantaneous figure lets the daylight
        // hours evaporate three or four times a day's worth of water.
        double h0 = std::acos(std::clamp(-std::tan(c.lat) * std::tan(h.dec), -1.0, 1.0));
        double swDay = SOLAR / 3.14159265 *
                       (h0 * std::sin(c.lat) * std::sin(h.dec) +
                        std::cos(c.lat) * std::cos(h.dec) * std::sin(h0)) *
                       (1.0 - c.alb) * (1.0 - CLOUD_ALB * c.cf) * (1.0 - SW_ATM);
        double afford = std::max(swDay, 0.0) + (water[i] ? STORED_FLUX_WATER : STORED_FLUX_LAND);
        double lFlux = evap * LATENT_J_PER_KG / DT;
        pAfford[i] = lFlux > afford ? 1.0 : 0.0;
        if (lFlux > afford) {
            evap *= afford / lFlux;
            lFlux = afford;
        }
        c.evap = evap;
        c.lFlux = lFlux;
    }

    // The surface: land, an ice skin, or open water.
    void surfaceAndIceAt(CellHour& c) {
        const int i = c.i;
        double fSurf = c.sw - c.lwUp + c.lwDown - c.sens - c.lFlux; // into the surface
        double cond = 0.0;
        if (!water[i]) {
            nT[i] = std::clamp(T[i] + fSurf / c.heatHere * DT, -90.0, 65.0);
            nIce[i] = 0.0;
        } else if (ice[i] > 0.0) {
            // An ice skin over water at freezing (see L_ICE). Heat
            // conducts up through the thickness to a colder skin,
            // and what it conducts it freezes at the bottom; a skin
            // warmer than the water sends heat down and melts there.
            double hIce = ice[i];
            cond = ICE_K / std::max(hIce, 0.05) * (SEA_FREEZE - T[i]);
            double Tn = T[i] + (fSurf + cond) / C_SEAICE * DT;
            hIce += cond * DT / L_ICE;
            if (Tn > 0.0) { // the top melts, and the skin waits at zero
                hIce -= (Tn - 0.0) * C_SEAICE / L_ICE;
                Tn = 0.0;
            }
            if (hIce <= ICE_MIN) {                           // gone: the water beneath takes over
                double spare = std::max(-hIce, 0.0) * L_ICE; // melting energy left over
                hIce = 0.0;
                Tn = SEA_FREEZE + spare / C_WATER;
            }
            nT[i] = std::clamp(Tn, -90.0, 65.0);
            nIce[i] = std::min(hIce, ICE_MAX);
        } else {
            // Open water: the slab. A deficit below freezing is not a
            // colder sea, it is ice.
            double Tn = T[i] + fSurf / C_WATER * DT;
            if (Tn < SEA_FREEZE) {
                nIce[i] = (SEA_FREEZE - Tn) * C_WATER / L_ICE;
                Tn = SEA_FREEZE;
            } else {
                nIce[i] = 0.0;
            }
            nT[i] = std::clamp(Tn, -90.0, 65.0);
        }
    }

    // Two layers, two budgets (see GAP_BF).
    void layerBudgetsAt(const HourCtx& h, CellHour& c) {
        const int i = c.i;
        // The boundary layer's advection and vertical exchange
        // happened on the dynamics' substeps (see FLUX FORM in
        // stepDynamics); Tb already holds them. What is left here is
        // the physics and the eddies, against the mass that is
        // actually there.
        double hb0 = H_LAYER + hP[i];
        // With the mesh weather carrying this layer's heat on its
        // storms, the Budyko diffusion that stood in for the storms
        // would count them twice (measured on the geodesic branch,
        // 2026-09-02): the layer keeps KT_BL_SHARE of it.
        double kShare = (QG2GEO && qggInit) ? KT_BL_SHARE : 1.0;
        double kxa = c.ktx * kShare, kya = c.kty * kShare;
        c.difT = kxa * (Tb[c.xe] + Tb[c.xw] - 2 * Tb[i]) +
                 kya * (c.fN * (Tb[c.yn] - Tb[i]) + c.fS * (Tb[c.ys] - Tb[i]));
        // The free troposphere has no wind of its own here, so the
        // eddies are all it gets sideways -- at the full coefficient,
        // since it spends nothing on advection.
        c.difTf = c.ktx * (Tf[c.xe] + Tf[c.xw] - 2 * Tf[i]) +
                  c.kty * (c.fN * (Tf[c.yn] - Tf[i]) + c.fS * (Tf[c.ys] - Tf[i]));
        // (The upwind temperature-form advection that stood here --
        // and before it the whole column's temperature scaled by the
        // layer's mass share, and before that the column at full
        // strength, which froze the planet -- is replaced by the flux
        // form above: the same wind, carrying exactly the heat of the
        // mass it moves.)
        // The boundary layer: its share of the sunlight; what it
        // absorbs of the ground's face and of the free troposphere's
        // lower face, less its own two faces; the sensible heat the
        // ground gives it; its own wind and the eddies; and the
        // subsiding air the upper branch hands back, arriving
        // dry-adiabatically warmed by the gap.
        c.xch = RHO * CP_AIR * W_EXPORT;
        c.swB = c.swAir * BL_WV_SHARE;
        c.swF = c.swAir - c.swB;
        // In heat-content form (see FLUX FORM): thickness times
        // temperature, in and out with every mass that moves. Air
        // exported through the top leaves with the layer's own heat;
        // the relaxation's mass arrives at the layer's own
        // temperature; and the subsiding air arrives warmed by the
        // gap -- but only warms the layer if the layer is stirred
        // enough to mix it in. Over a surface colder than its air
        // the layer is stable, the warm air sits on top as a lid, and
        // the surface goes on cooling under it: the polar winter
        // inversion. The same stability factor the sensible heat
        // uses gates that entrainment; what is not mixed in arrives
        // at the layer's own temperature, and its warmth stays in
        // the free troposphere's books, which is where the lid is.
        nTb[i] =
            std::clamp(Tb[i] + c.difT +
                           (c.swB + c.absB - c.eBup - c.eBdn + c.sens) / (RHO * CP_AIR * hb0) * DT,
                       -95.0, 70.0);
        stabArr[i] = c.stab; // read by next hour's dynamics
        hbNew[i] = hb0;
        physRow[c.y] += c.swB + c.absB - c.eBup - c.eBdn + c.sens + c.swF + c.absF - c.eFup -
                        c.eFdn + rainStep[i] * LATENT_J_PER_KG / DT;
        // For the probes: the temperature the wind and the vertical
        // exchange would each have made on their own.
        c.advT = Tb[i] - TbPrev[i]; // what the dynamics did this hour
        // The free troposphere: the rest of the sunlight; what it
        // absorbs of everything coming up through the boundary
        // layer, less its two faces; the latent heat of every rain,
        // which condenses aloft; the eddies; the detrained air
        // arriving from below, cooled by the gap; and the pool's air
        // passing through on its way down.
        c.condense = rainStep[i] * LATENT_J_PER_KG / h.C_FT;
        nTf[i] = std::clamp(
            Tf[i] + (c.swF + c.absF - c.eFup - c.eFdn) / h.C_FT * DT + c.condense + c.difTf +
                // -- and the share of the subsiding air's warmth that
                // a stable boundary layer would not take (see stab,
                // above): it stays aloft, as the lid, so it stays in
                // this layer's books. Dropping it was a 15 W/m2 leak
                // out of the whole planet, measured at the top of
                // the atmosphere.
                c.xch *
                    (std::max(wTop[i], 0.0) * (Tb[i] - GAP_BF - Tf[i]) +
                     std::max(wTopMean, 0.0) * (tfPool - Tf[i]) +
                     (1.0 - c.stab) * std::max(wTopMean, 0.0) * (Tf[i] + GAP_BF - Tb[i])) /
                    h.C_FT * DT,
            -95.0, 70.0);
    }

    // PROBE: the tropical-ocean column, term by term. OLR is
    // what escapes the top: the window through the greenhouse
    // plus the air's own upward face. Then the layers' own budgets
    // (see TROPICAL OCEAN COLUMN).
    void probeColumnAt(const HourCtx& h, const CellHour& c) {
        const int i = c.i;
        if (!(recordBudget && water[i] && std::fabs(latRad[i]) < 0.2618)) return;
        double* b = &budRow[Climatology::NTB * c.y];
        b[0] += c.sw;
        b[1] += c.swAir;
        b[2] += c.lwUp;
        b[3] += c.lwDown;
        b[4] += c.sens;
        b[5] += c.lFlux;
        b[6] += c.olr;
        b[7] += T[i];
        b[8] += Tb[i];
        b[9] += Wv[i];
        b[10] += c.em;
        b[11] += c.cf;
        b[12] += Tf[i];
        b[13] += 1.0;
        b[14] += c.swB;
        b[15] += c.absB;
        b[16] += c.eBup + c.eBdn;
        b[17] += c.swF;
        b[18] += c.absF;
        b[19] += c.eFup + c.eFdn;
        b[20] += c.condense * h.C_FT / DT;
        b[21] += (c.difT + c.advT) * h.C_BL / DT;
        b[22] += c.difTf * h.C_FT / DT +
                 c.xch * (std::max(wTop[i], 0.0) * (Tb[i] - GAP_BF - Tf[i]) +
                          std::max(wTopMean, 0.0) * (tfPool - Tf[i]) +
                          (1.0 - c.stab) * std::max(wTopMean, 0.0) * (Tf[i] + GAP_BF - Tb[i]));
        b[23] += c.stab * c.xch * std::max(wTopMean, 0.0) * (Tf[i] + GAP_BF - Tb[i]);
    }

    // PROBE: the zonal budget, every term as W/m2 (see zonBud).
    void probeZonalAt(const HourCtx& h, const CellHour& c) {
        const int i = c.i;
        if (!recordBudget) return;
        double* z = &zonRow[((curSeason * 2 + (water[i] ? 0 : 1)) * H + c.y) * Climatology::NZB];
        z[0] += c.sw + c.swAir;
        z[1] += c.olr;
        z[2] += c.sw;
        z[3] += c.lwDown;
        z[4] += c.lwUp;
        z[5] += c.sens;
        z[6] += c.lFlux;
        z[7] += (c.difT + c.advT) * h.C_BL / DT; // temperature-change form
        z[8] += c.difTf * h.C_FT / DT;
        z[9] += c.stab * c.xch * std::max(wTopMean, 0.0) * (Tf[i] + GAP_BF - Tb[i]);
        z[10] += c.xch * std::max(wTop[i], 0.0) * (Tb[i] - GAP_BF - Tf[i]);
        z[11] += c.xch * std::max(wTopMean, 0.0) *
                 ((tfPool - Tf[i]) + (1.0 - c.stab) * (Tf[i] + GAP_BF - Tb[i]));
        z[12] += c.condense * h.C_FT / DT;
        z[13] += 1.0;
    }

    // PROBE: one cell's daily budget terms (see probe). One cell only:
    // no write contention.
    void probeCellAt(const CellHour& c) {
        if (c.i != probe) return;
        pSw += c.sw / c.heatHere * DT;
        pOlr -= (c.lwUp - c.lwDown) / c.heatHere * DT;
        pDif += c.difT;
    }

    // How fast the air here is rising, from every cause there is.
    void upliftAt(const HourCtx& h, const CellHour& c) {
        const int i = c.i;
        double gtx = (Tsl[c.xe] - Tsl[c.xw]) / (2 * c.dx),
               gty = (Tsl[c.yn] - Tsl[c.ys]) / (2 * h.dy);
        // (Orography is already in div[], from divergenceAndOrography.)
        // Convective: the heat the surface actually gives the air,
        // both ways it gives it. Latent is the larger by an order of
        // magnitude over warm water, and it is the half that makes a
        // thunderhead; sens alone left the ITCZ becalmed.
        // Gated by moist buoyancy (see CONV_BAR): the flux is the
        // fuel, and it only becomes deep convection where the skin
        // parcel's moist static energy clears the column's.
        double hSfc = CP_AIR * T[i] + LATENT_J_PER_KG * c.capSkin / Q_SCALE;
        double hEnv =
            CP_AIR * Tf[i] + CONV_BAR + LATENT_J_PER_KG * CONV_QSAT_LIFT * capOf(Tf[i]) / Q_SCALE;
        double buoy = std::clamp((hSfc - hEnv) / CONV_RAMP, 0.0, 1.0);
        double wConv = W_CONV * std::max(c.sens + c.lFlux, 0.0) * buoy;
        // Large-scale ascent where the flow converges, and frontal
        // lifting where warm air meets cold.
        // Signed: convergence lifts, divergence sinks, and the
        // subtropical deserts are the sinking half of the Hadley
        // cell. Taking only the positive part is what made the
        // rectifier that had to be dealt with above.
        double wDiv = W_DIVERGE * divSm[i] + (PRESCRIBED ? pre.beltUplift(i, h.doy) : 0.0);
        // Only where the wind carries warm air over cold ground:
        // warm advection is the classic condition for ascent, and
        // the warm air is what rises. Lifting both sides of a
        // gradient exported 75 W/m2 of air through the top of the
        // winter continent's boundary layer (against 26 over the
        // sea beside it), the surface wind converged from the warm
        // sea to fill the hole, 85 W/m2 of marine warmth landed in
        // the continental boundary layer, and 60N winter sat at -13
        // where Earth's is -25 -- with the same term digging the
        // 300 m low around the Antarctic coast. Cold air draining
        // off a continent now lifts nothing.
        //
        // Gated on warm ADVECTION it took the storm tracks with it:
        // with no eddies to carry air across a zonal gradient the
        // term went quiet over the oceans too, the subpolar lows
        // and the westerlies vanished, and the Antarctic fell to
        // -60. The gradient's magnitude is the eddy activity a model
        // without eddies can see (baroclinic growth goes as the
        // gradient), so it stays; the SIDE is what changes. Lift the
        // warm side of the gradient -- the cell standing above its
        // neighbours -- and not the cold one.
        double gmag = std::sqrt(gtx * gtx + gty * gty);
        double tNb = 0.25 * (Tsl[c.xe] + Tsl[c.xw] + Tsl[c.yn] + Tsl[c.ys]);
        // (With the climate painted there is no boundary layer to drain,
        // and the gradient's magnitude is the mid-latitude rain.)
        double warmSide =
            PRESCRIBED ? 1.0 : std::clamp(0.5 + (Tsl[i] - tNb) / FRONT_SIDE_K, 0.0, 1.0);
        double wFront = W_FRONT * gmag * warmSide;
        // The diabatic part detrains into the upper branch (see
        // W_EXPORT); read next hour by the dynamics.
        wTop[i] = wConv + wFront;
        pConv[i] = wConv;
        pDiv[i] = wDiv;
        pFront[i] = wFront;
        pOrog[i] = pOro[i];
    }

    // The rain is a rule (see THE CLIMATE AS RULES OF THUMB); the
    // column mirrors a humidity that follows it, for the record
    // and the cloud.
    void waterAt(const HourCtx& h, const CellHour& c) {
        const int i = c.i;
        double rainDay = rul.rain(i, h.doy);
        double rain = rainDay / 24.0;
        double rh = std::clamp(0.45 + 0.1 * rainDay, 0.3, 0.9);
        capEff[i] = std::max(c.cap, 0.05);
        cloudF[i] = std::clamp(0.25 + 0.12 * rainDay, 0.15, 0.85);
        evapAcc[i] += c.evap;
        wvAcc[i] += rh * c.cap;
        rainAcc[i] += rain;
        nW[i] = rh * c.cap;
        if (!water[i]) soil[i] = std::clamp(soil[i] + rain - c.evap, 0.0, SOIL_CAP_MM);
        rainStep[i] = rain;
    }

    // Polar rows: copy neighbours. Then the polar filter: the shrinking
    // cells near the poles go unstable otherwise (moisture spikes,
    // temperature pinned at the clamp). Relax the polar rows toward
    // their zonal means, strength fading with distance from the pole.
    // The water always; the temperatures only when the hour integrated
    // them (see PROBES).
    void polarCapsAndFilter(bool full) {
        for (int x = 0; x < W; x++) {
            nW[idx(x, 0)] = nW[idx(x, 1)];
            nW[idx(x, H - 1)] = nW[idx(x, H - 2)];
            if (!full) continue;
            nT[idx(x, 0)] = nT[idx(x, 1)];
            nT[idx(x, H - 1)] = nT[idx(x, H - 2)];
            // The air too: these were never written by the loop above, so the
            // caps carried a stale temperature for the whole run.
            nTb[idx(x, 0)] = nTb[idx(x, 1)];
            nTb[idx(x, H - 1)] = nTb[idx(x, H - 2)];
        }
        for (int y = 0; y < H; y++) {
            int dPole = std::min(y, H - 1 - y);
            if (dPole > 5) continue;
            double f = 0.5 * (1.0 - dPole / 6.0);
            double mT = 0, mW = 0;
            for (int x = 0; x < W; x++) {
                mT += nT[idx(x, y)];
                mW += nW[idx(x, y)];
            }
            mT /= W;
            mW /= W;
            for (int x = 0; x < W; x++) nW[idx(x, y)] += f * (mW - nW[idx(x, y)]);
            if (!full) continue;
            for (int x = 0; x < W; x++) nT[idx(x, y)] += f * (mT - nT[idx(x, y)]);
        }
    }

    // PROBE (see dbgE): what the air's heat did this hour against what
    // it was given, as W/m2 over the planet.
    void probeConservation() {
        double dE = 0, ph = 0, mm = 0, wsum = 0;
        for (int y = 1; y < H - 1; y++) {
            double cw = std::cos(((y + 0.5) / (double)H - 0.5) * 3.14159265);
            double rowE = 0, rowM = 0;
            for (int x = 0; x < W; x++) {
                int i = idx(x, y);
                rowE += RHO * CP_AIR * (hbNew[i] * nTb[i] - (H_LAYER + hPrev[i]) * TbPrev[i]) +
                        (C_AIR - RHO * CP_AIR * H_LAYER) * (nTf[i] - Tf[i]);
                rowM += RHO * CP_AIR * (hbNew[i] - (H_LAYER + hP[i])) * nTb[i];
            }
            dE += rowE * cw;
            ph += physRow[y] * cw * DT;
            mm += rowM * cw;
            physRow[y] = 0.0;
            wsum += W * cw;
        }
        dbgE[0] += dE / wsum / DT;
        dbgE[1] += ph / wsum / DT;
        dbgE[2] += mm / wsum / DT;
        // and where it could have gone: clamps, limiter, thickness
        int cb = 0, cf2 = 0, sh = 0;
        double hmin = 1e9, hmax = -1e9, tbmax = -1e9, tfmin = 1e9;
        for (int i = 0; i < W * H; i++) {
            if (nTb[i] <= -95.0 || nTb[i] >= 70.0) cb++;
            if (nTf[i] <= -95.0 || nTf[i] >= 70.0) cf2++;
            (void)sh;
            hmin = std::min(hmin, hP[i]);
            hmax = std::max(hmax, hP[i]);
            tbmax = std::max(tbmax, nTb[i]);
            tfmin = std::min(tfmin, nTf[i]);
        }
        dbgN[0] += cb;
        dbgN[1] += cf2;
        dbgN[2] += sh;
        dbgX[0] = std::min(dbgX[0], hmin);
        dbgX[1] = std::max(dbgX[1], hmax);
        dbgX[2] = std::max(dbgX[2], tbmax);
        dbgX[3] = std::min(dbgX[3], tfmin);
    }

    void step(double doy, double hour) {
        if (PRESCRIBED || PAINT_INIT || DYN2 || QG2 || QG2GEO) prescribeHour(doy, hour);
        const HourCtx h = hourContext(doy, hour);
        // The full column, or only what survives the painting (see PROBES).
        const bool full = !PRESCRIBED || PROBES;
        if (full) {
            reduceToSeaLevel();
            upperPoolMean();
            hPrev = hP;
            TbPrev = Tb;
            if (!PRESCRIBED) moveAir();
            divergenceAndOrography(h);
        }
        // Thermodynamics, and the rule rain, per cell.
#pragma omp parallel for
        for (int y = 1; y < H - 1; y++) {
            const RowCtx row = rowContext(h, y);
            for (int x = 0; x < W; x++) {
                CellHour c;
                cellSetup(row, x, c);
                solarAt(h, c);
                if (full) radiationAt(c);
                surfaceExchangeAt(c);
                evaporationAt(h, c);
                if (full) {
                    surfaceAndIceAt(c);
                    layerBudgetsAt(h, c);
                    probeColumnAt(h, c);
                    probeZonalAt(h, c);
                    probeCellAt(c);
                    upliftAt(h, c);
                }
                waterAt(h, c);
            }
        }
        polarCapsAndFilter(full);
        if (full) {
            if (PRESCRIBED) {
                nT = T;
                nTb = Tb;
                nTf = Tf;
                nIce = ice;
            } // painted, not integrated
            std::swap(T, nT);
            std::swap(ice, nIce);
            probeConservation();
            std::swap(Tb, nTb);
            std::swap(Tf, nTf);
        }
        std::swap(Wv, nW);
    }
};

inline Climatology build(const terrain::ContinentParams& cp, float seaLevel, const float rot[9],
                         terrain::V3 offset, const plates::Field& pf, const hydrology::Result& hy,
                         bool verbose = false, void (*progress)(int day, int totalDays) = nullptr) {
    Model m;
    m.init(cp, seaLevel, rot, offset, pf, hy);
    // PROBE: the painted climate's ingredients at named places, when asked
    // (HH_DEBUG_PRE in the environment), for checking the rules by hand.
    if (PRESCRIBED && std::getenv("HH_DEBUG_PRE")) {
        struct Place { const char* name; double lat, lon; };
        static const Place PL[] = {{"Berlin", 52.5, 13.4}, {"Madrid", 40.4, -3.7}, {"Lima", -12.0, -77.0},
                                   {"Delhi", 28.6, 77.2}, {"Kansas", 38.5, -98.0}, {"Bergen", 60.4, 5.3},
                                   {"Chicago", 41.9, -87.6}, {"Tehran", 35.7, 51.4}, {"Beijing", 39.9, 116.4},
                                   {"Lisbon", 38.7, -9.1}, {"New York", 40.7, -74.0}};
        for (const Place& q : PL) {
            int x = (int)((q.lon + 180.0) / 360.0 * W) % W, y = std::clamp((int)((q.lat + 90.0) / 180.0 * H), 0, H - 1);
            int i = y * W + x;
            fprintf(stderr, "PRE %-9s water %d elev %5.0f cont %.2f wide %.2f dUpwind %6.0f dEast %6.0f dWest %6.0f | T jan %5.1f jul %5.1f\n",
                    q.name, (int)m.water[i], m.elev[i], m.pre.cont[i], m.pre.wide[i], m.pre.dUpwind[i],
                    m.pre.dEast[i], m.pre.dWest[i], m.pre.surfaceT(i, m.water[i] != 0, m.elev[i], 15.0, 12.0),
                    m.pre.surfaceT(i, m.water[i] != 0, m.elev[i], 200.0, 12.0));
        }
    }
    Climatology c;
    c.elev.assign(m.elev.begin(), m.elev.end());
    std::vector<double> dayMin(W * H), dayMax(W * H);
    // running totals, so each sample sees the interval and not the epoch
    std::vector<double> lastE(W * H, 0.0);
    bool statSeeded = false;
    std::vector<double> cnt(SEASONS, 0.0);
    int totalDays = SPINUP_DAYS + STAT_YEARS * 365;
    for (int day = 0; day < totalDays; day++) {
        int doy = day % 365;
        int season = Climatology::seasonOfDay(doy);
        bool stat = day >= SPINUP_DAYS;
        m.recordBudget = stat;
        m.curSeason = season;
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
            lastE = m.evapAcc;
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
                c.iceM[si] += (float)m.ice[i];
                c.soilM[si] += (float)m.soil[i];
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
                    c.latF[si] += (float)(m.rainStep[i] * LATENT_J_PER_KG / DT);
                    c.spdF[si] += (float)m.pSpd[i];
                    c.capSkinF[si] += (float)m.pCapSkin[i];
                    c.supplyF[si] += (float)m.pSupply[i];
                    c.affordF[si] += (float)m.pAfford[i];
                    lastE[i] = m.evapAcc[i];
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
        if (day % 30 == 0 && DYN2) fprintf(stderr, "dyn2: day %d, ps [%.0f, %.0f] hPa, u1 rms %.1f\n", day, *std::min_element(m.d2.ps.begin(), m.d2.ps.end()) / 100.0, *std::max_element(m.d2.ps.begin(), m.d2.ps.end()) / 100.0, std::sqrt(std::inner_product(m.d2.u1.begin(), m.d2.u1.end(), m.d2.u1.begin(), 0.0) / (W * H)));
        if (day % 30 == 0 && !PRESCRIBED) { // PROBE: the leak is the point (painted air conserves nothing)
            fprintf(stderr, "  air heat, 30-day mean W/m2: changed %+.2f  given %+.2f  leak %+.2f"
                            "  mass-mismatch %+.2f\n",
                    m.dbgE[0] / 720, m.dbgE[1] / 720, (m.dbgE[0] - m.dbgE[1]) / 720,
                    m.dbgE[2] / 720);
            fprintf(stderr, "    clamped cell-hours: Tb %ld  Tf %ld  limiter %ld;  hP [%.0f, %.0f]  Tb max %.0f  Tf min %.0f\n",
                    m.dbgN[0], m.dbgN[1], m.dbgN[2], m.dbgX[0], m.dbgX[1], m.dbgX[2], m.dbgX[3]);
            m.dbgE[0] = m.dbgE[1] = m.dbgE[2] = 0;
            m.dbgN[0] = m.dbgN[1] = m.dbgN[2] = 0;
            m.dbgX[0] = 1e9; m.dbgX[1] = -1e9; m.dbgX[2] = -1e9; m.dbgX[3] = 1e9;
        }
    }
    {
        double e = 0, r = 0, wsum = 0, wv = 0, wnd = 0, rh = 0;
        for (int y = 0; y < H; y++) {
            double wgt = std::cos(((y + 0.5) / (double)H - 0.5) * 3.14159265);
            for (int x = 0; x < W; x++) {
                int i = y * W + x;
                e += m.evapAcc[i] * wgt;
                r += m.rainAcc[i] * wgt;
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
        c.dbgWv = wv / wsum / allHours;
        c.dbgWind = wnd / wsum;   // instantaneous, at the end of the run
        c.dbgRH = rh / wsum;
    }
    c.isWater.assign(m.water.begin(), m.water.end());
    if (QG2GEO && m.qgg.hoursBanked > 0) {
        double n = m.qgg.hoursBanked;
        c.d2u1.assign(W * H, 0.0f); c.d2u2.assign(W * H, 0.0f); c.d2ps.assign(W * H, 0.0f);
        c.d2psSd.assign(W * H, 0.0f); c.d2eke.assign(W * H, 0.0f);
        for (int i = 0; i < W * H; i++) {
            int j = m.meshOfCell[i];
            c.d2u1[i] = (float)(m.qgg.u2Acc[j] / n);
            c.d2u2[i] = (float)(m.qgg.u1Acc[j] / n);
            c.d2ps[i] = (float)(m.qgg.psiAcc[j] / n * 1e-4 + 1e5);
            c.d2eke[i] = (float)(m.qgg.ekeAcc[j] / n);
        }
    }
    if (QG2 && m.qg.hoursBanked > 0) {
        double n = m.qg.hoursBanked;
        c.d2u1.assign(W * H, 0.0f); c.d2u2.assign(W * H, 0.0f); c.d2ps.assign(W * H, 0.0f);
        c.d2psSd.assign(W * H, 0.0f); c.d2eke.assign(W * H, 0.0f);
        for (int i = 0; i < W * H; i++) {
            c.d2u1[i] = (float)(m.qg.u2Acc[i] / n);   // "low": the QG lower layer
            c.d2u2[i] = (float)(m.qg.u1Acc[i] / n);   // "up": the upper
            c.d2ps[i] = (float)(m.qg.psiAcc[i] / n * 1e-4 + 1e5); // the lower streamfunction, scaled into hPa-like units
            c.d2eke[i] = (float)(m.qg.ekeAcc[i] / n);
        }
    }
    if (DYN2 && m.d2.hoursBanked > 0) {
        double n = m.d2.hoursBanked;
        c.d2u1.assign(W * H, 0.0f); c.d2u2.assign(W * H, 0.0f); c.d2ps.assign(W * H, 0.0f);
        c.d2psSd.assign(W * H, 0.0f); c.d2eke.assign(W * H, 0.0f);
        for (int i = 0; i < W * H; i++) {
            double mp = m.d2.psAcc[i] / n;
            c.d2u1[i] = (float)(m.d2.u1Acc[i] / n);
            c.d2u2[i] = (float)(m.d2.u2Acc[i] / n);
            c.d2ps[i] = (float)mp;
            c.d2psSd[i] = (float)std::sqrt(std::max(m.d2.ps2Acc[i] / n - mp * mp, 0.0));
            c.d2eke[i] = (float)(m.d2.ekeAcc[i] / n);
        }
    }
    {
        double s[Climatology::NTB] = {};
        for (int y = 0; y < H; y++)
            for (int k = 0; k < Climatology::NTB; k++) s[k] += m.budRow[Climatology::NTB * y + k];
        double n = std::max(s[13], 1.0);
        for (int k = 0; k < Climatology::NTB; k++) c.tropBud[k] = k == 13 ? s[k] : s[k] / n;
        c.tropBud[13] = s[13];
        {
            const int NZ = Climatology::NZB;
            c.zonBud.assign(NZ * H, 0.0);
            c.zonBudS.assign(SEASONS * NZ * H, 0.0);
            c.zonBudLS.assign(SEASONS * 2 * NZ * H, 0.0);
            for (int y = 0; y < H; y++) {
                double tot[Climatology::NZB] = {0};
                for (int se = 0; se < SEASONS; se++) {
                    double zs[Climatology::NZB] = {0};
                    for (int sf = 0; sf < 2; sf++) {
                        const double* zl = &m.zonRow[((se * 2 + sf) * H + y) * NZ];
                        double cl = std::max(zl[NZ - 1], 1.0);
                        for (int k = 0; k < NZ - 1; k++) {
                            c.zonBudLS[((se * 2 + sf) * H + y) * NZ + k] = zl[k] / cl;
                            zs[k] += zl[k];
                        }
                        c.zonBudLS[((se * 2 + sf) * H + y) * NZ + NZ - 1] = zl[NZ - 1];
                        zs[NZ - 1] += zl[NZ - 1];
                    }
                    const double* z = zs;
                    double cnt = std::max(z[NZ - 1], 1.0);
                    for (int k = 0; k < NZ - 1; k++) {
                        c.zonBudS[(se * H + y) * NZ + k] = z[k] / cnt;
                        tot[k] += z[k];
                    }
                    c.zonBudS[(se * H + y) * NZ + NZ - 1] = z[NZ - 1];
                    tot[NZ - 1] += z[NZ - 1];
                }
                double cnt = std::max(tot[NZ - 1], 1.0);
                for (int k = 0; k < NZ - 1; k++) c.zonBud[NZ * y + k] = tot[k] / cnt;
                c.zonBud[NZ * y + NZ - 1] = tot[NZ - 1];
            }
        }
    }
    for (int s = 0; s < SEASONS; s++) {
        double hours = cnt[s] * 24.0;
        for (int i = 0; i < W * H; i++) {
            int si = s * W * H + i;
            c.meanT[si] /= (float)hours;
            c.iceM[si] /= (float)hours;
            c.soilM[si] /= (float)hours;
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
    // The rules' rain steps by a factor of two or three from one cell to the
    // next (a lift, a shadow), and one pass left the cells drawn as blocks.
    blur(c.rainMmDay, SEASONS); blur(c.rainMmDay, SEASONS); blur(c.snowMmDay, SEASONS); blur(c.snowMmDay, SEASONS);
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
