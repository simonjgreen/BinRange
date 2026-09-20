// Makerfabs ESP32 UWB DW3000 / WROVER-E enclosure. All dimensions in mm.
// Published v1.0 Eagle outline: 72 x 32.6; holes 68 x 28.6, diameter 3.
// Current USB-C board visually cross-checked against 2026-09-19 user photo.
// V3: corner guides, positive PCB clamping, M2 screws and side-loaded M2 nuts.
// Original board outline/hole pattern passed the user's physical fit check.
// V3 needs a new base AND lid; do not mix with the legacy M2.5 parts.
// See README.md for provenance, printing and screw selection.

/* [View] */
part = "layout"; // [layout,base,lid,assembled,exploded,fitcheck,test_base,test_lid]
show_board = true;

/* [PCB - published outline] */
pcb_length = 72;
pcb_width = 32.6;
hole_inset_x = 2;
hole_inset_y = 2;
pcb_hole_d = 3;

/* [PCB - check on your board] */
pcb_thickness = 1.6;
// Clearance below PCB for solder joints / underside components; no pin headers.
under_board = 4;
// Height above PCB top to lid underside.
above_board = 8;
// Lid pillars contact the PCB. A relieved case rim lets the screws clamp it.
// This is an intentional seam at the rim, NOT a gap above the board.
rim_relief = 0.3;

/* [Board location] */
// Per-edge insertion clearance. Guides locate; screw clamping prevents rattle.
guide_gap = 0.15;
guide_length = 5;
// Guides engage the lower portion of the PCB edge, below the lid pillars.
guide_height = 1;
guide_chamfer = 0.4;

/* [Shell] */
wall = 2;
floor_thickness = 2;
lid_thickness = 2;
// Air gap between PCB edge and inner wall; also accommodates lid locating lip.
edge_gap = 2;
corner_radius = 3;
lip_depth = 2;
lip_wall = 1;
// Per-side clearance between lid lip and case wall.
lid_clearance = 0.3;

/* [Fasteners] */
// Four M2 x 12 mm socket BUTTON-head screws + four M2 nuts from the RED kit.
// Button-head screw length is measured UNDER the head. No plastic threads.
screw_length = 12;
screw_d = 2;
screw_clearance_d = 2.4;
// PCB-contact footprints stay 5 mm; bases widen to contain the hex nuts.
post_d = 5;
base_post_d = 7.2;
bore_floor = 1;
// Reference head envelope, not a measurement of the user's kit.
screw_head_d = 3.5;
screw_head_h = 1.3;
// Flat-bottom counterbore for a button head; NOT a countersunk cone.
head_well_d = 4.4;
head_well_depth = 3.4;
lid_post_d = 6.4;
lid_post_taper_h = 2;

/* [Nut pockets] */
nut_af = 4;
nut_h = 1.6;
// Total extra across-flats width and height, not per-side values.
nut_clearance_xy = 0.4;
nut_clearance_z = 0.2;
nut_floor_z = 3;

/* [USB-C opening - check cable moulding] */
// Coordinates are in PCB frame: USB at x=0; y=0 is UWB antenna edge.
usb_y = 16;
usb_width = 16;
// Bottom of cable opening relative to TOP of PCB; negative extends below it.
usb_bottom_offset = -2;

/* [Button access - legacy CAD estimates] */
button_access = true;
reset_x = 2.4166;
reset_y = 8.1851;
flash_x = 2.5;
flash_y = 24.532;
button_access_d = 5;

/* [Ventilation] */
vents = true;
// Slots over the ESP32 shield, away from the UWB antenna and corner pillars.
vent_width = 1.4;

