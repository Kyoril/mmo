![Linux Servers](https://github.com/Kyoril/mmo/workflows/Linux%20Servers/badge.svg)

Login Screen             |  Character Selection Screen
:-------------------------:|:-------------------------:
<img src="https://user-images.githubusercontent.com/9358023/85418557-34ccf280-b571-11ea-866c-7e18aaf989b6.png" width="600">  |  <img src="https://cdn.discordapp.com/attachments/679667054424359054/793784839647395850/unknown.png" width="600">

# What is this?
This repository contains code for servers which can be used to run an MMORPG prototype using Unreal Engine 5. It does **NOT** contain client files, tools or any game assets. These are hosted in separate repositories.

# You mentioned prototype - what does that mean?
Since this is designed to be a protoype, it won't be a massive MMORPG with dozens of races, classes and maps. It will probably contain one race, one or two classes, one or two small maps. The scope may grow over time, if the effort required to so is reasonable.

# Contact
If you want to contact the author of this project, feel free to join the discord server at: https://discord.gg/uNMHCGj

# Supported platforms
There are three supported platforms:

* Windows: This is the main development OS for me, so it is fully supported. Windows 10 is highly recommended although it should also work on Windows 7 and newer.
* Linux: Full support for the servers.
* Mac OS X: Compile and run of the servers should be possible, however I do not support it officially.

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

# How to build using Docker Compose?

TODO: finish

Bootstrapping: `docker-compose --file=docker-compose.init.yml build`

Once bootstrapped: `docker-compose up --build`

Force container recreation: `docker-compose up --force-recreate`

Remove dangling images: `docker image prune --filter="dangling=true"`
