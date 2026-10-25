// =========================== AES-128-ECB 实现 ===========================
namespace aes {

static const uint8_t sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};
static const uint8_t inv_sbox[256] = {
    0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb,
    0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb,
    0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e,
    0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25,
    0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92,
    0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84,
    0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06,
    0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b,
    0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73,
    0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e,
    0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b,
    0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4,
    0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f,
    0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef,
    0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61,
    0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d
};
static const uint8_t rcon[11] = {0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36};

inline uint8_t xtime(uint8_t x) { return (uint8_t)((x << 1) ^ (((x >> 7) & 1) * 0x1b)); }
inline uint8_t gmul(uint8_t a, uint8_t b) {
    uint8_t r = 0;
    for (int i = 0; i < 8; ++i) {
        if (b & 1) r ^= a;
        a = xtime(a);
        b >>= 1;
    }
    return r;
}

static void keyExpansion(const uint8_t key[16], uint8_t rk[176]) {
    for (int i = 0; i < 16; ++i) rk[i] = key[i];
    uint8_t t[4];
    for (int i = 16; i < 176; i += 4) {
        for (int j = 0; j < 4; ++j) t[j] = rk[i - 4 + j];
        if ((i & 0x0f) == 0) {
            uint8_t tmp = t[0]; t[0] = t[1]; t[1] = t[2]; t[2] = t[3]; t[3] = tmp;
            for (int j = 0; j < 4; ++j) t[j] = sbox[t[j]];
            t[0] ^= rcon[i >> 4];
        }
        for (int j = 0; j < 4; ++j) rk[i + j] = rk[i - 16 + j] ^ t[j];
    }
}
static void addRoundKey(uint8_t s[16], const uint8_t* rk) {
    for (int i = 0; i < 16; ++i) s[i] ^= rk[i];
}
static void invShiftRows(uint8_t s[16]) {
    uint8_t t;
    t = s[13]; s[13] = s[9]; s[9] = s[5]; s[5] = s[1]; s[1] = t;
    t = s[2];  s[2]  = s[10]; s[10] = t;
    t = s[6];  s[6]  = s[14]; s[14] = t;
    t = s[3];  s[3]  = s[7];  s[7]  = s[11]; s[11] = s[15]; s[15] = t;
}
static void invSubBytes(uint8_t s[16]) {
    for (int i = 0; i < 16; ++i) s[i] = inv_sbox[s[i]];
}
static void invMixColumns(uint8_t s[16]) {
    for (int c = 0; c < 4; ++c) {
        uint8_t* col = s + c * 4;
        uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
        col[0] = (uint8_t)(gmul(0x0e,a0)^gmul(0x0b,a1)^gmul(0x0d,a2)^gmul(0x09,a3));
        col[1] = (uint8_t)(gmul(0x09,a0)^gmul(0x0e,a1)^gmul(0x0b,a2)^gmul(0x0d,a3));
        col[2] = (uint8_t)(gmul(0x0d,a0)^gmul(0x09,a1)^gmul(0x0e,a2)^gmul(0x0b,a3));
        col[3] = (uint8_t)(gmul(0x0b,a0)^gmul(0x0d,a1)^gmul(0x09,a2)^gmul(0x0e,a3));
    }
}
static void decryptBlock(uint8_t s[16], const uint8_t rk[176]) {
    addRoundKey(s, rk + 160);
    for (int round = 9; round >= 1; --round) {
        invShiftRows(s);
        invSubBytes(s);
        addRoundKey(s, rk + round * 16);
        invMixColumns(s);
    }
    invShiftRows(s);
    invSubBytes(s);
    addRoundKey(s, rk);
}
// ECB 解密 (仅用于 ncm 头部的密钥/元数据, 数据量很小)
static std::vector<uint8_t> ecbDecrypt(const uint8_t* data, size_t len, const uint8_t key[16]) {
    if (len == 0 || (len % 16) != 0) return {};
    uint8_t rk[176];
    keyExpansion(key, rk);
    std::vector<uint8_t> out(len);
    uint8_t blk[16];
    for (size_t i = 0; i < len; i += 16) {
        memcpy(blk, data + i, 16);
        decryptBlock(blk, rk);
        memcpy(out.data() + i, blk, 16);
    }
    return out;
}
static size_t pkcs7Unpad(const uint8_t* data, size_t len) {
    if (len == 0 || (len % 16) != 0) return len;
    uint8_t pad = data[len - 1];
    if (pad == 0 || pad > 16) return len;
    for (size_t i = 0; i < pad; ++i)
        if (data[len - 1 - i] != pad) return len;
    return len - pad;
}
} // namespace aes

