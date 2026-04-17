## Mire-Plugins
A collection of lightweight LADSPA and LV2 audio plugins for Linux.

## How to build plugins

#01.Prerequisites for building plugins
   
   To compile these plugins, you will need `gcc`, `g++`, `make`, and the development headers for LADSPA and LV2.
   Debian: sudo apt install build-essential ladspa-sdk lv2-dev
   Arch: sudo pacman -S base-devel ladspa lv2
   Fedora: sudo dnf install gcc-c++ make ladspa-devel lv2-devel

02.Building plugins
   
   Build all ladspa and LV2 plugins at once with: make
   Build only ladspa plugins with: make ladspa
   Build only lv2 plugins with: make lv2
  
03.Installing plugins
   
   After building copy the compiled .so files with: make install
   Ladspa plugins will be installed in home directory "/.ladspa" and lv2 plugins in "/.lv2" folder.



