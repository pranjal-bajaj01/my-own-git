#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <zlib.h>
#include <openssl/sha.h>

using namespace std;

/* ---------- Helpers ---------- */

string toHex(const unsigned char* bytes, int len) {
    ostringstream ss;
    for (int i = 0; i < len; i++) {
        ss << hex << setw(2) << setfill('0') << (int)bytes[i];
    }
    return ss.str();
}

bool readObject(const string& sha, vector<char>& decompressed, uLongf& size) {
    string path = ".git/objects/" + sha.substr(0, 2) + "/" + sha.substr(2);
    ifstream in(path, ios::binary);
    if (!in) return false;

    vector<char> compressed(
        (istreambuf_iterator<char>(in)),
        istreambuf_iterator<char>()
    );

    decompressed.resize(1024 * 1024);
    size = decompressed.size();

    return uncompress(
        (Bytef*)decompressed.data(), &size,
        (Bytef*)compressed.data(), compressed.size()
    ) == Z_OK;
}

/* ---------- Main ---------- */

int main(int argc, char* argv[]) {
    cout << unitbuf;
    cerr << unitbuf;

    if (argc < 2) {
        cerr << "No command provided\n";
        return EXIT_FAILURE;
    }

    string command = argv[1];

    /* ---------- init ---------- */
    if (command == "init") {
        filesystem::create_directory(".git");
        filesystem::create_directory(".git/objects");
        filesystem::create_directory(".git/refs");

        ofstream head(".git/HEAD");
        head << "ref: refs/heads/main\n";

        cout << "Initialized git directory\n";
    }

    /* ---------- hash-object ---------- */
    else if (command == "hash-object") {
        if (argc != 4 || string(argv[2]) != "-w") {
            cerr << "Invalid arguments\n";
            return EXIT_FAILURE;
        }

        ifstream file(argv[3], ios::binary);
        if (!file) return EXIT_FAILURE;

        string content(
            (istreambuf_iterator<char>(file)),
            istreambuf_iterator<char>()
        );

        string header = "blob " + to_string(content.size()) + '\0';
        string store = header + content;

        unsigned char hash[SHA_DIGEST_LENGTH];
        SHA1((unsigned char*)store.data(), store.size(), hash);

        string sha = toHex(hash, 20);
        cout << sha << "\n";

        filesystem::create_directories(".git/objects/" + sha.substr(0, 2));
        string path = ".git/objects/" + sha.substr(0, 2) + "/" + sha.substr(2);

        uLongf compressedSize = compressBound(store.size());
        vector<unsigned char> compressed(compressedSize);

        compress(
            compressed.data(), &compressedSize,
            (Bytef*)store.data(), store.size()
        );

        ofstream out(path, ios::binary);
        out.write((char*)compressed.data(), compressedSize);
    }

    /* ---------- cat-file ---------- */
    else if (command == "cat-file") {
        if (argc != 4 || string(argv[2]) != "-p") {
            cerr << "Invalid arguments\n";
            return EXIT_FAILURE;
        }

        vector<char> data;
        uLongf size;
        if (!readObject(argv[3], data, size)) {
            cerr << "Object not found\n";
            return EXIT_FAILURE;
        }

        size_t i = 0;
        while (data[i] != '\0') i++;
        cout.write(data.data() + i + 1, size - i - 1);
    }

    /* ---------- ls-tree ---------- */
    else if (command == "ls-tree") {

    bool nameOnly = false;
    string sha;

    if (argc == 4 && string(argv[2]) == "--name-only") {
        nameOnly = true;
        sha = argv[3];
    } else if (argc == 3) {
        sha = argv[2];
    } else {
        cerr << "Invalid arguments\n";
        return EXIT_FAILURE;
    }

    vector<char> data;
    uLongf size;
    if (!readObject(sha, data, size)) {
        cerr << "Object not found\n";
        return EXIT_FAILURE;
    }

    size_t pos = 0;
    while (data[pos] != '\0') pos++;
    pos++;

    while (pos < size) {
        size_t space = pos;
        while (data[space] != ' ') space++;
        string mode(data.data() + pos, space - pos);
        pos = space + 1;

        size_t nullPos = pos;
        while (data[nullPos] != '\0') nullPos++;
        string name(data.data() + pos, nullPos - pos);
        pos = nullPos + 1;

        string entrySha = toHex(
            (unsigned char*)data.data() + pos, 20
        );
        pos += 20;

        string type = (mode == "040000") ? "tree" : "blob";

        if (nameOnly) {
            cout << name << "\n";
        } else {
            cout << mode << " " << type << " " << entrySha
                 << "\t" << name << "\n";
        }
    }
}


    else {
        cerr << "Unknown command\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
