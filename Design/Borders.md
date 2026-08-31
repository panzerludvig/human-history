# Borders

How a settlement comes to hold ground, how far it holds it, and what happens when two claims meet. Rules live in `src/sim.h` (`growClaim`, `roomKm`, `claimFits`, `wantedReachKm`) and `src/population.h` (the constants and the yield curve). Drawn by `borderNear` in `shaders/globe.frag`.

## What a claim is

Sixteen sectors, each with its own reach in km, blended between neighbours so a claim is a closed curve rather than sixteen arcs. A single radius could only ever grow until its first neighbour and then never again; sectors let a settlement blocked to the east keep growing west, which is what makes borders irregular and worth looking at.

Two limits bracket it. The **floor**, 20 km, is what any community holds however small, and it sets how close two settlements can ever stand — 40 km apart, half the spacing the old fixed rule enforced. The **cap**, 60 km, is as far as one place can work from: beyond that the land is somebody else's problem, whoever they are.

## Land is worth less the further out it lies

A day spent walking is a day not spent gathering, so value per km² falls as `1/(1+(d/20 km)²)`. Over a disc that integrates to `πT²·ln(1+r²/T²)`, normalised so a **floor claim is worth exactly the 314 km² catchment every settlement used to be handed outright**. That normalisation is what keeps the old calibration meaningful: a settlement at the floor is the settlement we had before.

| Reach | Worked value | Against the floor |
|---|---|---|
| 20 km (floor) | 314 km² | 1.00 |
| 30 km | 534 km² | 1.70 |
| 40 km | 729 km² | 2.32 |
| 60 km (cap) | 1043 km² | 3.32 |

A settlement's food capacity is its cell's yield scaled by this, so claims are not decoration: they are the settlement's means.

## What makes a claim grow

The wanted reach is the radius whose worked value feeds the people now living there, times a margin of 1.4 for the ones coming. Two arithmetic traps, both hit while building this:

- `kFoodP` already carries `SUSTAIN_R`, so people-supported-per-km² is `kFoodP/314`. Multiplying by the ratio again asked for 45% less land than was needed, and no claim in the world ever wanted to grow.
- Land yields `perKm² × R`, and lived-on land settles at R ≈ 0.55. Priced at the pristine figure, every settlement concludes it has land to spare — again, nothing grows.

Growth is 10 km per century, multiplied up to sevenfold by hunger: people range further before they abandon a place. A sector stops where it meets a claim already made. **Whoever claimed the ground first keeps it, and a border once settled never moves on its own** — a challenge-and-response for moving one is deliberately deferred.

## Hunger widens the border before it empties the village

This is the ordering that makes the whole system live. Pressure used to go straight to emigration; now a food-limited settlement first asks whether its claim can still grow, and if it can, it works the ground it just took and asks again later. Only a settlement that is **hemmed in** — at the cap, or with neighbours on every side — sends anyone away.

That single rule is what stopped the map filling up before anyone could grow. Emigration was instant while expansion was a trickle, so every pressure discharged as a departing band, each band planted a floor-sized claim, and the world packed to 40 km spacing with nobody left any room. Being hemmed in is also exactly the condition the raid trigger already wants, so conflict and expansion now share a cause.

## Where colonists go

A prospect is priced by the claim its takers would actually make there — what they need, or what fits, whichever is less — not by all the room there is. Valuing a target at the largest claim anyone could ever hold made every prospect outbid home three to one, and whole settlements marched off instead of sending colonists (400 settlements and 3 bands in five centuries — the frontier never moved). Room is computed only for the two dozen finalists of a search: the scan is far too costly to run over every cell in a radius.

Founding needs the floor claim to fit in unclaimed land, and the arrivals take what they need on the day they get there, up to the room available. A group that walked here with five hundred people does not wait a century to work enough ground to feed them.

## What it did to the world

Measured over 500 years, seed 7, against the same run before borders existed:

| | before | with borders |
|---|---|---|
| settlements | 4,004 | 5,437 |
| people | 1.08M | 3.00M |
| claims grown past the floor | — | 3,895 of 5,437 |
| claims at the cap | — | 2,097 |

The population is 2.8× what it was, and that is the honest consequence of the old rule: a settlement used to work 314 km² while excluding everyone else from 5,000, so 95% of the land fed nobody. It now averages 37.8 km of reach. **Everything calibrated against world population — invention rates above all — is now measured against a different world**, and wants re-checking.

## Deferred

Challenging a border. Bands trespassing, and intercepting them. Claims shrinking when a people dwindles — today a claim is held until the settlement dies, and released whole when it does, at which point neighbours may grow into it.
