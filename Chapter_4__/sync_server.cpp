#ifdef WIN32
#define _WIN32_WINNT 0x0501
#include <stdio.h>
#endif


#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
using namespace boost::asio;
using namespace boost::posix_time;
io_service service;

struct talk_to_client;
typedef boost::shared_ptr<talk_to_client> client_ptr;
typedef std::vector<client_ptr> array;
array clients;
// thread-safe access to clients array
boost::recursive_mutex cs;

void update_clients_changed() ;

/** simple connection to server:
    - logs in just with username (no password)
    - all connections are initiated by the client: client asks, server answers
    - server disconnects any client that hasn't pinged for 5 seconds

    Possible requests:
    - gets a list of all connected clients
    - ping: the server answers either with "ping ok" or "ping client_list_changed"
*/
struct talk_to_client : boost::enable_shared_from_this<talk_to_client> {
    talk_to_client() 
        : sock_(service), started_(false), already_read_(0) {
        last_ping = microsec_clock::local_time();
    }
    std::string username() const { return username_; }

    void answer_to_client() {
        try {
            read_request();
            process_request();
        } catch ( boost::system::system_error&) {
            stop();
        }
        if ( timed_out()) {
            stop();
            std::cout << "stopping " << username_ << " - no ping in time" << std::endl;
        }
    }
    void set_clients_changed() { clients_changed_ = true; }
    ip::tcp::socket & sock() { return sock_; }
    bool timed_out() const {
        ptime now = microsec_clock::local_time();
        long long ms = (now - last_ping).total_milliseconds();
        return ms > 5000 ;
    }
    void stop() {
        // close client connection
        boost::system::error_code err;
        sock_.close(err);
    }
private:
    void read_request() {
        if ( sock_.available())
            already_read_ += sock_.read_some(
                buffer(buff_ + already_read_, max_msg - already_read_));
    }
    void process_request() {
        bool found_enter = std::find(buff_, buff_ + already_read_, '\n') 
                          < buff_ + already_read_;
        if ( !found_enter)
            return; // message is not full
        // process the msg
        last_ping = microsec_clock::local_time();
        size_t pos = std::find(buff_, buff_ + already_read_, '\n') - buff_;
        std::string msg(buff_, pos);
        std::copy(buff_ + already_read_, buff_ + max_msg, buff_);
        already_read_ -= pos + 1;

        if ( msg.find("login ") == 0) on_login(msg);
        else if ( msg.find("ping") == 0) on_ping();
        else if ( msg.find("ask_clients") == 0) on_clients();
        else std::cerr << "invalid msg " << msg << std::endl;
    }
    
    void on_login(const std::string & msg) {
        std::istringstream in(msg);
        in >> username_ >> username_;
        std::cout << username_ << " logged in" << std::endl;
        write("login ok\n");
        update_clients_changed();
    }
    void on_ping() {
        write(clients_changed_ ? "ping client_list_changed\n" : "ping ok\n");
        clients_changed_ = false;
    }
    void on_clients() {
        std::string msg;
        { boost::recursive_mutex::scoped_lock lk(cs);
          for( array::const_iterator b = clients.begin(), e = clients.end() ; b != e; ++b)
            msg += (*b)->username() + " ";
        }
        write("clients " + msg + "\n");
    }


    void write(const std::string & msg) {
        sock_.write_some(buffer(msg));
    }
private:
    ip::tcp::socket sock_;
    enum { max_msg = 1024 };
    int already_read_;
    char buff_[max_msg];
    bool started_;
    std::string username_;
    bool clients_changed_;
    ptime last_ping;
};

void update_clients_changed() {
    boost::recursive_mutex::scoped_lock lk(cs);
    for( array::iterator b = clients.begin(), e = clients.end(); b != e; ++b)
        (*b)->set_clients_changed();
}



void accept_thread() {
    ip::tcp::acceptor acceptor(service, ip::tcp::endpoint(ip::tcp::v4(), 8001));
    while ( true) {
        client_ptr new_( new talk_to_client);
        acceptor.accept(new_->sock());
        
        boost::recursive_mutex::scoped_lock lk(cs);
        clients.push_back(new_);
    }
}

void handle_clients_thread() {
    while ( true) {
        boost::this_thread::sleep( millisec(1));
        boost::recursive_mutex::scoped_lock lk(cs);
        for ( array::iterator b = clients.begin(), e = clients.end(); b != e; ++b) 
            (*b)->answer_to_client();
        // erase clients that timed out
        clients.erase(std::remove_if(clients.begin(), clients.end(), 
                   boost::bind(&talk_to_client::timed_out,_1)), clients.end());
    }
}

int main(int argc, char* argv[]) {
    boost::thread_group threads;
    threads.create_thread(accept_thread);
    threads.create_thread(handle_clients_thread);
    threads.join_all();
}










