# Builds KaiPlayer.nro with the kaiplayer-devkit Docker image: ounce-devkit, plus libsmb2
# (smb/Dockerfile), plus FFmpeg for the console and the host tests (ffmpeg/Dockerfile).
#   .\build.ps1           build the .nro
#   .\build.ps1 test      run the host tests with coverage
#   .\build.ps1 fuzz      fuzz the file parsers (set $env:ITER for more inputs)
#   .\build.ps1 preview   render UI screenshots to tests\preview\build\*.ppm
#   .\build.ps1 clean     remove build output
param([ValidateSet('nro', 'test', 'fuzz', 'preview', 'clean')][string]$Target = 'nro')

$ErrorActionPreference = 'Stop'
$project = $PSScriptRoot
$image = 'kaiplayer-devkit'

docker image inspect $image *> $null
if ($LASTEXITCODE -ne 0) {
    throw "Docker image '$image' not found. Build ounce-devkit (in its folder: docker build -t ounce-devkit .), then here: docker build -t ${image}:smb smb; docker build --build-arg DEVKIT_IMAGE=${image}:smb -t $image ffmpeg"
}
docker run --rm $image test -d /opt/ffmpeg-host *> $null
if ($LASTEXITCODE -ne 0) {
    throw "Docker image '$image' has no FFmpeg. Here: docker tag $image ${image}:smb; docker build --build-arg DEVKIT_IMAGE=${image}:smb -t $image ffmpeg"
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
