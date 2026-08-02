#include "rzone_parser.hpp"

#include <pugixml.hpp>
#include <sstream>

namespace providers::device {

namespace {

MemoryRegion TranslateMemory(const pugi::xml_node &node) {
    MemoryRegion region;
    region.name = node.attribute("name").as_string();
    region.info = node.attribute("info").as_string();
    region.start = node.attribute("start").as_ullong();
    region.size = node.attribute("size").as_ullong();

    const std::string access = node.attribute("access").as_string();
    region.readable = access.find('r') != std::string::npos;
    region.writable = access.find('w') != std::string::npos;
    region.executable = access.find('x') != std::string::npos;

    const std::string type = node.attribute("type").as_string();
    region.kind = type == "RAM" ? MemoryKind::kRam : MemoryKind::kRom;

    return region;
}

MemoryRegion TranslateProvisioning(const pugi::xml_node &node) {
    MemoryRegion region;
    region.name = node.attribute("name").as_string();
    region.info = node.attribute("info").as_string();
    region.start = node.attribute("start").as_ullong();
    region.size = node.attribute("size").as_ullong();
    region.kind = MemoryKind::kExternal;
    return region;
}

}  // namespace

std::optional<MemoryMap> LoadRzoneMemoryMap(const std::filesystem::path &rzone_dir, const std::string &die,
                                             std::uint32_t ram_kb, std::uint32_t flash_kb,
                                             std::uint32_t core_count) {
    std::ostringstream filename;
    filename << "STM32_" << die << "_" << ram_kb << "_" << flash_kb;
    if (core_count > 1) filename << "_DUAL";
    filename << ".xml";

    const std::filesystem::path path = rzone_dir / filename.str();

    pugi::xml_document doc;
    if (!doc.load_file(path.c_str())) return std::nullopt;

    const pugi::xml_node memories = doc.child("rzone").child("resources").child("memories");
    if (!memories) return std::nullopt;

    std::vector<MemoryRegion> regions;
    for (const pugi::xml_node &node : memories.children("memory")) {
        regions.push_back(TranslateMemory(node));
    }
    for (const pugi::xml_node &node : memories.children("provisioning")) {
        regions.push_back(TranslateProvisioning(node));
    }

    return MemoryMap(std::move(regions));
}

}  // namespace providers::device
