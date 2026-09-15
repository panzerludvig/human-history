# Climate Review

**Status:** Implemented (the probe) — see [[Meta/Status Vocabulary]]

The climate measured on the Earth template against what Earth records, region by region: first the painted climate ([[Design/Weather]], decision of 2026-09-04, the survey of 2026-09-07 at the bottom), then the equations alone (decision of 2026-09-08, the survey below). The point is not to fit Earth: it is to find which rules hold and which fail, so that what survives can be trusted on any world. The probe is `sweep.exe earth`, section `THE WORLD REVIEW`; the reference figures are climatological means, rounded, from memory of standard climate atlases. The game and the sweep were confirmed to compute the same world (screenshot of the game in vegetation mode against the sweep's map, 2026-09-07), so this table is what the player sees.

## Survey of 2026-09-09: the rules of thumb

The climate as rules ([[Design/Weather]], decision of 2026-09-09), seventh pass, `sweep.exe earth 0 rules 1`. Temperature is the painted one and unchanged from the survey of 2026-09-07; rain (mm/day, model against Earth):

| Region | Rain | Earth | | Region | Rain | Earth |
|---|---|---|---|---|---|---|
| US Pacific NW | 3.2 | 3.5 | | W Europe | 2.9 | 2.2 |
| California | 1.7 | 1.2 | | Spain | 3.6 | 1.5 |
| Great Basin | 1.1 | 0.7 | | E Europe | 1.2 | 1.6 |
| Great Plains | 0.8 | 1.5 | | Scandinavia | 2.1 | 2.0 |
| US Midwest | 1.6 | 2.5 | | C Siberia | 0.6 | 1.2 |
| US Southeast | 2.4 | 3.5 | | Arabia | 2.0 | 0.2 |
| Canada boreal | 0.9 | 1.2 | | Iran | 0.8 | 0.6 |
| Alaska interior | 1.6 | 0.8 | | C Asia | 1.1 | 0.6 |
| Mexico plateau | 2.5 | 1.2 | | India | 3.3 | 3.0 |
| Amazon | 5.1 | 6.0 | | N China | 2.4 | 1.8 |
| NE Brazil | 5.2 | 2.0 | | S China | 2.8 | 4.5 |
| Pampas | 2.6 | 2.5 | | Mongolia | 1.1 | 0.6 |
| Patagonia | 3.1 | 0.6 | | SE Asia | 3.6 | 5.0 |
| Peru coast | 3.0 | 0.1 | | Japan | 2.5 | 4.5 |
| NW Africa | 1.3 | 1.2 | | Sahara | 1.0 | 0.1 |
| Sahel | 3.5 | 1.5 | | Congo | 5.1 | 5.0 |
| E Africa | 5.2 | 2.0 | | S Africa | 1.8 | 1.4 |
| Australia interior | 0.8 | 0.6 | | E Australia | 3.2 | 2.5 |
| N Australia | 3.9 | 2.5 | | | | |

Within 0.7 mm/day in 20 regions; the seasons come out by class (India 0.4 in winter and 7.4 in summer, northern Australia 7.5 and 0.9, Spain's winter, the Midwest's summer).

What holds: everything the rules state, the temperate west coasts, the interiors, the subtropical deserts, the tropics, the monsoon. What fails, by kind:

- **The humid subtropics and their hinterland are dry by 0.7 to 1.7** (the Southeast 2.4 against 3.5, the Midwest 1.6 against 2.5, South China 2.8 against 4.5, Japan 2.5 against 4.5). The east-coast rule's reach and multiple are both a little small; one constant each.
- **Earth's exceptions.** Northeast Brazil is semi-arid under a subsidence the rules cannot see; East Africa is a dry plateau under a monsoon flow that runs along the coast; the Sahel's monsoon stops sooner than India's; the Peru coast is 3.0 because the review's box includes the Andes; Patagonia's shadow needs the template's Andes to be higher than the 208 km cells make them; Spain's plateau lift is real on the Cantabrian side and wrong for the Meseta. None of these is a rule any other world would need.
- **Arabia** at 2.0 against 0.2 gets the monsoon rule because the sea lies equatorward and Asia poleward; on Earth the monsoon reaches only its southern coast. The plateau test that distinguishes India from Africa does not distinguish Arabia.

## Survey of 2026-09-08: the equations alone

The all-physics mode ([[Design/Weather]], decision of 2026-09-08): painted once at hour zero, then radiation, surface balance, ice, the boundary layer, the mesh QG weather and the two-layer water, with no ocean transport. `sweep.exe earth 0 physgeo 1` (a mode removed on 2026-09-15 with the column water; last at commit `3e87389`), commit `b12302b`, one spin-up year and one measured. Reference figures as in the table below.

### The world review

| Region | Rain | Earth | DJF | JJA | Jan | Earth | Jul | Earth | Cover | Earth |
|---|---|---|---|---|---|---|---|---|---|---|
| US Pacific NW | 1.3 | 3.5 | 1.9 | 1.3 | −1 | 3 | 26 | 17 | steppe | forest |
| California | 1.4 | 1.2 | 2.1 | 1.0 | 8 | 10 | 28 | 20 | steppe | shrub |
| Great Basin | 1.8 | 0.7 | 2.2 | 2.1 | 5 | −1 | 27 | 23 | steppe | steppe/desert |
| Great Plains | 3.4 | 1.5 | 3.0 | 4.9 | 1 | −6 | 25 | 24 | forest | grass |
| US Midwest | 2.5 | 2.5 | 3.0 | 3.5 | 5 | −5 | 26 | 23 | forest | forest |
| US Southeast | 3.2 | 3.5 | 3.8 | 4.0 | 13 | 8 | 25 | 27 | forest | forest |
| Canada boreal | 1.4 | 1.2 | 1.3 | 1.2 | −14 | −20 | 25 | 16 | taiga | taiga |
| Alaska interior | 0.9 | 0.8 | 0.8 | 1.1 | −20 | −22 | 16 | 15 | taiga | taiga |
| Mexico plateau | 3.7 | 1.2 | 3.9 | 4.1 | 15 | 12 | 24 | 24 | forest | steppe |
| Amazon | 3.2 | 6.0 | 2.9 | 2.9 | 26 | 26 | 25 | 25 | forest | rainforest |
| NE Brazil | 3.2 | 2.0 | 3.3 | 2.7 | 26 | 27 | 24 | 25 | forest | savanna |
| Pampas | 3.4 | 2.5 | 4.2 | 3.1 | 25 | 23 | 14 | 9 | forest | grass |
| Patagonia | 4.4 | 0.6 | 4.0 | 5.4 | 20 | 14 | 8 | 2 | forest | steppe |
| Peru coast | 6.3 | 0.1 | 6.8 | 5.0 | 21 | 22 | 19 | 16 | forest | desert |
| W Europe | 1.3 | 2.2 | 1.9 | 0.7 | −1 | 3 | 24 | 18 | steppe | forest |
| Spain | 1.4 | 1.5 | 1.6 | 1.7 | 6 | 6 | 26 | 25 | steppe | shrub |
| E Europe | 1.3 | 1.6 | 1.5 | 1.3 | −15 | −6 | 24 | 19 | taiga | forest |
| Scandinavia | 0.9 | 2.0 | 1.2 | 0.5 | −14 | −6 | 13 | 15 | taiga | taiga |
| C Siberia | 1.3 | 1.2 | 0.8 | 1.4 | −21 | −30 | 24 | 17 | taiga | taiga |
| Arabia | 2.2 | 0.2 | 1.7 | 3.6 | 12 | 15 | 25 | 35 | savanna | desert |
| Iran | 1.7 | 0.6 | 1.3 | 3.0 | 2 | 4 | 26 | 28 | grass | desert |
| C Asia | 1.6 | 0.6 | 1.3 | 2.2 | −11 | −5 | 27 | 26 | grass | steppe/desert |
| India | 2.6 | 3.0 | 2.3 | 3.9 | 14 | 20 | 26 | 29 | savanna | savanna |
| N China | 2.8 | 1.8 | 2.3 | 4.1 | 2 | −2 | 26 | 26 | forest | forest |
| S China | 3.1 | 4.5 | 3.9 | 3.3 | 11 | 8 | 25 | 28 | forest | forest |
| Mongolia | 1.8 | 0.6 | 1.2 | 2.4 | −8 | −20 | 26 | 18 | taiga | steppe |
| SE Asia | 3.4 | 5.0 | 3.3 | 4.1 | 20 | 24 | 26 | 28 | forest | rainforest |
| Japan | 2.8 | 4.5 | 2.4 | 3.4 | 13 | 4 | 24 | 25 | forest | forest |
| NW Africa | 2.0 | 1.2 | 1.9 | 2.9 | 7 | 10 | 26 | 27 | forest | shrub |
| Sahara | 1.9 | 0.1 | 1.2 | 3.3 | 11 | 13 | 25 | 33 | steppe | desert |
| Sahel | 2.5 | 1.5 | 0.9 | 3.8 | 20 | 24 | 26 | 29 | savanna | savanna |
| Congo | 3.0 | 5.0 | 2.5 | 2.9 | 24 | 25 | 25 | 24 | savanna | rainforest |
| E Africa | 4.5 | 2.0 | 5.1 | 4.0 | 23 | 21 | 24 | 19 | forest | savanna |
| S Africa | 2.5 | 1.4 | 3.4 | 1.8 | 24 | 22 | 16 | 11 | shrub | steppe |
| Australia interior | 2.0 | 0.6 | 3.2 | 1.2 | 26 | 29 | 18 | 13 | steppe | desert |
| E Australia | 3.7 | 2.5 | 5.2 | 2.3 | 26 | 24 | 17 | 12 | steppe | forest |
| N Australia | 2.9 | 2.5 | 3.7 | 1.7 | 26 | 29 | 24 | 24 | savanna | savanna |

### The globe

| Quantity | Model | Earth |
|---|---|---|
| Evaporation, rain (mm/day) | 2.59, 2.56 | 2.7 |
| Column water, residence | 24.4 mm, 9.5 d | 25 mm, 9 d |
| Column relative humidity | 90 % | 60 to 70 % |
| Top of atmosphere, net | +2.4 W/m² | 0 |
| Absorbed sunlight, tropics / poles | 270 / 65 W/m² | 312 / 92 |
| Outgoing longwave, tropics / poles | 235 / 151 W/m² | 255 / 185 |
| Poleward heat transport, peak N / S | 5.3 / 3.1 PW | 5.3 / 5.1 |
| Surface westerlies, N / S | 1.4 to 5.3 at 51–81° / 0.5 to 1.7 at 54–62° | 3 to 6 at 40–60° / 5 to 8 at 45–60° |
| Trades | −4 to −5 | −5 to −6 |
| Eddy energy, N / S storm track | 20 to 24 / 5 to 8 | 30 to 60 / 30 to 60 |
| Subtropical subsidence at 28° | −0.0005 m/s | about −0.005 |
| Convective ascent on the equator | 0.003 m/s | 0.005 to 0.01 |

### What holds

- **The global water cycle.** Evaporation, rain, column water and residence time are all within five percent of Earth's, with no constant chosen to make them so.
- **The northern storm track.** Surface westerlies, trades, eddy energy and the subtropical high are Earth-like in kind and within a few degrees in place; the northern poleward heat transport peaks at Earth's 5.3 PW.
- **The dry places that are dry.** Alaska 0.9 against 0.8, the boreal belt 1.4 against 1.2, Siberia 1.3 against 1.2, the Midwest 2.5 against 2.5, India 2.6 against 3.0, N Australia 2.9 against 2.5, the Sahel 2.5 against 1.5. The taiga, the savannas and the Midwest's forest come out as themselves.
- **Tropical temperature.** The Amazon, Congo and Southeast Asia sit within one degree of Earth in both seasons.

### What fails, and why

1. **Rain is spread too evenly: the deserts are wet and the wet places dry.** Every desert on Earth's map gets 1.5 to 2.5 mm/day here (Sahara 1.9 against 0.1, Arabia 2.2 against 0.2, the Peru coast 6.3 against 0.1, Patagonia 4.4 against 0.6, Australia's interior 2.0 against 0.6) and comes out as steppe, savanna or forest, while the wettest places get half of Earth's (Amazon 3.2 against 6.0, Congo 3.0 against 5.0, Southeast Asia 3.4 against 5.0, the Pacific Northwest 1.3 against 3.5). The lift table says why: the subtropical subsidence that makes deserts is a tenth of Earth's, because the model has no Hadley cell (the QG flow is nondivergent and the tropics' large-scale ascent is the grid's own weak convergence), and the convective ascent on the equator is half. Without descent, convection over a warm dry surface still finds the layer's mean vapour and rains it. The Peru coast adds the missing upwelling.
2. **The southern hemisphere's storm track is weak.** Eddy energy 5 to 8 against the north's 20 to 24 and Earth's 30 to 60 in the south; surface westerlies under 2 m/s where Earth's roaring forties are 5 to 8; southern heat transport 3.1 PW against 5.1. The southern baroclinicity is made by the ocean's temperature gradient, and a slab ocean without transport has too little of it, on top of the missing subtropical jet.
3. **Land is warm, sea is cold.** Northern land runs 4 to 9 K hot in summer (Canada 25 against 16, Siberia 24 against 17, W Europe 24 against 18) and 7 to 12 K mild in winter inland (Canada −14 against −20, Siberia −21 against −30, Mongolia −8 against −20, the Midwest 5 against −5), southern land 5 K warm in its winter; the sea's coasts are cold in winter (W Europe −1 against 3, Scandinavia −14 against −6, E Europe −15 against −6). The winter interior's warmth is the uniform subsidence deposit, 44 to 78 W/m² of free-troposphere air brought down at the dry-adiabatic gap; the coasts' cold is the absent ocean drift; the summer heat is the deserts' rain in reverse, land that rains too little for its evaporation to cool it, plus a boundary layer with no cloud shading it. The Sahara and Arabia are 8 to 10 K too cool in summer for the opposite reason: they rain.
4. **The tropics absorb too little sunlight and emit too little.** Absorbed 270 against 312 W/m² and outgoing 235 against 255 on the equator: too much cloud or albedo in the tropics, offset by too strong a greenhouse (the column sits at 90 percent humidity). The two errors cancel in the net, which is why the tropical surplus is only 35 percent short and the northern transport still matches. The planet is still taking in 2.4 W/m² after the spin-up year, so the equilibrium is warmer than this.
5. **Rain shadows and plateaus.** The Rockies at 47 north take 1.8 to 2.4 mm/day across both slopes and the Great Basin 1.8 against 0.7; the 47N transect shows the moisture and the rain nearly flat from the Pacific to the Atlantic. The mountains neither wring the westerlies out on their windward side nor shadow their lee, because the flow over them is 1.5 m/s in the mean and the lift the terrain makes is small against the eddies.

