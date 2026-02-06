# -*- coding: utf-8 -*-
# SPDX-License-Identifier: GPL-3.0-or-later
"""
H2GIS Driver Installer - Main Plugin Class
"""

from qgis.PyQt.QtCore import QCoreApplication, QSettings
from qgis.PyQt.QtWidgets import QAction, QMessageBox
from qgis.PyQt.QtGui import QIcon
from qgis.core import QgsMessageLog, Qgis, QgsSettings

import os
from pathlib import Path


class H2GISDriverInstallerPlugin:
    """Main QGIS Plugin class for H2GIS Driver Installer."""

    def __init__(self, iface):
        """
        Constructor.
        
        :param iface: QgisInterface instance
        """
        self.iface = iface
        self.plugin_dir = os.path.dirname(__file__)
        self.actions = []
        self.menu = self.tr("&H2GIS Driver")
        self.toolbar = None
        self.dialog = None
        
        # Try to load driver at plugin initialization (before GUI)
        self._load_driver_if_installed()

    def tr(self, message):
        """Get the translation for a string."""
        return QCoreApplication.translate("H2GISDriverInstaller", message)

    def initGui(self):
        """Initialize the plugin GUI - called by QGIS when plugin is loaded."""
        # Create action for the plugin
        icon_path = os.path.join(self.plugin_dir, "resources", "icon.png")
        if not os.path.exists(icon_path):
            icon_path = ""
        
        self.action_install = QAction(
            QIcon(icon_path),
            self.tr("Install H2GIS Driver..."),
            self.iface.mainWindow()
        )
        self.action_install.triggered.connect(self.show_installer_dialog)
        self.action_install.setWhatsThis(
            self.tr("Install the GDAL H2GIS driver for reading .mv.db files")
        )
        
        # Add to menu
        self.iface.addPluginToDatabaseMenu(self.menu, self.action_install)
        self.actions.append(self.action_install)
        
        # Check if driver is already installed and log status
        self._check_driver_status()

    def unload(self):
        """Remove the plugin menu items - called by QGIS when plugin is unloaded."""
        for action in self.actions:
            self.iface.removePluginDatabaseMenu(self.menu, action)
        self.actions.clear()

    def show_installer_dialog(self):
        """Show the installer dialog."""
        from .installer_dialog import H2GISInstallerDialog
        
        if self.dialog is None:
            self.dialog = H2GISInstallerDialog(self.iface.mainWindow())
        
        self.dialog.show()
        self.dialog.raise_()
        self.dialog.activateWindow()

    def _load_driver_if_installed(self):
        """
        Load the H2GIS driver if it was previously installed.
        
        This runs the startup script created during installation to set up
        environment variables before GDAL is fully initialized.
        """
        settings = QgsSettings()
        install_dir = settings.value("h2gis_driver/install_dir")
        
        if not install_dir:
            return  # Not installed
        
        install_path = Path(install_dir)
        startup_script = install_path / "h2gis_startup.py"
        
        if startup_script.exists():
            try:
                # Execute the startup script to set environment variables
                with open(startup_script, 'r') as f:
                    exec(compile(f.read(), str(startup_script), 'exec'))
                QgsMessageLog.logMessage(
                    f"Loaded H2GIS configuration from {startup_script}",
                    "H2GIS",
                    Qgis.Info
                )
            except Exception as e:
                QgsMessageLog.logMessage(
                    f"Failed to load H2GIS startup script: {e}",
                    "H2GIS",
                    Qgis.Warning
                )

    def _check_driver_status(self):
        """Check if H2GIS driver is already available and log status."""
        try:
            from osgeo import ogr
            driver = ogr.GetDriverByName("H2GIS")
            if driver is not None:
                QgsMessageLog.logMessage(
                    "H2GIS driver is installed and available",
                    "H2GIS",
                    Qgis.Info
                )
            else:
                QgsMessageLog.logMessage(
                    "H2GIS driver not found. Use Database > H2GIS Driver > Install to set it up.",
                    "H2GIS",
                    Qgis.Warning
                )
        except Exception as e:
            QgsMessageLog.logMessage(
                f"Error checking H2GIS driver status: {e}",
                "H2GIS",
                Qgis.Warning
            )
