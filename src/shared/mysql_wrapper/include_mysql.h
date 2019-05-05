// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#if defined(WIN32) || defined(_WIN32)
#	include <winsock2.h>
#	include <mysql.h>
#else
#	include <mysql/mysql.h>
#endif
