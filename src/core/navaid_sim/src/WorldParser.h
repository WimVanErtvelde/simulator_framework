#pragma once
#ifndef AS_WORLD_PARSER_H
#define AS_WORLD_PARSER_H
#include <string>
#include "World.h"

class MagDec;  // forward declaration

namespace AS
{
	class WorldParser
	{
	public:
		World parse(std::string filename);
		void parseXP12(std::string filename, AS::World& world, const MagDec* magdec = nullptr);
	};
}

#endif