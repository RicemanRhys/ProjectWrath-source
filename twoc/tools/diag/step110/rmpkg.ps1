# rmpkg.ps1 -Dir DIR : deletes a test folder made by mkpkg.ps1: every junction under it removed as a link first
# (never followed), then the rest. Refuses a folder outside the session's scratch or the system temp folder.
param([Parameter(Mandatory)][string]$Dir)
$ErrorActionPreference = 'Stop'
$full = (Resolve-Path -LiteralPath $Dir).Path
if ($full -notlike "$env:TEMP\*") { throw "not under %TEMP%: $full" }
Get-ChildItem -LiteralPath $full -Recurse -Force -Attributes ReparsePoint -ErrorAction SilentlyContinue |
    ForEach-Object { cmd /c rmdir "$($_.FullName)" }
# a second pass: nothing that is a link may remain before the recursive delete
if (Get-ChildItem -LiteralPath $full -Recurse -Force -Attributes ReparsePoint -ErrorAction SilentlyContinue) {
    throw "a link is still there under $full"
}
Remove-Item -LiteralPath $full -Recurse -Force
Write-Output "removed $full"
