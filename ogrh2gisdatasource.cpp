// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2024-2026 H2GIS Team

#include "ogr_h2gis.h"
#include <cstring>
#include <cstdio>
#include <string>
#include <set>
#include <map>

#include "cpl_error.h"

// Types and functions come from ogr_h2gis.h which includes h2gis_wrapper.h

static void LogDebugDS(const char *msg)
{
    CPLDebug("H2GIS", "[DS] %s", msg);
}

OGRH2GISDataSource::OGRH2GISDataSource()
    : m_pszName(nullptr), m_papoLayers(nullptr), m_nLayers(0),
      m_hConnection(-1), m_hThread(nullptr)
{
}

OGRH2GISDataSource::~OGRH2GISDataSource()
{
    for (int i = 0; i < m_nLayers; i++)
        delete m_papoLayers[i];
    CPLFree(m_papoLayers);
    CPLFree(m_pszName);

    if (m_hThread && m_hConnection >= 0)
    {
        h2gis_close_connection((graal_isolatethread_t *)m_hThread,
                               m_hConnection);
    }
}

// Helpers for buffer parsing (Single Row Context)
static std::string ParseColumnAsString(uint8_t *colPtr, int64_t colOffset)
{
    if (colOffset <= 0)
        return "";
    uint8_t *ptr = colPtr;

    // Skip Name
    int32_t nameLen;
    std::memcpy(&nameLen, ptr, 4);
    ptr += 4 + nameLen;

    // Read Type
    int32_t type;
    std::memcpy(&type, ptr, 4);
    ptr += 4;

    // Read Data Len
    int32_t dLen;
    std::memcpy(&dLen, ptr, 4);
    ptr += 4;

    if (type == H2GIS_TYPE_STRING && dLen >= 4)
    {
        int32_t strLen;
        std::memcpy(&strLen, ptr, 4);
        ptr += 4;
        if (strLen > 0 && strLen <= dLen - 4)
        {
            return std::string((char *)ptr, strLen);
        }
    }
    return "";
}

static int ParseColumnAsInt(uint8_t *colPtr, int64_t colOffset)
{
    if (colOffset <= 0)
        return 0;
    uint8_t *ptr = colPtr;

    // Skip Name
    int32_t nameLen;
    std::memcpy(&nameLen, ptr, 4);
    ptr += 4 + nameLen;

    // Read Type
    int32_t type;
    std::memcpy(&type, ptr, 4);
    ptr += 4;

    // Read Data Len
    int32_t dLen;
    std::memcpy(&dLen, ptr, 4);
    ptr += 4;

    if (type == H2GIS_TYPE_INT && dLen >= 4)
    {
        int32_t val;
        std::memcpy(&val, ptr, 4);
        return val;
    }
    // Also handle LONG (BIGINT) type - SRID may be returned as BIGINT
    if (type == H2GIS_TYPE_LONG && dLen >= 8)
    {
        int64_t val;
        std::memcpy(&val, ptr, 8);
        return (int)val;  // Safe truncation - SRIDs are small integers
    }
    return 0;
}

class OGRH2GISResultLayer final : public OGRLayer
{
    OGRH2GISDataSource *m_poDS;
    OGRFeatureDefn *m_poFeatureDefn;
    std::string m_osSQL;
    long long m_nRS;
    long long m_hStmt;
    void *m_pBatchBuffer;
    int m_nBatchRows;
    int m_iNextRowInBatch;
    GIntBig m_iNextFID;
    std::vector<uint8_t *> m_columnValues;
    std::vector<int> m_columnTypes;
    std::vector<std::string> m_columnNames;

  public:
    OGRH2GISResultLayer(OGRH2GISDataSource *poDS, const char *pszSQL)
        : m_poDS(poDS), m_poFeatureDefn(new OGRFeatureDefn("Result")),
          m_osSQL(pszSQL), m_nRS(0), m_hStmt(0), m_pBatchBuffer(nullptr),
          m_nBatchRows(0), m_iNextRowInBatch(0), m_iNextFID(0)
    {
        SetDescription(m_poFeatureDefn->GetName());
        m_poFeatureDefn->Reference();

        graal_isolatethread_t *thread =
            (graal_isolatethread_t *)m_poDS->GetThread();
        long long conn = m_poDS->GetConnection();
        m_hStmt = h2gis_prepare(thread, conn, (char *)m_osSQL.c_str());
        if (m_hStmt)
        {
            m_nRS = h2gis_execute_prepared(thread, m_hStmt);
            if (m_nRS)
                BuildFeatureDefn();
        }
    }

    ~OGRH2GISResultLayer()
    {
        ClearStatement();
        m_poFeatureDefn->Release();
    }

    void ClearStatement()
    {
        graal_isolatethread_t *thread =
            (graal_isolatethread_t *)m_poDS->GetThread();
        if (m_pBatchBuffer)
            h2gis_free_result_buffer(thread, m_pBatchBuffer);
        if (m_nRS)
            h2gis_close_query(thread, m_nRS);
        if (m_hStmt)
            h2gis_close_query(thread, m_hStmt);
        m_nRS = 0;
        m_hStmt = 0;
        m_pBatchBuffer = nullptr;
    }

