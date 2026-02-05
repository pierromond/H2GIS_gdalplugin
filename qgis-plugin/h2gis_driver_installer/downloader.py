# -*- coding: utf-8 -*-
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Download utilities for H2GIS Driver Installer.

Downloads driver binaries from GitHub and H2GIS native library from nightly.link.
"""

import hashlib
import os
import tempfile
import zipfile
from pathlib import Path
from typing import Optional, Callable, Tuple
from urllib.request import urlopen, Request
from urllib.error import URLError, HTTPError

from qgis.core import QgsMessageLog, Qgis


# H2GIS native library artifact from orbisgis/h2gis
# Latest build from CI GraalVM workflow (Run 12, commit 4b4ca00, Feb 2 2026)
H2GIS_ARTIFACT_ID = "5341331001"
H2GIS_ARTIFACT_URL = f"https://nightly.link/orbisgis/h2gis/actions/artifacts/{H2GIS_ARTIFACT_ID}.zip"
H2GIS_EXPECTED_SHA256 = "80c293f6fce732b62978ea7100e17c7e3efb83ca53721ae9fbfc7d16bc9e77e4"

# GitHub repository for GDAL driver
GITHUB_REPO = "pierromond/H2GIS_gdalplugin"
GITHUB_ACTIONS_URL = f"https://github.com/{GITHUB_REPO}/actions"


class DownloadError(Exception):
    """Exception raised when download fails."""
    pass


def calculate_sha256(file_path: Path) -> str:
    """
    Calculate SHA256 hash of a file.
    
    Args:
        file_path: Path to the file
        
    Returns:
        Hex string of SHA256 hash
    """
    sha256_hash = hashlib.sha256()
    with open(file_path, "rb") as f:
        for byte_block in iter(lambda: f.read(4096), b""):
            sha256_hash.update(byte_block)
    return sha256_hash.hexdigest()


def download_file(url: str, dest_path: Path, 
                  progress_callback: Optional[Callable[[int, int], None]] = None) -> None:
    """
    Download a file from URL.
    
    Args:
        url: URL to download from
        dest_path: Destination file path
        progress_callback: Optional callback(bytes_downloaded, total_bytes)
        
    Raises:
        DownloadError: If download fails
    """
    try:
        request = Request(url, headers={'User-Agent': 'QGIS-H2GIS-Installer/1.0'})
        
        with urlopen(request, timeout=60) as response:
            total_size = int(response.headers.get('Content-Length', 0))
            downloaded = 0
            
            with open(dest_path, 'wb') as f:
                while True:
                    chunk = response.read(8192)
                    if not chunk:
                        break
                    f.write(chunk)
                    downloaded += len(chunk)
                    if progress_callback:
                        progress_callback(downloaded, total_size)
        
        QgsMessageLog.logMessage(
            f"Downloaded {dest_path.name} ({downloaded} bytes)",
            "H2GIS",
            Qgis.Info
        )
        
    except HTTPError as e:
        raise DownloadError(f"HTTP Error {e.code}: {e.reason}")
    except URLError as e:
        raise DownloadError(f"URL Error: {e.reason}")
    except Exception as e:
        raise DownloadError(f"Download failed: {e}")


def download_h2gis_library(dest_dir: Path, 
                           platform_subdir: str,
                           lib_name: str,
                           progress_callback: Optional[Callable[[int, int], None]] = None,
                           verify_hash: bool = True) -> Path:
    """
    Download H2GIS native library from the specified artifact.
    
    Args:
        dest_dir: Directory to extract to
        platform_subdir: 'linux', 'windows', or 'macos'
        lib_name: Library filename ('libh2gis.so', 'h2gis.dll', 'libh2gis.dylib')
        progress_callback: Optional progress callback
        verify_hash: Whether to verify SHA256 hash
        
    Returns:
        Path to the extracted library
        
    Raises:
        DownloadError: If download or extraction fails
    """
    with tempfile.TemporaryDirectory() as tmp_dir:
        tmp_path = Path(tmp_dir)
        zip_path = tmp_path / "h2gis-libs.zip"
        
        # Download artifact
        QgsMessageLog.logMessage(
            f"Downloading H2GIS library from {H2GIS_ARTIFACT_URL}",
            "H2GIS",
            Qgis.Info
        )
        download_file(H2GIS_ARTIFACT_URL, zip_path, progress_callback)
        
        # Verify hash if requested
        if verify_hash:
            actual_hash = calculate_sha256(zip_path)
            if actual_hash != H2GIS_EXPECTED_SHA256:
                raise DownloadError(
                    f"SHA256 mismatch! Expected {H2GIS_EXPECTED_SHA256}, got {actual_hash}"
                )
            QgsMessageLog.logMessage(
                "SHA256 hash verified successfully",
                "H2GIS",
                Qgis.Info
            )
        
        # Extract
        extract_dir = tmp_path / "extracted"
        extract_dir.mkdir()
        
        with zipfile.ZipFile(zip_path, 'r') as zf:
            zf.extractall(extract_dir)
        
        # The artifact may contain nested zips (h2gis-graalvm-all-platforms.zip)
        for nested_zip in extract_dir.rglob("*.zip"):
            with zipfile.ZipFile(nested_zip, 'r') as zf:
                zf.extractall(extract_dir)
        
        # Find the library for our platform
        lib_path = None
        
        # Search patterns
        search_patterns = [
            f"{platform_subdir}/{lib_name}",
            f"*/{platform_subdir}/{lib_name}",
            f"**/{lib_name}",
        ]
        
        for pattern in search_patterns:
            matches = list(extract_dir.rglob(lib_name))
            for match in matches:
                if platform_subdir in str(match.parent).lower() or platform_subdir in match.parent.name.lower():
                    lib_path = match
                    break
            if lib_path:
                break
        
        # Fallback: just find the library anywhere
        if not lib_path:
            matches = list(extract_dir.rglob(lib_name))
            if matches:
                lib_path = matches[0]
        
        if not lib_path or not lib_path.exists():
            raise DownloadError(
                f"Could not find {lib_name} for platform {platform_subdir} in artifact"
            )
        
        # Copy to destination
        dest_lib_path = dest_dir / lib_name
        import shutil
        shutil.copy2(lib_path, dest_lib_path)
        
        QgsMessageLog.logMessage(
            f"Extracted H2GIS library to {dest_lib_path}",
            "H2GIS",
            Qgis.Info
        )
        
        return dest_lib_path


def download_gdal_driver(artifact_name: str,
                         dest_dir: Path,
                         driver_extension: str,
                         progress_callback: Optional[Callable[[int, int], None]] = None) -> Path:
    """
    Download GDAL H2GIS driver from GitHub Actions artifacts.
    
    Note: This requires the artifacts to be available. For now, we'll use
    nightly.link which provides public access to artifacts.
    
    Args:
        artifact_name: Name of the artifact (e.g., 'gdal-h2gis-ubuntu24.04-gdal3.8')
        dest_dir: Directory to extract to
        driver_extension: '.so', '.dll', or '.dylib'
        progress_callback: Optional progress callback
        
    Returns:
        Path to the extracted driver
        
    Raises:
        DownloadError: If download fails
    """
    # For GitHub Actions artifacts, we need to use nightly.link or GitHub Releases
    # nightly.link format: https://nightly.link/{owner}/{repo}/actions/artifacts/{artifact_id}.zip
    # 
    # For now, we'll provide instructions to download manually or use releases
    # Once releases are set up, this will be updated
    
    raise DownloadError(
        f"Automatic GDAL driver download not yet available.\n\n"
        f"Please download '{artifact_name}' manually from:\n"
        f"{GITHUB_ACTIONS_URL}\n\n"
        f"Then place 'gdal_H2GIS{driver_extension}' in:\n"
        f"{dest_dir}"
    )


def get_latest_release_url(artifact_name: str, driver_extension: str) -> Optional[str]:
    """
    Get the download URL for a driver from the latest GitHub release.
    
    Args:
        artifact_name: Artifact name pattern
        driver_extension: Driver file extension
        
    Returns:
        Download URL or None if not found
    """
    # This will be implemented when GitHub Releases are set up
    # For now, return None to fall back to manual download
    return None


def check_for_updates() -> Tuple[bool, Optional[str]]:
    """
    Check if a newer version of the driver is available.
    
    Returns:
        Tuple of (update_available, new_version)
    """
    # This will be implemented later with GitHub Releases API
    # For now, return no updates available
    return (False, None)
