#!/usr/bin/env python3
"""
Version Management Script for ESP32-S3-SIM7670G GPS Tracker

This script helps manage version numbers across all project files.
Usage:
    python update_version.py --version 1.0.1
    python update_version.py --show
    python update_version.py --bump major|minor|patch
"""

import argparse
import re
import os
from pathlib import Path

PROJECT_ROOT = Path(__file__).parent
VERSION_FILE = PROJECT_ROOT / "VERSION"
VERSION_H = PROJECT_ROOT / "main" / "version.h"
README_FILE = PROJECT_ROOT / "README.md"
CHANGELOG_FILE = PROJECT_ROOT / "CHANGELOG.md"

def read_current_version():
    """Read current version from VERSION file"""
    if VERSION_FILE.exists():
        return VERSION_FILE.read_text().strip()
    return "0.0.0"

def parse_version(version_str):
    """Parse version string into major, minor, patch components"""
    match = re.match(r'^(\d+)\.(\d+)\.(\d+)$', version_str)
    if not match:
        raise ValueError(f"Invalid version format: {version_str}")
    return tuple(map(int, match.groups()))

def bump_version(current_version, bump_type):
    """Bump version based on type (major, minor, patch)"""
    major, minor, patch = parse_version(current_version)
    
    if bump_type == 'major':
        return f"{major + 1}.0.0"
    elif bump_type == 'minor':
        return f"{major}.{minor + 1}.0"
    elif bump_type == 'patch':
        return f"{major}.{minor}.{patch + 1}"
    else:
        raise ValueError(f"Invalid bump type: {bump_type}")

def update_version_file(new_version):
    """Update VERSION file"""
    VERSION_FILE.write_text(new_version + '\n')
    print(f"✓ Updated VERSION file: {new_version}")

def update_version_h(new_version):
    """Update version.h file"""
    major, minor, patch = parse_version(new_version)
    
    content = VERSION_H.read_text()
    
    # Update version components
    content = re.sub(r'#define PROJECT_VERSION_MAJOR \d+', f'#define PROJECT_VERSION_MAJOR {major}', content)
    content = re.sub(r'#define PROJECT_VERSION_MINOR \d+', f'#define PROJECT_VERSION_MINOR {minor}', content)
    content = re.sub(r'#define PROJECT_VERSION_PATCH \d+', f'#define PROJECT_VERSION_PATCH {patch}', content)
    content = re.sub(r'#define PROJECT_VERSION_STRING "[^"]*"', f'#define PROJECT_VERSION_STRING "{new_version}"', content)
    
    VERSION_H.write_text(content)
    print(f"✓ Updated version.h: {new_version}")

def update_readme(new_version):
    """Update README.md badge"""
    content = README_FILE.read_text()
    content = re.sub(
        r'\[!\[Version\]\(https://img\.shields\.io/badge/version-[^-]+-blue\.svg\)\]',
        f'[![Version](https://img.shields.io/badge/version-{new_version}-blue.svg)]',
        content
    )
    README_FILE.write_text(content)
    print(f"✓ Updated README.md badge: {new_version}")

def show_version_info():
    """Show current version information"""
    current = read_current_version()
    major, minor, patch = parse_version(current)
    
    print(f"Current Version: {current}")
    print(f"  Major: {major}")
    print(f"  Minor: {minor}")
    print(f"  Patch: {patch}")
    print(f"\nNext versions would be:")
    print(f"  Major: {bump_version(current, 'major')}")
    print(f"  Minor: {bump_version(current, 'minor')}")
    print(f"  Patch: {bump_version(current, 'patch')}")

def update_all_files(new_version):
    """Update version in all project files"""
    print(f"Updating version to: {new_version}")
    
    update_version_file(new_version)
    update_version_h(new_version)
    update_readme(new_version)
    
    print(f"\n✅ Version updated to {new_version} in all files!")
    print(f"\nNext steps:")
    print(f"1. Update CHANGELOG.md with new version notes")
    print(f"2. Commit changes: git add . && git commit -m 'Bump version to {new_version}'")
    print(f"3. Create tag: git tag v{new_version}")
    print(f"4. Push: git push origin main --tags")

def main():
    parser = argparse.ArgumentParser(description='Manage project version numbers')
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--version', help='Set specific version (e.g., 1.0.1)')
    group.add_argument('--bump', choices=['major', 'minor', 'patch'], help='Bump version component')
    group.add_argument('--show', action='store_true', help='Show current version info')
    
    args = parser.parse_args()
    
    if args.show:
        show_version_info()
    elif args.version:
        # Validate version format
        parse_version(args.version)
        update_all_files(args.version)
    elif args.bump:
        current = read_current_version()
        new_version = bump_version(current, args.bump)
        update_all_files(new_version)

if __name__ == '__main__':
    main()