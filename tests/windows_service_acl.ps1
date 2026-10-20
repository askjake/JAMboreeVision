# Native Windows regression: repair an empty file DACL without broad user grants.
$ErrorActionPreference = 'Stop'
$tokens=$null; $errors=$null
$manager = Join-Path $PSScriptRoot '..\deploy\windows\Manage-Service.ps1'
$ast = [System.Management.Automation.Language.Parser]::ParseFile($manager,[ref]$tokens,[ref]$errors)
if ($errors.Count) { throw 'Service manager parse failed' }
foreach ($name in 'Invoke-Native','Grant-Tree') {
    $function = $ast.Find({param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name},$true)
    . ([scriptblock]::Create($function.Extent.Text))
}
$fixture = Join-Path $env:TEMP ('jv-acl-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path "$fixture\nested" -Force | Out-Null
$file = "$fixture\nested\probe.txt"
Set-Content -LiteralPath $file -Value 'fixture'
try {
    $acl = Get-Acl -LiteralPath $file
    $acl.SetAccessRuleProtection($true,$false)
    foreach ($rule in @($acl.Access)) { $acl.RemoveAccessRuleSpecific($rule) }
    Set-Acl -LiteralPath $file -AclObject $acl
    if ((Get-Acl -LiteralPath $file).Access.Count -ne 0) { throw 'Empty ACL fixture failed' }
    Grant-Tree $fixture 'RX'
    if ((Get-Content -LiteralPath $file) -ne 'fixture') { throw 'File unreadable after repair' }
    $sidRules = (Get-Acl -LiteralPath $file).GetAccessRules($true,$true,[Security.Principal.SecurityIdentifier])
    $service = @($sidRules | Where-Object { $_.IdentityReference.Value -eq 'S-1-5-19' -and $_.AccessControlType -eq 'Allow' })
    if (-not $service -or ($service[0].FileSystemRights -band [Security.AccessControl.FileSystemRights]::ReadAndExecute) -ne [Security.AccessControl.FileSystemRights]::ReadAndExecute) { throw 'LocalService cannot read/execute inherited file' }
    if (@($sidRules | Where-Object { $_.IdentityReference.Value -in @('S-1-1-0','S-1-5-32-545') }).Count) { throw 'Unexpected broad user grant' }
    'Windows service ACL regression: PASS'
} finally {
    Remove-Item -LiteralPath $fixture -Recurse -Force
}
