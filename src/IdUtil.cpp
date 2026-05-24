#include "fw/IdUtil.hpp"
#include "fw/Logger.hpp"
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/thread/tss.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>
#include <boost/chrono.hpp>
#include <sstream>
#include <stdexcept>
#include <cstring>
#include <cstdlib>
#include <vector>
#ifdef _WIN32
  #include <winsock2.h>
  #include <iphlpapi.h>
  #pragma comment(lib, "iphlpapi.lib")
#else
  #include <sys/ioctl.h>
  #include <net/if.h>
  #include <ifaddrs.h>
  #include <unistd.h>
  #include <netpacket/packet.h>
#endif

namespace alkaidlab {
namespace fw {

SnowflakeGenerator::SnowflakeGenerator(int64_t datacenterId, int64_t workerId)
    : m_datacenterId(datacenterId)
    , m_workerId(workerId)
    , m_sequence(0)
    , m_lastTimestamp(-1)
{
    if (datacenterId > MAX_DATACENTER_ID || datacenterId < 0) {
        throw std::invalid_argument("datacenterId must be between 0 and 31");
    }
    if (workerId > MAX_WORKER_ID || workerId < 0) {
        throw std::invalid_argument("workerId must be between 0 and 31");
    }
}

int64_t SnowflakeGenerator::currentTimeMillis() {
    // 获取最新的系统时间戳（毫秒）
    boost::chrono::milliseconds ms = boost::chrono::duration_cast<boost::chrono::milliseconds>(boost::chrono::system_clock::now().time_since_epoch());
    return ms.count();
}

int64_t SnowflakeGenerator::nextId() {
    int64_t timestamp = currentTimeMillis();    
    if (timestamp == m_lastTimestamp) {
        // 同一毫秒内，序列号递增
        m_sequence = (m_sequence + 1) & MAX_SEQUENCE;
        if (m_sequence == 0) {
            // 序列号溢出，等待下一毫秒
            // 加上最大等待次数，防止时钟回拨时死循环
            int waitCount = 0;
            const int MAX_WAIT_COUNT = 5; // 最多等待5次（约5ms）
            
            while (timestamp <= m_lastTimestamp && waitCount < MAX_WAIT_COUNT) {
                timestamp = currentTimeMillis();
                ++waitCount;
                
                // 检测时钟回拨
                if (timestamp < m_lastTimestamp) {
                    LOG_ERROR("Snowflake clock moved backwards: last=" + std::to_string(m_lastTimestamp) 
                              + "ms, current=" + std::to_string(timestamp) + "ms");
                    return 0;
                }
            }
            
            // 等待超时
            if (timestamp <= m_lastTimestamp) {
                LOG_ERROR("Snowflake sequence overflow timeout after " + std::to_string(MAX_WAIT_COUNT) + " attempts");
                return 0;
            }
        }
    } else {
        // 不同毫秒，序列号重置
        m_sequence = 0;
    }
    
    m_lastTimestamp = timestamp;
    
    // 组装ID
    return ((timestamp - EPOCH) << TIMESTAMP_SHIFT)
         | (m_datacenterId << DATACENTER_ID_SHIFT)
         | (m_workerId << WORKER_ID_SHIFT)
         | m_sequence;
}

std::string SnowflakeGenerator::nextIdStr() {
    std::ostringstream oss;
    oss << nextId();
    return oss.str();
}

// ============================================================================
// IdUtil 实现
// ============================================================================

// ============================================================================
// 辅助函数：获取本机 MAC 地址
// ============================================================================
namespace {
    /**
     * 获取主机名+MAC地址混合哈希值（用于计算数据中心ID）
     * @return 混合哈希值
     */
    int64_t getHostMacHash() {
        // 1. 获取主机名
        char hostname[256] = {0};
        gethostname(hostname, sizeof(hostname));
        
        // 2. 获取MAC地址
        unsigned char macAddr[6] = {0};
        bool macFound = false;

#ifdef _WIN32
        {
            ULONG bufLen = 16 * 1024;
            std::vector<unsigned char> buf(bufLen);
            ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
            DWORD ret = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr,
                                             reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()),
                                             &bufLen);
            if (ret == ERROR_BUFFER_OVERFLOW) {
                buf.resize(bufLen);
                ret = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr,
                                           reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()),
                                           &bufLen);
            }
            if (ret == NO_ERROR) {
                for (IP_ADAPTER_ADDRESSES* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
                     a != nullptr; a = a->Next) {
                    if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
                    if (a->PhysicalAddressLength != 6) continue;
                    bool allZero = true;
                    for (int i = 0; i < 6; ++i) {
                        if (a->PhysicalAddress[i] != 0) { allZero = false; break; }
                    }
                    if (allZero) continue;
                    memcpy(macAddr, a->PhysicalAddress, 6);
                    macFound = true;
                    break;
                }
            }
        }
