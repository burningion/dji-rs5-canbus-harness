/* Shared RSA holder for Mill-Max 889-22-008-70-501010. Units: mm.
 * Default output: BASE + REAR COVER, oriented for printing.
 * Mounting envelope derived from Riley Harmon's community RS2 connector STL:
 * https://github.com/rileyharmon/DJI-Ronin-RS2-Log-and-Replay
 * Credit: Riley Harmon, Cornelius von Einem, Casey Basichis.
 * CAD derivative: CC BY-NC 4.0, https://creativecommons.org/licenses/by-nc/4.0/
 * Rev B: Mill-Max geometry, captive housing, insulated wire tunnels,
 * asymmetric locating pins, rear cable-tie ear. No pogo-block solder or glue.
 * RS5 adaptation: shared nominal mount, separate entry point, thin fit gauge.
 * DJI lists the same M4-mounted Focus Wheel for RS2 and RS5. This supports
 * reusing the mount; it does not dimension or validate this printed holder.
 * Drawing: ../references/millmax-889-family.pdf p1, 889-22-0XX family.
 * Sheet title says 14 contacts; family drawing gives length = pins*0.100 in/2.
 * Here pins=8. RS5 fit/pad recess/pin orientation still need physical checks.
 * RS5 has ONE electrical RSA port; its other NATO rail has no communication.
 */

/* [Output] */
part = "print_plate"; // [print_plate,base,cover,fit_gauge,assembly,exploded,connector,collision]

/* [RSA mount - RS2 community dimensions, provisional on RS5] */
body_width = 19.80;
body_height = 28.85;
corner_radius = 2.0;
mount_pitch = 19.85;
mount_clearance = 4.3;
contact_offset_x = 0.02;
contact_offset_y = 0.22;
pad_recess = 0.0; // Placeholder: positive = pads below the supporting mount face.

/* [Mill-Max 889-22-008-70-501010 - drawing dimensions converted from inches] */
contact_pitch = 0.100 * 25.4;
header_width = 8 * 0.100 * 25.4 / 2;
header_height = 0.200 * 25.4;
header_body_depth = 0.110 * 25.4;
tip_to_rear_shoulder = 0.283 * 25.4;
rear_shoulder_to_wire = 0.158 * 25.4;
housing_rear_to_wire = 0.204 * 25.4;
tip_diameter = 0.042 * 25.4;
front_barrel_diameter = 0.059 * 25.4;
rear_shoulder_diameter = 0.067 * 25.4;
rear_crimp_diameter = 0.062 * 25.4;
header_max_stroke = 0.055 * 25.4;

/* [Fit and preload] */
target_compression = 0.60;
pocket_clearance = 0.40; // TOTAL increase in each pocket dimension.
axial_clearance = 0.20; // Forward housing play when NOT contacting the gimbal.
front_aperture_diameter = 1.95; // Clears the fixed barrel, not just the tip.
wire_tunnel_diameter = 2.10; // Verify printed bore size and actual wire insulation OD.
rear_insulation_overlap = 1.25;
locating_pin_diameter = 1.8;
locating_clearance = 0.25;
locating_pin_height = 1.2;

/* [Cable support] */
strain_relief_ear = true;

/* [Hidden] */
target_gimbal = is_undef(gimbal_model) ? "RS5" : gimbal_model;
$fn = 64;
eps = 0.02;
tip_to_housing_rear = tip_to_rear_shoulder + rear_shoulder_to_wire - housing_rear_to_wire;
front_projection = tip_to_housing_rear - header_body_depth;
front_fixed_projection = front_projection - header_max_stroke;
rear_shoulder_length = housing_rear_to_wire - rear_shoulder_to_wire;
// Loaded housing rear bears on cover's inner face; this datum sets compression.
base_thickness = tip_to_housing_rear - pad_recess - target_compression;
front_web = base_thickness - header_body_depth - axial_clearance;
cover_thickness = housing_rear_to_wire + rear_insulation_overlap;
total_thickness = base_thickness + cover_thickness;
key_positions = [[-7.5,3.8],[7.5,-3.0]]; // Not rotationally symmetric.

assert(target_compression>0 && target_compression<header_max_stroke,
       "Compression must be within specified spring stroke.");
assert(front_web>=0.8,"Front web too thin: revise standoff geometry.");
assert(axial_clearance>=0.15,"Allow for +/-0.13 mm housing height tolerance.");
assert(front_aperture_diameter>=front_barrel_diameter+0.30,
       "Front holes must clear the fixed barrel, not just the tip.");
assert(wire_tunnel_diameter>=rear_shoulder_diameter+0.30,
       "Wire tunnels must clear crimp shoulders.");
assert(contact_pitch-wire_tunnel_diameter>=0.4,
       "Keep a printable web between wire tunnels.");
assert(rear_insulation_overlap>=1,"Enclose bare metal up to insulated wire.");
assert(body_height/2-mount_pitch/2-mount_clearance/2>1.5,
       "Insufficient plastic outside M4 holes.");

