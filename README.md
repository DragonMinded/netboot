# Tools for working with a net dimm

This repository started when I looked at triforcetools.py and realized that it was ancient (Python2) and contained a lot of dead code. So, as one of the first projects I did when setting up my NNC to netboot was to port triforcetools.py to Python3, clean up dead code and add type hints. I also added percentage display for transfer and `--help` to the command-line. The current code is capable of sending data to a net dimm with on-the-fly patches, managing a net dimm to ensure that it always gets a particular game every boot, reading information from the net dimm cartridge, editing EEPROM dumps and attaching both SRAM and EEPROM dumps to Naomi ROMs when sending images to net boot. This has all been tested on a Naomi with a net dimm, but has not been verified on Triforce/Chihiro. There is no reason why the non-EEPROM code should not work on both Triforce and Chihiro, however, as much of the information was verified by reverse-engineering both the net dimm 3.17 firmware and the SEGA transfergame.exe utility.

## Setup Requirements

This requires at least Python 3.6, and a few packages installed. To install the required packages, run the following. You may need to preface with sudo if you are installing into system Python. However, I recommend setting up a virtualenv instead of polluting your system environment.

```
python3 -m pip install -r requirements.txt
```

Remember to append a `--upgrade` to the above command if you are refreshing packages!

## Script Invocation

For all of these scripts, they should run out of the box on Linux and Mac once you've run the above installation command. However, on Windows you will have to prefix all commands with `python3`. For example, instead of running `./netdimm_send --help` you would instead run `python3 ./netdimm_send --help`. All scripts will function identically thereafter, regardless of the operating system, as long as you have Python 3.6 or greater and the correct dependencies installed.

### netdimm_info

This script requests firmware information from a single cabinet, displaying it on the screen. Included in that info is the net dimm firmware revision, the memory capacity of the net dimm, the available memory that can be used for game storage, the current game's CRC image, whether the net dimm thinks that CRC is valid or not and whether the CRC check screen has been disabled for this game or not. Invoke the script like so to see options:

```
./netdimm_info --help
```

Assuming your net dimm is at 192.168.1.1, the following will print information about the firmware running on the cabinet:

```
./netdimm_info 192.168.1.1
```

### netdimm_send

This script handles sending a single binary to a single cabinet, assuming it is on and ready for connections. For most setups, this means that the cabinet is sitting on the "CHECKING NETWORK" screen. In some cases, you can also send a game to an already-running net dimm and it will reboot the currently running game and receive a new one. This does not work for all games, however, as the reboot hook that the net dimm installs does not work for every game. Invoke the script like so to see options:

```
./netdimm_send --help
```

You can invoke it identically to the original triforcetools.py as well. Assuming your net dimm is at 192.168.1.1, the following will load the ROM named `my_favorite_game.bin` from the current directory:

```
./netdimm_send 192.168.1.1 my_favorite_game.bin
```

As well as sending a single game to a Net Dimm, this script can optionally handle applying patches. See `--help` for more details. It can also handle sending settings along with a game for the Naomi target. Again, see `--help` for more details. This can be a valid EEPROM or SRAM file as obtained from an emulator. Also see `edit_settings` for how to generate settings files that can be sent along with a game to a Naomi system. It can also disable the CRC check screen in order to boot the ROM you are sending faster. See `--help` for more information.

### netdimm_ensure

This script will monitor a cabinet, and send a single binary to that cabinet whenever it powers on. It will run indefinitely, waiting for the cabinet to power on before sending, and then waiting again for the cabinet to be powered off and back on again before sending again. If the cabinet is already running a game when it is run and that game matches what it was attempting to send, it will not reboot the cabinet but instead wait for the cabinet to be powered off and back on to send once more. If the cabinet is already running a game and that game does not match what it is attempting to send, it will reboot the cabinet and send the new game. If the battery in your net dimm is functional and your cabinet boots into the game that it was attempting to send, it will not resend the game but instead wait for the CRC to fail before resending. Invoke the script like so to see options:

```
./netdimm_ensure --help
```

It works identically to netdimm_send, except for it only supports a zero PIC and it tries its best to always ensure the cabinet has the right game. Run it the same way you would run netdimm_send. Just like `netdimm_send`, this script can optionally handle applying patches. See `--help` for more details. It can also handle sending settings along with a game for
the Naomi target. Again, see `--help` for more details. This can be a valid EEPROM or SRAM file as obtained from an emulator. Also see `edit_settings` for how to generate settings files that can be sent along with a game.