    void BuildFeatureDefn()
    {
        graal_isolatethread_t *thread =
            (graal_isolatethread_t *)m_poDS->GetThread();
        long long sizeOut = 0;
        void *buf = h2gis_get_column_types(thread, m_nRS, &sizeOut);
        if (buf && sizeOut > 0)
        {
            uint8_t *ptr = (uint8_t *)buf;
            int32_t colCount;
            memcpy(&colCount, ptr, 4);
            ptr += 4;

            m_columnNames.clear();
            m_columnNames.reserve(colCount);

            for (int i = 0; i < colCount; i++)
            {
                int32_t nameLen;
                memcpy(&nameLen, ptr, 4);
                ptr += 4;
                std::string colName((char *)ptr, nameLen);
                ptr += nameLen;
                int32_t type;
                memcpy(&type, ptr, 4);
                ptr += 4;
                m_columnNames.push_back(colName);

                if (type == H2GIS_TYPE_GEOM)
                {
                    OGRGeomFieldDefn gfd(colName.c_str(), wkbUnknown);
                    m_poFeatureDefn->AddGeomFieldDefn(&gfd);
                }
                else
                {
                    OGRFieldType ogrType = OFTString;
                    if (type == H2GIS_TYPE_INT || type == H2GIS_TYPE_BOOL)
                        ogrType = OFTInteger;
                    else if (type == H2GIS_TYPE_LONG)
                        ogrType = OFTInteger64;
                    else if (type == H2GIS_TYPE_DOUBLE ||
                             type == H2GIS_TYPE_FLOAT)
                        ogrType = OFTReal;
                    else if (type == H2GIS_TYPE_DATE)
                        ogrType = OFTDate;

                    OGRFieldDefn field(colName.c_str(), ogrType);
                    m_poFeatureDefn->AddFieldDefn(&field);
                }
            }
            h2gis_free_result_buffer(thread, buf);
        }
    }

    bool FetchNextBatch()
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
            return false;

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

            if (m_columnNames.size() != static_cast<size_t>(colCount))
            {
                if (m_columnNames.size() < static_cast<size_t>(colCount))
                    m_columnNames.resize(colCount);
                m_columnNames[i] = colName;
            }

            m_columnValues[i] = colPtr;
            m_columnTypes[i] = type;
        }

        m_iNextRowInBatch = 0;
        return true;
    }

    void ResetReading() override
    {
        ClearStatement();
        graal_isolatethread_t *thread =
            (graal_isolatethread_t *)m_poDS->GetThread();
        long long conn = m_poDS->GetConnection();
        m_hStmt = h2gis_prepare(thread, conn, (char *)m_osSQL.c_str());
        if (m_hStmt)
            m_nRS = h2gis_execute_prepared(thread, m_hStmt);
        m_nBatchRows = 0;
        m_iNextRowInBatch = 0;
        m_iNextFID = 0;
    }

#if GDAL_VERSION_NUM >= 3120000
    const OGRFeatureDefn *GetLayerDefn() const override
    {
        return m_poFeatureDefn;
    }

    int TestCapability(const char *) const override
    {
        return FALSE;
    }
#else
    OGRFeatureDefn *GetLayerDefn() override
    {
        return m_poFeatureDefn;
    }

    int TestCapability(const char *) override
    {
        return FALSE;
    }
#endif

    OGRFeature *GetNextFeature() override
    {
        if (!m_nRS)
            return nullptr;
        graal_isolatethread_t *thread =
            (graal_isolatethread_t *)m_poDS->GetThread();

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

            const std::string &colName = (iCol < m_columnNames.size())
                                             ? m_columnNames[iCol]
                                             : std::string();

            if (!colName.empty() && EQUAL(colName.c_str(), "_ROWID_") &&
                type == H2GIS_TYPE_LONG)
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
                    OGRGeometryFactory::createFromWkb(ptr, nullptr, &poGeom,
                                                      len);
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
                    poFeature->SetField(iField, s.c_str());
                }
                ptr += len;
                iField++;
            }
            else if (type == H2GIS_TYPE_INT)
            {
                int32_t val;
                memcpy(&val, ptr, 4);
                ptr += 4;
                poFeature->SetField(iField, val);
                iField++;
            }
            else if (type == H2GIS_TYPE_LONG)
            {
                int64_t val;
                memcpy(&val, ptr, 8);
                ptr += 8;
                poFeature->SetField(iField, (GIntBig)val);
                iField++;
            }
            else if (type == H2GIS_TYPE_DOUBLE)
            {
                double val;
                memcpy(&val, ptr, 8);
                ptr += 8;
                poFeature->SetField(iField, val);
                iField++;
            }
            else if (type == H2GIS_TYPE_FLOAT)
            {
                float val;
                memcpy(&val, ptr, 4);
                ptr += 4;
                poFeature->SetField(iField, (double)val);
                iField++;
            }
            else if (type == H2GIS_TYPE_BOOL)
            {
                int8_t val;
                memcpy(&val, ptr, 1);
                ptr += 1;
                poFeature->SetField(iField, (int)val);
                iField++;
            }
            else
            {
                int32_t len;
                memcpy(&len, ptr, 4);
                ptr += 4;
                if (len > 0)
                    ptr += len;
                iField++;
            }
        }

        if (!fidSet)
        {
            poFeature->SetFID(m_iNextFID);
        }
        m_iNextFID++;
        m_iNextRowInBatch++;
        return poFeature;
    }
};

