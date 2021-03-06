/*
"stalling pidof"

like pidof -s, but waits for process to show up

Usage example:
sudo perf top -p $(/opt/usr/bin/spidof h264dec)
htop -p $(/opt/usr/bin/spidof h264dec)

*/

#include <atomic>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <experimental/filesystem>

#ifdef USE_PROC_CONN
// Employ proc connector API to reduce need for polling /proc.

extern "C" {
#include <linux/cn_proc.h>
#include <arpa/inet.h>
#include <linux/filter.h>
#include <linux/netlink.h>
#include <linux/connector.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/capability.h>
}

#include <cassert>
#include <fstream>
#include <vector>

namespace {

// Filter out all non-relevant messages in the kernel.
void filter(int sock)
{
    // return amount of bytes of the packet
    // context: | struct nlmsghdr | struct cn_msg | struct proc_event ... |
    struct sock_filter f[] = {
        // 1. return all if type != NLMSG_DONE
        BPF_STMT(BPF_LD | BPF_H | BPF_ABS,
                 __builtin_offsetof(struct nlmsghdr, nlmsg_type)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, htons(NLMSG_DONE), 1, 0),
        BPF_STMT(BPF_RET | BPF_K, 0xffffffff),

        // 2. return all if cn_msg::id::idx != CN_IDX_PROC
        // load 32bit id from absolute address given in argument
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                 // skip nlmsghdr
                 NLMSG_LENGTH(0)
                 // add offset to: cn_msg::id::idx
                 + __builtin_offsetof(struct cn_msg, id)
                 + __builtin_offsetof(struct cb_id, idx)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, htonl(CN_IDX_PROC), 1, 0),
        BPF_STMT(BPF_RET | BPF_K, 0xffffffff),

        // 3. return all if cn_msg::id::val != CN_VAL_PROC
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                 NLMSG_LENGTH(0) + __builtin_offsetof(struct cn_msg, id)
                 + __builtin_offsetof(struct cb_id, val)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, htonl(CN_VAL_PROC), 1, 0),
        BPF_STMT(BPF_RET | BPF_K, 0xffffffff),

        // packet contains 1 netlink msg from proc_cn

        // 4. if proc_event type is not PROC_EVENT_EXEC, throw away packet
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                 NLMSG_LENGTH(0) + __builtin_offsetof(struct cn_msg, data)
                 + __builtin_offsetof(struct proc_event, what)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, htonl(proc_event::PROC_EVENT_EXEC), 1, 0),
        BPF_STMT(BPF_RET | BPF_K, 0),
        BPF_STMT(BPF_RET | BPF_K, 0xffffffff),
    };

    struct sock_fprog fprog;
    fprog.filter = f;
    fprog.len = sizeof f / sizeof f[0];
    if(setsockopt(sock, SOL_SOCKET, SO_ATTACH_FILTER, &fprog, sizeof fprog) == -1)
        perror("setsockopt");
}

bool cap_set_if_not(cap_t caps, cap_flag_value_t val, cap_value_t *list, size_t size)
{
    cap_flag_value_t flag;
    if(cap_get_flag(caps, list[0], CAP_EFFECTIVE, &flag) == -1) {
        perror("cap_get_flag");
        return false;
    }

    if(flag != val) {
        if(cap_set_flag(caps, CAP_EFFECTIVE, size, list, val) == -1) {
            perror("cap_set_flag");
            return false;

        }
        if(cap_set_proc(caps) == -1) {
            perror("cap_set_proc");
            return false;
        }
    }
    return true;
}

// Test if CAP_NET_ADMIN is effective, else make it effective
bool get_priv()
{
    cap_value_t cap_list[1] = { CAP_NET_ADMIN };
    if(!CAP_IS_SUPPORTED(CAP_NET_ADMIN)) {
        std::cerr << "Capability CAP_NET_ADMIN is not supported\n";
        return false;
    }

    cap_t caps = cap_get_proc();
    if(caps == NULL) {
        perror("cap_get_proc");
        return false;
    }

    // Ensure that CAP_NET_ADMIN is permitted
    bool ret = false;
    cap_flag_value_t flag;
    if(cap_get_flag(caps, cap_list[0], CAP_PERMITTED, &flag) == -1) {
        perror("cap_get_flag");
        goto out;
    }
    if(flag != CAP_SET) {
        std::cerr << "Capability CAP_NET_ADMIN is not CAP_PERMITTED, run setcap CAP_NET_ADMIN=p <binary>\n";
        goto out;
    }

    ret = cap_set_if_not(caps, CAP_SET, cap_list, 1);
    assert(ret || (cap_get_flag(caps, cap_list[0], CAP_EFFECTIVE, &flag) == 0
                   && flag == CAP_SET));

out:
    cap_free(caps);
    return ret;
}

