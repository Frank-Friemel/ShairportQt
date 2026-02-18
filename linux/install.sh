#!/bin/bash
echo "Installing ShairportQt..."
systemctl enable avahi-daemon
systemctl start avahi-daemon
cp ./ShairportQt /usr/bin/
chmod a+x /usr/bin/ShairportQt
cp org.shairport.ShairportQt.png /usr/share/icons/hicolor/256x256/apps/
cp org.shairport.ShairportQt.desktop /usr/share/applications/

# Refresh desktop / icon caches if the tools are available
if command -v update-desktop-database &>/dev/null; then
    update-desktop-database "${INSTALL_APPLICATIONS}"
fi
if command -v gtk-update-icon-cache &>/dev/null; then
    gtk-update-icon-cache -f -t "${INSTALL_ICONS%/256x256/apps}"/../.. 2>/dev/null || true
fi

echo "ShairportQt installed successfully."
echo "You can now launch it from your application menu or by running: ShairportQt"
