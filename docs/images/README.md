# README hardware images

The public hardware gallery uses the following derivatives. Private originals
and Live Photo videos are not included here.

| Asset | Subject / source |
| --- | --- |
| `wrover-case-v7.1.0-assembled.jpg` | Accepted v7.1.0 base/lid STL meshes, assembled |
| `wrover-case-v7.1.0-open.jpg` | The same meshes, separated to show their interiors |
| `k4w-tag-installed-green-bin.jpg` | User-supplied installation photo, cropped |
| `k4w-tag-installed-food-caddy.jpg` | User-supplied installation photo, cropped |
| `k4w-tag-installed-brown-bin.jpg` | User-supplied installation photo |
| `tagged-bins-installation.jpg` | User-supplied installation overview |
| `makerfabs-wrover-dw3000-board.jpg` | Existing local development-board photo, cropped |

The case renders are generated with
[`render_readme.py`](../../hardware/wrover-case/tools/render_readme.py), using
Blender 4.5.3, soft studio lighting and illustrative polymer materials. They
preserve the printable geometry. They are not photographs of a manufactured
case. The board is omitted.

Photos are at most 1200 × 900 pixels; renders are 1600 × 1000. All seven images
are optimized progressive JPEGs. Orientation is baked into the pixels before
export. Each image is copied into a fresh RGB image and encoded without EXIF,
GPS, XMP, IPTC, comments, embedded thumbnails or source colour profiles.

Readable device QR codes and identifiers are covered with opaque masks before
resizing. The food-caddy crop also excludes the label on its lid. Model names
and regulatory markings remain where possible. Original filenames, capture
timestamps, device identities and local source paths are not recorded here.

For future additions, inspect the pixels as well as metadata: strip metadata,
check for labels, faces and addresses, export a fresh derivative, then verify
the result and its README link. Existing walk-test screenshots are separate
historical assets and are not part of this photo export.
