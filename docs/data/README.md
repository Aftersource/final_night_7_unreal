# Fault lines (shared by client and server)

`PB2002_boundaries.json`: tectonic plate boundaries, the in-game FAULT LINES. GeoJSON LineStrings, [lon, lat] degrees.
Source: Bird, P. (2002), PB2002; GeoJSON by Hugo Ahlenius, Nordpil. https://github.com/fraxen/tectonicplates (GeoJSON/PB2002_boundaries.json), downloaded 2026-11-14.
Licence: ODC-BY 1.0. Credit required: "Plate boundaries: Bird, P. (2002), PB2002; GeoJSON by Hugo Ahlenius, Nordpil (ODC-BY 1.0)."
Citation: Bird, P. (2002). An updated digital model of plate boundaries. G3 4(3), 1027. https://doi.org/10.1029/2001GC000252

The Unreal client draws these same lines from Source/FinalNight/Private/FNEarthLines.h (FNEarthPlates: lat*100, lon*100 int16 pairs, 32767 ends a line), generated from this file.
The detailed GEM active faults (CC BY-SA 4.0, share-alike) are deliberately NOT used in the game; reference only.
