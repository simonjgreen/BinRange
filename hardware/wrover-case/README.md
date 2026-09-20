# WROVER case — working design v7.1.0

**Working design: [v7.1.0](versions/v7.1.0/README.md), accepted by the user. Both halves remain compatible with v7.0.0.**

[Complete v7.1.0 print package](releases/wrover-case-v7.1.0.zip) · [Full base STL](versions/v7.1.0/exports/wrover-case-v7.1.0-base.stl) · [Full lid STL](versions/v7.1.0/exports/wrover-case-v7.1.0-lid.stl) · [OpenSCAD source](versions/v7.1.0/wrover-case-v7.1.0.scad) · [Preview](versions/v7.1.0/exports/wrover-case-v7.1.0-layout.png)

The user reports v7.0.0 as “really good” and has accepted v7.1.0, with wider locating cones and centred vents, as the working design. No quantified retention, thermal or endurance results have been reported.

## Version history

Old names have been normalised without changing the archived geometry. The historical v5.1 is now v5.1.0, for example.

| Version | Closure | Physical result | Base compatibility |
|---|---|---|---|
| v1.0.0 | Original screws | Original board fit check passed; standalone source not found | Not established |
| [v2.0.0](versions/v2.0.0/README.md) | Recessed M2.5 screws | Superseded small-hardware approach | v2 only |
| [v3.0.0](versions/v3.0.0/README.md) | M2 bolts and nuts | Rejected: PLA fits and small hardware impractical | v3 only |
| [v4.0.0](versions/v4.0.0/README.md) | External flexible clips | Pegs and PCB springs worked; lid loose | v4 only |
| [v5.0.0](versions/v5.0.0/README.md) | Internal lip, 0.6 mm hooks | Good fit; insufficient retention | v5 base |
| [v5.1.0](versions/v5.1.0/README.md) | 1.0 mm hooks | Better retention; still pulls off easily | v5 base |
| [v5.2.0](versions/v5.2.0/README.md) | 3.0 mm full-wall hooks | **Failed: both roots broke on insertion** | v5 base |
| [v6.0.0](versions/v6.0.0/README.md) | Torsion-arm catches | CAD checks only; untested, superseded by research direction | v5 base |
| [v7.0.0](versions/v7.0.0/README.md) | Overlapping shells, four shallow beads | **User-tested: “really good”; two refinements requested** | v7.0.0 / v7.1.0 halves |
| [v7.1.0](versions/v7.1.0/README.md) | Same closure; 3 mm cone roots, centred vents | **Working design: accepted by user** | v7.0.0 / v7.1.0 halves |

## Folder and naming rules

```text
wrover-case/
  README.md                  Start here: working version, history, compatibility
  versions.json              Machine-readable version/status index
  versions/v7.1.0/            One self-contained design snapshot
    README.md                Printing, assembly, evidence and limitations
    status.json              Physical status and part hashes
    wrover-case-v7.1.0.scad   Versioned source
    verify_geometry.scad     Collision/engagement checks
    verify_geometry.py       Check runner
    geometry-results.json    Recorded results, not a physical-test certificate
    exports/
      wrover-case-v7.1.0-base.stl
      wrover-case-v7.1.0-lid.stl
      wrover-case-v7.1.0-layout.png
  releases/                  Versioned distribution ZIPs
  tools/                     Shared mesh validation
  sources/                   Board dimensions and original PCB evidence
  research/                  Maker references and design findings
  archive/                   Verified, byte-preserved original file tree ZIP
```

- Use `vMAJOR.MINOR.PATCH`, numerically sorted. Increment **major** for a new closure architecture or incompatible mating geometry; **minor** for a compatible geometry variant; **patch** for corrected exports or manufacturing details with the same intended fit. A patch is still a new printable file, never a silent overwrite.
- Treat delivered sources and print meshes as snapshots. Store subsequent geometry under a new version. Status/physical feedback may be updated without changing geometry.
- Use `wrover-case-v<version>-<part>.<extension>` for printable/downloadable files. Do not use floating `base.stl`, `lid.stl`, `latest`, `final`, or an unqualified `trial` filename.
- Each version directory includes its complete matching base/lid pair, even when a base is unchanged. Equal base hashes document actual reuse. The compatibility table overrides assumptions based on version numbers; historical v6 retained the v5 base.
- Record physical status separately from version: working, experimental, user-tested, partial-success, failed, rejected or superseded. A newer number is not proof of improvement.
- Produce full-case candidates. Small closure coupons are historical only, following the user's printing preference.

## Preserved history

All 67 previously present case files (excluding Python bytecode and the unchanged board/research directories) were preserved and SHA-256 verified in [the original-layout archive](archive/README.md) before reorganisation. This includes prior ZIPs, duplicated aliases, small coupons, previews and historical instructions. Canonical historical source/STL copies retain their original bytes; verification includes were updated to the renamed sources.

The unversioned `case.3mf` and `base.png` could not be assigned a reliable version and remain labelled as unverified historical assets in the archive. No v1 source has been invented. The board evidence remains in `sources/`.
