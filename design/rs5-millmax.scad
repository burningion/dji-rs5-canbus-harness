// RS5 prototype entry point: Mill-Max 889-22-008-70-501010 prewired contacts.
// Uses the shared RSA footprint supported by DJI Focus Wheel compatibility.
// Nominal dimensions are inherited from RS2, NOT measured on an RS5.
// Use ONLY the electrical RSA/NATO port (manual item 17), next to the
// joystick-mode switch. The opposite NATO rail has no communication.
// First export/print part="fit_gauge" and check the unpowered gimbal by hand.
// Edit dimensions in rsa-millmax.scad or pass OpenSCAD -D overrides.
gimbal_model = "RS5";
include <rsa-millmax.scad>