// =========================== NCM 解密 (流式, 低内存) ===========================
struct NcmResult {
    bool success = false;
    std::vector<uint8_t> audio;
    std::string songName;      // UTF-8
    std::string format;        // "mp3" / "flac"
    std::string error;
    long long fileSize = 0;
};

// Base64 解码
static bool b64Decode(const std::string& in, std::vector<uint8_t>& out) {
    static const char* tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int val[256];
    for (int i = 0; i < 256; ++i) val[i] = -1;
    for (int i = 0; i < 64; ++i) val[(uint8_t)tbl[i]] = i;
    out.clear();
    out.reserve(in.size() / 4 * 3);
    int buf = 0, bits = 0;
    for (unsigned char c : in) {
        if (c == '=' || c == '\r' || c == '\n') continue;
        int v = val[c];
        if (v < 0) return false;
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((uint8_t)((buf >> bits) & 0xff));
        }
    }
    return true;
}

static NcmResult decryptNcm(const std::wstring& path, std::function<void(float)> prog) {
    NcmResult r;
    FILE* fp = _wfopen(path.c_str(), L"rb");
    if (!fp) { r.error = "无法打开文件"; return r; }

    auto freadFull = [&](void* buf, size_t n) -> bool {
        return n == 0 || fread(buf, 1, n, fp) == n;
    };

    _fseeki64(fp, 0, SEEK_END);
    long long fsize = _ftelli64(fp);
    _fseeki64(fp, 0, SEEK_SET);
    r.fileSize = fsize;
    if (fsize <= 0) { fclose(fp); r.error = "文件为空"; return r; }

    char magic[8];
    if (!freadFull(magic, 8)) { fclose(fp); r.error = "读取失败"; return r; }
    if (memcmp(magic, "CTENFDAM", 8) != 0) { fclose(fp); r.error = "不是有效的 NCM 文件"; return r; }

    static const uint8_t coreKey[16] = {
        0x68,0x7a,0x48,0x52,0x41,0x6d,0x73,0x6f,
        0x35,0x6b,0x49,0x6e,0x62,0x61,0x78,0x57};
    static const uint8_t metaKey[16] = {
        0x23,0x31,0x34,0x6c,0x6a,0x6b,0x5f,0x21,
        0x5c,0x5d,0x26,0x30,0x55,0x3c,0x27,0x28};

    // --- magic 后有 2 字节 gap ---
    _fseeki64(fp, 2, SEEK_CUR);

    // --- RC4 密钥: 4 字节小端长度, 数据先 XOR 0x64 再 AES-ECB 解密 ---
    uint32_t keyLen = 0;
    if (!freadFull(&keyLen, 4)) { fclose(fp); r.error = "读取失败"; return r; }
    if (keyLen == 0 || keyLen > 1024 * 1024) { fclose(fp); r.error = "文件格式错误(密钥)"; return r; }
    std::vector<uint8_t> encKey(keyLen);
    if (!freadFull(encKey.data(), keyLen)) { fclose(fp); r.error = "读取失败"; return r; }
    for (size_t i = 0; i < encKey.size(); ++i) encKey[i] ^= 0x64;

    auto decKey = aes::ecbDecrypt(encKey.data(), keyLen, coreKey);
    size_t dkLen = aes::pkcs7Unpad(decKey.data(), decKey.size());
    std::string keyStr;
    if (dkLen > 17) keyStr = std::string(reinterpret_cast<char*>(decKey.data()) + 17, dkLen - 17);
    // 兼容: 去掉可能存在的 "163 key(Don't modify):" 前缀
    {
        const std::string pfx = "163 key(Don't modify):";
        auto pos = keyStr.find(pfx);
        if (pos != std::string::npos) keyStr = keyStr.substr(pos + pfx.size());
    }
    // 去尾部空白
    while (!keyStr.empty() && (keyStr.back() == '\0' || keyStr.back() == ' '))
        keyStr.pop_back();
    if (keyStr.empty()) { fclose(fp); r.error = "解析密钥失败"; return r; }

    // --- 元数据: 4 字节小端长度, 数据 XOR 0x63, "163 key(Don't modify):" 后为 base64 ---
    uint32_t metaLen = 0;
    if (!freadFull(&metaLen, 4)) { fclose(fp); r.error = "读取失败"; return r; }
    if (metaLen > 16 * 1024 * 1024) { fclose(fp); r.error = "文件格式错误(元数据)"; return r; }
    if (metaLen > 0) {
        std::vector<uint8_t> encMeta(metaLen);
        if (!freadFull(encMeta.data(), metaLen)) { fclose(fp); r.error = "读取失败"; return r; }
        for (size_t i = 0; i < encMeta.size(); ++i) encMeta[i] ^= 0x63;
        std::string meta(reinterpret_cast<char*>(encMeta.data()), encMeta.size());
        std::string body = meta;
        const std::string pfx = "163 key(Don't modify):";
        auto pos = meta.find(pfx);
        bool needAes = false;
        if (pos != std::string::npos) {          // 经 base64 + AES 的旧式封装
            body = meta.substr(pos + pfx.size());
            needAes = true;
        } else if (meta.find("\"musicName\"") == std::string::npos) {
            body.clear();                         // 未知结构, 放弃元数据
        }
        if (needAes) {
            std::vector<uint8_t> b64out;
            if (b64Decode(body, b64out) && !b64out.empty() && b64out.size() % 16 == 0) {
                auto dm = aes::ecbDecrypt(b64out.data(), b64out.size(), metaKey);
                size_t dmLen = aes::pkcs7Unpad(dm.data(), dm.size());
                if (dmLen > 6) body = std::string(reinterpret_cast<char*>(dm.data()) + 6, dmLen - 6);
                else body.clear();
            } else body.clear();
        }
        // 提取歌名与格式
        auto extractStr = [&](const std::string& keyfield) -> std::string {
            auto np = body.find("\"" + keyfield + "\"");
            if (np == std::string::npos) return "";
            auto cp = body.find(':', np);
            if (cp == std::string::npos) return "";
            auto q1 = body.find('"', cp);
            if (q1 == std::string::npos) return "";
            auto q2 = body.find('"', q1 + 1);
            if (q2 == std::string::npos) return "";
            return body.substr(q1 + 1, q2 - q1 - 1);
        };
        r.songName = extractStr("musicName");
        r.format = extractStr("format");
        if (!r.format.empty()) {
            for (auto& c : r.format) c = (char)tolower((unsigned char)c);
        }
    }

    // --- CRC(4) + gap(5) ---
    _fseeki64(fp, 9, SEEK_CUR);

    // --- 封面图: 4 字节小端长度 ---
    uint32_t imgSize = 0;
    if (!freadFull(&imgSize, 4)) { fclose(fp); r.error = "读取失败"; return r; }
    if (imgSize > 0 && imgSize < (uint32_t)fsize)
        _fseeki64(fp, imgSize, SEEK_CUR);

    long long musicStart = _ftelli64(fp);
    if (musicStart < 0) { fclose(fp); r.error = "文件读取错误"; return r; }
    size_t musicLen = (size_t)(fsize - musicStart);
    if (musicLen == 0) { fclose(fp); r.error = "无音频数据"; return r; }

    // --- 构造 key box (RC4-KSA 变种) ---
    std::vector<uint8_t> rc4Key(keyStr.begin(), keyStr.end());
    uint8_t box[256];
    for (int i = 0; i < 256; ++i) box[i] = (uint8_t)i;
    {
        uint8_t c = 0, last = 0;
        size_t ko = 0;
        for (int i = 0; i < 256; ++i) {
            uint8_t swap = box[i];
            c = (uint8_t)((swap + last + rc4Key[ko]) & 0xff);
            ko++;
            if (ko >= rc4Key.size()) ko = 0;
            box[i] = box[c];
            box[c] = swap;
            last = c;
        }
    }

    // --- 流式解密: 第 k 字节(0起) 用流索引 j=(k+1)&0xff ---
    r.audio.reserve(musicLen);
    const size_t CHUNK = 1 << 20;                    // 1MB
    std::vector<uint8_t> buf(CHUNK);
    size_t processed = 0;
    while (processed < musicLen) {
        size_t n = (std::min)(CHUNK, musicLen - processed);
        if (fread(buf.data(), 1, n, fp) != n) { fclose(fp); r.error = "读取音频数据失败"; return r; }
        uint8_t* p = buf.data();
        for (size_t k = 0; k < n; ++k) {
            unsigned j = (unsigned)((processed + k + 1) & 0xff);
            p[k] ^= box[(box[j] + box[(box[j] + j) & 0xff]) & 0xff];
        }
        r.audio.insert(r.audio.end(), buf.begin(), buf.begin() + n);
        processed += n;
        if (prog) prog((float)((double)processed / (double)musicLen));
    }
    fclose(fp);
    if (prog) prog(1.0f);

    // --- 校验音频头 (ID3/mp3 或 fLaC), 不匹配则报错 ---
    if (r.audio.size() >= 4) {
        bool isMp3 = r.audio[0] == 'I' && r.audio[1] == 'D' && r.audio[2] == '3';
        bool isMp3Raw = r.audio[0] == 0xFF && (r.audio[1] & 0xE0) == 0xE0;
        bool isFlac = r.audio[0] == 'f' && r.audio[1] == 'L' && r.audio[2] == 'a' && r.audio[3] == 'C';
        if (!isMp3 && !isMp3Raw && !isFlac) {
            r.audio.clear();
            r.error = "解密校验失败(密钥不匹配)";
            return r;
        }
        if (r.format.empty()) r.format = isFlac ? "flac" : "mp3";
    }

    r.success = true;
    return r;
}

