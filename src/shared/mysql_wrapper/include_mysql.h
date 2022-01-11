// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#if defined(WIN32) || defined(_WIN32)
#	include <winsock2.h>
#	include <mysql.h>
#elif defined(__APPLE__)
#    include <mysql.h>
#else
#	include <mysql/mysql.h>
#endif

#if MYSQL_VERSION_ID >= 80000
// https://dev.mysql.com/doc/relnotes/mysql/8.0/en/news-8-0-1.html
// 
// Incompatible Change: The my_bool type is no longer used in MySQL source code. 
// Any third-party code that used this type to represent C boolean variables 
// should use the bool or int C type instead. 
typedef bool my_bool;
#endif
