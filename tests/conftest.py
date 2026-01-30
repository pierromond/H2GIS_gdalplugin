import pytest
from osgeo import ogr, gdal

# Register driver if not already registered (local build usage)
# In standard run, GDAL_DRIVER_PATH should point to build dir
# But here we might rely on it being present
try:
    ogr.GetDriverByName("H2GIS")
except:
    pass

@pytest.fixture
def h2gis_driver():
    driver = ogr.GetDriverByName("H2GIS")
    if driver is None:
        pytest.skip("H2GIS driver not available", allow_module_level=True)
    return driver

@pytest.fixture
def h2gis_ds(tmp_path, h2gis_driver):
    """Create a fresh H2GIS database for each test."""
    db_path = tmp_path / "test.mv.db"
    ds = h2gis_driver.CreateDataSource(str(db_path))
    if ds is None:
        pytest.fail("Could not create H2GIS datasource")
    return ds 

@pytest.fixture
def h2gis_ds_path(tmp_path):
     return str(tmp_path / "test.mv.db")
