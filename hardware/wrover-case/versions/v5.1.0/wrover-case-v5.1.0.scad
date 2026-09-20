// Makerfabs ESP32 UWB DW3000 / WROVER-E enclosure, V5.1. All units mm.
// Screwless: four tapered locators, four independent PCB pressure springs,
// internal locating skirt and two wall-backed catches. Print exterior-face down.
// The source PCB outline and mounting pattern passed the original fit check.
// V5 physically fits well; V5.1 increases hook reach only. Reuse the V5 base.

/* [View] */
part = "layout"; // [layout,base,lid,assembled,exploded,fitcheck,test_base,test_lid]
show_board = true;

/* [PCB] */
pcb_length = 72;
pcb_width = 32.6;
pcb_thickness = 1.6;
pcb_hole_d = 3;
hole_inset = 2;
under_board = 4;
above_board = 8;

/* [Shell] */
wall = 3;
floor_thickness = 2;
lid_thickness = 2;
edge_gap = 4; // Extra room keeps the new skirt clear of the proven PCB leaves.
corner_radius = 3;
// Per-side skirt clearance; the frame constrains movement in BOTH axes.
alignment_clearance = 0.25;
lip_depth = 4;
lip_wall = 1.2;

/* [Board location and anti-rattle springs] */
post_d = 5.8;
// Clearance pins, not an interference fit. Four 2.5 mm pins passed original tray fit.
pin_root_d = 2.5;
pin_tip_d = 1.5;
pin_height = 3;
// Free contact pads extend below nominal PCB top by this amount.
// With 0.3 mm latch lift, nominal retained spring deflection is 0.6 mm.
board_preload = 0.9;
board_spring_length = 20;
board_spring_width = 6.6;
board_spring_thickness = 1.2;
slot_width = 0.8;
pin_socket_d = 4;
pin_socket_depth = 3.6;

/* [Internal snap catches] */
// Wide tongues are part of the internal skirt, with positive retaining shoulders.
// Only these two tongues flex; the surrounding skirt locates the lid.
clip_width = 12;
clip_thickness = 1;
clip_side_gap = 1;
clip_engagement = 1.0; // V5.1: each hook projects 0.4 mm further into its base window.
clip_lift_clearance = 0.3; // Preserve the physically tested PCB spring travel.
clip_shoulder = 12.2; // Z above lid outside face, in print orientation
clip_tip = 14;
clip_window_clearance = 0.8;

/* [USB and buttons] */
usb_y = 16;
usb_width = 16;
usb_bottom_offset = -2;
button_access = true;
reset_x = 2.4166;
reset_y = 8.1851;
flash_x = 2.5;
flash_y = 24.532;
button_access_d = 5;
vents = true;

/* [Hidden] */
$fn = 64;
eps = 0.02;
ox = wall + edge_gap;
oy = wall + edge_gap;
outer_l = pcb_length + 2*ox;
outer_w = pcb_width + 2*oy;
pcb_z = floor_thickness + under_board;
pcb_top = pcb_z + pcb_thickness;
body_h = pcb_top + above_board;
total_h = body_h + lid_thickness;
foot_h = total_h - pcb_top + board_preload;
lip_outer = wall + alignment_clearance;
clip_x = (outer_l-clip_width)/2;
clip_pocket_top = total_h - clip_shoulder + clip_lift_clearance;
clip_pocket_bottom = total_h - clip_tip - 0.3;
holes = [[hole_inset,hole_inset], [pcb_length-hole_inset,hole_inset],
         [pcb_length-hole_inset,pcb_width-hole_inset],
         [hole_inset,pcb_width-hole_inset]];

assert(lip_outer+lip_wall < oy+hole_inset-board_spring_width/2-slot_width-0.2,
       "Skirt must not touch or bridge a PCB spring slot.");
