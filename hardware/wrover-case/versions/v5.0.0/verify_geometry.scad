// Run with -D 'part="none"'. Most intersections must be empty.
// catch / locating_x / locating_y must contain material: positive mechanical stops.
include <wrover-case-v5.0.0.scad>
check = "closed";
module lid_without_tongues() {
    difference() { lid_print(); both_sides() latch_tongue(); }
}
module assembled_without_tongues(lift=0) {
    translate([0,outer_w,total_h+lift]) rotate([180,0,0]) lid_without_tongues();
}
module tongues_assembled(lift=0,release=0) {
    translate([0,outer_w,total_h+lift]) rotate([180,0,0])
        both_sides() translate([0,release,0]) latch_tongue();
}
if(check=="closed") intersection() { base(); lid_assembled(); }
// Intentional tongue deflection is excluded; all rigid features need a clear path.
if(check=="insertion") for(lift=[0:1:16])
    intersection() { base(); assembled_without_tongues(lift); }
if(check=="components") intersection() {
    lid_assembled();
    // Unloaded pressure pads deliberately overlap the PCB, not its components.
    intersection() {
        board_mockup();
        translate([-10,-10,pcb_top+eps]) cube([outer_l+20,outer_w+20,30]);
    }
}
if(check=="catch") intersection() {
    base(); tongues_assembled(clip_lift_clearance+0.2);
}
// A displaced tongue is a clearance envelope, not an elastic simulation.
if(check=="release") for(lift=[0:1:16]) intersection() {
    union() { base(); board_mockup(); }
    tongues_assembled(lift,1.2);
}
// The skirt alone must stop excessive lateral motion in both axes.
if(check=="locating_x") for(sign=[-1,1]) intersection() {
    base(); translate([sign*(alignment_clearance+0.15),0,0])
        assembled_without_tongues();
}
if(check=="locating_y") for(sign=[-1,1]) intersection() {
    base(); translate([0,sign*(alignment_clearance+0.15),0])
        assembled_without_tongues();
}
if(check=="allowed_play") for(x=[-0.2,0.2],y=[-0.2,0.2]) intersection() {
    base(); translate([x,y,0]) lid_assembled();
}
