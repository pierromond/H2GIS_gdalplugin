// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2024-2026 H2GIS Team

#ifndef OGR_H2GIS_H_INCLUDED
#define OGR_H2GIS_H_INCLUDED

#include "ogrsf_frmts.h"
// Use wrapper instead of direct h2gis.h to enable lazy loading via dlopen
#include "h2gis_wrapper.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>

constexpr const char *H2GIS_DRIVER_NAME = "H2GIS";

// GraalVM access functions (use wrapper functions)
inline graal_isolate_t *GetGlobalIsolate()
{
    return h2gis_wrapper_get_isolate();
}

inline graal_isolatethread_t *GetOrAttachThread()
{
    return h2gis_wrapper_get_thread();
}

// H2GIS Type Constants
#define H2GIS_TYPE_INT 1
#define H2GIS_TYPE_LONG 2
#define H2GIS_TYPE_FLOAT 3
#define H2GIS_TYPE_DOUBLE 4
#define H2GIS_TYPE_BOOL 5
#define H2GIS_TYPE_STRING 6
#define H2GIS_TYPE_DATE 7
#define H2GIS_TYPE_GEOM 8
#define H2GIS_TYPE_OTHER 99

// Pre-fetched column metadata from INFORMATION_SCHEMA.COLUMNS
struct H2GISColumnInfo
{
    std::string name;  // COLUMN_NAME
    std::string
        dataType;  // DATA_TYPE (e.g., "INTEGER", "GEOMETRY", "CHARACTER VARYING")
    int ordinalPosition;  // ORDINAL_POSITION
    std::string
        geometryType;  // GEOMETRY_TYPE (e.g., "MULTIPOLYGON Z", "POINT Z") - empty for non-geometry
    int geometrySrid;  // GEOMETRY_SRID - 0 if unknown

    bool isGeometry() const
    {
        return dataType == "GEOMETRY" || !geometryType.empty();
    }
};

// Map H2GIS geometry type string to OGRwkbGeometryType
// Handles both OGC format ("POINT Z") and H2GIS format ("POINTZ")
inline OGRwkbGeometryType MapH2GeometryType(const std::string &h2Type)
{
    static const std::unordered_map<std::string, OGRwkbGeometryType> typeMap = {
        // 2D types
        {"POINT", wkbPoint},
        {"LINESTRING", wkbLineString},
        {"POLYGON", wkbPolygon},
        {"MULTIPOINT", wkbMultiPoint},
        {"MULTILINESTRING", wkbMultiLineString},
        {"MULTIPOLYGON", wkbMultiPolygon},
        {"GEOMETRYCOLLECTION", wkbGeometryCollection},
        {"GEOMETRY", wkbUnknown},

        // 2.5D types - OGC format (with space)
        {"POINT Z", wkbPoint25D},
        {"LINESTRING Z", wkbLineString25D},
        {"POLYGON Z", wkbPolygon25D},
        {"MULTIPOINT Z", wkbMultiPoint25D},
        {"MULTILINESTRING Z", wkbMultiLineString25D},
        {"MULTIPOLYGON Z", wkbMultiPolygon25D},
        {"GEOMETRYCOLLECTION Z", wkbGeometryCollection25D},

        // 2.5D types - H2GIS format (no space)
        {"POINTZ", wkbPoint25D},
        {"LINESTRINGZ", wkbLineString25D},
        {"POLYGONZ", wkbPolygon25D},
        {"MULTIPOINTZ", wkbMultiPoint25D},
        {"MULTILINESTRINGZ", wkbMultiLineString25D},
        {"MULTIPOLYGONZ", wkbMultiPolygon25D},
        {"GEOMETRYCOLLECTIONZ", wkbGeometryCollection25D},

        // 3D types (ZM suffix) - OGC format
        {"POINT ZM", wkbPointZM},
        {"LINESTRING ZM", wkbLineStringZM},
        {"POLYGON ZM", wkbPolygonZM},
        {"MULTIPOINT ZM", wkbMultiPointZM},
        {"MULTILINESTRING ZM", wkbMultiLineStringZM},
        {"MULTIPOLYGON ZM", wkbMultiPolygonZM},
        {"GEOMETRYCOLLECTION ZM", wkbGeometryCollectionZM},

        // 3D types (ZM suffix) - H2GIS format
        {"POINTZM", wkbPointZM},
        {"LINESTRINGZM", wkbLineStringZM},
        {"POLYGONZM", wkbPolygonZM},
        {"MULTIPOINTZM", wkbMultiPointZM},
        {"MULTILINESTRINGZM", wkbMultiLineStringZM},
        {"MULTIPOLYGONZM", wkbMultiPolygonZM},
        {"GEOMETRYCOLLECTIONZM", wkbGeometryCollectionZM},

        // M suffix only - OGC format
        {"POINT M", wkbPointM},
        {"LINESTRING M", wkbLineStringM},
        {"POLYGON M", wkbPolygonM},
        {"MULTIPOINT M", wkbMultiPointM},
        {"MULTILINESTRING M", wkbMultiLineStringM},
        {"MULTIPOLYGON M", wkbMultiPolygonM},
        {"GEOMETRYCOLLECTION M", wkbGeometryCollectionM},

        // M suffix only - H2GIS format
        {"POINTM", wkbPointM},
        {"LINESTRINGM", wkbLineStringM},
        {"POLYGONM", wkbPolygonM},
        {"MULTIPOINTM", wkbMultiPointM},
        {"MULTILINESTRINGM", wkbMultiLineStringM},
        {"MULTIPOLYGONM", wkbMultiPolygonM},
        {"GEOMETRYCOLLECTIONM", wkbGeometryCollectionM},
    };

    auto it = typeMap.find(h2Type);
    return (it != typeMap.end()) ? it->second : wkbUnknown;
}

