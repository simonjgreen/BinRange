# Screwless enclosure research for the WROVER case

Research date: 2026-09-20. This is evidence gathering, not a new design or a claim that another enclosure's dimensions transfer directly to our WROVER case.

## Strong PLA examples

### Adafruit PyGamer snap-fit case — Ruiz Brothers

The PCB rests on built-in standoffs and is captured between two snapping halves without screws. The makers describe opening it by pressing the marked edges on both sides, starting near the top. Their printing page specifies PLA, reports tests on an Ultimaker 3 and Flashforge Inventor II, requires no supports, and offers original Fusion 360 and STEP files. This is a documented physical build, not merely a CAD rendering. The display bezel uses glue, so the complete accessory is not exclusively printed assembly, but the case closure and PCB retention are screwless. No cycle-life or retention-force measurements are provided. [Assembly and overview](https://learn.adafruit.com/pygamer-snapfit-case?view=all), [printing and source files](https://learn.adafruit.com/pygamer-snapfit-case/3d-printing), [linked download](https://www.thingiverse.com/thing:3686964).

**Relevance:** A vertical assembly can capture the PCB and let the case edges provide the release action. We should inspect the original geometry before copying exact latch dimensions; text alone does not establish hook angles or deformation paths.

An independent published make by Lucina reports that the case fits well and prints easily. The make does not identify filament or quantify latch strength. The documented squeeze-to-open procedure is useful evidence of intentional release behavior, but does not prove it cannot also be pulled apart. Direct model downloads could not be inspected during this pass, so no claim is made about exact retaining-face angles. [Independent make](https://cults3d.com/en/3d-printing/pygamer-snap-fit-case), [creator's opening instructions](https://learn.adafruit.com/pygamer-snapfit-case/assembly).

### Adafruit Fruit Jam case — Noe Ruiz

The enclosure requires no hardware fasteners. Assembly places the PCB on locating standoffs, then presses the upper and lower halves together. The maker explicitly reports PLA/FDM testing and publishes Fusion 360/STEP source plus 3MF files. Their reference settings include 0.2 mm layers, 0.42 mm lines, and two wall loops. They also warn that the parts have tight tolerances. The guide includes assembly photographs, but does not publish retention-force or fatigue data. [Creator's guide, CAD links and assembly](https://learn.adafruit.com/fruit-jam-case?view=all).

**Relevance:** Confirms that a hardware-free PCB enclosure in ordinary PLA is practical. It does not establish that the same closure will remain secure under our lid's four continuously loaded PCB springs.

### Circuit Playground case — Ruiz Brothers (closure-only precedent)

This design has nubs on the inside edges mating with recesses in an overlapping lip. The creator reports PLA testing and provides STL and editable CAD downloads. Its board is attached with M3 screws, so it is an example of a screwless **case closure**, not an entirely screwless enclosure. [Original design and print notes](https://learn.adafruit.com/case-for-circuit-playground).

**Relevance:** Particularly close to the user's proposed lip-and-snap arrangement. Locating overlap and shallow mating features offer a simpler benchmark than tall isolated catch stalks. Treat improved suitability as an engineering inference, not proof of a drop-in fix. These are releasable snap detents; the documentation does not establish a lock that can only be opened by pressing release tabs.

## Useful sliding mechanism, but not PLA evidence

Florian's TTGO T5 VPN controller is a real published ESP32 project with FreeCAD files. Its back lid slides and is locked by a pin; bending the lid outward releases that pin. Crucially, it was designed for **SLS PA12**, not PLA FDM. The creator also documents first-print problems: 0.5 mm clearances allowed the PCB to slide around, and added packing was required. It should not be presented as a proven no-rattle PLA solution. [Original repository and print feedback](https://github.com/flrs/vpn_remote_control_gadget), [editable CAD](https://github.com/flrs/vpn_remote_control_gadget/tree/main/cad/design).

**Relevance:** Demonstrates separation between a sliding cover and its release catch. For our case, a full-length sliding lid cannot retain the existing tall hold-down pillars unchanged: they would sweep across the board components. It would require separate fixed PCB retention or another closure path. That is our geometry assessment, not a claim from this maker.

## OpenSCAD reference: YAPP — Willem Aandewiel

YAPP uses an overlapping rim and bulges that engage lid openings. Inspection of `printSnapJoins()` in the downloaded `YAPPgenerator_v3.scad` confirms round or diamond-section features integrated into the shell, rather than our tall isolated tongues. The generator includes PCB locating pins and opposing lid supports. [Project and editable source](https://github.com/mrWheel/YAPP_Box), [snap geometry source](https://github.com/mrWheel/YAPP_Box/blob/main/YAPPgenerator_v3.scad).

The author's original article includes completed-print feedback from users, including a Wemos enclosure, but the reviewed reports do not establish PLA material or measured retention strength. This is a useful geometry reference, not proof of a positive press-to-release lock. [Author's explanation and reader reports](https://willem.aandewiel.nl/index.php/2022/01/02/yet-another-parametric-projectbox-generator/).

## A deliberately pull-off example

Philipp Schweizer's UNO Q enclosure is reported printed in PLA with four diamond-profile snaps and downloadable OpenSCAD files. Its instructions explicitly open it by pulling upward: both sides of the snaps are chamfered. That is useful evidence of a printable enclosure, but the intended release behaviour does not meet our wish for stronger retention requiring deliberate release. [Creator's build and downloads](https://raspberry.tips/en/3d-druck/arduino-uno-q-case-3d-printed).

## A useful independent follow-up on the lip approach

Probonopd's Onshape tutorial builds an overlapping shell with mating chamfered catches, acknowledges the Adafruit approach, and discusses clearance and retention angle separately. A reader reports that changing the angle to 33 degrees worked well for their enclosure. This is one person's feedback, not a general tolerance specification or a durability study. [Tutorial and discussion](https://gist.github.com/probonopd/5a3fa855498af0d3224c110723781efe).

## Recommendation for the next design decision

Use an overlapping lip with short, distributed snap features as the first geometry benchmark, with vertical assembly preserving the working PCB location and hold-down concept. Inspect actual source cross-sections before choosing dimensions. Retention must also be evaluated against the spring preload already present in this case; a generic storage-box detent may be too easy to pull open.

The PyGamer is the strongest comparable PLA build found in this pass. Its original model download was inaccessible during this research, so exact geometry has not been inspected. Its squeeze-opening instructions do not prove that straight pulling cannot also release it. A future adaptation should treat the base and lid as a matched pair; compatibility with the existing rigid base must not dictate another fragile tongue. Keep the physically successful board pegs and hold-down principle, and evaluate any next candidate as a full case with the PCB fitted, as the user requested.

Do not assume that deeper protrusions, a thicker root, or a watertight mesh prove a PLA latch will work. The makers above provide stronger evidence of printability than our new torsion-catch concept, but none establishes our required opening force or lifetime. If positive press-to-release locking is essential, a slide-and-stop design remains a candidate, with PCB retention redesigned explicitly rather than silently swept sideways.

No case geometry was modified during this research.
