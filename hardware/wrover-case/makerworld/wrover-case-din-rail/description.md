# DIN rail case for Makerfabs ESP32 UWB DW3000 (WROVER)

This is the screwless WROVER case with a snap-on mount for standard 35 mm top-hat (TS35 × 7.5) DIN rail. It fits mid-rail like a breaker, so you don't need to slide it on from the rail end. It needs no screws, glue or supports.

- **Base**: the standalone case base on a 3 mm plinth with two dovetail grooves underneath. Closed size: 86 × 46.6 × 20.6 mm.
- **Lid**: identical to the standalone version.
- **DIN clip**: a separate part that slides into the dovetails and clicks home. It has a fixed hook at one end and a spring latch with a pull tab at the USB end.
- **On the rail**: the case takes about 2.6 modules (46.6 mm) of rail. The USB end faces down, and the front sits about 39 mm off the mounting panel.

## Files

- `wrover-case-din-rail-v7.2.0.3mf`: base, lid and clip on one plate, in print orientation
- `wrover-case-v7.2.0-base.stl`, `wrover-case-v7.2.0-lid.stl`, `wrover-case-v7.2.0-din-clip.stl`: the same parts as individual STLs

## Print settings

- 0.4 mm nozzle, 0.16 mm layers, 4 walls, 5 top and 5 bottom layers, 20% infill
- No supports. Keep the parts in the orientation they're laid out in:
  - The base and lid print outside face down.
  - The clip prints on its side, 26 mm tall, so its spring flexes along the layer lines.
- The base and lid work in PLA. PETG is the better choice for the clip's spring.
- Watch for elephant's foot on the base, because it narrows the dovetail grooves.

## Assembly

1. Fit the board and lid exactly as in the standalone case.
2. Slide the clip into the plinth grooves from the open side until it stops and clicks.
3. Put it on the rail: tilt the case about 10° and hook the fixed end over the top rail edge. Then rotate the USB end down until the latch snaps over.
4. To remove it: put a screwdriver or fingernail between the pull tab and the case. Pull the tab outward, tilt the USB end up and lift the case off.

## Status

The case itself is the maker's current working design. The DIN mount is new and has passed CAD clearance checks only; it hasn't been printed yet. Snap-on force and detent hold haven't been measured. Feedback and makes are welcome.