### The energy balance, term by term (2026-09-09)

A probe of the two air layers' own budgets over the tropical ocean (`TROPICAL OCEAN COLUMN`, the `BOUNDARY LAYER` and `FREE TROPOSPHERE` lines) and four Earth years with different cloud rules. What was found:

- **The tropical column was 6 to 8 K cold at every level under a sea only 1.4 K cold** (boundary layer 16.6 against 23, free troposphere −27 against −18), and its budget said why: the boundary layer emitted 60 W/m² more longwave than it absorbed and made it up with 31 W/m² of sensible heat drawn from the sea (Earth: 10). It emitted like a black body because half the cloud's optical depth sits in it and the cloud fraction was 0.95. The overcast also reflected 42 W/m² of sunlight Earth absorbs and held the outgoing longwave 20 W/m² low.
- **The cloud fraction came from the rule, not the weather.** The exponential rule read a layer at 80 percent saturation as 85 percent cloud; the lower layer of the split water sits at 80 to 90 percent in the tropics like Earth's marine layer, which carries 50 to 60. The loop closed: cold air, higher saturation, more cloud, colder air. The upper layer's saturation, which the rule also used, is not cloud at all: that layer sits at saturation whenever it has just condensed.
- **Sundqvist's relation (critical humidity 0.7) fixed the tropics to the watt** — absorbed 308 against 312, emitted 255 against 255, the sea at 26.9 against 27 — **and cleared the extratropics** to 10 to 20 percent cloud against 70 observed, 30 to 60 W/m² of extra sunlight from 40 degrees poleward, +13 W/m² on the planet, and 35-degree summers over land. Cloud from the model's own ascent added nothing, because the QG interface velocity in the storm tracks is a few millimetres per second where frontal ascent at cloud level is centimetres.
- **The exponential re-derived so that 85 percent saturation gives 60 percent cloud** (the state the tree is left in, with anvils where the upper layer rains): cloud 42 to 57 percent at every latitude against Earth's 50 to 82, the tropics absorbing 290 and emitting 245, the planet +7.8 W/m², the tropical air 2 K closer than before and the extratropics still 15 to 20 W/m² too sunlit.

