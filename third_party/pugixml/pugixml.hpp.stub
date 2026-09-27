#ifndef PUGIXML_HPP
#define PUGIXML_HPP

// pugixml header wrapper
// This file includes the real pugixml.hpp when available.
// Download pugixml from https://github.com/zeux/pugixml/releases
// and copy pugixml.hpp + pugixml.cpp into this directory.

#include "pugiconfig.hpp"

#ifdef PUGIXML_INSTALLED
// Real pugixml is available — include it
#include "pugixml_real.hpp"
#else
// Stub: provide minimal declarations for code to compile
// without the real pugixml library (for code navigation only).
// The real library must be installed before building.

#include <string>
#include <vector>

namespace pugi {

class xml_attribute {
public:
    xml_attribute() {}
    bool empty() const { return true; }
    const char* name() const { return ""; }
    const char* value() const { return ""; }
    double as_double(double def = 0.0) const { return def; }
    int as_int(int def = 0) const { return def; }
    unsigned as_uint(unsigned def = 0) const { return def; }
    const char* as_string(const char* def = "") const { return def; }
};

class xml_node {
public:
    xml_node() {}
    bool empty() const { return true; }
    const char* name() const { return ""; }
    xml_attribute attribute(const char*) const { return xml_attribute(); }
    xml_node first_child() const { return xml_node(); }
    xml_node next_sibling() const { return xml_node(); }
    xml_node child(const char*) const { return xml_node(); }
    const char* child_value() const { return ""; }
    const char* text() const { return ""; }
};

class xml_document : public xml_node {
public:
    bool load_file(const char*) { return false; }
    bool load_string(const char*) { return false; }
};

} // namespace pugi

#endif // PUGIXML_INSTALLED

#endif // PUGIXML_HPP
