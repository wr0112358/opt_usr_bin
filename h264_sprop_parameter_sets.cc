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

extern "C" {
#include <gst/codecparsers/gsth264parser.h>
}

#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>

#include <libaan/string.hh>

//#define TEST
#ifdef TEST
const std::vector<std::string> sprops_ex = {
    "Z0IAKeNQFAe2AtwEBAaQeJEV,aM48gA==",
    "sprop-parameter-sets=Z2QAFKzZQ0R+f/zBfMMAQAAAAwBAAAAKI8UKZYA=,aOvssiw=;",
    "Z0KAHoxoEBM/8AAQABAI,aM4EYg==",
    "sprop-parameter-sets=Z0KAHoxoEBM/8AAQABAI,aM4EYg==",
    "Z2QAH6zZQFAFuwEQAAADABAAAAMDIPGDGWA=,aOvjyyLA",
    "sprop-parameter-sets=Z2QAH6zZQFAFuwEQAAADABAAAAMDIPGDGWA=,aOvjyyLA"
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
    for(size_t i = 0; i < lim; i++) {
        if(i && i % 24 == 0)
            puts("");
        printf("%02x ", (unsigned int)*(unsigned char *)&vec[i]);
    }
    puts("");
}

void dumpv(const std::vector<unsigned char> &vec, size_t max = 0)
{
    return dumpv(vec.data(), vec.size(), max);
}

bool encode_sprops(const std::vector<unsigned char> &sps,
                   const std::vector<unsigned char> &pps)
{
    std::string sps64(124, '\0');
    sps64.resize(base64encode(reinterpret_cast<const char *>(&sps[0]), sps.size(),
                              &sps64[0], sps64.size()));
    std::string pps64(1024, '\0');
    pps64.resize(base64encode(reinterpret_cast<const char *>(&pps[0]), pps.size(),
                              &pps64[0], pps64.size()));
                              
    printf("sprop-parameter-sets=%s,%s\n", sps64.c_str(), pps64.c_str());

    return true;
}

template<size_t blobsize, typename lambda_t>
void foreach_blob(const char *filename, lambda_t lambda)
{
    std::ifstream fp(filename, std::ios::binary);
    std::vector<unsigned char> buf(blobsize, 0);
    size_t fill = 0;
    size_t total_off = 0;
    do {
        fp.read(reinterpret_cast<char *>(buf.data() + fill), buf.size() - fill);
        fill += fp.gcount();
        if(!fill)
            return;
        const auto ret = lambda(buf.data(), fill, total_off);
        if(!ret.first)
            break;

        assert(fill >= ret.second);
        assert(!ret.first ? ret.second != 0 : true);
        if(ret.first && ret.second == 0)
            printf("No NALUs found in buffer of size %zu. incresing buffer might help\n", blobsize);

        fill -= ret.second;
        if(fill)
            std::rotate(std::begin(buf), std::begin(buf) + ret.second, std::end(buf));
        total_off += ret.second;
    } while(!fp.eof());
}

bool do_blockwise(const char *file)
{
    auto parser = gst_h264_nal_parser_new();
    size_t count = 0;
    size_t last_sps = 0;
    bool have = false;
    std::vector<unsigned char> sps;
    std::vector<unsigned char> pps;
    // buffer size must be larger than largest NALU
    foreach_blob<32768 * 6>
        (file, [&parser, &count, &sps, &pps, &last_sps, &have]
         (const unsigned char *data, size_t len, size_t total_off) {
            size_t off = 0;
            do {
                GstH264NalUnit nalu;
                const auto result = gst_h264_parser_identify_nalu(parser, data, off, len, &nalu);
                if(result == GST_H264_PARSER_NO_NAL_END)
                    break;
                if(result != GST_H264_PARSER_OK)
                    return std::make_pair(false, 0lu);

                ++count;
                assert(nalu.offset >= nalu.sc_offset);
                assert(len > (nalu.offset + nalu.size));
                //const auto sc_size = nalu.offset - nalu.sc_offset;
                switch(nalu.type) {
                case GST_H264_NAL_SPS:
                    if(!sps.empty()) {
                        puts("no pps between sps. ignoring last sps");
                        sps.clear();
                    }

                    printf("have sps: size=%u\n", nalu.size);
                    dumpv(data + nalu.offset, nalu.size, nalu.size);
                    sps.insert(std::begin(sps), data + nalu.offset,
                               data + nalu.offset + nalu.size);
                    last_sps = count;
                    break;
                case GST_H264_NAL_PPS:
                    assert(pps.empty());
                    assert(count > last_sps);
                    if(count - last_sps > 1)
                        printf("distance: pps - sps > 1: %zu\n", count - last_sps);
                    printf("have pps: size=%u\n", nalu.size);
                    dumpv(data + nalu.offset, nalu.size, nalu.size);
                    if(sps.empty()) {
                        puts("no sps before pps. ignoring pps");
                        break;
                    }
                    pps.insert(std::begin(pps), data + nalu.offset,
                               data + nalu.offset + nalu.size);
                    assert(!sps.empty() && !pps.empty());
                    encode_sprops(sps, pps);
                    have = true;
                    puts("");
                    sps.clear();
                    pps.clear();

                    break;
                }

                off = nalu.offset + nalu.size;
            } while(true);
            return std::make_pair(true, off);
        });
    gst_h264_nal_parser_free(parser);
    return have;
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
        ok = do_blockwise(argv[1]);
    }

    if(!ok) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    exit(EXIT_SUCCESS);
#endif
}
