# -*- coding: utf-8 -*-
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Installer logic for H2GIS Driver.

Handles installation, uninstallation, and verification of the driver.
Uses user-space installation (no admin rights required).
"""

import os
import sys
import shutil
from pathlib import Path
from typing import Optional, Callable, Dict, Any, Tuple

from qgis.core import QgsMessageLog, QgsSettings, Qgis

from .platform_detector import (
    get_platform_info,
    get_user_install_dir,
    get_gdal_driver_path,
    get_artifact_name,
    get_h2gis_lib_subdir,
    is_driver_installed,
)
from .downloader import (
    download_h2gis_library,
    download_gdal_driver,
    DownloadError,
)


class InstallError(Exception):
    """Exception raised when installation fails."""
    pass


class H2GISInstaller:
    """
    Manages installation and configuration of the H2GIS GDAL driver.
    
    Installation is done in user space to avoid requiring admin privileges.
    Uses GDAL_DRIVER_PATH environment variable for driver discovery.
    """
    
    # Settings keys
    SETTINGS_PREFIX = "h2gis_driver/"
    SETTING_INSTALL_DIR = SETTINGS_PREFIX + "install_dir"
    SETTING_DRIVER_PATH = SETTINGS_PREFIX + "driver_path"
    SETTING_H2GIS_LIB_PATH = SETTINGS_PREFIX + "h2gis_lib_path"
    SETTING_INSTALLED_VERSION = SETTINGS_PREFIX + "installed_version"
    
    def __init__(self):
        """Initialize the installer."""
        self.settings = QgsSettings()
        self.platform_info = get_platform_info()
    
    def get_status(self) -> Dict[str, Any]:
        """
        Get current installation status.
        
        Returns:
            Dictionary with status information
        """
        status = {
            'installed': False,
            'driver_available': is_driver_installed(),
            'platform': self.platform_info,
            'install_dir': self.settings.value(self.SETTING_INSTALL_DIR),
            'driver_path': self.settings.value(self.SETTING_DRIVER_PATH),
            'h2gis_lib_path': self.settings.value(self.SETTING_H2GIS_LIB_PATH),
            'version': self.settings.value(self.SETTING_INSTALLED_VERSION),
            'artifact_name': get_artifact_name(self.platform_info),
            'supported': get_artifact_name(self.platform_info) is not None,
        }
        
        # Check if files actually exist
        if status['driver_path'] and Path(status['driver_path']).exists():
            status['installed'] = True
        
        return status
    
    def install(self, 
                driver_path: Optional[Path] = None,
                progress_callback: Optional[Callable[[str, int], None]] = None) -> Tuple[bool, str]:
        """
        Install the H2GIS driver and native library.
        
        Args:
            driver_path: Optional path to pre-downloaded driver binary.
                        If None, will attempt to download.
            progress_callback: Optional callback(message, percent)
            
        Returns:
            Tuple of (success, message)
        """
        try:
            def report(msg: str, pct: int):
                QgsMessageLog.logMessage(msg, "H2GIS", Qgis.Info)
                if progress_callback:
                    progress_callback(msg, pct)
            
            # Get install directory
            install_dir = get_user_install_dir()
            report(f"Install directory: {install_dir}", 5)
            
            # Step 1: Download/copy H2GIS library
            report("Downloading H2GIS native library...", 10)
            try:
                lib_subdir = get_h2gis_lib_subdir(self.platform_info)
                lib_name = self.platform_info['h2gis_lib_name']
                lib_dest = install_dir / "lib"
                
                h2gis_lib_path = download_h2gis_library(
                    lib_dest,
                    lib_subdir,
                    lib_name,
                    progress_callback=lambda d, t: report(f"Downloading H2GIS: {d}/{t} bytes", 20 + int(30 * d / max(t, 1)))
                )
                report(f"H2GIS library installed: {h2gis_lib_path}", 50)
            except DownloadError as e:
                return (False, f"Failed to download H2GIS library: {e}")
            
            # Step 2: Install driver
            driver_dest = install_dir / "gdalplugins"
            driver_name = f"gdal_H2GIS{self.platform_info['driver_extension']}"
            final_driver_path = driver_dest / driver_name
            
            if driver_path and Path(driver_path).exists():
                # Copy provided driver
                report(f"Copying driver from {driver_path}...", 60)
                shutil.copy2(driver_path, final_driver_path)
                report(f"Driver installed: {final_driver_path}", 70)
            else:
                # Try to download
                report("Attempting to download GDAL driver...", 60)
                artifact_name = get_artifact_name(self.platform_info)
                if artifact_name:
                    try:
                        downloaded_path = download_gdal_driver(
                            artifact_name,
                            driver_dest,
                            self.platform_info['driver_extension'],
                        )
                        final_driver_path = downloaded_path
                        report(f"Driver downloaded: {final_driver_path}", 70)
                    except DownloadError as e:
                        # Return instructions for manual download
                        return (False, str(e))
                else:
                    return (False, 
                        f"No pre-built driver available for your configuration:\n"
                        f"OS: {self.platform_info['os']}\n"
                        f"Architecture: {self.platform_info['arch']}\n"
                        f"GDAL: {self.platform_info['gdal_version']}\n\n"
                        f"Please compile from source or request a build."
                    )
            
            # Step 3: Configure environment
            report("Configuring environment...", 80)
            self._configure_environment(install_dir, final_driver_path, h2gis_lib_path)
            
            # Step 4: Save settings
            report("Saving configuration...", 90)
            self.settings.setValue(self.SETTING_INSTALL_DIR, str(install_dir))
            self.settings.setValue(self.SETTING_DRIVER_PATH, str(final_driver_path))
            self.settings.setValue(self.SETTING_H2GIS_LIB_PATH, str(h2gis_lib_path))
            self.settings.setValue(self.SETTING_INSTALLED_VERSION, "1.0.0")
            
            report("Installation complete!", 100)
            
            return (True, 
                f"H2GIS driver installed successfully!\n\n"
                f"Driver: {final_driver_path}\n"
                f"Library: {h2gis_lib_path}\n\n"
                f"Please restart QGIS to activate the driver."
            )
            
        except Exception as e:
            QgsMessageLog.logMessage(f"Installation error: {e}", "H2GIS", Qgis.Critical)
            return (False, f"Installation failed: {e}")
    
    def uninstall(self) -> Tuple[bool, str]:
        """
        Uninstall the H2GIS driver.
        
        Returns:
            Tuple of (success, message)
        """
        try:
            driver_path = self.settings.value(self.SETTING_DRIVER_PATH)
            h2gis_lib_path = self.settings.value(self.SETTING_H2GIS_LIB_PATH)
            install_dir = self.settings.value(self.SETTING_INSTALL_DIR)
            
            removed = []
            
            # Remove driver
            if driver_path and Path(driver_path).exists():
                Path(driver_path).unlink()
                removed.append(f"Driver: {driver_path}")
            
            # Remove H2GIS library
            if h2gis_lib_path and Path(h2gis_lib_path).exists():
                Path(h2gis_lib_path).unlink()
                removed.append(f"Library: {h2gis_lib_path}")
            
            # Clear settings
            self.settings.remove(self.SETTING_INSTALL_DIR)
            self.settings.remove(self.SETTING_DRIVER_PATH)
            self.settings.remove(self.SETTING_H2GIS_LIB_PATH)
            self.settings.remove(self.SETTING_INSTALLED_VERSION)
            
            if removed:
                return (True, 
                    f"H2GIS driver uninstalled.\n\n"
                    f"Removed:\n" + "\n".join(f"  - {r}" for r in removed) +
                    "\n\nPlease restart QGIS."
                )
            else:
                return (True, "No H2GIS driver installation found to remove.")
            
        except Exception as e:
            return (False, f"Uninstall failed: {e}")
    
    def verify(self) -> Tuple[bool, str]:
        """
        Verify the installation is working.
        
        Returns:
            Tuple of (success, message)
        """
        status = self.get_status()
        
        if not status['installed']:
            return (False, "H2GIS driver is not installed.")
        
        if not status['driver_available']:
            return (False, 
                "H2GIS driver is installed but not loaded.\n"
                "Please restart QGIS to activate it."
            )
        
        # Try to use the driver
        try:
            from osgeo import ogr
            driver = ogr.GetDriverByName("H2GIS")
            if driver:
                return (True, 
                    f"H2GIS driver is installed and working!\n\n"
                    f"Driver path: {status['driver_path']}\n"
                    f"H2GIS library: {status['h2gis_lib_path']}\n"
                    f"GDAL version: {status['platform']['gdal_version']}"
                )
            else:
                return (False, "H2GIS driver not found in OGR.")
        except Exception as e:
            return (False, f"Verification failed: {e}")
    
    def _configure_environment(self, install_dir: Path, driver_path: Path, h2gis_lib_path: Path):
        """
        Configure environment variables for the driver.
        
        This creates a startup script that will be run by QGIS.
        """
        # Set environment variables for current session
        gdal_driver_path = get_gdal_driver_path(install_dir)
        os.environ['GDAL_DRIVER_PATH'] = gdal_driver_path
        os.environ['H2GIS_NATIVE_LIB'] = str(h2gis_lib_path)
        
        # Create QGIS startup script for persistence
        startup_script = install_dir / "h2gis_startup.py"
        script_content = f'''# -*- coding: utf-8 -*-
# Auto-generated by H2GIS Driver Installer
# This script is sourced by QGIS to configure H2GIS driver

import os

# Configure GDAL to find the H2GIS driver
gdal_plugins = "{install_dir / 'gdalplugins'}"
existing_path = os.environ.get('GDAL_DRIVER_PATH', '')
sep = ';' if os.name == 'nt' else ':'
if gdal_plugins not in existing_path:
    os.environ['GDAL_DRIVER_PATH'] = gdal_plugins + (sep + existing_path if existing_path else '')

# Configure H2GIS native library path
os.environ['H2GIS_NATIVE_LIB'] = "{h2gis_lib_path}"

# Optional: Add library directory to system path (for Windows DLL loading)
if os.name == 'nt':
    lib_dir = "{install_dir / 'lib'}"
    path = os.environ.get('PATH', '')
    if lib_dir not in path:
        os.environ['PATH'] = lib_dir + ';' + path
'''
        
        with open(startup_script, 'w') as f:
            f.write(script_content)
        
        QgsMessageLog.logMessage(
            f"Created startup script: {startup_script}",
            "H2GIS",
            Qgis.Info
        )
        
        # Note: The user will need to configure QGIS to run this startup script
        # or we can use QgsSettings to store the paths and load them in the plugin
