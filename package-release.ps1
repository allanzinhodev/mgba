<#
    Empacota o build num .zip pronto para o release.

        powershell -ExecutionPolicy Bypass -File package-release.ps1

    Gera dist\mgba-ffta-assets-<versao>-win64.zip


    POR QUE AS DLLs VAO JUNTO

    O triplet e o dinamico por imposicao: libepoxy nao suporta CRT estatica no
    Windows, e o epoxy nao e opcional -- o CMakeLists do mGBA tem um
    FATAL_ERROR que nem USE_EPOXY=OFF contorna.

    Entao o .exe sozinho nao roda, e quem baixa nao tem como adivinhar quais
    DLLs faltam: o Windows so diz "o codigo nao pode prosseguir". Por isso o
    pacote leva todas as DLLs que o build produziu, e nao uma lista escrita a
    mao aqui, que envelheceria calada na proxima dependencia.
#>

$ErrorActionPreference = 'Stop'
$raiz = Split-Path -Parent $MyInvocation.MyCommand.Path
$build = Join-Path $raiz 'build'
$exe = Join-Path $build 'mgba-sdl.exe'

if (-not (Test-Path $exe)) {
    throw "mgba-sdl.exe nao encontrado em $build -- rode build-windows.ps1 antes"
}

$versao = (Get-Content (Join-Path $raiz 'version.cmake') |
    Select-String -Pattern 'set\(LIB_VERSION_(MAJOR|MINOR|PATCH) (\d+)\)' |
    ForEach-Object { $_.Matches[0].Groups[2].Value }) -join '.'

$nome = "mgba-ffta-assets-$versao-win64"
$stage = Join-Path $raiz "dist\$nome"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage -Force | Out-Null

Copy-Item $exe $stage
Get-ChildItem (Join-Path $build '*.dll') | Copy-Item -Destination $stage

# O compositor e parte do produto: sem ele o despejo nao vira PNG.
$tools = Join-Path $stage 'tools\ffta-sprites'
New-Item -ItemType Directory -Path $tools -Force | Out-Null
Copy-Item (Join-Path $raiz 'tools\ffta-sprites\compose.js') $tools

Copy-Item (Join-Path $raiz 'FFTA-ASSETS.md') $stage
Copy-Item (Join-Path $raiz 'LICENSE') $stage

$zip = Join-Path $raiz "dist\$nome.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path "$stage\*" -DestinationPath $zip

Write-Output "versao: $versao"
Write-Output "pacote: $zip"
Write-Output "conteudo:"
Get-ChildItem $stage -Recurse -File | ForEach-Object {
    "  {0,-28} {1,10:N0} bytes" -f $_.Name, $_.Length
}
