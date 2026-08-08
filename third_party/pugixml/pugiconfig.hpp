#ifndef PUGICONFIG_HPP
#define PUGICONFIG_HPP

// pugixml configuration header
// This is a minimal config for sin project usage.
// Download pugixml.hpp and pugixml.cpp from https://github.com/zeux/pugixml
// and place them in this directory before building.

// Enable wchar_t string mode (not needed for ARXML parsing)
// #define PUGIXML_WCHAR_MODE

// Disable XPath (not needed, reduces compile time)
// #define PUGIXML_NO_XPATH

// Enable STL compatibility
#ifndef PUGIXML_NO_STL
#define PUGIXML_HAS_STL 1
#endif

#endif // PUGICONFIG_HPP
