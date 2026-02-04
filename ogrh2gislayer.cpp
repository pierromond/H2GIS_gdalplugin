// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2024-2026 H2GIS Team

#include "ogr_h2gis.h"
#include <cstring>
#include <limits>
#include <cstdio>
#include "cpl_error.h"

// Standard GDAL logging helper
static void LogLayer(const char *func, const char *tableName)
{
    CPLDebug("H2GIS", "[LAYER] %s: %s", func, tableName);
}

// h2gis_free_result_buffer is declared in h2gis.h, no need to redeclare

OGRH2GISLayer::OGRH2GISLayer(OGRH2GISDataSource *poDS, const char *pszTableName,
                             const char *pszLayerName, const char *pszGeomCol,
                             const char *pszFIDCol, int nSrid,
                             OGRwkbGeometryType eGeomType,
                             GIntBig nRowCountEstimate,
                             const std::vector<H2GISColumnInfo> &columns)
    : m_poDS(poDS), m_poFeatureDefn(new OGRFeatureDefn(pszLayerName)),
      m_osTableName(pszTableName ? pszTableName : ""),
      m_osGeomCol(pszGeomCol ? pszGeomCol : ""),
      m_osFIDCol(pszFIDCol ? pszFIDCol : ""), m_nSRID(nSrid), m_nRS(0),
      m_hStmt(0), m_pBatchBuffer(nullptr), m_nBatchBufferSize(0),
      m_nBatchRows(0), m_iNextRowInBatch(0), m_iNextShapeId(0),
      m_nFeatureCount(nRowCountEstimate),
      m_bSchemaFetched(
          !columns.empty()),  // Schema is pre-fetched if columns provided
      m_bResetPending(true)
{
    SetDescription(m_poFeatureDefn->GetName());
    m_poFeatureDefn->Reference();

    // OGRFeatureDefn creates a default unnamed geometry field by default.
    // Remove it first, then add our properly named geometry field.
    while (m_poFeatureDefn->GetGeomFieldCount() > 0)
    {
        m_poFeatureDefn->DeleteGeomFieldDefn(0);
    }

    // Add geometry field if this is a spatial layer
    if (!m_osGeomCol.empty())
    {
        OGRGeomFieldDefn gfd(m_osGeomCol.c_str(), eGeomType);
        m_poFeatureDefn->AddGeomFieldDefn(&gfd);

        // Set SRS AFTER adding to feature defn (to avoid cloning issues)
        if (nSrid > 0 && m_poFeatureDefn->GetGeomFieldCount() > 0)
        {
            OGRSpatialReference *poSRS = new OGRSpatialReference();
            OGRErr err = poSRS->importFromEPSG(nSrid);
            if (err != OGRERR_NONE)
            {
                // Fallback: try SetFromUserInput (handles more formats)
                std::string sridStr = "EPSG:" + std::to_string(nSrid);
                err = poSRS->SetFromUserInput(sridStr.c_str());
            }
            if (err == OGRERR_NONE)
            {
                m_poFeatureDefn->GetGeomFieldDefn(0)->SetSpatialRef(poSRS);
                LogLayer("SRID set OK", std::to_string(nSrid).c_str());
            }
            else
            {
                LogLayer("SRID import FAILED", std::to_string(nSrid).c_str());
            }
            poSRS->Release();
        }
    }
    else
    {
        // Non-spatial layer
        m_poFeatureDefn->SetGeomType(wkbNone);
    }

    // Pre-populate attribute fields from INFORMATION_SCHEMA
    for (const auto &col : columns)
    {
        // Skip geometry columns (already handled above)
        if (col.isGeometry())
            continue;
        if (!m_osFIDCol.empty() && EQUAL(col.name.c_str(), m_osFIDCol.c_str()))
            continue;

        OGRFieldType ogrType = MapH2DataType(col.dataType);
        OGRFieldDefn oField(col.name.c_str(), ogrType);
        m_poFeatureDefn->AddFieldDefn(&oField);
    }

    LogLayer("Constructor (pre-fetched schema)", pszLayerName);
}

OGRH2GISLayer::~OGRH2GISLayer()
{
    ClearStatement();
    if (m_pBatchBuffer)
    {
        graal_isolatethread_t *thread =
            (graal_isolatethread_t *)m_poDS->GetThread();
        if (thread)
            h2gis_free_result_buffer(thread, m_pBatchBuffer);
    }
    m_poFeatureDefn->Release();
}

void OGRH2GISLayer::ClearStatement()
{
    graal_isolatethread_t *thread =
        (graal_isolatethread_t *)m_poDS->GetThread();
    if (m_nRS)
    {
        h2gis_close_query(thread, m_nRS);
        m_nRS = 0;
    }
    if (m_hStmt)
    {
        h2gis_close_query(thread, m_hStmt);
        m_hStmt = 0;
    }
}

void OGRH2GISLayer::FetchSchema()
{
    // If schema was pre-populated from INFORMATION_SCHEMA, nothing to do
    if (m_bSchemaFetched)
    {
        LogLayer("FetchSchema SKIPPED (pre-fetched)",
                 m_poFeatureDefn->GetName());
        return;
    }

    graal_isolatethread_t *thread =
        (graal_isolatethread_t *)m_poDS->GetThread();
    long long conn = m_poDS->GetConnection();

    // Use table name for SQL queries (not layer name which may be TABLE.GEOM_COL)
    std::string sql = "SELECT * FROM \"" + m_osTableName + "\" LIMIT 0";
    long long stmt = h2gis_prepare(thread, conn, (char *)sql.c_str());
    if (!stmt)
        return;

    long long rs = h2gis_execute_prepared(thread, stmt);
    if (!rs)
    {
        h2gis_close_query(thread, stmt);
        return;
    }

    long long sizeOut = 0;
    void *buf = h2gis_fetch_one(thread, rs, &sizeOut);

    if (buf && sizeOut > 0)
    {
        uint8_t *ptr = (uint8_t *)buf;
        int32_t colCount;
        memcpy(&colCount, ptr, 4);
        ptr += 4;

        ptr += 4;  // Skip RowCount (should be 0)

        // Read Offsets
        std::vector<int64_t> offsets(colCount);
        for (int i = 0; i < colCount; i++)
        {
            memcpy(&offsets[i], ptr, 8);
            ptr += 8;
        }

        uint8_t *base = (uint8_t *)buf;

        for (int i = 0; i < colCount; i++)
        {
            uint8_t *colPtr = base + offsets[i];

            int32_t nameLen;
            memcpy(&nameLen, colPtr, 4);
            colPtr += 4;
            std::string colName((char *)colPtr, nameLen);
            colPtr += nameLen;

            int32_t type;
            memcpy(&type, colPtr, 4);

            // Skip FID column (used for FID, not exposed as field)
            if (!m_osFIDCol.empty() &&
                EQUAL(colName.c_str(), m_osFIDCol.c_str()))
            {
                continue;
            }

            if (type == H2GIS_TYPE_GEOM)
            {
                // Only add geometry field if not already present
                if (m_poFeatureDefn->GetGeomFieldCount() == 0)
                {
                    OGRGeomFieldDefn gfd(colName.c_str(), wkbUnknown);
                    m_poFeatureDefn->AddGeomFieldDefn(&gfd);
                }
            }
            else
            {
                // Skip if field already exists in definition
                if (m_poFeatureDefn->GetFieldIndex(colName.c_str()) >= 0)
                {
                    continue;
                }

                OGRFieldType ogrType = OFTString;
                if (type == H2GIS_TYPE_INT)
                    ogrType = OFTInteger;
                else if (type == H2GIS_TYPE_LONG)
                    ogrType = OFTInteger64;
                else if (type == H2GIS_TYPE_FLOAT)
                    ogrType = OFTReal;  // Float32 -> Real
                else if (type == H2GIS_TYPE_DOUBLE)
                    ogrType = OFTReal;
                else if (type == H2GIS_TYPE_DATE)
                    ogrType = OFTDate;
                else if (type == H2GIS_TYPE_BOOL)
                    ogrType = OFTInteger;

                OGRFieldDefn oField(colName.c_str(), ogrType);
                m_poFeatureDefn->AddFieldDefn(&oField);
            }
        }
        h2gis_free_result_buffer(thread, buf);
    }

    // Close FetchSchema handles immediately
    h2gis_close_query(thread, rs);
    h2gis_close_query(thread, stmt);

    // Apply cached SRID
    if (m_poFeatureDefn->GetGeomFieldCount() > 0 && m_nSRID > 0)
    {
        OGRSpatialReference *poSRS = new OGRSpatialReference();
        poSRS->importFromEPSG(m_nSRID);
        m_poFeatureDefn->GetGeomFieldDefn(0)->SetSpatialRef(poSRS);
        poSRS->Release();
    }
}

