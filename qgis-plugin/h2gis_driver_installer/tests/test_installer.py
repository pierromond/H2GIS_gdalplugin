# -*- coding: utf-8 -*-
"""
Tests for installer module.
"""

import os
import sys
import tempfile
import types
import unittest
from pathlib import Path


# Mock QGIS modules before importing our modules
def setup_mocks():
    """Set up mock QGIS and GDAL modules for testing."""
    
    class MockQgis:
        Info = 0
        Warning = 1
        Critical = 2
        Success = 3
    
    class MockQgsMessageLog:
        messages = []
        
        @staticmethod
        def logMessage(msg, cat, level):
            MockQgsMessageLog.messages.append((msg, cat, level))
    
    class MockQgsApplication:
        @staticmethod
        def qgisSettingsDirPath():
            return tempfile.gettempdir()
        
        @staticmethod
        def instance():
            return None
    
    class MockQgsSettings:
        def __init__(self):
            self._data = {}
        def value(self, key, default=None):
            return self._data.get(key, default)
        def setValue(self, key, val):
            self._data[key] = val
        def remove(self, key):
            self._data.pop(key, None)
    
    # Inject mocks
    sys.modules['qgis'] = types.ModuleType('qgis')
    sys.modules['qgis.core'] = types.ModuleType('qgis.core')
    sys.modules['qgis.core'].Qgis = MockQgis
    sys.modules['qgis.core'].QgsMessageLog = MockQgsMessageLog
    sys.modules['qgis.core'].QgsApplication = MockQgsApplication
    sys.modules['qgis.core'].QgsSettings = MockQgsSettings
    sys.modules['qgis.core'].QgsVectorLayer = None
    sys.modules['qgis.core'].QgsProject = None
    sys.modules['qgis.core'].QgsCoordinateReferenceSystem = None
    
    # Mock osgeo.gdal
    sys.modules['osgeo'] = types.ModuleType('osgeo')
    sys.modules['osgeo.gdal'] = types.ModuleType('osgeo.gdal')
    sys.modules['osgeo.gdal'].VersionInfo = lambda x='VERSION_NUM': '3100000'
    sys.modules['osgeo.gdal'].__version__ = '3.10.0'


# Set up mocks before importing
setup_mocks()

# Now we can import our modules
from h2gis_driver_installer.installer import H2GISInstaller


class TestH2GISInstaller(unittest.TestCase):
    """Tests for H2GISInstaller class."""
    
    def test_can_instantiate(self):
        """Test that H2GISInstaller can be instantiated."""
        installer = H2GISInstaller()
        self.assertIsNotNone(installer)
    
    def test_get_status_returns_dict(self):
        """Test that get_status returns a dictionary."""
        installer = H2GISInstaller()
        status = installer.get_status()
        self.assertIsInstance(status, dict)
    
    def test_status_has_required_keys(self):
        """Test that status dict has required keys."""
        installer = H2GISInstaller()
        status = installer.get_status()
        required_keys = ['installed', 'driver_available', 'platform', 
                         'artifact_name', 'supported']
        for key in required_keys:
            self.assertIn(key, status)
    
    def test_platform_info_in_status(self):
        """Test that platform info is included in status."""
        installer = H2GISInstaller()
        status = installer.get_status()
        self.assertIn('platform', status)
        self.assertIsInstance(status['platform'], dict)
        self.assertIn('os', status['platform'])
        self.assertIn('arch', status['platform'])
    
    def test_not_installed_by_default(self):
        """Test that driver is not installed in test environment."""
        installer = H2GISInstaller()
        status = installer.get_status()
        # In test environment, driver should not be installed
        self.assertFalse(status['installed'])


class TestH2GISInstallerMethods(unittest.TestCase):
    """Tests for H2GISInstaller methods."""
    
    def test_has_install_method(self):
        """Test that installer has install method."""
        installer = H2GISInstaller()
        self.assertTrue(hasattr(installer, 'install'))
        self.assertTrue(callable(installer.install))
    
    def test_has_uninstall_method(self):
        """Test that installer has uninstall method."""
        installer = H2GISInstaller()
        self.assertTrue(hasattr(installer, 'uninstall'))
        self.assertTrue(callable(installer.uninstall))
    
    def test_has_verify_method(self):
        """Test that installer has verify method."""
        installer = H2GISInstaller()
        self.assertTrue(hasattr(installer, 'verify'))
        self.assertTrue(callable(installer.verify))


if __name__ == '__main__':
    unittest.main()
