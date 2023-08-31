# Exit immediately for non zero status
$ErrorActionPreference = "Stop"
# Print commands
Set-PSDebug -Trace 1

# Absolute path to the toplevel milvus directory.
$toplevel = Split-Path -Parent (Split-Path -Parent (Resolve-Path $MyInvocation.MyCommand.Path))

$OS_NAME = if ($env:OS_NAME) { $env:OS_NAME } else { "ubuntu20.04" }
$MILVUS_IMAGE_REPO = if ($env:MILVUS_IMAGE_REPO) { $env:MILVUS_IMAGE_REPO } else { "milvusdb/milvus" }
$MILVUS_IMAGE_TAG = if ($env:MILVUS_IMAGE_TAG) { $env:MILVUS_IMAGE_TAG } else { "latest" }

if (-not $env:IMAGE_ARCH) {
    $MACHINE = (Get-WmiObject -Class Win32_Processor).AddressWidth
    if ($MACHINE -eq 64) {
        $IMAGE_ARCH = "amd64"
    } else {
        $IMAGE_ARCH = "arm64"
    }
}

Write-Host $IMAGE_ARCH

$BUILD_ARGS = "${BUILD_ARGS:---build-arg TARGETARCH=$IMAGE_ARCH}"

Push-Location $toplevel

docker build $BUILD_ARGS --platform linux/$IMAGE_ARCH -f "./build/docker/milvus/$OS_NAME/Dockerfile" -t "${MILVUS_IMAGE_REPO}:${MILVUS_IMAGE_TAG}" .

$image_size = (docker inspect ${MILVUS_IMAGE_REPO}:${MILVUS_IMAGE_TAG} -f '{{.Size}}' | ForEach-Object { [math]::Round($_ / 1GB, 2) }) -join " GB"

Write-Host "Image Size for ${MILVUS_IMAGE_REPO}:${MILVUS_IMAGE_TAG} is ${image_size}"

Pop-Location