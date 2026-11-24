# Ancient rifts and sky islands

Two opt-in landform families, both selected by `config.voidRevision=1` plus a `config.voidRegions` block. A recipe without those fields produces exactly its previous terrain; there is no silent migration.

```json
"voidRevision": 1,
"voidRegions": {
  "enabled": true,
  "ancientRift": {"enabled": true, "grid": 6144, "chance": 0.55,
                  "radiusMin": 420, "radiusMax": 760,
                  "floorDepth": 12, "rimMargin": 24},
  "skyIslands": {"enabled": true, "grid": 5120, "chance": 0.50,
                 "radiusMin": 340, "radiusMax": 620,
                 "islandGrid": 176, "islandChance": 0.80,
                 "islandMinRadius": 30, "islandMaxRadius": 86,
                 "islandMinTop": 300, "islandMaxTop": 450}
}
```

Both are pure functions of `(cell, seed)` plus the pre-void shaped height. They never sample a generated chunk and never call `surface()`/`base()` while building a site, so workers, chunk order and saved worlds always agree.

## Ancient rift

A closed inland chasm. The floor sits `seaLevel - floorDepth` (12 blocks below sea level in the shipped recipe) while the rim keeps its shaped height. Nearly the entire drop happens inside one narrow radial band, so no ring-shaped plateau appears outside the wall and no island of old terrain survives inside it.

- **Floor relief**: broad terraces plus two noise octaves; the bowl reads as a landscape, not a flat disc.
- **Water**: one shallow lake below sea level. The floor is a closed basin, so the hydrology planner never treats that water as a marine sink.
- **Climate**: the floor is forced warm (`T=0.78`) and humid (`H=0.84`) regardless of regional climate, then feeds the shared weight machinery. The result is warm-humid vegetation, not an alpine skin.
- **Skin**: moss, grass and mud on the surface, clay subsoil, deepslate below Y=-40. Walls carry banded tuff/calcite/deepslate.
- **Ecology**: moss carpet, ferns and tall grass on damp terraces; old-growth trunks on an 11-block jittered lattice with flattened crowns; vines on the chasm wall only, where a neighbour column is at least three blocks higher.

Site acceptance requires a dry inland rim at two radii, no ocean or lake cut by the wall, no overlap with an accepted stone or giant forest, and a real drop (`rim - floor >= 120`).

## Sky archipelago

A separate region whose **entire column is void**. There is no stone, no cave, no aquifer and no bedrock: Y=-64 is air. Platforms are lens-shaped with a deepslate keel, stone shell and grass cap, tops between Y300 and Y450, and only the interval `[bottom, top]` is ever filled.

- **Placement**: a jittered `islandGrid` lattice; neighbouring cells are visited in a stable order and the highest top wins, so a column never holds two islands.
- **Shape**: angular lobe warp plus a lens thickness profile; a platform that would reach the floor of the region is dropped.
- **Decoration**: thin grass, occasional flowers, hanging vines under the rim, and small trees whose four-neighbour footprint must stay on the platform.
- **Invariant**: after every other pass, one final sweep forces bare void columns to air and trims platform columns to `[bottom, top + 32]`, the bounded decoration band (trunk, flat crown, rim vines). Nothing can leave rock, snow or fluid hanging below an island.

## Biome identity

Neither landform registers a new engine biome. They reuse the existing landform mechanism: `src/BiomeCatalog.h` entries with a negative `biome` id, matching on `Surface::voidFloor` and `Surface::voidPlatform`. The frozen palette and `biomeNames` contract are untouched, and parent climate indices stay available in diagnostics.

`/locate biome` gains:

| Identifier | Grid | Anchors |
| --- | --- | --- |
| `peakarma:ancient_rift` | `ancientRift.grid` | centre plus two rings at 25% and 55% radius |
| `peakarma:sky_islands` | `skyIslands.grid` | centre plus two rings at 0% and 45% radius |

## Implementation map

| Path | Purpose |
| --- | --- |
| `src/native/VoidRegions.h` | site builders, edge metric, `voidShape`, rift climate, both material functions |
| `src/native/VoidEcology.inc` | rift flora, old-growth trunks, wall vines, island grass/vines/trees |
| `src/native/Surface.h` | `voidRegion`, `voidFloor`, `voidPlatform`, `voidVoid`, `voidTop`, `voidBottom` |
| `src/native/Field.h` | `voidShape` call after every other height shaper; void branch in `material()` and `materialColumn()`; `voidSurfaceEcology` in the cached skin |
| `src/native/Engine.h` | no bedrock fill for void columns; three decoration passes; final invariant sweep |
| `tests/void_landforms.cpp` | opt-in identity, rift climate/height, island bounds, byte-level void guarantee, catalogue matching |

## Bounds and costs

- Site surveys sample 24×2 points at two radii and run once per lattice cell, cached in a 256-entry `Cache`. A cheap band test rejects columns that no owner cell can reach, so ordinary terrain pays two comparisons.
- Island plans sample a 3×3 lattice of cells per column, cached by the column's own cell key.
- A void column costs one branch in `material()`; platform columns additionally write only their `[bottom, top]` interval.
- Recipe validation rejects overlapping lattices, a rim margin below 8, `islandMaxRadius*2 > islandGrid`, floors above sea level and island tops outside the generated height.

## Ecology detail

The first pass produced a bare pole with a flattened ball, and the sky platforms
were undecorated lenses. Both now use the layered silhouettes the other biomes
already carry.

### Ancient rift old growth

Three silhouettes — emergent canopy, leaning buttress, hollow elder — with six
buttress roots, a 3x3 tapering trunk that keeps oriented bark on its core (so a
cut top shows rings), three limbs reaching into offset sub-crowns, a top tuft,
hanging vines from the outer crown ring, and moss on the root plate.

The floor is a rainforest floor, not a lawn: moss replaces the soil cap, ferns
dominate, azalea patches carry the flowering variant, clay lenses sit in the
wettest hollows, cave-vine curtains with berry tips and spore blossoms hang under
the rim, and glow lichen lines the overhang. The below-sea-level lake gets a mud
ring, reeds and lilies.

The standard jungle pass now yields the rift floor to the old-growth stand, so
two forests are never planted inside the same chasm.

### Sky islands

The cap is never empty: ground cover and cap material are chosen independently,
so a column cannot fall through every branch. Islands carry fern, grass, flowers
and coarse-dirt patches, shallow ponds whose lip is verified against a full 3x3
platform footprint, azalea pockets, and hanging roots and vines below the keel.

### Two decoration bands

The final void sweep keeps two bounded bands: 12 blocks below the keel for
hanging roots and vines, and 44 above the cap for trunks and crowns. Everything
outside them is air. A one-block hole in a cap (a pond write that lost its water,
a snow-support cleanup) is restored directly by that sweep rather than left to
whichever pass ran last.