module rounded_plate(w,h,t,r) {
    linear_extrude(height=t)
        hull() for(x=[-w/2+r,w/2-r],y=[-h/2+r,h/2-r])
            translate([x,y]) circle(r=r);
}

module contact_positions() {
    translate([contact_offset_x,contact_offset_y,0])
        for(x=[-1.5,-0.5,0.5,1.5],y=[-0.5,0.5])
            translate([x*contact_pitch,y*contact_pitch,0]) children();
}

module screw_holes(height) {
    for(y=[-mount_pitch/2,mount_pitch/2])
        translate([0,y,-eps]) cylinder(d=mount_clearance,h=height+2*eps);
}

module base() {
    difference() {
        union() {
            rounded_plate(body_width,body_height,base_thickness,corner_radius);
            for(p=key_positions)
                translate([p[0],p[1],base_thickness-eps])
                    cylinder(d=locating_pin_diameter,h=locating_pin_height+eps);
        }
        screw_holes(base_thickness);
        contact_positions() translate([0,0,-eps])
            cylinder(d=front_aperture_diameter,h=front_web+2*eps);
        translate([contact_offset_x-(header_width+pocket_clearance)/2,
                   contact_offset_y-(header_height+pocket_clearance)/2,front_web])
            cube([header_width+pocket_clearance,header_height+pocket_clearance,
                  base_thickness-front_web+eps]);
    }
}

// Hand-held alignment template only. No contacts or screws: the screws for
// the full holder would project much too far through this thin gauge.
// Does not establish pad recess, preload or rear-cover clearance.
module fit_gauge() {
    gauge_thickness = 1.2;
    difference() {
        rounded_plate(body_width,body_height,gauge_thickness,corner_radius);
        screw_holes(gauge_thickness);
        contact_positions() translate([0,0,-eps])
            cylinder(d=front_aperture_diameter,h=gauge_thickness+2*eps);
    }
}

// z=0 is INNER face. Print flipped, outer face on bed.
module cover() {
    difference() {
        union() {
            rounded_plate(body_width,body_height,cover_thickness,corner_radius);
            if(strain_relief_ear)
                translate([body_width/2+4,0,cover_thickness-3])
                    rounded_plate(10,9,3,1.5);
        }
        screw_holes(cover_thickness);
        contact_positions() translate([0,0,-eps])
            cylinder(d=wire_tunnel_diameter,h=cover_thickness+2*eps);
        for(p=key_positions) translate([p[0],p[1],-eps])
            cylinder(d=locating_pin_diameter+locating_clearance,
                     h=locating_pin_height+0.25+eps);
        if(strain_relief_ear)
            translate([body_width/2+3,-2,cover_thickness-3-eps])
                cube([2.5,4,3+2*eps]);
    }
}

module cover_for_print() {
    translate([0,0,cover_thickness]) rotate([180,0,0]) cover();
}

// Nominal envelope for interference checks. Wire OD 1.5 is illustrative only.
module connector_envelope() {
    translate([contact_offset_x-header_width/2,contact_offset_y-header_height/2,
               base_thickness-header_body_depth])
        cube([header_width,header_height,header_body_depth]);
    contact_positions() {
        translate([0,0,base_thickness-tip_to_housing_rear])
            cylinder(d=tip_diameter,h=header_max_stroke+eps);
        translate([0,0,base_thickness-header_body_depth-front_fixed_projection])
            cylinder(d=front_barrel_diameter,h=front_fixed_projection+eps);
        translate([0,0,base_thickness-eps])
            cylinder(d=rear_shoulder_diameter,h=rear_shoulder_length+eps);
        translate([0,0,base_thickness+rear_shoulder_length-eps])
            cylinder(d=rear_crimp_diameter,h=rear_shoulder_to_wire+eps);
        translate([0,0,base_thickness+housing_rear_to_wire]) cylinder(d=1.5,h=6);
    }
}

module assembly(explode=0) {
    color("#4b94a3") base();
    color("#c7a355") translate([0,0,explode/2]) connector_envelope();
    color("#9dbcc4") translate([0,0,base_thickness+explode]) cover();
}

echo("Target / connector",target_gimbal,"Mill-Max 889-22-008-70-501010");
echo("Mount is inherited from RS2 community mesh; physical fit is unverified.");
echo("Housing W/H/depth",[header_width,header_height,header_body_depth]);
echo("Free tip to housing rear",tip_to_housing_rear);
echo("Base / cover / total",[base_thickness,cover_thickness,total_thickness]);
echo("Front web / nominal loaded compression",[front_web,target_compression]);
echo("Pad recess is a physical-fit input",pad_recess);

if(part=="base") base();
else if(part=="cover") cover_for_print();
else if(part=="fit_gauge") fit_gauge();
else if(part=="assembly") assembly();
else if(part=="exploded") assembly(8);
else if(part=="connector") connector_envelope();
else if(part=="collision") intersection() {
    union() { base(); translate([0,0,base_thickness]) cover(); }
    connector_envelope();
}
else if(part=="print_plate") {
    translate([-16,0,0]) base();
    translate([16,0,0]) cover_for_print();
}
else assert(false,"Unknown output part");
