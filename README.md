![Linux Servers](https://github.com/Kyoril/mmo/workflows/Linux%20Servers/badge.svg)

# What is this project about?
This project will grow to a fully fledged MMORPG prototype. This means, that it will contain all tools required to build an MMORPG from scratch.

# You mentioned prototype - what does that mean?
Since this is designed to be a protoype, it won't be a massive MMORPG with dozens of races, classes and maps. It will probably contain one race, one or two classes, one or two small maps. The scope may grow over time, if the effort required to so is reasonable.

# Contact
If you want to contact the author of this project, feel free to join the discord server at: https://discord.gg/uNMHCGj

# Supported platforms
There are three supported platforms:

* Windows: Everything is supported, Windows 10 is highly recommended although it should also work on Windows 7
* Linux: No support for client, game editor and launcher due to missing gui and graphics api implementation
* Mac OS X: No support for client and game editor due to missing graphics api implementation

While support for the Mac OS X game client may be available in the future, I don't plan anything for Linux at all, as I mainly use Linux for running the game servers and I don't run a desktop linux distribution personally to test and support it, nor do I have the time to do so right now.

# What do I need to build?
In order to build this project, you need to meet certain requirements:

* A modern 64 bit compatible c++ compiler like G++ 8 or newer, or Visual C++ 2017 or newer.
* A 64 bit operating system, either Windows or Linux are tested right now
* A modern cmake client (Version 3.12 or newer recommended)
* An installation of mysql developer libraries
* An installation of openssl 1.1.1d or newer (older versions aren't tested but might work eventually)
* A git client

# How to build on Windows
In order to build this project on windows, you need to do the following steps:

* Clone the repository into an empty directory
* Run: git submodule update --init
* Create a new directory called "win" in the cloned directory (this directory is ignored by git, don't worry)
* Run cmake, use the cloned directory as source and the "win" directory as build directory
* Choose Visual Studio 15 2017 (or newer) as compiler, and make sure that you use the x64 (Win64) platform
* Click Configure and Generate the solution
* Open the solution and build it in the Debug or Release build configuration
* Your binaries will be generated in a directory called "bin" inside your cloned directory (ignored by git as well, don't worry)

# How to build on Linux?
Building on linux is similar to building on windows, except that you won't use an IDE here (you can, if you want, but I don't support it. I mainly use linux for server builds.

* Clone the repository
* git submodule update --init
* mkdir build
* cd build
* cmake ../
* make -jX    (where X is the number of cpu cores to use when building to speed things up)
