// Makerfabs ESP32 UWB DW3000 / WROVER-E enclosure. All dimensions in mm.
// Published v1.0 Eagle outline: 72 x 32.6; holes 68 x 28.6, diameter 3.
// Current USB-C board visually cross-checked against 2026-09-19 user photo.
// User confirmed fitcheck.stl fits perfectly on 2026-09-19; geometry unchanged.
// Full lid fit, USB-C envelope and button positions still need a physical check.
// See README.md for provenance, printing and screw selection.

/* [View] */
part = "layout"; // [layout,base,lid,assembled,exploded,fitcheck]
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
// Gap between PCB top and lid retaining pillars; prevents clamping the board.
retainer_gap = 0.25;

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
// Four M2.5 x 8 mm countersunk Phillips screws from the blue assortment box.
// Countersunk screw length INCLUDES the head. The existing base is unchanged.
screw_length = 8;
screw_d = 2.5;
screw_clearance_d = 2.8;
pilot_d = 2.1;
// Base posts and the PCB-contact ends of the lid pillars retain this diameter.
post_d = 5;
pilot_floor = 1;
// Typical DIN 965 M2.5 head; the supplied kit's exact head size is unmeasured.
screw_head_d = 4.7;
// Reference maximum overall head height, including its outer rim.
screw_head_h = 1.5;
countersink_angle = 90;
// Straight screwdriver well, followed by the conical screw seat.
head_well_d = 5.2;
head_well_depth = 6.4;
// Wider lid pillars leave 1 mm of plastic around the straight wells.
lid_post_d = 7.2;
// Taper back down to the original 5 mm diameter before reaching the PCB.
lid_post_taper_h = 2;

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
body_h = pcb_top + above_board;
total_h = body_h + lid_thickness;
retainer_h = above_board - retainer_gap;
lid_post_h = lid_thickness + retainer_h;
wide_post_h = lid_post_h - lid_post_taper_h;
seat_cone_h = (head_well_d-screw_clearance_d)/(2*tan(countersink_angle/2));
// Include a possible cylindrical head rim: not every head is a perfect cone.
screw_head_rim_h = max(0,screw_head_h-
                      (screw_head_d-screw_d)/(2*tan(countersink_angle/2)));
screw_head_depth = head_well_depth +
                   (head_well_d-screw_head_d)/(2*tan(countersink_angle/2))-
                   screw_head_rim_h;
screw_tip_z = total_h - screw_head_depth - screw_length;
screw_engagement = pcb_z - screw_tip_z;
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
assert(under_board > 0 && above_board > lip_depth + retainer_gap);
assert(pilot_d < screw_clearance_d && screw_clearance_d < pcb_hole_d);
assert(pilot_d < screw_d && screw_d < screw_clearance_d);
assert(post_d > screw_clearance_d + 1.6);
assert(countersink_angle > 0 && countersink_angle < 180);
assert(screw_head_d > screw_clearance_d && head_well_d > screw_head_d);
assert(head_well_depth >= lid_thickness);
assert(lid_post_d >= head_well_d + 2,
       "Keep at least 1 mm of pillar wall around the head well.");
assert(lid_post_taper_h > 0 && wide_post_h > lid_thickness);
assert(head_well_depth+seat_cone_h < wide_post_h,
       "Countersink must finish before the pillar narrows.");
assert(lid_post_h-head_well_depth-seat_cone_h >= 2,
       "Keep at least 2 mm of pillar beneath the countersunk seat.");
assert(lid_post_d/2 < edge_gap+min(hole_inset_x,hole_inset_y),
       "Wider lid pillars must clear the existing base walls.");
assert(usb_bottom > floor_thickness && usb_bottom < body_h);
assert(usb_y-usb_width/2 > hole_inset_y+post_d/2);
assert(usb_y+usb_width/2 < pcb_width-hole_inset_y-post_d/2);
assert(screw_engagement >= 2,
       "Screw needs at least 2 mm of engagement in base post.");
assert(screw_tip_z >= pilot_floor+0.5,
       "Screw too long: may bottom out or penetrate the floor.");
assert(part == "layout" || part == "base" || part == "lid" ||
       part == "assembled" || part == "exploded" || part == "fitcheck");

echo(str("Case: ",outer_l," x ",outer_w," x ",total_h," mm"));
echo(str("Hole pitch: ",pcb_length-2*hole_inset_x," x ",
         pcb_width-2*hole_inset_y," mm"));
echo(str("M2.5 x ",screw_length," CSK: head ",screw_head_depth,
         " mm below lid; engagement ",screw_engagement,
         " mm; tip ",screw_tip_z," mm above case underside"));

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

module base(drill=true) {
    difference() {
        union() {
            difference() {
                rounded_prism(outer_l,outer_w,body_h,corner_radius);
                translate([wall,wall,floor_thickness])
                    rounded_prism(outer_l-2*wall,outer_w-2*wall,
                                  body_h, max(0.5,corner_radius-wall));
            }
            at_holes(floor_thickness-eps)
                cylinder(d=post_d,h=under_board+eps);
        }
        // Blind pilot bores continue into the base, leaving a sealed floor.
        if(drill) at_holes(pilot_floor) cylinder(d=pilot_d,h=pcb_z-pilot_floor+eps);
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
    // The cylinder and cone meet at exactly the same circular section.
    translate([0,0,head_well_depth])
        cylinder(d1=head_well_d,d2=screw_clearance_d,h=seat_cone_h);
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
    // Low tray with four 2.5 mm pins: verifies outline, hole pitch and underside.
    union() {
        intersection() {
            base(drill=false);
            cube([outer_l,outer_w,pcb_z+0.4]);
        }
        at_holes(pcb_z-0.3) cylinder(d=2.5,h=pcb_thickness+0.3);
    }
}

if(part=="base") base();
else if(part=="lid") lid_print();
else if(part=="fitcheck") fitcheck();
else if(part=="layout") {
    color("#426777") base();
    translate([0,outer_w+8,0]) color("#e2d6bd") lid_print();
} else {
    color("#426777") base();
    if(show_board && $preview) board_mockup();
    color("#e2d6bd") lid_assembled(part=="exploded" ? 18 : 0);
}
