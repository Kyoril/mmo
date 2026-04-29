[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

function Assert-CommandExists {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required command '$Name' was not found in PATH."
    }
}

function Invoke-ExternalStep {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [Parameter(Mandatory = $true)]
        [scriptblock]$Command
    )

    Write-Host "`n==> $Name" -ForegroundColor Cyan
    & $Command
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "$Name failed with exit code $exitCode."
    }
}

Push-Location $repoRoot
try {
    foreach ($commandName in 'cmake', 'ctest', 'rg') {
        Assert-CommandExists -Name $commandName
    }

    foreach ($requiredPath in 'CMakeLists.txt', 'build', 'src/mmo_bot/README.md', 'src/mmo_bot/bot_actions/companion_follow_action.cpp') {
        if (-not (Test-Path $requiredPath)) {
            throw "Expected repo path '$requiredPath' was not found from $repoRoot."
        }
    }

    Invoke-ExternalStep -Name 'Build Debug unit_tests + mmo_bot' -Command {
        cmake --build build --config Debug --target unit_tests mmo_bot
    }

    $unitTestsExe = Join-Path $repoRoot 'bin/Debug/unit_tests.exe'
    if (-not (Test-Path $unitTestsExe)) {
        throw "Expected unit test binary '.\bin\Debug\unit_tests.exe' was not found after the build."
    }

    $focusedSuites = @(
        @{ Name = 'Focused warrior runtime suite'; Filter = '[bot-warrior][runtime]' },
        @{ Name = 'Focused cleric runtime suite'; Filter = '[bot-cleric][runtime]' },
        @{ Name = 'Focused mage runtime suite'; Filter = '[bot-mage][runtime]' },
        @{ Name = 'Focused companion runtime suite'; Filter = '[bot-companion][runtime]' },
        @{ Name = 'Focused follow runtime suite'; Filter = '[bot-follow][runtime]' }
    )

    foreach ($suite in $focusedSuites) {
        $filter = $suite.Filter
        Invoke-ExternalStep -Name $suite.Name -Command {
            & $unitTestsExe $filter
        }
    }

    Invoke-ExternalStep -Name 'Aggregate CTest unit_tests proof' -Command {
        ctest --test-dir build -C Debug --output-on-failure -R '^unit_tests$'
    }

    $readmePath = Join-Path $repoRoot 'src/mmo_bot/README.md'
    $runtimePath = Join-Path $repoRoot 'src/mmo_bot/bot_actions/companion_follow_action.cpp'

    $readmeInventory = @(
        'Integrated recovery proof',
        'live session checklist',
        'cast_failure_backoff',
        'follow_runtime_blocked',
        'companion mode=',
        'anchor decision='
    )

    foreach ($pattern in $readmeInventory) {
        Invoke-ExternalStep -Name "README inventory: $pattern" -Command {
            rg -n --fixed-strings $pattern $readmePath
        }
    }

    $runtimeInventory = @(
        'companion mode=',
        'anchor decision=',
        'warrior action=',
        'warrior failure=',
        'cleric action=',
        'cleric failure=',
        'mage action=',
        'mage failure='
    )

    foreach ($pattern in $runtimeInventory) {
        Invoke-ExternalStep -Name "Runtime inventory: $pattern" -Command {
            rg -n --fixed-strings $pattern $runtimePath
        }
    }

    Write-Host "`nS07 recovery proof completed successfully." -ForegroundColor Green
}
finally {
    Pop-Location
}