### netdimm_receive

This script handles querying the current running game's size and then downloading it to a file. It is fairly useless as it won't dump anything other than what was previously sent to the net dimm by `netdimm_send` or `netdimm_ensure`. However, it is included for documentation purposes. Invoke the script like so to see options:

```
./netdimm_receive --help
```

### netdimm_menu

This script creates a simple menu ROM given a directory containing Naomi ROM files, uploads it to a Naomi and then waits for a user selection before rebooting the cabinet and sending the selected ROM to the net dimm. Use this instead of a Raspberry Pi with an LCD screen to select a game on boot up. Note that it only works with Naomi at the moment! Invoke the script like so to see options:

```
./netdimm_menu --help
```

### netdimm_peekpoke

This script connects to a net dimm and requests it to peek at system RAM or poke a value into system RAM at a specified address. This is mostly provided as a curiosity as there are not many uses for such a utility. Invoke the script like so to see options:

```
./netdimm_peekpoke --help
```

### binary_patch

This script can either diff two same-length binaries and produce a patch similar to the files found in `patches/` or it can take a binary and one or more patch files and produce a new binary with the patches applied. Note that this is just a frontend to the same utility that lives in <https://github.com/DragonMinded/arcadeutils> and as such all documentation there applies here as well. The patches that this produces in diff mode can also be applied on-the-fly when using `netdimm_send` or `netdimm_ensure` by using the `--patch` argument to either tool. See either of the tools above for more information. Invoke the script like so to see options:

```
./binary_patch --help
```

To diff two binary files named `file1.bin` and `file2.bin`, outputting their differences to a file named `differences.binpatch`, run like so:

```
./binary_patch diff file1.bin file2.bin --patch-file differences.binpatch
```

To apply a patch to a binary file named `file.bin` with patches found in `differences.binpatch`, outputting it to a new file named `newfile.bin`, run like so:

```
./binary_patch patch file.bin newfile.bin --patch-file differences.binpatch
```

### rominfo

This script will read a ROM and output information found in the header. Currently it only supports Naomi ROMs but it can be extended to Triforce and Chihiro ROMs as well if somebody wants to put in the effort. Invoke the script like so to see options:

```
./rominfo --help
```

To output information about a particular binary, run like so:

```
./rominfo somefile.bin
```

### eeprominfo

This script will read an EEPROM and output the serial number as well as the contents of the game section as hex digits. Currently it only supports Naomi EEPROMs. Optionally it can load a settings definition file for the EEPROM out of the `settings/definitions/` directory and print the current settings contained in the EEPROM. This is most useful when reverse-engineering the settings format for a particular game. Invoke the script like so to see options:

```
./eeprominfo --help
```

To output information about an EEPROM file as extracted from demul's `nvram` folder, run like so:

```
./eeprominfo dummy.eeprom
```

### attach_sram

In 'attach' mode, this script will take an SRAM file dumped from a Naomi using `naomi_sram` or obtained from an emulator such as demul and attach it to an Atomiswave conversion game so that your Naomi initializes the SRAM with its contents. If an Atomiswave conversion ROM already has an SRAM initialization section, it will overwrite it with the new SRAM. Otherwise, it enlarges the ROM to make room for the init section. Use this to set up defaults for a game using the test menu in an emulator and apply those settings to your game for netbooting with chosen defaults.

In 'extract' mode, this script will extract a previously-attached SRAM from a Naomi game so that you can load it in an emulator such as demul. If the ROM does not have an attached SRAM section, it will not extract anything.

Invoke the script like so to see options:

```
./attach_sram --help
```

To attach a SRAM file from demul to a ROM named demo.bin, run like so:

```
./attach_sram attach demo.bin dummy.sram
```

### attach_settings

In 'attach' mode, this script will take an EEPROM file from an emulator such as demul and attach it to Naomi game so that your Naomi initializes the EEPROM with its contents. If a Naomi ROM already has an EEPROM initialization section, it will overwrite it with the new EEPROM. Otherwise, it enlarges the ROM to make room for the init section. Use this to set up defaults for a game using the test menu in an emulator and apply those settings to your game for netbooting with chosen defaults.

In 'extract' mode, this script will extract a previously-attached EEPROM from a Naomi game so that you can load it in an emulator such as demul. If the ROM does not have an attached EEPROM settings file, it will not extract anything.

