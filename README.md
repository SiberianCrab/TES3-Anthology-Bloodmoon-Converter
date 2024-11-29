# TES3-Anthology-Bloodmoon-Converter

 A simple command-line tool that lets you convert Bloodmoon .esp/.esm mods from vanilla Solstheim location to the position it has on the Anthology map.
 
 Requires the latest version of tes3conv.exe from Greatness7 (https://github.com/Greatness7/tes3conv) to run.
 
 Program Capabilities:
 
 I. Processing grid Coordinates:
 - Identifies and adjusts grid coordinates for Cell, Landscape, and PathGrid records.

 II. Processing translation Coordinates:
 - Adjusts translation coordinates for objects located within relocatable Cell zones.
 - Updates coordinates for doors leading out of interior zones.
 - Modifies coordinates for transportation teleportation points.
 
 III. Updating Coordinates in Game Scripts:
 - Supports coordinate updates in script commands: AiEscort, AiFollow, AiTravel, PlaceItem, Position.
 - Handles commands specifying target Cell: AiEscortCell, AiFollowCell, AiTravelCell, PlaceItemCell, PositionCell.
 
 IV. Updating Coordinates in Dialogue-Linked Scripts (Dialogue window Result section):
 - Supports the same set of commands as standard scripts: AiEscort, AiFollow, AiTravel, PlaceItem, Position, AiEscortCell, AiFollowCell, AiTravelCell, PlaceItemCell, PositionCell.

 V. Supports coordinate processing for a user-defined custom Cell list.
