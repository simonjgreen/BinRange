include <wrover-case-v6.0.0.scad>
check="closed";
module assembled(lift=0) {
    translate([0,outer_w,total_h+lift]) rotate([180,0,0]) children();
}
module moving(angle=0) { both_sides() rotated_carrier(angle); }
// A kinematic twist envelope, not a material or force simulation.
module twisted_arm(angle=0,right=false) {
    x0 = right ? clip_x+clip_width : clip_x-torsion_length;
    slices=12;
    module section(u) {
        widen=0.5*max(0,1-min(u,1-u)*torsion_length/1.5);
        a=angle*(right ? 1-u : u);
        translate([x0+u*torsion_length,torsion_axis_y,lid_thickness/2])
            rotate([-a,0,0])
                translate([-eps/2,-torsion_width/2,-lid_thickness/2])
                    cube([eps,torsion_width+widen,lid_thickness]);
    }
    for(i=[0:slices-1]) hull() { section(i/slices); section((i+1)/slices); }
}
if(check=="closed") intersection() { base(); lid_assembled(); }
if(check=="catch") intersection() { base(); assembled(0.5) moving(); }
if(check=="release") for(lift=[0:1:16],offset=[-0.2,0.2]) intersection() {
    union() { base(); board_mockup(); }
    translate([0,offset,0]) assembled(lift) moving(10);
}
if(check=="pcb_sweep") for(angle=[0:1:10]) intersection() {
    board_mockup(); assembled() moving(angle);
}
if(check=="lid_sweep") for(angle=[0:1:10]) intersection() {
    fixed_lid(); moving(angle);
}
if(check=="arm_sweep") for(angle=[0:2:10]) intersection() {
    base(); assembled() both_sides() {
        twisted_arm(angle); twisted_arm(angle,right=true);
    }
}
if(check=="components") intersection() {
    lid_assembled();
    intersection() {
        board_mockup();
        translate([-10,-10,pcb_top+eps]) cube([outer_l+20,outer_w+20,30]);
    }
}
