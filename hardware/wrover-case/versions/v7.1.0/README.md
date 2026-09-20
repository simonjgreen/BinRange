# WROVER case v7.1.0 — overlapping shells

**Status: working design, accepted by the user for commit and publication.**

Acceptance is recorded separately from measured test results. No quantified retention, thermal or endurance report has been supplied. The source header records the original candidate state; this README and `status.json` carry the current acceptance status.

The user reported v7.0.0 as “really good”. v7.1.0 addresses the two requested refinements: closer-fitting cone roots and a centred vent pattern.

Both v7.1.0 parts mate with v7.0.0 counterparts. Print the new base for the tighter board location; the new lid is optional if the vent position does not bother you. Neither revision mates with v5/v6. The two print files are:

- `exports/wrover-case-v7.1.0-base.stl`
- `exports/wrover-case-v7.1.0-lid.stl`

The base and lid carry recessed `W7.1.0 BASE` / `W7.1.0 LID` marks on their outside faces. Overall closed size is 86 × 46.6 × 17.6 mm, with up to 0.3 mm nominal initial lid lift against the catches. The supplied parts lie flat in print orientation. No screws, nuts, glue, metal springs or separate fastening pieces are required.

## Changes from v7.0.0

- The cones now measure **3.0 mm diameter exactly at the PCB seating plane**, matching the nominal mounting holes. The old root diameter was 2.5 mm. The 1.5 mm tip, 3 mm cone height, hole positions and support height are unchanged. The taper continues slightly below the seating plane solely to overlap the support in CAD. It does not introduce an oversized shoulder above the support.
- The same five 1.4 × 12 mm vent slots are centred on both lid axes. The pattern moves 5.7 mm towards the USB end; its area and spacing do not change. There is no component-specific duct in this design. No temperature measurements or thermal comparison have been made.
- Closure and PCB pressure-spring geometry are unchanged; case markings now read W7.1.0.

The 3.0 mm root is a nominal line fit, not a guaranteed printer-calibrated fit. If a printed cone is oversized or a real hole smaller than nominal, the PCB may sit high; check that it rests on all four supports before closing. Do not force it down. Check this fit on each new printer or filament combination.

## Retained closure

The lid has a 4 mm deep outer skirt. It overlaps the base with 0.25 mm clearance per side. Four short beads on the base sides engage four skirt windows. The base walls are 1.6 mm thick and the skirt is 1.2 mm thick; the exposed base walls provide the intended flex for insertion and release. There are no tall isolated catch tongues or torsion arms.

Each bead projects 0.65 mm from the base wall, giving 0.40 mm nominal engagement beyond the skirt clearance. It has a gradual entry ramp and a shorter retaining bevel. The CAD release envelope uses 0.60 mm inward travel. These are prototype dimensions, not a measured release force or a strain calculation. The bevel permits removal under sufficient pull; this is not advertised as an absolute positive lock.

The PCB mounting grid, support height, four spring leaves and hollow contact feet retain the v5.1 dimensions; cone roots are the change noted above. Three spring modules are identical in source. Their nominal free preload is 0.9 mm; initial catch contact retains approximately 0.6 mm deflection. Further elastic movement under load can change this. The PCB springs must not be joined to the skirt or filled by support material.

The approach is informed by the [maker research](../../research/other-maker-examples.md), particularly the Adafruit shell closures and YAPP overlap geometry. This is an original adaptation, not a dimension-for-dimension copy or a tested commercial latch.

## Printing and assembly

1. Print the full base and, for the centred vents, the full lid, exterior faces on the bed, using ordinary PLA. Start with a 0.4 mm nozzle, 0.16 mm layers, 4 wall loops, 5 top/bottom layers and 20% infill. The narrow walls/leaves should be solid. Do not scale either part.
2. Inspect the slicer around the four bead undersides and the approximately 10.8 mm skirt-window bridges. The small retaining bevel is intended to print without supports; its finish matters. Avoid automatic supports inside the spring slots and peg sockets. Remove strings or elephant foot that obstruct the seam.
3. Lower the PCB onto its original peg pattern, USB at the open end. Align the lid's button-access holes with the two buttons. Lower the lid vertically and press near each catch in turn. Stop if the walls whiten or closure needs heavy force.
4. To open, press the exposed long base walls inward just below the lid skirt, near the catches, while lifting that end gently. A fingernail through a skirt window can also press a bead inward. Release the other end next. Check the release action on your print.
5. With the PCB fitted, check all four catches, USB plug access and button access. Confirm that ordinary handling does not release the lid, then gently shake with the cable disconnected to check for board rattle. Recheck after a day closed to assess retained board pressure. Report retention, insertion/release effort and any movement separately.

## Verification and limits

See `geometry-results.json` for the actual geometric-check results. Checks include a solid-PCB seating interference check and cover closed fit, the rigid insertion path, approximate component envelopes, catch overlap on both sides, locating stops and an inward-shifted bead release envelope. The seating interference check excludes 0.001 mm at the intentional support-contact plane to avoid numerical slivers; the exact 3.0 mm diameter at that plane is also checked analytically. The release check is not a simulation of a bending wall. The pressure feet intentionally overlap the unloaded PCB model because they deflect in service.

`../../tools/verify_meshes.py` checks the exported STLs for one connected, closed, consistently oriented manifold and a flat z=0 printing position. These checks do not establish PLA strength, fatigue life, dimensional accuracy, latch holding force or rattle-free performance. Those require physical testing.

## Rebuild

From this directory:

```sh
openscad -o exports/wrover-case-v7.1.0-base.stl -D 'part="base"' wrover-case-v7.1.0.scad
openscad -o exports/wrover-case-v7.1.0-lid.stl -D 'part="lid"' wrover-case-v7.1.0.scad
python3 ../../tools/verify_meshes.py exports/wrover-case-v7.1.0-base.stl exports/wrover-case-v7.1.0-lid.stl
python3 verify_geometry.py
```

The source also provides `layout`, `assembled` and `exploded` views. Future geometry changes must receive a new version rather than silently replacing these files after this working version is delivered.