void OGRH2GISLayer::EnsureSchema()
{
    if (!m_bSchemaFetched)
    {
        LogLayer("EnsureSchema", m_poFeatureDefn->GetName());
        FetchSchema();
        m_bSchemaFetched = true;
    }
}

OGRFeatureDefn *OGRH2GISLayer::GetLayerDefn()
{
    LogLayer("GetLayerDefn", m_poFeatureDefn->GetName());
    // Don't call EnsureSchema() here - it's called on ALL layers when listing
    // Schema will be loaded on first feature read or explicit request
    return m_poFeatureDefn;
}

void OGRH2GISLayer::ResetReading()
{
    // Lazy reset - don't prepare query until first GetNextFeature
    // This avoids expensive SQL queries when QGIS just lists layers
    ClearStatement();
    m_iNextShapeId = 0;
    m_nBatchRows = 0;
    m_iNextRowInBatch = 0;
    m_bResetPending = true;  // Mark that we need to prepare on first read
}

void OGRH2GISLayer::PrepareQuery()
{
    if (!m_bResetPending)
        return;
    m_bResetPending = false;

    // Ensure schema is loaded before building query (needed for geometry column name)
    EnsureSchema();

    LogLayer("PrepareQuery", m_poFeatureDefn->GetName());

    // Use table name for SQL (not layer name which may be TABLE.GEOM_COL)
    std::string sql;
    if (!m_osFIDCol.empty())
    {
        sql = "SELECT * FROM \"" + m_osTableName + "\"";
    }
    else
    {
        sql = "SELECT _ROWID_, * FROM \"" + m_osTableName + "\"";
    }

    // Build WHERE clause combining spatial and attribute filters
    bool bHasWhere = false;

    // Spatial filter (critical for large tables!)
    // Use m_osGeomCol directly - it's set from GEOMETRY_COLUMNS in constructor
    if (m_poFilterGeom != nullptr && !m_osGeomCol.empty())
    {
        OGREnvelope env;
        m_poFilterGeom->getEnvelope(&env);

        // Use && operator (spatial index) AND ST_Intersects (exact check)
        // H2GIS docs state ST_Intersects doesn't always use the index, but && does.
        // Use CPLSPrintf for locale-independent decimal formatting (dot, not comma)
        std::string sEnv = CPLSPrintf("ST_MakeEnvelope(%.15g, %.15g, %.15g, %.15g, %d)",
                                      env.MinX, env.MinY, env.MaxX, env.MaxY,
                                      m_nSRID > 0 ? m_nSRID : 0);

        sql += " WHERE \"" + m_osGeomCol + "\" && " + sEnv +
               " AND ST_Intersects(\"" + m_osGeomCol + "\", " + sEnv + ")";
        bHasWhere = true;

        LogLayer("PrepareQuery with spatial index (&&) + filter",
                 m_osGeomCol.c_str());
    }

    // Attribute filter push-down - send WHERE clause directly to H2GIS
    if (!m_osAttributeFilter.empty())
    {
        if (bHasWhere)
        {
            sql += " AND (" + m_osAttributeFilter + ")";
        }
        else
        {
            sql += " WHERE " + m_osAttributeFilter;
            bHasWhere = true;
        }
        LogLayer("PrepareQuery with attribute filter",
                 m_osAttributeFilter.c_str());
    }

    if (!bHasWhere)
    {
        // NO spatial or attribute filter
        // We handle >1M rows tables by relying on:
        // 1. Correct Geometry Type reporting (prevents QGIS scan)
        // 2. Fast Feature Count from metadata (prevents QGIS COUNT(*))
        // 3. Fast Extent (approximate or sampled)
        LogLayer("PrepareQuery without filters", m_poFeatureDefn->GetName());
    }

    // Add OFFSET for SetNextByIndex support
    if (m_iNextShapeId > 0)
    {
        sql += " OFFSET " + std::to_string(m_iNextShapeId);
        LogLayer("PrepareQuery with OFFSET", std::to_string(m_iNextShapeId).c_str());
    }

    graal_isolatethread_t *thread =
        (graal_isolatethread_t *)m_poDS->GetThread();
    long long conn = m_poDS->GetConnection();

    m_hStmt = h2gis_prepare(thread, conn, (char *)sql.c_str());
    if (m_hStmt)
    {
        m_nRS = h2gis_execute_prepared(thread, m_hStmt);
        if (!m_nRS)
        {
            // Execute failed - close the prepared statement to avoid leak
            h2gis_close_query(thread, m_hStmt);
            m_hStmt = 0;
        }
    }
}

bool OGRH2GISLayer::FetchNextBatch()
{
    if (!m_nRS)
        return false;

    graal_isolatethread_t *thread =
        (graal_isolatethread_t *)m_poDS->GetThread();

    if (m_pBatchBuffer)
    {
        h2gis_free_result_buffer(thread, m_pBatchBuffer);
        m_pBatchBuffer = nullptr;
    }

    long long sizeOut = 0;
    m_pBatchBuffer = h2gis_fetch_batch(thread, m_nRS, 1000, &sizeOut);

    if (!m_pBatchBuffer || sizeOut <= 0)
    {
        return false;
    }

    uint8_t *ptr = (uint8_t *)m_pBatchBuffer;
    int32_t colCount;
    memcpy(&colCount, ptr, 4);
    ptr += 4;
    memcpy(&m_nBatchRows, ptr, 4);
    ptr += 4;

    if (m_nBatchRows <= 0)
        return false;

    std::vector<int64_t> offsets(colCount);
    for (int i = 0; i < colCount; i++)
    {
        memcpy(&offsets[i], ptr, 8);
        ptr += 8;
    }

    m_columnValues.resize(colCount);
    m_columnTypes.resize(colCount);
    m_columnNames.resize(colCount);

    uint8_t *base = (uint8_t *)m_pBatchBuffer;

    for (int i = 0; i < colCount; i++)
    {
        uint8_t *colPtr = base + offsets[i];

        int32_t nameLen;
        memcpy(&nameLen, colPtr, 4);
        colPtr += 4;
        std::string colName((char *)colPtr, nameLen);
        colPtr += nameLen;

        int32_t type;
        memcpy(&type, colPtr, 4);
        colPtr += 4;

        int32_t totalDataLen;
        memcpy(&totalDataLen, colPtr, 4);
        colPtr += 4;

        m_columnValues[i] = colPtr;
        m_columnTypes[i] = type;
        m_columnNames[i] = colName;
    }

    m_iNextRowInBatch = 0;
    return true;
}

