# Resourcepack Manager

**ResourcepackManager** is an application developed to assist resourcepack creators in managing multiple resource packs. With an implemented script system, ResourcepackManager can select a folder or file to execute user-defined actions.

## Instructions

The instruction system works with a file containing a list of commands that point to a file and tell the program what to do with it. It accepts 2 resourceapcks to be called,
with '>' on the instructions it will look for the target in the main folder, typing '<' it's going to look for the target in the secondary folder.

The current version (1.0) of ResourcepackManager has the following actions:

---

### **Copy/Move**:
- The program selects the target file/folder and moves/copies it to the destination path.
- The program automatically creates folders in the specified path if they do not exist.
- If the path includes a file name, the program automatically updates the file name to match the path.

**Examples:**
```
> "pack/pack 1.21.4.mcmeta"
   copy "pack.mcmeta";

> "blockstates/"
   copy "assets/minecraft/";
```

---

### **Remove**:
- The program selects the target file/folder and deletes it.

**Examples:**
```
> "minecraft:models/item/sweet_berries.json"
   remove;

> "minecraft:textures/entity/equipment/"
   remove;
```

---

### **Edit**:
This function contains a set of sub-actions related to the file:

1. **"name"**: changes the file name.
```
> "minecraft:models/item/potion.json"
   edit name "bottle_drinkable.json";
```

2. **"display" / "texture_path"**: updates the respective model member to the value provided in the script.
```
> "tools/fishing_rod_cast.json"
   edit texture_path set {
     "0": "item/fishing_rod",
     "particle": "item/fishing_rod"
   };
```

3. **"dimensions"**: resizes images to the specified dimensions.
```
> "armors/iron_layer_1.png"
   edit dimensions 64x32;
```

---

### **Autofill**:
- The program creates a file that reflects the folders in the resourcepack to be loaded by the atlas logger.

**Example:**
```
> "minecraft:atlases/custom.json"
   autofill;
```

---

### **Disassemble**:
- The program selects the target file and extracts the groups into their own file models.
**Example:**
```
> "tools/bow.json"
   disassemble "minecraft:models/item/";
```

---

### **Paint**:
- The program takes the texture and paints it according to the specified palette.
- Add "./" prefix to point to the target folder, by defualt it's going to look in the assets folder.
**Example:**
```
> "tools/bow.png"
   paint "./materials/wood_map.png" "./materials/wood_material.png";
```

---

### **Permutate_Texture**:
- The program takes the texture and a list of palettes to paint the duplicates.
- Add "./" prefix to point to the target folder, by defualt it's going to look in the assets folder.
**Example:**
```
> "bricks.png"
   permutate_texture "snowstone_map.png" {"yellow_stone.png", "pink_stone.png", "brown_stone.png", "burgundy.png", "limestone.png", "blue_stone.png", "green_stone.png"};
```

- The duplicate will be saved in the same location as the target file, using the palette name as a prefix.

---

### **Convert_Overrides**:
- With the change in the overrides system, this action translates the bow pull, compass and clock time/angle, and `custom_model_data` into their own file.

**Example:**
```
> "minecraft:models/item/bow.json"
   convert_overrides;
```

- (There are no intentions to add support for other predicates).