The conclusion is that no relation between a layer's saturation and its cloud fits both the tropics and the storm tracks, because Earth's cloud depends only weakly on saturation: the storm tracks' 70 percent is frontal cloud made by the dynamics. What the model lacks is that lift at the right strength. The frontal lift proxy from the temperature gradient (`wFront`, derived from the model's own state) exists and is used only inside the tropics since the split water; the resolved QG lift that replaced it outside is too weak at 250 km for cloud. Two other terms were measured and stand: the winter continent's warmth is the uniform subsidence deposit (44 to 78 W/m² into the boundary layer from 49 to 86 north) more than the diffusion that was removed; the summer land's 10 K of heat survives every cloud rule and points at the land surface itself — a July surface at 49 north gaining 50 W/m² net with latent heat 54 against Earth's 80, under a 370 W/m² sky.

### What to change, and in what order

- **A divergent tropical circulation.** The Hadley cell is the single largest absence: it makes the deserts by subsidence, the ITCZ by convergence, the subtropical jet, and half the southern baroclinicity. The QG model cannot supply it; the two-layer shallow-water core on the mesh can, and it was the next step of the plan of 2026-09-08 before the painting came out.
- **The ocean drift.** Wind-driven Ekman transport of the slab's heat along the mesh, with coastal upwelling: the cold eastern boundaries and their deserts, the warm western boundaries and Europe's winter, and the southern ocean's gradient.
- **The subsidence deposit.** Let the free-troposphere air come down where the dynamics bring it down, at the rate they do, instead of everywhere at the global mean.
- **Cloud and humidity in the tropics.** Where the 90 percent column and the 42 W/m² of missing absorbed sunlight come from, once the circulation is there to dry the subtropics.