// Drop CAP_NET_ADMIN to permitted if effective
bool drop_priv()
{
    cap_value_t cap_list[1] = { CAP_NET_ADMIN };
    auto caps = cap_get_proc();
    if(caps == NULL) {
        perror("cap_get_proc");
        return false;
    }

    const auto ret = cap_set_if_not(caps, CAP_CLEAR, cap_list, 1);

#ifndef NDEBUG
    cap_flag_value_t flag;
    assert(!ret || (cap_get_flag(caps, cap_list[0], CAP_PERMITTED, &flag) == 0
                    && flag == CAP_SET
                    && cap_get_flag(caps, cap_list[0], CAP_EFFECTIVE, &flag) == 0
                    && flag == CAP_CLEAR));
#endif

    cap_free(caps);
    return ret;
}

struct fork_handler_t {
    fork_handler_t()
        : fd(-1), total(0), ok(0)
    {
        if(!get_priv())
            return;
        fd = socket(PF_NETLINK, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
                    NETLINK_CONNECTOR);
        if(fd == -1) {
            perror("socket");
            return;
        }

        addr.nl_family = AF_NETLINK;
        addr.nl_pid = getpid();
        addr.nl_groups = CN_IDX_PROC;

        auto err = bind(fd, (struct sockaddr *)&addr, sizeof addr);
        if(err == -1) {
            perror("bind");
            close(fd);
            fd = -1;
            return;
        }

        filter(fd);

        { // send subscription message
            char nlmsghdrbuf[NLMSG_LENGTH(0)];
            nlmsghdr *nlmsghdr = reinterpret_cast<struct nlmsghdr *>(nlmsghdrbuf);

            nlmsghdr->nlmsg_len = NLMSG_LENGTH(sizeof(cn_msg) + sizeof(proc_cn_mcast_op));
            nlmsghdr->nlmsg_type = NLMSG_DONE;
            nlmsghdr->nlmsg_flags = 0;
            nlmsghdr->nlmsg_seq = 0;
            nlmsghdr->nlmsg_pid = getpid();

            struct cn_msg cn_msg;
            enum proc_cn_mcast_op op = PROC_CN_MCAST_LISTEN;
            cn_msg.id.idx = CN_IDX_PROC;
            cn_msg.id.val = CN_VAL_PROC;
            cn_msg.seq = 0;
            cn_msg.ack = 0;
            cn_msg.len = sizeof op;

            struct iovec iov[3];
            iov[0].iov_base = nlmsghdrbuf;
            iov[0].iov_len = NLMSG_LENGTH(0);
            iov[1].iov_base = &cn_msg;
            iov[1].iov_len = sizeof cn_msg;
            iov[2].iov_base = &op;
            iov[2].iov_len = sizeof op;

            // TODO: loop if EINTR, poll to handle EAGAIN
            const auto tx = writev(fd, iov, 3);
            if(tx == -1) {
                perror("writev");
                close(fd);
                fd = -1;
                return;
            }
            // TODO: writev for datagram socket means all iovs or none?
            assert(size_t(tx) == (iov[0].iov_len + iov[1].iov_len + iov[2].iov_len));
            std::cerr << "subscription ok\n";
        }
        // If capabilites are dropped before sending the subscription
        // message leads to strange behavior: no messages will be
        // received until subscription message is sent once with
        // uid=0.
        drop_priv();
        buf.resize(getpagesize());
    }

    ~fork_handler_t()
    {
        if(fd != -1)
            close(fd);
    }

    bool is_ok() const { return fd != -1; }

    bool wait_for(size_t ms_timeout)
    {
        pollfd pfd[1];
        pfd[0].events = POLLIN;
        pfd[0].fd = fd;
        const auto mux = poll(pfd, 1, ms_timeout);
        if(mux == 0) {
            return false;
        } else if(mux == -1) {
            perror("poll");
            close(fd);
            fd = -1;
            return false;
        }
        assert(mux == 1);
        return true;
    }

