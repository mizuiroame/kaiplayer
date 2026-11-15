# Builds KaiPlayer.nro with the ounce-devkit Docker image.
#   .\build.ps1          build the .nro
#   .\build.ps1 test     run the host tests
#   .\build.ps1 clean    remove build output
param([ValidateSet('nro', 'test', 'clean')][string]$Target = 'nro')

$ErrorActionPreference = 'Stop'
$project = $PSScriptRoot
$image = 'ounce-devkit'

docker image inspect $image *> $null
if ($LASTEXITCODE -ne 0) {
    throw "Docker image '$image' not found. Build it in the ounce-devkit folder with: docker build -t $image ."
}

$mount = "${project}:/build/kaiplayer"
switch ($Target) {
    'nro'   { docker run --rm -v $mount -w /build/kaiplayer $image make -j8 }
    'test'  { docker run --rm --security-opt seccomp=unconfined -v $mount -w /build/kaiplayer/tests $image make coverage }
    'clean' { docker run --rm -v $mount -w /build/kaiplayer $image sh -c 'make clean && make -C tests clean' }
}
if ($LASTEXITCODE -ne 0) { throw "Build failed (exit code $LASTEXITCODE)." }
