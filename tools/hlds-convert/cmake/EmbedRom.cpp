// Writes the C++ source that holds the console's program inside the converter
// (HLDS_BUNDLED_ROM): its bytes as one string of octal escapes, which every
// compiler takes far more cheaply than an array of as many numbers. Given "-"
// for the program, a source that holds none.
//
//   hlds-embed-rom <program.nds | -> <out.cpp>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "usage: hlds-embed-rom <program.nds | -> <out.cpp>\n");
        return 2;
    }
    std::vector<unsigned char> bytes;
    if (std::string(argv[1]) != "-")
    {
        std::ifstream in(argv[1], std::ios::binary);
        if (!in)
        {
            std::fprintf(stderr, "hlds-embed-rom: cannot read %s\n", argv[1]);
            return 1;
        }
        bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    const std::string out = argv[2];
    const std::string temporary = out + ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary);
        file << "// Made by hlds-embed-rom: the console's program, built into the converter.\n"
                "#include \"BundledRom.hpp\"\n\n"
                "namespace\n{\n";
        if (bytes.empty())
            file << "    const char kRom[1] = {0};\n";
        else
        {
            file << "    const char kRom[] =\n";
            static const char kDigits[] = "01234567";
            std::string line;
            for (std::size_t i = 0; i < bytes.size(); ++i)
            {
                if (i % 64 == 0)
                    line = "        \"";
                const unsigned v = bytes[i];
                line += '\\';
                line += kDigits[(v >> 6) & 7];
                line += kDigits[(v >> 3) & 7];
                line += kDigits[v & 7];
                if (i % 64 == 63 || i + 1 == bytes.size())
                {
                    line += "\"\n";
                    file << line;
                }
            }
            file << "        ;\n";
        }
        file << "}\n\n"
                "namespace hlds::install\n{\n"
                "    std::span<const std::uint8_t> bundledRom()\n    {\n"
                "        return {reinterpret_cast<const std::uint8_t *>(kRom), "
             << bytes.size() << "u};\n    }\n}\n";
        if (!file)
        {
            std::fprintf(stderr, "hlds-embed-rom: cannot write %s\n", temporary.c_str());
            return 1;
        }
    }
    // (Only when it differs: the converter is not linked again for nothing.)
    {
        std::ifstream a(temporary, std::ios::binary), b(out, std::ios::binary);
        const std::string made((std::istreambuf_iterator<char>(a)), std::istreambuf_iterator<char>());
        const std::string had((std::istreambuf_iterator<char>(b)), std::istreambuf_iterator<char>());
        if (b && made == had)
        {
            a.close();
            std::remove(temporary.c_str());
            return 0;
        }
    }
    std::remove(out.c_str());
    if (std::rename(temporary.c_str(), out.c_str()) != 0)
    {
        std::fprintf(stderr, "hlds-embed-rom: cannot write %s\n", out.c_str());
        return 1;
    }
    return 0;
}