// Map H2 DATA_TYPE string to OGRFieldType
inline OGRFieldType MapH2DataType(const std::string &h2Type)
{
    if (h2Type == "INTEGER" || h2Type == "SMALLINT" || h2Type == "TINYINT")
        return OFTInteger;
    if (h2Type == "BIGINT")
        return OFTInteger64;
    if (h2Type == "REAL" || h2Type == "DOUBLE PRECISION" || h2Type == "FLOAT" ||
        h2Type == "DECIMAL" || h2Type == "NUMERIC")
        return OFTReal;
    if (h2Type == "BOOLEAN")
        return OFTInteger;  // OGR uses Integer for bool
    if (h2Type == "DATE")
        return OFTDate;
    if (h2Type == "TIME" || h2Type == "TIME WITH TIME ZONE")
        return OFTTime;
    if (h2Type == "TIMESTAMP" || h2Type == "TIMESTAMP WITH TIME ZONE")
        return OFTDateTime;
    if (h2Type == "BINARY" || h2Type == "VARBINARY" || h2Type == "BLOB")
        return OFTBinary;
    // Default: CHARACTER VARYING, VARCHAR, CLOB, etc.
    return OFTString;
}

// Map OGRwkbGeometryType to H2GIS geometry type name for CREATE TABLE
inline const char *MapOGRGeomTypeToH2Name(OGRwkbGeometryType eType)
{
    switch (wkbFlatten(eType))
    {
        case wkbPoint:
            return "POINT";
        case wkbLineString:
            return "LINESTRING";
        case wkbPolygon:
            return "POLYGON";
        case wkbMultiPoint:
            return "MULTIPOINT";
        case wkbMultiLineString:
            return "MULTILINESTRING";
        case wkbMultiPolygon:
            return "MULTIPOLYGON";
        case wkbGeometryCollection:
            return "GEOMETRYCOLLECTION";
        default:
            return "GEOMETRY";  // Generic fallback
    }
}

// Get Z/M suffix for H2GIS geometry type
inline const char *GetH2GeomZMSuffix(OGRwkbGeometryType eType)
{
    bool hasZ = wkbHasZ(eType);
    bool hasM = wkbHasM(eType);
    if (hasZ && hasM)
        return " ZM";
    if (hasZ)
        return " Z";
    if (hasM)
        return " M";
    return "";
}

class OGRH2GISDataSource;

