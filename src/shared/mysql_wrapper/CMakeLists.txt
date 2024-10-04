# Copyright (C) 2019, Kyoril. All rights reserved.

add_lib(mysql_wrapper)
target_link_libraries(mysql_wrapper sql_wrapper)
target_link_libraries(mysql_wrapper ${MYSQL_LIBRARY})
set_property(TARGET mysql_wrapper PROPERTY FOLDER "shared")
