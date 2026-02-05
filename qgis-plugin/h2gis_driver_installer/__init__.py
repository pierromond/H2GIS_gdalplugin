# -*- coding: utf-8 -*-
# SPDX-License-Identifier: GPL-3.0-or-later
"""
H2GIS Driver Installer - QGIS Plugin

This plugin automates the installation of the GDAL H2GIS driver for QGIS.
It downloads the appropriate pre-compiled binaries based on your OS and GDAL version.
"""

def classFactory(iface):
    """
    QGIS Plugin entry point.
    
    :param iface: QgisInterface instance
    :return: Plugin instance
    """
    from .h2gis_plugin import H2GISDriverInstallerPlugin
    return H2GISDriverInstallerPlugin(iface)
