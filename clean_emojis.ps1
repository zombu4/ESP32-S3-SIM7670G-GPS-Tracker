# PowerShell script to remove all emoji characters from modem_init.c
$filePath = "main\modules\modem_init\modem_init.c"
$content = Get-Content $filePath -Raw -Encoding UTF8

# Replace all emoji patterns with clean text
$content = $content `
    -replace 'ðŸ"§\s*', '' `
    -replace 'âŒ\s*', '' `
    -replace 'ðŸ"¡\s*', '' `
    -replace 'âœ…\s*', '' `
    -replace 'ðŸ"±\s*', '' `
    -replace 'âš\s*ï¿½ï¿½\s*', 'WARNING: ' `
    -replace 'ðŸ"¶\s*', '' `
    -replace 'ðŸ"Š\s*', '' `
    -replace 'ðŸŒ\s*', '' `
    -replace 'ðŸ"—\s*', '' `
    -replace 'â³\s*', '' `
    -replace 'ðŸ›°ï¿½ï¿½\s*', '' `
    -replace 'ðŸ"Œ\s*', '' `
    -replace 'ðŸ"„\s*', '' `
    -replace 'ðŸ"\s*', '' `
    -replace 'ðŸŽ‰\s*', 'SUCCESS! ' `
    -replace 'ðŸ"­\s*', '' `
    -replace 'ðŸ§ª\s*', '' `
    -replace 'ï¿½\s*', '' `
    -replace 'âš\s*ï¿½\s*', 'WARNING: ' `
    -replace 'ðŸš€\s*', '' `
    -replace 'ðŸ"–\s*', '' `
    -replace 'ðŸ›°\s*', '' `
    -replace 'ðŸŽ¯\s*', '' `
    -replace 'â°\s*', ''

# Write back to file
$content | Out-File -FilePath $filePath -Encoding UTF8 -NoNewline

Write-Host "Emoji cleanup completed for $filePath"