class OGRH2GISLayer final : public OGRLayer
{
    OGRH2GISDataSource *m_poDS;
    OGRFeatureDefn *m_poFeatureDefn;
    std::string m_osTableName;  // Original table name (for SQL queries)
    std::string m_osGeomCol;    // Geometry column name for this layer
    std::string m_osFIDCol;     // FID column name (empty => use _ROWID_)
    int m_nSRID;                // Cached SRID

    // Iterator state
    long long m_nRS;    // ResultSet Handle
    long long m_hStmt;  // Prepared Statement Handle

    // Batch Buffer State
    void *m_pBatchBuffer;
    long long m_nBatchBufferSize;
    int m_nBatchRows;
    int m_iNextRowInBatch;
    std::vector<uint8_t *>
        m_columnValues;              // Cursors to current row's data in buffer
    std::vector<int> m_columnTypes;  // Types of the columns in the buffer
    std::vector<std::string> m_columnNames;  // Column names in batch

    GIntBig m_iNextShapeId;
    GIntBig
        m_nFeatureCount;  // Cached feature count (pre-filled from INFORMATION_SCHEMA)
    bool m_bSchemaFetched;  // True if schema was pre-filled in constructor
    bool m_bResetPending;   // Lazy reset - don't prepare query until first read
    std::unordered_set<std::string> m_ignoredFields;
    std::string
        m_osAttributeFilter;  // Attribute filter WHERE clause for push-down

    void ClearStatement();
    void PrepareQuery();
    bool FetchNextBatch();
    void FetchSchema();
    void EnsureSchema();

  public:
    // New constructor with pre-fetched metadata from INFORMATION_SCHEMA
    OGRH2GISLayer(
        OGRH2GISDataSource *poDS,
        const char *pszTableName,  // Original table name
        const char *pszLayerName,  // Layer name (TABLE or TABLE.GEOM_COL)
        const char *pszGeomCol,  // Geometry column name (empty for non-spatial)
        const char *pszFIDCol,   // FID column name (empty => use _ROWID_)
        int nSrid,               // SRID
        OGRwkbGeometryType eGeomType,  // Geometry type from GEOMETRY_TYPE
        GIntBig nRowCountEstimate,     // ROW_COUNT_ESTIMATE
        const std::vector<H2GISColumnInfo> &columns);  // Pre-fetched columns

    virtual ~OGRH2GISLayer();

    virtual void ResetReading() override;
    virtual OGRFeature *GetNextFeature() override;
    virtual OGRFeature *GetFeature(GIntBig nFID) override;
#if GDAL_VERSION_NUM >= 3100000
    virtual const OGRFeatureDefn *GetLayerDefn() const override
    {
        return m_poFeatureDefn;
    }
#else
    virtual OGRFeatureDefn *GetLayerDefn() override
    {
        return m_poFeatureDefn;
    }
#endif

    const char *GetGeomColumnName() const
    {
        return m_osGeomCol.c_str();
    }

    const char *GetTableName() const
    {
        return m_osTableName.c_str();
    }

#if GDAL_VERSION_NUM >= 3100000
    virtual int TestCapability(const char *) const override;
#else
    virtual int TestCapability(const char *) override;
#endif

    // GDAL 3.10+ const correctness
#if GDAL_VERSION_NUM >= 3100000
    const char *GetFIDColumn() const override
#else
    const char *GetFIDColumn() override
#endif
    {
        return m_osFIDCol.empty() ? "_ROWID_" : m_osFIDCol.c_str();
    }

#if GDAL_VERSION_NUM >= 3100000
    const char *GetGeometryColumn() const override
#else
    const char *GetGeometryColumn() override
#endif
    {
        return m_osGeomCol.c_str();
    }

    // GDAL 3.12+ changed SetSpatialFilter/GetExtent to non-virtual
    // with ISetSpatialFilter/IGetExtent as protected virtual overrides
#if GDAL_VERSION_NUM >= 3120000
  protected:
    virtual OGRErr ISetSpatialFilter(int iGeomField,
                                     const OGRGeometry *) override;
    virtual OGRErr IGetExtent(int iGeomField, OGREnvelope *psExtent,
                              bool bForce) override;

