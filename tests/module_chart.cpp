#include <boost/test/unit_test.hpp>
#include <boost/test/unit_test_suite.hpp>
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>

#include "echo_server.hpp"
#include "runner.hpp"
#include "async_client.hpp"

#include <memory>
#include <string>
#include <mutex>
#include <memory>
#include <chrono>
#include <iomanip>

namespace fs = boost::filesystem;
namespace test = tests;

template <typename T>
using Runner = test::Runner<T>;
using TestProto = meta::proto<test::SimpleClientMessage>;
using Client = protoserv::async_client<TestProto>;
using test::EchoServer;

struct AppModuleChartFixture
{
};

class Timer
{
public:
    explicit Timer(const std::string& name)
        : name_(name)
    {
        started_ = now();
    }

    ~Timer()
    {
        stopped_ = now();

        using std::chrono::duration_cast;
        using std::chrono::milliseconds;

        std::cout << "[" << name_ << "]: "
                  << duration_cast<milliseconds>(stopped_ - started_).count()
                  << " ms" << std::endl;
    }

private:
    using clock = std::chrono::high_resolution_clock;
    using time_point = clock::time_point;

    static time_point now()
    {
        return std::chrono::high_resolution_clock::now();
    }

    int iterations_ = 0;

    std::string name_;
    time_point started_;
    time_point stopped_;
};

class Bandwidth
{
public:
    void start()
    {
        stopped_ = started_ = now();
    }

    void stop()
    {
        stopped_ = now();
    }

    void iterate(int iter = 1)
    {
        iterations_ += iter;
    }

    auto took_us() const
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(stopped_ - started_).count();
    }

    double band() const
    {
        return static_cast<double>(iterations_) / took_us() * 1000 * 1000 * 1000;
    }

    Bandwidth()
    {
        start();
    }

    ~Bandwidth()
    {
        stop();

        std::cout << "Bandwidth: processed " << iterations_ <<
                  " messages over " << (took_us() / 1000 / 1000) << " ms" << std::endl;

        std::cout << "Bandwidth: " << band() << " msg/sec" << std::endl;
    }

private:
    using clock = std::chrono::high_resolution_clock;
    using time_point = clock::time_point;

    static time_point now()
    {
        return std::chrono::high_resolution_clock::now();
    }

    int iterations_ = 0;

    time_point started_;
    time_point stopped_;
};


BOOST_FIXTURE_TEST_SUITE(AppModuleChart, AppModuleChartFixture, *boost::unit_test::disabled())

class AsioClient
{
public:
    AsioClient()
        : resolver_(service_)
    {
    }

    void disconnect()
    {
        connections_.clear();
    }

    using tcp = boost::asio::ip::tcp;
    void connect(uint16_t port, int times)
    {
        auto ip = boost::asio::ip::address::from_string("127.0.0.1");
        auto endpoint = tcp::endpoint(ip, port);
        auto it = resolver_.resolve(endpoint);

        for (int i = 0; i < times; ++i)
        {
            tcp::socket socket(service_);
            socket.open(tcp::v4());
            socket.set_option(tcp::no_delay(true));
            socket.set_option(boost::asio::socket_base::linger(true, 0));
            socket.connect(endpoint);
            connections_.emplace_back(std::move(socket), *this);
        }
    }

    template <typename T>
    void send(int messageId, const T& msg)
    {
        auto prevSize = write_buffer_.size();
        int size = msg.ByteSize();
        write_buffer_.resize(prevSize + size + 4);
        auto head = reinterpret_cast<uint16_t*>(&write_buffer_[prevSize]);
        head[0] = size + 4;
        head[1] = messageId;
        msg.SerializeToArray(&write_buffer_[prevSize + 4], size);
        ++write_buffer_count_;
    }

    void prepare(int times)
    {
        int batchSize = 0;
        std::vector<uint8_t> batch;
        do
        {
            batch.insert(batch.end(), write_buffer_.begin(), write_buffer_.end());
            batchSize += 1;
        }
        while (batch.size() < 2048 && batchSize * write_buffer_count_ < times);

        write_buffer_count_ *= batchSize;
        write_buffer_ = std::move(batch);
        for (auto& c : connections_)
        {
            c.transact(times, write_buffer_, write_buffer_count_);
        }
    }

    void transact()
    {
        service_.run();
    }

    class Connection
    {
    public:
        Connection(tcp::socket&& socket, AsioClient& client)
            : socket_(std::move(socket))
            , client_(client)
        {
        }

        ~Connection()
        {
        }

        void transact(int times, const std::vector<uint8_t>& writeBuffer, int batchSize)
        {
            left_to_send_ = times / batchSize;
            left_to_recv_ = left_to_send_ * batchSize;
            write_buffer_ = &writeBuffer;
            readbuf_.reserve(writeBuffer.size());
            do_write();
            do_read();
        }

