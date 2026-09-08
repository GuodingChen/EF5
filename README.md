Ensemble Framework For Flash Flood Forecasting (EF5)
===
![version](https://img.shields.io/badge/version-1.2.7-orange.svg?style=flat) ![Linux Build Status](https://img.shields.io/github/actions/workflow/status/AHWALab/EF5/build.yml?label=Linux%20Build%20Status&branch=v1.2.5&style=flat&logo=linux) ![Windows Build Status](https://img.shields.io/github/actions/workflow/status/AHWALab/EF5/build.yml?label=Windows%20Build%20Status&branch=v1.2.5&style=flat&logo=windows)

EF5 was created by the Hydrometeorology and Remote Sensing Laboratory at the University of Oklahoma.
The goal of EF5 is to have a framework for distributed hydrologic modeling that is user friendly, adaptable, expandable, all while being suitable for large scale (e.g. continental scale) modeling of flash floods with rapid forecast updates. Currently EF5 incorporates 3 water balance models including the Sacramento Soil Moisture Accouning Model (SAC-SMA), Coupled Routing and Excess Storage (CREST), and hydrophobic (HP). These water balance models can be coupled with either linear reservoir or kinematic wave routing. 

## Learn More

General information about EF5 can be found at [AHWA Lab's Webpage](https://ahwa.lab.uiowa.edu/ensemble-framework-flash-flood-forecasting-ef5). YouTube videos with legacy basic training may be found at [EF5's YouTube Channel](https://www.youtube.com/channel/UCgoGJtdeqHgwoYIRhkgMwog). The source code for the original development transitioned to NWS operations can be found on HyDROSLab's GitHub at [https://github.com/HyDROSLab/EF5](https://github.com/HyDROSLab/EF5).

See [EF5's Documentation](https://ef5docs.readthedocs.io/en/latest/) for the EF5 operating manual which describes configuration options.

## Compiling with CMake

EF5 requires a C++11 compiler, CMake 3.16 or newer, OpenMP, zlib, libtiff,
and libgeotiff.

### Linux

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### macOS

Apple Clang requires a separate OpenMP runtime. You can either install
Homebrew `libomp`, or use Homebrew GCC directly:

```sh
cmake -S . -B build -DCMAKE_CXX_COMPILER=g++-15 -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The executable is written to the project root as `ef5`, next to
`CMakeLists.txt`.

### Optional utilities

The kinematic-wave test and data-conversion utilities are disabled by default.
Enable them only when needed:

```sh
cmake -S . -B build -DEF5_BUILD_KWTEST=ON -DEF5_BUILD_TOOLS=ON
cmake --build build --parallel
```

`TRMMV6Clip` additionally requires HDF4 and JPEG and is controlled by
`EF5_BUILD_TRMMV6_TOOL`.

### Installation

```sh
cmake --install build --prefix /desired/install/prefix
```

## Contributors

The following people currently contribute to EF5:

- [Humberto Vergara](https://ahwa.lab.uiowa.edu/people/humberto-vergara-phd)
- [Naman Mehta](https://ahwa.lab.uiowa.edu/people/naman-mehta)
- [Vanessa Robledo](https://ahwa.lab.uiowa.edu/people/vanessa-robledo-msc)
- [Santiago Henao Gomez](https://ahwa.lab.uiowa.edu/people/santiago-henao-gomez)
- [Mohamed Abdelkader](https://ahwa.lab.uiowa.edu/people/mohamed-abdelkader)

### Original contributors

The following people are acknowledged for their contributions to the creation of EF5.

- Zac Flamig
- Humberto Vergara
- Race Clark
- JJ Gourley
- Yang Hong