Run of 2026-09-07, branch `prescribed-climate` at the currents commit.

## The table

Rain is annual mean in mm/day; T is January and July surface temperature in °C; the model's biome is the most common dominant cover over the region's cells.

| region | rain | Earth | DJF | JJA | Tjan | Earth | Tjul | Earth | model | Earth |
|---|---|---|---|---|---|---|---|---|---|---|
| US Pacific NW | 10.9 | 3.5 | 8.8 | 10.7 | 3 | 3 | 18 | 17 | forest | forest |
| California | 20.8 | 1.2 | 25.1 | 17.9 | 5 | 10 | 17 | 20 | forest | shrub |
| Great Basin | 4.4 | 0.7 | 3.4 | 6.9 | -1 | -1 | 16 | 23 | forest | steppe |
| Great Plains | 0.0 | 1.5 | 0.1 | 0.0 | -2 | -6 | 22 | 24 | bare | grass |
| US Midwest | 0.2 | 2.5 | 0.2 | 0.1 | -1 | -5 | 23 | 23 | bare | forest |
| US Southeast | 6.2 | 3.5 | 5.4 | 9.2 | 7 | 8 | 26 | 27 | forest | forest |
| Canada boreal | 1.2 | 1.2 | 0.1 | 3.0 | -18 | -20 | 15 | 16 | taiga | taiga |
| Alaska interior | 3.6 | 0.8 | 0.1 | 7.5 | -23 | -22 | 5 | 15 | tundra | taiga |
| Mexico plateau | 10.3 | 1.2 | 4.8 | 15.1 | 5 | 12 | 16 | 24 | forest | steppe |
| Amazon | 7.5 | 6.0 | 6.9 | 4.0 | 24 | 26 | 21 | 25 | rainforest | rainforest |
| NE Brazil | 17.7 | 2.0 | 24.4 | 7.9 | 26 | 27 | 21 | 25 | rainforest | savanna |
| Pampas | 1.1 | 2.5 | 2.6 | 0.0 | 25 | 23 | 8 | 9 | desert | grass |
| Patagonia | 4.9 | 0.6 | 5.7 | 3.5 | 9 | 14 | -6 | 2 | tundra | steppe |
| Peru coast | 11.4 | 0.1 | 18.2 | 0.5 | 15 | 22 | 9 | 16 | forest | desert |
| W Europe | 6.5 | 2.2 | 4.8 | 6.4 | 0 | 3 | 18 | 18 | forest | forest |
| Spain | 8.3 | 1.5 | 6.9 | 10.1 | 6 | 6 | 20 | 25 | forest | shrub |
| E Europe | 3.5 | 1.6 | 0.9 | 5.9 | -11 | -6 | 18 | 19 | taiga | forest |
| Scandinavia | 3.9 | 2.0 | 2.7 | 4.8 | -9 | -6 | 9 | 15 | tundra | taiga |
| C Siberia | 2.9 | 1.2 | 0.2 | 6.8 | -29 | -30 | 12 | 17 | taiga | taiga |
| Arabia | 3.6 | 0.2 | 0.3 | 8.9 | 14 | 15 | 26 | 35 | forest | desert |
| Iran | 2.3 | 0.6 | 0.6 | 5.8 | 0 | 4 | 20 | 28 | forest | desert |
| C Asia | 1.8 | 0.6 | 2.8 | 0.2 | -1 | -5 | 20 | 26 | forest | steppe |
| India | 1.3 | 3.0 | 0.1 | 3.6 | 13 | 20 | 26 | 29 | desert | savanna |
| N China | 0.9 | 1.8 | 0.1 | 1.8 | 3 | -2 | 26 | 26 | bare | forest |
| S China | 3.4 | 4.5 | 1.9 | 4.4 | 12 | 8 | 26 | 28 | forest | forest |
| Mongolia | 0.8 | 0.6 | 1.0 | 0.1 | -11 | -20 | 18 | 18 | taiga | steppe |
| SE Asia | 11.6 | 5.0 | 4.1 | 19.4 | 20 | 24 | 26 | 28 | rainforest | rainforest |
| Japan | 7.3 | 4.5 | 7.4 | 7.7 | 10 | 4 | 20 | 25 | forest | forest |
| NW Africa | 4.0 | 1.2 | 5.5 | 2.3 | 5 | 10 | 21 | 27 | forest | shrub |
| Sahara | 0.6 | 0.1 | 0.0 | 2.3 | 11 | 13 | 26 | 33 | bare | desert |
| Sahel | 4.3 | 1.5 | 0.0 | 11.5 | 17 | 24 | 25 | 29 | forest | savanna |
| Congo | 5.6 | 5.0 | 2.7 | 2.5 | 23 | 25 | 22 | 24 | forest | rainforest |
| E Africa | 12.9 | 2.0 | 14.8 | 7.8 | 21 | 21 | 21 | 19 | rainforest | savanna |
| S Africa | 6.8 | 1.4 | 10.2 | 4.3 | 21 | 22 | 8 | 11 | forest | steppe |
| Australia interior | 4.4 | 0.6 | 12.8 | 0.4 | 27 | 29 | 11 | 13 | forest | desert |
| E Australia | 7.3 | 2.5 | 10.7 | 4.8 | 24 | 24 | 9 | 12 | forest | forest |
| N Australia | 5.8 | 2.5 | 8.3 | 1.8 | 27 | 29 | 20 | 24 | forest | savanna |