/* [Hidden] */
$fn = 64;
eps = 0.02;
ox = wall + edge_gap;
oy = wall + edge_gap;
outer_l = pcb_length + 2 * ox;
outer_w = pcb_width + 2 * oy;
pcb_z = floor_thickness + under_board;
pcb_top = pcb_z + pcb_thickness;
lid_bottom = pcb_top + above_board;
body_h = lid_bottom - rim_relief;
total_h = lid_bottom + lid_thickness;
retainer_h = above_board;
lid_post_h = lid_thickness + retainer_h;
wide_post_h = lid_post_h - lid_post_taper_h;
nut_pocket_af = nut_af + nut_clearance_xy;
nut_pocket_h = nut_h + nut_clearance_z;
nut_roof_z = nut_floor_z + nut_pocket_h;
// Tightening pulls each nut up against its pocket roof.
nut_seated_bottom = nut_roof_z - nut_h;
screw_tip_z = total_h - head_well_depth - screw_length;
screw_head_depth = head_well_depth - screw_head_h;
guide_top = pcb_z + guide_height;
usb_bottom = pcb_top + usb_bottom_offset;
holes = [[hole_inset_x,hole_inset_y],
         [pcb_length-hole_inset_x,hole_inset_y],
         [pcb_length-hole_inset_x,pcb_width-hole_inset_y],
         [hole_inset_x,pcb_width-hole_inset_y]];

assert(wall >= 1.2 && floor_thickness >= 1.2 && lid_thickness >= 1.2);
assert(pcb_length > 20 && pcb_width > 20);
assert(hole_inset_x > pcb_hole_d/2 && hole_inset_y > pcb_hole_d/2);
assert(edge_gap > lid_clearance + lip_wall,
       "Lid lip must clear the PCB edge.");
assert(under_board > 0 && above_board > lip_depth);
assert(rim_relief > 0 && rim_relief < lip_depth-0.5,
       "Leave rim relief for PCB clamping and at least 0.5 mm lip engagement.");
assert(guide_gap > 0 && guide_gap+guide_chamfer < edge_gap);
assert(guide_height > guide_chamfer && guide_height < pcb_thickness-0.2);
assert(screw_d < screw_clearance_d && screw_clearance_d < pcb_hole_d);
assert(post_d > screw_clearance_d + 1.6);
assert(screw_head_d > screw_clearance_d && head_well_d > screw_head_d);
assert(head_well_depth > screw_head_h);
assert(lid_post_d >= head_well_d + 2,
       "Keep at least 1 mm of pillar wall around the head well.");
assert(lid_post_taper_h > 0 && wide_post_h > lid_thickness);
assert(head_well_depth < wide_post_h && lid_post_h-head_well_depth >= 2);
assert(lid_post_d/2 < edge_gap+min(hole_inset_x,hole_inset_y),
       "Lid pillars must clear the case walls.");
assert(nut_clearance_xy > 0 && nut_clearance_z > 0);
assert(base_post_d > nut_pocket_af/cos(30)+2,
       "Keep at least 1 mm of post around the back of the hex pocket.");
assert(nut_floor_z >= floor_thickness);
assert(pcb_z-nut_roof_z >= 1,
       "Keep at least 1 mm of support above the nut pocket.");
assert(usb_bottom > floor_thickness && usb_bottom < body_h);
assert(usb_y-usb_width/2 > hole_inset_y+post_d/2);
assert(usb_y+usb_width/2 < pcb_width-hole_inset_y-post_d/2);
assert(screw_tip_z <= nut_seated_bottom-0.4,
       "Screw must pass all the way through the nut with 0.4 mm spare.");
assert(screw_tip_z >= bore_floor+0.5,
       "Screw too long: may bottom out in its clearance bore.");
assert(part == "layout" || part == "base" || part == "lid" ||
       part == "assembled" || part == "exploded" || part == "fitcheck" ||
       part == "test_base" || part == "test_lid");

echo(str("Case: ",outer_l," x ",outer_w," x ",total_h," mm"));
echo(str("Hole pitch: ",pcb_length-2*hole_inset_x," x ",
         pcb_width-2*hole_inset_y," mm"));
echo(str("M2 x ",screw_length," button: head ",screw_head_depth,
         " mm below lid; nut engagement ",nut_h,
         " mm; tip ",screw_tip_z," mm above case underside"));