    template<typename lambda_t>
    void try_rx(lambda_t cb)
    {
        struct msghdr msghdr;
        struct iovec iov[1];

        msghdr.msg_name = &addr;
        msghdr.msg_namelen = sizeof(addr);
        msghdr.msg_iov = iov;
        msghdr.msg_iovlen = 1;
        msghdr.msg_control = NULL;
        msghdr.msg_controllen = 0;
        msghdr.msg_flags = 0;

        iov[0].iov_base = buf.data();
        iov[0].iov_len = buf.size();

        // read: | nlmsghdr | cn_msg | proc_event ... |
        auto len = recvmsg(fd, &msghdr, 0);
        if(len == -1) {
            perror("recvmsg");
            return;
        }

        //std::cout << "have " << len << " bytes. pid=" << addr.nl_pid << "\n";
        // from kernel?
        if(addr.nl_pid != 0)
            return;
        for(nlmsghdr *nlhdr = (nlmsghdr *)buf.data(); NLMSG_OK(nlhdr, len);
            nlhdr = NLMSG_NEXT(nlhdr, len)) {
            total++;
            if((nlhdr->nlmsg_type == NLMSG_ERROR)
               || (nlhdr->nlmsg_type == NLMSG_NOOP)) {
                // filter out?
                assert(false);
                continue;
            }
            struct cn_msg *cn_msg = (struct cn_msg *)NLMSG_DATA(nlhdr);
            assert((cn_msg->id.idx == CN_IDX_PROC)
                   && (cn_msg->id.val == CN_VAL_PROC));
            ok++;
            std::cerr << "ok/total: " << ok << "/" << total << "\n";

            proc_event *ev = (struct proc_event *)cn_msg->data;
            switch(ev->what) {
            case proc_event::PROC_EVENT_EXEC:
                // if pid != tgid -> new thread, else new process -> should be filtered out
                std::cerr << "EXEC pid: " << ev->event_data.exec.process_pid
                          << " tgid:" << ev->event_data.exec.process_tgid << "\n";
                cb(ev->event_data.exec.process_pid, ev->event_data.exec.process_tgid);
                break;
            default:
                std::cerr << "OTHER " << ev->event_data.fork.parent_pid
                          << "/" << ev->event_data.fork.parent_tgid << "\n";
                // shouldnt occur with filter active
                assert(false);
                break;
            }
        }
    }

    int fd;
    size_t total;
    size_t ok;
    struct sockaddr_nl addr;
    std::vector<char> buf;
};

}

#endif

namespace {
const char *get_basename(const char *filename)
{
    const char *result = filename;
    while(*filename != '\0')
        if(*(filename++) == '/')
            result = filename;
    return result;
}

static bool check_pid(const std::string &name, pid_t pid)
{
//    std::cout << name << "/" << pid << "\n";
    std::string buff(512, '\0');
    {
        char path[256];
        std::snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
        std::ifstream fp(path);
        buff.resize(fp.read(&*buff.begin(), buff.length()).gcount());
    }
    const char * base = get_basename(buff.c_str());

    // oneshot
    if(std::strncmp(name.c_str(), base, name.length()) == 0) {
        std::cerr << "match: " << buff << "\n";
        std::cout << pid << "\n" << std::flush;
        return true;
    }

    std::cerr << "mismatch: " << buff << "\n";
    return false;
}

bool proc_iterate(const std::string &name)
{
    bool have = false;
    auto const path_handler = [&name, &have](const std::experimental::filesystem::path &path) {
        const auto pid = std::atoi(path.filename().c_str());
        if(pid == 0)
            return true;
        if(check_pid(name, pid)) {
            have = true;
            return false;
        }
        return false;
    };

    for(auto &it: std::experimental::filesystem::directory_iterator("/proc")) {
        std::error_code ec;
        if(!std::experimental::filesystem::is_directory(it, ec))
            continue;
        path_handler(it.path());
        if(have)
            break;
    }
    return have;
}

std::atomic_bool sigint;
void sighandler(int)
{
    sigint = true;
}
}

int main(int argc, char *argv[])
{
    if(argc != 2)
        return -1;

    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = sighandler;
    sa.sa_flags = SA_RESTART;
    if(sigaction(SIGINT, &sa, NULL) == -1)
        perror("sigaction");

    const std::string name(argv[1]);

#ifdef USE_PROC_CONN
    // subscribe for events
    fork_handler_t fork_notify;
    if(fork_notify.is_ok()) {
        // check existing processes first
        if(proc_iterate(name)) {
            std::cerr << "\n";
            return 0;
        }

        for(size_t i = 0; i < 200 && !sigint;) {
            if(!fork_notify.wait_for(100)) {
                std::cerr << "." << std::flush;
                if(!fork_notify.is_ok())
                    break;
                ++i;
                continue;
            }
            bool have = false;
            fork_notify.try_rx([&name, &have](pid_t pid, pid_t tgid) {
                    std::cerr << "pid/tgid: " << pid << "/" << tgid << "\n";
                    if(check_pid(name, pid))
                        have = true; });
            if(have)
                break;
        }
        std::cerr << "\n";
        return 0;
    }
#endif

    for(size_t i = 0; i < 200 && !sigint; i++) {
        if(proc_iterate(name))
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cerr << "." << std::flush;
    }
    std::cerr << "\n";
}
