#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <map>
#include <zlib.h>
#include <openssl/sha.h>

using namespace std;

/* ===================== GLOBAL ===================== */
string GIT_DIR = ".git";

/* ===================== HELPERS ===================== */

string toHex(const unsigned char* b, int len) {
    stringstream ss;
    for (int i = 0; i < len; i++)
        ss << hex << setw(2) << setfill('0') << (int)b[i];
    return ss.str();
}

string sha1(const string& data) {
    unsigned char h[20];
    SHA1((const unsigned char*)data.data(), data.size(), h);
    return toHex(h, 20);
}

/* ===================== ZLIB ===================== */

vector<char> zlibCompress(const string& s) {
    z_stream zs{};
    deflateInit(&zs, Z_BEST_COMPRESSION);
    zs.next_in = (Bytef*)s.data();
    zs.avail_in = s.size();

    vector<char> out;
    char buf[4096];
    int ret;

    do {
        zs.next_out = (Bytef*)buf;
        zs.avail_out = sizeof(buf);
        ret = deflate(&zs, Z_FINISH);
        out.insert(out.end(), buf, buf + sizeof(buf) - zs.avail_out);
    } while (ret == Z_OK);

    deflateEnd(&zs);
    return out;
}

string zlibInflate(const vector<char>& c) {
    z_stream zs{};
    inflateInit(&zs);
    zs.next_in = (Bytef*)c.data();
    zs.avail_in = c.size();

    string out;
    char buf[4096];
    int ret;

    do {
        zs.next_out = (Bytef*)buf;
        zs.avail_out = sizeof(buf);
        ret = inflate(&zs, Z_NO_FLUSH);
        out.append(buf, sizeof(buf) - zs.avail_out);
    } while (ret == Z_OK);

    inflateEnd(&zs);
    return out;
}

/* ===================== OBJECT IO ===================== */

void writeObject(const string& sha, const string& data) {
    string dir = GIT_DIR + "/objects/" + sha.substr(0,2);
    filesystem::create_directories(dir);
    ofstream out(dir + "/" + sha.substr(2), ios::binary);
    auto c = zlibCompress(data);
    out.write(c.data(), c.size());
}

pair<string,string> readObject(const string& sha) {
    string path = GIT_DIR + "/objects/" + sha.substr(0,2) + "/" + sha.substr(2);
    ifstream f(path, ios::binary);
    vector<char> c((istreambuf_iterator<char>(f)), {});
    string d = zlibInflate(c);
    size_t p = d.find('\0');
    return { d.substr(0, p), d.substr(p + 1) };
}

/* ===================== write-tree ===================== */

string writeTree(const filesystem::path& dir) {
    vector<filesystem::directory_entry> entries;
    for (auto& e : filesystem::directory_iterator(dir))
        if (e.path().filename() != ".git")
            entries.push_back(e);

    sort(entries.begin(), entries.end(),
         [](auto& a, auto& b) {
             return a.path().filename() < b.path().filename();
         });

    string body;

    for (auto& e : entries) {
        string name = e.path().filename().string();
        if (e.is_directory()) {
            string sha = writeTree(e.path());
            body += "40000 " + name + '\0';
            for (int i = 0; i < 20; i++)
                body.push_back((char)strtol(sha.substr(i*2,2).c_str(),0,16));
        } else {
            ifstream f(e.path(), ios::binary);
            string content((istreambuf_iterator<char>(f)), {});
            string blob = "blob " + to_string(content.size()) + '\0' + content;
            string sha = sha1(blob);
            writeObject(sha, blob);

            body += "100644 " + name + '\0';
            for (int i = 0; i < 20; i++)
                body.push_back((char)strtol(sha.substr(i*2,2).c_str(),0,16));
        }
    }

    string tree = "tree " + to_string(body.size()) + '\0' + body;
    string sha = sha1(tree);
    writeObject(sha, tree);
    return sha;
}

/* ===================== commit-tree ===================== */

string commitTree(const string& tree,
                  const string& parent,
                  const string& msg) {
    string c;
    c += "tree " + tree + "\n";
    if (!parent.empty()) c += "parent " + parent + "\n";
    c += "author Test <a@b.com> 0 +0000\n";
    c += "committer Test <a@b.com> 0 +0000\n\n";
    c += msg + "\n";

    string obj = "commit " + to_string(c.size()) + '\0' + c;
    string sha = sha1(obj);
    writeObject(sha, obj);
    return sha;
}

/* ===================== CHECKOUT ===================== */

void checkoutTree(const string& sha, const filesystem::path& dir) {
    auto [_, d] = readObject(sha);
    size_t i = 0;
    while (i < d.size()) {
        size_t s = d.find(' ', i);
        string mode = d.substr(i, s-i);
        i = s+1;
        size_t z = d.find('\0', i);
        string name = d.substr(i, z-i);
        i = z+1;
        string child = toHex((unsigned char*)d.data()+i,20);
        i += 20;

        auto p = dir / name;
        if (mode=="40000") {
            filesystem::create_directories(p);
            checkoutTree(child, p);
        } else {
            auto [__, c] = readObject(child);
            ofstream(p, ios::binary).write(c.data(), c.size());
        }
    }
}

/* ===================== MAIN ===================== */

int main(int argc, char* argv[]) {
    if (argc < 2) return 1;
    string c = argv[1];

    if (c=="init") {
        filesystem::create_directory(".git");
        filesystem::create_directory(".git/objects");
        filesystem::create_directory(".git/refs");
        ofstream(".git/HEAD")<<"ref: refs/heads/main\n";
        cout<<"Initialized git directory\n";
    }
    else if (c=="hash-object") {
        ifstream f(argv[3], ios::binary);
        string d((istreambuf_iterator<char>(f)),{});
        string o="blob "+to_string(d.size())+'\0'+d;
        string s=sha1(o);
        writeObject(s,o);
        cout<<s<<"\n";
    }
    else if (c=="cat-file") {
        auto [_,d]=readObject(argv[3]);
        cout<<d;
    }
    else if (c=="ls-tree") {
        bool n = argc==4;
        string sha = n?argv[3]:argv[2];
        auto [_, d] = readObject(sha);

size_t pos = 0; 

while (pos < d.size()) {
    // mode
    size_t spacePos = pos;
    while (d[spacePos] != ' ') spacePos++;
    string mode(d.data() + pos, spacePos - pos);
    pos = spacePos + 1;

    // name
    size_t nullPos = pos;
    while (d[nullPos] != '\0') nullPos++;
    string name(d.data() + pos, nullPos - pos);
    pos = nullPos + 1;

    // raw 20-byte SHA
    string entrySha = toHex(
        reinterpret_cast<const unsigned char*>(d.data() + pos), 20
    );
    pos += 20;

    if (n) {
        cout << name << "\n";
    } else {
        string type = (mode == "40000") ? "tree" : "blob";
        cout << mode << " " << type << " "
             << entrySha << "\t" << name << "\n";
    }
}

    }
    else if (c=="write-tree") cout<<writeTree(".")<<"\n";
    else if (c=="commit-tree") {
        cout<<commitTree(argv[2], argv[4], argv[6])<<"\n";
    }
    else {
    cerr << "Unknown command: " << c << "\n";
    cerr << "Supported commands: init, hash-object, cat-file, ls-tree, write-tree, commit-tree, clone\n";
    return EXIT_FAILURE;
}

}
