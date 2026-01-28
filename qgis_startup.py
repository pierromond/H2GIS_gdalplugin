import ctypes
import os
from osgeo import gdal, ogr
from qgis.core import QgsMessageLog, QgsApplication, Qgis

def register_h2gis():
    """Register the H2GIS OGR driver."""
    driver_path = os.environ.get('H2GIS_DRIVER_SO')
    
    if driver_path and os.path.exists(driver_path):
        try:
            print(f"[H2GIS-STARTUP] Loading driver from: {driver_path}")
            
            # Load library with RTLD_GLOBAL so it can find libh2gis symbols
            lib = ctypes.CDLL(driver_path, mode=ctypes.RTLD_GLOBAL)
            
            # Call registration - GraalVM isolate will be created in a dedicated 
            # thread with large stack size to avoid StackOverflowError
            lib.GDALRegister_H2GIS()
            
            # Verify
            if ogr.GetDriverByName("H2GIS"):
                QgsMessageLog.logMessage(f"H2GIS Driver Loaded Successfully from {driver_path}", "H2GIS", Qgis.Info)
                print("[H2GIS-STARTUP] Driver registered successfully!")
            else:
                QgsMessageLog.logMessage("GDALRegister_H2GIS called but driver not found in OGR.", "H2GIS", Qgis.Critical)
                print("[H2GIS-STARTUP] WARNING: Driver registration failed!")

        except Exception as e:
            QgsMessageLog.logMessage(f"Failed to load H2GIS driver: {e}", "H2GIS", Qgis.Critical)
            print(f"[H2GIS-STARTUP] ERROR: {e}")
            import traceback
            traceback.print_exc()
    else:
        QgsMessageLog.logMessage("H2GIS_DRIVER_SO environment variable not set or file missing.", "H2GIS", Qgis.Warning)
        print(f"[H2GIS-STARTUP] WARNING: Driver not found at {driver_path}")

# === INITIALIZATION ===
print("[H2GIS-STARTUP] Starting H2GIS driver registration...")
register_h2gis()
