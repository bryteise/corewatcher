#!/bin/bash

autoreconf --install

args="--prefix=/usr \
--sysconfdir=/etc"

./configure $args $@