OGRFeature *OGRH2GISLayer::GetNextFeature()
{
    // EnsureSchema(); // Already done

    // Lazy preparation
    if (m_bResetPending)
    {
        EnsureSchema();
        PrepareQuery();
    }

    if (!m_nRS)
        ResetReading();

    if (m_iNextRowInBatch >= m_nBatchRows)
    {
        if (!FetchNextBatch())
            return nullptr;
    }

    OGRFeature *poFeature = new OGRFeature(m_poFeatureDefn);

    int iField = 0;
    int iGeom = 0;
    bool fidSet = false;

    for (size_t iCol = 0; iCol < m_columnValues.size(); iCol++)
    {
        int type = m_columnTypes[iCol];
        uint8_t *&ptr = m_columnValues[iCol];
        const std::string &colName = m_columnNames[iCol];

        // Use configured FID column when available; otherwise use _ROWID_
        if (!m_osFIDCol.empty() && EQUAL(colName.c_str(), m_osFIDCol.c_str()))
        {
            if (type == H2GIS_TYPE_LONG)
            {
                int64_t rowid;
                memcpy(&rowid, ptr, 8);
                ptr += 8;
                poFeature->SetFID(rowid);
                fidSet = true;
                continue;
            }
            else if (type == H2GIS_TYPE_INT)
            {
                int32_t rowid;
                memcpy(&rowid, ptr, 4);
                ptr += 4;
                poFeature->SetFID(rowid);
                fidSet = true;
                continue;
            }
        }

        if (m_osFIDCol.empty() && iCol == 0 && type == H2GIS_TYPE_LONG)
        {
            int64_t rowid;
            memcpy(&rowid, ptr, 8);
            ptr += 8;
            poFeature->SetFID(rowid);
            fidSet = true;
            continue;
        }

        if (type == H2GIS_TYPE_GEOM)
        {
            int32_t len;
            memcpy(&len, ptr, 4);
            ptr += 4;

            if (len > 0)
            {
                OGRGeometry *poGeom = nullptr;

                // H2GIS sends EWKB (Extended WKB) with SRID embedded.
                // Format: [byte order 1][type 4 with SRID flag][SRID 4][geometry data]
                // We need to convert EWKB to WKB by removing SRID and clearing the flag.

                // Check if SRID flag is set in type (offset 1-4 after byte order)
                uint8_t byteOrder = ptr[0];
                uint32_t wkbType;
                if (byteOrder == 1)
                {
                    // Little endian
                    memcpy(&wkbType, ptr + 1, 4);
                }
                else
                {
                    // Big endian - swap bytes
                    wkbType = ((uint32_t)ptr[1] << 24) |
                              ((uint32_t)ptr[2] << 16) |
                              ((uint32_t)ptr[3] << 8) | (uint32_t)ptr[4];
                }

                const uint32_t SRID_FLAG = 0x20000000;

                if (wkbType & SRID_FLAG)
                {
                    // EWKB with SRID - convert to standard WKB
                    // New buffer: [byte order 1][type 4 without SRID flag][geometry data]
                    // Skip original SRID bytes (4 bytes after type)
                    int newLen = len - 4;  // 4 bytes less (remove SRID)
                    std::vector<uint8_t> wkbBuf(newLen);

                    // Copy byte order
                    wkbBuf[0] = ptr[0];

                    // Copy type without SRID flag
                    uint32_t newType = wkbType & ~SRID_FLAG;
                    if (byteOrder == 1)
                    {
                        memcpy(&wkbBuf[1], &newType, 4);
                    }
                    else
                    {
                        wkbBuf[1] = (newType >> 24) & 0xFF;
                        wkbBuf[2] = (newType >> 16) & 0xFF;
                        wkbBuf[3] = (newType >> 8) & 0xFF;
                        wkbBuf[4] = newType & 0xFF;
                    }

                    // Copy geometry data (skip 4 bytes of SRID after original type)
                    // Original: [1 byte order][4 type][4 SRID][rest]
                    // New:      [1 byte order][4 type][rest]
                    memcpy(&wkbBuf[5], ptr + 9, len - 9);

                    OGRGeometryFactory::createFromWkb(wkbBuf.data(), nullptr,
                                                      &poGeom, newLen);
                }
                else
                {
                    // Standard WKB
                    OGRGeometryFactory::createFromWkb(ptr, nullptr, &poGeom,
                                                      len);
                }

                if (poGeom)
                    poFeature->SetGeomFieldDirectly(iGeom, poGeom);
            }
            ptr += len;
            iGeom++;
        }
        else if (type == H2GIS_TYPE_STRING)
        {
            int32_t len;
            memcpy(&len, ptr, 4);
            ptr += 4;
            if (len > 0)
            {
                std::string s((char *)ptr, len);
                if (iField < m_poFeatureDefn->GetFieldCount())
                {
                    const char *pszFieldName =
                        m_poFeatureDefn->GetFieldDefn(iField)->GetNameRef();
                    if (!pszFieldName || m_ignoredFields.find(pszFieldName) ==
                                             m_ignoredFields.end())
                    {
                        poFeature->SetField(iField, s.c_str());
                    }
                }
            }
            ptr += len;
            iField++;
        }
        else if (type == H2GIS_TYPE_INT)
        {
            int32_t val;
            memcpy(&val, ptr, 4);
            ptr += 4;
            if (iField < m_poFeatureDefn->GetFieldCount())
            {
                const char *pszFieldName =
                    m_poFeatureDefn->GetFieldDefn(iField)->GetNameRef();
                if (!pszFieldName ||
                    m_ignoredFields.find(pszFieldName) == m_ignoredFields.end())
                {
                    poFeature->SetField(iField, val);
                }
            }
            iField++;
        }
        else if (type == H2GIS_TYPE_LONG)
        {
            int64_t val;
            memcpy(&val, ptr, 8);
            ptr += 8;
            if (iField < m_poFeatureDefn->GetFieldCount())
            {
                const char *pszFieldName =
                    m_poFeatureDefn->GetFieldDefn(iField)->GetNameRef();
                if (!pszFieldName ||
                    m_ignoredFields.find(pszFieldName) == m_ignoredFields.end())
                {
                    poFeature->SetField(iField, (GIntBig)val);
                }
            }
            iField++;
        }
        else if (type == H2GIS_TYPE_DOUBLE)
        {
            double val;
            memcpy(&val, ptr, 8);
            ptr += 8;
            if (iField < m_poFeatureDefn->GetFieldCount())
            {
                const char *pszFieldName =
                    m_poFeatureDefn->GetFieldDefn(iField)->GetNameRef();
                if (!pszFieldName ||
                    m_ignoredFields.find(pszFieldName) == m_ignoredFields.end())
                {
                    poFeature->SetField(iField, val);
                }
            }
            iField++;
        }
        else if (type == H2GIS_TYPE_FLOAT)
        {
            float val;
            memcpy(&val, ptr, 4);
            ptr += 4;
            if (iField < m_poFeatureDefn->GetFieldCount())
            {
                const char *pszFieldName =
                    m_poFeatureDefn->GetFieldDefn(iField)->GetNameRef();
                if (!pszFieldName ||
                    m_ignoredFields.find(pszFieldName) == m_ignoredFields.end())
                {
                    poFeature->SetField(iField, (double)val);
                }
            }
            iField++;
        }
        else if (type == H2GIS_TYPE_BOOL)
        {
            int8_t val;
            memcpy(&val, ptr, 1);
            ptr += 1;
            if (iField < m_poFeatureDefn->GetFieldCount())
            {
                const char *pszFieldName =
                    m_poFeatureDefn->GetFieldDefn(iField)->GetNameRef();
                if (!pszFieldName ||
                    m_ignoredFields.find(pszFieldName) == m_ignoredFields.end())
                {
                    poFeature->SetField(iField, (int)val);
                }
            }
            iField++;
        }
    }

    // Fallback FID if not set from _ROWID_
    if (!fidSet)
    {
        poFeature->SetFID(m_iNextShapeId);
    }
    m_iNextShapeId++;

    m_iNextRowInBatch++;
    return poFeature;
}

