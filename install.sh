#!/bin/bash
#==============================================================================
# install.sh - Installation du driver GDAL H2GIS
#
# Usage: ./install.sh [--uninstall]
#
#==============================================================================

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_DIR="/usr/lib/x86_64-linux-gnu/gdalplugins"
LIB_DIR="/usr/local/lib"

print_banner() {
    echo ""
    echo "╔══════════════════════════════════════════════════╗"
    echo "║         🗄️ GDAL H2GIS Driver Installer           ║"
    echo "║              Version 1.0.0                       ║"
    echo "╚══════════════════════════════════════════════════╝"
    echo ""
}

check_root() {
    if [[ $EUID -ne 0 ]]; then
        echo -e "${YELLOW}⚠️ Ce script nécessite les droits sudo.${NC}"
        exec sudo "$0" "$@"
    fi
}

check_prerequisites() {
    echo "🔍 Vérification des prérequis..."
    if ! command -v ogrinfo &> /dev/null; then
        echo -e "${RED}❌ GDAL n'est pas installé!${NC}"
        exit 1
    fi
    GDAL_VERSION=$(ogrinfo --version | grep -oP 'GDAL \K[0-9]+\.[0-9]+')
    echo -e "   ✅ GDAL ${GDAL_VERSION} trouvé"
    [[ ! -d "$PLUGIN_DIR" ]] && mkdir -p "$PLUGIN_DIR"
}

install_driver() {
    echo ""
    echo "📦 Installation du driver..."
    
    if [[ -f "$SCRIPT_DIR/build/gdal_H2GIS.so" ]]; then
        DRIVER_SO="$SCRIPT_DIR/build/gdal_H2GIS.so"
    elif [[ -f "$SCRIPT_DIR/gdal_H2GIS.so" ]]; then
        DRIVER_SO="$SCRIPT_DIR/gdal_H2GIS.so"
    else
        echo -e "${RED}❌ gdal_H2GIS.so introuvable!${NC}"
        exit 1
    fi
    
    if [[ -f "$SCRIPT_DIR/libh2gis.so" ]]; then
        LIB_SO="$SCRIPT_DIR/libh2gis.so"
    else
        echo -e "${RED}❌ libh2gis.so introuvable!${NC}"
        exit 1
    fi
    
    cp "$DRIVER_SO" "$PLUGIN_DIR/" && chmod 644 "$PLUGIN_DIR/gdal_H2GIS.so"
    cp "$LIB_SO" "$LIB_DIR/" && chmod 644 "$LIB_DIR/libh2gis.so"
    ldconfig
    echo -e "${GREEN}✅ Installation réussie!${NC}"
}

uninstall_driver() {
    echo "🗑️ Désinstallation..."
    rm -f "$PLUGIN_DIR/gdal_H2GIS.so" "$LIB_DIR/libh2gis.so"
    ldconfig
    echo -e "${GREEN}✅ Désinstallation terminée!${NC}"
    exit 0
}

verify_installation() {
    echo ""
    echo "🧪 Vérification..."
    if ogrinfo --formats 2>/dev/null | grep -q "H2GIS"; then
        echo -e "${GREEN}✅ Driver H2GIS enregistré dans GDAL${NC}"
    else
        echo -e "${RED}❌ Driver non détecté!${NC}"
        exit 1
    fi
}

print_usage() {
    echo ""
    echo "🎉 Installation terminée!"
    echo ""
    echo "  ogrinfo /chemin/vers/database.mv.db"
    echo "  qgis /chemin/vers/database.mv.db"
    echo ""
}

print_banner
[[ "$1" == "--uninstall" ]] && check_root "$@" && uninstall_driver
check_root "$@"
check_prerequisites
install_driver
verify_installation
print_usage
