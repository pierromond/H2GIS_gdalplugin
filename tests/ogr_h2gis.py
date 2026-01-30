#!/usr/bin/env pytest
###############################################################################
#
# Project:  GDAL/OGR Test Suite
# Purpose:  Test H2GIS driver functionality.
#
###############################################################################

import pytest
from osgeo import ogr, osr, gdal

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

