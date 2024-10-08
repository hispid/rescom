#include "LegacyCppCodeGenerator.hpp"
#include "Configuration.hpp"
#include "StringHelpers.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <streambuf>
#include <variant>

namespace
{
    static constexpr char const* NamespaceForResourceData = "rescom";

    void loadFile(std::filesystem::path const& filePath, std::vector<char>& buffer)
    {
        std::ifstream file{filePath, std::ios::binary};

        if (file.is_open())
        {
            buffer.assign(std::istreambuf_iterator<char>(file), {});
        }
        else
        {
            throw std::runtime_error(format("unable to read '{}'", filePath.generic_string()));
        }
    }
}

static std::string const HeaderProtectionMacroPrefix = "RESCOM_GENERATED_FILE_";

LegacyCppCodeGenerator::LegacyCppCodeGenerator(Configuration const& configuration)
: _configuration(configuration)
, _tabulation(configuration.tabulationSize, ' ')
, _headerProtectionMacroName(HeaderProtectionMacroPrefix + toUpper(_configuration.configurationFilePath.stem().generic_string()))
{
}

std::string LegacyCppCodeGenerator::tab(unsigned int count) const
{
    if (count == 0)
        return {};

    std::string result;

    result.reserve(count * _tabulation.size());

    for (auto i = 0u; i < count; ++i)
        result += _tabulation;

    return result;
}

void LegacyCppCodeGenerator::generate(std::ostream& output)
{
    writeFileHeader(output);
    writeResources(output);
    writeAccessFunction(output);
    writeFileFooter(output);
}

void LegacyCppCodeGenerator::writeFileHeader(std::ostream& output) const
{
    static std::string const Includes[] = {
        "<iterator>", // for std::iterator_traits
        "<string_view>",
        "<cstring>" // for std::strcmp
    };
    auto resourceFileStem = toLower(_configuration.configurationFilePath.stem().generic_string());

    output << "// Generated by Rescom\n";
    output << format("#ifndef {}\n#define {}\n", _headerProtectionMacroName, _headerProtectionMacroName);

    for (auto const& include : Includes)
        output << format("#include {}\n", include);
    output << "\n";

    output << tab(0) << "namespace " << NamespaceForResourceData << "::" << resourceFileStem << "\n{\n";
    output << tab(1) << "struct Resource\n"
           << tab(1) << "{\n"
           << tab(2) << "char const* const key;\n"
           << tab(2) << "char const* const bytes;\n"
           << tab(2) << "unsigned int const size;\n"
           << "\n"
           << tab(2) << "constexpr Resource(char const* key, unsigned int size, char const* bytes)\n"
           << tab(2) << ": key(key), bytes(bytes), size(size) {}\n"
           << tab(1) << "};\n\n";
}

void LegacyCppCodeGenerator::writeFileFooter(std::ostream& output) const
{
    auto resourceFileStem = toLower(_configuration.configurationFilePath.stem().generic_string());

    output << tab(0) << "} // namespace " << NamespaceForResourceData << "::" << resourceFileStem << "\n";
    output << "#endif // " << _headerProtectionMacroName << "\n";
}

std::string makeResourceName(unsigned int i)
{
    return format("R{}", i);
}

void LegacyCppCodeGenerator::writeResource(Input const&, unsigned int inputPosition, std::vector<char> const& bytes, std::ostream& output) const
{
    output << tab(2) << format("static constexpr char const {}[] = {", makeResourceName(inputPosition));

    output << std::hex;
    for (auto i = 0u; i < bytes.size(); ++i) {
        if (i > 0u)
            output << ", ";
        // Ensure the value printed is never negative by casting the byte to unsigned char
        // then convert to unsigned int to ensure operator << will print a hexadecimal number
        // and not a character.
        output << "'\\x" << static_cast<unsigned int>(static_cast<unsigned char>(bytes[i])) << "'";
    }
    output << std::dec;
    output << "};\n";
}