// =========================== 咪咕 MG3D 解密 ===========================
// 算法依据 unlock-music 开源实现 (MIT):
//   密钥藏在文件 0x20 起、每 0x20 字节一段(前 19 段)的 32 字节大写 HEX 串中;
//   对候选密钥: 头 0x100 字节每字节做 (data - key[i%32]) 模 256,
//   校验 RIFF / WAVEfmt / fmt 块大小 16|18|40 / 后续块名可打印;
//   用首个有效密钥对全文件做同样的减法, 输出 WAV。
static NcmResult decryptMg3d(const std::wstring& path, std::function<void(float)> prog) {
    NcmResult r;
    FILE* fp = _wfopen(path.c_str(), L"rb");
    if (!fp) { r.error = "无法打开文件"; return r; }
    _fseeki64(fp, 0, SEEK_END);
    long long fsize = _ftelli64(fp);
    _fseeki64(fp, 0, SEEK_SET);
    r.fileSize = fsize;
    if (fsize < 0x280) { fclose(fp); r.error = "MG3D 文件过小或已损坏"; return r; }

    const int SEG = 0x20;
    uint8_t head[0x280];
    if (fread(head, 1, sizeof(head), fp) != sizeof(head)) {
        fclose(fp); r.error = "读取文件头失败"; return r;
    }

    auto isHexUpper = [](uint8_t c) {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
    };
    auto isPrintable = [](uint8_t c) { return c >= 0x20 && c <= 0x7E; };
    auto rd32le = [](const uint8_t* p) -> uint32_t {
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
               ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    };

    uint8_t key[SEG] = {0};
    bool found = false;
    uint8_t trial[0x100];
    for (int i = SEG; i < SEG * 20 && i + SEG <= (int)sizeof(head); i += SEG) {
        const uint8_t* cand = head + i;
        bool hexOk = true;
        for (int j = 0; j < SEG; ++j) if (!isHexUpper(cand[j])) { hexOk = false; break; }
        if (!hexOk) continue;
        for (int j = 0; j < 0x100; ++j) trial[j] = (uint8_t)(head[j] - cand[j % SEG]);
        if (memcmp(trial, "RIFF", 4) != 0) continue;
        if (memcmp(trial + 8, "WAVEfmt ", 8) != 0) continue;
        uint32_t fmtSize = rd32le(trial + 0x10);
        if (fmtSize != 16 && fmtSize != 18 && fmtSize != 40) continue;
        int chunk1 = 0x14 + (int)fmtSize;
        if (chunk1 + 8 > 0x100) continue;
        bool c1 = true;
        for (int j = 0; j < 4; ++j) if (!isPrintable(trial[chunk1 + j])) { c1 = false; break; }
        if (!c1) continue;
        int chunk2 = chunk1 + 8 + (int)rd32le(trial + chunk1 + 4);
        if (chunk2 + 4 <= 0x100) {
            bool c2 = true;
            for (int j = 0; j < 4; ++j) if (!isPrintable(trial[chunk2 + j])) { c2 = false; break; }
            if (!c2) continue;
        }
        memcpy(key, cand, SEG);
        found = true;
        break;
    }
    if (!found) { fclose(fp); r.error = "未找到 MG3D 内嵌密钥(文件可能已更新格式)"; return r; }

    // 流式整文件解密: p[k] = (p[k] - key[k % 32]) mod 256
    _fseeki64(fp, 0, SEEK_SET);
    r.audio.reserve((size_t)fsize);
    const size_t CHUNK = 1 << 20;
    std::vector<uint8_t> buf(CHUNK);
    long long processed = 0;
    while (processed < fsize) {
        size_t n = (std::min)(CHUNK, (size_t)(fsize - processed));
        if (fread(buf.data(), 1, n, fp) != n) { fclose(fp); r.error = "读取音频数据失败"; return r; }
        for (size_t k = 0; k < n; ++k) {
            size_t pos = (size_t)processed + k;
            buf[k] = (uint8_t)(buf[k] - key[pos % SEG]);
        }
        r.audio.insert(r.audio.end(), buf.begin(), buf.begin() + n);
        processed += (long long)n;
        if (prog) prog((float)((double)processed / (double)fsize));
    }
    fclose(fp);
    if (r.audio.size() < 12 || memcmp(r.audio.data(), "RIFF", 4) != 0 ||
        memcmp(r.audio.data() + 8, "WAVE", 4) != 0) {
        r.audio.clear();
        r.error = "MG3D 解密校验失败";
        return r;
    }
    r.format = "wav";
    if (prog) prog(1.0f);
    r.success = true;
    return r;
}

// 格式选择
enum FmtChoice { FMT_AUTO = 0, FMT_NCM = 1, FMT_MG3D = 2, FMT_M4P = 3, FMT_MGU = 4 };

static std::wstring extOf(const std::wstring& path) {
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) return L"";
    std::wstring e = path.substr(dot + 1);
    for (auto& c : e) c = (wchar_t)towlower((wint_t)c);
    return e;
}

// 按选择的格式分发解密
static NcmResult decryptFile(const std::wstring& path, int choice,
                             std::function<void(float)> prog) {
    NcmResult r;
    int fmt = choice;
    if (fmt == FMT_AUTO) {
        std::wstring e = extOf(path);
        if (e == L"ncm") fmt = FMT_NCM;
        else if (e == L"mg3d") fmt = FMT_MG3D;
        else { r.error = "不支持的文件类型, 仅支持 .ncm(网易云) 和 .mg3d(咪咕)"; return r; }
    }
    switch (fmt) {
    case FMT_NCM:  return decryptNcm(path, prog);
    case FMT_MG3D: return decryptMg3d(path, prog);
    default:
        r.error = "不支持的文件类型, 仅支持 .ncm(网易云) 和 .mg3d(咪咕)";
        return r;
    }
}