int OGRH2GISDataSource::Open(const char *pszFilename, int bUpdate,
                             const char *pszUser, const char *pszPassword)
{
    LogDebugDS("Open() Called");

    // Ignore bUpdate for now
    if (!pszFilename || strlen(pszFilename) == 0)
    {
        return FALSE;
    }
    m_pszName = CPLStrdup(pszFilename);

    // Get thread handle from global GraalVM (initialized on main thread)
    graal_isolatethread_t *thread = GetOrAttachThread();
    if (!thread)
    {
        LogDebugDS("Failed to get GraalVM thread handle");
        CPLError(CE_Failure, CPLE_AppDefined,
                 "H2GIS: GraalVM not initialized or thread attach failed");
        return FALSE;
    }
    m_hThread = thread;

    char ptrStr[64];
    snprintf(ptrStr, sizeof(ptrStr), "IsolateThread Ptr: %p", (void *)thread);
    LogDebugDS(ptrStr);

    // Parse URI for credentials: H2GIS:/path/db.mv.db?user=xxx&password=yyy
    // Also support GDAL-style: /path/db.mv.db|user=xxx|password=yyy
    std::string uriUser, uriPass;
    std::string path(pszFilename);

    // Check for query string format (?user=...&password=...)
    size_t qPos = path.find('?');
    if (qPos != std::string::npos)
    {
        std::string params = path.substr(qPos + 1);
        path = path.substr(0, qPos);

        size_t pos = 0;
        while (pos < params.size())
        {
            size_t ampPos = params.find('&', pos);
            if (ampPos == std::string::npos)
                ampPos = params.size();

            std::string kv = params.substr(pos, ampPos - pos);
            size_t eqPos = kv.find('=');
            if (eqPos != std::string::npos)
            {
                std::string key = kv.substr(0, eqPos);
                std::string val = kv.substr(eqPos + 1);

                if (key == "user" || key == "username")
                    uriUser = val;
                else if (key == "password" || key == "pass")
                    uriPass = val;
            }
            pos = ampPos + 1;
        }
    }

    // Check for pipe format (|user=...|password=...) - GDAL style
    size_t pipePos = path.find('|');
    if (pipePos != std::string::npos)
    {
        std::string params = path.substr(pipePos);
        path = path.substr(0, pipePos);

        size_t pos = 1;  // Skip first pipe
        while (pos < params.size())
        {
            size_t nextPipe = params.find('|', pos);
            if (nextPipe == std::string::npos)
                nextPipe = params.size();

            std::string kv = params.substr(pos, nextPipe - pos);
            size_t eqPos = kv.find('=');
            if (eqPos != std::string::npos)
            {
                std::string key = kv.substr(0, eqPos);
                std::string val = kv.substr(eqPos + 1);

                if (key == "user" || key == "username")
                    uriUser = val;
                else if (key == "password" || key == "pass")
                    uriPass = val;
            }
            pos = nextPipe + 1;
        }
    }

    // Auto-append .mv.db if missing extension
    const std::string mvSuffix = ".mv.db";
    const std::string dbSuffix = ".db";
    if (path.size() < mvSuffix.size() ||
        path.compare(path.size() - mvSuffix.size(), mvSuffix.size(),
                     mvSuffix) != 0)
    {
        if (path.size() < dbSuffix.size() ||
            path.compare(path.size() - dbSuffix.size(), dbSuffix.size(),
                         dbSuffix) != 0)
        {
            path += mvSuffix;
        }
    }

    // Enforce .mv.db/.db extension after normalization
    const bool hasMvDb = path.size() >= mvSuffix.size() &&
                         path.compare(path.size() - mvSuffix.size(),
                                      mvSuffix.size(), mvSuffix) == 0;
    const bool hasDb = EQUAL(CPLGetExtension(path.c_str()), "db");
    if (!hasDb && !hasMvDb)
    {
        return FALSE;
    }

    CPLFree(m_pszName);
    m_pszName = CPLStrdup(path.c_str());

    // Strip .mv.db for connection string
    const std::string suffix1 = ".mv.db";
    const std::string suffix2 = ".db";

    if (path.size() >= suffix1.size() &&
        path.compare(path.size() - suffix1.size(), suffix1.size(), suffix1) ==
            0)
    {
        path = path.substr(0, path.size() - suffix1.size());
    }
    else if (path.size() >= suffix2.size() &&
             path.compare(path.size() - suffix2.size(), suffix2.size(),
                          suffix2) == 0)
    {
        path = path.substr(0, path.size() - suffix2.size());
    }

    LogDebugDS(std::string("Connecting to: " + path).c_str());

    // Priority for credentials (highest to lowest):
    // 1. GDAL Open Options (pszUser, pszPassword) - from Data Source Manager
    // 2. URI parameters (?user=... or |user=...)
    // 3. Environment variables (H2GIS_USER, H2GIS_PASSWORD)
    std::string optUser = pszUser ? pszUser : "";
    std::string optPass = pszPassword ? pszPassword : "";
    std::string envUser = CPLGetConfigOption("H2GIS_USER", "");
    std::string envPass = CPLGetConfigOption("H2GIS_PASSWORD", "");

    // Apply priority: OpenOptions > URI > EnvVars
    std::string finalUser =
        !optUser.empty() ? optUser : (!uriUser.empty() ? uriUser : envUser);
    std::string finalPass =
        !optPass.empty() ? optPass : (!uriPass.empty() ? uriPass : envPass);

    // Defined candidates to try - ORDER MATTERS
    struct Credential
    {
        std::string u;
        std::string p;
    };

    std::vector<Credential> candidates;

    // 1. Try user's explicit credentials FIRST if provided
    if (!finalUser.empty() || !finalPass.empty())
    {
        candidates.push_back({finalUser, finalPass});
    }

    // 2. Try empty credentials (most common for local DBs)
    candidates.push_back({"", ""});

    // 3. Try H2 default (sa, "")
    candidates.push_back({"sa", ""});

    // 4. Try (sa, sa) - some older H2 versions
    candidates.push_back({"sa", "sa"});

    long long conn = -1;

    for (size_t i = 0; i < candidates.size(); i++)
    {
        const auto &cred = candidates[i];
        LogDebugDS((std::string("Attempting connection (") +
                    std::to_string(i + 1) + "/" +
                    std::to_string(candidates.size()) + ") user='" + cred.u +
                    "' pass='" + (cred.p.empty() ? "(empty)" : "****") + "'")
                       .c_str());

        conn = h2gis_connect(thread, (char *)path.c_str(),
                             (char *)cred.u.c_str(), (char *)cred.p.c_str());

        // Assuming 0 is invalid handle (null) and -1 is error
        if (conn != 0 && conn != -1)
        {
            LogDebugDS("Connection successful!");
            m_hConnection = conn;
            break;
        }
        else
        {
            LogDebugDS("Connection failed, trying next...");
        }
    }

    if (m_hConnection < 0)
    {
        CPLError(
            CE_Failure, CPLE_OpenFailed,
            "H2GIS: Connection failed. Database may require authentication.\n"
            "Specify credentials using:\n"
            "  - URI: /path/db.mv.db?user=xxx&password=yyy\n"
            "  - GDAL style: /path/db.mv.db|user=xxx|password=yyy\n"
            "  - Environment: H2GIS_USER and H2GIS_PASSWORD");
        return FALSE;
    }

    // Initialize H2GIS functions
    // This creates the alias and GEOMETRY_COLUMNS if missing.
    LogDebugDS("Initializing H2GIS...");
    h2gis_load(thread, m_hConnection);

    // =======================================================================
    // SINGLE QUERY: Get ALL metadata from INFORMATION_SCHEMA.COLUMNS
    // JOIN with GEOMETRY_COLUMNS to get accurate geometry type/SRID even
    // for unconstrained geometry columns (H2GIS fills GEOMETRY_COLUMNS)
    // =======================================================================
    const char *metaSql =
        "SELECT "
        "  c.TABLE_NAME, c.COLUMN_NAME, c.DATA_TYPE, c.ORDINAL_POSITION, "
        "  COALESCE(g.TYPE, c.GEOMETRY_TYPE) AS GEOMETRY_TYPE, "
        "  COALESCE(g.SRID, c.GEOMETRY_SRID, 0) AS GEOMETRY_SRID, "
        "  t.ROW_COUNT_ESTIMATE "
        "FROM INFORMATION_SCHEMA.COLUMNS c "
        "JOIN INFORMATION_SCHEMA.TABLES t "
        "  ON c.TABLE_NAME = t.TABLE_NAME AND c.TABLE_SCHEMA = t.TABLE_SCHEMA "
        "LEFT JOIN GEOMETRY_COLUMNS g "
        "  ON c.TABLE_NAME = g.F_TABLE_NAME AND c.COLUMN_NAME = "
        "g.F_GEOMETRY_COLUMN "
        "WHERE c.TABLE_SCHEMA = 'PUBLIC' AND t.TABLE_TYPE = 'BASE TABLE' "
        "  AND c.TABLE_NAME NOT IN ('GEOMETRY_COLUMNS', 'SPATIAL_REF_SYS') "
        "ORDER BY c.TABLE_NAME, c.ORDINAL_POSITION";

    LogDebugDS((std::string("Metadata SQL: ") + metaSql).c_str());

    long long stmt = h2gis_prepare(thread, m_hConnection, (char *)metaSql);
    if (!stmt)
    {
        LogDebugDS("INFORMATION_SCHEMA query failed. Opening as empty DB.");
        return TRUE;
    }
    LogDebugDS("Metadata query prepared OK");

    long long qHandle = h2gis_execute_prepared(thread, stmt);
    if (!qHandle)
    {
        h2gis_close_query(thread, stmt);
        LogDebugDS("Metadata query execute failed");
        return TRUE;
    }
    LogDebugDS("Metadata query executed OK");

    // Parse results into a map of table -> columns
    // Each table can have multiple geometry columns -> create one layer per geometry column
    struct TableInfo
    {
        std::vector<H2GISColumnInfo> columns;
        GIntBig rowCountEstimate = 0;
        std::vector<std::string> geomColumns;  // Names of geometry columns
        std::map<std::string, OGRwkbGeometryType> geomTypes;  // geomCol -> type
        std::map<std::string, int> geomSrids;                 // geomCol -> srid
    };

    std::map<std::string, TableInfo> tables;

    // =========================================================================
    // BATCH FETCH: Get all metadata rows in one call (instead of N calls)
    // This dramatically improves performance: 1 JNI call vs N calls
    // =========================================================================
    long long sizeOut = 0;
    void *buffer = nullptr;

    while ((buffer = h2gis_fetch_batch(thread, qHandle, 10000, &sizeOut)) !=
               nullptr &&
           sizeOut > 0)
    {
        uint8_t *ptr = (uint8_t *)buffer;

        // Header: [ColCount 4] [RowCount 4]
        int32_t colCount, rowCount;
        std::memcpy(&colCount, ptr, 4);
        ptr += 4;
        std::memcpy(&rowCount, ptr, 4);
        ptr += 4;

        LogDebugDS((std::string("Batch received: ") + std::to_string(rowCount) +
                    " rows, " + std::to_string(colCount) + " columns")
                       .c_str());

        if (rowCount <= 0 || colCount < 7)
        {
            h2gis_free_result_buffer(thread, buffer);
            break;
        }

        // Read column offsets
        std::vector<int64_t> offsets(colCount);
        for (int i = 0; i < colCount; i++)
        {
            std::memcpy(&offsets[i], ptr, 8);
            ptr += 8;
        }

        uint8_t *base = (uint8_t *)buffer;

        // Set up column pointers - each column has concatenated data for all rows
        // Format per column: [nameLen 4][name bytes][type 4][totalDataLen 4][row data...]
        std::vector<uint8_t *> colPtrs(colCount);
        std::vector<int32_t> colTypes(colCount);

        for (int c = 0; c < colCount; c++)
        {
            uint8_t *colPtr = base + offsets[c];
            int32_t nameLen;
            std::memcpy(&nameLen, colPtr, 4);
            colPtr += 4 + nameLen;
            int32_t type;
            std::memcpy(&type, colPtr, 4);
            colPtr += 4;
            int32_t totalDataLen;
            std::memcpy(&totalDataLen, colPtr, 4);
            colPtr += 4;

            colTypes[c] = type;
            colPtrs[c] = colPtr;  // Points to start of row data for this column
        }

        // Parse each row by advancing all column pointers together
        for (int row = 0; row < rowCount; row++)
        {
            // Column 0: TABLE_NAME (STRING)
            std::string tableName;
            if (colTypes[0] == H2GIS_TYPE_STRING)
            {
                int32_t strLen;
                std::memcpy(&strLen, colPtrs[0], 4);
                colPtrs[0] += 4;
                if (strLen > 0)
                {
                    tableName = std::string((char *)colPtrs[0], strLen);
                    colPtrs[0] += strLen;
                }
            }

            // Column 1: COLUMN_NAME (STRING)
            std::string columnName;
            if (colTypes[1] == H2GIS_TYPE_STRING)
            {
                int32_t strLen;
                std::memcpy(&strLen, colPtrs[1], 4);
                colPtrs[1] += 4;
                if (strLen > 0)
                {
                    columnName = std::string((char *)colPtrs[1], strLen);
                    colPtrs[1] += strLen;
                }
            }

            // Column 2: DATA_TYPE (STRING)
            std::string dataType;
            if (colTypes[2] == H2GIS_TYPE_STRING)
            {
                int32_t strLen;
                std::memcpy(&strLen, colPtrs[2], 4);
                colPtrs[2] += 4;
                if (strLen > 0)
                {
                    dataType = std::string((char *)colPtrs[2], strLen);
                    colPtrs[2] += strLen;
                }
            }

            // Column 3: ORDINAL_POSITION (INT)
            int ordinalPos = 0;
            if (colTypes[3] == H2GIS_TYPE_INT)
            {
                std::memcpy(&ordinalPos, colPtrs[3], 4);
                colPtrs[3] += 4;
            }
            else if (colTypes[3] == H2GIS_TYPE_LONG)
            {
                int64_t val;
                std::memcpy(&val, colPtrs[3], 8);
                colPtrs[3] += 8;
                ordinalPos = (int)val;
            }

            // Column 4: GEOMETRY_TYPE (STRING, may be NULL/empty)
            std::string geometryType;
            if (colTypes[4] == H2GIS_TYPE_STRING)
            {
                int32_t strLen;
                std::memcpy(&strLen, colPtrs[4], 4);
                colPtrs[4] += 4;
                if (strLen > 0)
                {
                    geometryType = std::string((char *)colPtrs[4], strLen);
                    colPtrs[4] += strLen;
                }
            }
            // If type is not STRING (e.g., OTHER for NULL), geometryType stays empty

            // Column 5: GEOMETRY_SRID (INT or LONG, may be NULL)
            int geometrySrid = 0;
            if (colTypes[5] == H2GIS_TYPE_INT)
            {
                std::memcpy(&geometrySrid, colPtrs[5], 4);
                colPtrs[5] += 4;
            }
            else if (colTypes[5] == H2GIS_TYPE_LONG)
            {
                int64_t val;
                std::memcpy(&val, colPtrs[5], 8);
                colPtrs[5] += 8;
                geometrySrid = (int)val;
            }
            // If type is OTHER (NULL), geometrySrid stays 0

            // Column 6: ROW_COUNT_ESTIMATE (BIGINT)
            int64_t rowEstimate = 0;
            if (colTypes[6] == H2GIS_TYPE_LONG)
            {
                std::memcpy(&rowEstimate, colPtrs[6], 8);
                colPtrs[6] += 8;
            }
            else if (colTypes[6] == H2GIS_TYPE_INT)
            {
                int32_t val;
                std::memcpy(&val, colPtrs[6], 4);
                colPtrs[6] += 4;
                rowEstimate = val;
            }

            if (tableName.empty())
                continue;

            // Add to table info
            TableInfo &ti = tables[tableName];
            ti.rowCountEstimate = rowEstimate;

            H2GISColumnInfo col;
            col.name = columnName;
            col.dataType = dataType;
            col.ordinalPosition = ordinalPos;
            col.geometryType = geometryType;
            col.geometrySrid = geometrySrid;
            ti.columns.push_back(col);

            // Track geometry columns
            if (col.isGeometry())
            {
                ti.geomColumns.push_back(columnName);
                OGRwkbGeometryType mappedType = MapH2GeometryType(geometryType);
                ti.geomTypes[columnName] = mappedType;
                ti.geomSrids[columnName] = geometrySrid;

                LogDebugDS((std::string("  Column ") + columnName +
                            " GEOMETRY_TYPE='" + geometryType +
                            "' -> OGR=" + std::to_string((int)mappedType) +
                            " SRID=" + std::to_string(geometrySrid))
                               .c_str());
            }
        }

        h2gis_free_result_buffer(thread, buffer);
    }

    h2gis_close_query(thread, qHandle);
    h2gis_close_query(thread, stmt);

    LogDebugDS(
        (std::string("Found ") + std::to_string(tables.size()) + " tables")
            .c_str());

    // =======================================================================
    // Create layers: one layer per geometry column (or one for non-spatial)
    // Naming: TABLE if one geom, TABLE.GEOM_COL if multiple geoms
    // =======================================================================
    for (const auto &kv : tables)
    {
        const std::string &tableName = kv.first;
        const TableInfo &ti = kv.second;

        std::string fidColName;
        for (const auto &col : ti.columns)
        {
            if (EQUAL(col.name.c_str(), "ID"))
            {
                fidColName = "ID";
                break;
            }
        }

        if (ti.geomColumns.empty())
        {
            // Non-spatial table: create single layer with table name
            LogDebugDS((std::string("Adding non-spatial table: ") + tableName)
                           .c_str());

            m_papoLayers = (OGRH2GISLayer **)CPLRealloc(
                m_papoLayers, sizeof(OGRH2GISLayer *) * (m_nLayers + 1));
            m_papoLayers[m_nLayers++] = new OGRH2GISLayer(
                this,
                tableName.c_str(),   // Table name for SQL
                tableName.c_str(),   // Layer name (same as table)
                "",                  // No geometry column
                fidColName.c_str(),  // FID column
                0,                   // No SRID
                wkbNone,             // No geometry type
                ti.rowCountEstimate, ti.columns);
        }
        else if (ti.geomColumns.size() == 1)
        {
            // Single geometry column: layer name = table name
            const std::string &geomCol = ti.geomColumns[0];
            OGRwkbGeometryType geomType = ti.geomTypes.at(geomCol);
            int srid = ti.geomSrids.at(geomCol);

            LogDebugDS((std::string("Adding spatial table: ") + tableName +
                        " (geom=" + geomCol + ", srid=" + std::to_string(srid) +
                        ")")
                           .c_str());

            m_papoLayers = (OGRH2GISLayer **)CPLRealloc(
                m_papoLayers, sizeof(OGRH2GISLayer *) * (m_nLayers + 1));
            m_papoLayers[m_nLayers++] = new OGRH2GISLayer(
                this,
                tableName.c_str(),   // Table name for SQL
                tableName.c_str(),   // Layer name (same as table)
                geomCol.c_str(),     // Geometry column
                fidColName.c_str(),  // FID column
                srid, geomType, ti.rowCountEstimate, ti.columns);
        }
        else
        {
            // Multiple geometry columns: create one layer per geometry column
            // Layer name = TABLE.GEOM_COL
            for (const std::string &geomCol : ti.geomColumns)
            {
                OGRwkbGeometryType geomType = ti.geomTypes.at(geomCol);
                int srid = ti.geomSrids.at(geomCol);
                std::string layerName = tableName + "." + geomCol;

                LogDebugDS((std::string("Adding multi-geom layer: ") +
                            layerName + " (srid=" + std::to_string(srid) + ")")
                               .c_str());

                m_papoLayers = (OGRH2GISLayer **)CPLRealloc(
                    m_papoLayers, sizeof(OGRH2GISLayer *) * (m_nLayers + 1));
                m_papoLayers[m_nLayers++] = new OGRH2GISLayer(
                    this,
                    tableName.c_str(),   // Table name for SQL
                    layerName.c_str(),   // Layer name = TABLE.GEOM_COL
                    geomCol.c_str(),     // Geometry column for this layer
                    fidColName.c_str(),  // FID column
                    srid, geomType, ti.rowCountEstimate, ti.columns);
            }
        }
    }

    LogDebugDS(
        (std::string("Total layers created: ") + std::to_string(m_nLayers))
            .c_str());

    // Set description for GDAL (required for proper identification)
    SetDescription(m_pszName);

    return TRUE;
}