        void do_write()
        {
            auto buf = &(*write_buffer_)[0];
            auto sz = write_buffer_->size();
            boost::asio::async_write(socket_, boost::asio::buffer(buf, sz),
                                     [this](boost::system::error_code ec, size_t byteWritten)
            {
                if (!ec)
                {
                    --left_to_send_;
                    if (left_to_send_ > 0)
                    {
                        do_write();
                    }
                }
            });
        }

        void do_read()
        {
            socket_.async_read_some(boost::asio::buffer(readbuf_.end(), readbuf_.free_capacity()),
                                    [this](boost::system::error_code ec, size_t bytesRead)
            {
                if (!ec)
                {
                    readbuf_.grow(bytesRead);

                    auto buf = readbuf_.begin();
                    auto beg = buf;
                    auto end = readbuf_.end();

                    while (end - buf >= 4)
                    {
                        auto messageSize = *(reinterpret_cast<uint16_t*>(buf));

                        if (messageSize <= end - buf)
                        {
                            buf += messageSize;
                            --left_to_recv_;
                        }
                        else
                        {
                            break;
                        }
                    }

                    if (buf != beg)
                    {
                        readbuf_.erase(buf - beg);
                    }

                    if (left_to_recv_ > 0)
                    {
                        do_read();
                    }
                }
            });
        }


    private:
        protoserv::dyn_buffer readbuf_;
        const std::vector<uint8_t>* write_buffer_ = nullptr;
        tcp::socket socket_;
        AsioClient& client_;
        int left_to_send_ = 0;
        int left_to_recv_ = 0;
    };

private:
    std::vector<uint8_t> write_buffer_;
    int write_buffer_count_ = 0;
    boost::asio::io_service service_;

    tcp::resolver resolver_;

    std::list<Connection> connections_;
};

double bench_concurrent_clients(int messageCount, int clientCount, int messageSize)
{
    Runner<EchoServer> server;
    server.run_in_background(5999);

    AsioClient client;

    {
        Timer timer("timer async connect");
        client.connect(5999, clientCount);
    }

    int msg_per_client = messageCount / clientCount;


    test::SimpleClientMessage inmsg;
    std::vector<char> payload(messageSize, 'X');
    inmsg.set_payload(&payload[0], payload.size());

    server->send_message(client, inmsg);
    client.prepare(msg_per_client);

    double ret = 0;
    {
        Bandwidth band;
        client.transact();
        band.iterate(msg_per_client * clientCount);
        band.stop();
        ret = band.band();
    }

    client.disconnect();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return ret;
}

double bench_concurrent_clients_iocp(int messageCount, int clientCount, int messageSize)
{
    Runner<EchoServer> server;
    server.run_in_background(5999);

    std::vector<std::shared_ptr<Client>> clients;
    clients.reserve(clientCount);
    {
        Timer timer("timer async connect");
        for (int i = 0; i < clientCount; ++i)
        {
            clients[i].reset(new Client);
            clients[i]->connect(5999);
        }
    }

    int msg_per_client = messageCount / clientCount;
    int left_to_send = msg_per_client;
    int left_to_recv = msg_per_client * clientCount;
    test::SimpleClientMessage inmsg;
    std::vector<char> payload(messageSize, 'X');
    inmsg.set_payload(&payload[0], payload.size());

    Bandwidth band;
    for (int i = 0; i < clientCount; ++i)
    {
        server->send_message(*clients[i], inmsg);
    }
    --left_to_send;
    {
        do
        {
            if (left_to_send > 0)
            {
                left_to_send--;
                for (int i = 0; i < clientCount; ++i)
                {
                    clients[i]->send(inmsg);
                }
            }

            int clientid = 0;
            for (int i = 0; i < clientCount; ++i)
            {
                if (clients[i]->try_receive(inmsg))
                {
                    left_to_recv--;
                    band.iterate();
                }
            }

        }
        while (left_to_recv > 0);
    }

    band.stop();
    return band.band();
}

std::pair<double, double> bench_random_messages(int messageCount, int clientCount, int maxMessageSize)
{
    Runner<EchoServer> server;
    server.run_in_background(5999);

    AsioClient client;

    {
        Timer timer("timer async connect");
        client.connect(5999, clientCount);
    }

    int msg_per_client = messageCount / clientCount;


    test::SimpleClientMessage inmsg;
    srand(static_cast<unsigned int>(time(nullptr)));
    double mb_per_client = 0;
    for (int i = 0; i < msg_per_client; ++i)
    {
        std::vector<char> payload((rand() % (maxMessageSize - 1) + 1), 'X');
        inmsg.set_payload(&payload[0], payload.size());

        server->send_message(client, inmsg);
        mb_per_client += inmsg.ByteSize() / 1024. / 1024.;
    }

    client.prepare(msg_per_client);

    double msg_per_sec = 0;
    double mb_per_sec = 0;
    {
        Bandwidth band;
        client.transact();
        band.iterate(msg_per_client * clientCount);
        band.stop();
        msg_per_sec = band.band();

        double sec = band.took_us() / 1000. / 1000. / 1000.;
        mb_per_sec = mb_per_client * clientCount / sec;
    }

    client.disconnect();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return { msg_per_sec, mb_per_sec };
}



