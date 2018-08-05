/*
makefile
This makefile contains a set of directives used by the cppmake tool to generate a target.
It depends on cppmake.lib.
All makefiles in one archive are compiled and linked into makefile.dll
This makefile.dll is picked up by the cppmake tool, so that it 'knows' what to do.
*/
#include "cppmake/makefile.h"

namespace { // anonymous

class main_t
	: cppmake::main_t
{
	char const* filename() final { return __FILE__; }
	void provides() final
	{}
	void requires() final
	{}
	void contains() final
	{}
} main;

}; // namespace anonymous