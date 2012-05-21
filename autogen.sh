#!/bin/bash

autoreconf --install

args="--prefix=/usr \
--sysconfdir=/etc"

echo ./configure $args $@
./configure $args $@
