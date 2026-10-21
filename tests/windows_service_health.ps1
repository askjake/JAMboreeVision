# No services/network are accessed. Reproduce the SCM startup transition race.
$ErrorActionPreference = 'Stop'
$tokens=$null; $errors=$null
$path=Join-Path $PSScriptRoot '..\deploy\windows\Manage-Service.ps1'
$ast=[Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors)
if($errors.Count){throw ($errors|Out-String)}
$fn=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Wait-Healthy'},$true)
Invoke-Expression $fn.Extent.Text
$script:checks=0; $script:http=0; $script:sleeps=0; $Port=5004
function Get-Service {
    $script:checks++
    $status=if($script:checks -eq 1){'Stopped'}elseif($script:checks -eq 2){'StartPending'}else{'Running'}
    [pscustomobject]@{Status=$status}
}
function Invoke-RestMethod {
    $script:http++
    if($script:http -eq 1){return @{stbs=@{}}}
    return @{stbs=@{};remotes=@()}
}
function Start-Sleep {$script:sleeps++}
Wait-Healthy
if($script:checks -ne 4 -or $script:http -ne 2 -or $script:sleeps -ne 3){throw 'Startup was not polled through SCM and HTTP readiness'}
Write-Host 'PASS: stopped/pending startup transitions and incomplete HTTP readiness are retried.'
