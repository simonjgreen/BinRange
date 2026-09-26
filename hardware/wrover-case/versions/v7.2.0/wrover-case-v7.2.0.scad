// Makerfabs ESP32 UWB DW3000 / WROVER-E case v7.2.0, millimetres.
// EXPERIMENTAL: DIN rail mount added to the accepted v7.1.0 case.
// Overlapping shells with four low-profile beads, inspired by the maker
// references in ../../research/other-maker-examples.md. Original geometry.
// v7.2: 3 mm dovetail plinth under the unchanged v7.1.0 base floor, plus a
// separate snap-on TS35 (EN 60715 35 x 7.5) clip. The lid is unchanged from v7.1.0.
// Print base and lid exterior-face down; print the clip on its side (profile down).

/* [View] */
part = "layout"; // [layout,base,lid,din_clip,assembled,exploded]
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
// Nominal line contact at the lower PCB-hole rim; no designed interference.
// Root diameter is measured AT pcb_z, not at the buried CSG overlap.
pin_root_d = pcb_hole_d;
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

/* [DIN rail mount] */
// The rail runs along the case width (Y). Fixed hook at the far end, sprung
// latch and pull tab at the USB end, so the USB end faces down on a level rail.
din_mount = true;
plinth_h = 3; // Added below the unchanged floor; carries the dovetail grooves.
rail_width = 35;
rail_lip = 1;
rail_depth = 7.5;
rail_crown_w = 27;
din_width = 26; // Clip length along the rail.
din_standoff = 11; // Plinth to rail front face; sets the spring beam length.
din_edge_clearance = 0.3;
din_fixed_clearance = 0.4; // Extra room to rock the fixed hook on first.
din_latch_clearance = 0.25;
din_hook_reach = 1.5;
din_hook_wall = 2.5;
din_block_w = 4.2;
din_top_bar = 2.4;
din_spring_beams = 5; // Odd count: anchored at the top bar, ends at the block.
din_spring_t = 1;
din_spring_gap = 1.2;
din_tab_t = 1.6;
din_pull_gap = 2.5; // Screwdriver/fingernail slot beyond the case end.
din_pull_t = 1.5;
dovetail_x = [-11, 14.5]; // Relative to the rail centre line.
dovetail_neck = 8;
dovetail_depth = 2.4;
dovetail_angle = 30;
dovetail_clearance = 0.2;
groove_end_clearance = 0.3;
// Sprung detent finger: its ridge drops into a plinth pocket at full insertion.
detent_finger = [-5, 8];
detent_ridge = [5.8, 7.8];
detent_rise = 0.8;
detent_pocket_depth = 1;
detent_finger_t = 1.2;
detent_slot = 0.8;
detent_entry_run = 3;

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
// DIN clip profile: x across the rail from its centre line (+x towards the far
// case end), v from the plinth underside towards the mounting panel.
rail_x = outer_l/2;
din_y0 = (outer_w-din_width)/2;
din_y1 = din_y0+din_width;
groove_end = din_y1+groove_end_clearance;
rail_edge = rail_width/2;
din_throat = rail_edge+din_edge_clearance;
din_body_end = -(din_throat-2*din_edge_clearance); // Rigid body stops short of latch.
fixed_lip_top = din_standoff+rail_lip+din_fixed_clearance;
latch_lip_top = din_standoff+rail_lip+din_latch_clearance;
lip_tip = rail_edge-din_hook_reach;
din_block_outer = -din_throat-din_block_w;
din_turn_v = din_top_bar+0.8;
din_spring_bottom = latch_lip_top+din_hook_reach+0.55;
din_tab_top = din_spring_bottom+0.5;
din_tab_bottom = din_tab_top+din_tab_t;
function beam_face(i) = din_block_outer-din_spring_gap*i-din_spring_t*(i-1);
din_spring_outer = beam_face(din_spring_beams)-din_spring_t;
din_pull_inner = -rail_x-din_pull_gap;
dovetail_top = dovetail_neck+2*dovetail_depth*tan(dovetail_angle);
din_release_travel = din_hook_reach+din_edge_clearance-0.1;

assert(base_inner < oy+hole_inset-board_spring_width/2-slot_width-0.2);
assert(lip_wall < ox+hole_inset-4-slot_width);
assert(pin_root_d <= pcb_hole_d && pin_tip_d < pin_root_d);
assert(pin_socket_depth > pin_height-pcb_thickness+board_preload+0.5);
assert(board_preload > clip_lift_clearance+0.3);
assert(bead_projection > alignment_clearance && bead_projection < lip_wall);
assert(window_bottom > body_h-lip_depth+0.8);
assert(window_top < body_h-0.8);
assert(base_inner+release_travel < oy-post_d/2);
assert(din_spring_beams%2==1);
assert(din_spring_outer > din_pull_inner+2);
assert(din_tab_bottom < din_standoff+rail_depth-1);
assert(dovetail_depth+dovetail_clearance < plinth_h-0.3);
assert(detent_pocket_depth > detent_rise && detent_pocket_depth < plinth_h-1);
assert(detent_ridge[1] < detent_finger[1] &&
       detent_finger[1]+detent_slot < dovetail_x[1]-dovetail_top/2);
