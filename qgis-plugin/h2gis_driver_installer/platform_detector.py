# -*- coding: utf-8 -*-
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Platform detection utilities for H2GIS Driver Installer.

Detects OS, architecture, GDAL version, and provides appropriate paths.
"""

import os
import sys
import platform
from pathlib import Path
from typing import Optional, Dict, Any

from qgis.core import QgsApplication


def get_platform_info() -> Dict[str, Any]:
    """
    Detect current platform information.
    
    Returns:
        Dictionary with keys:
        - os: 'linux', 'windows', 'macos'
        - arch: 'x86_64', 'arm64', etc.
        - gdal_version: '3.8.4', etc.
        - gdal_major_minor: '3.8'
        - qgis_version: '3.34.0', etc.
        - driver_extension: '.so', '.dll', '.dylib'
        - h2gis_lib_name: 'h2gis.so', 'h2gis.dll', 'h2gis.dylib'
    """
    info = {}
    
    # OS detection
    system = platform.system().lower()
    if system == 'linux':
        info['os'] = 'linux'
        info['driver_extension'] = '.so'
        info['h2gis_lib_name'] = 'h2gis.so'
    elif system == 'windows':
        info['os'] = 'windows'
        info['driver_extension'] = '.dll'
        info['h2gis_lib_name'] = 'h2gis.dll'
    elif system == 'darwin':
        info['os'] = 'macos'  # Normalize 'darwin' to 'macos' for clarity
        info['driver_extension'] = '.dylib'
        info['h2gis_lib_name'] = 'h2gis.dylib'
    else:
        info['os'] = system
        info['driver_extension'] = '.so'
        info['h2gis_lib_name'] = 'h2gis.so'
    
    # Architecture
    machine = platform.machine().lower()
    if machine in ('x86_64', 'amd64'):
        info['arch'] = 'x86_64'
    elif machine in ('arm64', 'aarch64'):
        info['arch'] = 'arm64'
    else:
        info['arch'] = machine
    
    # GDAL version
    try:
        from osgeo import gdal
        info['gdal_version'] = gdal.__version__
        parts = gdal.__version__.split('.')
        info['gdal_major_minor'] = f"{parts[0]}.{parts[1]}"
    except ImportError:
        info['gdal_version'] = 'unknown'
        info['gdal_major_minor'] = 'unknown'
    
    # QGIS version
    info['qgis_version'] = QgsApplication.instance().applicationVersion() if QgsApplication.instance() else 'unknown'
    
    return info


def get_user_install_dir() -> Path:
    """
    Get a user-writable directory for installing the driver and libraries.
    
    Returns:
        Path to user install directory (created if doesn't exist)
    """
    # Use QGIS profile directory for user-specific installation
    profile_path = QgsApplication.qgisSettingsDirPath()
    if profile_path:
        install_dir = Path(profile_path) / "h2gis"
    else:
        # Fallback to user home
        if sys.platform == 'win32':
            install_dir = Path(os.environ.get('APPDATA', Path.home())) / "QGIS" / "h2gis"
        elif sys.platform == 'darwin':
            install_dir = Path.home() / "Library" / "Application Support" / "QGIS" / "h2gis"
        else:
            install_dir = Path.home() / ".local" / "share" / "QGIS" / "h2gis"
    
    # Create subdirectories
    (install_dir / "gdalplugins").mkdir(parents=True, exist_ok=True)
    (install_dir / "lib").mkdir(parents=True, exist_ok=True)
    (install_dir / "demo").mkdir(parents=True, exist_ok=True)
    
    return install_dir


def get_gdal_driver_path(install_dir: Path) -> str:
    """
    Get the GDAL_DRIVER_PATH value for user installation.
    
    Args:
        install_dir: User install directory
        
    Returns:
        Path string for GDAL_DRIVER_PATH environment variable
    """
    plugins_dir = install_dir / "gdalplugins"
    
    # Get existing GDAL_DRIVER_PATH if set
    existing = os.environ.get('GDAL_DRIVER_PATH', '')
    
    if existing:
        # Prepend our path
        sep = ';' if sys.platform == 'win32' else ':'
        if str(plugins_dir) not in existing:
            return f"{plugins_dir}{sep}{existing}"
        return existing
    
    return str(plugins_dir)


def get_artifact_name(platform_info: Dict[str, Any]) -> Optional[str]:
    """
    Get the GitHub artifact name for the current platform.
    
    Args:
        platform_info: Dictionary from get_platform_info()
        
    Returns:
        Artifact name or None if not supported
    """
    os_name = platform_info['os']
    arch = platform_info['arch']
    gdal_mm = platform_info['gdal_major_minor']
    
    # Mapping of supported configurations
    # Format: (os, arch, gdal_major_minor) -> artifact_name
    artifact_map = {
        # Linux x86_64
        ('linux', 'x86_64', '3.4'): 'gdal-h2gis-ubuntu22.04-gdal3.4',
        ('linux', 'x86_64', '3.6'): 'gdal-h2gis-ubuntu22.04-gdal3.4',  # Best effort
        ('linux', 'x86_64', '3.8'): 'gdal-h2gis-ubuntu24.04-gdal3.8',
        ('linux', 'x86_64', '3.9'): 'gdal-h2gis-ubuntu24.04-gdal3.8',  # Best effort
        ('linux', 'x86_64', '3.10'): 'gdal-h2gis-ubuntu25.10-gdal3.10',
        ('linux', 'x86_64', '3.11'): 'gdal-h2gis-ubuntu25.10-gdal3.10',  # Best effort
        ('linux', 'x86_64', '3.12'): 'gdal-h2gis-ubuntu25.10-gdal3.10',  # Best effort
        # macOS ARM64 (M1/M2/M3)
        ('macos', 'arm64', '3.10'): 'gdal-h2gis-macos-arm64-gdal3.12',
        ('macos', 'arm64', '3.11'): 'gdal-h2gis-macos-arm64-gdal3.12',
        ('macos', 'arm64', '3.12'): 'gdal-h2gis-macos-arm64-gdal3.12',
        # macOS Intel (x86_64)
        ('macos', 'x86_64', '3.3'): 'gdal-h2gis-macos-intel-x86_64',
        ('macos', 'x86_64', '3.4'): 'gdal-h2gis-macos-intel-x86_64',
        ('macos', 'x86_64', '3.5'): 'gdal-h2gis-macos-intel-x86_64',
        ('macos', 'x86_64', '3.6'): 'gdal-h2gis-macos-intel-x86_64',
        ('macos', 'x86_64', '3.7'): 'gdal-h2gis-macos-intel-x86_64',
        ('macos', 'x86_64', '3.8'): 'gdal-h2gis-macos-intel-x86_64',
        ('macos', 'x86_64', '3.9'): 'gdal-h2gis-macos-intel-x86_64',
        ('macos', 'x86_64', '3.10'): 'gdal-h2gis-macos-intel-x86_64',
        # Windows x64
        ('windows', 'x86_64', '3.8'): 'gdal-h2gis-windows-x64-gdal3.8',
        ('windows', 'x86_64', '3.9'): 'gdal-h2gis-windows-x64-gdal3.8',  # Best effort
    }
    
    key = (os_name, arch, gdal_mm)
    return artifact_map.get(key)


def get_h2gis_lib_subdir(platform_info: Dict[str, Any]) -> str:
    """
    Get the subdirectory name for H2GIS library in the artifact.
    
    Args:
        platform_info: Dictionary from get_platform_info()
        
    Returns:
        Subdirectory name: 'linux', 'windows', or 'macos'
    """
    # OS is already normalized to 'macos' instead of 'darwin'
    return platform_info['os']


def is_driver_installed() -> bool:
    """
    Check if the H2GIS driver is currently available.
    
    Returns:
        True if driver is loaded and accessible
    """
    try:
        from osgeo import ogr
        driver = ogr.GetDriverByName("H2GIS")
        return driver is not None
    except Exception:
        return False


def get_driver_path_from_env() -> Optional[str]:
    """
    Get the path to the installed H2GIS driver from environment.
    
    Returns:
        Path to gdal_H2GIS library or None if not found
    """
    gdal_driver_path = os.environ.get('GDAL_DRIVER_PATH', '')
    if not gdal_driver_path:
        return None
    
    info = get_platform_info()
    driver_name = f"gdal_H2GIS{info['driver_extension']}"
    
    sep = ';' if sys.platform == 'win32' else ':'
    for path_dir in gdal_driver_path.split(sep):
        driver_path = Path(path_dir) / driver_name
        if driver_path.exists():
            return str(driver_path)
    
    return None
