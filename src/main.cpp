#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>
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

/* ---------- write-tree ---------- */

string writeTree(const filesystem::path& dir) {
    vector<filesystem::directory_entry> entries;

    for (auto& entry : filesystem::directory_iterator(dir)) {
        if (entry.path().filename() == ".git") continue;
        entries.push_back(entry);
    }

    sort(entries.begin(), entries.end(),
         [](const auto& a, const auto& b) {
             return a.path().filename().string() <
                    b.path().filename().string();
         });

    string treeContent;

    for (auto& entry : entries) {
        string name = entry.path().filename().string();

        if (entry.is_directory()) {
            string subSha = writeTree(entry.path());

            treeContent += "40000 " + name + '\0';

            for (int i = 0; i < 20; i++) {
                treeContent.push_back(
                    (char)strtol(subSha.substr(i * 2, 2).c_str(), nullptr, 16)
                );
            }
        }
        else if (entry.is_regular_file()) {
            ifstream file(entry.path(), ios::binary);
            string content(
                (istreambuf_iterator<char>(file)),
                istreambuf_iterator<char>()
            );

            string header = "blob " + to_string(content.size()) + '\0';
            string store = header + content;

            unsigned char hash[SHA_DIGEST_LENGTH];
            SHA1((unsigned char*)store.data(), store.size(), hash);
            string blobSha = toHex(hash, 20);

            filesystem::create_directories(".git/objects/" + blobSha.substr(0, 2));
            string path = ".git/objects/" + blobSha.substr(0, 2) + "/" + blobSha.substr(2);

            if (!filesystem::exists(path)) {
                uLongf compressedSize = compressBound(store.size());
                vector<unsigned char> compressed(compressedSize);

                compress(
                    compressed.data(), &compressedSize,
                    (Bytef*)store.data(), store.size()
                );

                ofstream out(path, ios::binary);
                out.write((char*)compressed.data(), compressedSize);
            }

            treeContent += "100644 " + name + '\0';

            for (int i = 0; i < 20; i++) {
                treeContent.push_back(
                    (char)strtol(blobSha.substr(i * 2, 2).c_str(), nullptr, 16)
                );
            }
        }
    }

    string header = "tree " + to_string(treeContent.size()) + '\0';
    string store = header + treeContent;

    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1((unsigned char*)store.data(), store.size(), hash);
    string treeSha = toHex(hash, 20);

    filesystem::create_directories(".git/objects/" + treeSha.substr(0, 2));
    string path = ".git/objects/" + treeSha.substr(0, 2) + "/" + treeSha.substr(2);

    if (!filesystem::exists(path)) {
        uLongf compressedSize = compressBound(store.size());
        vector<unsigned char> compressed(compressedSize);

        compress(
            compressed.data(), &compressedSize,
            (Bytef*)store.data(), store.size()
        );

        ofstream out(path, ios::binary);
        out.write((char*)compressed.data(), compressedSize);
    }

    return treeSha;
}

/* ---------- commit-tree (STAGE 5 ADDED) ---------- */

string commitTree(const string& treeSha,
                  const string& parentSha,
                  const string& message) {
    string content;

    content += "tree " + treeSha + "\n";
    content += "parent " + parentSha + "\n";
    content += "author Test User <test@example.com> 0 +0000\n";
    content += "committer Test User <test@example.com> 0 +0000\n\n";
    content += message + "\n";

    string header = "commit " + to_string(content.size()) + '\0';
    string store = header + content;

    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1((unsigned char*)store.data(), store.size(), hash);
    string commitSha = toHex(hash, 20);

    filesystem::create_directories(".git/objects/" + commitSha.substr(0, 2));
    string path = ".git/objects/" + commitSha.substr(0, 2) + "/" + commitSha.substr(2);

    uLongf compressedSize = compressBound(store.size());
    vector<unsigned char> compressed(compressedSize);

    compress(
        compressed.data(), &compressedSize,
        (Bytef*)store.data(), store.size()
    );

    ofstream out(path, ios::binary);
    out.write((char*)compressed.data(), compressedSize);

    return commitSha;
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

    if (command == "init") {
        filesystem::create_directory(".git");
        filesystem::create_directory(".git/objects");
        filesystem::create_directory(".git/refs");

        ofstream head(".git/HEAD");
        head << "ref: refs/heads/main\n";

        cout << "Initialized git directory\n";
    }

    else if (command == "hash-object") {
        if (argc != 4 || string(argv[2]) != "-w") return EXIT_FAILURE;

        ifstream file(argv[3], ios::binary);
        string content(
            (istreambuf_iterator<char>(file)),
            istreambuf_iterator<char>()
        );

        string header = "blob " + to_string(content.size()) + '\0';
        string store = header + content;

        unsigned char hash[SHA_DIGEST_LENGTH];
        SHA1((unsigned char*)store.data(), store.size(), hash);
        string sha = toHex(hash, 20);

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

        cout << sha << "\n";
    }

    else if (command == "cat-file") {
        vector<char> data;
        uLongf size;
        readObject(argv[3], data, size);

        size_t i = 0;
        while (data[i] != '\0') i++;
        cout.write(data.data() + i + 1, size - i - 1);
    }

    else if (command == "ls-tree") {
        bool nameOnly = false;
        string sha;

        if (argc == 4 && string(argv[2]) == "--name-only") {
            nameOnly = true;
            sha = argv[3];
        } else {
            sha = argv[2];
        }

        vector<char> data;
        uLongf size;
        readObject(sha, data, size);

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

            string entrySha = toHex((unsigned char*)data.data() + pos, 20);
            pos += 20;

            if (nameOnly) {
                cout << name << "\n";
            } else {
                string type = (mode == "40000") ? "tree" : "blob";
                cout << mode << " " << type << " " << entrySha
                     << "\t" << name << "\n";
            }
        }
    }

    else if (command == "write-tree") {
        cout << writeTree(".") << "\n";
    }

    /* ---------- commit-tree command (STAGE 5 ADDED) ---------- */
    else if (command == "commit-tree") {
        string treeSha, parentSha, message;

        for (int i = 2; i < argc; i++) {
            if (string(argv[i]) == "-p") parentSha = argv[++i];
            else if (string(argv[i]) == "-m") message = argv[++i];
            else treeSha = argv[i];
        }

        cout << commitTree(treeSha, parentSha, message) << "\n";
    }

    else {
        cerr << "Unknown command\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
