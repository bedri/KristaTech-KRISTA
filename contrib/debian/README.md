
Debian
====================
This directory contains files used to package kristatechd/kristatech-qt
for Debian-based Linux systems. If you compile kristatechd/kristatech-qt yourself, there are some useful files here.

## kristatech: URI support ##


kristatech-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install kristatech-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your kristatech-qt binary to `/usr/bin`
and the `../../share/pixmaps/kristatech128.png` to `/usr/share/pixmaps`

kristatech-qt.protocol (KDE)

