#pragma once

#pragma warning(push)
#include <RE/Skyrim.h>
#include <RE/T/TESObjectREFR.h>
#include <RE/T/TESObjectCELL.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/C/Character.h>
#include <RE/A/Actor.h>
#include <RE/N/NiNode.h>
#include <RE/N/NiAVObject.h>
#include <REL/Relocation.h>
#include <SKSE/SKSE.h>

#include <fstream>

#include <spdlog/sinks/basic_file_sink.h>
#ifndef NDEBUG
#	include <spdlog/sinks/msvc_sink.h>
#endif
#pragma warning(pop)

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace logger = SKSE::log;

namespace util
{
	using SKSE::stl::report_and_fail;
}

#define DLLEXPORT __declspec(dllexport)

#define RELOCATION_OFFSET(SE, AE) REL::VariantOffset(SE, AE, 0).offset()

#include "plugin.h"
