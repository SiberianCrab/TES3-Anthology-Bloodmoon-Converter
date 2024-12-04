# TES3-Anthology-Bloodmoon-Converter

 A simple command-line tool that lets you move Bloodmoon .esp/.esm mods from vanilla Solstheim location to the position it has on the Anthology map.
 
 Requires the latest version of tes3conv.exe from Greatness7 (https://github.com/Greatness7/tes3conv) to run.
 
 Program capabilities:
 
 I. Processing grid coordinates:
 - Updates grid coordinates for Cell, Landscape, and PathGrid records.

 II. Processing translation coordinates:
 - Updates coordinates for objects located within relocatable Cells.
 - Updates coordinates for Interior cell doors, leading to relocatable Cells.
 - Updates coordinates for Travel Services, leading to relocatable Cells.
 
 III. Processing translation coordinates in game Scripts:
 - Updates coordinates in script commands: AiEscort, AiFollow, AiTravel, PlaceItem, Position, AiEscortCell, AiFollowCell, AiTravelCell, PlaceItemCell, PositionCell.
 
 IV. Processing translation coordinates in dialogue-linked scripts (dialogue window 'Result' section):
 - Updates the same set of commands as standard scripts: AiEscort, AiFollow, AiTravel, PlaceItem, Position, AiEscortCell, AiFollowCell, AiTravelCell, PlaceItemCell, PositionCell.

 V. Supports translation and grid coordinates updates for a user-defined custom Cell list (tes3_ab_custom_cell_x-y_data.txt).