#if GDAL_VERSION_NUM >= 3120000
OGRLayer *OGRH2GISDataSource::GetLayer(int i) const
{
    if (i < 0 || i >= m_nLayers)
        return nullptr;
    return m_papoLayers[i];
}

int OGRH2GISDataSource::TestCapability(const char *pszCap) const
{
    if (EQUAL(pszCap, ODsCCreateLayer))
        return TRUE;
    if (EQUAL(pszCap, ODsCDeleteLayer))
        return TRUE;
    if (EQUAL(pszCap, ODsCTransactions))
        return TRUE;
    return FALSE;
}
#else
OGRLayer *OGRH2GISDataSource::GetLayer(int i)
{
    if (i < 0 || i >= m_nLayers)
        return nullptr;
    return m_papoLayers[i];
}

int OGRH2GISDataSource::TestCapability(const char *pszCap)
{
    if (EQUAL(pszCap, ODsCCreateLayer))
        return TRUE;
    if (EQUAL(pszCap, ODsCDeleteLayer))
        return TRUE;
    if (EQUAL(pszCap, ODsCTransactions))
        return TRUE;
    return FALSE;
}
#endif

#if GDAL_VERSION_NUM >= 3100000
OGRLayer *
OGRH2GISDataSource::ICreateLayer(const char *pszName,
                                 const OGRGeomFieldDefn *poGeomFieldDefn,
                                 CSLConstList papszOptions)
{
    const OGRSpatialReference *poSpatialRef =
        poGeomFieldDefn ? poGeomFieldDefn->GetSpatialRef() : nullptr;
    OGRwkbGeometryType eGType =
        poGeomFieldDefn ? poGeomFieldDefn->GetType() : wkbUnknown;

    graal_isolatethread_t *thread = (graal_isolatethread_t *)m_hThread;

    // Validate Name
    std::string tableName(pszName);  // Escape?

    // Layer creation options
    const char *pszGeomName = CSLFetchNameValue(papszOptions, "GEOMETRY_NAME");
    const char *pszFIDCol = CSLFetchNameValue(papszOptions, "FID");
    const char *pszSpatialIndex =
        CSLFetchNameValue(papszOptions, "SPATIAL_INDEX");

    std::string geomCol =
        (pszGeomName && strlen(pszGeomName) > 0) ? pszGeomName : "GEOM";
    std::string fidCol =
        (pszFIDCol && strlen(pszFIDCol) > 0) ? pszFIDCol : "ID";
    bool bCreateSpatialIndex = true;
    if (pszSpatialIndex && EQUAL(pszSpatialIndex, "NO"))
    {
        bCreateSpatialIndex = false;
    }

    // Construct SQL
    // Default to FID (Serial)
    std::string sql = "CREATE TABLE \"" + tableName + "\" (\"" + fidCol +
                      "\" INT AUTO_INCREMENT PRIMARY KEY";

    int srid = 0;
    if (poSpatialRef)
    {
        // Get EPSG
        const char *epsg = poSpatialRef->GetAuthorityCode(nullptr);
        if (epsg)
            srid = atoi(epsg);
    }

    if (eGType != wkbNone)
    {
        // Use typed geometry syntax: GEOMETRY(POINT Z, 4326)
        const char *pszGeomTypeName = MapOGRGeomTypeToH2Name(eGType);
        const char *pszZMSuffix = GetH2GeomZMSuffix(eGType);
        
        sql += ", \"" + geomCol + "\" GEOMETRY(" + std::string(pszGeomTypeName) + 
               std::string(pszZMSuffix);
        if (srid > 0)
        {
            sql += ", " + std::to_string(srid);
        }
        sql += ")";
    }

    sql += ")";

    long long stmt = h2gis_prepare(thread, m_hConnection, (char *)sql.c_str());
    if (!stmt)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "Failed to prepare CREATE TABLE statement.");
        return nullptr;
    }

    long long rs = h2gis_execute_prepared(thread, stmt);
    h2gis_close_query(thread, stmt);
    // RS is usually update count or null for DDL
    if (rs)
        h2gis_close_query(thread, rs);

    // Create Spatial Index if requested
    if (bCreateSpatialIndex && eGType != wkbNone)
    {
        std::string idxSql = "CREATE SPATIAL INDEX ON \"" + tableName +
                             "\"(\"" + geomCol + "\")";
        stmt = h2gis_prepare(thread, m_hConnection, (char *)idxSql.c_str());
        if (stmt)
        {
            rs = h2gis_execute_prepared(thread, stmt);
            h2gis_close_query(thread, stmt);
            if (rs)
                h2gis_close_query(thread, rs);
        }
    }

    // Refresh layer list
    // Ideally we should just add the new layer to our list instead of re-opening
    // But for now, returning the layer requires creating it.

    // Create the layer object with schema already known (we just created the table)
    // Pass bSchemaFetched=true via the columns vector to prevent FetchSchema
    // from re-reading the table and incorrectly adding the FID column as a field.
    // An empty columns vector with the flag set is sufficient since geometry
    // and FID are already configured from constructor parameters.
    std::vector<H2GISColumnInfo> cols;  // Empty: no user-defined fields yet

    OGRH2GISLayer *poLayer =
        new OGRH2GISLayer(this, tableName.c_str(), tableName.c_str(),
                          (eGType != wkbNone ? geomCol.c_str() : ""),
                          fidCol.c_str(), srid, eGType, 0, cols,
                          true /* bSchemaFetched */);

    // Add to list
    m_nLayers++;
    m_papoLayers = (OGRH2GISLayer **)CPLRealloc(
        m_papoLayers, sizeof(OGRH2GISLayer *) * m_nLayers);
    m_papoLayers[m_nLayers - 1] = poLayer;

    return poLayer;
}
#else
#if GDAL_VERSION_NUM >= 3090000
OGRLayer *OGRH2GISDataSource::ICreateLayer(
    const char *pszName, const OGRSpatialReference *poSpatialRef,
    OGRwkbGeometryType eGType, CSLConstList papszOptions)
