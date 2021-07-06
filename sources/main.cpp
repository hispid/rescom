#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>

#include <cxxopts.hpp>

#include "Configuration.hpp"
#include "LegacyCppCodeGenerator.hpp"
#include "GeneratedConstants.hpp"
#include "StringHelpers.hpp"
#include "ConfigurationParser.hpp"
#include "FileSystem.hpp"

void releaseResults(cxxopts::ParseResult const& parseResult, std::ostringstream const& outputStream);

std::ostream& selectOutputStream(std::ofstream& file, std::ostream& fallback)
{
    return file.is_open() ? file : fallback;
}

void registerCodeGenerators()
{
    registerCodeGenerator("legacy", [](Configuration const& configuration){ return std::make_unique<LegacyCppCodeGenerator>(configuration); }, true);
}

CodeGeneratorPointer createGenerator(cxxopts::ParseResult const& parseResult, Configuration const& configuration)
{
    if (parseResult["generator"].count() == 0)
        return instanciateDefaultCodeGenerator(configuration);
    else
        return instanciateCodeGenerator(parseResult["generator"].as<std::string>(), configuration);
}

int main(int argc, char** argv)
{
    registerCodeGenerators();

    std::vector<std::string> inputs;
    cxxopts::Options options("rescom", "Resources compiler");

    options.add_options()
        ("i,input", "Input file", cxxopts::value<std::string>())
        ("o,output", "Output file", cxxopts::value<std::string>())
        ("G,generator", "Generator", cxxopts::value<std::string>())
        ("version", "Print version", cxxopts::value<bool>())
        ;

    auto parseResult = options.parse(argc, argv);

    if (parseResult["version"].count() > 0)
    {
        std::cout << "rescom version " << RescomVersion << "\n";
        return 0;
    }

    std::filesystem::path const inputFilePath{parseResult["input"].as<std::string>()};
    std::ostringstream outputStream;

    try
    {
        ConfigurationParser parser{std::make_unique<LocalFileSystem>()};
        auto configuration = parser.parseFile(inputFilePath);
        auto generator = createGenerator(parseResult, configuration);

        if (generator != nullptr)
            generator->generate(outputStream);

        releaseResults(parseResult, outputStream);
    }
    catch (std::exception const& error)
    {
        std::cerr << "Rescom error: " << error.what() << "\n";

        return 1;
    }

    return 0;
}

void releaseResults(cxxopts::ParseResult const& parseResult, std::ostringstream const& outputStream)
{
    if (parseResult.count("output") > 0)
    {
        std::filesystem::path const outputFilePath{parseResult["output"].as<std::string>()};
        std::ofstream outputFile(outputFilePath, std::ios::out | std::ios::ate);

        if (!outputFile.is_open())
            throw std::runtime_error(format("unable to open '{}' for writing", outputFilePath.generic_string()));

        outputFile << outputStream.str();
    }
    else
    {
        std::cout << outputStream.str();
    }
}

