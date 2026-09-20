// Render diagnostic intersections using -D 'part="none"'.
// Empty results are expected except check="catch", which must have volume.
include <wrover-case-v4.0.0.scad>
check = "closed"; // [closed,insertion,components,catch,release]
module lid_without_hooks() {
    difference() {
        lid_print();
        both_sides()
            translate([clip_end_x-clip_hook_width-eps,-6,-eps])
                cube([clip_hook_width+2*eps,8,clip_tip+2*eps]);
    }
}
module assembled_without_hooks(lift) {
    translate([0,outer_w,total_h+lift]) rotate([180,0,0]) lid_without_hooks();
}
module hooks_assembled(lift=0,release=0) {
    translate([0,outer_w,total_h+lift]) rotate([180,0,0])
        both_sides() translate([0,-release,0]) intersection() {
            latch_arm();
            translate([clip_end_x-clip_hook_width-eps,-6,-eps])
                cube([clip_hook_width+2*eps,8,clip_tip+2*eps]);
        }
}
if(check=="closed") intersection() { base(); lid_assembled(); }
// Hooks intentionally touch the entry ramp while closing. Verify all other
// features have a free vertical insertion path, including sockets over pins.
if(check=="insertion") for(lift=[0:1:12])
    intersection() { base(); assembled_without_hooks(lift); }
if(check=="components") intersection() {
    lid_assembled();
    // Exclude PCB thickness: the unloaded spring pads deliberately overlap it.
    intersection() {
        board_mockup();
        translate([-10,-10,pcb_top+eps]) cube([outer_l+20,outer_w+20,30]);
    }
}
if(check=="catch") intersection() { base(); hooks_assembled(clip_lift_clearance+0.2); }
if(check=="release") for(lift=[0:1:12])
    intersection() { base(); hooks_assembled(lift,1.3); }
