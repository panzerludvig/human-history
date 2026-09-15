# Suggestions

A running log of suggestions from Claude — alternative approaches, potential additions, or things that may have been overlooked. These are not instructions or next steps, just options worth considering.

Each suggestion includes a timestamp and a reference to what prompted it. Ask for suggestions by recency ("what are the new suggestions?"), by topic ("suggestions about the Dev Log"), or just browse here directly. Mark resolved suggestions with ✓ and strike the text.

---

<!-- format:
### YYYY-MM-DD — [what prompted this]
Suggestion text.
-->

### 2026-09-15 — Work order 02, the atmosphere's hour split into stages
Give `atmosphere::build` (297 non-blank lines) the same treatment: the month-end probe prints, the season binning and the final blur are each a seam already marked by a comment, and the "done when" of order 02 could not be applied to the whole file because of it. `stepDynamics` (138 lines) is the other candidate. A work order, not a side effect of the next atmosphere change.

### 2026-09-15 — Frame-rate bisect while drawing tilled plots
Close-zoom frame rate is bound by the procedural terrain, not by anything drawn on it: measured on the Arc 140V at 4 km altitude, the whole field layer costs 12→15 fps while the remaining ~65 ms/frame is the 16-octave terrain evaluation (`uOctaves` ramps to 16 as km/pixel shrinks). If close zoom should run at 60 fps, the lever is terrain LOD — e.g. rendering the high-octave detail into a cached local tile texture when the camera lingers, or capping octaves and blending in a baked detail texture — a project of its own, not a fields tweak.

### 2026-09-07 — [[Technical/Climate Review]], the world review on the Earth template
Target the water's residence time in the air as the one number to calibrate the rain rule against: 3.5 days here, 9 on Earth. The over-wet coasts and the empty interiors are both that number, and it is measured directly by the sweep (`Wv / rain`), so it carries to any world without an Earth to compare against.

### 2026-09-07 — [[Technical/Climate Review]], hot deserts 6–9 K too cool in summer
Add an aridity term to the painted land temperature: dry soil cannot evaporate, so its summer runs above the zonal mean. The model's own soil-water store is the input, which makes it a rule rather than a patch, and it fixes Arabia, the Sahara, Iran, the Great Basin and Mexico together.

### 2026-09-07 — [[Technical/Climate Review]], monsoons on every subtropical continent
Condition the continental ITCZ shift on a warm sea lying equatorward of the land (and, if it is to be Earth-faithful, mountains behind it), rather than on continentality alone. As it stands the rule treats Arabia, the Sahel, Mexico and interior Australia as India.

### 2026-08-22 — Vault setup
Before choosing any core concept, write down one or two "desired end states" in [[Design/Overview]] — concrete scenes the game should be able to produce (Delegate had a quick note to this effect that was never acted on). They make it much easier to judge whether a candidate concept is needed by the core.
