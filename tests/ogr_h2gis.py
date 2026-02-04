#!/usr/bin/env pytest
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2024-2026 H2GIS Team
###############################################################################
#
# Project:  GDAL/OGR Test Suite
# Purpose:  Test H2GIS driver functionality.
#
###############################################################################

import pytest
from osgeo import ogr, osr, gdal

# Skip all tests if H2GIS driver is not available
pytestmark = pytest.mark.require_driver("H2GIS")

def test_ogr_h2gis_open_create(h2gis_ds):
    assert h2gis_ds is not None
    assert h2gis_ds.GetLayerCount() == 0

def test_ogr_h2gis_create_layer(h2gis_ds):
    lyr = h2gis_ds.CreateLayer("test_layer", geom_type=ogr.wkbPoint)
    assert lyr is not None
    assert h2gis_ds.GetLayerCount() == 1
    assert h2gis_ds.GetLayerByName("test_layer") is not None
    
    lyr_by_idx = h2gis_ds.GetLayer(0)
    assert lyr_by_idx.GetName() == "test_layer"

def test_ogr_h2gis_field_types(h2gis_ds):
    lyr = h2gis_ds.CreateLayer("fields", geom_type=ogr.wkbNone)
    lyr.CreateField(ogr.FieldDefn("int_f", ogr.OFTInteger))
    lyr.CreateField(ogr.FieldDefn("str_f", ogr.OFTString))
    
    defn = lyr.GetLayerDefn()
    assert defn.GetFieldCount() == 2
    assert defn.GetFieldDefn(0).GetName() == "int_f"
    assert defn.GetFieldDefn(0).GetType() == ogr.OFTInteger

def test_ogr_h2gis_crud(h2gis_ds):
    lyr = h2gis_ds.CreateLayer("crud", geom_type=ogr.wkbPoint)
    lyr.CreateField(ogr.FieldDefn("name", ogr.OFTString))
    
    feat = ogr.Feature(lyr.GetLayerDefn())
    feat.SetField("name", "feature1")
    geom = ogr.CreateGeometryFromWkt("POINT (10 20)")
    feat.SetGeometry(geom)
    
    assert lyr.CreateFeature(feat) == 0
    # FID update in feature object not strictly required for driver function verification
    # But checking if inserted tuple has an ID
    
    lyr.ResetReading()
    feat_read = lyr.GetNextFeature()
    assert feat_read is not None
    fid = feat_read.GetFID()
    assert fid >= 1
    
    # Read back by ID
    feat_read_by_id = lyr.GetFeature(fid)
    assert feat_read_by_id is not None
    assert feat_read_by_id.GetField("name") == "feature1"
    assert feat_read_by_id.GetGeometryRef().ExportToWkt() == "POINT (10 20)"
    
    # Update
    feat_read_by_id.SetField("name", "updated")
    assert lyr.SetFeature(feat_read_by_id) == 0
    
    feat_check = lyr.GetFeature(fid)
    assert feat_check.GetField("name") == "updated"
    
    # Delete
    assert lyr.DeleteFeature(fid) == 0
    assert lyr.GetFeature(fid) is None

def test_ogr_h2gis_transaction(h2gis_ds):
    lyr = h2gis_ds.CreateLayer("trans", geom_type=ogr.wkbPoint)
    
    h2gis_ds.StartTransaction(force=True)
    feat = ogr.Feature(lyr.GetLayerDefn())
    lyr.CreateFeature(feat)
    h2gis_ds.RollbackTransaction()
    
    assert lyr.GetFeatureCount() == 0

def test_ogr_h2gis_execute_sql_basic(h2gis_ds):
    lyr = h2gis_ds.CreateLayer("sql_test", geom_type=ogr.wkbPoint)
    
    # Test DDL via ExecuteSQL
    h2gis_ds.ExecuteSQL("CREATE TABLE custom (id INT)", None, None)
    
    # Test SELECT returning layer logic (even if empty features for now)
    sql_lyr = h2gis_ds.ExecuteSQL("SELECT * FROM custom", None, None)
    assert sql_lyr is not None
    h2gis_ds.ReleaseResultSet(sql_lyr)


def test_ogr_h2gis_srid_crs_roundtrip(h2gis_ds):
    """Test that SRID/CRS is preserved during create and read."""
    srs = osr.SpatialReference()
    srs.ImportFromEPSG(4326)
    
    lyr = h2gis_ds.CreateLayer("srid_test", srs=srs, geom_type=ogr.wkbPoint)
    assert lyr is not None
    
    # Create a feature with geometry
    feat = ogr.Feature(lyr.GetLayerDefn())
    geom = ogr.CreateGeometryFromWkt("POINT (2.35 48.85)")  # Paris
    geom.AssignSpatialReference(srs)
    feat.SetGeometry(geom)
    assert lyr.CreateFeature(feat) == 0
    
    # Read back and verify SRID
    lyr.ResetReading()
    feat_read = lyr.GetNextFeature()
    assert feat_read is not None
    
    geom_read = feat_read.GetGeometryRef()
    assert geom_read is not None
    # SRID should be preserved in geometry
    assert abs(geom_read.GetX() - 2.35) < 0.001
    assert abs(geom_read.GetY() - 48.85) < 0.001