OGRFeature *OGRH2GISLayer::GetFeature(GIntBig nFID)
{
    EnsureSchema();

    std::string fidCol =
        m_osFIDCol.empty() ? "_ROWID_" : ("\"" + m_osFIDCol + "\"");
    std::string sql = "SELECT * FROM \"" + m_osTableName + "\" WHERE " +
                      fidCol + " = " + std::to_string(nFID);

    graal_isolatethread_t *thread =
        (graal_isolatethread_t *)m_poDS->GetThread();
    long long conn = m_poDS->GetConnection();

    long long stmt = h2gis_prepare(thread, conn, (char *)sql.c_str());
    if (!stmt)
        return nullptr;

    long long rs = h2gis_execute_prepared(thread, stmt);
    if (!rs)
    {
        h2gis_close_query(thread, stmt);
        return nullptr;
    }

    long long sizeOut = 0;
    void *buffer = h2gis_fetch_one(thread, rs, &sizeOut);

    if (!buffer || sizeOut <= 0)
    {
        if (buffer)
            h2gis_free_result_buffer(thread, buffer);
        h2gis_close_query(thread, rs);
        h2gis_close_query(thread, stmt);
        return nullptr;
    }

    uint8_t *ptr = (uint8_t *)buffer;

    int32_t colCount;
    memcpy(&colCount, ptr, 4);
    ptr += 4;

    int32_t rowCount;
    memcpy(&rowCount, ptr, 4);
    ptr += 4;

    // If no rows returned, the feature doesn't exist
    if (rowCount == 0)
    {
        h2gis_free_result_buffer(thread, buffer);
        h2gis_close_query(thread, rs);
        h2gis_close_query(thread, stmt);
        return nullptr;
    }

    OGRFeature *poFeature = new OGRFeature(m_poFeatureDefn);
    poFeature->SetFID(nFID);

    // Read offsets
    std::vector<int64_t> offsets(colCount);
    for (int i = 0; i < colCount; i++)
    {
        memcpy(&offsets[i], ptr, 8);
        ptr += 8;
    }

    uint8_t *base = (uint8_t *)buffer;

    // Iterate all columns, start from 0
    for (int i = 0; i < colCount; i++)
    {
        uint8_t *colPtr = base + offsets[i];

        // Read column name
        int32_t nameLen;
        memcpy(&nameLen, colPtr, 4);
        colPtr += 4;
        std::string colName((char *)colPtr, nameLen);
        colPtr += nameLen;

        int32_t type;
        memcpy(&type, colPtr, 4);
        colPtr += 4;

        int32_t dataLen;
        memcpy(&dataLen, colPtr, 4);
        colPtr += 4;

        // Skip FID column (already used for FID)
        if (!m_osFIDCol.empty() && EQUAL(colName.c_str(), m_osFIDCol.c_str()))
        {
            continue;
        }

        if (type == H2GIS_TYPE_GEOM)
        {
            if (dataLen > 0)
            {
                int32_t blobLen;
                memcpy(&blobLen, colPtr, 4);
                colPtr += 4;
                if (blobLen > 0)
                {
                    OGRGeometry *poGeom = nullptr;
                    if (OGRGeometryFactory::createFromWkb(
                            colPtr, nullptr, &poGeom, blobLen) == OGRERR_NONE)
                    {
                        if (poGeom)
                        {
                            if (m_nSRID > 0)
                                poGeom->assignSpatialReference(
                                    m_poFeatureDefn->GetGeomFieldDefn(0)
                                        ->GetSpatialRef());
                            poFeature->SetGeometryDirectly(poGeom);
                        }
                    }
                }
            }
        }
        else
        {
            // Find field by name, simpler logic than sequence matching
            int fieldIdx = m_poFeatureDefn->GetFieldIndex(colName.c_str());

            if (fieldIdx < 0)
                continue;
            if (m_ignoredFields.find(colName) != m_ignoredFields.end())
                continue;

            if (type == H2GIS_TYPE_INT)
            {
                int32_t val;
                memcpy(&val, colPtr, 4);
                poFeature->SetField(fieldIdx, val);
            }
            else if (type == H2GIS_TYPE_LONG)
            {
                int64_t val;
                memcpy(&val, colPtr, 8);
                poFeature->SetField(fieldIdx, (GIntBig)val);
            }
            else if (type == H2GIS_TYPE_DOUBLE)
            {
                double val;
                memcpy(&val, colPtr, 8);
                poFeature->SetField(fieldIdx, val);
            }
            else if (type == H2GIS_TYPE_STRING)
            {
                if (dataLen > 0)
                {
                    int32_t strLen;
                    memcpy(&strLen, colPtr, 4);
                    colPtr += 4;
                    if (strLen >= 0)
                    {
                        std::string s((char *)colPtr, strLen);
                        poFeature->SetField(fieldIdx, s.c_str());
                    }
                }
            }
            else if (type == H2GIS_TYPE_BOOL)
            {
                int8_t val;
                memcpy(&val, colPtr, 1);
                poFeature->SetField(fieldIdx, (int)val);
            }
        }
    }

    h2gis_free_result_buffer(thread, buffer);
    h2gis_close_query(thread, rs);
    h2gis_close_query(thread, stmt);

    return poFeature;
}

