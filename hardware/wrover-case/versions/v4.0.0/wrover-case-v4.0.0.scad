// Makerfabs ESP32 UWB DW3000 / WROVER-E enclosure, V4. All units mm.
// Screwless: four tapered locators, four independent PCB pressure springs,
// two long in-plane snap arms. Both parts print exterior-face down.
// The source PCB outline and mounting pattern passed the original fit check.
// Spring force / PLA fatigue / printed fit require a new physical check.

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
edge_gap = 2;
corner_radius = 3;
alignment_clearance = 0.4;
alignment_depth = 2.5;

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

/* [Two finger-release latches] */
// Arms bend sideways in the plane of the printed layers, not across layers.
clip_length = 26;
clip_arm_width = 1.4;
clip_gap = 0.8;
clip_engagement = 0.6;
clip_lift_clearance = 0.3;
clip_pocket_depth = 1.2;
clip_hook_width = 5;

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
clip_root_x = outer_l/2 - clip_length/2;
clip_end_x = clip_root_x + clip_length;
clip_shoulder = 4.2; // height above outside lid face, in print orientation
clip_tip = 5.9;
clip_pocket_top = total_h - clip_shoulder + clip_lift_clearance;
clip_pocket_bottom = total_h - clip_tip - 0.5;
holes = [[hole_inset,hole_inset], [pcb_length-hole_inset,hole_inset],
         [pcb_length-hole_inset,pcb_width-hole_inset],
         [hole_inset,pcb_width-hole_inset]];

assert(wall-clip_pocket_depth >= 1.6);
assert(pin_root_d < pcb_hole_d && pin_tip_d < pin_root_d);
assert(pin_socket_d > pin_root_d+0.6 && pin_socket_d < post_d-1.2);
assert(pin_height > pcb_thickness &&
       pin_socket_depth > pin_height-pcb_thickness+board_preload+0.5);
assert(board_preload > clip_lift_clearance+0.3);
assert(board_preload <= 1.2 && board_spring_length >= 16);
assert(board_spring_thickness >= 1 && board_spring_thickness < lid_thickness);
assert(board_spring_width > post_d && slot_width >= 0.6);
assert(clip_engagement > 0 && clip_engagement < clip_pocket_depth-0.3);
assert(clip_pocket_top < body_h-1 && clip_pocket_bottom > floor_thickness);
assert(alignment_clearance < clip_engagement);
assert(foot_h-pin_socket_depth > lid_thickness);

echo(str("V4 screwless body: ",outer_l," x ",outer_w," x ",total_h," mm"));
echo(str("Four PCB springs: free preload ",board_preload,
         "; nominal preload at latch stop ",board_preload-clip_lift_clearance," mm"));
echo("Both parts are in print orientation; use matching V4 base and lid.");

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
        // Generous open-sided catches; no tiny holes or captured hardware.
        both_sides()
            translate([clip_end_x-clip_hook_width-1,-eps,clip_pocket_bottom])
                cube([clip_hook_width+2,clip_pocket_depth+eps,
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
module alignment_tabs() {
    // Short, loose tabs outside PCB edges; no continuous rim to stiffen springs.
    both_sides()
        translate([outer_l/2-6,wall+alignment_clearance,lid_thickness-eps])
            cube([12,1,alignment_depth+eps]);
    // One at the WiFi end. The USB end stays clear for the cable moulding.
    translate([outer_l-wall-alignment_clearance-1,outer_w/2-6,lid_thickness-eps])
        cube([1,12,alignment_depth+eps]);
}
module latch_arm() {
    // 3 mm root bridge: beam starts where it leaves the lid edge.
    translate([clip_root_x-3,-clip_gap-clip_arm_width,0])
        cube([3+eps,clip_gap+clip_arm_width+0.8,lid_thickness]);
    translate([clip_root_x-eps,-clip_gap-clip_arm_width,0])
        cube([clip_length+eps,clip_arm_width,lid_thickness]);
    // Hook stem is on the free end. Pull its broad ear away from the case.
    translate([clip_end_x-clip_hook_width,-clip_gap-clip_arm_width,0])
        cube([clip_hook_width,clip_arm_width,clip_tip]);
    translate([clip_end_x-clip_hook_width,-clip_gap-clip_arm_width-2.6,0])
        rounded_prism(clip_hook_width,2.6+eps,lid_thickness,0.6);
    // Flat retaining shoulder, sloping entry ramp. A 1.4 mm print overhang.
    translate([clip_end_x-clip_hook_width,0,0]) extrude_x(clip_hook_width)
        polygon([[-clip_gap-eps,clip_shoulder],
                 [clip_engagement,clip_shoulder],
                 [-clip_gap,clip_tip],[-clip_gap-eps,clip_tip]]);
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
            alignment_tabs();
            both_sides() latch_arm();
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
    // A full-width central strip preserves both real latches and their roots.
    // Only tests the closure; the full case is required for board retention.
    start = clip_root_x-4;
    end = clip_end_x+2;
    translate([-start,clip_gap+clip_arm_width+2.6,0]) intersection() {
        if(lid) lid_print(); else base();
        translate([start,-10,-eps]) cube([end-start,outer_w+20,total_h+2*eps]);
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
