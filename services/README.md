# Raspberry Pi Services

This directory contains various services that are available for installing on a Raspberry Pi in order to turn it into a net dimm capable device. Once setup, the Raspberry Pi will boot up and then perform the stated action forever until its power is lost. These scripts assume that you are using your boot partition to store the ROM image(s) and configuration. No documentation on how to resize the boot partition to ensure it has enough space is provided, but you can search online for various tutorials to help you do so.

Do note that the following services are designed for Naomi systems ONLY! The kiosk mode should work for any Naomi/Triforce/Chihiro setup you have, but it is untested for everything but Naomi. For menu mode, only code for the Naomi has been written so it will not work at all for other systems!

## Initial Setup

All of these scripts assume you are using a modern version of the Raspberry Pi OS installed on a microSD card that's inserted into your RPI. To install the correct version of the OS, you can use the Raspberry Pi installer at <https://www.raspberrypi.com/software/>. Under the OS selection menu, choose "Raspberry Pi OS (Other)" and then choose "Raspberry Pi OS (Lite)" to install the OS without a desktop which you will not need. Once you have installed the OS on your SD card, inserted it into the RPI, booted up and logged in you will want to run the following commands to prime the RPI to run this softare:

```
sudo apt update
sudo apt upgrade
sudo apt install python3 python3-pip git
cd ~
git clone https://github.com/DragonMinded/netboot.git
cd ~/netboot
sudo python3 -m pip install -r requirements.txt
```

After all of these commands have run, you should have the contents of this repository on your RPI and they should be ready to go. If you try the following command it should print the help and give you no errors about missing packages:

```
cd ~/netboot
./netdimm_send --help
```

## Setting up Static IP

In order for the RPI to directly talk to a net dimm with no router, switch or anything inbetween them, it must be set up with a static IP. This is because there is nothing to give it an IP address, and if it does not have one it won't even try to talk to the net dimm. The following instructions assume that you have a net dimm with IP "192.168.1.2" which is the standard IP address for net dimms sold as kits and using piforcetools. Run the following commands to set your RPI up for fixed IP:

```
cd ~/netboot/services
sudo ./setup_fixed_ip.sh
sudo reboot now
```

If everything worked right, your RPI should reboot. When you log back in, type the following command:

```
ifconfig
```

If you can see `inet 192.168.1.1  netmask 255.255.255.0  broadcast 192.168.1.255` under the `eth0` interface, then you successfully set up fixed IP!

## Kiosk Mode

If you want to always send the same game to your net dimm every single boot, no matter what, you should use kiosk mode. In this mode, a script called `netdimm_ensure` attempts to make the net dimm act like it has a GD-ROM or CF card inserted, always booting the same game no matter what, every time the target net dimm is turned on. For this to work, you will need a file called "autorun.bin" in your `/boot` partition. From the perspective of the RPI it will be `/boot/autorun.bin`. From the perspective of a Windows/OSX/Linux computer with an SD card reader, it will be "autorun.bin" on the root of the first (or only) partition the OS can see. Copy whatever game you want to run every boot to the boot partition and rename the file to `autorun.bin`. Now, run the following command to install the service:

```
sudo cp kiosk.service /etc/systemd/system/
sudo systemctl enable kiosk
```

Now, you can shut down the RPI, plug it into the net dimm and switch on the RPI and the target system at once. If all was setup properly you should see the game loading onto the net dimm and then booting when you are finished! It is recommended at this point to put your RPI root filesystem into read-only mode. Failure to do so can cause filesystem corruption over time as you turn the power off unexpectedly without shutting down. You can do so by running `sudo raspi-config` and activating "OverlayFS" in the menu. Be sure to do so after you've tested everything as this makes changes difficult!

## Menu Mode

If you want to see a menu on your arcade cabinet full of all of the ROMs you have on your RPI every time you cycle power, you should use menu mode. In this mode, a script called `netdimm_menu` attempts to send a menu full of your games to the target net dimm every time it is powered on. You can then select the game you want from the menu and press start to boot it. The game will be sent to the net dimm and you can play it for as long as you want. Then, when you want a new game, you can cycle power and the menu will again be sent to the cabinet. The menu allows you to set up options and patches per-game as a bonus feature. For this to work, you will need a dirctory called "roms" in your `/boot` partition.  From the perspective of the RPI it will be `/boot/roms/`. From the perspective of a Windows/OSX/Linux computer with an SD card reader, it will be "roms" on the root of the first (or only) partition the OS can see. Copy whatever games you want to appear on the menu into this directory. Now, run the following command to install the service:

```
sudo mkdir /boot/roms
sudo cp menu.service /etc/systemd/system/
sudo systemctl enable menu
```

Now, you can shut down the RPI, plug it into the net dimm and swithc on the RPI and the menu should appear. You can hit the test button on the main game list to configure the menu, and you can hold start on any game for 1+ second to enter the game configuration screen. It is recommended at this point to put your RPI root filesystem into read-only mode. Failure to do so can cause filesystem corruption over time as you turn the power off unexpectedly without shutting down. You can do so by running `sudo raspi-config` and activating "OverlayFS" in the menu. Be sure to do so after you've tested everything as this makes changes difficult!
