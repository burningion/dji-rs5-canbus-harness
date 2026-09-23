/* RS2 side-port spring-contact carrier, prototype rev A.
 * Units: mm. This is a fit-test candidate, not a measured DJI part.
 * Nominal envelope / mounting pitch derived from Riley Harmon's connector STL:
 * https://github.com/rileyharmon/DJI-Ronin-RS2-Log-and-Replay
 * Credit: Riley Harmon, Cornelius von Einem, Casey Basichis; see source README.
 * This derivative: CC BY-NC 4.0, https://creativecommons.org/licenses/by-nc/4.0/
 * Changes: parametric solid, individual plunger apertures, stepped header pocket,
 * controlled seating depth, optional cable tie ear.
 * Superseded PRECI-DIP revision; current Mill-Max build guide: ../../README.md.
 */

/* [Carrier dimensions - verify against your RS2] */
body_width = 19.80;
body_height = 28.85;
body_thickness = 8.0;
corner_radius = 2.0;
mount_pitch = 19.85; // Measured from upstream STL, NOT an official DJI dimension.
mount_clearance = 4.3;
contact_offset_x = 0.02;
contact_offset_y = 0.22;

/* [PRECI-DIP 813-S1-008-10-014101] */
contact_pitch = 2.54;
header_width = 10.16;
header_height = 5.08;
header_body_depth = 4.0;
header_initial_height = 6.0;
header_max_stroke = 1.4;
pocket_clearance = 0.20; // Total size increase, not per side.
plunger_hole_diameter = 1.45; // For 1.07 mm tip; check your printer's actual hole size.

/* [Contact preload - MEASURE pad recess before assembly] */
// Front carrier face is z=0. RS2 pads are at z=-pad_recess when mounted.
// Positive pad_recess means pads lie below the surface supporting the carrier.
pad_recess = 0.0; // Placeholder; NOT a measurement of the RS2.
target_compression = 0.60; // Engineering starting point, not a DJI specification.

/* [Optional cable support] */
strain_relief_ear = false;

$fn = 64;
eps = 0.02;
face_web = header_initial_height - header_body_depth - pad_recess - target_compression;

assert(target_compression > 0 && target_compression < header_max_stroke,
       "Compression must remain inside the selected contact's stroke.");
assert(face_web >= 0.7,
       "Insufficient face web: adjust mounting standoff or choose a longer contact.");
assert(body_thickness > face_web + header_body_depth,
       "Carrier must contain the header body and leave rear solder access.");
assert(body_height/2 - mount_pitch/2 - mount_clearance/2 > 1.5,
       "Not enough plastic outside the M4 holes.");

module rounded_plate(w,h,t,r) {
    linear_extrude(height=t)
        hull() for(x=[-w/2+r,w/2-r], y=[-h/2+r,h/2-r])
            translate([x,y]) circle(r=r);
}

module carrier() {
    difference() {
        union() {
            rounded_plate(body_width,body_height,body_thickness,corner_radius);
            if(strain_relief_ear)
                translate([body_width/2+3,0,body_thickness-3])
                    rounded_plate(9,9,3,1.5);
        }
        // Plain M4 clearance holes. Select screw length from actual thread depth.
        for(y=[-mount_pitch/2,mount_pitch/2])
            translate([0,y,-eps]) cylinder(d=mount_clearance,h=body_thickness+2*eps);

        // Eight independent tip apertures hold the housing behind a supporting web.
        translate([contact_offset_x,contact_offset_y,0]) {
            for(x=[-1.5,-0.5,0.5,1.5],y=[-0.5,0.5])
                translate([x*contact_pitch,y*contact_pitch,-eps])
                    cylinder(d=plunger_hole_diameter,h=face_web+2*eps);
            // Header installs from the rear. Bond its plastic body after fit checks.
            translate([-(header_width+pocket_clearance)/2,
                       -(header_height+pocket_clearance)/2,face_web])
                cube([header_width+pocket_clearance,
                      header_height+pocket_clearance,
                      body_thickness-face_web+eps]);
        }
        if(strain_relief_ear)
            translate([body_width/2+3,-2,body_thickness-3-eps])
                cube([2.5,4,3+2*eps]);
    }
}

echo("Face web", face_web);
echo("Free tip protrusion beyond front face", pad_recess+target_compression);
echo("RS2 pad recess placeholder - verify", pad_recess);
carrier();
