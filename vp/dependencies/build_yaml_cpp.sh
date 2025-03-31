#!/usr/bin/env bash

git clone git@github.com:jbeder/yaml-cpp.git
cd yaml-cpp/
git checkout -b r0.6.0 yaml-cpp-0.6.0
mkdir -p lib
cd lib
cmake ..
make
cd ../..
