# TES3-Anthology-Bloodmoon-Converter

A simple command-line tool that lets you move Bloodmoon .esp/.esm mods from vanilla Solstheim location to it's Anthology map position - 7 cells east, 6 cells north.

Requires the latest version of `tes3conv.exe` from Greatness7: [https://github.com/Greatness7/tes3conv](https://github.com/Greatness7/tes3conv)

---
 
Program capabilities

I. Processing grid X | Y coordinates:
- Updates grid coordinates for Cell, Landscape, and PathGrid records.

II. Processing translation X | Y coordinates:
- Updates coordinates for objects located within relocatable Cells.
- Updates destination coordinates for Interior cell doors, leading to relocatable Cells.
- Updates destination coordinates for NPC's Travel services, leading to relocatable Cells.

III. Processing translation X | Y coordinates in game Scripts:
- Updates coordinates in script commands: AiEscort, AiFollow, AiTravel, PlaceItem, Position, AiEscortCell, AiFollowCell, AiTravelCell, PlaceItemCell, PositionCell.

IV. Processing translation X | Y coordinates in dialogue-linked scripts (dialogue window 'Result' section):
- Updates the same set of commands as standard scripts: AiEscort, AiFollow, AiTravel, PlaceItem, Position, AiEscortCell, AiFollowCell, AiTravelCell, PlaceItemCell, PositionCell.

V. Supports translation and grid coordinates updates for a user-defined Cells list (tes3_ab_custom_cell_x-y_data.txt).

---

## Usage

### Windows
```bash
.\tes3_ab_converter.exe [OPTIONS] "[TARGETS]"
```

### Linux
```bash
./tes3_ab_converter [OPTIONS] "[TARGETS]"
```

---

## Options

| Option        | Description                                             |
|---------------|---------------------------------------------------------|
| `-b`, `--batch`    | Enable batch mode (required when processing multiple files) |
| `-s`, `--silent`   | Suppress non-critical messages (faster conversion)        |
| `-1`, `--bm-to-ab` | Convert Bloodmoon -> Anthology Bloodmoon                        |
| `-2`, `--ab-to-bm` | Convert Anthology Bloodmoon -> Bloodmoon                        |
| `-h`, `--help`     | Show help message                                  |

---

## Target Formats

### Single File (works without batch mode):

#### Windows
```
mod-in-the-same-folder.esp  
C:\Morrowind\Data Files\mod.esm
```

#### Linux
```
mod-in-the-same-folder.esp  
/home/user/morrowind/Data Files/mod.esm
```

---

### Multiple Files (requires -b batch mode):

#### Windows
```
file1.esp;file2.esm;file 3.esp  
:\Mods\mod.esp;C:\Morrowind\Data Files\Master mod.esm;Mod-in-the-same-folder.esp
```

#### Linux
```
file1.esp;file2.esm;file 3.esp  
/mnt/data/mods/file1.esp;/home/user/morrowind/Data Files/Master mod.esm;mod-in-the-same-folder.esp
```

---

### Entire Directory (batch mode, recursive processing):

#### Windows
```
C:\Morrowind\Data Files\  
.\Data\  (relative path)
```

#### Linux
```
/home/user/morrowind/Data Files/  
./Data/  (relative path)
```

---

## Important Notes

### Supported:
- ASCII-only file paths (English letters, numbers, standard symbols)
- Both absolute (`C:\...`) and relative (`.\Data\...`) paths

### Not Supported:
- Paths containing non-ASCII characters (e.g., Cyrillic, Chinese, special symbols)
- Wildcards (`*`, `?`) in CMD

### Solution for Non-ASCII Paths:
If your files are in a folder with non-ASCII characters (e.g., `C:\Игры\Morrowind\`),  
move them to a folder with only English characters (`C:\Games\Morrowind\`).

---

## Shell Compatibility

### PowerShell (Recommended on Windows)
- Fully supports batch processing, recursive search, and wildcards

### CMD (Limited Support)
- Does not support recursive file selection with wildcards

### Bash/Zsh (on Linux)
- Fully supports batch processing and wildcard expansion

---

## Wildcard Support

### PowerShell (Recommended for Windows)

Convert all `.esp` files recursively in current folder:
```powershell
& .\tes3_ab_converter.exe -b (Get-ChildItem -Recurse -Include "*.esp").FullName
```

Convert all `.esm` files in specific folder (without subfolders):
```powershell
& .\tes3_ab_converter.exe -b (Get-ChildItem -Path "C:\Mods\" -Include "*.esm").FullName
```

Convert all `.esm` files in specific folder recursively:
```powershell
& .\tes3_ab_converter.exe -b (Get-ChildItem -Path "C:\Mods\" -Recurse -Include "*.esm" -File).FullName
```

---

### CMD (Limited Wildcard Support, No Recursion)

Convert all `.esp` files in current folder:
```cmd
for %f in ("*.esp") do tes3_ab_converter.exe -b -2 "%~f"
```

Convert all `.esm` files in specific folder (without subfolders):
```cmd
for %f in ("C:\Mods\*.esm") do tes3_ab_converter.exe -b -2 "%~f"
```

---

### Bash/Zsh (Full Wildcard Support on Linux)

Convert all `.esp` files recursively in current folder:
```bash
find . -type f -iname "*.esp" -exec ./tes3_ab_converter -b -2 {} \;
```

Convert all `.esm` files in specific folder (without subfolders):
```bash
find /path/to/mods -maxdepth 1 -type f -iname "*.esm" -exec ./tes3_ab_converter -b -2 {} \;
```

Convert all `.esm` files in specific folder recursively:
```bash
find /path/to/mods -type f -iname "*.esm" -exec ./tes3_ab_converter -b -2 {} \;
```

---

## Example Commands

Convert an entire folder:
```powershell
& .\tes3_ab_converter.exe -b -1 "C:\Morrowind\Data Files\"
```

```bash
./tes3_ab_converter -b -1 "/home/user/morrowind/Data Files/"
```

Convert multiple specific files:
```powershell
& .\tes3_ab_converter.exe -b -2 "D:\Mods\mod.esp;Mod-in-the-same-folder.esp"
```

```bash
./tes3_ab_converter -b -2 "/mnt/data/mods/mod.esp;./Mod-in-the-same-folder.esp"
```

Convert all files starting with `RR_` in a folder:
```powershell
& .\tes3_ab_converter.exe -b (Get-ChildItem -Path "C:\Morrowind\Data Files\" -Recurse -Include "RR_*.esp").FullName
```

```bash
find "/home/user/morrowind/Data Files/" -type f -iname "RR_*.esp" -exec ./tes3_ab_converter -b -1 "{}" \;
```