assert(detent_finger[0] > dovetail_x[0]+dovetail_top/2);
assert(din_y0-detent_pocket_depth-0.2 > detent_entry_run+1);
echo(str("v7.2.0 closed case: ",outer_l," x ",outer_w," x ",total_h+(din_mount ? plinth_h : 0),
         " mm; clip ",din_pull_inner-din_pull_t+rail_x," to ",din_throat+din_hook_wall+rail_x,
         " mm along the case, ",din_width," mm along the rail"));
echo(str("DIN latch travel to clear the rail edge: ",din_release_travel," mm"));
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
            // Extend the same taper below the seating plane for a robust union.
            // This gives exactly pin_root_d at pcb_z, despite the buried overlap.
            at_holes(pcb_z-eps)
                cylinder(d1=pin_root_d+(pin_root_d-pin_tip_d)*eps/pin_height,
                         d2=pin_tip_d,h=pin_height+eps);
        }
        usb_cut();
        // With the plinth this recess would become a sealed void; the plinth carries the mark.
        if(version_marks && !din_mount) translate([outer_l/2,outer_w/2,-eps])
            mirror([1,0,0]) linear_extrude(0.4+eps)
                text("W7.1.0 BASE",size=2.5,halign="center",valign="center");
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
        // Unchanged v7.1.0 lid, including its mark, so the export is byte-identical.
        if(version_marks) translate([outer_l/2,8,-eps])
            mirror([1,0,0]) linear_extrude(0.4+eps)
                text("W7.1.0 LID",size=2.5,halign="center",valign="center");
        if(button_access)
            for(p=[[reset_x,reset_y],[flash_x,flash_y]])
                translate([ox+p[0],outer_w-(oy+p[1]),-eps])
                    cylinder(d=button_access_d,h=lid_thickness+2*eps);
        if(vents)
            // Five unchanged 1.4 x 12 mm slots, symmetric about both lid axes.
            for(dx=[-8:4:8])
                translate([outer_l/2+dx-0.7,outer_w/2-6,-eps])
                    rounded_prism(1.4,12,lid_thickness+2*eps,0.7);
    }
}
module lid_assembled(lift=0) {
    translate([0,outer_w,total_h+lift]) rotate([180,0,0]) lid_print();
}

