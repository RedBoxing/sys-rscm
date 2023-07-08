# sys-rscm : Red's Stupid Cheat Module
Inspired by [sys-botbase](https://github.com/olliz0r/sys-botbase/), [sys-netcheat](https://github.com/jakibaki/sys-netcheat/) and [Noexes](https://github.com/mdbell/Noexes)

## What is this?
This is a cheat module for the Nintendo Switch. It allows you to read and write to the memory of any process running on the Switch. It is intended to be used with [rscm-rs](https://github.com/RedBoxing/rscm-rs) 

## Features
- [x] Attach to any process running on the Switch
- [x] Read and write to the memory of the process
- [x] Query the memory of the process
- [x] Pause and resume the process
- [-] Set a breakpoint on the memory of the process 
- [x] Get the list of the processes running on the Switch
- [x] Get the current Process ID
- [x] Get the title ID of any process

## How do I install it?
- Download the latest release from the [releases page](https://github.com/RedBoxing/sys-rscm/releases/latest)
- Extract the content of the zip file to the root of your SD card
- Reboot your Switch

## FAQ
### How do I use it?
- Install the sysmodule
- Download [rscm-rs](https://github.com/RedBoxing/rscm-rs) or a client capable of using the sysmodule
- Connect your client on the port 6060 of your nintendo switch
- Enjoy !