Of 37 regions: rain within reason in 5, both temperatures within 3 K in 9, biome right in 11.

## What holds

These are the rules to keep for other worlds.

- **The temperature framework.** Zonal curves for land and sea, a seasonal swing that grows with distance from the sea upwind, a lapse rate, and painted currents give January and July within 3 to 5 K at most mid-latitude places on both sides of both oceans: Berlin, Madrid, Lisbon, Chicago, New York, Beijing, Delhi, the Canadian and Siberian boreal, the Amazon and Congo. Sea ice, the poles and the seasonal lag are right.
- **The deep tropics and the boreal band.** The Amazon, the Congo and the Canadian boreal get Earth's rain to within 30 percent, with the right seasonality.
- **Deserts under the subtropical highs where no monsoon reaches.** The Sahara core, Mongolia, the Pampas' dry season, the Great Basin's winter.
- **Rain shadows and windward coasts exist in the right places.** The signs are right everywhere; only the amounts are not.

## What fails, and why

Every rain failure in the table is one of three mechanisms.

1. **Rain lands too hard on the first coast and the first ridge.** California 21 mm/day against 1.2, the Pacific Northwest 11 against 3.5, Spain 8 against 1.5, Japan 7 against 4.5, Patagonia 5 against 0.6. The air under the belt winds gives up its water on the first uplift it meets. The number behind it is the water's residence time in the air: 3.5 days here, 9 on Earth. Water that stays aloft 2.5 times longer travels 2.5 times further inland, and the coast keeps a third of what it keeps now. This is the same fact as the next one.
2. **Continental interiors in the lee get nothing.** The Great Plains 0.0 against 1.5, the Midwest 0.2 against 2.5, the Pampas 1.1 against 2.5, northern China 0.9 against 1.8, India 1.3 against 3. Nothing carries moisture 1000 km inland once the coast has taken it: on Earth the eddies do, mixing Gulf air across the plains and the Bay of Bengal's across India. Raising the moisture diffusion to the eddy scale reached the Midwest but manufactured water elsewhere, so the mechanism has to be a bounded transport, not a bigger diffusivity.
3. **The continental monsoon lands on the wrong subtropics.** Shifting the ITCZ further over continents gives India a monsoon, but it also gives one to Arabia (8.9 mm/day in summer), Iran (5.8), the Sahel (11.5), Mexico (15), and in the southern summer to Australia's interior (12.8), southern Africa (10) and north-east Brazil (24). The rule treats every continent as India. Earth's monsoons are where a warm ocean lies equatorward of a large heated landmass with mountains behind it; a flat rule by continentality alone paints them on every desert.

