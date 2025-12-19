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
#include <curl/curl.h>

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

/* ===================== HTTP ===================== */

size_t curlWrite(void* ptr, size_t s, size_t n, string* out) {
    out->append((char*)ptr, s*n);
    return s*n;
}

string httpGet(const string& url) {
    CURL* c = curl_easy_init();
    string out;
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curlWrite);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "git/codecrafters");
    curl_easy_perform(c);
    curl_easy_cleanup(c);
    return out;
}

string httpPost(const string& url, const string& body) {
    CURL* c = curl_easy_init();
    string out;
    struct curl_slist* h = nullptr;
    h = curl_slist_append(h, "Content-Type: application/x-git-upload-pack-request");
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.data());
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, body.size());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curlWrite);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "git/codecrafters");
    curl_easy_perform(c);
    curl_slist_free_all(h);
    curl_easy_cleanup(c);
    return out;
}

/* ===================== PACK (simplified, no deltas) ===================== */

void parsePack(const string& pack) {
    size_t pos = 12;
    uint32_t n =
        (unsigned char)pack[8]<<24 |
        (unsigned char)pack[9]<<16 |
        (unsigned char)pack[10]<<8 |
        (unsigned char)pack[11];

    for (uint32_t i = 0; i < n; i++) {
        unsigned char b = pack[pos++];
        int type = (b >> 4) & 7;
        size_t size = b & 0xf;
        int shift = 4;
        while (b & 0x80) {
            b = pack[pos++];
            size |= (b & 0x7f) << shift;
            shift += 7;
        }

        z_stream zs{};
        inflateInit(&zs);
        zs.next_in = (Bytef*)(pack.data() + pos);
        zs.avail_in = pack.size() - pos;

        string data;
        char buf[4096];
        int r;
        do {
            zs.next_out = (Bytef*)buf;
            zs.avail_out = sizeof(buf);
            r = inflate(&zs, Z_NO_FLUSH);
            data.append(buf, sizeof(buf) - zs.avail_out);
        } while (r == Z_OK);

        pos += zs.total_in;
        inflateEnd(&zs);

        string typeStr =
            type==1?"commit":type==2?"tree":"blob";

        string obj = typeStr + " " + to_string(data.size()) + '\0' + data;
        string sha = sha1(obj);
        writeObject(sha, obj);
    }
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

/* ===================== CLONE ===================== */

void cloneRepo(const string& url, const string& dir) {
    filesystem::create_directories(dir);
    GIT_DIR = dir + "/.git";
    filesystem::create_directories(GIT_DIR + "/objects");
    filesystem::create_directories(GIT_DIR + "/refs/heads");

    string refs = httpGet(url + "/info/refs?service=git-upload-pack");

    string head;
    for (size_t i=0;i+4<refs.size();) {
        int len = stoi(refs.substr(i,4),0,16);
        i+=4;
        if (len==0) continue;
        string l = refs.substr(i,len-4);
        if (l.find("refs/heads/")!=string::npos)
            head = l.substr(0,40);
        i+=len-4;
    }

    string req = "0032want " + head + "\n00000009done\n";
    string resp = httpPost(url + "/git-upload-pack", req);

    size_t p = resp.find("PACK");
    parsePack(resp.substr(p));

    ofstream(GIT_DIR + "/HEAD") << "ref: refs/heads/main\n";
    ofstream(GIT_DIR + "/refs/heads/main") << head << "\n";

    auto [__, commit] = readObject(head);
    string tree;
    istringstream iss(commit);
    string line;
    while (getline(iss,line))
        if (line.rfind("tree ",0)==0)
            tree=line.substr(5);

    checkoutTree(tree, dir);
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

/* 1️⃣ Skip "tree <size>\0" header */
size_t pos = 0;  // START DIRECTLY AT FIRST ENTRY


/* 2️⃣ Parse entries */
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
    else if (c=="clone") {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        cloneRepo(argv[2], argv[3]);
        curl_global_cleanup();
    }
}
