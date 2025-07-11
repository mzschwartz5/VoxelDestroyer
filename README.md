# VoxelDestroyer
A Maya plugin for simulation of deformable-body destruction, using a voxelized control-mesh.

# Developming the plug-in

Requirements:
- vcpkg: (clone from https://github.com/microsoft/vcpkg.git , go to the directory, and run the bootstrap: .\bootstrap-vcpkg.bat)
- Environment variables: 
  - `MAYA_PLUGIN_DIR` set to a directory where Maya finds plugins. There are several (run `getenv MAYA_PLUG_IN_PATH` in MEL command line or open the plug-in manager). Ideally, choose one that doesn't require admin privelege. 
  - `VCPKG_ROOT` set to the installation directory of `vcpkg`.
 - Dependencies listed in `vcpkg.json` and will be downloaded automatically on initial build.

Debug configuration: builds dependencies as DLLs and puts them alongside the plugin `.mll` in the specified maya plugin directory. Building as DLLs reduces the build time.

Release configuration: builds dependencies as static libs. Slower to build, but easier to distribute / consume. Release builds, of course, will run much faster as well.

# Using the plug-in

Requirements: Windows OS (for DirectX11 + Maya Viewport 2.0)

Simply download the `.mll` file and put it in yoru plug-in directory. Then load it up from Maya's plugin manager. (WIP: instructions on using the plug-in)