#ifdef WIN32
#define _WIN32_WINNT 0x0501
#include <stdio.h>
#endif


#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

#include "coroutine.hpp"
#include "yield.hpp"

using namespace boost::asio;
io_service service;


#define MEM_FN(x)       boost::bind(&self_type::x, shared_from_this())
#define MEM_FN1(x,y)    boost::bind(&self_type::x, shared_from_this(),y)
#define MEM_FN2(x,y,z)  boost::bind(&self_type::x, shared_from_this(),y,z)



/** simple connection to server:
    - logs in just with username (no password)
    - all connections are initiated by the client: client asks, server answers
    - server disconnects any client that hasn't pinged for 5 seconds

    Possible requests:
    - gets a list of all connected clients
    - ping: the server answers either with "ping ok" or "ping client_list_changed"
*/
class talk_to_svr : public boost::enable_shared_from_this<talk_to_svr>
                  , public coroutine, boost::noncopyable {
    typedef talk_to_svr self_type;
    talk_to_svr(const std::string & username) 
      : sock_(service), started_(false), username_(username), timer_(service) {}
    void start(ip::tcp::endpoint ep) {
        sock_.async_connect(ep, MEM_FN2(step,_1,0) );
    }
public:
    typedef boost::system::error_code error_code;
    typedef boost::shared_ptr<talk_to_svr> ptr;

    static ptr start(ip::tcp::endpoint ep, const std::string & username) {
        ptr new_(new talk_to_svr(username));
        new_->start(ep); // start ourselves
        return new_;
    }
    void step(const error_code & err = error_code(), size_t bytes = 0) {
        reenter(this) {
            for (;;) {
                if ( !started_) {
                    started_ = true;
                    std::ostream out(&write_buffer_);
                    out << "login " << username_ << "\n";
                }
                yield async_write(sock_, write_buffer_, MEM_FN2(step,_1,_2) );
                yield async_read_until( sock_, read_buffer_, "\n", MEM_FN2(step,_1,_2));
                yield service.post( MEM_FN(on_answer_from_server));
            }
        }
    }
private:
    void on_answer_from_server() {
        std::istream in(&read_buffer_);
        std::string word; in >> word;
        if ( word == "login") on_login();
        else if ( word == "ping") on_ping();
        else if ( word == "clients") on_clients();
        else std::cerr << "invalid msg " << std::endl;
        read_buffer_.consume( read_buffer_.size());
        if ( write_buffer_.size() > 0)
            service.post( MEM_FN2(step,error_code(),0));
    }

    void on_login() {
        std::cout << username_ << " logged in" << std::endl;
        do_ask_clients();
    }
    void on_ping() {
        std::istream in(&read_buffer_);
        std::string answer; 
        in >> answer;
        if ( answer == "client_list_changed") do_ask_clients();
        else postpone_ping();
    }
    void on_clients() {
        std::ostringstream clients;
        clients << &read_buffer_;
        std::cout << username_ << ", new client list:" << clients.str();
        postpone_ping();
    }

    void do_ping() {
        std::ostream out(&write_buffer_); out << "ping\n";
        service.post( MEM_FN2(step,error_code(),0));
    }
    void postpone_ping() {
        // note: even though the server wants a ping every 5 secs, we randomly 
        // don't ping that fast - so that the server will randomly disconnect us
        int millis = rand() % 3000;
        std::cout << username_ << " postponing ping " << millis 
                  << " millis" << std::endl;
        timer_.expires_from_now(boost::posix_time::millisec(millis));
        timer_.async_wait( MEM_FN(do_ping));
    }
    void do_ask_clients() {
        std::ostream out(&write_buffer_); out << "ask_clients\n";
    }

private:
    ip::tcp::socket sock_;
    streambuf read_buffer_, write_buffer_;
    bool started_;
    std::string username_;
    deadline_timer timer_;
};

int main(int argc, char* argv[]) {
    ip::tcp::endpoint ep( ip::address::from_string("127.0.0.1"), 8001);
    talk_to_svr::start(ep, "John");
    talk_to_svr::start(ep, "Suzie");
    service.run();
}


