# MeatyArena

Windows x64 C++ application for Escape from Tarkov: Arena DMA data, Fuser rendering, and MAKCU/Ferrum input support.

# Features

* Automatic runtime resolving
* Makcu & Ferrum aim Support
* Aim modes : Movement only / Auto fire with/without movement
* Fireport aim point
* Close to crosshair bone targeting / Or selection
* Custom aim settings
* Team Tracking
* Game Mode detection
* Fuser Only
* **Read Memory Only!**

# Support / Discord

You can find us on discord, for support, feedback and update details
https://discord.gg/GXmBagdP5s

# Supporting our projects

Source code is provided for all the use, adjust and improve. If you feel the need to support our work, then check out our discord channel!
All donations are welcome, and can be made by many methods

## Build requirements

- Windows 10 or 11 x64
- Visual Studio 2022 17.13 or newer
- Desktop development with C++ workload
- Windows 10 or 11 SDK

Visual Studio can read the included `.vsconfig` and offer to install the required workload.

## Building

Open `MeatyArena.slnx` and select **Build > Build Solution**. The solution exposes `Release | x64` by default and uses the Visual Studio 2022 `v143` toolset.

From a Visual Studio Developer PowerShell:

```powershell
msbuild MeatyArena.slnx -m -p:Configuration=Release -p:Platform=x64
```

The output is written to `MeatyArena\bin\Release`. The project copies the required MemProcFS runtime DLLs and Font Awesome asset into the output automatically.

## Binary Files

Latest prebuilt files should only be obtained from our discord, where we provide them. No files are now uploaded to GitHub

## Runtime files

On first use, the application creates its configuration and log folders beside the executable. `mmap.txt` is also generated beside the executable.