The temperature failures are two.

- **Hot deserts are not hot.** Arabia 26 in July against 35, the Sahara 26 against 33, Iran 20 against 28, the Great Basin 16 against 23, Mexico 16 against 24. The land curve is a zonal mean, and dry land runs 6 to 9 K above it in summer because nothing evaporates. An aridity term on the land's summer would fix all five together, and it follows from the model's own soil water, so it would carry to any world.
- **High-latitude summers are cold.** Alaska 5 against 15, Scandinavia 9 against 15, Siberia 12 against 17. The land swing at 60 to 67 degrees is too small where the upwind sea damps it, and the template's interior elevations add to it.

Global rain is 4.3 mm/day against Earth's 2.7, and evaporation with it. That is the residence-time fact again: the belt winds scaled to Earth's surface speeds evaporate as Earth's do, but the water is rained out at once instead of being carried, so the cycle runs 60 percent too fast.

## What to change, and what not to

The failures are three rules, not thirty. Two of them, the residence time and the interior transport, are one thing: water must stay in the air longer and travel further, and the rain rule's threshold and rate are where that lives. The third, the monsoon rule, should be weakened to a modest shift or conditioned on a warm sea lying equatorward. None of these are Earth-specific, so they carry.

What not to do: more painted patches for named places. The Gulf jet, the Sahel, the Peru coast are each a symptom of the three mechanisms above, and a patch for each would fit Earth and nothing else.
