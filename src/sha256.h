#pragma once
#include <string>
#include <stdint.h>

// Невелика самостійна реалізація SHA-256 (публічний домен), без сторонніх бібліотек.
std::string Sha256Hex(const std::string& data);
