# Reference sources

Restored on 2026-09-23 using the sources cited in the project README. The PDFs,
community STL, and upstream README are unmodified source files. The PNGs are
page renders or crops, with white backgrounds for readability.

| Local file | Source |
| --- | --- |
| [dji-rs5-user-manual.pdf](dji-rs5-user-manual.pdf) | [DJI RS 5 User Manual, English](https://dl.djicdn.com/downloads/DJI_RS_5/20260115/UM_2/DJI_RS_5_User_Manual_en.pdf), 37 pages |
| [dji-rs5-overview.png](dji-rs5-overview.png) | RS5 manual, PDF/printed page 6 |
| [dji-rs5-ports.png](dji-rs5-ports.png) | RS5 manual, PDF/printed page 17 |
| [dji-r-sdk-v2.5.pdf](dji-r-sdk-v2.5.pdf) | `SDK documentation_20210729/DJI_R_SDK_Protocol_and_User_Interface_EN_v2.5.pdf` inside [DJI's SDK archive](https://terra-1-g.djicdn.com/851d20f7b9f64838a34cd02351370894/Ronin%E7%B3%BB%E5%88%97/SDK%20documentation_20210729.zip), linked by the [DJI RS SDK page](https://www.dji.com/rs-sdk); 22 pages |
| [dji-sdk-pinout.png](dji-sdk-pinout.png) | SDK PDF page 21 / printed page 19, section 3.1.2, including the illustration, signal table, and orientation note |
| [millmax-889-family.pdf](millmax-889-family.pdf) | [Mill-Max 889-22-014-70-501010 datasheet hosted by DigiKey](https://mm.digikey.com/Volume0/opasdata/d220001/medias/docus/6448/8892201470501010.pdf), 2 pages; page 1 contains the 889-22-0XX family drawing |
| [millmax-889-drawing-detail.png](millmax-889-drawing-detail.png) | Family drawing cropped from page 1 of the Mill-Max PDF |
| [rileyharmon-rs2-connector.stl](rileyharmon-rs2-connector.stl) | Riley Harmon's [3d-print-ronin-can-connector.stl](https://github.com/rileyharmon/DJI-Ronin-RS2-Log-and-Replay/blob/38b6f139398a4b34156e8ca66073ca1744ca357b/3d-print-ronin-can-connector.stl) |
| [rileyharmon-README.md](rileyharmon-README.md) | Riley Harmon's [original README](https://github.com/rileyharmon/DJI-Ronin-RS2-Log-and-Replay/blob/38b6f139398a4b34156e8ca66073ca1744ca357b/README.md), including attribution and CC BY-NC 4.0 license notice |

The Riley Harmon files were downloaded from `main`, whose head at restoration
was `38b6f139398a4b34156e8ca66073ca1744ca357b` (2022-12-06).
Credit to Riley Harmon, Cornelius von Einem, and Casey Basichis as stated in the
upstream README. DJI and Mill-Max documents retain their original notices.

The image restoration reused surviving renders in `tmp/pdfs/` after comparing
their associated PDF page text with the freshly downloaded PDFs and visually
checking the images. These temporary files are not needed to use the references.
For reproduction, all four source pages were rendered at three times their PDF
point dimensions (216 dpi). The SDK image retains pixels `(0, 0, 1174, 1090)` from its page render;
the Mill-Max detail retains `(100, 435, 1010, 1055)`. Crop coordinates are
`(left, top, right, bottom)` measured from the upper-left corner.

The SDK pinout is the legacy RS2 illustration. Restoring it does not independently
validate the RS5 electrical or mechanical assumptions documented in the project
README.
