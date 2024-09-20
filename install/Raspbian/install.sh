#!/bin/bash
apt install -y libavahi-compat-libdnssd-dev
systemctl enable avahi-daemon
systemctl start avahi-daemon
cp ./ShairportQt /usr/bin/
chmod a+x /usr/bin/ShairportQt
cp ./org.shairport.ShairportQt.png /usr/share/icons/hicolor/256x256/apps/
cp ./org.shairport.ShairportQt.desktop /usr/share/applications/

