#pragma once

#include <iostream>
#include <cmath> 

#define _USE_MATH_DEFINES
#include <math.h>
#include <filesystem>

using namespace std;
namespace fs = std::experimental::filesystem;

double rad2deg(double rad);

double deg2rad(double deg);


void replaceAll(std::string& str, const std::string& from, const std::string& to);

void sanitizeString(string& to_sanitize);

void find_files(fs::path startingDir, string extension, vector<fs::path>& results);