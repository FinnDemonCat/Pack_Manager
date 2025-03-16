# Resourcepack Manager
 ResourcepackManager is an application developed to assist resourcepack creators to manage multiple resourcepacks.
 With an implemented scrip system, the instructions, ResourcepackManager can pick a folder or group of files to execute actions determined by the user.
# Instructions
 The instruction system works with a file with a list of commands that point to a file and tell the program what to do with it.
 The current version (1.0) of ResourcepackManager has the following actions:
 - Copy/Move:
  The program will pick the target file/folder and move/copy to destination path.
  The program will automatically create the folders as in the path when it can't find it.
  In case the path includes a file name, the program will automatically update the file name to match the path.
 - Remove:
  The program will The program will pick the target file/folder and delete it.
 - Edit:
  This function contain a set of sub-actions related to the file:
  - "name" will change the filename.
  - "display" / "texture_path" will upate the respective model member to the one given in the scrip.
  - "dimentions" will resize images to the given dimentions.
 - Autofill:
   The program will create a file that reflects the folders in the resourcepack to be loaded by the atlas logger.
 - Disassemble:
   The program will pick the target file and extract the groups to their own file models.
 - Paint:
   The program will take the texture and paint to the acording pallet
 - Permutate_Texture:
   The program will take the texture and a list of pallets to paint the duplicates with.
 - Convert_Overrides:
   As with the change of overrides system, this action will translate bow pull, compass and clock time/angle and custom_model_data to it's own file.
   (No intentios to add support to other predicates)
