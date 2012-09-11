#!/bin/bash

autoreconf --install

args="--prefix=/usr \
--libdir=/usr/lib64 \
--sysconfdir=/etc"

echo ./configure $args $@
./configure $args $@