assert(lip_outer+lip_wall < ox+hole_inset-4-0.2);
assert(clip_tip < total_h-floor_thickness-0.5);
assert(clip_shoulder > lid_thickness+lip_depth+4);
assert(clip_thickness >= 1 && clip_width >= 10);
assert(clip_side_gap >= 0.8);
assert(pin_root_d < pcb_hole_d && pin_tip_d < pin_root_d);
assert(pin_socket_d > pin_root_d+0.6 && pin_socket_d < post_d-1.2);
assert(pin_height > pcb_thickness &&
       pin_socket_depth > pin_height-pcb_thickness+board_preload+0.5);
assert(board_preload > clip_lift_clearance+0.3);
assert(board_preload <= 1.2 && board_spring_length >= 16);
assert(board_spring_thickness >= 1 && board_spring_thickness < lid_thickness);
assert(board_spring_width > post_d && slot_width >= 0.6);
assert(clip_engagement >= 0.5 && clip_engagement <= 1.0);
assert(clip_pocket_top < body_h-1 && clip_pocket_bottom > floor_thickness);
assert(alignment_clearance < clip_engagement);
assert(foot_h-pin_socket_depth > lid_thickness);

echo(str("V5.1 internal-lip body: ",outer_l," x ",outer_w," x ",total_h," mm"));
echo(str("Four PCB springs: free preload ",board_preload,
         "; nominal preload at latch stop ",board_preload-clip_lift_clearance," mm"));
echo("Both parts are in print orientation; V5.1 lid fits the unchanged V5 base.");

module rounded_prism(l,w,h,r) {
    linear_extrude(height=h)
        hull() for(x=[r,l-r],y=[r,w-r]) translate([x,y]) circle(r=r);
}
module at_holes(z=0) {
    for(p=holes) translate([ox+p[0],oy+p[1],z]) children();
}
// Local polygon X,Y becomes global Y,Z; extrusion is along global X.
module extrude_x(h) {
    multmatrix([[0,0,1,0],[1,0,0,0],[0,1,0,0],[0,0,0,1]])
        linear_extrude(height=h) children();
}
module both_sides() {
    children();
    translate([0,outer_w,0]) mirror([0,1,0]) children();
}
module usb_cut() {
    translate([-eps,oy+usb_y-usb_width/2,pcb_top+usb_bottom_offset])
        cube([ox+eps,usb_width,total_h]);
}
module base() {
    difference() {
        union() {
            difference() {
                rounded_prism(outer_l,outer_w,body_h,corner_radius);
                translate([wall,wall,floor_thickness])
                    rounded_prism(outer_l-2*wall,outer_w-2*wall,
                                  body_h,0.6);
            }
            at_holes(floor_thickness-eps)
                cylinder(d=post_d,h=under_board+eps);
            at_holes(pcb_z-eps)
                cylinder(d1=pin_root_d,d2=pin_tip_d,h=pin_height+eps);
        }
        usb_cut();
        // Through-windows give positive roofs to catch under and release access.
        both_sides()
            translate([clip_x-clip_window_clearance,-eps,clip_pocket_bottom])
                cube([clip_width+2*clip_window_clearance,wall+2*eps,
                      clip_pocket_top-clip_pocket_bottom]);
    }
}

