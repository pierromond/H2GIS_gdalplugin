# -*- coding: utf-8 -*-
"""
Tests for platform detection module.
"""

import sys
import types
import unittest


# Mock QGIS modules before importing our modules
def setup_mocks():
    """Set up mock QGIS and GDAL modules for testing."""
    
    class MockQgis:
        Info = 0
        Warning = 1
        Critical = 2
        Success = 3
    
    class MockQgsMessageLog:
        @staticmethod
        def logMessage(msg, cat, level):
            pass
    
    class MockQgsApplication:
        @staticmethod
        def qgisSettingsDirPath():
            return "/tmp/qgis_test"
        
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
from h2gis_driver_installer.platform_detector import (
    get_platform_info,
    get_artifact_name,
    get_h2gis_lib_subdir
)


class TestPlatformDetection(unittest.TestCase):
    """Tests for platform detection."""
    
    def test_get_platform_info_returns_dict(self):
        """Test that get_platform_info returns a dictionary."""
        info = get_platform_info()
        self.assertIsInstance(info, dict)
    
    def test_get_platform_info_has_required_keys(self):
        """Test that get_platform_info returns all required keys."""
        info = get_platform_info()
        required_keys = ['os', 'arch', 'gdal_version', 'gdal_major_minor',
                         'driver_extension', 'h2gis_lib_name']
        for key in required_keys:
            self.assertIn(key, info)
    
    def test_os_is_valid(self):
        """Test that OS is one of the expected values."""
        info = get_platform_info()
        self.assertIn(info['os'], ['linux', 'windows', 'macos'])
    
    def test_arch_is_valid(self):
        """Test that architecture is one of the expected values."""
        info = get_platform_info()
        self.assertIn(info['arch'], ['x86_64', 'arm64', 'aarch64', 'amd64'])
    
    def test_driver_extension_matches_os(self):
        """Test that driver extension matches the OS."""
        info = get_platform_info()
        if info['os'] == 'linux':
            self.assertEqual(info['driver_extension'], '.so')
        elif info['os'] == 'windows':
            self.assertEqual(info['driver_extension'], '.dll')
        elif info['os'] == 'macos':
            self.assertEqual(info['driver_extension'], '.dylib')


class TestArtifactMapping(unittest.TestCase):
    """Tests for artifact name mapping."""
    
    def test_linux_x86_64_gdal310(self):
        """Test artifact name for Linux x86_64 GDAL 3.10."""
        info = {
            'os': 'linux',
            'arch': 'x86_64',
            'gdal_major_minor': '3.10'
        }
        artifact = get_artifact_name(info)
        self.assertEqual(artifact, 'gdal-h2gis-ubuntu25.10-gdal3.10')
    
    def test_macos_arm64_gdal312(self):
        """Test artifact name for macOS ARM64 GDAL 3.12."""
        info = {
            'os': 'macos',
            'arch': 'arm64',
            'gdal_major_minor': '3.12'
        }
        artifact = get_artifact_name(info)
        self.assertEqual(artifact, 'gdal-h2gis-macos-arm64-gdal3.12')
    
    def test_macos_intel_gdal33(self):
        """Test artifact name for macOS Intel GDAL 3.3."""
        info = {
            'os': 'macos',
            'arch': 'x86_64',
            'gdal_major_minor': '3.3'
        }
        artifact = get_artifact_name(info)
        self.assertEqual(artifact, 'gdal-h2gis-macos-intel-x86_64')
    
    def test_windows_x64_gdal38(self):
        """Test artifact name for Windows x64 GDAL 3.8."""
        info = {
            'os': 'windows',
            'arch': 'x86_64',
            'gdal_major_minor': '3.8'
        }
        artifact = get_artifact_name(info)
        self.assertEqual(artifact, 'gdal-h2gis-windows-x64-gdal3.8')
    
    def test_unsupported_platform_returns_none(self):
        """Test that unsupported platform returns None."""
        info = {
            'os': 'freebsd',
            'arch': 'x86_64',
            'gdal_major_minor': '3.8'
        }
        artifact = get_artifact_name(info)
        self.assertIsNone(artifact)


class TestH2GISLibSubdir(unittest.TestCase):
    """Tests for H2GIS library subdirectory."""
    
    def test_linux_returns_linux(self):
        """Test that Linux returns 'linux'."""
        info = {'os': 'linux'}
        self.assertEqual(get_h2gis_lib_subdir(info), 'linux')
    
    def test_windows_returns_windows(self):
        """Test that Windows returns 'windows'."""
        info = {'os': 'windows'}
        self.assertEqual(get_h2gis_lib_subdir(info), 'windows')
    
    def test_macos_returns_macos(self):
        """Test that macOS returns 'macos'."""
        info = {'os': 'macos'}
        self.assertEqual(get_h2gis_lib_subdir(info), 'macos')


if __name__ == '__main__':
    unittest.main()