#elif GDAL_VERSION_NUM >= 3050000
OGRLayer *
OGRH2GISDataSource::ICreateLayer(const char *pszName,
                                 const OGRSpatialReference *poSpatialRef,
                                 OGRwkbGeometryType eGType, char **papszOptions)
#else
OGRLayer *OGRH2GISDataSource::ICreateLayer(const char *pszName,
                                           OGRSpatialReference *poSpatialRef,
                                           OGRwkbGeometryType eGType,
                                           char **papszOptions)
#endif
{
    graal_isolatethread_t *thread = (graal_isolatethread_t *)m_hThread;

    // Validate Name
    std::string tableName(pszName);  // Escape?

    // Layer creation options
    const char *pszGeomName = CSLFetchNameValue(papszOptions, "GEOMETRY_NAME");
    const char *pszFIDCol = CSLFetchNameValue(papszOptions, "FID");
    const char *pszSpatialIndex =
        CSLFetchNameValue(papszOptions, "SPATIAL_INDEX");

    std::string geomCol =
        (pszGeomName && strlen(pszGeomName) > 0) ? pszGeomName : "GEOM";
    std::string fidCol =
        (pszFIDCol && strlen(pszFIDCol) > 0) ? pszFIDCol : "ID";
    bool bCreateSpatialIndex = true;
    if (pszSpatialIndex && EQUAL(pszSpatialIndex, "NO"))
    {
        bCreateSpatialIndex = false;
    }

    // Construct SQL
    // Default to FID (Serial)
    std::string sql = "CREATE TABLE \"" + tableName + "\" (\"" + fidCol +
                      "\" INT AUTO_INCREMENT PRIMARY KEY";

    int srid = 0;
    if (poSpatialRef)
    {
        // Get EPSG
        const char *epsg = poSpatialRef->GetAuthorityCode(nullptr);
        if (epsg)
            srid = atoi(epsg);
    }

    if (eGType != wkbNone)
    {
        // Use typed geometry syntax: GEOMETRY(POINT Z, 4326)
        const char *pszGeomTypeName = MapOGRGeomTypeToH2Name(eGType);
        const char *pszZMSuffix = GetH2GeomZMSuffix(eGType);
        
        sql += ", \"" + geomCol + "\" GEOMETRY(" + std::string(pszGeomTypeName) + 
               std::string(pszZMSuffix);
        if (srid > 0)
        {
            sql += ", " + std::to_string(srid);
        }
        sql += ")";
    }

    sql += ")";

    LogDebugDS(("Creating Layer: " + sql).c_str());

    if (h2gis_execute(thread, m_hConnection, (char *)sql.c_str()) < 0)
    {
        CPLError(CE_Failure, CPLE_AppDefined, "Failed to create table");
        return nullptr;
    }

    // Force Spatial Index
    if (eGType != wkbNone && bCreateSpatialIndex)
    {
        std::string idxSql = "CREATE SPATIAL INDEX ON \"" + tableName +
                             "\"(\"" + geomCol + "\")";
        h2gis_execute(thread, m_hConnection, (char *)idxSql.c_str());
    }

    // Register Layer
    m_papoLayers = (OGRH2GISLayer **)CPLRealloc(
        m_papoLayers, sizeof(OGRH2GISLayer *) * (m_nLayers + 1));

    // Empty columns vector: no user-defined fields yet.
    // Pass bSchemaFetched=true to prevent FetchSchema from re-reading
    // the table and incorrectly adding the FID column as a field.
    std::vector<H2GISColumnInfo> cols;

    OGRH2GISLayer *layer =
        new OGRH2GISLayer(this,
                          tableName.c_str(),  // Table name for SQL
                          tableName.c_str(),  // Layer name (same as table)
                          geomCol.c_str(),    // Geometry column
                          fidCol.c_str(),     // FID column
                          srid,
                          eGType,  // Use the requested geometry type
                          0,       // Row count (empty table)
                          cols,
                          true /* bSchemaFetched */);
    m_papoLayers[m_nLayers++] = layer;

    return layer;
}
#endif

