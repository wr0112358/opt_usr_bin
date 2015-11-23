/* test:
 file=jvt_nal.h264
 ./h264_sprop_parameter_sets $file | less
 h264_analyze  $file | grep -E 'Found\ NAL|nal_unit_type' | less

usage:
./h264_sprop_parameter_sets $file | tail
...
SPS: 67 42 80 1e 8c 68 10 13 3f f0 00 10 00 10 08 
PPS: 68 ce 04 62 
sprop-parameter-sets=Z0KAHoxoEBM/8AAQABAI,aM4EYg==

./h264_sprop_parameter_sets sprop-parameter-sets=Z0KAHoxoEBM/8AAQABAI,aM4EYg==
sps "67 42 80 1e 8c 68 10 13 3f f0 0 10 0 10 8 "
pps "68 ce 4 62 "

*/

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>

#include <libaan/fd.hh>
#include <libaan/string.hh>

#ifdef TEST
const std::vector<std::string> sprops_ex = {
    "Z0IAKeNQFAe2AtwEBAaQeJEV,aM48gA==",
    "sprop-parameter-sets=Z2QAFKzZQ0R+f/zBfMMAQAAAAwBAAAAKI8UKZYA=,aOvssiw=;"
};
#endif

size_t base64decode(const void* in, const size_t in_len, char* out, const size_t out_len)
{
    BIO *b64 = BIO_new(BIO_f_base64());
    BIO *mem = BIO_new(BIO_s_mem());
    const auto w = BIO_write(mem, in, in_len);
    assert(w > 0 && (size_t)w == in_len);
    BIO_push(b64, mem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    const auto r = BIO_read(b64, out, out_len);
    BIO_free_all(b64);
    return r > 0 ? r : 0;
}

size_t base64encode(const void* in, const size_t in_len, char* out, const size_t out_len)
{
    BIO *b64 = BIO_new(BIO_f_base64());
    BIO *mem = BIO_new(BIO_s_mem());
    mem = BIO_push(b64, mem);
    BIO_set_flags(mem, BIO_FLAGS_BASE64_NO_NL);
    const auto w = BIO_write(mem, in, in_len);
    assert(w > 0 && (size_t)w == in_len);
    (void)BIO_flush(mem);

    BUF_MEM *buf;
    BIO_get_mem_ptr(mem, &buf);
    const auto r = std::min(buf->length, out_len);
    memcpy(out, buf->data, r);
    BIO_free_all(mem);
    return r > 0 ? r : 0;
}

bool decode_sprops(const std::string &sprops)
{
    const char *in = sprops.c_str();
    size_t len = sprops.length();
    size_t start = 0;
    const auto SPROP = std::string("sprop-parameter-sets=");
    if(libaan::startswith(sprops, SPROP)) {
        in += SPROP.length();
        if(len <= SPROP.length())
            return false;
        len -= SPROP.length();
        start += SPROP.length();
    }

    auto off = sprops.find(',', start);
    if(off == std::string::npos)
        return false;
    assert(off >= start);
    off -= start;
    off++; // skip ,

    std::string sps(1024, '\0');
    sps.resize(base64decode(in, off, &sps[0], sps.length()));
    std::string pps(1024, '\0');
    pps.resize(base64decode(in + off, len - off, &pps[0], pps.length()));

    std::cout << "sps \"";
    for(unsigned char c: sps)
        std::cout << std::hex << +c << " ";
    std::cout << "\"\n";
    std::cout << "pps \"";
    for(unsigned char c: pps)
        std::cout << std::hex << +c << " ";
    std::cout << "\"\n";

//    decode_sps(sps.c_str(), sps.length());
//    decode_sps_ffmpeg(sps.c_str(), sps.length());
    return true;
}

void dumpv(const unsigned char *vec, size_t size, size_t max)
{
    const auto lim = max ? std::min(max, size) : size;
    for(size_t i = 0; i < lim; i++)
        if(i && i % 20 == 0)
            puts("");
        else
            printf("%02x ", (unsigned int)*(unsigned char *)&vec[i]);
    puts("");
}

void dumpv(const std::vector<unsigned char> &vec, size_t max = 0)
{
    return dumpv(vec.data(), vec.size(), max);
}

bool encode_sprops(const std::vector<unsigned char> &sps,
                   const std::vector<unsigned char> &pps)
{
    std::cout << "SPS: ";
    dumpv(sps);
    std::cout << "PPS: ";
    dumpv(pps);

    std::string sps64(124, '\0');
    sps64.resize(base64encode(reinterpret_cast<const char *>(&sps[0]), sps.size(),
                              &sps64[0], sps64.size()));
    std::string pps64(1024, '\0');
    pps64.resize(base64encode(reinterpret_cast<const char *>(&pps[0]), pps.size(),
                              &pps64[0], pps64.size()));
                              
    std::cout << "sprop-parameter-sets=" << sps64 << "," << pps64 << "\n";

    return true;
}

template<typename lambda_t>
void for_each_nalu(const char *file, lambda_t lambda)
{
    int fd = open(file, O_RDONLY, NULL);
    if(fd == -1) {
        perror("open");
        return;
    }

    std::vector<char> buf(8192, 0);
    std::vector<char> old(8192, 0);
    size_t old_off = 0;
   size_t old_size = 0;

    int ret;
    size_t total = 0;
    bool exit = false;
    do {
        ret = libaan::readall(fd, &buf[0], buf.size());
        if(ret <= 0) {
            if(old_off)
                if(!lambda(reinterpret_cast<unsigned char *>(&old[old_off]), old_size - old_off)) {
                    exit = true;
                    break;
                }
            break;
        }

        const uint8_t start_code[] = { 0x0, 0x0, 0x01 };
        const void *x;
        size_t off = 0;

        size_t last_start = 0;
        while((x = memmem(&buf[off], ret - off, start_code, sizeof(start_code))) != NULL) {
            size_t start = (uintptr_t)x - (uintptr_t)&buf[0];
            const auto start_code_size = (start > 0 && buf[start - 1] == 0u) ? 4 : 3;
            //std::cout << "have NALU at offset " << total + start << " off=" << start << "\n";
            if((size_t)ret == off)
                break;
            const size_t len = start - off;
            off = start + sizeof(start_code);
            if(old_off > 0) {
                if(start == 0) {
                    if(!lambda(reinterpret_cast<unsigned char *>(&old[old_off]), old_size - old_off)) {
                        exit = true;
                        break;
                    }
                    if(!lambda(reinterpret_cast<unsigned char *>(&buf[off]), len)) {
                        exit = true;
                        break;
                    }
                } else {
                    std::rotate(std::begin(old), std::begin(old) + old_off, std::end(old));
                    memcpy(reinterpret_cast<unsigned char *>(&old[old_size - old_off]), &buf[0], start);
                    if(!lambda(reinterpret_cast<unsigned char *>(&old[0]), old_size - old_off + start)) {
                        exit = true;
                        break;
                    }
                }
                old_off = 0;
            } else {
                if(last_start > 0)
                    if(!lambda(reinterpret_cast<unsigned char *>(&buf[last_start + sizeof(start_code)]), start - last_start - start_code_size)) {
                        exit = true;
                        break;
                    }
                last_start = start;
            }
        }
        if(exit)
            break;

        if(old_off)
            ;

        old_off = off;
        old_size = ret;
        //std::cout << "buf done. left = " << ret - old_off << "\n\n";
        if(old_off)
            std::swap(buf, old);
        total += off;
    } while(ret > 0);
    close(fd);
}

bool get_spspps(const char *file)
{
    std::vector<std::vector<unsigned char>> sps;
    std::vector<std::vector<unsigned char>> pps;
    for_each_nalu(file, [&sps, &pps](const unsigned char *nalu, size_t len) {
            std::cout << "  nalu len = " << len << " first 20 are: ";
            dumpv(nalu, len, 20);
            assert((nalu[0] & 0x80) == 0);
            std::cout << "NAL_REF_IDC = " << (unsigned int)((nalu[0] & 0xe0) >> 5) << " ";
            std::cout << "NAL_TYPE = " << (unsigned int)((nalu[0] & 0x1f)) << " ";
            assert(((nalu[0] & 0xe0) >> 5) == 3); // x264 sets nal_ref_idc always to 3(highest priority IDR)

            switch(nalu[0] & 0x1f) {
            case 7:
                std::cout << "SPS\n";
                sps.emplace_back(nalu, nalu + len);
                break;
            case 8:
                std::cout << "PPS\n";
                if(pps.empty())
                    pps.emplace_back(nalu, nalu + len);
                break;
            case 9: std::cout << "AUD\n"; break;
            default: std::cout << "OTHER\n"; break;
            }
                return true;
        });

    assert(sps.size() == pps.size());
    for(size_t i = 0; i < sps.size(); i++)
        encode_sprops(sps[i], pps[i]);

    return true;
}

void usage(const char *arg0)
{
    std::cout << "Usage: " << arg0 << "<sprop-parameter-sets>|<file_with_annex_b_h264_bytestream>\n";
}

int main(int argc, char *argv[])
{
#ifdef TEST
    for(const auto &s: sprops_ex)
        decode_sprops(s);
#else

    if(argc != 2) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    bool ok = false;

    struct stat s;
    const auto err = ::stat(argv[1], &s);
    if(err == -1) {
        if(!(errno == EACCES || errno == ENOENT))
            perror("stat");
        else
            ok = decode_sprops(argv[1]);
    } else {
        ok = get_spspps(argv[1]);
    }

    if(!ok) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    exit(EXIT_SUCCESS);
#endif
}
