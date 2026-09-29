# Launches a night run (work_orders/README.md, "What the run does") at BelowNormal
# priority, so the machine stays usable while the run builds and probes.
#
# Windows starts a child of a BelowNormal process at BelowNormal too, so every process the
# run starts (subagents' shells, cl, probes, the game) inherits it. The run still uses every
# idle core; whatever the developer does gets the cores first. Six orders' probes at normal
# priority put ~50 threads on 8 cores and made the desktop unusable (2026-09-28).
#
# Run from the repo root: powershell -File tools\night.ps1 [claude arguments]

$self = Get-Process -Id $PID
$previousPriority = $self.PriorityClass
try {
    $self.PriorityClass = 'BelowNormal'
    Write-Host "Night run: priority BelowNormal"
    & claude @args
}
finally {
    $self.PriorityClass = $previousPriority
}
