// Geometric checks only, not a PLA force/strain simulation.
include <wrover-case-v7.2.0.scad>
check = "closed";
module skirt_assembled(lift=0) {
    translate([0,outer_w,total_h+lift]) rotate([180,0,0]) lid_skirt();
}
if(check=="closed") intersection() { base(); lid_assembled(); }
if(check=="rigid_insertion") for(lift=[0:0.5:16])
    intersection() { base_without_beads(); lid_assembled(lift); }
if(check=="components") intersection() {
    lid_assembled();
    intersection() {
        board_mockup();
        translate([-10,-10,pcb_top+eps]) cube([outer_l+20,outer_w+20,30]);
    }
}
// Each side separately must have a positive overlap on attempted upward pull.
if(check=="catch_left" || check=="catch_right") intersection() {
    base_beads(); skirt_assembled(clip_lift_clearance+0.2);
    translate([-1,check=="catch_left" ? -1 : outer_w/2,-1])
        cube([outer_l+2,outer_w/2+1,total_h+2]);
}
// Translate the beads inward as a clearance envelope; not an elastic solution.
if(check=="release") for(lift=[0:0.5:16]) intersection() {
    base_beads(release_travel); skirt_assembled(lift);
}
if(check=="release_board") intersection() {
    base_beads(release_travel); board_mockup();
}
// At rounded corners clearance is radial: 0.20 on axes, 0.212 on diagonals.
if(check=="allowed_play") for(p=[[0.2,0],[-0.2,0],[0,0.2],[0,-0.2],
                                    [0.15,0.15],[0.15,-0.15],[-0.15,0.15],[-0.15,-0.15]]) intersection() {
    base(); translate([p[0],p[1],0]) lid_assembled();
}
if(check=="locating_x") for(sign=[-1,1]) intersection() {
    base_without_beads(); translate([sign*(alignment_clearance+0.15),0,0]) skirt_assembled();
}
if(check=="locating_y") for(sign=[-1,1]) intersection() {
    base_without_beads(); translate([0,sign*(alignment_clearance+0.15),0]) skirt_assembled();
}

// Actual solid PCB slab (without approximate components). Seating on the posts
// is intentional surface contact; the cones must not occupy PCB material.
// Ignore 0.001 mm at the exact coincident seating face to avoid CGAL slivers.
// Nominal cone diameter at that plane is separately checked analytically.
module pcb_slab() {
    translate([ox,oy,pcb_z+0.001]) difference() {
        rounded_prism(pcb_length,pcb_width,pcb_thickness-0.001,1.25);
        for(p=holes) translate([p[0],p[1],-eps])
            cylinder(d=pcb_hole_d,h=pcb_thickness+2*eps);
    }
}
if(check=="board_seating") intersection() { base(); pcb_slab(); }

// ---- DIN rail mount (v7.2.0) ----
// Coincident seating faces are separated by 0.001 mm to avoid CGAL slivers.
module din_half(fixed) {
    translate([fixed ? rail_x : rail_x-100,-50,-100]) cube([100,outer_w+100,100]);
}
module latch_released() {
    din_place() translate([-din_release_travel,0]) linear_extrude(din_width) din_latch_2d();
}
if(check=="din_seated") intersection() {
    base_din(); translate([0,0,-0.001]) din_clip_assembled();
}
// Sliding the clip in along Y: tongues, grooves and body sweep; ridge sprung down.
if(check=="din_slide_path") intersection() {
    base_din();
    translate([0,0,-0.001]) din_place(-5) linear_extrude(din_y1+5) din_clip_2d(false);
}
if(check=="din_end_stop") intersection() {
    base_din();
    translate([0,0,-0.001]) din_place(-5)
        linear_extrude(din_y1+5+groove_end_clearance+0.2) din_clip_2d(false);
}
// Backing out 0.6 mm must meet the pocket's retaining ramp.
if(check=="din_detent") intersection() {
    base_din();
    translate([0,0,-0.001]) din_place(din_y0-0.6) linear_extrude(din_width)
        translate([detent_ridge[0],-detent_rise])
            square([detent_ridge[1]-detent_ridge[0],detent_rise]);
}
if(check=="din_rail_seated") intersection() { din_clip_assembled(); rail_mockup(0.001); }
if(check=="din_hook_fixed" || check=="din_hook_latch") intersection() {
    din_clip_assembled();
    rail_mockup(max(din_fixed_clearance,din_latch_clearance)+0.2);
    din_half(check=="din_hook_fixed");
}
// Latch pulled outward by its release travel: the rail passes straight by.
if(check=="din_latch_path") for(dv=[0:0.5:16]) intersection() {
    latch_released(); rail_mockup(dv);
}
// The pulled latch block and tab must not hit the rigid body or the case.
if(check=="din_latch_free") intersection() {
    latch_released();
    union() { base_din(); din_place() linear_extrude(din_width) din_body_2d(); }
}

// Mid-rail hook-on path, run in reverse as removal. 2D cross-section checks:
// the clip is a prism along the rail. Pivot is at the fixed-hook throat.
// Extruded only so the STL-based runner can report an empty result.
din_tilt = 10;
module din_tilted(a,latch_x=0) {
    translate([din_throat,latch_lip_top-din_latch_clearance]) rotate(a)
        translate([-din_throat,-(latch_lip_top-din_latch_clearance)]) union() {
            din_body_2d(); din_spring_2d(); translate([latch_x,0]) din_latch_2d();
        }
}
// Latch pulled by its release travel, case tilted about the fixed hook to 10 degrees.
if(check=="din_tilt_rotate") for(a=[0:1:din_tilt]) linear_extrude(1) intersection() {
    din_tilted(a,-din_release_travel); translate([0,0.001]) rail_2d();
}
// At 10 degrees, with the latch at rest: shift towards the fixed end, then lift off.
if(check=="din_tilt_unhook") for(dx=[0:0.25:1.8], dv=[0:0.25:12]) linear_extrude(1) intersection() {
    translate([dv>0 ? 1.8 : dx,-dv]) din_tilted(din_tilt);
    translate([0,0.001]) rail_2d();
}