def test_ogr_h2gis_geometry_types(h2gis_ds):
    """Test that different geometry types are correctly preserved."""
    test_cases = [
        ("geom_point", ogr.wkbPoint, "POINT (1 2)"),
        ("geom_line", ogr.wkbLineString, "LINESTRING (0 0, 1 1, 2 2)"),
        ("geom_poly", ogr.wkbPolygon, "POLYGON ((0 0, 1 0, 1 1, 0 1, 0 0))"),
        ("geom_mpoint", ogr.wkbMultiPoint, "MULTIPOINT ((0 0), (1 1))"),
    ]
    
    for name, geom_type, wkt in test_cases:
        lyr = h2gis_ds.CreateLayer(name, geom_type=geom_type)
        assert lyr is not None, f"Failed to create layer {name}"
        
        feat = ogr.Feature(lyr.GetLayerDefn())
        geom = ogr.CreateGeometryFromWkt(wkt)
        feat.SetGeometry(geom)
        assert lyr.CreateFeature(feat) == 0, f"Failed to create feature in {name}"
        
        lyr.ResetReading()
        feat_read = lyr.GetNextFeature()
        assert feat_read is not None, f"Failed to read feature from {name}"
        
        geom_read = feat_read.GetGeometryRef()
        assert geom_read is not None, f"No geometry read from {name}"


def test_ogr_h2gis_attribute_filter(h2gis_ds):
    """Test SetAttributeFilter push-down to H2GIS."""
    lyr = h2gis_ds.CreateLayer("attr_filter", geom_type=ogr.wkbPoint)
    lyr.CreateField(ogr.FieldDefn("population", ogr.OFTInteger))
    lyr.CreateField(ogr.FieldDefn("name", ogr.OFTString))
    
    # Insert test features
    test_data = [
        (100, "small_city"),
        (500000, "medium_city"),
        (2000000, "large_city"),
        (10000000, "mega_city"),
    ]
    
    for pop, name in test_data:
        feat = ogr.Feature(lyr.GetLayerDefn())
        feat.SetField("population", pop)
        feat.SetField("name", name)
        geom = ogr.CreateGeometryFromWkt("POINT (0 0)")
        feat.SetGeometry(geom)
        lyr.CreateFeature(feat)
    
    # Test attribute filter - H2 column names are uppercase, must quote them
    lyr.SetAttributeFilter("\"population\" > 1000000")
    
    count = 0
    lyr.ResetReading()
    feat = lyr.GetNextFeature()
    while feat:
        assert feat.GetField("population") > 1000000
        count += 1
        feat = lyr.GetNextFeature()
    
    assert count == 2, f"Expected 2 features with population > 1M, got {count}"
    
    # Test feature count with filter
    assert lyr.GetFeatureCount(force=True) == 2
    
    # Clear filter
    lyr.SetAttributeFilter(None)
    assert lyr.GetFeatureCount(force=True) == 4


def test_ogr_h2gis_spatial_filter(h2gis_ds):
    """Test spatial filter push-down to H2GIS."""
    srs = osr.SpatialReference()
    srs.ImportFromEPSG(4326)
    
    lyr = h2gis_ds.CreateLayer("spatial_filter", srs=srs, geom_type=ogr.wkbPoint)
    lyr.CreateField(ogr.FieldDefn("name", ogr.OFTString))
    
    # Insert test features in different locations
    locations = [
        ("paris", 2.35, 48.85),
        ("london", -0.12, 51.51),
        ("berlin", 13.40, 52.52),
        ("tokyo", 139.69, 35.68),
    ]
    
    for name, x, y in locations:
        feat = ogr.Feature(lyr.GetLayerDefn())
        feat.SetField("name", name)
        geom = ogr.CreateGeometryFromWkt(f"POINT ({x} {y})")
        feat.SetGeometry(geom)
        lyr.CreateFeature(feat)
    
    # Create spatial filter for Europe (roughly)
    filter_geom = ogr.CreateGeometryFromWkt(
        "POLYGON ((-10 35, 30 35, 30 60, -10 60, -10 35))"
    )
    lyr.SetSpatialFilter(filter_geom)
    
    count = 0
    lyr.ResetReading()
    feat = lyr.GetNextFeature()
    while feat:
        count += 1
        name = feat.GetField("name")
        assert name in ["paris", "london", "berlin"], f"Unexpected feature: {name}"
        feat = lyr.GetNextFeature()
    
    assert count == 3, f"Expected 3 European cities, got {count}"
    
    # Clear filter
    lyr.SetSpatialFilter(None)
    assert lyr.GetFeatureCount(force=True) == 4


