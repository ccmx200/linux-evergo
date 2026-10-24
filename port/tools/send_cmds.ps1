# Send a list of plain commands over serial, avoiding PowerShell interpolation
# pitfalls: no $() and no redirection, so nothing is eaten before it reaches the
# phone.
param([string]$Port = 'COM20', [string]$CmdFile = '',
      [string[]]$Cmd = @(), [int]$WaitMs = 1500)

$sp = New-Object System.IO.Ports.SerialPort $Port, 115200, 'None', 8, 'One'
$sp.ReadTimeout = 250
$sp.DtrEnable = $true
$sp.RtsEnable = $true
$sp.Open()
Start-Sleep -Milliseconds 200

function Drain([int]$ms) {
    $sb = New-Object System.Text.StringBuilder
    $end = (Get-Date).AddMilliseconds($ms)
    while ((Get-Date) -lt $end) {
        try {
            $buf = New-Object byte[] 4096
            $n = $sp.Read($buf, 0, $buf.Length)
            if ($n -gt 0) { [void]$sb.Append([System.Text.Encoding]::UTF8.GetString($buf, 0, $n)) }
        } catch [TimeoutException] { }
    }
    return $sb.ToString()
}

$list = @()
if ($CmdFile -and (Test-Path $CmdFile)) {
    $list = Get-Content $CmdFile | Where-Object { $_.Trim() -ne '' }
} elseif ($Cmd.Count -gt 0) {
    $list = $Cmd
}

# break out of any continuation prompt
$sp.Write([char]3)
Start-Sleep -Milliseconds 200
$sp.Write("`r`n")
[void](Drain 500)

foreach ($c in $list) {
    if ($c.TrimStart().StartsWith('#')) { Write-Output "  (skip) $c"; continue }
    Write-Output ">> $c"
    $sp.Write("$c`r`n")
    $out = Drain $WaitMs
    Write-Output (($out -replace "`r", "").TrimEnd())
    Write-Output ""
}

$sp.Close()
$sp.Dispose()