int OGRH2GISLayer::TestCapability(const char *pszCap)
{
    if (EQUAL(pszCap, OLCCreateField))
        return TRUE;
    if (EQUAL(pszCap, OLCSequentialWrite))
        return TRUE;
    if (EQUAL(pszCap, OLCRandomWrite))
        return TRUE;
    if (EQUAL(pszCap, OLCDeleteFeature))
        return TRUE;
    if (EQUAL(pszCap, OLCStringsAsUTF8))
        return TRUE;
    if (EQUAL(pszCap, OLCFastFeatureCount))
        return TRUE;
    // OLCFastGetExtent: FALSE - we don't cache extent, GetExtent requires bForce=TRUE
    if (EQUAL(pszCap, OLCFastSpatialFilter))
        return TRUE;  // Spatial index supported
    if (EQUAL(pszCap, OLCRandomRead))
        return TRUE;  // GetFeature(FID) implemented
    if (EQUAL(pszCap, OLCTransactions))
        return TRUE;  // Transactions supported
    if (EQUAL(pszCap, OLCIgnoreFields))
        return TRUE;
    if (EQUAL(pszCap, OLCFastSetNextByIndex))
        return TRUE;  // SetNextByIndex with OFFSET supported
    return FALSE;
}

void OGRH2GISLayer::SetSpatialFilter(OGRGeometry *poGeom)
{
    OGRLayer::SetSpatialFilter(poGeom);
    ResetReading();
}

void OGRH2GISLayer::SetSpatialFilter(int iGeom, OGRGeometry *poGeom)
{
    OGRLayer::SetSpatialFilter(iGeom, poGeom);
    ResetReading();
}

OGRErr OGRH2GISLayer::SetNextByIndex(GIntBig nIndex)
{
    if (nIndex < 0)
        return OGRERR_FAILURE;

    // Clear any existing statement
    ClearStatement();

    // Set the starting index - PrepareQuery will add OFFSET
    m_iNextShapeId = nIndex;
    m_bResetPending = true;

    return OGRERR_NONE;
}

OGRErr OGRH2GISLayer::SetAttributeFilter(const char *pszQuery)
{
    // Store the attribute filter for push-down to H2GIS
    if (pszQuery && strlen(pszQuery) > 0)
    {
        m_osAttributeFilter = pszQuery;
    }
    else
    {
        m_osAttributeFilter.clear();
    }

    // Call base class to set m_poAttrQuery (used for fallback filtering)
    OGRErr err = OGRLayer::SetAttributeFilter(pszQuery);
    ResetReading();
    return err;
}

#if GDAL_VERSION_NUM >= 3090000
OGRErr OGRH2GISLayer::SetIgnoredFields(const char *const *papszFields)
#else
OGRErr OGRH2GISLayer::SetIgnoredFields(const char **papszFields)
#endif
{
    m_ignoredFields.clear();
    if (papszFields)
    {
        for (int i = 0; papszFields[i] != nullptr; i++)
        {
            m_ignoredFields.insert(papszFields[i]);
        }
    }
    OGRLayer::SetIgnoredFields(papszFields);
    ResetReading();
    return OGRERR_NONE;
}

GIntBig OGRH2GISLayer::GetFeatureCount(int bForce)
{
    LogLayer("GetFeatureCount", m_poFeatureDefn->GetName());

    // If there's a spatial filter or attribute filter, we need to query with the filter
    bool bHasFilter =
        (m_poFilterGeom != nullptr) || !m_osAttributeFilter.empty();

    if (!bHasFilter)
    {
        // No filters - return pre-cached row count from INFORMATION_SCHEMA
        // m_nFeatureCount was pre-filled in constructor from ROW_COUNT_ESTIMATE
        if (!bForce)
        {
            return m_nFeatureCount;
        }
    }
    else
    {
        // Has filters - need to actually count with WHERE clause
        if (!bForce)
            return -1;  // Indicate we need force to compute
    }

    LogLayer("GetFeatureCount FORCED", m_poFeatureDefn->GetName());

    // Force mode: use SELECT COUNT(*) for exact count, with filters applied
    graal_isolatethread_t *thread =
        (graal_isolatethread_t *)m_poDS->GetThread();
    long long conn = m_poDS->GetConnection();

    // Use table name for SQL (not layer name which may be TABLE.GEOM_COL)
    std::string sql = "SELECT COUNT(*) FROM \"" + m_osTableName + "\"";

    // Apply same filter logic as PrepareQuery()
    bool bHasWhere = false;

    // Spatial filter
    if (m_poFilterGeom != nullptr && !m_osGeomCol.empty())
    {
        OGREnvelope env;
        m_poFilterGeom->getEnvelope(&env);

        // Use CPLSPrintf for locale-independent decimal formatting
        std::string sEnv = CPLSPrintf("ST_MakeEnvelope(%.15g, %.15g, %.15g, %.15g, %d)",
                                      env.MinX, env.MinY, env.MaxX, env.MaxY,
                                      m_nSRID > 0 ? m_nSRID : 0);

        sql += " WHERE \"" + m_osGeomCol + "\" && " + sEnv +
               " AND ST_Intersects(\"" + m_osGeomCol + "\", " + sEnv + ")";
        bHasWhere = true;
    }

    // Attribute filter push-down
    if (!m_osAttributeFilter.empty())
    {
        if (bHasWhere)
        {
            sql += " AND (" + m_osAttributeFilter + ")";
        }
        else
        {
            sql += " WHERE " + m_osAttributeFilter;
        }
    }

    long long stmt = h2gis_prepare(thread, conn, (char *)sql.c_str());
    if (!stmt)
        return m_nFeatureCount;  // Fallback to cached estimate

    long long rs = h2gis_execute_prepared(thread, stmt);
    if (!rs)
    {
        h2gis_close_query(thread, stmt);
        return m_nFeatureCount;
    }

    long long sizeOut = 0;
    void *buffer = h2gis_fetch_one(thread, rs, &sizeOut);

    if (buffer && sizeOut > 0)
    {
        uint8_t *ptr = (uint8_t *)buffer;

        int32_t colCount, rowCount;
        memcpy(&colCount, ptr, 4);
        ptr += 4;
        memcpy(&rowCount, ptr, 4);
        ptr += 4;

        if (rowCount > 0 && colCount > 0)
        {
            // Skip offsets
            ptr += colCount * 8;

            // Read first column value - skip name and type header
            int32_t nameLen;
            memcpy(&nameLen, ptr, 4);
            ptr += 4 + nameLen;

            int32_t type;
            memcpy(&type, ptr, 4);
            ptr += 4;

            int32_t dataLen;
            memcpy(&dataLen, ptr, 4);
            ptr += 4;

            // COUNT(*) returns BIGINT (type 2 = LONG)
            if (type == H2GIS_TYPE_LONG && dataLen >= 8)
            {
                int64_t val;
                memcpy(&val, ptr, 8);
                m_nFeatureCount = val;  // Update cache with exact count
            }
        }

        h2gis_free_result_buffer(thread, buffer);
    }

    h2gis_close_query(thread, rs);
    h2gis_close_query(thread, stmt);

    return m_nFeatureCount;
}

OGRErr OGRH2GISLayer::GetExtent(OGREnvelope *psExtent, int bForce)
{
    return GetExtent(0, psExtent, bForce);
}

