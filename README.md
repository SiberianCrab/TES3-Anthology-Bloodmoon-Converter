# TES3-Anthology-Bloodmoon-Converter

 A simple command-line tool that lets you convert Bloodmoon .esp/.esm mods from vanilla Solstheim location to the position it has on the Anthology map.
 
 Requires the latest version of tes3conv.exe from Greatness7 (https://github.com/Greatness7/tes3conv) to run.
 
 Program Capabilities:
 
 I. Processing grid Coordinates:
 - Updates grid coordinates for Cell, Landscape, and PathGrid records.

 II. Processing translation Coordinates:
 - Updates translation coordinates for objects located within relocatable Cells.
 - Updates coordinates for doors leading out of Interior cells.
 - Updates coordinates for transportation teleportations.
 
 III. Processing Coordinates in Game Scripts:
 - Updates coordinates in script commands: AiEscort, AiFollow, AiTravel, PlaceItem, Position, AiEscortCell, AiFollowCell, AiTravelCell, PlaceItemCell, PositionCell.
 
 IV. Processing Coordinates in Dialogue-Linked Scripts (Dialogue window Result section):
 - Updates the same set of commands as standard scripts: AiEscort, AiFollow, AiTravel, PlaceItem, Position, AiEscortCell, AiFollowCell, AiTravelCell, PlaceItemCell, PositionCell.

 V. Supports coordinate updates for a user-defined custom Cell list.