/// Write the code to access to a specific resource.
/// The generated code uses the fact resources are ordered by their key to use a constexpr version of std::lower_bound and
///// keep the compilation time acceptable.
void LegacyCppCodeGenerator::writeAccessFunction(std::ostream& output) const
{
    // Print function lowerBound
    // The content of the function is basically a copy-paste of https://en.cppreference.com/w/cpp/algorithm/lower_bound.
    // I can't use std::lower_bound because it's not constexpr (yet, C++ adds a constexpr version, see #46).
    output << tab(1) << "namespace details {\n";
    if (!_configuration.inputs.empty()) {
        output << tab(2) << "constexpr bool compareSlot(Resource const& slot, char const * key) { return std::string_view(slot.key) < key; }\n\n";
        output << tab(2) << "template<class ForwardIt, class Compare>\n"
               << tab(2) << "constexpr ForwardIt lowerBound(ForwardIt first, ForwardIt last, char const* value, Compare compare)\n"
               << tab(2) << "{\n"
               << tab(3) << "if (value == nullptr) return last;\n"
               << tab(3) << "typename std::iterator_traits<ForwardIt>::difference_type count;\n"
               << tab(3) << "typename std::iterator_traits<ForwardIt>::difference_type step;\n"
               << tab(3) << "count = std::distance(first, last);\n"
               << tab(3) << "ForwardIt it;\n"
               << tab(3) << "while (count > 0u) {\n"
               << tab(4) << "it = first; step = count / 2; std::advance(it, step);\n"
               << tab(4) << "if (compare(*it, value)) { first = ++it; count -= step + 1; } else { count = step; }\n"
               << tab(3) << "}\n"
               << tab(3) << "return first->key != nullptr && std::strcmp(value, first->key) == 0 ? first : last;\n"
               << tab(2) << "}\n";
    }

    output << tab(2) << "static constexpr Resource const NullResource{nullptr, 0u, nullptr};\n";
    output << tab(1) << "} // namespace details\n\n";

    output << tab(1) << "using ResourceIterator = Resource const*;\n\n";

    // Print function rescom::getResource, if no resources always returns the null resource
    if (_configuration.inputs.empty())
    {
        output << tab() << "inline constexpr Resource const& getResource(char const*)\n"
               << tab() << "{\n"
               << tab(2) << "return details::NullResource;\n"
               << tab() << "}\n";    }
    else
    {
        output << tab() << "inline constexpr Resource const& getResource(char const* key)\n"
               << tab() << "{\n"
               << tab(2) << "auto it = details::lowerBound(std::begin(details::ResourcesIndex), std::end(details::ResourcesIndex), key, details::compareSlot);\n"
               << "\n"
               << tab(2) << "if (it == std::end(details::ResourcesIndex))\n"
               << tab(3) << "return details::NullResource;\n"
               << "\n"
               << tab(2) << "return *it;\n"
               << tab() << "}\n";
    }
    output << "\n";

    // Print function rescom::contains
    output << tab() << "inline constexpr bool contains(char const* key)\n"
           << tab() << "{\n"
           << tab(2) << "return &getResource(key) != &details::NullResource;\n"
           << tab() << "}\n";

    // Print function rescom::getText
    output << "\n"
           << tab() << "inline constexpr std::string_view getText(char const* key)\n"
           << tab() << "{\n"
           << tab(2) << "auto const& resource = getResource(key);\n"
           << "\n"
           << tab(2) << "return std::string_view{resource.bytes, resource.size};\n"
           << tab() << "}\n";

    // Print rescom::begin
    output << "\n"
           << tab() << "inline constexpr ResourceIterator begin()\n"
           << tab() << "{\n";
    output << tab(2) << (_configuration.inputs.empty() ? "return &details::NullResource;\n" : "return std::begin(details::ResourcesIndex);\n");
    output << tab() << "}\n";

    // Print rescom::end
    output << "\n"
           << tab() << "inline constexpr ResourceIterator end()\n"
           << tab() << "{\n";
    output << tab(2) << (_configuration.inputs.empty() ? "return &details::NullResource;\n" : "return std::end(details::ResourcesIndex);\n");
    output << tab() << "}\n";
}

void LegacyCppCodeGenerator::writeResources(std::ostream& output) const
{
    if (_configuration.inputs.empty())
        return;

    std::vector<char> buffer;
    std::vector<std::string_view> keys;

    buffer.reserve(1024 * 16);

    output << tab(1) << "namespace details {\n";
    output << tab(2) << "static constexpr unsigned int const ResourcesCount = " << _configuration.inputs.size() << ";\n";

    // Write data
    for (auto i = 0u; i < _configuration.inputs.size(); ++i)
    {
        auto const& input = _configuration.inputs[i];

        loadFile(input.filePath, buffer);
        writeResource(input, i, buffer, output);
    }

    // Write index
    output << tab(2) << "static constexpr Resource const ResourcesIndex[ResourcesCount] = \n";
    output << tab(2) << "{\n";

    for (auto i = 0u; i < _configuration.inputs.size(); ++i)
    {
        auto const& input = _configuration.inputs[i];
        auto resourceName = makeResourceName(i);

        output << tab(3) << "{\"" << input.key << "\", " << input.size << ", " << resourceName << "},\n";
    }

    output << tab(2) << "};\n";
    output << tab(1) << "} // namespace details\n\n";
}