// ---- DIN rail mount ----
// Maps clip profile (x,v) extruded along e to case coordinates.
module din_place(e0=din_y0) {
    multmatrix([[1,0,0,rail_x],[0,0,1,e0],[0,-1,0,-plinth_h],[0,0,0,1]]) children();
}
module dovetail_2d(c=0,below=eps) {
    offset(delta=c) polygon([
        [-dovetail_neck/2,below], [dovetail_neck/2,below],
        [dovetail_top/2,-dovetail_depth], [-dovetail_top/2,-dovetail_depth]]);
}
// Rigid body: rail seat, fixed hook, dovetail tongues and the spring anchor bar.
module din_body_2d(ridge=true) {
    difference() {
        union() {
            translate([din_body_end,0]) square([din_throat-din_body_end,din_standoff]);
            translate([din_spring_outer,0]) square([din_body_end-din_spring_outer,din_top_bar]);
            translate([din_throat,0]) square([din_hook_wall,fixed_lip_top+din_hook_reach]);
            polygon([[din_throat,fixed_lip_top], [lip_tip,fixed_lip_top],
                     [lip_tip,fixed_lip_top+0.5], [din_throat-0.6,fixed_lip_top+din_hook_reach],
                     [din_throat,fixed_lip_top+din_hook_reach]]);
            for(x=dovetail_x) translate([x,0]) dovetail_2d();
        }
        // Cantilever finger, joined only at detent_finger[0].
        translate([detent_finger[0],detent_finger_t])
            square([detent_finger[1]+detent_slot-detent_finger[0],detent_slot]);
        translate([detent_finger[1],-eps])
            square([detent_slot,detent_finger_t+detent_slot+eps]);
    }
    if(ridge) translate([detent_ridge[0],-detent_rise])
        square([detent_ridge[1]-detent_ridge[0],detent_rise+eps]);
}
// Five-beam serpentine: lets the latch block slide outward along x.
module din_spring_2d() {
    n = din_spring_beams; t = din_spring_t;
    for(i=[1:n]) translate([beam_face(i)-t,i==n ? din_top_bar-eps : din_turn_v])
        square([t,din_spring_bottom-(i==n ? din_top_bar-eps : din_turn_v)]);
    translate([beam_face(1)-eps,din_spring_bottom-t])
        square([din_block_outer-beam_face(1)+2*eps,t]);
    for(i=[1:n-1]) translate([beam_face(i+1)-eps,i%2==1 ? din_turn_v : din_spring_bottom-t])
        square([beam_face(i)-beam_face(i+1)+2*eps,t]);
}
// Moving latch: hook with snap-on ramp, tab under the spring and pull lip.
module din_latch_2d() {
    polygon([[din_block_outer,din_turn_v], [-din_throat,din_turn_v],
             [-din_throat,latch_lip_top], [-lip_tip,latch_lip_top],
             [-lip_tip,latch_lip_top+0.3], [-din_throat,din_spring_bottom],
             [-din_throat,din_tab_bottom], [din_block_outer,din_tab_bottom]]);
    translate([din_pull_inner,din_tab_top])
        square([din_block_outer-din_pull_inner+eps,din_tab_t]);
    translate([din_pull_inner-din_pull_t,0]) square([din_pull_t,din_tab_bottom]);
}
module din_clip_2d(ridge=true) {
    din_body_2d(ridge); din_spring_2d(); din_latch_2d();
}
module din_clip() { linear_extrude(din_width) din_clip_2d(); }
module din_clip_assembled(dy=0) { din_place(din_y0+dy) din_clip(); }
module rail_2d() {
    for(s=[-1,1]) scale([s,1]) {
        translate([rail_crown_w/2-rail_lip,din_standoff])
            square([rail_edge-rail_crown_w/2+rail_lip,rail_lip]);
        translate([rail_crown_w/2-rail_lip,din_standoff])
            square([rail_lip,rail_depth]);
    }
    translate([-rail_crown_w/2,din_standoff+rail_depth-rail_lip])
        square([rail_crown_w,rail_lip]);
}
module rail_mockup(dv=0) {
    din_place(-30) translate([0,dv]) linear_extrude(outer_w+60) rail_2d();
}
module din_grooves() {
    for(x=dovetail_x) din_place(-1) translate([x,0])
        linear_extrude(groove_end+1) dovetail_2d(dovetail_clearance,1);
}
module detent_pocket() {
    pz = -plinth_h; d = detent_pocket_depth;
    translate([rail_x+detent_ridge[0]-0.3,0,0])
        extrude_x(detent_ridge[1]-detent_ridge[0]+0.6) {
            // 45 degree retaining edge: removable by a firm pull, not a hard lock.
            polygon([[din_y0-0.2-d,pz-eps], [din_y0-0.2,pz+d],
                     [groove_end+0.2,pz+d], [groove_end+0.2,pz-eps]]);
            polygon([[-1,pz-eps], [-1,pz+d], [0,pz+d], [detent_entry_run,pz-eps]]);
        }
}
// Base in assembled coordinates: the unchanged v7.1.0 base above z=0.
module base_din() {
    difference() {
        union() {
            base();
            translate([0,0,-plinth_h])
                rounded_prism(outer_l,outer_w,plinth_h+eps,corner_radius);
        }
        din_grooves();
        detent_pocket();
        if(version_marks) translate([outer_l/2,(groove_end+outer_w)/2,-plinth_h-eps])
            mirror([1,0,0]) linear_extrude(0.4+eps)
                text("W7.2.0 BASE",size=2.5,halign="center",valign="center");
    }
}
module base_assembled() { if(din_mount) base_din(); else base(); }
module base_print() {
    if(din_mount) translate([0,0,plinth_h]) base_din(); else base();
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
if(part=="base") base_print();
else if(part=="lid") lid_print();
else if(part=="din_clip") din_clip();
else if(part=="layout") {
    color("#426777") base_print();
    translate([0,outer_w+12,0]) color("#e2d6bd") lid_print();
    if(din_mount) translate([rail_x,2*outer_w+24,0]) color("#8a9a5b") din_clip();
} else if(part=="assembled" || part=="exploded") {
    ex = part=="exploded";
    color("#426777") base_assembled();
    if(show_board && $preview) board_mockup();
    color("#e2d6bd") lid_assembled(ex ? 22 : 0);
    if(din_mount) {
        color("#8a9a5b") din_clip_assembled(ex ? -din_y1-6 : 0);
        if($preview) color("silver") rail_mockup(ex ? 12 : 0);
    }
} else if(part!="none") assert(false,"Unknown part");