  public:
#else
    virtual void SetSpatialFilter(OGRGeometry *) override;
    virtual void SetSpatialFilter(int iGeomField, OGRGeometry *) override;
#endif
    virtual OGRErr SetAttributeFilter(const char *pszQuery) override;

#if GDAL_VERSION_NUM >= 3090000
    virtual OGRErr SetIgnoredFields(const char *const *papszFields) override;
#else
    virtual OGRErr SetIgnoredFields(const char **papszFields) override;
#endif

    virtual GIntBig GetFeatureCount(int bForce) override;
#if GDAL_VERSION_NUM < 3120000
    virtual OGRErr GetExtent(OGREnvelope *psExtent, int bForce = TRUE) override;
    virtual OGRErr GetExtent(int iGeomField, OGREnvelope *psExtent,
                             int bForce = TRUE) override;
#endif

    virtual OGRErr SetNextByIndex(GIntBig nIndex) override;

    virtual OGRErr ICreateFeature(OGRFeature *poFeature) override;
    virtual OGRErr ISetFeature(OGRFeature *poFeature) override;
    virtual OGRErr DeleteFeature(GIntBig nFID) override;

#if GDAL_VERSION_NUM >= 3090000
    virtual OGRErr CreateField(const OGRFieldDefn *poField,
                               int bApproxOK = TRUE) override;
#else
    virtual OGRErr CreateField(OGRFieldDefn *poField,
                               int bApproxOK = TRUE) override;
#endif
};

class OGRH2GISDataSource final : public GDALDataset
{
    char *m_pszName;
    OGRH2GISLayer **m_papoLayers;
    int m_nLayers;

    long long m_hConnection;  // H2GIS Connection ID (long long in C API)
    void *m_hThread;          // GraalVM Isolate Thread

  public:
    OGRH2GISDataSource();
    virtual ~OGRH2GISDataSource();

    int Open(const char *pszFilename, int bUpdate,
             const char *pszUser = nullptr, const char *pszPassword = nullptr);

#if GDAL_VERSION_NUM >= 3100000
    virtual int GetLayerCount() const override
    {
        return m_nLayers;
    }

    virtual OGRLayer *GetLayer(int) const override;
#else
    virtual int GetLayerCount() override
    {
        return m_nLayers;
    }

    virtual OGRLayer *GetLayer(int) override;
#endif
    virtual OGRLayer *GetLayerByName(const char *) override;

    virtual OGRLayer *ExecuteSQL(const char *pszSQL,
                                 OGRGeometry *poSpatialFilter,
                                 const char *pszDialect) override;
    virtual void ReleaseResultSet(OGRLayer *poLayer) override;

#if GDAL_VERSION_NUM >= 3100000
    virtual OGRLayer *ICreateLayer(const char *pszName,
                                   const OGRGeomFieldDefn *poGeomFieldDefn,
                                   CSLConstList papszOptions) override;
#elif GDAL_VERSION_NUM >= 3090000
    virtual OGRLayer *
    ICreateLayer(const char *pszName,
                 const OGRSpatialReference *poSpatialRef = nullptr,
                 OGRwkbGeometryType eGType = wkbUnknown,
                 CSLConstList papszOptions = nullptr) override;
#elif GDAL_VERSION_NUM >= 3050000
    virtual OGRLayer *
    ICreateLayer(const char *pszName,
                 const OGRSpatialReference *poSpatialRef = nullptr,
                 OGRwkbGeometryType eGType = wkbUnknown,
                 char **papszOptions = nullptr) override;
#else
    virtual OGRLayer *ICreateLayer(const char *pszName,
                                   OGRSpatialReference *poSpatialRef = nullptr,
                                   OGRwkbGeometryType eGType = wkbUnknown,
                                   char **papszOptions = nullptr) override;
#endif

    virtual OGRErr DeleteLayer(int iLayer) override;

    virtual OGRErr StartTransaction(int bForce) override;
    virtual OGRErr CommitTransaction() override;
    virtual OGRErr RollbackTransaction() override;

#if GDAL_VERSION_NUM >= 3100000
    virtual int TestCapability(const char *) const override;
#else
    virtual int TestCapability(const char *) override;
#endif

    long long GetConnection()
    {
        return m_hConnection;
    }

    void *GetThread()
    {
        return m_hThread;
    }
};

#endif  // OGR_H2GIS_H_INCLUDED
