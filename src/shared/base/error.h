/////////////////////////////////////////////////////////////////////////////////////
// simple - lightweight & fast signal/slots & utility library                      //
//                                                                                 //
//   v1.2 - public domain                                                          //
//   no warranty is offered or implied; use this code at your own risk             //
//                                                                                 //
// AUTHORS                                                                         //
//                                                                                 //
//   Written by Michael Bleis                                                      //
//                                                                                 //
//                                                                                 //
// LICENSE                                                                         //
//                                                                                 //
//   This software is dual-licensed to the public domain and under the following   //
//   license: you are granted a perpetual, irrevocable license to copy, modify,    //
//   publish, and distribute this file as you see fit.                             //
/////////////////////////////////////////////////////////////////////////////////////

// Note: Moved custom error class out of signal.h since signal.h got split. This way,
// multiple header files (if needed) can define custom exception classes without
// having to include the full signal library.

#pragma once

#include <exception>


namespace mmo
{
#ifndef SIMPLE_NO_EXCEPTIONS
	struct error : std::exception
	{
	};
#endif
}