In 'info' mode, this script will print information about any attached EEPROM settings on an existing ROM, displaying what version of the settings trojan was used, whether some options were selected and the full list of settings chosen for the game.

In 'edit' mode, this script will extract an existing EEPROM settings file from a ROM, let you edit those settings, and then save the settings back to the ROM. If the Naomi ROM does not have any EEPROM settings already attached, it will create them using defaults stored in the definition files before letting you edit those defaults and then save them.

Note that some Naomi games have protection code that prevents successful modification of the header. In these cases, attempting to apply settings to a ROM will result in the game freezing randomly. Usually this can be seen in the attract sequence. To circumvent this and apply settings successfully, check the `patches/` folder to see if a `noprotect` patch for that game exists and apply it first before attempting to attach or edit settings for a ROM.

Invoke the script like so to see options:

```
./attach_settings --help
```

To attach an EEPROM file from demul to a ROM named demo.bin, run like so:

```
./attach_settings attach demo.bin dummy.eeprom
```

To edit the settings you just attached, run like so:

```
./attach_settings edit demo.bin
```

### edit_settings

This script spawns a command-line EEPROM file editor. Use this to create a new EEPROM file from scratch or edit an EEPROM file that you've previously made using an emulator. Note that editing settings for an arbitrary game requires that the game have a settings definition file in `settings/definitions/`. Settings definitions are looked up using the game's serial number which is found both in the rom header for a game (use `rominfo` to view this) and in a previously-created EEPROM file (use `eeprominfo` to view this). The resulting edited EEPROM can be attached to a Naomi ROM using `attach_settings` so that the game will load those settings when it boots up. It can also be patched on-the-fly when sending a Naomi ROM using the `--patch` argument in either `netdimm_send` or `netdimm_ensure`. Invoke the script like so to see options:


```
./edit_settings --help
```

### naomi_sram

This script can run in either 'dump' or 'restore' mode. In dump mode, it takes the IP of a net dimm running on a Naomi system and a filename, and dumps the current contents of the SRAM on the Naomi into the file specified. This can then be later attached to an Atomiswave conversion ROM using the `--settings-file` option on either `netdimm_send` or `netdimm_ensure` or placed into an SRAM folder for the web frontend to attach to a game on-the-fly when booting. Use this to set up preferred settings and then dump the SRAM, or to back up high scores that you wish to preserve when net booting into another game. In restore mode, it takes the IP of a net dimm and a filename, and attempts to load an SRAM from the filename and restore it back to the Naomi. This option is somewhat redundant given so many other tools can attach an SRAM to an image or send an SRAM on the fly, but the option is available if necessary. Invoke the script like so to see options:

```
./naomi_sram dump --help
```

or

```
./naomi_sram restore --help
```

### Free-Play/No Attract Patch Generators

Both `make_freeplay_patch` and `make_no_attract_patch` can be invoked in the same manner and will produce a patch that applies either forced free-play or forced silent attract mode. Note that these patches are considered obsolete as you can customize all system settings using `attach_settings` or `edit_settings` as documented above. Still, they are provided for posterity assuming you wish to use another netboot package and just want to patch your ROMs to be in free-play. You can run them like so:

```
./make_no_attract_patch game.bin --patch-file game_no_attract.binpatch
./make_freeplay_patch game.bin --patch-file game_freeplay.binpatch
```

You can also see options available for running by running it with the `--help` option:

```
./make_no_attract_patch --help
./make_freeplay_patch --help
```

### Default Game Settings Patch Generator

The `patch_default_settings` script attempts to take an EEPROM file that was obtained from an emulator or created using `edit_settings` and apply the game section to a ROM such that when it initializes EEPROM it does so with your chosen settings instead of its original defaults. Note that this requires a settings definition file for the game in order to work properly and is not guaranteed to work. It operates under the principle that many games have the entire default game EEPROM compiled into their code somewhere and changing that changes what they write to the EEPROM when creating new settings. It uses the defaults specified in the settings definition file to figure out what EEPROM contents to search for and if it finds a chunk of data matching that contents it will patch the ROM to have the settings found in the specified EEPROM file instead. If you wish to generate a patch instead of patching the ROM itself, you can run with the `--patch-file` argument. Invoke the script like so to see options:

```
./patch_default_settings --help
```

## Web Interface

