#include<iostream>
#include <fstream>
#include <vector>
#include <unistd.h>
using namespace std;
bool access_data(const std::string &key, char *value_buf,
                           size_t value_size) {
    std::string readpath = key;
    if (access(readpath.c_str(), 0) == -1) {
        cout << "[Disk][Get] file does not exist!" << readpath << endl;
        return false;
    } else {
        std::ifstream ifs(readpath, std::ios::binary);
        ifs.read(value_buf, static_cast<std::streamsize>(value_size));
        if (!ifs || ifs.gcount() != static_cast<std::streamsize>(value_size)) {
            return false;
        }
        ifs.close();
    }
    return true;
}

bool access_data(const std::string &key, char *value_buf,
                           const vector<int> &idxs) {
    std::string readpath = key;
    std::ifstream file(readpath, std::ios::binary);
    if (!file)
        return false;

    file.seekg(0, std::ios::end);
    std::streamsize file_size = file.tellg();
    file.seekg(0);

    size_t n = idxs.size();
    if (n == 0)
        return true;
    if (file_size % n != 0)
        return false; // 不满足“刚好整除”前提 → 错误

    std::streamsize packet_size = file_size / n;
    char *out = value_buf;

    for (size_t i = 0; i < n; ++i) {
        if (idxs[i] != 1)
            continue;

        file.seekg(i * packet_size);
        file.read(out, packet_size);
        if (!file)
            return false;
        out += packet_size;
    }
    return true;
}

bool store_data(const std::string &key, const char *value,
                          size_t value_size) {
    // ELOG(WARNING) << "[store_data] write data to disk. key = " << key;
    std::string writepath = key;
    std::ofstream ofs(writepath,
                      std::ios::binary | std::ios::out | std::ios::trunc);
    ofs.write(value, value_size);
    ofs.flush();
    ofs.close();
    return true;
}