void draw_chart(const std::map<int, double>& perfdata, std::string x_annotation, std::string y_annotation)
{
    double maxPerf = 0;
    for (auto& p : perfdata)
    {
        if (p.second > maxPerf)
        {
            maxPerf = p.second;
        }
    }


    constexpr auto height = 20;
    constexpr auto width = 4;

    auto numColumns = perfdata.size();
    auto lineWidth = width * numColumns;

    std::stringstream ss;
    ss << "max " << maxPerf << " " << x_annotation;
    auto header = ss.str();
    header.append(lineWidth > header.size() ? lineWidth - header.size() : 0, '-');
    std::cout << "\n\n" << header << std::endl;

    double step = maxPerf / height;

    for (int i = 0; i < height; ++i)
    {
        double threshold = maxPerf - step * (i + 1);
        for (auto& p : perfdata)
        {
            std::cout << std::setw(width) << (p.second >= threshold ? '#' : ' ');
        }
        std::cout << std::endl;
    }

    for (auto& p : perfdata)
    {
        if (p.first < 1000)
        {
            std::cout << std::setw(width) << p.first;
        }
        else
            if (p.first < 1000 * 1000)
            {
                std::stringstream ss;
                auto k = p.first / 1000;
                if (k > 100)
                {
                    ss << "." << (k / 100) << "m";
                }
                else
                {
                    ss << k << "k";
                }
                std::cout << std::setw(width) << ss.str();
            }
            else
            {
                std::stringstream ss;
                ss << (p.first / 1000 / 1000) << "m";
                std::cout << std::setw(4) << ss.str();
            }
    }

    auto footer = y_annotation;
    footer.append(lineWidth > footer.size() ? lineWidth - footer.size() : 0, '-');
    std::cout << "\n" << footer << "\n\n";
}

BOOST_AUTO_TEST_CASE(short_concurrent_client_sessions_single_threaded)
{
    constexpr int clients = 1;
    bench_concurrent_clients(clients * 100, clients, 8);
}

BOOST_AUTO_TEST_CASE(performance_chart)
{
    std::map<int, double> perfdata;

    int messageSize = 8;
    int clients = 1;
    do
    {
        perfdata[clients] = bench_concurrent_clients(100 * clients, clients, messageSize);

        if (clients > 1024)
        {
            clients += 1024;
        }
        else
        {
            clients *= 2;
        }
    }
    while (clients <= 8 * 1024);

    draw_chart(perfdata, "msg/sec", "num clients");

    for (auto& p : perfdata)
    {
        p.second *= messageSize;
        p.second /= 1024 * 1024;
    }

    draw_chart(perfdata, "mb/sec", "num clients");

}

BOOST_AUTO_TEST_CASE(message_size_performance_chart)
{
    std::map<int, double> perfdata;

    int messageSize = 1;
    int clients = 1;
    do
    {
        auto messages = 3000000 / messageSize;
        perfdata[messageSize] = bench_concurrent_clients(messages, clients, messageSize);

        if (false && messageSize > 1024)
        {
            messageSize += 1024;
        }
        else
        {
            messageSize *= 2;
        }
    }
    while (messageSize <= 32 * 1024);

    draw_chart(perfdata, "msg/sec", "message size (bytes)");

    for (auto& p : perfdata)
    {
        p.second *= p.first;
        p.second /= 1024 * 1024;
    }

    draw_chart(perfdata, "mb/sec", "message size (bytes)");
}

BOOST_AUTO_TEST_CASE(single_session_performance_chart)
{
    std::map<int, double> perfdata;

    int messageCount = 128;
    int messageSize = 64;
    int clientCount = 1;
    do
    {
        auto totalMessages = messageCount * clientCount;
        perfdata[messageCount] = bench_concurrent_clients(totalMessages, clientCount, messageSize);
        messageCount *= 2;
    }
    while (messageCount <= 4 * 1024 * 1024);

    draw_chart(perfdata, "msg/sec", "num messages");

    for (auto& p : perfdata)
    {
        p.second *= messageSize;
        p.second /= 1024 * 1024;
    }

    draw_chart(perfdata, "mb/sec", "num messages");
}

BOOST_AUTO_TEST_CASE(random_messages_chart)
{
    std::map<int, double> msg_per_sec;
    std::map<int, double> mb_per_sec;

    int messageSize = 8;
    int clients = 1;
    do
    {
        auto res = bench_random_messages(100 * clients, clients, 128);
        msg_per_sec[clients] = res.first;
        mb_per_sec[clients] = res.second;

        if (clients < 1024)
        {
            clients *= 2;
        }
        else
        {
            clients += 1024;
        }
    }
    while (clients <= 4096);

    draw_chart(msg_per_sec, "msg/sec", "num clients");
    draw_chart(mb_per_sec, "mb/sec", "num clients");
}

BOOST_AUTO_TEST_SUITE_END()