echo(str("PCB clamped between supports; intentional rim seam ",rim_relief," mm"));

module rounded_prism(l,w,h,r) {
    linear_extrude(height=h)
        hull() for(x=[r,l-r],y=[r,w-r]) translate([x,y]) circle(r=r);
}

module at_holes(z=0) {
    for(p=holes) translate([ox+p[0],oy+p[1],z]) children();
}

// Open-top notch avoids bridging the USB opening when printing the base.
module usb_cut() {
    translate([-eps,oy+usb_y-usb_width/2,usb_bottom])
        cube([ox+eps,usb_width,total_h-usb_bottom+eps]);
}

// One corner's guide in PCB coordinates. The top chamfer helps the board drop in.
module edge_guide() {
    translate([-edge_gap-eps,-edge_gap-eps,floor_thickness-eps])
        cube([edge_gap+eps-guide_gap,edge_gap+eps+guide_length,
              guide_top-guide_chamfer-floor_thickness+eps]);
    hull() {
        translate([-edge_gap-eps,-edge_gap-eps,guide_top-guide_chamfer-eps])
            cube([edge_gap+eps-guide_gap,edge_gap+eps+guide_length,eps]);
        translate([-edge_gap-eps,-edge_gap-eps,guide_top-eps])
            cube([edge_gap+eps-guide_gap-guide_chamfer,
                  edge_gap+eps+guide_length,eps]);
    }
}

module board_guides() {
    for(sx=[-1,1],sy=[-1,1])
        translate([ox+(sx==1 ? 0 : pcb_length),
                   oy+(sy==1 ? 0 : pcb_width),0]) scale([sx,sy,1]) {
            edge_guide();
            mirror([1,-1,0]) edge_guide();
        }
}

module nut_pocket() {
    // Hex flats parallel to entry-slot sides. Entry points towards case centre.
    cylinder(d=nut_pocket_af/cos(30),h=nut_pocket_h,$fn=6);
    translate([0,-nut_pocket_af/2,0])
        cube([base_post_d/2+eps,nut_pocket_af,nut_pocket_h]);
}

module base() {
    difference() {
        union() {
            difference() {
                rounded_prism(outer_l,outer_w,body_h,corner_radius);
                translate([wall,wall,floor_thickness])
                    rounded_prism(outer_l-2*wall,outer_w-2*wall,
                                  body_h, max(0.5,corner_radius-wall));
            }
            at_holes(floor_thickness-eps) {
                cylinder(d=base_post_d,h=nut_roof_z-floor_thickness+eps);
                translate([0,0,nut_roof_z-floor_thickness+eps])
                    cylinder(d1=base_post_d,d2=post_d,h=pcb_z-nut_roof_z);
            }
            board_guides();
        }
        // Free-running clearance all the way to the nut; no tapping of plastic.
        at_holes(bore_floor)
            cylinder(d=screw_clearance_d,h=pcb_z-bore_floor+eps);
        for(p=holes) translate([ox+p[0],oy+p[1],nut_floor_z])
            rotate([0,0,p[0]<pcb_length/2 ? 0 : 180]) nut_pocket();
        usb_cut();
    }
}

// Lid is defined in print orientation: exterior face on z=0, pillars upwards.
// A Y reflection maps it back onto the base when it is turned over.
module lid_pillar() {
    // Start in the plate for a robust boolean union; taper is support-free.
    translate([0,0,lid_thickness-eps])
        cylinder(d=lid_post_d,h=wide_post_h-lid_thickness+eps);
    translate([0,0,wide_post_h])
        cylinder(d1=lid_post_d,d2=post_d,h=lid_post_taper_h);
}

module screw_well() {
    translate([0,0,-eps]) cylinder(d=head_well_d,h=head_well_depth+eps);
    // Flat shoulder bears on the underside of the button head.
    translate([0,0,-eps]) cylinder(d=screw_clearance_d,h=total_h+eps);
}

