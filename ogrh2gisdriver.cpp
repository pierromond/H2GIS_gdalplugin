/******************************************************************************
 *
 * Project:  OpenGIS Simple Features Reference Implementation
 * Purpose:  Implements OGRH2GISDriver class (GraalVM Native Image version)
 * Author:   H2GIS Team
 *
 ******************************************************************************
 * Copyright (c) 2024, H2GIS Team
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 ****************************************************************************/

#include "ogr_h2gis.h"
#include "cpl_conv.h"
#include "cpl_error.h"
#include "cpl_string.h"

#include <string.h>
#include <string>

// Debug logging
static void LogDebug(const char* msg) {
    FILE* f = fopen("/tmp/h2gis_driver_debug.log", "a");
    if (f) {
        fprintf(f, "[H2GIS DRIVER] %s\n", msg);
        fclose(f);
    }
}

/************************************************************************/
/*                              Identify()                              */
/************************************************************************/

static int OGRH2GISDriverIdentify(GDALOpenInfo *poOpenInfo)
{
    // Normalize filename (strip driver prefix, QGIS layer options, and URI query strings)
    std::string filename = poOpenInfo->pszFilename ? poOpenInfo->pszFilename : "";
    if (filename.rfind("H2GIS:", 0) == 0) {
        filename = filename.substr(strlen("H2GIS:"));
    }
    // Strip pipe format: /path/file.mv.db|layername=...
    size_t optPos = filename.find('|');
    if (optPos != std::string::npos) {
        filename = filename.substr(0, optPos);
    }
    // Strip URI query string: /path/file.mv.db?user=...&password=...
    size_t queryPos = filename.find('?');
    if (queryPos != std::string::npos) {
        filename = filename.substr(0, queryPos);
    }
    if (filename.empty())
        return FALSE;

    const char *pszExt = CPLGetExtension(filename.c_str());

    // Check for .mv.db extension (H2 database files)
    if (EQUAL(pszExt, "db"))
    {
        // Check if it ends with .mv.db
        const char *pszFilename = filename.c_str();
        size_t nLen = strlen(pszFilename);
        if (nLen > 6 && EQUAL(pszFilename + nLen - 6, ".mv.db"))
        {
            char buf[256];
            snprintf(buf, sizeof(buf), "Identify: Matched .mv.db file: %s", pszFilename);
            LogDebug(buf);
            return TRUE;
        }
    }

    // Accept direct .mv.db path even if GDAL did not open a file handle yet
    if (filename.size() > 6 && filename.compare(filename.size() - 6, 6, ".mv.db") == 0) {
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/
/*                                Open()                                */
/************************************************************************/

static GDALDataset *OGRH2GISDriverOpen(GDALOpenInfo *poOpenInfo)
{
    if (!OGRH2GISDriverIdentify(poOpenInfo))
        return nullptr;

    char buf[256];
    snprintf(buf, sizeof(buf), "Open: Opening file: %s", poOpenInfo->pszFilename);
    LogDebug(buf);

    // Extract GDAL Open Options for authentication
    const char* pszUser = CSLFetchNameValue(poOpenInfo->papszOpenOptions, "USER");
    const char* pszPassword = CSLFetchNameValue(poOpenInfo->papszOpenOptions, "PASSWORD");
    
    // Also extract from URI query string: /path/file.mv.db?user=...&password=...
    std::string uriUser, uriPass;
    std::string rawFilename = poOpenInfo->pszFilename ? poOpenInfo->pszFilename : "";
    size_t queryPos = rawFilename.find('?');
    if (queryPos != std::string::npos) {
        std::string params = rawFilename.substr(queryPos + 1);
        size_t pos = 0;
        while (pos < params.size()) {
            size_t ampPos = params.find('&', pos);
            if (ampPos == std::string::npos) ampPos = params.size();
            std::string kv = params.substr(pos, ampPos - pos);
            size_t eqPos = kv.find('=');
            if (eqPos != std::string::npos) {
                std::string key = kv.substr(0, eqPos);
                std::string val = kv.substr(eqPos + 1);
                if (key == "user" || key == "username" || key == "USER") uriUser = val;
                else if (key == "password" || key == "pass" || key == "PASSWORD") uriPass = val;
            }
            pos = ampPos + 1;
        }
    }
    
    // Priority: Open Options > URI query string
    std::string finalUser = pszUser ? pszUser : uriUser;
    std::string finalPass = pszPassword ? pszPassword : uriPass;
    
    if (!finalUser.empty()) {
        snprintf(buf, sizeof(buf), "Open: Using credentials USER='%s'", finalUser.c_str());
        LogDebug(buf);
    }

    OGRH2GISDataSource *poDS = new OGRH2GISDataSource();

    // Normalize filename for actual open
    std::string filename = rawFilename;
    if (filename.rfind("H2GIS:", 0) == 0) {
        filename = filename.substr(strlen("H2GIS:"));
    }
    // Strip pipe format
    size_t optPos = filename.find('|');
    if (optPos != std::string::npos) {
        filename = filename.substr(0, optPos);
    }
    // Strip query string
    queryPos = filename.find('?');
    if (queryPos != std::string::npos) {
        filename = filename.substr(0, queryPos);
    }

    if (!poDS->Open(filename.c_str(),
                    poOpenInfo->eAccess == GA_Update,
                    finalUser.empty() ? nullptr : finalUser.c_str(),
                    finalPass.empty() ? nullptr : finalPass.c_str()))
    {
        delete poDS;
        return nullptr;
    }

    LogDebug("Open: Successfully opened datasource");
    return poDS;
}

/************************************************************************/
/*                          OGRH2GISDriverCreate()                      */
/************************************************************************/

static GDALDataset *OGRH2GISDriverCreate( const char * pszName,
                                          int nXSize, int nYSize, int nBands,
                                          GDALDataType eType,
                                          char ** papszOptions )
{
    OGRH2GISDataSource *poDS = new OGRH2GISDataSource();

    if( !poDS->Open( pszName, TRUE ) )
    {
        delete poDS;
        return nullptr;
    }

    return poDS;
}

/************************************************************************/
/*                           RegisterOGRH2GIS()                         */
/************************************************************************/

void RegisterOGRH2GIS()
{
    LogDebug("RegisterOGRH2GIS: Starting registration");

    if (GDALGetDriverByName("H2GIS") != nullptr)
    {
        LogDebug("RegisterOGRH2GIS: Driver already registered");
        return;
    }

    GDALDriver *poDriver = new GDALDriver();

    poDriver->SetDescription("H2GIS");
    poDriver->SetMetadataItem(GDAL_DCAP_VECTOR, "YES");
    
#ifdef GDAL_DCAP_CREATE_LAYER
    poDriver->SetMetadataItem(GDAL_DCAP_CREATE_LAYER, "YES");
#endif
#ifdef GDAL_DCAP_CREATE_FIELD
    poDriver->SetMetadataItem(GDAL_DCAP_CREATE_FIELD, "YES");
#endif
    
    poDriver->SetMetadataItem(GDAL_DMD_LONGNAME, "H2GIS Spatial Database");
    poDriver->SetMetadataItem(GDAL_DMD_EXTENSION, "mv.db");
    poDriver->SetMetadataItem(GDAL_DMD_HELPTOPIC, "drivers/vector/h2gis.html");
    poDriver->SetMetadataItem(GDAL_DCAP_VIRTUALIO, "YES");
    poDriver->SetMetadataItem(GDAL_DMD_CONNECTION_PREFIX, "H2GIS:");
    
    // Open options for authentication
    poDriver->SetMetadataItem(GDAL_DMD_OPENOPTIONLIST,
        "<OpenOptionList>"
        "  <Option name='USER' type='string' description='Database username'/>"
        "  <Option name='PASSWORD' type='string' description='Database password'/>"
        "</OpenOptionList>");

    poDriver->pfnIdentify = OGRH2GISDriverIdentify;
    poDriver->pfnOpen = OGRH2GISDriverOpen;
    poDriver->pfnCreate = OGRH2GISDriverCreate;

    GetGDALDriverManager()->RegisterDriver(poDriver);

    LogDebug("RegisterOGRH2GIS: Driver registered successfully");
}

/************************************************************************/
/*                      GDALRegister_H2GIS()                            */
/*         Entry point called by GDAL's plugin autoloader               */
/************************************************************************/

extern "C" void GDALRegister_H2GIS()
{
    RegisterOGRH2GIS();
}
