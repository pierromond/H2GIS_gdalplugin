#!/usr/bin/env python3
import time
import os
import sys

try:
    from osgeo import ogr, gdal
except ImportError:
    print("Error: GDAL/OGR Python bindings not found.")
    sys.exit(1)

ogr.UseExceptions()

DB_NAME = os.path.abspath("bench_many_tables.mv.db")
DRIVER_NAME = "H2GIS"
NUM_TABLES = 1000  # Number of tables to create

def check_driver():
    driver = ogr.GetDriverByName(DRIVER_NAME)
    if not driver:
        print(f"Error: {DRIVER_NAME} driver not found. Is the plugin installed?")
        # Try to print registered drivers
        cnt = ogr.GetDriverCount()
        print(f"Available drivers ({cnt}):")
        for i in range(cnt):
            d = ogr.GetDriver(i)
            print(f"  - {d.GetName()}")
        sys.exit(1)
    return driver

def create_db(driver):
    # Remove existing DB files
    db_base = DB_NAME.replace(".mv.db", "")
    for ext in [".mv.db", ".trace.db"]:
        f = db_base + ext
        if os.path.exists(f):
            try:
                os.remove(f)
            except OSError as e:
                print(f"Warning: Could not remove {f}: {e}")

    print(f"Creating {NUM_TABLES} tables in {DB_NAME}...")
    try:
        ds = driver.CreateDataSource(DB_NAME)
    except Exception as e:
        print(f"Failed to create DataSource: {e}")
        print("Does the driver implement CreateDataSource?")
        return False
        
    start_time = time.time()
    for i in range(NUM_TABLES):
        layer_name = f"layer_{i}"
        # Create minimal layer
        try:
            ds.CreateLayer(layer_name)
        except Exception as e:
             print(f"Failed to create layer {i}: {e}")
             return False
             
        if (i + 1) % 100 == 0:
            print(f"  Created {i + 1} tables...")
            
    # Explicitly close/destroy to flush
    ds = None
    elapsed = time.time() - start_time
    print(f"Creation complete in {elapsed:.2f}s")
    return True

def benchmark_open():
    print(f"Benchmarking Open() on {DB_NAME}...")
    
    start_time = time.time()
    try:
        ds = ogr.Open(DB_NAME)
    except Exception as e:
        print(f"Failed to open DB: {e}")
        return

    open_time = time.time() - start_time
    print(f"Open() took: {open_time:.4f}s")
    
    if ds is None:
        print("Error: ds is None")
        return

    start_count = time.time()
    count = ds.GetLayerCount()
    count_time = time.time() - start_count
    
    print(f"GetLayerCount() took: {count_time:.4f}s")
    print(f"Layer count: {count}")
    
    if count < NUM_TABLES:
        print(f"WARNING: Expected {NUM_TABLES} layers, got {count}. Are some missing?")
    else:
        print(f"SUCCESS: Linear scan seems fast enough (Total Open+Count: {open_time + count_time:.4f}s)")

    # Verify GetFeatureCount optimization
    print("\nVerifying GetFeatureCount optimization...")
    layer = ds.GetLayer(0)
    if layer:
        start_fc = time.time()
        fc = layer.GetFeatureCount(0) # Force=0
        fc_time = time.time() - start_fc
        print(f"GetFeatureCount(0) took: {fc_time:.6f}s (Result: {fc})")
        
        # If it was optimized, it should be very fast (<< 10ms)
        if fc_time < 0.01:
            print("SUCCESS: Feature count is instant (likely using Estimate)")
        else:
            print("WARNING: Feature count might be slow")
            
    else:
        print("Error: Could not get first layer")

if __name__ == "__main__":
    driver = check_driver()
    
    # Check if we need to create DB
    db_base = DB_NAME.replace(".mv.db", "")
    if not os.path.exists(db_base + ".mv.db"):
        if not create_db(driver):
            print("Skipping benchmark due to creation failure")
            sys.exit(1)
    else:
        print(f"Using existing database {DB_NAME}")
        
    benchmark_open()
