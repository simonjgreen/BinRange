// Geometric checks only, not a PLA force/strain simulation.
include <wrover-case-v7.1.0.scad>
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
