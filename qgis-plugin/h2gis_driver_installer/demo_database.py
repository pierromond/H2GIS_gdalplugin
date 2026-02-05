# -*- coding: utf-8 -*-
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Demo database creation for H2GIS Driver Installer.

Creates a sample H2GIS database with example spatial layers.
"""

import os
from pathlib import Path
from typing import Tuple, Optional

from qgis.core import (
    QgsMessageLog, 
    QgsVectorLayer,
    QgsProject,
    QgsCoordinateReferenceSystem,
    Qgis,
)

from .platform_detector import get_user_install_dir, is_driver_installed


class DemoDatabase:
    """Creates and manages demo H2GIS databases."""
    
    # Sample data: French cities with population
    CITIES_DATA = [
        ("Paris", 2161000, 2.3522, 48.8566),
        ("Lyon", 516092, 4.8357, 45.7640),
        ("Marseille", 861635, 5.3698, 43.2965),
        ("Toulouse", 479553, 1.4442, 43.6047),
        ("Nice", 342669, 7.2620, 43.7102),
        ("Nantes", 309346, -1.5536, 47.2184),
        ("Strasbourg", 280966, 7.7521, 48.5734),
        ("Montpellier", 285121, 3.8767, 43.6108),
        ("Bordeaux", 254436, -0.5792, 44.8378),
        ("Lille", 232787, 3.0573, 50.6292),
    ]
    
    # Sample polygon: Bounding box of France (simplified)
    FRANCE_BBOX = "POLYGON((-5.14 41.33, 9.56 41.33, 9.56 51.09, -5.14 51.09, -5.14 41.33))"
    
    # Sample lines: Major rivers (simplified)
    RIVERS_DATA = [
        ("Seine", "LINESTRING(2.35 48.86, 1.09 49.44, 0.11 49.49)"),
        ("Loire", "LINESTRING(-1.55 47.22, 0.07 47.39, 1.91 47.09, 2.78 47.27)"),
        ("Rhône", "LINESTRING(4.84 45.76, 4.83 44.93, 4.81 43.95, 4.87 43.40)"),
        ("Garonne", "LINESTRING(-0.58 44.84, -0.33 44.57, 0.62 44.20, 1.44 43.60)"),
    ]

    def __init__(self):
        """Initialize demo database creator."""
        self.install_dir = get_user_install_dir()
        self.demo_dir = self.install_dir / "demo"
        self.demo_dir.mkdir(parents=True, exist_ok=True)

    def get_demo_path(self) -> Path:
        """Get path to the demo database."""
        return self.demo_dir / "h2gis_demo.mv.db"

    def create_demo_database(self) -> Tuple[bool, str]:
        """
        Create a demo H2GIS database with sample layers.
        
        Returns:
            Tuple of (success, message)
        """
        if not is_driver_installed():
            return (False, 
                "H2GIS driver is not available.\n"
                "Please install the driver first and restart QGIS."
            )
        
        try:
            from osgeo import ogr, osr
            
            db_path = self.get_demo_path()
            
            # Remove existing demo database
            if db_path.exists():
                db_path.unlink()
            
            # Create H2GIS datasource
            driver = ogr.GetDriverByName("H2GIS")
            if driver is None:
                return (False, "H2GIS driver not found in OGR")
            
            ds = driver.CreateDataSource(str(db_path))
            if ds is None:
                return (False, f"Failed to create database: {db_path}")
            
            # Create SRS (WGS84)
            srs = osr.SpatialReference()
            srs.ImportFromEPSG(4326)
            
            # Create Cities layer (Points)
            cities_layer = ds.CreateLayer(
                "cities",
                srs,
                ogr.wkbPoint,
                options=["GEOMETRY_NAME=GEOM", "FID=ID", "SPATIAL_INDEX=YES"]
            )
            
            # Add fields
            cities_layer.CreateField(ogr.FieldDefn("name", ogr.OFTString))
            cities_layer.CreateField(ogr.FieldDefn("population", ogr.OFTInteger))
            
            # Add features
            for name, pop, lon, lat in self.CITIES_DATA:
                feature = ogr.Feature(cities_layer.GetLayerDefn())
                feature.SetField("name", name)
                feature.SetField("population", pop)
                point = ogr.Geometry(ogr.wkbPoint)
                point.AddPoint(lon, lat)
                feature.SetGeometry(point)
                cities_layer.CreateFeature(feature)
                feature = None
            
            QgsMessageLog.logMessage(
                f"Created cities layer with {len(self.CITIES_DATA)} features",
                "H2GIS",
                Qgis.Info
            )
            
            # Create Rivers layer (Lines)
            rivers_layer = ds.CreateLayer(
                "rivers",
                srs,
                ogr.wkbLineString,
                options=["GEOMETRY_NAME=GEOM", "FID=ID", "SPATIAL_INDEX=YES"]
            )
            
            rivers_layer.CreateField(ogr.FieldDefn("name", ogr.OFTString))
            
            for name, wkt in self.RIVERS_DATA:
                feature = ogr.Feature(rivers_layer.GetLayerDefn())
                feature.SetField("name", name)
                geom = ogr.CreateGeometryFromWkt(wkt)
                feature.SetGeometry(geom)
                rivers_layer.CreateFeature(feature)
                feature = None
            
            QgsMessageLog.logMessage(
                f"Created rivers layer with {len(self.RIVERS_DATA)} features",
                "H2GIS",
                Qgis.Info
            )
            
            # Create France boundary layer (Polygon)
            boundary_layer = ds.CreateLayer(
                "france_boundary",
                srs,
                ogr.wkbPolygon,
                options=["GEOMETRY_NAME=GEOM", "FID=ID", "SPATIAL_INDEX=YES"]
            )
            
            boundary_layer.CreateField(ogr.FieldDefn("name", ogr.OFTString))
            
            feature = ogr.Feature(boundary_layer.GetLayerDefn())
            feature.SetField("name", "France (simplified)")
            geom = ogr.CreateGeometryFromWkt(self.FRANCE_BBOX)
            feature.SetGeometry(geom)
            boundary_layer.CreateFeature(feature)
            feature = None
            
            QgsMessageLog.logMessage(
                "Created france_boundary layer",
                "H2GIS",
                Qgis.Info
            )
            
            # Close datasource to flush to disk
            ds = None
            
            return (True, 
                f"Demo database created successfully!\n\n"
                f"Location: {db_path}\n\n"
                f"Layers:\n"
                f"  - cities: {len(self.CITIES_DATA)} French cities (Points)\n"
                f"  - rivers: {len(self.RIVERS_DATA)} major rivers (LineStrings)\n"
                f"  - france_boundary: Simplified boundary (Polygon)\n\n"
                f"CRS: EPSG:4326 (WGS84)"
            )
            
        except Exception as e:
            QgsMessageLog.logMessage(
                f"Demo database creation failed: {e}",
                "H2GIS",
                Qgis.Critical
            )
            return (False, f"Failed to create demo database: {e}")

    def open_demo_in_qgis(self) -> Tuple[bool, str]:
        """
        Open the demo database in QGIS.
        
        Returns:
            Tuple of (success, message)
        """
        db_path = self.get_demo_path()
        
        if not db_path.exists():
            return (False, 
                "Demo database does not exist.\n"
                "Please create it first."
            )
        
        try:
            # Add layers to QGIS
            project = QgsProject.instance()
            layers_added = []
            
            # Open each layer
            for layer_name in ["cities", "rivers", "france_boundary"]:
                uri = str(db_path)
                layer = QgsVectorLayer(f"{uri}|layername={layer_name}", layer_name, "ogr")
                
                if layer.isValid():
                    project.addMapLayer(layer)
                    layers_added.append(layer_name)
                    QgsMessageLog.logMessage(
                        f"Added layer: {layer_name}",
                        "H2GIS",
                        Qgis.Info
                    )
                else:
                    QgsMessageLog.logMessage(
                        f"Failed to load layer: {layer_name}",
                        "H2GIS",
                        Qgis.Warning
                    )
            
            if layers_added:
                return (True, 
                    f"Opened demo database in QGIS!\n\n"
                    f"Layers added:\n" + 
                    "\n".join(f"  - {l}" for l in layers_added)
                )
            else:
                return (False, "No layers could be loaded from the demo database.")
                
        except Exception as e:
            return (False, f"Failed to open demo database: {e}")

    def demo_exists(self) -> bool:
        """Check if demo database already exists."""
        return self.get_demo_path().exists()
