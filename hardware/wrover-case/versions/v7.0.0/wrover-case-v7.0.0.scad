// Makerfabs ESP32 UWB DW3000 / WROVER-E case v7.0.0, millimetres.
// EXPERIMENTAL: matched base and lid, pure PLA, physical validation pending.
// Overlapping shells with four low-profile beads, inspired by the maker
// references in ../../research/other-maker-examples.md. Original geometry.
// Board pegs and four PCB leaves retain their physically successful dimensions.
// Print both parts exterior-face down. Do not mix with v5/v6 parts.

/* [View] */
part = "layout"; // [layout,base,lid,assembled,exploded]
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
// PCB origin is independent of the thinner shell: preserve the proven hole grid.
wall = 3; // Legacy coordinate offset only; see shell_wall for physical wall.
edge_gap = 4;
floor_thickness = 2;
lid_thickness = 2;
corner_radius = 3;
shell_wall = 1.6;
lip_wall = 1.2;
alignment_clearance = 0.25;
lip_depth = 4;

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

/* [Distributed shell catches] */
bead_width = 10;
bead_projection = 0.65; // Net engagement into lid skirt = 0.40, not 3 mm.
bead_bottom = 12.9;
bead_lower_rise = 0.32; // Short retaining bevel; inspect these layers in slicer.
bead_land = 0.38;
bead_entry_rise = 0.9;
window_side_clearance = 0.4;
clip_lift_clearance = 0.3;
release_clearance = 0.2;
version_marks = true;

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
base_inset = lip_wall + alignment_clearance;
base_inner = base_inset + shell_wall;
bead_top = bead_bottom+bead_lower_rise+bead_land+bead_entry_rise;
bead_contact_z = bead_bottom + bead_lower_rise*alignment_clearance/bead_projection;
window_bottom = bead_contact_z-clip_lift_clearance;
window_top = bead_top+0.2;
bead_centres = [23, outer_l-23];
release_travel = bead_projection-alignment_clearance+release_clearance;
holes = [[hole_inset,hole_inset], [pcb_length-hole_inset,hole_inset],
         [pcb_length-hole_inset,pcb_width-hole_inset],
         [hole_inset,pcb_width-hole_inset]];

assert(base_inner < oy+hole_inset-board_spring_width/2-slot_width-0.2);
assert(lip_wall < ox+hole_inset-4-slot_width);
assert(pin_root_d < pcb_hole_d && pin_tip_d < pin_root_d);
assert(pin_socket_depth > pin_height-pcb_thickness+board_preload+0.5);
assert(board_preload > clip_lift_clearance+0.3);
assert(bead_projection > alignment_clearance && bead_projection < lip_wall);
assert(window_bottom > body_h-lip_depth+0.8);
assert(window_top < body_h-0.8);
assert(base_inner+release_travel < oy-post_d/2);
echo(str("v7.0.0 matched pair: ",outer_l," x ",outer_w," x ",total_h," mm"));
echo(str("Net bead engagement: ",bead_projection-alignment_clearance,
         "; clearance-envelope release: ",release_travel," mm"));
echo(str("Nominal retained PCB spring deflection: ",board_preload-clip_lift_clearance));

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
module bead() {
    // The lower bevel gives the first overhanging layers some support.
    // A gentler entry ramp above the bead assists closure.
    extrude_x(bead_width) polygon([
        [base_inset+eps,bead_bottom], [base_inset,bead_bottom],
        [base_inset-bead_projection,bead_bottom+bead_lower_rise],
        [base_inset-bead_projection,bead_bottom+bead_lower_rise+bead_land],
        [base_inset,bead_top], [base_inset+eps,bead_top]]);
}
module base_beads(inward=0) {
    both_sides() for(x=bead_centres)
        translate([x-bead_width/2,inward,0]) bead();
}
module base_without_beads() {
    difference() {
        union() {
            rounded_prism(outer_l,outer_w,floor_thickness,corner_radius);
            translate([base_inset,base_inset,floor_thickness-eps]) difference() {
                rounded_prism(outer_l-2*base_inset,outer_w-2*base_inset,
                              body_h-floor_thickness+eps,corner_radius-base_inset);
                translate([shell_wall,shell_wall,-eps])
                    rounded_prism(outer_l-2*base_inner,outer_w-2*base_inner,
                                  body_h,0.6);
            }
            at_holes(floor_thickness-eps) cylinder(d=post_d,h=under_board+eps);
            at_holes(pcb_z-eps) cylinder(d1=pin_root_d,d2=pin_tip_d,h=pin_height+eps);
        }
        usb_cut();
        if(version_marks) translate([outer_l/2,outer_w/2,-eps])
            mirror([1,0,0]) linear_extrude(0.4+eps)
                text("W7.0.0 BASE",size=2.5,halign="center",valign="center");
    }
}
module base() { union() { base_without_beads(); base_beads(); } }

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
module lid_skirt() {
    difference() {
        translate([0,0,lid_thickness-eps]) difference() {
            rounded_prism(outer_l,outer_w,lip_depth+eps,corner_radius);
            translate([lip_wall,lip_wall,-eps])
                rounded_prism(outer_l-2*lip_wall,outer_w-2*lip_wall,
                              lip_depth+3*eps,corner_radius-lip_wall);
        }
        both_sides() for(x=bead_centres)
            translate([x-bead_width/2-window_side_clearance,-eps,total_h-window_top])
                cube([bead_width+2*window_side_clearance,lip_wall+2*eps,
                      window_top-window_bottom]);
        // Preserve the full USB plug opening through the overlapping skirt.
        translate([-eps,outer_w-(oy+usb_y+usb_width/2),lid_thickness-2*eps])
            cube([ox+eps,usb_width,lip_depth+3*eps]);
    }
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
            lid_skirt();
        }
        if(version_marks) translate([outer_l/2,8,-eps])
            mirror([1,0,0]) linear_extrude(0.4+eps)
                text("W7.0.0 LID",size=2.5,halign="center",valign="center");
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
if(part=="base") base();
else if(part=="lid") lid_print();
else if(part=="layout") {
    color("#426777") base();
    translate([0,outer_w+12,0]) color("#e2d6bd") lid_print();
} else if(part=="assembled" || part=="exploded") {
    color("#426777") base();
    if(show_board && $preview) board_mockup();
    color("#e2d6bd") lid_assembled(part=="exploded" ? 22 : 0);
} else if(part!="none") assert(false,"Unknown part");
