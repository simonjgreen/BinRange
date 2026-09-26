# WROVER case v7.2.0 — DIN rail mount

**Status: experimental. CAD checks only; not yet printed.**

v7.2.0 is the accepted v7.1.0 case with a DIN rail mount added to the underside of the base, the larger half. The enclosure, closure beads, PCB cones and PCB springs are unchanged. The lid STL is byte-identical to v7.1.0 and still reads `W7.1.0 LID`. Any v7.0.0 or v7.1.0 lid fits this base.

Print files:

- `exports/wrover-case-v7.2.0-base.stl`: v7.1.0 base on a 3 mm dovetail plinth, marked `W7.2.0 BASE` underneath
- `exports/wrover-case-v7.2.0-lid.stl`: unchanged v7.1.0 lid
- `exports/wrover-case-v7.2.0-din-clip.stl`: separate snap-on clip for TS35 × 7.5 mm (EN 60715) top-hat rail

The closed case is now 86 × 46.6 × 20.6 mm. The clip holds the case front 11 mm off the rail face, so the case sits about 39 mm proud of the mounting panel.

## Why a separate clip

A clip moulded into the floor would stop the base printing flat and would need support under the whole case. Instead, the base gets a flat 3 mm plinth with two dovetail grooves cut into its underside. The clip prints on its side, so its spring and hooks are built from layer-parallel walls and flex along the layer lines rather than across them. Neither part needs supports. No screws, glue or metal parts are used.

## How it mounts

It snaps on and off anywhere along the rail, like a breaker. You never slide it on from the rail end.

The rail runs across the case width, along Y. On a level rail the case stands with its long axis vertical and takes about 2.6 DIN modules (46.6 mm) of rail.

- **Fixed hook** at the far end, away from the USB. Hook it over the top rail edge first.
- **Sprung latch** at the USB end. A five-beam serpentine spring lets the latch block slide outward. Its lower face is a 44° ramp, so rotating the case onto the rail snaps it on. With the latch at the bottom, the USB end faces down.
- **Pull tab**: a lip stands 2.5 mm beyond the USB end of the case. To remove the case, put a small flat screwdriver or a fingernail in the gap between the lip and the case, pull the lip outward about 1.7 mm, and tilt the case off the rail.

## Clip to base

1. Slide the clip into the grooves from the open side of the plinth, with the dovetails leading. A shallow ramp at the plinth edge presses the detent finger down.
2. Push the clip until it stops at the blind groove end. The finger's ridge drops into a pocket in the plinth and the clip clicks into place, centred under the case.
3. The pocket's back edge is a 45° ramp. The clip is held against vibration and casual knocks, and comes out with a firm pull along the rail direction. This is a detent, not a positive lock.

Fit the clip to the base before putting the case on the rail. Once the case is on the rail, the rail can't stop the case sliding along the clip; only the detent does.

## Key dimensions

| Feature | Value |
|---|---|
| Rail | TS35 × 7.5, 1.0 mm lip |
| Hook reach behind the rail lip | 1.5 mm each side |
| Clearance behind the lip | 0.40 mm fixed hook (to allow rocking on), 0.25 mm latch |
| Latch travel to clear the rail edge | 1.7 mm (release envelope checked) |
| Serpentine | 5 beams, 1.0 mm thick, about 9 mm free length, 1.2 mm gaps |
| Dovetails | 8 mm neck, 2.4 mm deep, 30°, 0.2 mm clearance per face |
| Detent | 0.8 mm ridge on a 1.2 mm finger, 1.0 mm deep pocket |
| Clip | 67.3 mm along the case, 26 mm along the rail |

## Printing

- **Base**: floor down, as for v7.1.0. The groove roofs and the detent pocket bridge about 11 mm and 3 mm respectively; no supports. Check the first layers: elephant's foot will narrow the dovetail mouths.
- **Clip**: lay it on its side, profile face down, 26 mm tall. PLA works, and PETG is the kinder material for the latch spring. Use at least 3 walls so that the 1.0 mm spring beams and the 1.2 mm detent finger print solid. No supports; nothing overhangs.
- **Lid**: unchanged from v7.1.0.

## Verification and limits

`verify_geometry.py` reruns all eleven v7.1.0 closure and PCB checks against the unchanged upper base, plus eleven DIN checks:

- The seated clip clears the base.
- The whole slide-in path is clear, with the detent ridge sprung down.
- Overtravel hits the blind groove end.
- Backing the clip out 0.6 mm meets the detent ramp.
- The seated clip clears the rail.
- Both hooks overlap the rail lip on a pull-off attempt.
- With the latch pulled 1.7 mm, the rail passes it on a straight-line path.
- The pulled latch doesn't hit the rigid clip body or the case.
- Mid-rail fitting, checked in reverse as removal: with the latch pulled, the case rotates about the fixed hook to 10° without touching the rail.
- At 10°, with the latch at rest, the case shifts 1.8 mm towards the fixed end and lifts straight off. At 6° and 8° it still collides, so it needs a proper tilt.

The results are in `geometry-results.json`.

These are rigid-body clearance checks. The spring beams are excluded from the pulled-latch check because they bend in service. Snap-on force, spring strain, detent hold and the real hand motion have not been calculated or measured. The tilt checks cover one rigid path through the rail's cross-section. The 1.0 mm beams take roughly 0.33 mm of deflection each at full latch travel. That is a modest bending strain for PLA, but it is an estimate, not a test.

Things to check on the first print:

- The clip slides into the base with hand pressure and clicks home.
- It snaps onto a real rail and comes off with the pull tab.
- The case doesn't rattle on the rail.

## Rebuild

From this directory:

```sh
openscad -o exports/wrover-case-v7.2.0-base.stl -D 'part="base"' wrover-case-v7.2.0.scad
openscad -o exports/wrover-case-v7.2.0-din-clip.stl -D 'part="din_clip"' wrover-case-v7.2.0.scad
python3 ../../tools/verify_meshes.py exports/wrover-case-v7.2.0-base.stl exports/wrover-case-v7.2.0-lid.stl exports/wrover-case-v7.2.0-din-clip.stl
python3 verify_geometry.py
```

The lid is copied from v7.1.0 rather than re-exported. OpenSCAD 2021.01 STL output isn't byte-reproducible, and copying keeps the equal hash as evidence of reuse. Setting `din_mount = false` rebuilds the plain v7.1.0 base geometry.
