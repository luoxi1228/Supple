## Install SGX driver

Install the SGX driver in the usual way.  This example is on a stock
Ubuntu 22.04.

```
apt install build-essential ocaml ocamlbuild automake autoconf libtool \
    wget python-is-python3 libssl-dev git cmake perl unzip debhelper \
    libcurl4-openssl-dev protobuf-compiler reprepro nasm

git clone https://github.com/intel/linux-sgx-driver
cd linux-sgx-driver
git checkout sgx_driver_2.14
make
sudo make install
sudo /sbin/modprobe isgx
```

## Install SGX SDK

We install version 2.19, plus one bugfix commit after that.

```
cd
git clone https://github.com/intel/linux-sgx
cd linux-sgx
git checkout d3fd8b4511
make preparation
make sdk_install_pkg
cd linux/installer/bin/
sudo ./sgx_linux_x64_sdk_2.19.100.3.bin
```
Answer to the prompts:

 - no
 - /opt/intel

```
cd ../../..
make psw_install_pkg
cd linux/installer/bin/
sudo ./sgx_linux_x64_psw_2.19.100.3.bin
```

## Install SGX crypto library

We install version lin_2.19_1.1.1t, plus a few bugfix commits after that.

```
cd
git clone git@github.com:intel/intel-sgx-ssl.git
cd intel-sgx-ssl
git checkout f9c1f96c3c
cd openssl_source
wget https://www.openssl.org/source/openssl-1.1.1t.tar.gz
cd ../Linux
make
sudo make install
```
