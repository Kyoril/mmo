#!/bin/sh
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
# `docker exec mmo-deployer deployer status` etc.
cd /app && exec python3 -m mmo_deployer "$@"
