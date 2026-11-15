# Builds KaiPlayer.nro with the ounce-devkit Docker image.
#   .\build.ps1           build the .nro
#   .\build.ps1 test      run the host tests with coverage
#   .\build.ps1 fuzz      fuzz the file parsers (set $env:ITER for more inputs)
#   .\build.ps1 preview   render UI screenshots to tests\preview\build\*.ppm
#   .\build.ps1 clean     remove build output
param([ValidateSet('nro', 'test', 'fuzz', 'preview', 'clean')][string]$Target = 'nro')

$ErrorActionPreference = 'Stop'
$project = $PSScriptRoot
$image = 'ounce-devkit'

docker image inspect $image *> $null
if ($LASTEXITCODE -ne 0) {
    throw "Docker image '$image' not found. Build it in the ounce-devkit folder with: docker build -t $image ."
}

$mount = "${project}:/build/kaiplayer"
$iterations = if ($env:ITER) { $env:ITER } else { '1000' }
switch ($Target) {
    'nro'     { docker run --rm -v $mount -w /build/kaiplayer $image sh -c 'sh third_party/verify.sh >/dev/null && make -j8' }
    'test'    { docker run --rm --security-opt seccomp=unconfined -v $mount -w /build/kaiplayer/tests $image make coverage }
    'fuzz'    { docker run --rm --security-opt seccomp=unconfined -v $mount -w /build/kaiplayer/tests $image make fuzz "ITER=$iterations" }
    'preview' { docker run --rm -v $mount -w /build/kaiplayer/tests/preview $image make }
    'clean'   { docker run --rm -v $mount -w /build/kaiplayer $image sh -c 'make clean && make -C tests clean && make -C tests/preview clean' }
}
if ($LASTEXITCODE -ne 0) { throw "Build failed (exit code $LASTEXITCODE)." }
