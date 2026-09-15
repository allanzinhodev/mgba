<#
    Build do mGBA no Windows (MSVC + Ninja + vcpkg).

        powershell -ExecutionPolicy Bypass -File build-windows.ps1

    Gera build\mgba.exe -- o frontend SDL, com o asset tap embutido.


    POR QUE POWERSHELL E NAO UM .BAT

    A primeira versao disto era um .bat, no mesmo molde do resto do projeto, e
    nao sobreviveu: `for /f` com o caminho do vswhere (que contem "(x86)") e a
    passagem de argumentos com espaco para o cmake se combinam mal, e o
    sintoma nao aponta para a causa -- o cmake reclamava de "toolchain file
    nao encontrado" mostrando meio caminho, porque o argumento tinha sido
    partido num espaco.

    Aqui a montagem dos argumentos e uma lista, nao uma string, entao nao ha
    o que partir.


    POR QUE UM BUILD REDUZIDO

    O alvo e extrair sprites do Final Fantasy Tactics Advance, nao jogar. Qt,
    FFmpeg, libzip, sqlite3 e LZMA nao servem a isso e cada um e uma
    dependencia a mais. Ficam zlib e libpng, que o asset tap usa para gravar.
#>

$ErrorActionPreference = 'Stop'
$raiz = Split-Path -Parent $MyInvocation.MyCommand.Path

# --- Visual Studio, descoberto e nao cravado: a edicao muda de maquina.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw "vswhere nao encontrado em $vswhere" }
$vsdir = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsdir) { throw 'nenhuma instalacao do Visual Studio com as ferramentas C++' }

# --- vcpkg: guardado ANTES do vcvars.
#
# O vcvars nao mexeu nele nesta maquina, mas o Visual Studio traz um vcpkg
# proprio em VC\vcpkg e ja houve build apontando para o errado. Guardar custa
# uma linha e tira a duvida.
$vcpkg = $env:VCPKG_ROOT
if (-not $vcpkg) { throw 'VCPKG_ROOT nao esta definido' }
$toolchain = Join-Path $vcpkg 'scripts\buildsystems\vcpkg.cmake'
if (-not (Test-Path $toolchain)) { throw "toolchain do vcpkg nao encontrado em $toolchain" }

# --- Importa o ambiente do vcvars64 para ESTA sessao.
#
# `cmd /c "call vcvars64 && set"` despeja o ambiente ja preparado; sem isso o
# cl.exe nao acha os headers da CRT e o erro fala de <cmath>, nao de ambiente.
$vcvars = Join-Path $vsdir 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { throw "vcvars64 nao encontrado em $vcvars" }
cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($matches[1])" -Value $matches[2] -ErrorAction SilentlyContinue }
}

# cmake e ninja vem embutidos no VS -- nao existem no PATH global.
$cmakeBin = Join-Path $vsdir 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
$ninjaBin = Join-Path $vsdir 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
$env:PATH = "$cmakeBin;$ninjaBin;$env:PATH"

Push-Location $raiz
try {
    # O triplet e o DINAMICO, e nao por escolha.
    #
    # O estatico seria melhor para um release (um .exe sozinho), mas o
    # libepoxy nao suporta CRT estatica no Windows -- o vcpkg recusa
    # x64-windows-static com "only supported on !(windows & staticcrt)". E o
    # epoxy nao e opcional aqui: o CMakeLists tem um FATAL_ERROR explicito,
    # "Windows requires epoxy module", que nem USE_EPOXY=OFF contorna.
    #
    # Consequencia pratica: o release tem que levar as DLLs junto. Ver
    # package-release.ps1, que as copia para o lado do .exe.
    $args = @(
        '-S', '.', '-B', 'build', '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=RelWithDebInfo',
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
        '-DVCPKG_TARGET_TRIPLET=x64-windows',
        '-DBUILD_QT=OFF', '-DBUILD_SDL=ON',
        '-DUSE_ZLIB=ON', '-DUSE_PNG=ON',
        '-DUSE_FFMPEG=OFF', '-DUSE_LIBZIP=OFF', '-DUSE_MINIZIP=OFF',
        '-DUSE_SQLITE3=OFF', '-DUSE_LZMA=OFF', '-DUSE_ELF=OFF',
        '-DUSE_EDITLINE=OFF', '-DUSE_DISCORD_RPC=OFF', '-DUSE_FREETYPE=OFF',
        '-DENABLE_SCRIPTING=OFF', '-DBUILD_UPDATER=OFF'
    )
    & cmake @args
    if ($LASTEXITCODE -ne 0) { throw "cmake configure falhou ($LASTEXITCODE)" }

    & cmake --build build --parallel
    if ($LASTEXITCODE -ne 0) { throw "build falhou ($LASTEXITCODE)" }

    Write-Output ''
    Write-Output "Pronto: $(Join-Path $raiz 'build\mgba.exe')"
} finally {
    Pop-Location
}
