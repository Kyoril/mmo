#!/bin/bash

# TODO: should implement retry strategy inside login server instead of sleeping here

sleep 10
echo "Starting login server"
./login_server
