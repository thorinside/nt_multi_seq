// Shared NT API stubs for unit tests.
// Include this BEFORE including any engine .cpp that references NT API functions.

#ifndef NT_STUBS_H
#define NT_STUBS_H

#include <stdio.h>
#include <string.h>
#include <math.h>

// --- No-op serialisation stubs ---
// Define _DISTINGNT_SERIALISATION_INTERNAL to gain access to private constructors.

#define _DISTINGNT_SERIALISATION_INTERNAL
#include <distingnt/api.h>
#include <distingnt/serialisation.h>

// --- Functional stubs (real behavior for tests) ---

extern "C" int NT_intToString(char* buffer, int32_t value)
{
    return snprintf(buffer, 64, "%d", (int)value);
}

extern "C" int NT_floatToString(char* buffer, float value, int decimalPlaces)
{
    return snprintf(buffer, 64, "%.*f", decimalPlaces, (double)value);
}

// --- No-op drawing stubs (match extern "C" declarations from api.h) ---

uint8_t NT_screen[128*64];

extern "C" void NT_drawText(int, int, const char*, int, _NT_textAlignment, _NT_textSize) {}
extern "C" void NT_drawShapeI(_NT_shape, int, int, int, int, int) {}
extern "C" void NT_drawShapeF(_NT_shape, float, float, float, float, float) {}

// _NT_jsonStream stubs
_NT_jsonStream::_NT_jsonStream(void* p) : refCon(p) {}
_NT_jsonStream::~_NT_jsonStream() {}
void _NT_jsonStream::openArray() {}
void _NT_jsonStream::closeArray() {}
void _NT_jsonStream::openObject() {}
void _NT_jsonStream::closeObject() {}
void _NT_jsonStream::addMemberName(const char*) {}
void _NT_jsonStream::addNumber(int) {}
void _NT_jsonStream::addNumber(float) {}
void _NT_jsonStream::addString(const char*) {}
void _NT_jsonStream::addFourCC(uint32_t) {}
void _NT_jsonStream::addBoolean(bool) {}
void _NT_jsonStream::addNull() {}

// _NT_jsonParse stubs
_NT_jsonParse::_NT_jsonParse(void* p, int idx) : refCon(p), i(idx) {}
_NT_jsonParse::~_NT_jsonParse() {}
bool _NT_jsonParse::numberOfArrayElements(int& num) { num = 0; return false; }
bool _NT_jsonParse::numberOfObjectMembers(int& num) { num = 0; return false; }
bool _NT_jsonParse::matchName(const char*) { return false; }
bool _NT_jsonParse::skipMember() { return true; }
bool _NT_jsonParse::number(int&) { return false; }
bool _NT_jsonParse::number(float&) { return false; }
bool _NT_jsonParse::string(const char*&) { return false; }
bool _NT_jsonParse::boolean(bool&) { return false; }
bool _NT_jsonParse::null() { return false; }

// FocusDetailLine / fmtInt implementations (engines use these, no NT dependency)
#include "../engines/FocusData.cpp"

#endif // NT_STUBS_H
