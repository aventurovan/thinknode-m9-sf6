#pragma once
#include <stdio.h>
#include <stdint.h>
#include <string>
class File { FILE* f = NULL; public: File() { } File(FILE* x) : f(x) { } operator bool() const { return f != NULL; }
  size_t read(uint8_t* b, size_t n) { return fread(b, 1, n, f); } size_t write(const uint8_t* b, size_t n) { return fwrite(b, 1, n, f); } void close() { if (f) fclose(f); f = NULL; } };
struct SpiffsClass { File open(const char* p, const char* m) { std::string s = std::string("/tmp/symfs") + p; return File(fopen(s.c_str(), m[0] == 'w' ? "wb" : "rb")); }
  void remove(const char*) { } size_t usedBytes() { return 120 * 1024; } size_t totalBytes() { return 3400 * 1024; } };
extern SpiffsClass SPIFFS;
