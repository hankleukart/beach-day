// WMO weather interpretation codes -> display text (../../shared/wmo-codes.json)
#pragma once
namespace beach {
const char* wmoDescription(int code);   // "Unknown" for anything not in the table
}