Along with scripts, several libraries and a series of patches, this repository also provides a simple web interface that can manage multiple cabinets. This is meant to be run on a server on your local network and will attempt to boot your net dimm-capable cabinet to the last game selected on every boot. It also allows you to easily select new games using a drop-down as well as configure settings for the Naomi platform and general patches to apply when sending a game to any platform. Use this if you want to treat your netboot setup much like a cabinet with a legitimate cartridge in it, but still allow yourself and friends to select a new game easily.

To try it out with the test server run the following:

```
./host_debug_server --config config.yaml
```

Like the other scripts, you can run this command with the `--help` flag to see additional options. By default, the config will look for ROMs in the `roms/` directory, patches in the `patches/` directory, SRAM files in the `srams/` directory and Naomi settings definitions files in the `naomi/settings/definitions` directory. It will listen on port 80. You do not want to run the above script to serve traffic on a production setup as it is single-threaded and will dump its caches if you change any files. By default, the server interface will hide advanced options, such as cabinet configuration. To show the options, either type the word 'config' on the home page, or go to the `/config` page. Both of these will un-hide the configuration options until you choose again to hide them.

### Production Setup

Since this uses a series of threads to manage cabinets in the background, it can be somewhat difficult to install in a normal nginx/uWSGI setup. A wsgi file is provided in the `scripts/` directory that is meant to be used alongside a virtualenv with this repository installed in it. Config files should be copied to the same directory, and don't forget to update the ROMs/patches/settings definition directories in your config to point at their actual locations. If you do something similar to my setup below, don't forget to set up a virtualenv in the venv directory!

Since the default configuration for this repository is to be used as a package in other code, setup.py will default to only installing the parts of the package that matter for that. If you want to use pip to update your virtualenv when running the full web interface you should run `FULL_INSTALLATION=1 pip install . --upgrade` to tell setup.py to install the web server packages as well. Make sure you do this with your virtualenv activated to avoid polluting your system packages!

My uWSGI config looks something similar to the following:

```
[uwsgi]
plugins = python3
socket = /path/to/wsgi/directory/netboot.sock
processes = 1
threads = 2
master = false
enable-threads = true

virtualenv = /path/to/wsgi/directory/venv
chdir = /path/to/wsgi/directory/
wsgi-file = server.wsgi
callable = app
```

My nginx config looks something like the following:

```
server {
    server_name your.domain.here.com;
    listen 443 ssl;
    server_tokens off;

    gzip on;
    gzip_types text/html text/css text/plain application/javascript application/xml application/json;
    gzip_min_length 1000;

    ssl_certificate /path/to/letsencrypt/fullchain.pem;
    ssl_certificate_key /path/to/letsencrypt/privkey.pem;

    location / {
        include uwsgi_params;
        uwsgi_pass unix:/path/to/wsgi/directory/netboot.sock;
    }

    location ^~ /static/ {
        include  /etc/nginx/mime.types;
        root /path/to/wsgi/directory/venv/lib/python3.6/site-packages/netboot/web/;
    }
}
```

With the above scripts, you should be able to visit `https://your.domain.here.com/`. The running uWSGI server will also manage your cabinets for you so there is no need to also use `netdimm_ensure` or any other scripts.

## Developing

The tools here are fully typed, and should be kept that way. To verify type hints, run the following:

```
mypy .
```

The tools are also lint clean (save for line length lints which are useless drivel). To verify lint, run the following:

```
flake8 .
```

There are aso tests for various pieces of the code, and if you are adding features it is advised to also add tests for them. To verify tests, run the following:

```
python3 -m unittest discover
```

## Including This Package

By design, much of this code can be used as a library by other python code, and as it is Public Domain, it can be included wherever. I would prefer that you attribute me when possible but it is not necessary. The pieces of this repo which are appropriate for external consumption have been packaged into the PyPI projects "netdimmutils" and "naomiutils". Alternatively, you can check out this repo and then run `pip install .` in the root of the checkout. The "netdimm", "naomi" and "naomi.settings" packages will be installed for you. Alternatively if you place the line `git+https://github.com/DragonMinded/netboot.git@trunk#egg=netboot` in your requirements file, then when you run `pip install -r requirements.txt` on your own code, the latest version of these packages will be installed for you. Note that by default, the webserver components are NOT included in this package. However, the "homebrew/settingstrojan/settingstrojan.bin" file is included along with "netdimm", "naomi" and "naomi.settings" as a compiled version of this file needs to exist for some of the code to work. The "naomi/settings/definitions/" directory and settings files are also included as part of the "naomiutils" package so that you don't have to provide your own copy of the settings definitions included in this repo.
