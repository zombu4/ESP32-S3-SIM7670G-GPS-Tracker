#!/usr/bin/env pwsh
# PowerShell script for quick version bump and commit
param(
    [Parameter(Mandatory=$true)]
    [ValidateSet("major", "minor", "patch")]
    [string]$Type,
    
    [Parameter(Mandatory=$true)]
    [string]$Message
)

Write-Host "🔄 Bumping $Type version..." -ForegroundColor Blue

# Bump version
python update_version.py --bump $Type

if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ Version bump failed!" -ForegroundColor Red
    exit 1
}

# Get new version
$version = Get-Content VERSION | Select-Object -First 1

Write-Host "📝 New version: $version" -ForegroundColor Green

# Stage all changes  
git add .

# Commit with version info
$commitMessage = "v${version}: $Message"
git commit -m $commitMessage

if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ Commit failed!" -ForegroundColor Red
    exit 1
}

# Create tag
git tag "v$version"

Write-Host "✅ Version bumped to v$version and committed!" -ForegroundColor Green
Write-Host "📋 Commit: $commitMessage" -ForegroundColor Cyan
Write-Host "🏷️  Tag: v$version created" -ForegroundColor Cyan
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Yellow
Write-Host "1. Review changes: git log --oneline -5" -ForegroundColor White  
Write-Host "2. Push: git push origin main --tags" -ForegroundColor White