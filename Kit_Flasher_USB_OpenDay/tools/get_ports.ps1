$ports = Get-CimInstance Win32_SerialPort -ErrorAction SilentlyContinue
if ($ports) {
    foreach ($p in $ports) {
        Write-Output "$($p.DeviceID)|$($p.Caption)"
    }
} else {
    foreach ($p in [System.IO.Ports.SerialPort]::GetPortNames()) {
        Write-Output "$p|$p"
    }
}
