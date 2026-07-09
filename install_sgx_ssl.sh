#!/bin/bash
set -e

echo "================================="
echo "Install dependencies"
echo "================================="

apt update

apt install -y \
    build-essential \
    nasm \
    wget \
    git \
    python3 \
    libssl-dev \
    pkg-config


echo "================================="
echo "Check SGX SDK"
echo "================================="

if [ ! -d "/opt/intel/sgxsdk" ]; then
    echo "ERROR: Intel SGX SDK not found"
    echo "Please install SGX SDK first"
    exit 1
fi

source /opt/intel/sgxsdk/environment


echo "================================="
echo "Clone intel-sgx-ssl"
echo "================================="

cd ~

if [ ! -d "intel-sgx-ssl" ]; then
    git clone git@github.com:intel/intel-sgx-ssl.git
fi


cd ~/intel-sgx-ssl

# Intel SGX SSL version
git checkout f9c1f96c3c


echo "================================="
echo "Download OpenSSL 1.1.1t"
echo "================================="

cd openssl_source

if [ ! -f "openssl-1.1.1t.tar.gz" ]; then
    wget -4 \
    https://github.com/openssl/openssl/releases/download/OpenSSL_1_1_1t/openssl-1.1.1t.tar.gz
fi


echo "================================="
echo "Build intel SGX SSL"
echo "================================="

cd ../Linux

make clean || true

make


echo "================================="
echo "Install SGX SSL"
echo "================================="

make install


echo "================================="
echo "Verify installation"
echo "================================="


if [ -d "/opt/intel/sgxssl" ]; then
    echo "SGX SSL installed:"
    ls /opt/intel/sgxssl
else
    echo "ERROR: SGX SSL installation failed"
    exit 1
fi


echo "================================="
echo "DONE"
echo "================================="