# bench.ps1 -- Barrido de mediciones para el Anexo 3 del informe.
#
# Corre la simulacion sin ventana variando N y el numero de hilos, con varias
# repeticiones por combinacion, y escribe resultados/bench.csv con el tiempo por
# frame, el speedup y la eficiencia.
#
# Uso:   .\scripts\bench.ps1
#        .\scripts\bench.ps1 -Repeticiones 10 -Frames 60

param(
    [int]   $Repeticiones = 10,                       # el enunciado pide minimo 10
    [int]   $Frames       = 50,                       # frames medidos por corrida
    [int[]] $ListaN       = @(1000, 2000, 4000, 8000, 16000),
    [int[]] $ListaHilos   = @(1, 2, 4, 8, 16, 32),
    [string]$Modo         = "colision",
    [string]$Schedule     = "dynamic",
    [string]$Salida       = "resultados/bench.csv",
    [switch]$SinAfinidad                              # desactiva el anclaje a P-core
)

$ErrorActionPreference = "Stop"
$raiz = Split-Path -Parent $PSScriptRoot
Set-Location $raiz

$seq = Join-Path $raiz "nbody_seq.exe"
$par = Join-Path $raiz "nbody_par.exe"
foreach ($bin in @($seq, $par)) {
    if (-not (Test-Path $bin)) { throw "Falta $bin. Compile primero con 'make'." }
}

New-Item -ItemType Directory -Force -Path (Split-Path $Salida) | Out-Null
$filas = New-Object System.Collections.Generic.List[object]

# Corre una medicion y devuelve la fila CSV ya partida en campos.
#
# Las corridas de UN SOLO hilo se anclan al procesador logico 0. Sin esto,
# Windows detecta el proceso largo de un hilo y lo baja a un E-core: medido, el
# secuencial con N=16000 pasa de 161 ms/paso en corridas cortas a 683 ms en
# corridas largas, y el speedup sale superlineal (eficiencia >100%), que es
# imposible. La referencia serial debe medirse en un nucleo rapido; las corridas
# multi-hilo se dejan libres para que usen toda la maquina.
function Invoke-Medicion {
    param([string]$Binario, [int]$N, [int]$Hilos, [int]$Rep)
    $lista = @("--bench", "-n", $N, "-f", $Frames, "-m", $Modo,
               "--schedule", $Schedule, "-t", $Hilos)

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName               = $Binario
    $psi.Arguments              = ($lista -join " ")
    $psi.RedirectStandardOutput = $true
    $psi.UseShellExecute        = $false
    $psi.CreateNoWindow         = $true

    $proc = [System.Diagnostics.Process]::Start($psi)
    if (-not $SinAfinidad -and $Hilos -le 1) {
        try {
            $proc.ProcessorAffinity = [IntPtr]1   # procesador logico 0 = un P-core
            $proc.PriorityClass     = [System.Diagnostics.ProcessPriorityClass]::High
        } catch { }                               # si el SO no deja, se mide igual
    }
    $linea = $proc.StandardOutput.ReadToEnd()
    $proc.WaitForExit()
    if ($proc.ExitCode -ne 0) { throw "Fallo la medicion N=$N hilos=$Hilos" }
    $c = $linea.Trim().Split(",")
    [pscustomobject]@{
        version   = if ($Binario -eq $seq) { "secuencial" } else { "paralelo" }
        anclado   = (-not $SinAfinidad -and $Hilos -le 1)
        modo      = $c[0]
        n         = [int]$c[1]
        hilos     = [int]$c[2]
        schedule  = $c[3]
        rep       = $Rep
        ms_fisica = [double]$c[6]
        ms_render = [double]$c[7]
        ms_frame  = [double]$c[8]
        fps       = [double]$c[9]
    }
}

$totalPasos = $ListaN.Count * (1 + $ListaHilos.Count) * $Repeticiones
$paso = 0

foreach ($n in $ListaN) {
    # Referencia: el binario secuencial, sin OpenMP. Es el denominador honesto
    # del speedup, porque -fopenmp por si solo ya cambia el codigo generado.
    for ($r = 1; $r -le $Repeticiones; $r++) {
        $paso++
        Write-Progress -Activity "Midiendo" -Status "N=$n secuencial rep $r" `
                       -PercentComplete (100 * $paso / $totalPasos)
        $filas.Add((Invoke-Medicion -Binario $seq -N $n -Hilos 1 -Rep $r))
    }
    foreach ($t in $ListaHilos) {
        for ($r = 1; $r -le $Repeticiones; $r++) {
            $paso++
            Write-Progress -Activity "Midiendo" -Status "N=$n hilos=$t rep $r" `
                           -PercentComplete (100 * $paso / $totalPasos)
            $filas.Add((Invoke-Medicion -Binario $par -N $n -Hilos $t -Rep $r))
        }
    }
}
Write-Progress -Activity "Midiendo" -Completed

# Speedup y eficiencia contra la mediana secuencial del mismo N. Se usa la
# mediana y no el promedio para que un pico del sistema no contamine la base.
$baseP = @{}
foreach ($g in ($filas | Where-Object version -eq "secuencial" | Group-Object n)) {
    $orden = ($g.Group.ms_frame | Sort-Object)
    $baseP[[int]$g.Name] = $orden[[int]($orden.Count / 2)]
}

$conMetricas = $filas | ForEach-Object {
    $sp = $baseP[$_.n] / $_.ms_frame
    # La eficiencia solo tiene sentido para el binario paralelo: la fila
    # secuencial es la referencia, compararla consigo misma no dice nada.
    $ef = if ($_.version -eq "paralelo") { [math]::Round($sp / $_.hilos, 3) } else { $null }
    $_ | Add-Member -NotePropertyName speedup    -NotePropertyValue ([math]::Round($sp, 3)) -PassThru |
         Add-Member -NotePropertyName eficiencia -NotePropertyValue $ef -PassThru
}

$conMetricas | Export-Csv -Path $Salida -NoTypeInformation -Encoding utf8
Write-Output "Escrito $Salida  ($($conMetricas.Count) mediciones)"
Write-Output ""

# Resumen en pantalla: mediana por combinacion
Write-Output "N        hilos   ms/frame    FPS   speedup  eficiencia"
Write-Output "------------------------------------------------------"
$grupos = $conMetricas | Group-Object n, hilos, version |
    Sort-Object { [int]($_.Group[0].n) },
                 { if ($_.Group[0].version -eq "secuencial") { -1 } else { [int]($_.Group[0].hilos) } }
foreach ($g in $grupos) {
    $f = $g.Group[0]
    $ms  = ($g.Group.ms_frame | Measure-Object -Average).Average
    $fps = ($g.Group.fps      | Measure-Object -Average).Average
    $sp  = ($g.Group.speedup  | Measure-Object -Average).Average
    if ($f.version -eq "secuencial") {
        "{0,6}   {1,5}   {2,8:N3}  {3,6:N1}   {4,6:N2}x  {5,8}" -f $f.n, "seq", $ms, $fps, $sp, "--"
    } else {
        $ef = ($g.Group.eficiencia | Measure-Object -Average).Average
        "{0,6}   {1,5}   {2,8:N3}  {3,6:N1}   {4,6:N2}x  {5,8:P0}" -f $f.n, $f.hilos, $ms, $fps, $sp, $ef
    }
}
