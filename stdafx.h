// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
// Windows Header Files:
#include <windows.h>
#include "strsafe.h"

#include <string>
#include <list>
#include <regex>

#include <algorithm>
#include <iostream>
#include <iterator>
#include <sstream>
#include <vector>
#include <cctype>
#include <fstream>
#include <direct.h>
#include <memory>
#include <map>

typedef const BYTE *LPCBYTE;

#include "headers/vfs plugins.h"
#include "headers/plugin support.h"

#include "firy.hpp"
#include "images/adf.hpp"
#include "images/d64.hpp"
#include "images/fat.hpp"
#include "images/iso9660.hpp"

#include "memorysource.hpp"
#include "offsetsource.hpp"
#include "dms.hpp"
#include "rdb.hpp"

#include "opusADF.hpp"
