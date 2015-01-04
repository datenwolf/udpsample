cmake_minimum_required(VERSION 2.8)
project(librdc)

add_library(rdc rdc.c vdm/fec.c)
