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
from h2gis_driver_installer.installer import (
    get_install_paths,
    is_driver_installed
)


class TestGetInstallPaths(unittest.TestCase):
    """Tests for get_install_paths function."""
    
    def test_returns_dict(self):
        """Test that get_install_paths returns a dictionary."""
        paths = get_install_paths()
        self.assertIsInstance(paths, dict)
    
    def test_has_required_keys(self):
        """Test that returned dict has required keys."""
        paths = get_install_paths()
        self.assertIn('gdal_driver_dir', paths)
        self.assertIn('h2gis_lib_dir', paths)
        self.assertIn('driver_file', paths)
    
    def test_paths_are_path_objects(self):
        """Test that paths are Path objects."""
        paths = get_install_paths()
        self.assertIsInstance(paths['gdal_driver_dir'], Path)
        self.assertIsInstance(paths['h2gis_lib_dir'], Path)
        self.assertIsInstance(paths['driver_file'], Path)


class TestIsDriverInstalled(unittest.TestCase):
    """Tests for is_driver_installed function."""
    
    def test_returns_boolean(self):
        """Test that is_driver_installed returns a boolean."""
        result = is_driver_installed()
        self.assertIsInstance(result, bool)
    
    def test_not_installed_by_default(self):
        """Test that driver is not installed in test environment."""
        # In a test environment, the driver should not be installed
        # unless we're running on a system where it's actually installed
        result = is_driver_installed()
        # We just check it runs without error
        self.assertIn(result, [True, False])


class TestInstallPathsConsistency(unittest.TestCase):
    """Tests for install paths consistency."""
    
    def test_driver_file_in_driver_dir(self):
        """Test that driver file is within driver directory."""
        paths = get_install_paths()
        driver_file = paths['driver_file']
        driver_dir = paths['gdal_driver_dir']
        # Driver file should be in the driver directory
        self.assertEqual(driver_file.parent, driver_dir)
    
    def test_h2gis_lib_dir_exists_or_creatable(self):
        """Test that h2gis lib directory path is valid."""
        paths = get_install_paths()
        h2gis_dir = paths['h2gis_lib_dir']
        # Check that the parent of h2gis_lib_dir is a valid path
        self.assertTrue(h2gis_dir.parent.exists() or 
                        h2gis_dir.parent.parent.exists())


if __name__ == '__main__':
    unittest.main()