OGRErr OGRH2GISLayer::GetExtent(int iGeomField, OGREnvelope *psExtent,
                                int bForce)
{
    LogLayer("GetExtent", m_poFeatureDefn->GetName());

    // If bForce is FALSE and we don't have a cached extent, return FAILURE
    // This is the correct behavior per GDAL API - caller should not expect extent
    // without explicitly forcing computation
    if (!bForce)
    {
        // We don't maintain a cached extent, so return failure
        // This tells GDAL/QGIS that extent is unknown without a full scan
        return OGRERR_FAILURE;
    }

    LogLayer("GetExtent FORCED", m_poFeatureDefn->GetName());

    // Use cached geometry column name - don't need to load full schema
    const char *pszGeomCol = nullptr;
    if (!m_osGeomCol.empty())
    {
        pszGeomCol = m_osGeomCol.c_str();
    }
    else if (m_bSchemaFetched &&
             m_poFeatureDefn->GetGeomFieldCount() > iGeomField)
    {
        pszGeomCol =
            m_poFeatureDefn->GetGeomFieldDefn(iGeomField)->GetNameRef();
    }

    if (!pszGeomCol || strlen(pszGeomCol) == 0)
    {
        pszGeomCol = "THE_GEOM";  // Default H2GIS geometry column name
    }

    graal_isolatethread_t *thread =
        (graal_isolatethread_t *)m_poDS->GetThread();
    long long conn = m_poDS->GetConnection();

    // Sample first 10000 features for extent estimation (fast)
    // Use table name for SQL (not layer name which may be TABLE.GEOM_COL)
    std::string sql = "SELECT ST_XMin(\"" + std::string(pszGeomCol) +
                      "\"), "
                      "ST_YMin(\"" +
                      std::string(pszGeomCol) +
                      "\"), "
                      "ST_XMax(\"" +
                      std::string(pszGeomCol) +
                      "\"), "
                      "ST_YMax(\"" +
                      std::string(pszGeomCol) +
                      "\") "
                      "FROM \"" +
                      m_osTableName +
                      "\" "
                      "WHERE \"" +
                      std::string(pszGeomCol) +
                      "\" IS NOT NULL "
                      "LIMIT 10000";

    long long stmt = h2gis_prepare(thread, conn, (char *)sql.c_str());
    if (!stmt)
    {
        // Fallback: don't compute extent
        psExtent->MinX = 0;
        psExtent->MinY = 0;
        psExtent->MaxX = 0;
        psExtent->MaxY = 0;
        return OGRERR_NONE;  // Return success with empty extent
    }

    long long rs = h2gis_execute_prepared(thread, stmt);
    if (!rs)
    {
        h2gis_close_query(thread, stmt);
        psExtent->MinX = 0;
        psExtent->MinY = 0;
        psExtent->MaxX = 0;
        psExtent->MaxY = 0;
        return OGRERR_NONE;
    }

    double minX = std::numeric_limits<double>::max();
    double minY = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double maxY = std::numeric_limits<double>::lowest();
    int nCount = 0;

    long long sizeOut = 0;
    void *buffer = nullptr;

    // Fetch batches of results
    while ((buffer = h2gis_fetch_batch(thread, rs, 1000, &sizeOut)) !=
               nullptr &&
           sizeOut > 0)
    {
        uint8_t *ptr = (uint8_t *)buffer;

        int32_t colCount, rowCount;
        memcpy(&colCount, ptr, 4);
        ptr += 4;
        memcpy(&rowCount, ptr, 4);
        ptr += 4;

        if (rowCount <= 0 || colCount < 4)
        {
            h2gis_free_result_buffer(thread, buffer);
            break;
        }

        // Read column offsets
        std::vector<int64_t> offsets(colCount);
        for (int i = 0; i < colCount; i++)
        {
            memcpy(&offsets[i], ptr, 8);
            ptr += 8;
        }

        uint8_t *base = (uint8_t *)buffer;

        // Setup column value pointers (skip headers)
        std::vector<uint8_t *> colPtrs(colCount);
        for (int i = 0; i < colCount; i++)
        {
            uint8_t *colPtr = base + offsets[i];
            int32_t nameLen;
            memcpy(&nameLen, colPtr, 4);
            colPtr += 4 + nameLen;
            colPtr += 4;  // Skip type
            colPtr += 4;  // Skip dataLen
            colPtrs[i] = colPtr;
        }

        // Process each row
        for (int row = 0; row < rowCount; row++)
        {
            double vals[4];
            bool valid = true;
            for (int col = 0; col < 4 && valid; col++)
            {
                double val;
                memcpy(&val, colPtrs[col], 8);
                colPtrs[col] += 8;
                if (std::isnan(val) || std::isinf(val))
                {
                    valid = false;
                }
                else
                {
                    vals[col] = val;
                }
            }

            if (valid)
            {
                if (vals[0] < minX)
                    minX = vals[0];
                if (vals[1] < minY)
                    minY = vals[1];
                if (vals[2] > maxX)
                    maxX = vals[2];
                if (vals[3] > maxY)
                    maxY = vals[3];
                nCount++;
            }
        }

        h2gis_free_result_buffer(thread, buffer);

        // Limit total features processed
        if (nCount >= 10000)
            break;
    }

    h2gis_close_query(thread, rs);
    h2gis_close_query(thread, stmt);

    if (nCount > 0)
    {
        psExtent->MinX = minX;
        psExtent->MinY = minY;
        psExtent->MaxX = maxX;
        psExtent->MaxY = maxY;
        return OGRERR_NONE;
    }

    // No valid features found
    psExtent->MinX = 0;
    psExtent->MinY = 0;
    psExtent->MaxX = 0;
    psExtent->MaxY = 0;
    return OGRERR_NONE;
}

#if GDAL_VERSION_NUM >= 3090000
OGRErr OGRH2GISLayer::CreateField(const OGRFieldDefn *poField, int bApproxOK)
#else
OGRErr OGRH2GISLayer::CreateField(OGRFieldDefn *poField, int bApproxOK)
#endif
{
    // ALTER TABLE ADD COLUMN
    // Use table name for SQL (not layer name which may be TABLE.GEOM_COL)
    std::string sql = "ALTER TABLE \"" + m_osTableName + "\" ADD COLUMN \"";
    sql += poField->GetNameRef();
    sql += "\" ";

    switch (poField->GetType())
    {
        case OFTInteger:
            sql += "INT";
            break;
        case OFTInteger64:
            sql += "BIGINT";
            break;
        case OFTReal:
            sql += "DOUBLE";
            break;
        case OFTString:
            sql += "VARCHAR";
            break;  // Limit?
        case OFTDate:
            sql += "DATE";
            break;
        case OFTTime:
            sql += "TIME";
            break;
        case OFTDateTime:
            sql += "TIMESTAMP";
            break;
        case OFTBinary:
            sql += "VARBINARY";
            break;
        default:
            if (!bApproxOK)
                return OGRERR_FAILURE;
            sql += "VARCHAR";
            break;
    }

    graal_isolatethread_t *thread =
        (graal_isolatethread_t *)m_poDS->GetThread();
    long long conn = m_poDS->GetConnection();

    if (h2gis_execute(thread, conn, (char *)sql.c_str()) < 0)
    {
        return OGRERR_FAILURE;
    }

    m_poFeatureDefn->AddFieldDefn(poField);
    return OGRERR_NONE;
}

