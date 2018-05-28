# protoserv

Debian 9
```
sudo apt-get install libprotobuf-dev protobuf-compiler

mkdir build && cd build
export BOOST_ROOT=/opt/boost_1_66_0
cmake -DCMAKE_CXX_COMPILER=/opt/gcc/bin/g++ ..
make
```