OGRErr OGRH2GISDataSource::DeleteLayer(int iLayer)
{
    if (iLayer < 0 || iLayer >= m_nLayers)
        return OGRERR_FAILURE;

    OGRH2GISLayer *poLayer = m_papoLayers[iLayer];
    std::string tableName = poLayer->GetLayerDefn()->GetName();

    std::string sql = "DROP TABLE IF EXISTS \"" + tableName + "\" CASCADE";

    graal_isolatethread_t *thread = (graal_isolatethread_t *)m_hThread;
    if (h2gis_execute(thread, m_hConnection, (char *)sql.c_str()) < 0)
    {
        return OGRERR_FAILURE;
    }

    delete m_papoLayers[iLayer];

    // Shift remaining
    for (int i = iLayer; i < m_nLayers - 1; i++)
        m_papoLayers[i] = m_papoLayers[i + 1];

    m_nLayers--;
    return OGRERR_NONE;
}

OGRLayer *OGRH2GISDataSource::ExecuteSQL(const char *pszSQL,
                                         OGRGeometry *poSpatialFilter,
                                         const char *pszDialect)
{
    // Handle SELECT/CALL/WITH queries returning a result set
    if (STARTS_WITH_CI(pszSQL, "SELECT") || STARTS_WITH_CI(pszSQL, "CALL") ||
        STARTS_WITH_CI(pszSQL, "WITH"))
    {
        OGRH2GISResultLayer *poLayer = new OGRH2GISResultLayer(this, pszSQL);
        if (poSpatialFilter != nullptr)
            poLayer->SetSpatialFilter(poSpatialFilter);
        return poLayer;
    }

    graal_isolatethread_t *thread = (graal_isolatethread_t *)m_hThread;

    // Handle INSERT/UPDATE/DELETE/DDL
    int ret = h2gis_execute(thread, m_hConnection, (char *)pszSQL);
    if (ret < 0)
    {
        CPLError(CE_Failure, CPLE_AppDefined, "H2GIS: ExecuteSQL failed.");
    }

    return nullptr;
}