OGRErr OGRH2GISLayer::ICreateFeature(OGRFeature *poFeature)
{
    // INSERT INTO "Table" (Fields...) VALUES (Values...)
    // Use table name for SQL (not layer name which may be TABLE.GEOM_COL)

    bool bReturnID = (poFeature->GetFID() == OGRNullFID);
    std::string sql;

    const std::string fidColName = m_osFIDCol.empty() ? "ID" : m_osFIDCol;
    if (bReturnID)
    {
        // H2 syntax for returning keys: SELECT <FID> FROM FINAL TABLE (INSERT ...)
        sql = "SELECT \"" + fidColName + "\" FROM FINAL TABLE (INSERT INTO \"" +
              m_osTableName + "\" (";
    }
    else
    {
        sql = "INSERT INTO \"" + m_osTableName + "\" (";
    }

    std::string values = "VALUES (";

    bool first = true;

    // 1. FID
    if (poFeature->GetFID() != OGRNullFID)
    {
        sql += "\"" + fidColName + "\"";
        values += std::to_string(poFeature->GetFID());
        first = false;
    }

    // 2. Geometry
    if (poFeature->GetGeometryRef() != nullptr)
    {
        if (!first)
        {
            sql += ", ";
            values += ", ";
        }

        std::string geomName = "GEOM";
        if (m_poFeatureDefn->GetGeomFieldCount() > 0)
        {
            const char *name =
                m_poFeatureDefn->GetGeomFieldDefn(0)->GetNameRef();
            if (name && strlen(name) > 0)
                geomName = std::string(name);
        }

        sql += "\"" + geomName + "\"";

        // WKB Version for better precision
        OGRGeometry *poGeom = poFeature->GetGeometryRef();
        int nWkbSize = poGeom->WkbSize();
        unsigned char *pabyWkb = (unsigned char *)CPLMalloc(nWkbSize);

        if (poGeom->exportToWkb(wkbNDR, pabyWkb) == OGRERR_NONE)
        {
            char *pszHex = CPLBinaryToHex(nWkbSize, pabyWkb);

            int nSRID = 0;
            if (m_poFeatureDefn->GetGeomFieldCount() > 0)
            {
                const OGRSpatialReference *poSRS =
                    m_poFeatureDefn->GetGeomFieldDefn(0)->GetSpatialRef();
                if (poSRS)
                {
                    const char *pszAuthName = poSRS->GetAuthorityName(nullptr);
                    const char *pszAuthCode = poSRS->GetAuthorityCode(nullptr);
                    if (pszAuthName && EQUAL(pszAuthName, "EPSG") &&
                        pszAuthCode)
                    {
                        nSRID = atoi(pszAuthCode);
                    }
                }
            }

            if (nSRID > 0)
            {
                values += "ST_GeomFromWKB(X'";
                values += pszHex;
                values += "', " + std::to_string(nSRID) + ")";
            }
            else
            {
                values += "X'";
                values += pszHex;
                values += "'";
            }
            CPLFree(pszHex);
        }
        else
        {
            values += "NULL";
        }
        CPLFree(pabyWkb);

        first = false;
    }

    // 3. Attributes
    int fieldCount = m_poFeatureDefn->GetFieldCount();
    for (int i = 0; i < fieldCount; i++)
    {
        if (!poFeature->IsFieldSet(i))
            continue;

        OGRFieldDefn *poFDefn = m_poFeatureDefn->GetFieldDefn(i);

        // Skip FID field as it is handled by the FID block above
        // or auto-incremented if FID is NULL
        if (!m_osFIDCol.empty() &&
            EQUAL(poFDefn->GetNameRef(), m_osFIDCol.c_str()))
            continue;

        if (!first)
        {
            sql += ", ";
            values += ", ";
        }

        sql += "\"";
        sql += poFDefn->GetNameRef();
        sql += "\"";

        switch (poFDefn->GetType())
        {
            case OFTInteger:
                values += std::to_string(poFeature->GetFieldAsInteger(i));
                break;
            case OFTInteger64:
                values += std::to_string(poFeature->GetFieldAsInteger64(i));
                break;
            case OFTReal:
                // Use CPLSPrintf for locale-independent decimal formatting
                values += CPLSPrintf("%.15g", poFeature->GetFieldAsDouble(i));
                break;
            case OFTString:
            {
                std::string s = poFeature->GetFieldAsString(i);
                // Escape single quote
                std::string escaped;
                for (char c : s)
                {
                    if (c == '\'')
                        escaped += "''";
                    else
                        escaped += c;
                }
                values += "'";
                values += escaped;
                values += "'";
                break;
            }
            case OFTDate:
            {
                // H2 expects YYYY-MM-DD format
                int year, month, day, hour, minute, tzflag;
                float second;
                poFeature->GetFieldAsDateTime(i, &year, &month, &day, &hour, &minute, &second, &tzflag);
                values += CPLSPrintf("'%04d-%02d-%02d'", year, month, day);
                break;
            }
            case OFTTime:
            {
                // H2 expects HH:MM:SS format
                int year, month, day, hour, minute, tzflag;
                float second;
                poFeature->GetFieldAsDateTime(i, &year, &month, &day, &hour, &minute, &second, &tzflag);
                values += CPLSPrintf("'%02d:%02d:%02d'", hour, minute, (int)second);
                break;
            }
            case OFTDateTime:
            {
                // H2 expects YYYY-MM-DD HH:MM:SS format
                int year, month, day, hour, minute, tzflag;
                float second;
                poFeature->GetFieldAsDateTime(i, &year, &month, &day, &hour, &minute, &second, &tzflag);
                values += CPLSPrintf("'%04d-%02d-%02d %02d:%02d:%02d'", year, month, day, hour, minute, (int)second);
                break;
            }
            default:
                values += "'";
                values += poFeature->GetFieldAsString(i);
                values += "'";
                break;
        }
        first = false;
    }

    sql += ") " + values + ")";

    if (bReturnID)
    {
        sql += ")";  // Close FINAL TABLE parens
    }

    graal_isolatethread_t *thread =
        (graal_isolatethread_t *)m_poDS->GetThread();
    long long conn = m_poDS->GetConnection();

    if (bReturnID)
    {
        long long hStmt = h2gis_prepare(thread, conn, (char *)sql.c_str());
        if (!hStmt)
            return OGRERR_FAILURE;
        long long hRS = h2gis_execute_prepared(thread, hStmt);
        if (!hRS)
        {
            h2gis_close_query(thread, hStmt);
            return OGRERR_FAILURE;
        }

        long long sizeOut = 0;
        void *pData = h2gis_fetch_one(thread, hRS, &sizeOut);

        if (pData && sizeOut > 0)
        {
            uint8_t *ptr = (uint8_t *)pData;
            // Skip col count (4 bytes)
            ptr += 4;

            // 1. Col Name
            int32_t nameLen;
            memcpy(&nameLen, ptr, 4);
            ptr += 4;
            ptr += nameLen;

            // 2. Type
            int32_t type;
            memcpy(&type, ptr, 4);
            ptr += 4;

            if (type == H2GIS_TYPE_LONG)
            {
                int64_t val;
                memcpy(&val, ptr, 8);
                poFeature->SetFID((GIntBig)val);
            }
            else if (type == H2GIS_TYPE_INT)
            {
                int32_t val;
                memcpy(&val, ptr, 4);
                poFeature->SetFID((GIntBig)val);
            }

            h2gis_free_result_buffer(thread, pData);
        }
        else
        {
            // Failed to get ID
        }
        h2gis_close_query(thread, hRS);
        h2gis_close_query(thread, hStmt);
    }
    else
    {
        if (h2gis_execute(thread, conn, (char *)sql.c_str()) < 0)
        {
            return OGRERR_FAILURE;
        }
    }

    return OGRERR_NONE;
}