#else
        struct ifaddrs* ifap = nullptr;
        struct ifaddrs* ifa = nullptr;
        if (getifaddrs(&ifap) == 0) {
            for (ifa = ifap; ifa != nullptr; ifa = ifa->ifa_next) {
                if (ifa->ifa_addr == nullptr) {
                    continue;
                }
                
                // 查找 AF_PACKET 类型的接口（包含MAC地址）
                if (ifa->ifa_addr->sa_family == AF_PACKET) {
                    struct sockaddr_ll* s = (struct sockaddr_ll*)(ifa->ifa_addr);
                    
                    // 跳过回环接口
                    if (strcmp(ifa->ifa_name, "lo") == 0) {
                        continue;
                    }
                    
                    // 检查是否有有效的 MAC 地址（不全为0）
                    bool allZero = true;
                    for (int i = 0; i < 6; ++i) {
                        if (s->sll_addr[i] != 0) {
                            allZero = false;
                            break;
                        }
                    }
                    
                    if (!allZero && s->sll_halen == 6) {
                        memcpy(macAddr, s->sll_addr, 6);
                        macFound = true;
                        break;
                    }
                }
            }
            freeifaddrs(ifap);
        }
#endif
        
        // 3. 计算混合哈希：主机名 + MAC 地址
        size_t hash = 0;
        
        // 主机名部分
        for (size_t i = 0; hostname[i] != '\0' && i < sizeof(hostname); ++i) {
            hash = hash * 31 + static_cast<size_t>(hostname[i]);
        }
        
        // MAC 地址部分
        if (macFound) {
            for (int i = 0; i < 6; ++i) {
                hash = hash * 256 + static_cast<size_t>(macAddr[i]);
            }
        }
        
        return static_cast<int64_t>(hash);
    }
} // namespace

// UUID 生成器（线程本地存储）
std::string IdUtil::generateV4() {
    static boost::thread_specific_ptr<boost::uuids::random_generator> generator;
    if (!generator.get()) {
        generator.reset(new boost::uuids::random_generator());
    }
    return boost::uuids::to_string((*generator)());
}

std::string IdUtil::generateTimedV4() {
    int64_t us = boost::chrono::duration_cast<boost::chrono::microseconds>(
        boost::chrono::system_clock::now().time_since_epoch()).count();
    std::ostringstream oss;
    oss << us << "-" << generateV4();
    return oss.str();
}

// 雪花算法全局配置
static int64_t s_customDatacenterId = -1;  // 自定义数据中心ID（-1表示未设置，使用MAC地址）
static int64_t s_customWorkerId = -1;      // 自定义机器ID（-1表示未设置，使用线程ID）
static boost::mutex s_snowflakeMutex;

void IdUtil::initSnowflake(int64_t datacenterId, int64_t workerId) {
    boost::lock_guard<boost::mutex> lock(s_snowflakeMutex);
    s_customDatacenterId = datacenterId;
    s_customWorkerId = workerId;
}

// 雪花算法生成器（线程本地存储）
static boost::thread_specific_ptr<SnowflakeGenerator> s_snowflakeGenerator;

int64_t IdUtil::generateSnowflakeId() {
    if (!s_snowflakeGenerator.get()) {
        boost::lock_guard<boost::mutex> lock(s_snowflakeMutex);
        
        // 1. 计算数据中心ID（使用自定义值或主机名+MAC混合）
        int64_t datacenterId;
        if (s_customDatacenterId >= 0) {
            datacenterId = s_customDatacenterId % 32;
        } else {
            // 从主机名+MAC地址混合计算数据中心ID
            datacenterId = getHostMacHash() % 32;
        }
        
        // 2. 计算机器ID（使用自定义值或线程ID）
        int64_t workerId;
        if (s_customWorkerId >= 0) {
            workerId = s_customWorkerId % 32;
        } else {
            // 从线程 ID 计算机器ID
            boost::thread::id tid = boost::this_thread::get_id();
            std::ostringstream oss;
            oss << tid;
            std::string tidStr = oss.str();
            
            size_t hash = 0;
            for (size_t i = 0; i < tidStr.length(); ++i) {
                hash = hash * 31 + static_cast<size_t>(tidStr[i]);
            }
            workerId = static_cast<int64_t>(hash % 32);
        }
        
        s_snowflakeGenerator.reset(new SnowflakeGenerator(datacenterId, workerId));
    }
    return s_snowflakeGenerator->nextId();
}

std::string IdUtil::generateSnowflakeIdStr() {
    std::ostringstream oss;
    oss << generateSnowflakeId();
    return oss.str();
}

} // namespace fw
} // namespace alkaidlab
