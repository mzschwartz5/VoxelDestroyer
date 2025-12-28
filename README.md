# VoxelDestroyer
A Maya plugin for simulation of soft-body deformation and fracture, in real-time.

Real-time in Maya? What's the point? That's a fair question - I see this plugin as a step in a (currently incomplete) pipeline. Prepare the mesh in Maya, using this plugin to paint simulation weights and constraints onto the mesh and quickly visualize the results. Then export the mesh and the weights + constraints to a game engine (where real-time actually matters), and run the simulation in-game with a corresponding engine plugin.

- [Installing the plugin](#installing-the-plugin)
  - [Installation requirements](#installation-requirements)
  - [Installation steps](#installation-steps)
- [Developing the plug-in](#developing-the-plug-in)
  - [Development requirements](#development-requirements)
- [License](#license)

# Installing the plugin

### Installation requirements

- Windows OS (for DirectX11 + Maya Viewport 2.0)
- Maya 2025+ (may work in lower versions but only tested in Maya 2025)

### Installation steps

- Download the `.mll` file and put it in your plugin directory. Then load it up from Maya's plugin manager (Windows > Settings and Preferences > Plug-in Manager). If you don't know your plugin directory path, look at the plug-in manager for examples.
- Ensure the `dx11Shader.mll` plugin is loaded in the plugin manager.
- Enable Viewport 2.0 with DirectX11: `Windows > Settings/Preferences > Preferences > Display > Viewport 2.0 > DirectX 11` (this may require a Maya restart).

In the future, I hope to make this available for download directly through the Maya plugin app store.

# Developing the plug-in

### Development requirements
- vcpkg: (clone from https://github.com/microsoft/vcpkg.git , go to the directory, and run the bootstrap: .\bootstrap-vcpkg.bat)
- Environment variables: 
  - `MAYA_PLUGIN_DIR` set to a directory where Maya finds plugins. There are several (run `getenv MAYA_PLUG_IN_PATH` in MEL command line or open the plug-in manager). Ideally, choose one that doesn't require admin privelege. 
  - `VCPKG_ROOT` set to the installation directory of `vcpkg`.
 - Dependencies listed in `vcpkg.json` and will be downloaded automatically on initial build.

Debug configuration: builds dependencies as DLLs and puts them alongside the plugin `.mll` in the specified maya plugin directory. Building as DLLs reduces the build time.

Release configuration: builds dependencies as static libs. Slower to build, but easier to distribute / consume. Release builds, of course, will run much faster as well.

# License

This project uses the CGAL (Computational Geometry Algorithms Library).

CGAL is licensed under the GNU General Public License (GPL).
See https://www.cgal.org/license.html for details.

If you distribute this software, you must comply with the terms of the GPL.