OGRErr OGRH2GISLayer::ISetFeature(OGRFeature *poFeature)
{
    if (poFeature->GetFID() == OGRNullFID)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "SetFeature: FID required for update");
        return OGRERR_FAILURE;
    }

    // UPDATE "Table" SET col1=val1, col2=val2, ... WHERE ID = fid
    // Use table name for SQL (not layer name which may be TABLE.GEOM_COL)
    std::string sql = "UPDATE \"" + m_osTableName + "\" SET ";

    bool first = true;

    // 1. Geometry
    if (poFeature->GetGeometryRef() != nullptr)
    {
        std::string geomName = "GEOM";
        if (m_poFeatureDefn->GetGeomFieldCount() > 0)
        {
            const char *name =
                m_poFeatureDefn->GetGeomFieldDefn(0)->GetNameRef();
            if (name && strlen(name) > 0)
                geomName = std::string(name);
        }

        sql += "\"" + geomName + "\" = ";

        OGRGeometry *poGeom = poFeature->GetGeometryRef();
        int nWkbSize = poGeom->WkbSize();
        unsigned char *pabyWkb = (unsigned char *)CPLMalloc(nWkbSize);

        if (poGeom->exportToWkb(wkbNDR, pabyWkb) == OGRERR_NONE)
        {
            char *pszHex = CPLBinaryToHex(nWkbSize, pabyWkb);

            int nSRID = 0;
            if (m_poFeatureDefn->GetGeomFieldCount() > 0)
            {
                const OGRSpatialReference *poSRS =
                    m_poFeatureDefn->GetGeomFieldDefn(0)->GetSpatialRef();
                if (poSRS)
                {
                    const char *pszAuthName = poSRS->GetAuthorityName(nullptr);
                    const char *pszAuthCode = poSRS->GetAuthorityCode(nullptr);
                    if (pszAuthName && EQUAL(pszAuthName, "EPSG") &&
                        pszAuthCode)
                    {
                        nSRID = atoi(pszAuthCode);
                    }
                }
            }

            if (nSRID > 0)
            {
                sql += "ST_GeomFromWKB(X'";
                sql += pszHex;
                sql += "', " + std::to_string(nSRID) + ")";
            }
            else
            {
                sql += "X'";
                sql += pszHex;
                sql += "'";
            }
            CPLFree(pszHex);
        }
        else
        {
            sql += "NULL";
        }
        CPLFree(pabyWkb);

        first = false;
    }

    // 2. Attributes
    int fieldCount = m_poFeatureDefn->GetFieldCount();
    for (int i = 0; i < fieldCount; i++)
    {
        OGRFieldDefn *poFDefn = m_poFeatureDefn->GetFieldDefn(i);

        // Skip FID field (it's the primary key)
        if (!m_osFIDCol.empty() &&
            EQUAL(poFDefn->GetNameRef(), m_osFIDCol.c_str()))
            continue;

        if (!first)
            sql += ", ";

        sql += "\"";
        sql += poFDefn->GetNameRef();
        sql += "\" = ";

        if (!poFeature->IsFieldSet(i) || poFeature->IsFieldNull(i))
        {
            sql += "NULL";
        }
        else
        {
            switch (poFDefn->GetType())
            {
                case OFTInteger:
                    sql += std::to_string(poFeature->GetFieldAsInteger(i));
                    break;
                case OFTInteger64:
                    sql += std::to_string(poFeature->GetFieldAsInteger64(i));
                    break;
                case OFTReal:
                    // Use CPLSPrintf for locale-independent decimal formatting
                    sql += CPLSPrintf("%.15g", poFeature->GetFieldAsDouble(i));
                    break;
                case OFTString:
                {
                    std::string s = poFeature->GetFieldAsString(i);
                    std::string escaped;
                    for (char c : s)
                    {
                        if (c == '\'')
                            escaped += "''";
                        else
                            escaped += c;
                    }
                    sql += "'";
                    sql += escaped;
                    sql += "'";
                    break;
                }
                case OFTDate:
                {
                    int year, month, day, hour, minute, tzflag;
                    float second;
                    poFeature->GetFieldAsDateTime(i, &year, &month, &day, &hour, &minute, &second, &tzflag);
                    sql += CPLSPrintf("'%04d-%02d-%02d'", year, month, day);
                    break;
                }
                case OFTTime:
                {
                    int year, month, day, hour, minute, tzflag;
                    float second;
                    poFeature->GetFieldAsDateTime(i, &year, &month, &day, &hour, &minute, &second, &tzflag);
                    sql += CPLSPrintf("'%02d:%02d:%02d'", hour, minute, (int)second);
                    break;
                }
                case OFTDateTime:
                {
                    int year, month, day, hour, minute, tzflag;
                    float second;
                    poFeature->GetFieldAsDateTime(i, &year, &month, &day, &hour, &minute, &second, &tzflag);
                    sql += CPLSPrintf("'%04d-%02d-%02d %02d:%02d:%02d'", year, month, day, hour, minute, (int)second);
                    break;
                }
                default:
                    sql += "'";
                    sql += poFeature->GetFieldAsString(i);
                    sql += "'";
                    break;
            }
        }
        first = false;
    }

    std::string fidCol =
        m_osFIDCol.empty() ? "_ROWID_" : ("\"" + m_osFIDCol + "\"");
    sql += " WHERE " + fidCol + " = " + std::to_string(poFeature->GetFID());

    graal_isolatethread_t *thread =
        (graal_isolatethread_t *)m_poDS->GetThread();
    long long conn = m_poDS->GetConnection();

    if (h2gis_execute(thread, conn, (char *)sql.c_str()) < 0)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "SetFeature: SQL execution failed");
        return OGRERR_FAILURE;
    }

    return OGRERR_NONE;
}

OGRErr OGRH2GISLayer::DeleteFeature(GIntBig nFID)
{
    // Use table name for SQL (not layer name which may be TABLE.GEOM_COL)
    std::string fidCol =
        m_osFIDCol.empty() ? "_ROWID_" : ("\"" + m_osFIDCol + "\"");
    std::string sql = "DELETE FROM \"" + m_osTableName + "\" WHERE " + fidCol +
                      " = " + std::to_string(nFID);

    graal_isolatethread_t *thread =
        (graal_isolatethread_t *)m_poDS->GetThread();
    long long conn = m_poDS->GetConnection();

    if (h2gis_execute(thread, conn, (char *)sql.c_str()) < 0)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "DeleteFeature: SQL execution failed");
        return OGRERR_FAILURE;
    }

    return OGRERR_NONE;
}
