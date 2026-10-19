param(
    [Parameter(Mandatory = $true)][string]$JavaHome,
    [string]$Maven = 'mvn',
    [string]$MavenRepository
)
$ErrorActionPreference = 'Stop'
$module = Split-Path $PSScriptRoot -Parent
$java = Join-Path $JavaHome 'bin/java.exe'
$javac = Join-Path $JavaHome 'bin/javac.exe'
if (!(Test-Path -LiteralPath $java) -or !(Test-Path -LiteralPath $javac)) { throw 'JavaHome 必须指向已有 JDK 21。' }
$mavenArgs = @('-f', (Join-Path $module 'pom.xml'), '-B', '-ntp', 'test')
if ($MavenRepository) { $mavenArgs = @("-Dmaven.repo.local=$MavenRepository") + $mavenArgs }
& $Maven @mavenArgs
if ($LASTEXITCODE -ne 0) { throw 'Java 构建或测试失败。' }
$fixtureClasses = Join-Path $module 'out/fixture-classes'
[void][IO.Directory]::CreateDirectory($fixtureClasses)
& $javac --release 21 -encoding UTF-8 -d $fixtureClasses (Join-Path $PSScriptRoot 'fixtures/ResultCommand.java')
if ($LASTEXITCODE -ne 0) { throw '测试故障进程构建失败。' }
Copy-Item -LiteralPath (Join-Path $module 'contracts/fixtures/result-demo-success.json') -Destination (Join-Path $fixtureClasses 'response-template.json')
$realClasses = Join-Path $module ('out/中文 class 目录-' + [Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($realClasses)
Get-ChildItem -LiteralPath (Join-Path $module 'target/classes') -Force | Copy-Item -Destination $realClasses -Recurse
dotnet run --project (Join-Path $PSScriptRoot 'Caller.Tests/Caller.Tests.csproj') --configuration Release -- $module $java $realClasses $fixtureClasses
if ($LASTEXITCODE -ne 0) { throw 'C# 接收校验或跨语言联调失败。' }