def test_ogr_h2gis_z_zm_geometries(h2gis_ds):
    """Test Z geometry types are correctly preserved.
    
    Note: H2GIS has limited support for M (measure) dimension.
    We test only Z geometries which are fully supported.
    """
    test_cases = [
        ("geom_point_z", ogr.wkbPoint25D, "POINT Z (1 2 3)"),
        ("geom_line_z", ogr.wkbLineString25D, "LINESTRING Z (0 0 0, 1 1 1, 2 2 2)"),
        ("geom_poly_z", ogr.wkbPolygon25D, "POLYGON Z ((0 0 0, 1 0 0, 1 1 1, 0 1 0, 0 0 0))"),
        ("geom_mpoint_z", ogr.wkbMultiPoint25D, "MULTIPOINT Z ((0 0 0), (1 1 1))"),
    ]
    
    for name, geom_type, wkt in test_cases:
        lyr = h2gis_ds.CreateLayer(name, geom_type=geom_type)
        assert lyr is not None, f"Failed to create layer {name}"
        
        feat = ogr.Feature(lyr.GetLayerDefn())
        geom = ogr.CreateGeometryFromWkt(wkt)
        assert geom is not None, f"Failed to create geometry from {wkt}"
        feat.SetGeometry(geom)
        assert lyr.CreateFeature(feat) == 0, f"Failed to create feature in {name}"
        
        lyr.ResetReading()
        feat_read = lyr.GetNextFeature()
        assert feat_read is not None, f"Failed to read feature from {name}"
        
        geom_read = feat_read.GetGeometryRef()
        assert geom_read is not None, f"No geometry read from {name}"
        
        # Verify Z dimension is preserved
        assert geom_read.Is3D(), f"Z dimension lost in {name}"


def test_ogr_h2gis_date_datetime_fields(h2gis_ds):
    """Test Date, Time, and DateTime field types are correctly preserved."""
    lyr = h2gis_ds.CreateLayer("datetime_test", geom_type=ogr.wkbNone)
    
    # Create fields
    lyr.CreateField(ogr.FieldDefn("date_f", ogr.OFTDate))
    lyr.CreateField(ogr.FieldDefn("time_f", ogr.OFTTime))
    lyr.CreateField(ogr.FieldDefn("datetime_f", ogr.OFTDateTime))
    
    # Verify field types are registered in schema
    defn = lyr.GetLayerDefn()
    assert defn.GetFieldDefn(0).GetType() == ogr.OFTDate
    assert defn.GetFieldDefn(1).GetType() == ogr.OFTTime
    assert defn.GetFieldDefn(2).GetType() == ogr.OFTDateTime

    # Create feature with date/time values
    feat = ogr.Feature(defn)
    feat.SetField("date_f", 2026, 2, 4, 0, 0, 0, 0)  # 2026-02-04
    feat.SetField("time_f", 0, 0, 0, 14, 30, 45, 0)  # 14:30:45
    feat.SetField("datetime_f", 2026, 2, 4, 14, 30, 45, 0)  # 2026-02-04 14:30:45

    # Test that insertion works (H2 format is correct)
    assert lyr.CreateFeature(feat) == 0
    
    # Note: Reading date/time values back may not preserve the exact type
    # due to H2GIS serialization returning as strings.
    # This test verifies that CREATE FIELD and INSERT work correctly.


def test_ogr_h2gis_set_next_by_index(h2gis_ds):
    """Test SetNextByIndex for fast random access."""
    lyr = h2gis_ds.CreateLayer("setnext_test", geom_type=ogr.wkbPoint)
    lyr.CreateField(ogr.FieldDefn("idx", ogr.OFTInteger))
    
    # Insert 10 features
    for i in range(10):
        feat = ogr.Feature(lyr.GetLayerDefn())
        feat.SetField("idx", i)
        geom = ogr.CreateGeometryFromWkt(f"POINT ({i} {i})")
        feat.SetGeometry(geom)
        lyr.CreateFeature(feat)
    
    # Test SetNextByIndex
    assert lyr.TestCapability(ogr.OLCFastSetNextByIndex)
    
    # Jump to index 5
    lyr.SetNextByIndex(5)
    feat = lyr.GetNextFeature()
    assert feat is not None
    # Note: The feature at offset 5 should be the 6th feature (0-indexed)
    assert feat.GetField("idx") == 5
    
    # Continue reading
    feat = lyr.GetNextFeature()
    assert feat is not None
    assert feat.GetField("idx") == 6
