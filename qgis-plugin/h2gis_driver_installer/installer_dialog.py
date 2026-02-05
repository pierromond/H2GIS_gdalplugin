# -*- coding: utf-8 -*-
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Installer Dialog for H2GIS Driver Installer.

Qt dialog providing UI for installation, uninstallation, and demo creation.
"""

import os
from pathlib import Path
from typing import Optional

from qgis.PyQt.QtCore import Qt, QThread, pyqtSignal
from qgis.PyQt.QtWidgets import (
    QDialog,
    QVBoxLayout,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QProgressBar,
    QTextEdit,
    QGroupBox,
    QFileDialog,
    QMessageBox,
    QFrame,
)
from qgis.PyQt.QtGui import QFont, QPixmap

from qgis.core import QgsMessageLog, Qgis

from .platform_detector import get_platform_info, is_driver_installed, get_artifact_name
from .installer import H2GISInstaller
from .demo_database import DemoDatabase


class InstallWorker(QThread):
    """Background worker for installation tasks."""
    
    progress = pyqtSignal(str, int)  # message, percent
    finished = pyqtSignal(bool, str)  # success, message
    
    def __init__(self, installer: H2GISInstaller, driver_path: Optional[Path] = None):
        super().__init__()
        self.installer = installer
        self.driver_path = driver_path
    
    def run(self):
        success, message = self.installer.install(
            driver_path=self.driver_path,
            progress_callback=lambda msg, pct: self.progress.emit(msg, pct)
        )
        self.finished.emit(success, message)


class H2GISInstallerDialog(QDialog):
    """Main installer dialog."""
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self.installer = H2GISInstaller()
        self.demo_db = DemoDatabase()
        self.worker = None
        
        self.setWindowTitle("H2GIS Driver Installer")
        self.setMinimumWidth(550)
        self.setMinimumHeight(500)
        
        self._setup_ui()
        self._update_status()
    
    def _setup_ui(self):
        """Setup the dialog UI."""
        layout = QVBoxLayout(self)
        
        # Header
        header = QLabel("H2GIS Driver Installer")
        header_font = QFont()
        header_font.setPointSize(16)
        header_font.setBold(True)
        header.setFont(header_font)
        header.setAlignment(Qt.AlignCenter)
        layout.addWidget(header)
        
        subtitle = QLabel("Install the GDAL/OGR driver for H2GIS spatial databases")
        subtitle.setAlignment(Qt.AlignCenter)
        subtitle.setStyleSheet("color: gray;")
        layout.addWidget(subtitle)
        
        layout.addSpacing(10)
        
        # Status Group
        status_group = QGroupBox("System Information")
        status_layout = QVBoxLayout(status_group)
        
        self.status_label = QLabel()
        self.status_label.setWordWrap(True)
        status_layout.addWidget(self.status_label)
        
        layout.addWidget(status_group)
        
        # Installation Group
        install_group = QGroupBox("Installation")
        install_layout = QVBoxLayout(install_group)
        
        # Progress bar
        self.progress_bar = QProgressBar()
        self.progress_bar.setVisible(False)
        install_layout.addWidget(self.progress_bar)
        
        # Progress label
        self.progress_label = QLabel("")
        self.progress_label.setStyleSheet("color: gray; font-size: 11px;")
        self.progress_label.setVisible(False)
        install_layout.addWidget(self.progress_label)
        
        # Buttons row
        buttons_layout = QHBoxLayout()
        
        self.install_btn = QPushButton("Install Driver")
        self.install_btn.setToolTip(
            "Download and install the H2GIS driver.\n"
            "The driver will be installed in your user profile (no admin required)."
        )
        self.install_btn.clicked.connect(self._on_install)
        buttons_layout.addWidget(self.install_btn)
        
        self.browse_btn = QPushButton("Install from File...")
        self.browse_btn.setToolTip(
            "Install a pre-downloaded driver binary.\n"
            "Use this if automatic download is not available for your system."
        )
        self.browse_btn.clicked.connect(self._on_install_from_file)
        buttons_layout.addWidget(self.browse_btn)
        
        self.uninstall_btn = QPushButton("Uninstall")
        self.uninstall_btn.setToolTip("Remove the installed H2GIS driver.")
        self.uninstall_btn.clicked.connect(self._on_uninstall)
        buttons_layout.addWidget(self.uninstall_btn)
        
        install_layout.addLayout(buttons_layout)
        layout.addWidget(install_group)
        
        # Demo Database Group
        demo_group = QGroupBox("Demo Database")
        demo_layout = QVBoxLayout(demo_group)
        
        demo_info = QLabel(
            "Create a sample H2GIS database with French cities, rivers, and boundaries.\n"
            "Perfect for testing the driver!"
        )
        demo_info.setWordWrap(True)
        demo_layout.addWidget(demo_info)
        
        demo_buttons = QHBoxLayout()
        
        self.create_demo_btn = QPushButton("Create Demo Database")
        self.create_demo_btn.clicked.connect(self._on_create_demo)
        demo_buttons.addWidget(self.create_demo_btn)
        
        self.open_demo_btn = QPushButton("Open in QGIS")
        self.open_demo_btn.clicked.connect(self._on_open_demo)
        demo_buttons.addWidget(self.open_demo_btn)
        
        demo_layout.addLayout(demo_buttons)
        layout.addWidget(demo_group)
        
        # Log output
        log_group = QGroupBox("Log")
        log_layout = QVBoxLayout(log_group)
        
        self.log_text = QTextEdit()
        self.log_text.setReadOnly(True)
        self.log_text.setMaximumHeight(120)
        self.log_text.setStyleSheet("font-family: monospace; font-size: 10px;")
        log_layout.addWidget(self.log_text)
        
        layout.addWidget(log_group)
        
        # Close button
        close_layout = QHBoxLayout()
        close_layout.addStretch()
        
        self.check_updates_btn = QPushButton("Check for Updates")
        self.check_updates_btn.clicked.connect(self._on_check_updates)
        close_layout.addWidget(self.check_updates_btn)
        
        self.close_btn = QPushButton("Close")
        self.close_btn.clicked.connect(self.close)
        close_layout.addWidget(self.close_btn)
        
        layout.addLayout(close_layout)
    
    def _update_status(self):
        """Update the status display."""
        status = self.installer.get_status()
        platform = status['platform']
        
        # Build status text
        lines = [
            f"<b>Operating System:</b> {platform['os'].capitalize()} ({platform['arch']})",
            f"<b>GDAL Version:</b> {platform['gdal_version']}",
            f"<b>QGIS Version:</b> {platform['qgis_version']}",
            "",
        ]
        
        if status['driver_available']:
            lines.append('<span style="color: green;">✓ H2GIS driver is installed and available</span>')
        elif status['installed']:
            lines.append('<span style="color: orange;">⚠ H2GIS driver is installed but not loaded (restart QGIS)</span>')
        else:
            lines.append('<span style="color: red;">✗ H2GIS driver is not installed</span>')
        
        if status['supported']:
            lines.append(f'<span style="color: gray;">Pre-built binary: {status["artifact_name"]}</span>')
        else:
            lines.append('<span style="color: orange;">⚠ No pre-built binary for your configuration</span>')
        
        self.status_label.setText("<br>".join(lines))
        
        # Update button states
        self.uninstall_btn.setEnabled(status['installed'])
        self.open_demo_btn.setEnabled(self.demo_db.demo_exists())
        self.create_demo_btn.setEnabled(status['driver_available'])
    
    def _log(self, message: str, level: str = "info"):
        """Add a message to the log."""
        color = {
            "info": "black",
            "success": "green",
            "warning": "orange",
            "error": "red",
        }.get(level, "black")
        
        self.log_text.append(f'<span style="color: {color};">{message}</span>')
        
        # Also log to QGIS
        qgis_level = {
            "info": Qgis.Info,
            "success": Qgis.Success,
            "warning": Qgis.Warning,
            "error": Qgis.Critical,
        }.get(level, Qgis.Info)
        QgsMessageLog.logMessage(message, "H2GIS", qgis_level)
    
    def _on_install(self):
        """Handle install button click."""
        status = self.installer.get_status()
        
        if not status['supported']:
            # No pre-built binary available
            QMessageBox.warning(
                self,
                "No Pre-built Binary",
                f"No pre-built binary is available for your configuration:\n\n"
                f"OS: {status['platform']['os']}\n"
                f"Architecture: {status['platform']['arch']}\n"
                f"GDAL: {status['platform']['gdal_version']}\n\n"
                f"Please download a compatible binary manually from:\n"
                f"https://github.com/pierromond/H2GIS_gdalplugin/actions\n\n"
                f"Then use 'Install from File...' to install it."
            )
            return
        
        self._start_installation(None)
    
    def _on_install_from_file(self):
        """Handle install from file button click."""
        platform = self.installer.platform_info
        ext = platform['driver_extension']
        
        file_filter = f"GDAL Plugin (*{ext});;All Files (*)"
        file_path, _ = QFileDialog.getOpenFileName(
            self,
            "Select H2GIS Driver Binary",
            "",
            file_filter
        )
        
        if file_path:
            self._start_installation(Path(file_path))
    
    def _start_installation(self, driver_path: Optional[Path]):
        """Start the installation process."""
        self.progress_bar.setVisible(True)
        self.progress_bar.setValue(0)
        self.progress_label.setVisible(True)
        self.install_btn.setEnabled(False)
        self.browse_btn.setEnabled(False)
        
        self._log("Starting installation...")
        
        self.worker = InstallWorker(self.installer, driver_path)
        self.worker.progress.connect(self._on_progress)
        self.worker.finished.connect(self._on_install_finished)
        self.worker.start()
    
    def _on_progress(self, message: str, percent: int):
        """Handle progress updates."""
        self.progress_bar.setValue(percent)
        self.progress_label.setText(message)
        self._log(message)
    
    def _on_install_finished(self, success: bool, message: str):
        """Handle installation completion."""
        self.progress_bar.setVisible(False)
        self.progress_label.setVisible(False)
        self.install_btn.setEnabled(True)
        self.browse_btn.setEnabled(True)
        
        if success:
            self._log(message, "success")
            QMessageBox.information(self, "Installation Complete", message)
        else:
            self._log(message, "error")
            QMessageBox.warning(self, "Installation Failed", message)
        
        self._update_status()
    
    def _on_uninstall(self):
        """Handle uninstall button click."""
        reply = QMessageBox.question(
            self,
            "Confirm Uninstall",
            "Are you sure you want to uninstall the H2GIS driver?",
            QMessageBox.Yes | QMessageBox.No,
            QMessageBox.No
        )
        
        if reply == QMessageBox.Yes:
            success, message = self.installer.uninstall()
            if success:
                self._log(message, "success")
                QMessageBox.information(self, "Uninstall Complete", message)
            else:
                self._log(message, "error")
                QMessageBox.warning(self, "Uninstall Failed", message)
            
            self._update_status()
    
    def _on_create_demo(self):
        """Handle create demo button click."""
        self._log("Creating demo database...")
        
        success, message = self.demo_db.create_demo_database()
        
        if success:
            self._log(message, "success")
            QMessageBox.information(self, "Demo Created", message)
            self._update_status()
            
            # Ask if user wants to open the demo
            reply = QMessageBox.question(
                self,
                "Open Demo?",
                "Demo database created successfully.\n\n"
                "Would you like to open it in QGIS now?",
                QMessageBox.Yes | QMessageBox.No,
                QMessageBox.Yes
            )
            if reply == QMessageBox.Yes:
                self._on_open_demo()
        else:
            self._log(message, "error")
            QMessageBox.warning(self, "Demo Creation Failed", message)
    
    def _on_open_demo(self):
        """Handle open demo button click."""
        success, message = self.demo_db.open_demo_in_qgis()
        
        if success:
            self._log(message, "success")
            QMessageBox.information(self, "Demo Opened", message)
        else:
            self._log(message, "error")
            QMessageBox.warning(self, "Open Failed", message)
    
    def _on_check_updates(self):
        """Handle check updates button click."""
        from .downloader import check_for_updates
        
        self._log("Checking for updates...")
        
        update_available, new_version = check_for_updates()
        
        if update_available:
            self._log(f"New version available: {new_version}", "info")
            QMessageBox.information(
                self,
                "Update Available",
                f"A new version ({new_version}) is available.\n\n"
                f"Please reinstall to update."
            )
        else:
            self._log("No updates available", "info")
            QMessageBox.information(
                self,
                "No Updates",
                "You have the latest version."
            )
