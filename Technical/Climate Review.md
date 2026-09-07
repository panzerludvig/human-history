# Climate Review

**Status:** Implemented (the probe) — see [[Meta/Status Vocabulary]]

The painted climate ([[Design/Weather]], decision of 2026-09-04) measured on the Earth template against what Earth records, region by region. The point is not to fit Earth: it is to find which rules hold and which fail, so that what survives can be trusted on any world. The probe is `sweep.exe earth`, section `THE WORLD REVIEW`; the reference figures are climatological means, rounded, from memory of standard climate atlases. The game and the sweep were confirmed to compute the same world (screenshot of the game in vegetation mode against the sweep's map, 2026-09-07), so this table is what the player sees.

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
