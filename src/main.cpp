#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <zlib.h>

int main(int argc, char *argv[])
{
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    std::cerr << "Logs from your program will appear here!\n";

    if (argc < 2) {
        std::cerr << "No command provided.\n";
        return EXIT_FAILURE;
    }
    
    std::string command = argv[1];
    
    if (command == "init") {
        try {
            std::filesystem::create_directory(".git");
            std::filesystem::create_directory(".git/objects");
            std::filesystem::create_directory(".git/refs");
    
            std::ofstream headFile(".git/HEAD");
            if (headFile.is_open()) {
                headFile << "ref: refs/heads/main\n";
                headFile.close();
            } else {
                std::cerr << "Failed to create .git/HEAD file.\n";
                return EXIT_FAILURE;
            }
    
            std::cout << "Initialized git directory\n";
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << e.what() << '\n';
            return EXIT_FAILURE;
        }
    } 
    
    // --------- ADDED CAT-FILE ----------
    else if (command == "cat-file") {

        // Expected: cat-file -p <sha>
        if (argc != 4 || std::string(argv[2]) != "-p") {
            std::cerr << "Invalid arguments\n";
            return EXIT_FAILURE;
        }

        std::string sha = argv[3];

        // Build object path
        std::string dir = sha.substr(0, 2);
        std::string file = sha.substr(2);
        std::string path = ".git/objects/" + dir + "/" + file;

        // Open object file in binary mode
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            std::cerr << "Object not found\n";
            return EXIT_FAILURE;
        }

        // Read compressed data
        std::vector<char> compressed(
            (std::istreambuf_iterator<char>(in)),
            std::istreambuf_iterator<char>()
        );

        // Prepare buffer for decompression
        std::vector<char> decompressed(1024 * 1024);
        uLongf decompressedSize = decompressed.size();

        // Decompress using zlib
        if (uncompress(
                (Bytef*)decompressed.data(), &decompressedSize,
                (Bytef*)compressed.data(), compressed.size()) != Z_OK) {
            std::cerr << "Decompression failed\n";
            return EXIT_FAILURE;
        }

        // Find null byte separating header and content
        size_t i = 0;
        while (i < decompressedSize && decompressed[i] != '\0') {
            i++;
        }

        // Print content only
        std::cout.write(
            decompressed.data() + i + 1,
            decompressedSize - i - 1
        );
    }
    
    else {
        std::cerr << "Unknown command " << command << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
