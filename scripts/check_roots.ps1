# Verifies which root CA each firmware-contacted host chains to,
# so certs/roots.pem coverage can be confirmed after regeneration.
$hosts = @(
    "api.open-meteo.com",
    "app.yasno.ua",
    "eu1-developer.deyecloud.com",
    "api.exchangerate.host",
    "github.com",
    "objects.githubusercontent.com",
    "pool.ntp.org"  # NTP is plain UDP; listed only to note it needs no cert
)

foreach ($h in $hosts) {
    if ($h -eq "pool.ntp.org") { Write-Output "$h : (NTP, no TLS)"; continue }
    try {
        $tcp = New-Object Net.Sockets.TcpClient($h, 443)
        $ssl = New-Object Net.Security.SslStream($tcp.GetStream(), $false, { param($s, $cert, $chain, $errs) $true })
        $ssl.AuthenticateAsClient($h)
        $cert2 = New-Object Security.Cryptography.X509Certificates.X509Certificate2($ssl.RemoteCertificate)
        $chain = New-Object Security.Cryptography.X509Certificates.X509Chain
        $chain.ChainPolicy.RevocationMode = "NoCheck"
        $null = $chain.Build($cert2)
        $root = $chain.ChainElements[$chain.ChainElements.Count - 1].Certificate
        Write-Output ("{0} : leaf issuer [{1}] root [{2}]" -f $h, $cert2.Issuer.Split(',')[0], $root.Subject)
        $ssl.Dispose(); $tcp.Close()
    } catch {
        Write-Output "$h : ERROR $($_.Exception.Message)"
    }
}
