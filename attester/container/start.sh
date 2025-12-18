#!/bin/bash
set -euo pipefail

IMAGE_NAME="optee-attester"
TAG="latest"

SCRIPT_DIR=$(dirname "$(realpath "$0")")

if ! command -v docker &> /dev/null
then
    echo "Error: Docker is not installed. Please install Docker and try again."
    exit 1
fi

if ! docker info &> /dev/null
then
    echo "Error: Docker is not running. Please start Docker and try again."
    exit 1
fi

echo "Building Docker image..."
docker build -t ${IMAGE_NAME}:${TAG} \
             --build-arg USER_UID=$(id -u) \
             --build-arg USER_GID=$(id -g) \
             -f "${SCRIPT_DIR}/Dockerfile" "${SCRIPT_DIR}/.."

echo "Docker image ${IMAGE_NAME}:${TAG} has been successfully built."

echo "Running Docker container..."

# Check if veraison-net network exists for Veraison server integration
NETWORK_OPTS=""
if docker network inspect veraison-net &> /dev/null; then
    echo "Connecting to veraison-net network for Veraison server integration..."
    NETWORK_OPTS="--network veraison-net"
else
    echo "Note: veraison-net not found. Running without Veraison server connection."
    echo "      To enable Veraison integration, start Veraison services first."
fi

docker run --rm -it \
           --entrypoint=/optee/entrypoint.sh \
           ${NETWORK_OPTS} \
           --name optee-attester-run \
           -v "${SCRIPT_DIR}/..:/optee/optee_examples/remote_attestation" \
           -v "${SCRIPT_DIR}/../pta_remote_attestation/remote_attestation:/optee/optee_os/core/pta/remote_attestation" \
           -v "${SCRIPT_DIR}/entrypoint.sh:/optee/entrypoint.sh:ro" \
           ${IMAGE_NAME}:${TAG} bash

echo "Docker container has been successfully run."
