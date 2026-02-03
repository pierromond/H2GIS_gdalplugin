.. _vector.h2gis:

H2GIS - H2 Database with Spatial Extensions
============================================

.. versionadded:: 3.11

.. shortname:: H2GIS

.. built_in_by_default::

This driver provides read/write access to spatial tables in `H2GIS <http://www.h2gis.org/>`__ databases.
H2GIS is a spatial extension of the H2 database engine, providing SQL spatial functions
compliant with the OGC Simple Features for SQL specification.

The driver uses a native library (``libh2gis.so`` / ``libh2gis.dll``) built with GraalVM Native Image
to interface with the H2GIS Java library without requiring a JVM at runtime.

Driver capabilities
-------------------

.. supports_create::

.. supports_georeferencing::

.. supports_virtualio::

Connection string
-----------------

The connection string can be specified in the following formats:

- ``H2GIS:/path/to/database.mv.db`` - Explicit driver prefix
- ``/path/to/database.mv.db`` - Direct file path (requires ``.mv.db`` extension)

.. note::

   The ``.mv.db`` extension is required for H2 database files (H2 v2 format).
   Files with other extensions will not be recognized by the driver.

Open options
------------

The following open options are available:

- **USER**: Username for database authentication. Default is empty.
- **PASSWORD**: Password for database authentication. Default is empty.

Dataset creation options
------------------------

None.

Layer creation options
----------------------

The following layer creation options are available:

- **GEOMETRY_NAME**: Name of the geometry column. Default is ``GEOM``.
- **FID**: Name of the FID (feature identifier) column. Default is ``ID``.
- **SPATIAL_INDEX**: Whether to create a spatial index. Default is ``YES``.
- **SRID**: Spatial Reference System Identifier (EPSG code). Default is ``0`` (undefined).

SQL support
-----------

The driver supports executing SQL statements directly against the H2GIS database
using the native H2 SQL dialect. This includes all H2GIS spatial functions such as:

- ``ST_Buffer``, ``ST_Intersection``, ``ST_Union``
- ``ST_Distance``, ``ST_Area``, ``ST_Length``
- ``ST_Contains``, ``ST_Intersects``, ``ST_Within``
- ``ST_Transform``, ``ST_SetSRID``

Example:

.. code-block::

   ogrinfo H2GIS:/path/to/db.mv.db -sql "SELECT ST_Buffer(GEOM, 100) FROM my_layer"

The OGRSQL dialect is also supported via the ``-dialect OGRSQL`` option.

Geometry types
--------------

The driver supports all OGC geometry types:

- POINT, LINESTRING, POLYGON
- MULTIPOINT, MULTILINESTRING, MULTIPOLYGON
- GEOMETRYCOLLECTION

Z (elevation) and M (measure) coordinates are supported.

Field types
-----------

The following field types are supported:

- Integer (32-bit)
- Integer64 (64-bit)
- Real (double precision)
- String
- Date
- DateTime
- Time
- Binary

Performance considerations
--------------------------

Filter push-down
++++++++++++++++

The driver implements filter push-down for optimal performance:

- **Spatial filters** are pushed to the database using H2GIS spatial predicates
  (``ST_Intersects`` with bounding box optimization).
- **Attribute filters** set via ``SetAttributeFilter()`` are pushed directly
  to the SQL WHERE clause.

Both filter types can be combined and are applied at the database level,
minimizing data transfer.

Spatial indexing
++++++++++++++++

H2GIS uses R-tree spatial indexes. The driver creates spatial indexes by default
when creating new layers (controlled by the ``SPATIAL_INDEX`` layer creation option).

Examples
--------

Opening an existing database
++++++++++++++++++++++++++++

.. code-block::

   ogrinfo H2GIS:/path/to/database.mv.db

Listing layers
++++++++++++++

.. code-block::

   ogrinfo -al H2GIS:/path/to/database.mv.db

Creating a new database and importing data
++++++++++++++++++++++++++++++++++++++++++

.. code-block::

   ogr2ogr -f H2GIS /path/to/new_database.mv.db input.shp

Specifying layer creation options
+++++++++++++++++++++++++++++++++

.. code-block::

   ogr2ogr -f H2GIS /path/to/database.mv.db input.shp \
       -lco GEOMETRY_NAME=THE_GEOM \
       -lco FID=GID \
       -lco SRID=4326

Applying attribute and spatial filters
++++++++++++++++++++++++++++++++++++++

.. code-block::

   ogrinfo H2GIS:/path/to/database.mv.db my_layer \
       -where "population > 10000" \
       -spat 1.5 48.5 2.5 49.5

Requirements
------------

The driver requires the ``libh2gis`` native library to be available:

- On Linux: ``libh2gis.so``
- On Windows: ``libh2gis.dll``
- On macOS: ``libh2gis.dylib``

The easiest way to obtain the native library is via PyPI:

.. code-block::

   pip install h2gis

The library is located in the ``h2gis/lib/`` directory of the installed package.

The library can be located via:

1. The ``H2GIS_NATIVE_LIB`` environment variable (full path to the library)
2. Standard system library paths (``/usr/lib``, ``/usr/local/lib``, etc.)
3. The GDAL plugin directory

See Also
--------

- `H2GIS Official Website <http://www.h2gis.org/>`__
- `H2GIS Documentation <https://h2gis.github.io/docs/>`__
- `H2 Database Engine <https://h2database.com/>`__
- :ref:`OGR SQL dialect <ogr_sql_dialect>`
