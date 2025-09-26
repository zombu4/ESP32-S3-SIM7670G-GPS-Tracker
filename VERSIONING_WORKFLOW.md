# Version Management Workflow

## 🔄 **CRITICAL RULE: Always Advance Versioning**

Every code change, feature addition, or bug fix **MUST** advance the version number. This ensures proper tracking and prevents confusion.

## Version Advancement Rules

### When to Bump Version Components:

**🔴 MAJOR (1.0.0 → 2.0.0)**
- Breaking API changes
- Hardware compatibility changes
- Major architecture overhauls
- Changes that require user config migration

**🟡 MINOR (1.0.0 → 1.1.0)**  
- New features added
- New module implementations
- Significant functionality additions
- Non-breaking API improvements

**🟢 PATCH (1.0.0 → 1.0.1)**
- Bug fixes
- Documentation updates  
- Minor code improvements
- Configuration tweaks
- Build system updates

## 🚀 Version Update Process

### Before ANY Code Changes:
```powershell
# 1. Check current version
python update_version.py --show

# 2. Advance version (choose appropriate bump)
python update_version.py --bump patch   # For bug fixes
python update_version.py --bump minor   # For new features  
python update_version.py --bump major   # For breaking changes

# 3. Update CHANGELOG.md with changes
# 4. Build and test
cd "C:\Espressif\frameworks\esp-idf-v5.5"; .\export.ps1; cd "c:\Users\dom\Documents\esp-idf-tracker"; idf.py build

# 5. Commit with version info
git add .
git commit -m "v{VERSION}: {DESCRIPTION OF CHANGES}"

# 6. Create version tag
git tag v{VERSION}

# 7. Push with tags
git push origin main --tags
```

### Example Commit Messages:
```
v1.0.1: Fix GPS parsing bug in NMEA coordinate conversion
v1.1.0: Add battery temperature monitoring feature  
v2.0.0: Migrate to new SIM7670G AT command interface
```

## 🔧 Automated Version Scripts

**Quick version bump and commit:**
```powershell
# Create bump_and_commit.ps1
param($type, $message)
python update_version.py --bump $type
$version = Get-Content VERSION
git add .
git commit -m "v${version}: $message"
git tag v$version
Write-Host "✅ Version bumped to $version and committed"
```

**Usage:**
```powershell
.\bump_and_commit.ps1 patch "Fix MQTT connection timeout issue"
.\bump_and_commit.ps1 minor "Add GPS waypoint logging feature"
```

## 📋 Version Checklist

Before pushing ANY changes:
- [ ] Version number advanced appropriately
- [ ] CHANGELOG.md updated with new version  
- [ ] Build successful with new version
- [ ] Version displayed correctly in logs
- [ ] Commit message includes version number
- [ ] Git tag created for version
- [ ] All version files consistent

## 🎯 Current Version Status

**Current Version:** 1.0.0
**Status:** Initial Release - Development
**Next Expected:** 1.0.1 (bug fixes) or 1.1.0 (features)

## ⚠️ Never Skip Versioning

**Always increment version for:**
- Code changes of any kind
- Configuration updates
- Documentation changes  
- Build system modifications
- Dependency updates

**This ensures:**
- Clear change tracking
- Easy rollback capability  
- Proper release management
- Solid development standards