module lid_print() {
    difference() {
        union() {
            rounded_prism(outer_l,outer_w,lid_thickness,corner_radius);
            // Locating lip stays outside PCB; screws provide retention.
            translate([wall+lid_clearance,wall+lid_clearance,lid_thickness-eps])
                difference() {
                    rounded_prism(outer_l-2*(wall+lid_clearance),
                                  outer_w-2*(wall+lid_clearance),lip_depth+eps,1);
                    translate([lip_wall,lip_wall,-eps])
                        rounded_prism(outer_l-2*(wall+lid_clearance+lip_wall),
                                      outer_w-2*(wall+lid_clearance+lip_wall),
                                      lip_depth+3*eps,0.5);
                }
            // Wider screw wells taper to the original PCB retaining footprint.
            at_holes() lid_pillar();
        }
        at_holes() screw_well();
        // Clear the lip from the USB cable path. Top plate remains continuous.
        translate([-eps,outer_w-(oy+usb_y+usb_width/2),lid_thickness])
            cube([ox+eps,usb_width,lip_depth+eps]);
        if(button_access)
            for(p=[[reset_x,reset_y],[flash_x,flash_y]])
                translate([ox+p[0],outer_w-(oy+p[1]),-eps])
                    cylinder(d=button_access_d,h=total_h);
        if(vents)
            for(x=[45:4:61])
                translate([ox+x,outer_w-(oy+22),-eps])
                    rounded_prism(vent_width,12,lid_thickness+2*eps,vent_width/2);
    }
}

module lid_assembled(lift=0) {
    translate([0,outer_w,total_h+lift]) rotate([180,0,0]) lid_print();
}

// Approximate component envelopes for visual context only; never export PCB.
module board_mockup() {
    color("firebrick") translate([ox,oy,pcb_z]) difference() {
        rounded_prism(pcb_length,pcb_width,pcb_thickness,1.25);
        for(p=holes) translate([p[0],p[1],-eps])
            cylinder(d=pcb_hole_d,h=pcb_thickness+2*eps);
    }
    color("silver") translate([ox+40.3,oy+7.04,pcb_top]) cube([25.2,18,3.3]);
    color("#252a30") translate([ox+65.5,oy+7.04,pcb_top]) cube([6,18,0.8]);
    color("silver") translate([ox+19.36,oy+9,pcb_top]) cube([13,14,3.5]);
    color("ivory") translate([ox+19.36,oy+0.15,pcb_top]) cube([13,8.85,3.5]);
    color("silver") translate([ox-1,oy+usb_y-4.5,pcb_top]) cube([8.5,9,3.3]);
    for(p=[[reset_x,reset_y],[flash_x,flash_y]])
        color("#252a30") translate([ox+p[0]-1.5,oy+p[1]-2.2,pcb_top])
            cube([3,4.4,2]);
}

module fitcheck() {
    // Low tray checks the new guides and nut pockets without the full-height walls.
    intersection() {
        base();
        cube([outer_l,outer_w,guide_top]);
    }
}

module mount_test(lid=false) {
    // A 14 mm corner of the REAL parts: inexpensive nut/head/clamping trial.
    // The real PCB's corner can project out of the two cut-away sides.
    // Lid is upside down for printing: crop the opposite Y end so the test
    // pieces' holes align when the lid is turned over around its own 14 mm edge.
    crop_y = lid ? outer_w-14 : 0;
    translate([14-outer_l,-crop_y,0]) intersection() {
        if(lid) lid_print(); else base();
        translate([outer_l-14,crop_y,-eps]) cube([14,14,total_h+2*eps]);
    }
}

if(part=="base") base();
else if(part=="lid") lid_print();
else if(part=="fitcheck") fitcheck();
else if(part=="test_base") mount_test();
else if(part=="test_lid") mount_test(lid=true);
else if(part=="layout") {
    color("#426777") base();
    translate([0,outer_w+8,0]) color("#e2d6bd") lid_print();
} else {
    color("#426777") base();
    if(show_board && $preview) board_mockup();
    color("#e2d6bd") lid_assembled(part=="exploded" ? 18 : 0);
}