void OGRH2GISDataSource::ReleaseResultSet(OGRLayer *poLayer)
{
    delete poLayer;
}

OGRLayer *OGRH2GISDataSource::GetLayerByName(const char *pszName)
{
    if (!pszName)
        return nullptr;
    for (int i = 0; i < m_nLayers; i++)
    {
        if (EQUAL(m_papoLayers[i]->GetName(), pszName))
            return m_papoLayers[i];
    }
    return nullptr;
}

OGRErr OGRH2GISDataSource::StartTransaction(int bForce)
{
    graal_isolatethread_t *thread = (graal_isolatethread_t *)m_hThread;
    if (h2gis_execute(thread, m_hConnection, (char *)"BEGIN") >= 0)
    {
        return OGRERR_NONE;
    }
    return OGRERR_FAILURE;
}

OGRErr OGRH2GISDataSource::CommitTransaction()
{
    graal_isolatethread_t *thread = (graal_isolatethread_t *)m_hThread;
    if (h2gis_execute(thread, m_hConnection, (char *)"COMMIT") >= 0)
    {
        return OGRERR_NONE;
    }
    return OGRERR_FAILURE;
}

OGRErr OGRH2GISDataSource::RollbackTransaction()
{
    graal_isolatethread_t *thread = (graal_isolatethread_t *)m_hThread;
    if (h2gis_execute(thread, m_hConnection, (char *)"ROLLBACK") >= 0)
    {
        return OGRERR_NONE;
    }
    return OGRERR_FAILURE;
}