// Coordinates for each PCB spring: free pad at local X=0;
// its fixed root is +spring_length. Mirrored at the WiFi end.
module at_springs() {
    for(p=holes)
        translate([ox+p[0],oy+p[1],0])
            scale([p[0]<pcb_length/2 ? 1 : -1,1,1]) children();
}
module spring_relief() {
    // Three-sided slot makes an independent leaf, joined only at its root.
    for(y=[-board_spring_width/2-slot_width,board_spring_width/2])
        translate([-4,y,-eps])
            cube([board_spring_length+4,slot_width,lid_thickness+2*eps]);
    translate([-4,-board_spring_width/2-slot_width,-eps])
        cube([slot_width,board_spring_width+2*slot_width,lid_thickness+2*eps]);
    // Thin the leaf from its inside face. It prints flat, fully on the bed.
    translate([-4,-board_spring_width/2,board_spring_thickness])
        cube([board_spring_length+4,board_spring_width,
              lid_thickness-board_spring_thickness+eps]);
}
module pressure_foot() {
    difference() {
        translate([0,0,board_spring_thickness-eps])
            cylinder(d=post_d,h=foot_h-board_spring_thickness+eps);
        // A blind socket lets the locator protrude through the PCB freely.
        // The annular face presses only the bare mounting-hole surround.
        translate([0,0,foot_h-pin_socket_depth])
            cylinder(d=pin_socket_d,h=pin_socket_depth+eps);
    }
}
module internal_lip() {
    difference() {
        translate([lip_outer,lip_outer,lid_thickness-eps]) difference() {
            rounded_prism(outer_l-2*lip_outer,outer_w-2*lip_outer,
                          lip_depth+eps,1.3);
            translate([lip_wall,lip_wall,-eps])
                rounded_prism(outer_l-2*(lip_outer+lip_wall),
                              outer_w-2*(lip_outer+lip_wall),
                              lip_depth+3*eps,0.3);
        }
        // Free each snap tongue from the skirt along both sides.
        both_sides()
            translate([clip_x-clip_side_gap,wall-eps,lid_thickness-2*eps])
                cube([clip_width+2*clip_side_gap,lip_wall+alignment_clearance+2*eps,
                      lip_depth+3*eps]);
        // Preserve the entire existing USB cable aperture, not just the socket.
        translate([-eps,outer_w-(oy+usb_y+usb_width/2),lid_thickness-2*eps])
            cube([ox+eps,usb_width,lip_depth+3*eps]);
    }
}
module latch_tongue() {
    translate([clip_x,lip_outer,lid_thickness-eps])
        cube([clip_width,clip_thickness,clip_tip-lid_thickness+eps]);
    // Horizontal retention shoulder faces the pocket roof after assembly.
    // The entry ramp pushes the tongue INWARDS, away from the rigid base wall.
    translate([clip_x,0,0]) extrude_x(clip_width)
        polygon([[lip_outer+eps,clip_shoulder],
                 [wall-clip_engagement,clip_shoulder],
                 [lip_outer,clip_tip],[lip_outer+eps,clip_tip]]);
}

// STLs and previews show the unloaded spring shape. In service the leaves
// deflect towards the outside of the lid when the pads touch the PCB.
module lid_print() {
    difference() {
        union() {
            difference() {
                rounded_prism(outer_l,outer_w,lid_thickness,corner_radius);
                at_springs() spring_relief();
            }
            at_springs() pressure_foot();
            internal_lip();
            both_sides() latch_tongue();
        }
        if(button_access)
            for(p=[[reset_x,reset_y],[flash_x,flash_y]])
                translate([ox+p[0],outer_w-(oy+p[1]),-eps])
                    cylinder(d=button_access_d,h=lid_thickness+2*eps);
        if(vents)
            for(x=[33:4:49])
                translate([ox+x,outer_w/2-6,-eps])
                    rounded_prism(1.4,12,lid_thickness+2*eps,0.7);
    }
}
module lid_assembled(lift=0) {
    translate([0,outer_w,total_h+lift]) rotate([180,0,0]) lid_print();
}
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
    intersection() { base(); cube([outer_l,outer_w,pcb_z+pin_height]); }
}
module latch_test(lid=false) {
    // Central strip contains the actual paired tongues, windows and skirt rails.
    // Its open cut ends mean it does not test the full frame's X location.
    start = clip_x-5;
    end = clip_x+clip_width+5;
    translate([-start,0,0]) intersection() {
        if(lid) lid_print(); else base();
        translate([start,-eps,-eps]) cube([end-start,outer_w+2*eps,total_h+2*eps]);
    }
}

if(part=="base") base();
else if(part=="lid") lid_print();
else if(part=="fitcheck") fitcheck();
else if(part=="test_base") latch_test();
else if(part=="test_lid") latch_test(lid=true);
else if(part=="layout") {
    color("#426777") base();
    translate([0,outer_w+12,0]) color("#e2d6bd") lid_print();
} else if(part=="assembled" || part=="exploded") {
    color("#426777") base();
    if(show_board && $preview) board_mockup();
    color("#e2d6bd") lid_assembled(part=="exploded" ? 22 : 0);
} else if(part!="none") assert(false,"Unknown part");
